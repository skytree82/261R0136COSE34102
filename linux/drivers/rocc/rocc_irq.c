// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) "rocc-irq: " fmt

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/smp.h>
#include <linux/workqueue.h>
#include <linux/atomic.h>
#include <linux/nv_interceptor.h>

#include <asm/irq.h>

#ifndef PRINT_ENABLE
#define PRINT_ENABLE 0
#endif

#if PRINT_ENABLE
#define NV_PRINTK(...) MY_PRINTK(__VA_ARGS__)
#define NV_PR_INFO(...) pr_info(__VA_ARGS__)
#else
#define NV_PRINTK(...) do { } while (0)
#define NV_PR_INFO(...) do { } while (0)
#endif

/*
 * Assumption:
 *   Rocket rocc_interrupt arrives as local interrupt cause/hwirq 13.
 */
#define ROCC_LOCAL_HWIIRQ 13

/*
 * Minimal RoCC instruction helper.
 * Replace XCUSTOM_ACC / k_FLUSH values if your tree defines them elsewhere.
 */
#ifndef XCUSTOM_ACC
#define XCUSTOM_ACC 3
#endif

#ifndef k_CLEAR
#define k_CLEAR 30
#endif

#define STR1(x) #x
#define STR(x) STR1(x)
#define CAT_(A, B) A##B
#define CAT(A, B) CAT_(A, B)

/* CUSTOM opcode with xd=0, xs1=1, xs2=1 */
#define ROCC_INSTRUCTION_0_R_R(x, rs1, rs2, func7)                           \
	do {                                                                 \
		asm volatile(                                                \
			".insn r " STR(CAT(CUSTOM_, x)) ", " STR(0x3) ", "   \
			STR(func7) ", x0, %0, %1"                             \
			:                                                      \
			: "r"(rs1), "r"(rs2));                                 \
	} while (0)

/* Use this to clear/deassert the RoCC interrupt source */
#define gemmini_clear(void) \
	ROCC_INSTRUCTION_0_R_R(XCUSTOM_ACC, 0, 0, k_CLEAR)

static DEFINE_PER_CPU(int, rocc_dummy_dev);
static int rocc_virq;
static atomic_t rocc_pending_completions = ATOMIC_INIT(0);
static void rocc_irq_workfn(struct work_struct *work);
static DECLARE_WORK(rocc_irq_work, rocc_irq_workfn);

static void rocc_irq_workfn(struct work_struct *work)
{
	int pending;
	bool need_kick;
	// bool was_npu_full;
	int index;
	model_ctx_t *model_ctx;

	pending = atomic_xchg(&rocc_pending_completions, 0);
	if (pending <= 0)
		//break;
		return;

	mutex_lock(&nv_sched_lock);
	// was_npu_full = READ_ONCE(npu_queue_info.count) >= NUM_MAX_GEMMINI_QUEUE;

	while (pending-- > 0) {
		index = dequeue_nv_command();
		// TODO : pending 이 남아있을 경우엔 여기서 break 되면 다른 명령이 안되니, continue 로 바꾸는게 좋으려나
		if (index < 0) {
			NV_PRINTK(KERN_WARNING "RoCC completion worker found empty NPU queue; stopping drain\n");
			// printk(KERN_WARNING "RoCC completion worker found empty NPU queue; stopping drain\n");
			// break;
			continue;
		}

		if (index >= NUM_MAX_MODELS)
			continue;

		model_ctx = &model_table.running_models[index];

		if (!READ_ONCE(model_ctx->in_use))
			continue;

		if (model_ctx->in_flight_ops <= 0)
			continue;

		model_ctx->in_flight_ops--;
		if (model_ctx->in_flight_ops == 0)
			nv_release_issue_owner(index, DEV_NPU);

	}

	// NPU 큐가 full 혹인 VPU inflight op > 0 이어서 발행이 안 되었다면, 스케줄러 깨워서 다음 명령이 실행될 수 있도록 한다.
	// need_kick = READ_ONCE(nv_retry_kick_for_npu) || READ_ONCE(nv_retry_kick_for_vpu);

	// if (need_kick) {
	// 	MY_PRINTK("IRQ kick sched\n");
	// 	nv_kick_scheduler();
	// }
	nv_kick_scheduler();
	mutex_unlock(&nv_sched_lock);

	// MY_PRINTK("UL\n");

	// for 문 없앤 대신, 혹시 도중에 새로 들어온 명령이 있을 수 있으니 한번 더 체크해서 워크 재스케줄링
	if (atomic_read(&rocc_pending_completions) > 0) {
		MY_PRINTK("New ISR\n");
		schedule_work(&rocc_irq_work);
	}

	// MY_PRINTK("Exit workfn\n");
}

/*
 * Minimal ISR:
 *   - clear/deassert the source by issuing gemmini_clear()
 *   - return handled
 *
 * If your hardware needs skip=1 semantics instead, change it here.
 */
static irqreturn_t rocc_interrupt_handler(int irq, void *dev_id)
{
	// MY_PRINTK("Before gemmini_clear\n");
	// printk("RoCC interrupt\n");
	atomic_inc(&rocc_pending_completions);
	schedule_work(&rocc_irq_work);
	gemmini_clear();
	return IRQ_HANDLED;
}

static int __init rocc_irq_init(void)
{
	struct irq_domain *domain;
	struct fwnode_handle *intc_fwnode;
	int ret;

	intc_fwnode = riscv_get_intc_hwnode();
	if (!intc_fwnode) {
		pr_err("failed to get RISC-V INTC fwnode\n");
		return -ENODEV;
	}

	domain = irq_find_matching_fwnode(intc_fwnode, DOMAIN_BUS_ANY);
	if (!domain) {
		pr_err("failed to find RISC-V INTC irq_domain\n");
		return -ENODEV;
	}

	/* hwirq 13 -> virq mapping */
	rocc_virq = irq_create_mapping(domain, ROCC_LOCAL_HWIIRQ);
	if (!rocc_virq) {
		pr_err("failed to map hwirq %d\n", ROCC_LOCAL_HWIIRQ);
		return -ENODEV;
	}

	/* register actual action->handler */
	ret = request_percpu_irq(rocc_virq, rocc_interrupt_handler,
				 "rocc-local", &rocc_dummy_dev);
	if (ret) {
		pr_err("request_percpu_irq failed for virq %d: %d\n",
		       rocc_virq, ret);
		irq_dispose_mapping(rocc_virq);
		rocc_virq = 0;
		return ret;
	}

	/* enable on current CPU */
	enable_percpu_irq(rocc_virq, irq_get_trigger_type(rocc_virq));

    unsigned long sie_val, sip_val;
    asm volatile ("csrr %0, sie" : "=r"(sie_val));
    asm volatile ("csrr %0, sip" : "=r"(sip_val));

	return 0;
}

static void __exit rocc_irq_exit(void)
{
	if (!rocc_virq)
		return;

	cancel_work_sync(&rocc_irq_work);

	disable_percpu_irq(rocc_virq);
	free_percpu_irq(rocc_virq, &rocc_dummy_dev);
	irq_dispose_mapping(rocc_virq);

	NV_PR_INFO("unregistered RoCC IRQ\n");
}

module_init(rocc_irq_init);
module_exit(rocc_irq_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Minimal Rocket RoCC local interrupt driver");

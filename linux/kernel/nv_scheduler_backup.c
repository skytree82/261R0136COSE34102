#include <linux/syscalls.h>
#include <linux/nv_queue.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <asm/mmu_context.h>

#define STR1(x) #x
#ifndef STR
#define STR(x) STR1(x)
#endif

#define __ROCC_INSN_CUSTOM_0(func7_imm, rs1_val, rs2_val) \
    asm volatile (".insn r CUSTOM_0, 0x3, " STR(func7_imm) ", x0, %0, %1" \
                 : : "r"(rs1_val), "r"(rs2_val))

#define __ROCC_INSN_CUSTOM_1(func7_imm, rs1_val, rs2_val) \
    asm volatile (".insn r CUSTOM_1, 0x3, " STR(func7_imm) ", x0, %0, %1" \
                 : : "r"(rs1_val), "r"(rs2_val))

#define __ROCC_INSN_CUSTOM_2(func7_imm, rs1_val, rs2_val) \
    asm volatile (".insn r CUSTOM_2, 0x3, " STR(func7_imm) ", x0, %0, %1" \
                 : : "r"(rs1_val), "r"(rs2_val))

#define __ROCC_INSN_CUSTOM_3(func7_imm, rs1_val, rs2_val) \
    asm volatile (".insn r CUSTOM_3, 0x3, " STR(func7_imm) ", x0, %0, %1" \
                 : : "r"(rs1_val), "r"(rs2_val))

static inline int gemmini_rocc_issue(uint8_t xcustom, uint8_t funct, uint64_t rs1, uint64_t rs2)
{
    switch (xcustom) {
    case 0:
        switch (funct) {
        case k_CONFIG: __ROCC_INSN_CUSTOM_0(k_CONFIG, rs1, rs2); return 0;
        case k_MVIN2: __ROCC_INSN_CUSTOM_0(k_MVIN2, rs1, rs2); return 0;
        case k_MVIN: __ROCC_INSN_CUSTOM_0(k_MVIN, rs1, rs2); return 0;
        case k_MVOUT: __ROCC_INSN_CUSTOM_0(k_MVOUT, rs1, rs2); return 0;
        case k_COMPUTE_PRELOADED: __ROCC_INSN_CUSTOM_0(k_COMPUTE_PRELOADED, rs1, rs2); return 0;
        case k_COMPUTE_ACCUMULATE: __ROCC_INSN_CUSTOM_0(k_COMPUTE_ACCUMULATE, rs1, rs2); return 0;
        case k_PRELOAD: __ROCC_INSN_CUSTOM_0(k_PRELOAD, rs1, rs2); return 0;
        case k_FLUSH: __ROCC_INSN_CUSTOM_0(k_FLUSH, rs1, rs2); return 0;
        case k_LOOP_WS: __ROCC_INSN_CUSTOM_0(k_LOOP_WS, rs1, rs2); return 0;
        case k_LOOP_WS_CONFIG_BOUNDS: __ROCC_INSN_CUSTOM_0(k_LOOP_WS_CONFIG_BOUNDS, rs1, rs2); return 0;
        case k_LOOP_WS_CONFIG_ADDRS_AB: __ROCC_INSN_CUSTOM_0(k_LOOP_WS_CONFIG_ADDRS_AB, rs1, rs2); return 0;
        case k_LOOP_WS_CONFIG_ADDRS_DC: __ROCC_INSN_CUSTOM_0(k_LOOP_WS_CONFIG_ADDRS_DC, rs1, rs2); return 0;
        case k_LOOP_WS_CONFIG_STRIDES_AB: __ROCC_INSN_CUSTOM_0(k_LOOP_WS_CONFIG_STRIDES_AB, rs1, rs2); return 0;
        case k_LOOP_WS_CONFIG_STRIDES_DC: __ROCC_INSN_CUSTOM_0(k_LOOP_WS_CONFIG_STRIDES_DC, rs1, rs2); return 0;
        case k_LOOP_CONV_WS: __ROCC_INSN_CUSTOM_0(k_LOOP_CONV_WS, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_1: __ROCC_INSN_CUSTOM_0(k_LOOP_CONV_WS_CONFIG_1, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_2: __ROCC_INSN_CUSTOM_0(k_LOOP_CONV_WS_CONFIG_2, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_3: __ROCC_INSN_CUSTOM_0(k_LOOP_CONV_WS_CONFIG_3, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_4: __ROCC_INSN_CUSTOM_0(k_LOOP_CONV_WS_CONFIG_4, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_5: __ROCC_INSN_CUSTOM_0(k_LOOP_CONV_WS_CONFIG_5, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_6: __ROCC_INSN_CUSTOM_0(k_LOOP_CONV_WS_CONFIG_6, rs1, rs2); return 0;
        case k_MVIN3: __ROCC_INSN_CUSTOM_0(k_MVIN3, rs1, rs2); return 0;
        case k_COUNTER: __ROCC_INSN_CUSTOM_0(k_COUNTER, rs1, rs2); return 0;
        default: return -EINVAL;
        }
    case 1:
        switch (funct) {
        case k_CONFIG: __ROCC_INSN_CUSTOM_1(k_CONFIG, rs1, rs2); return 0;
        case k_MVIN2: __ROCC_INSN_CUSTOM_1(k_MVIN2, rs1, rs2); return 0;
        case k_MVIN: __ROCC_INSN_CUSTOM_1(k_MVIN, rs1, rs2); return 0;
        case k_MVOUT: __ROCC_INSN_CUSTOM_1(k_MVOUT, rs1, rs2); return 0;
        case k_COMPUTE_PRELOADED: __ROCC_INSN_CUSTOM_1(k_COMPUTE_PRELOADED, rs1, rs2); return 0;
        case k_COMPUTE_ACCUMULATE: __ROCC_INSN_CUSTOM_1(k_COMPUTE_ACCUMULATE, rs1, rs2); return 0;
        case k_PRELOAD: __ROCC_INSN_CUSTOM_1(k_PRELOAD, rs1, rs2); return 0;
        case k_FLUSH: __ROCC_INSN_CUSTOM_1(k_FLUSH, rs1, rs2); return 0;
        case k_LOOP_WS: __ROCC_INSN_CUSTOM_1(k_LOOP_WS, rs1, rs2); return 0;
        case k_LOOP_WS_CONFIG_BOUNDS: __ROCC_INSN_CUSTOM_1(k_LOOP_WS_CONFIG_BOUNDS, rs1, rs2); return 0;
        case k_LOOP_WS_CONFIG_ADDRS_AB: __ROCC_INSN_CUSTOM_1(k_LOOP_WS_CONFIG_ADDRS_AB, rs1, rs2); return 0;
        case k_LOOP_WS_CONFIG_ADDRS_DC: __ROCC_INSN_CUSTOM_1(k_LOOP_WS_CONFIG_ADDRS_DC, rs1, rs2); return 0;
        case k_LOOP_WS_CONFIG_STRIDES_AB: __ROCC_INSN_CUSTOM_1(k_LOOP_WS_CONFIG_STRIDES_AB, rs1, rs2); return 0;
        case k_LOOP_WS_CONFIG_STRIDES_DC: __ROCC_INSN_CUSTOM_1(k_LOOP_WS_CONFIG_STRIDES_DC, rs1, rs2); return 0;
        case k_LOOP_CONV_WS: __ROCC_INSN_CUSTOM_1(k_LOOP_CONV_WS, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_1: __ROCC_INSN_CUSTOM_1(k_LOOP_CONV_WS_CONFIG_1, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_2: __ROCC_INSN_CUSTOM_1(k_LOOP_CONV_WS_CONFIG_2, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_3: __ROCC_INSN_CUSTOM_1(k_LOOP_CONV_WS_CONFIG_3, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_4: __ROCC_INSN_CUSTOM_1(k_LOOP_CONV_WS_CONFIG_4, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_5: __ROCC_INSN_CUSTOM_1(k_LOOP_CONV_WS_CONFIG_5, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_6: __ROCC_INSN_CUSTOM_1(k_LOOP_CONV_WS_CONFIG_6, rs1, rs2); return 0;
        case k_MVIN3: __ROCC_INSN_CUSTOM_1(k_MVIN3, rs1, rs2); return 0;
        case k_COUNTER: __ROCC_INSN_CUSTOM_1(k_COUNTER, rs1, rs2); return 0;
        default: return -EINVAL;
        }
    case 2:
        switch (funct) {
        case k_CONFIG: __ROCC_INSN_CUSTOM_2(k_CONFIG, rs1, rs2); return 0;
        case k_MVIN2: __ROCC_INSN_CUSTOM_2(k_MVIN2, rs1, rs2); return 0;
        case k_MVIN: __ROCC_INSN_CUSTOM_2(k_MVIN, rs1, rs2); return 0;
        case k_MVOUT: __ROCC_INSN_CUSTOM_2(k_MVOUT, rs1, rs2); return 0;
        case k_COMPUTE_PRELOADED: __ROCC_INSN_CUSTOM_2(k_COMPUTE_PRELOADED, rs1, rs2); return 0;
        case k_COMPUTE_ACCUMULATE: __ROCC_INSN_CUSTOM_2(k_COMPUTE_ACCUMULATE, rs1, rs2); return 0;
        case k_PRELOAD: __ROCC_INSN_CUSTOM_2(k_PRELOAD, rs1, rs2); return 0;
        case k_FLUSH: __ROCC_INSN_CUSTOM_2(k_FLUSH, rs1, rs2); return 0;
        case k_LOOP_WS: __ROCC_INSN_CUSTOM_2(k_LOOP_WS, rs1, rs2); return 0;
        case k_LOOP_WS_CONFIG_BOUNDS: __ROCC_INSN_CUSTOM_2(k_LOOP_WS_CONFIG_BOUNDS, rs1, rs2); return 0;
        case k_LOOP_WS_CONFIG_ADDRS_AB: __ROCC_INSN_CUSTOM_2(k_LOOP_WS_CONFIG_ADDRS_AB, rs1, rs2); return 0;
        case k_LOOP_WS_CONFIG_ADDRS_DC: __ROCC_INSN_CUSTOM_2(k_LOOP_WS_CONFIG_ADDRS_DC, rs1, rs2); return 0;
        case k_LOOP_WS_CONFIG_STRIDES_AB: __ROCC_INSN_CUSTOM_2(k_LOOP_WS_CONFIG_STRIDES_AB, rs1, rs2); return 0;
        case k_LOOP_WS_CONFIG_STRIDES_DC: __ROCC_INSN_CUSTOM_2(k_LOOP_WS_CONFIG_STRIDES_DC, rs1, rs2); return 0;
        case k_LOOP_CONV_WS: __ROCC_INSN_CUSTOM_2(k_LOOP_CONV_WS, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_1: __ROCC_INSN_CUSTOM_2(k_LOOP_CONV_WS_CONFIG_1, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_2: __ROCC_INSN_CUSTOM_2(k_LOOP_CONV_WS_CONFIG_2, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_3: __ROCC_INSN_CUSTOM_2(k_LOOP_CONV_WS_CONFIG_3, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_4: __ROCC_INSN_CUSTOM_2(k_LOOP_CONV_WS_CONFIG_4, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_5: __ROCC_INSN_CUSTOM_2(k_LOOP_CONV_WS_CONFIG_5, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_6: __ROCC_INSN_CUSTOM_2(k_LOOP_CONV_WS_CONFIG_6, rs1, rs2); return 0;
        case k_MVIN3: __ROCC_INSN_CUSTOM_2(k_MVIN3, rs1, rs2); return 0;
        case k_COUNTER: __ROCC_INSN_CUSTOM_2(k_COUNTER, rs1, rs2); return 0;
        default: return -EINVAL;
        }
    case 3:
        switch (funct) {
        case k_CONFIG: __ROCC_INSN_CUSTOM_3(k_CONFIG, rs1, rs2); return 0;
        case k_MVIN2: __ROCC_INSN_CUSTOM_3(k_MVIN2, rs1, rs2); return 0;
        case k_MVIN: __ROCC_INSN_CUSTOM_3(k_MVIN, rs1, rs2); return 0;
        case k_MVOUT: __ROCC_INSN_CUSTOM_3(k_MVOUT, rs1, rs2); return 0;
        case k_COMPUTE_PRELOADED: __ROCC_INSN_CUSTOM_3(k_COMPUTE_PRELOADED, rs1, rs2); return 0;
        case k_COMPUTE_ACCUMULATE: __ROCC_INSN_CUSTOM_3(k_COMPUTE_ACCUMULATE, rs1, rs2); return 0;
        case k_PRELOAD: __ROCC_INSN_CUSTOM_3(k_PRELOAD, rs1, rs2); return 0;
        case k_FLUSH: __ROCC_INSN_CUSTOM_3(k_FLUSH, rs1, rs2); return 0;
        case k_LOOP_WS: __ROCC_INSN_CUSTOM_3(k_LOOP_WS, rs1, rs2); return 0;
        case k_LOOP_WS_CONFIG_BOUNDS: __ROCC_INSN_CUSTOM_3(k_LOOP_WS_CONFIG_BOUNDS, rs1, rs2); return 0;
        case k_LOOP_WS_CONFIG_ADDRS_AB: __ROCC_INSN_CUSTOM_3(k_LOOP_WS_CONFIG_ADDRS_AB, rs1, rs2); return 0;
        case k_LOOP_WS_CONFIG_ADDRS_DC: __ROCC_INSN_CUSTOM_3(k_LOOP_WS_CONFIG_ADDRS_DC, rs1, rs2); return 0;
        case k_LOOP_WS_CONFIG_STRIDES_AB: __ROCC_INSN_CUSTOM_3(k_LOOP_WS_CONFIG_STRIDES_AB, rs1, rs2); return 0;
        case k_LOOP_WS_CONFIG_STRIDES_DC: __ROCC_INSN_CUSTOM_3(k_LOOP_WS_CONFIG_STRIDES_DC, rs1, rs2); return 0;
        case k_LOOP_CONV_WS: __ROCC_INSN_CUSTOM_3(k_LOOP_CONV_WS, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_1: __ROCC_INSN_CUSTOM_3(k_LOOP_CONV_WS_CONFIG_1, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_2: __ROCC_INSN_CUSTOM_3(k_LOOP_CONV_WS_CONFIG_2, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_3: __ROCC_INSN_CUSTOM_3(k_LOOP_CONV_WS_CONFIG_3, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_4: __ROCC_INSN_CUSTOM_3(k_LOOP_CONV_WS_CONFIG_4, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_5: __ROCC_INSN_CUSTOM_3(k_LOOP_CONV_WS_CONFIG_5, rs1, rs2); return 0;
        case k_LOOP_CONV_WS_CONFIG_6: __ROCC_INSN_CUSTOM_3(k_LOOP_CONV_WS_CONFIG_6, rs1, rs2); return 0;
        case k_MVIN3: __ROCC_INSN_CUSTOM_3(k_MVIN3, rs1, rs2); return 0;
        case k_COUNTER: __ROCC_INSN_CUSTOM_3(k_COUNTER, rs1, rs2); return 0;
        default: return -EINVAL;
        }
    default:
        return -EINVAL;
    }
}

static int consumer_thread(void *arg);
static int scheduler_init(void);
static int scheduler_exit(void);
static int consume_operation(void);
static gemmini_sched_entry_t *dequeue_operation(gemmini_sched_entry_t *queue);
fence_info_t fence_info;

struct task_struct *consumer;

spinlock_t gemmini_queue_lock;
wait_queue_head_t gemmini_inst_wait_queue;
wait_queue_head_t fence_queue;
volatile int gemmini_queue_initialized = 0;

// for linkedlist based queue
gemmini_sched_entry_t *op_queue_dequeue(op_queue_t *queue) {
    // 큐가 비어있는 경우 NULL 반환
    if (!queue->head) return NULL;

    op_node_t *node_to_remove = queue->head;
    gemmini_sched_entry_t *entry = &node_to_remove->op;

    queue->head = node_to_remove->next;
    if (queue->head) {
        queue->head->prev = NULL;
    } else {
        queue->tail = NULL;
    }
    kfree(node_to_remove);
    queue->count--;

    return entry;
}

static int scheduler_init(void)
{
    spin_lock_init(&gemmini_queue_lock);
    spin_lock_init(&fence_info.gemmini_fence_lock);
    init_waitqueue_head(&gemmini_inst_wait_queue);
    init_waitqueue_head(&fence_queue);
    gemmini_queue_reset();
    op_queue_init(&op_queue_1);
    
    gemmini_queue_initialized = 1;
    
    consumer = kthread_create(consumer_thread, NULL, "op_queue_consumer");
    if (IS_ERR(consumer)) 
    {
	    printk(KERN_ERR "Failed to create op_queue consumer thread.\n");
        
        consumer = NULL;
		return PTR_ERR(consumer);;
	}

    wake_up_process(consumer);
    printk(KERN_INFO "op_queue consumer thread started.\n");
    return 0;
}

void gemmini_queue_reset(void) {
    unsigned long flags;
    spin_lock_irqsave(&gemmini_queue_lock, flags);
    head_idx = 0;
    tail_idx = 0;
    count = 0;
    spin_unlock_irqrestore(&gemmini_queue_lock, flags);
}

static int scheduler_exit(void)
{
    wake_up_all(&gemmini_inst_wait_queue);
    wake_up_all(&fence_queue);
    kthread_stop(consumer);
    printk(KERN_INFO "op_queue consumer thread stopped.\n");
    consumer = NULL;
    
    return 0;
}


static int consumer_thread(void *arg)
{
    unsigned long fflags;

    while (!kthread_should_stop())
    {
        //wait_event_interruptible(gemmini_inst_wait_queue, !gemmini_queue_isEmpty());
        // msleep(2500);

        // printk(KERN_INFO "consumer thread running.\n");

        consume_operation();

        spin_lock_irqsave(&fence_info.gemmini_fence_lock, fflags);

        if (fence_info.fence_flag)
        {
            printk(KERN_DEBUG "Fence flag set, target count = %d.\n", fence_info.fence_target_count);
            
            fence_info.fence_target_count--;

            if (fence_info.fence_target_count <= 0)
            {
                fence_info.fence_flag = 0;
                printk(KERN_DEBUG "Operations before fence completed.\n");

                fence_info.fence_complete_flag = 1;
            }
        
        }

        spin_unlock_irqrestore(&fence_info.gemmini_fence_lock, fflags);

        wake_up(&fence_queue);
        
        if (gemmini_queue_isEmpty()) {
            printk(KERN_DEBUG "Operation queue is empty, consumer thread sleeping.\n");
            // TODO : Implement wait queue or condition variable to avoid busy waiting
            schedule();
            printk(KERN_DEBUG "Consumer thread woke up.\n");
        }

    }
    
    return 0;
}


static int consume_operation(void)
{
    gemmini_sched_entry_t *op = dequeue_operation(queue);

    // printk(KERN_INFO "[consume_operation] Dequeued.\n");
    if(op == NULL)
    {
        // printk(KERN_INFO "[consume_operation] NULL op dequeued.\n");
        return -1;
    }

    /* Issue RoCC inst under enqueueing task's mm */
    struct mm_struct *prev_mm = NULL;
    bool switched = false;

    if (op->mm && op->mm != current->active_mm) {
        prev_mm = current->active_mm;

        task_lock(current);
        current->mm = op->mm;
        current->active_mm = op->mm;
        task_unlock(current);

        switch_mm(prev_mm, op->mm, current);
        switched = true;
    }

    int ret = -1;
    
    if(op->cmd_type)
    {
        printk(KERN_INFO "[Kernel] param_ith : %d, nth : %d, wsize : %zu\n", ((struct ggml_compute_params *) op->params)->ith, ((struct ggml_compute_params *) op->params)->nth, ((struct ggml_compute_params *) op->params)->wsize);
    }
    else
    {
        ret = gemmini_rocc_issue(op->xcustom, op->funct, op->rs1, op->rs2);
    }

    if (switched && prev_mm) {
        task_lock(current);
        current->mm = NULL;
        current->active_mm = prev_mm;
        task_unlock(current);

        switch_mm(op->mm, prev_mm, current);
    }

    if (op->mm) {
        mmdrop(op->mm);
        op->mm = NULL;
    }

    if(op->cmd_type)
    {
        printk(KERN_INFO "[Kernel] Vector instruction issued successfully\n");

        return 0;
    }

    if (ret) {
        printk(KERN_DEBUG "[Kernel] RoCC issued with Error: xcustom=%u funct=%u (ret=%d)\n", op->xcustom, op->funct, ret);
    } else {
        printk(KERN_DEBUG "[Kernel] RoCC issued successfully: xcustom=%u funct=%u (ret=%d)\n", op->xcustom, op->funct, ret);
    }


    return 0;
}


static gemmini_sched_entry_t *dequeue_operation(gemmini_sched_entry_t *queue)
{
    unsigned long flags;
    spin_lock_irqsave(&gemmini_queue_lock, flags);
    
    // printk(KERN_INFO "[dequeue_operation] Dequeue locked.\n");
    if(count > 0)
    {        
        gemmini_sched_entry_t *op = &queue[head_idx];
        head_idx += 1;
        count--;

        // printk(KERN_INFO "[dequeue_operation] Dequeued operation at index %zu, count %zu\n", head_idx - 1, count);

        if (head_idx >= GEMMINI_SCHEDULE_QUEUE_CAPACITY)
            head_idx = 0;

        spin_unlock_irqrestore(&gemmini_queue_lock, flags);

        return op;
    }

    // printk(KERN_INFO "[dequeue_operation] Operation queue is empty\n");

    spin_unlock_irqrestore(&gemmini_queue_lock, flags);

    return NULL;
}


// TODo : syscall 이름 scheduler 관련으로 변경
SYSCALL_DEFINE0(gemmini_consumer_init) 
{
    printk(KERN_INFO "scheduler initialized.\n");

    int ret = scheduler_init();

    return ret;
}

SYSCALL_DEFINE0(gemmini_consumer_exit) 
{
    printk(KERN_INFO "scheduler exited.\n");

    int ret = scheduler_exit();

    return ret;
}
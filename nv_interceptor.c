#include <linux/syscalls.h>
#include <linux/linkage.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/compiler.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/uaccess.h>
#include <linux/ktime.h>
#include <linux/nv_interceptor.h>
#include <linux/irqflags.h>

#ifndef PRINT_ENABLE
#define PRINT_ENABLE 0
#endif

#if PRINT_ENABLE
#define NV_PRINTK(...) MY_PRINTK(__VA_ARGS__)
#else
#define NV_PRINTK(...) do { } while (0)
#endif


int register_model(char *model_name, uint64_t deadline) 
{
    model_ctx_t *slot;
    int model_id;
    int ret;

    ret = mutex_lock_interruptible(&nv_sched_lock);
    if (ret)
        return ret;

    model_id = -1;
    for (int i = 0; i < NUM_MAX_MODELS; i++) {
        if (!READ_ONCE(model_table.running_models[i].in_use)) {
            model_id = i;
            break;
        }
    }

    if (model_id < 0) {
        mutex_unlock(&nv_sched_lock);
        return -ENOSPC;
    }

    slot = &model_table.running_models[model_id];

    memset(slot, 0, sizeof(*slot));
    slot->model_id = model_id; // 모델 테이블에 등록된 모델 수를 ID로 사용
    slot->priority = 0;
    slot->dev = DEV_NONE;
    slot->in_flight_ops = 0;
    slot->wait_flag = 0;
    slot->deadline = ktime_get_ns() + deadline; // 절대 데드라인 계산. 사용자가 상대 데드라인을 보냈다고 가정.

    strscpy(slot->model_name, model_name, sizeof(slot->model_name));

    init_waitqueue_head(&slot->wq);

    WRITE_ONCE(slot->in_use, 1);
    model_table.table_count++;
    mutex_unlock(&nv_sched_lock);

    NV_PRINTK(KERN_INFO "[Kernel] Model registered: %s\n", slot->model_name);

    return model_id;
}

/*------------------------------------*/

SYSCALL_DEFINE2(init_model, uint64_t, model_name_addr, uint64_t, deadline) 
{
    char model_name[64];
    long copied;

    copied = strncpy_from_user(model_name, (char __user *)model_name_addr,
                               sizeof(model_name) - 1);
    if (copied < 0)
        return -EFAULT;

    model_name[copied] = '\0';
    
    int model_id = register_model(model_name, deadline);
    if (model_id < 0)
        return model_id;

    NV_PRINTK(KERN_INFO "Model initialized: %s (ID: %d, Deadline: %llu)\n", 
           model_table.running_models[model_id].model_name, model_id, deadline);

    return model_id;       // 배정된 모델 ID 반환. 이후 syscall 은 이 모델 ID를 사용하여 작업을 식별.
}

SYSCALL_DEFINE1(exit_model, uint32_t, model_id) {
    NV_PRINTK(KERN_INFO "Model %d requests exit.\n", model_id);
    enum dev_type released_dev;
    model_ctx_t *model_ctx;

    if (model_id >= NUM_MAX_MODELS)
        return -EINVAL;

    model_ctx = &model_table.running_models[model_id];

    mutex_lock(&nv_sched_lock);

    if (!READ_ONCE(model_ctx->in_use)) {
        mutex_unlock(&nv_sched_lock);
        return -EINVAL;
    }

    released_dev = model_ctx->dev;
    nv_release_issue_owner(model_id, released_dev);

    // lock/wq는 유지하고, 스케줄링 상태만 비활성화한다.
    model_ctx->priority = 0;
    model_ctx->dev = DEV_NONE;
    model_ctx->in_flight_ops = 0;
    model_ctx->wait_flag = 0;
    model_ctx->deadline = 0;
    model_ctx->dispatch_ts = 0;
    model_ctx->consecutive_grants = 0;
    model_ctx->total_grants = 0;
    model_ctx->model_name[0] = '\0';

    WRITE_ONCE(model_ctx->in_use, 0);

    WARN_ON(model_table.table_count == 0);
    if (model_table.table_count > 0)
        model_table.table_count--;

    // 모델 종료로 비워진 디바이스 기준으로 다음 runnable 모델을 깨운다.
    if (model_table.table_count > 0)
        nv_kick_scheduler();

    mutex_unlock(&nv_sched_lock);

    NV_PRINTK(KERN_INFO "Model exited: ID %d\n", model_id);

    return 0;
}

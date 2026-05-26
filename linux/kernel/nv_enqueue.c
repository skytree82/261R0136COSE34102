#include <linux/syscalls.h>
#include <linux/kernel.h>
#include <linux/linkage.h>

#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/compiler.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/uaccess.h>

#include <linux/stddef.h>
#include <linux/types.h>

#include <linux/nv_queue.h>


/* For double linked list declaration */

int nv_queue_enqueue(nv_queue_t *queue, nv_entry_t *entry) {
    unsigned long flags;
    nv_node_t *new_node = kmalloc(sizeof(nv_node_t), GFP_KERNEL);

    if (!new_node) {
        printk(KERN_ERR "Failed to allocate memory for new operation node\n");
        return -ENOMEM;
    }

    new_node->op = *entry;
    new_node->prev = NULL;
    new_node->next = NULL;

    spin_lock_irqsave(&nv_queue_lock, flags);

    if (queue->tail) {
        queue->tail->next = new_node;
        new_node->prev = queue->tail;
        queue->tail = new_node;
    } else {
        queue->head = new_node;
        queue->tail = new_node;
    }
    queue->count++;
    spin_unlock_irqrestore(&nv_queue_lock, flags);

    return 0;
}


int nv_queue_isEmpty(nv_queue_t *queue) {
    unsigned long flags;
    spin_lock_irqsave(&nv_queue_lock, flags);
    
    int isEmpty = (queue->count == 0);

    spin_unlock_irqrestore(&nv_queue_lock, flags);
    return isEmpty;
}

size_t nv_queue_count(nv_queue_t *queue) {
    unsigned long flags;
    size_t cur_count;

    spin_lock_irqsave(&nv_queue_lock, flags);
    cur_count = queue->count;
    spin_unlock_irqrestore(&nv_queue_lock, flags);

    return cur_count;
}

/*------------------------------------*/



static int gemmini_enqueue_internal(nv_queue_t *queue, uint8_t xcustom, uint8_t funct, uint64_t rs1, uint64_t rs2) {
    nv_entry_t new_entry = {
        .cmd_type = 0,
        .xcustom = xcustom,
        .funct = funct,
        .rs1 = rs1,
        .rs2 = rs2,
        .mm = NULL,
        .params = 0,
        .dst = 0,
    };

    new_entry.mm = get_task_mm(current);

    int ret = nv_queue_enqueue(queue, &new_entry);
    if (ret) {
        if (new_entry.mm)
            mmput(new_entry.mm);
        return ret;
    }

    /* 디버그 정보 출력 */
    printk(KERN_DEBUG
        "[Kernel] gemmini_inst_enqueued with xcustom=%u, funct=%u, rs1=%llu, rs2=%llu\n",
        xcustom, funct, (unsigned long long)rs1, (unsigned long long)rs2);

    // wake_up(&gemmini_inst_wait_queue);

    // printk(KERN_INFO "[Kernel] Gemmini Schedule Queue Size after enqueue: %zu\n", count);


    return 0;
}

static int saturn_enqueue_internal(nv_queue_t *queue, uint64_t params, size_t param_size, uint64_t dst, size_t dst_size) {
    
    nv_entry_t new_entry = {
        .cmd_type = 1,
        .xcustom = 0,
        .funct = 0,
        .rs1 = 0,
        .rs2 = 0,
        .mm = NULL,
        .params = params,
        .param_size = param_size,
        .dst = dst,
        .dst_size = dst_size,
    };

    new_entry.mm = get_task_mm(current);

    int ret = nv_queue_enqueue(queue, &new_entry);

    if (ret) {
        if (new_entry.mm)
            mmput(new_entry.mm);
        return ret;
    }

    printk(KERN_INFO "[Kernel] vector_inst_enqueued with params=%llu, param_size=%zu, dst=%llu, dst_size=%zu\n",
        (unsigned long long)params, param_size, (unsigned long long)dst, dst_size);

    // wake_up(&gemmini_inst_wait_queue);

    return 0;
}

SYSCALL_DEFINE4(gemmini_inst_enqueue, uint8_t, xcustom, uint8_t, funct, uint64_t, rs1, uint64_t, rs2) {

    int ret = gemmini_enqueue_internal(&nv_queue_1, xcustom, funct, rs1, rs2);

    return ret;
}

SYSCALL_DEFINE0(gemmini_fence) {
    printk(KERN_DEBUG "[Kernel] gemmini_fence called\n");
    
    unsigned long fflags;

    // 현재 남아있는 큐 사이즈만큼만 pop 하기
    spin_lock_irqsave(&fence_info.gemmini_fence_lock, fflags);

    fence_info.fence_target_count = nv_queue_count(&nv_queue_1);
    fence_info.fence_flag = 1;

    printk(KERN_DEBUG "[Kernel] Waiting for %d operations to complete before fence.\n", fence_info.fence_target_count);
    spin_unlock_irqrestore(&fence_info.gemmini_fence_lock, fflags);
    
    // wait until all prior enqueued operations are complete.
    wait_event(fence_queue, fence_info.fence_complete_flag == 1);

    spin_lock_irqsave(&fence_info.gemmini_fence_lock, fflags);

    fence_info.fence_complete_flag = 0;
    
    spin_unlock_irqrestore(&fence_info.gemmini_fence_lock, fflags);

    printk(KERN_DEBUG "[Kernel] all prior instructions issued.\n");

    asm volatile("fence");

    printk(KERN_DEBUG "[Kernel] gemmini_fence completed\n");
    
    return 0;
}

SYSCALL_DEFINE4(vec_inst_enqueue, uint64_t, param, size_t, param_size, uint64_t, dst, size_t, dst_size) {
    
    int ret = saturn_enqueue_internal(&nv_queue_1, param, param_size, dst, dst_size);

    return ret;
}
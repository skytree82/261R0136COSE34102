#include <linux/syscalls.h>
#include <linux/kernel.h>
#include <linux/linkage.h>

#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/compiler.h>
#include <linux/mm.h>
#include <linux/uaccess.h>

#include <linux/stddef.h>
#include <linux/types.h>

#include <linux/nv_queue.h>


gemmini_sched_entry_t queue[GEMMINI_SCHEDULE_QUEUE_CAPACITY];
size_t head_idx;
size_t tail_idx;
size_t count;

/* For double linked list declaration */

op_queue_t op_queue_1;

void op_queue_init(op_queue_t *queue) {
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
}

void op_queue_enqueue(op_queue_t *queue, gemmini_sched_entry_t *entry) {
    unsigned long flags;
    op_node_t *new_node = kmalloc(sizeof(op_node_t), GFP_KERNEL);
    // kmalloc 실패 시 에러 처리
    if (!new_node) {
        printk(KERN_ERR "Failed to allocate memory for new operation node\n");
        return;
    }
    new_node->op = *entry;
    new_node->prev = NULL;
    new_node->next = NULL;

    spin_lock_irqsave(&gemmini_queue_lock, flags);


    if (queue->tail) {
        queue->tail->next = new_node;
        new_node->prev = queue->tail;
        queue->tail = new_node;
    } else {
        queue->head = new_node;
        queue->tail = new_node;
    }
    queue->count++;
    spin_unlock_irqrestore(&gemmini_queue_lock, flags);
}


int op_queue_isEmpty(op_queue_t *queue) {
    unsigned long flags;
    spin_lock_irqsave(&gemmini_queue_lock, flags);
    
    int isEmpty = (queue->count == 0);

    spin_unlock_irqrestore(&gemmini_queue_lock, flags);
    return isEmpty;
}

size_t op_queue_count(op_queue_t *queue) {
    unsigned long flags;
    size_t cur_count;

    spin_lock_irqsave(&gemmini_queue_lock, flags);
    cur_count = queue->count;
    spin_unlock_irqrestore(&gemmini_queue_lock, flags);

    return cur_count;
}

/*------------------------------------*/


void gemmini_queue_list(void) {
    unsigned long flags;
    size_t idx;

    spin_lock_irqsave(&gemmini_queue_lock, flags);

    printk(KERN_INFO "Gemmini Schedule Queue (size=%zu):\n", count);
    idx = head_idx;
    for (size_t i = 0; i < count; i++) {
        gemmini_sched_entry_t *entry = &queue[idx];
        printk(KERN_INFO "  Entry %zu: xcustom=%u, funct=%u, rs1=%llu, rs2=%llu\n",
               i, entry->xcustom, entry->funct,
               (unsigned long long)entry->rs1, (unsigned long long)entry->rs2);
        idx++;
        if (idx >= GEMMINI_SCHEDULE_QUEUE_CAPACITY)
            idx = 0;
    }

    spin_unlock_irqrestore(&gemmini_queue_lock, flags);
}


int gemmini_queue_isEmpty(void) {
    unsigned long flags;
    spin_lock_irqsave(&gemmini_queue_lock, flags);
    
    int isEmpty = (count == 0);

    spin_unlock_irqrestore(&gemmini_queue_lock, flags);
    return isEmpty;
}

int gemmini_queue_isFull(void) {
    unsigned long flags;
    spin_lock_irqsave(&gemmini_queue_lock, flags);

    int isFull = (count >= GEMMINI_SCHEDULE_QUEUE_CAPACITY);

    spin_unlock_irqrestore(&gemmini_queue_lock, flags);
    return isFull;
}

size_t gemmini_queue_count(void) {
    unsigned long flags;
    size_t cur_count;

    spin_lock_irqsave(&gemmini_queue_lock, flags);
    cur_count = count;
    spin_unlock_irqrestore(&gemmini_queue_lock, flags);

    return cur_count;
}


static int gemmini_enqueue_internal(uint8_t xcustom, uint8_t funct, uint64_t rs1, uint64_t rs2) {
    unsigned long flags;
    spin_lock_irqsave(&gemmini_queue_lock, flags);

    if (count >= GEMMINI_SCHEDULE_QUEUE_CAPACITY) {
        spin_unlock_irqrestore(&gemmini_queue_lock, flags);
        return -ENOSPC;
    }

    queue[tail_idx].cmd_type = 0;

    queue[tail_idx].xcustom = xcustom;
    queue[tail_idx].funct = funct;
    queue[tail_idx].rs1 = rs1;
    queue[tail_idx].rs2 = rs2;

    // Not used for NPU instructions
    queue[tail_idx].params = 0;
    queue[tail_idx].dst = 0;

    if (current->mm) {
        mmgrab(current->mm);
        queue[tail_idx].mm = current->mm;
    } else {
        queue[tail_idx].mm = NULL;
    }

    tail_idx++;
    if (tail_idx >= GEMMINI_SCHEDULE_QUEUE_CAPACITY)
        tail_idx = 0;

    count++;

    /* 디버그 정보 출력 */
    printk(KERN_DEBUG
        "[Kernel] gemmini_inst_enqueued with xcustom=%u, funct=%u, rs1=%llu, rs2=%llu\n",
        xcustom, funct, (unsigned long long)rs1, (unsigned long long)rs2);

    // wake_up(&gemmini_inst_wait_queue);

    // printk(KERN_INFO "[Kernel] Gemmini Schedule Queue Size after enqueue: %zu / %d\n", count, GEMMINI_SCHEDULE_QUEUE_CAPACITY);


    spin_unlock_irqrestore(&gemmini_queue_lock, flags);
    return 0;
}

static int saturn_enqueue_internal(uint64_t params, uint64_t dst) {
    unsigned long flags;
    spin_lock_irqsave(&gemmini_queue_lock, flags);

    if (count >= GEMMINI_SCHEDULE_QUEUE_CAPACITY) {
        spin_unlock_irqrestore(&gemmini_queue_lock, flags);
        return -ENOSPC;
    }

    queue[tail_idx].cmd_type = 1;

    queue[tail_idx].params = params;
    queue[tail_idx].dst = dst;

    // Not used for vector instructions
    queue[tail_idx].xcustom = 0;
    queue[tail_idx].funct = 0;
    queue[tail_idx].rs1 = 0;
    queue[tail_idx].rs2 = 0;

    if (current->mm) {
        mmgrab(current->mm);
        queue[tail_idx].mm = current->mm;
    } else {
        queue[tail_idx].mm = NULL;
    }

    tail_idx++;
    if (tail_idx >= GEMMINI_SCHEDULE_QUEUE_CAPACITY)
        tail_idx = 0;

    count++;

    // wake_up(&gemmini_inst_wait_queue);

    spin_unlock_irqrestore(&gemmini_queue_lock, flags);
    return 0;
}

SYSCALL_DEFINE4(gemmini_inst_enqueue, uint8_t, xcustom, uint8_t, funct, uint64_t, rs1, uint64_t, rs2) {

    int ret = gemmini_enqueue_internal(xcustom, funct, rs1, rs2);

    return ret;
}

SYSCALL_DEFINE0(gemmini_fence) {
    printk(KERN_DEBUG "[Kernel] gemmini_fence called\n");
    
    unsigned long fflags;

    // 현재 남아있는 큐 사이즈만큼만 pop 하기
    spin_lock_irqsave(&fence_info.gemmini_fence_lock, fflags);

    fence_info.fence_target_count = gemmini_queue_count();
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

    unsigned long param_offset = offset_in_page(param);
    unsigned long param_npages = DIV_ROUND_UP(param_offset + param_size, PAGE_SIZE);

    struct page **param_pages;

    param_pages = vmalloc(param_npages * sizeof(struct page *));

    if (!param_pages)
        return -ENOMEM;

    long pinned = pin_user_pages(param,
                        param_npages,
                        FOLL_WRITE,
                        param_pages);

    if (pinned < 0) 
    {
        vfree(param_pages);
        return -1;
    }

    void *params_kaddr = vmap(param_pages, param_npages, VM_MAP, PAGE_KERNEL);

    printk(KERN_INFO "[Kernel] param_ith : %d, nth : %d, wsize : %zu\n", ((struct ggml_compute_params *) params_kaddr)->ith, ((struct ggml_compute_params *) params_kaddr)->nth, ((struct ggml_compute_params *) params_kaddr)->wsize);
    
    vunmap(params_kaddr);
    unpin_user_pages(param_pages, pinned);
    vfree(param_pages);

    // int ret = saturn_enqueue_internal(param, dst);

    // return ret;

    return 0;
}
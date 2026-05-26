// Minimal interface for the Gemmini syscall enqueue path.
// Keep this header freestanding within the kernel tree.

#ifndef _LINUX_GEMMINI_QUEUE_H
#define _LINUX_GEMMINI_QUEUE_H

#include <linux/stddef.h>
#include <linux/types.h>

#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/mm_types.h>

#ifndef GEMMINI_SCHEDULE_ON_FULL_FLUSH
#define GEMMINI_SCHEDULE_ON_FULL_FLUSH 1
#endif

#ifndef GEMMINI_SCHEDULE_WAKE_THRESHOLD
#define GEMMINI_SCHEDULE_WAKE_THRESHOLD 10
#endif

// Funct (func7) values must match userspace gemmini.h
#define k_CONFIG 0
#define k_MVIN2 1
#define k_MVIN 2
#define k_MVOUT 3
#define k_COMPUTE_PRELOADED 4
#define k_COMPUTE_ACCUMULATE 5
#define k_PRELOAD 6
#define k_FLUSH 7

#define k_LOOP_WS 8
#define k_LOOP_WS_CONFIG_BOUNDS 9
#define k_LOOP_WS_CONFIG_ADDRS_AB 10
#define k_LOOP_WS_CONFIG_ADDRS_DC 11
#define k_LOOP_WS_CONFIG_STRIDES_AB 12
#define k_LOOP_WS_CONFIG_STRIDES_DC 13

#define k_MVIN3 14

#define k_COUNTER 126

#define k_LOOP_CONV_WS 15
#define k_LOOP_CONV_WS_CONFIG_1 16
#define k_LOOP_CONV_WS_CONFIG_2 17
#define k_LOOP_CONV_WS_CONFIG_3 18
#define k_LOOP_CONV_WS_CONFIG_4 19
#define k_LOOP_CONV_WS_CONFIG_5 20
#define k_LOOP_CONV_WS_CONFIG_6 21

#define CONFIG_EX 0
#define CONFIG_LD 1
#define CONFIG_ST 2
#define CONFIG_BERT 3

#define GARBAGE_ADDR ((uint32_t)(-1))
#define OUTPUT_STATIONARY 0
#define WEIGHT_STATIONARY 1

#define NO_ACTIVATION 0
#define RELU 1
#define LAYERNORM 2
#define IGELU 3
#define SOFTMAX 4

typedef struct {
  uint32_t cmd_type;  // 0 for NPU, 1 for VPU insts
  
  // Only used for NPU instructions.
  uint8_t xcustom;
  uint8_t funct;
  uint64_t rs1;
  uint64_t rs2;

  // MM context of the enqueueing task (for correct page table during DMA)
  struct mm_struct *mm;

  // Only used for vector instructions.
  uint64_t params;
  size_t param_size;
  uint64_t dst;
  size_t dst_size;

} nv_entry_t;

typedef struct {
    int fence_flag;
    int fence_complete_flag;
    int fence_target_count;

    spinlock_t gemmini_fence_lock;
} fence_info_t;

typedef enum{
    __riscv_vsetvlmax_e32m8 = 0X20,
    __riscv_vfmv_v_f_f32m1 = 0x21,
    __riscv_vfmv_v_f_f32m8_tu = 0x22,
    __riscv_vsetvl_e32m8 = 0x23,
    __riscv_vle32_v_f32m8_tu = 0x24,
    __riscv_vfmacc_vv_f32m8_tu = 0x25,
    __riscv_vfredusum_vs_f32m8_f32m1 = 0x26,
    __riscv_vfmv_f_s_f32m1_f32 = 0x27,
    __riscv_vfmul_vf_f32m8 = 0x28,
    __riscv_vse32_v_f32m8 = 0x29,
} vector_inst;

extern fence_info_t fence_info;
extern spinlock_t nv_queue_lock;
extern wait_queue_head_t nv_inst_wait_queue;
extern wait_queue_head_t fence_queue;

int consume_directly(uint8_t xcustom, uint8_t funct, uint64_t rs1, uint64_t rs2);

// 연결리스트 기반 큐
typedef struct nv_node {
    nv_entry_t op;
    struct nv_node *prev;
    struct nv_node *next;
} nv_node_t;

typedef struct nv_queue {
    nv_node_t *head;
    nv_node_t *tail;
    size_t count;
} nv_queue_t;

typedef struct {
    struct page **pages;
    size_t npages;
    void *base_kaddr;
    char *kaddr; // base_kaddr + offset
} user_addr_t;


extern nv_queue_t nv_queue_0;   // Urgent
extern nv_queue_t nv_queue_1;   // High priority
extern nv_queue_t nv_queue_2;   // Normal priority
extern nv_queue_t nv_queue_3;   // Low priority

extern void nv_queue_init(nv_queue_t *queue);
extern int nv_queue_enqueue(nv_queue_t *queue, nv_entry_t *entry);
extern nv_entry_t *nv_queue_dequeue(nv_queue_t *queue);
extern int nv_queue_isEmpty(nv_queue_t *queue);
extern size_t nv_queue_count(nv_queue_t *queue);
void nv_queue_reset(nv_queue_t *queue);

#endif /* _LINUX_NV_QUEUE_H */

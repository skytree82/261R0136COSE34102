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
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/mm_types.h>

#define NUM_MAX_MODELS 10
#define NPU_QUEUE_SIZE 2048  // NPU Config 에 따라 결정
#define NUM_MAX_GEMMINI_QUEUE 4

#include <linux/atomic.h>

extern atomic64_t log_seq;

/*
 * Disabled sequence-number logging while debugging the RoCC IRQ path.
 * Keep the old definition here for easy restore.
 */
/*
#define MY_PRINTK(fmt, ...)                                           \
    printk(KERN_INFO "[myseq=%lld] " fmt,                             \
           atomic64_inc_return(&log_seq),                             \
           ##__VA_ARGS__)
*/
// #define MY_PRINTK(fmt, ...) printk(KERN_INFO fmt, ##__VA_ARGS__)
#define MY_PRINTK(fmt, ...) do { } while (0)

enum ggml_op {
    GGML_OP_NONE = 0,
    GGML_OP_DUP,
    GGML_OP_ADD,
    GGML_OP_ADD1,
    GGML_OP_ACC,
    GGML_OP_SUB,
    GGML_OP_MUL,
    GGML_OP_DIV,
    GGML_OP_SQR,
    GGML_OP_SQRT,
    GGML_OP_LOG,
    GGML_OP_SIN,
    GGML_OP_COS,
    GGML_OP_SUM,
    GGML_OP_SUM_ROWS,
    GGML_OP_MEAN,
    GGML_OP_ARGMAX,
    GGML_OP_COUNT_EQUAL,
    GGML_OP_REPEAT,
    GGML_OP_REPEAT_BACK,
    GGML_OP_CONCAT,
    GGML_OP_SILU_BACK,
    GGML_OP_NORM, // normalize
    GGML_OP_RMS_NORM,
    GGML_OP_RMS_NORM_BACK,
    GGML_OP_GROUP_NORM,
    GGML_OP_MUL_MAT,
    GGML_OP_MUL_MAT_ID,
    GGML_OP_OUT_PROD,
    GGML_OP_SCALE,
    GGML_OP_SET,
    GGML_OP_CPY,
    GGML_OP_CONT,
    GGML_OP_RESHAPE,
    GGML_OP_VIEW,
    GGML_OP_PERMUTE,
    GGML_OP_TRANSPOSE,
    GGML_OP_GET_ROWS,
    GGML_OP_GET_ROWS_BACK,
    GGML_OP_DIAG,
    GGML_OP_DIAG_MASK_INF,
    GGML_OP_DIAG_MASK_ZERO,
    GGML_OP_SOFT_MAX,
    GGML_OP_SOFT_MAX_BACK,
    GGML_OP_ROPE,
    GGML_OP_ROPE_BACK,
    GGML_OP_CLAMP,
    GGML_OP_CONV_TRANSPOSE_1D,
    GGML_OP_IM2COL,
    GGML_OP_IM2COL_BACK,
    GGML_OP_CONV_TRANSPOSE_2D,
    GGML_OP_POOL_1D,
    GGML_OP_POOL_2D,
    GGML_OP_POOL_2D_BACK,
    GGML_OP_UPSCALE, // nearest interpolate
    GGML_OP_PAD,
    GGML_OP_ARANGE,
    GGML_OP_TIMESTEP_EMBEDDING,
    GGML_OP_ARGSORT,
    GGML_OP_LEAKY_RELU,
    GGML_OP_FLASH_ATTN_EXT,
    GGML_OP_FLASH_ATTN_BACK,
    GGML_OP_SSM_CONV,
    GGML_OP_SSM_SCAN,
    GGML_OP_WIN_PART,
    GGML_OP_WIN_UNPART,
    GGML_OP_GET_REL_POS,
    GGML_OP_ADD_REL_POS,
    GGML_OP_RWKV_WKV,
    GGML_OP_UNARY,
    GGML_OP_MAP_UNARY,
    GGML_OP_MAP_BINARY,
    GGML_OP_MAP_CUSTOM1_F32,
    GGML_OP_MAP_CUSTOM2_F32,
    GGML_OP_MAP_CUSTOM3_F32,
    GGML_OP_MAP_CUSTOM1,
    GGML_OP_MAP_CUSTOM2,
    GGML_OP_MAP_CUSTOM3,
    GGML_OP_CROSS_ENTROPY_LOSS,
    GGML_OP_CROSS_ENTROPY_LOSS_BACK,
    GGML_OP_OPT_STEP_ADAMW,
    GGML_OP_COUNT,
};

typedef struct {
    uint32_t cmd_type;  // 0 for NPU, 1 for VPU insts
    enum ggml_op op;
    uint32_t model_id;
} nv_cmd_t;

// 연결리스트 기반 큐
typedef struct nv_node {
    nv_cmd_t op;
    struct nv_node *prev;
    struct nv_node *next;
} nv_node_t;

typedef struct nv_queue {
    nv_node_t *head;
    nv_node_t *tail;
    size_t count;
} nv_queue_t;

// 모델 컨택스트
// 모델 op 
enum dev_type {
    DEV_NONE = 0,
    DEV_NPU,
    DEV_VPU,
    DEV_COUNT,
};

typedef struct model_ctx {
    uint32_t in_use;             // 슬롯 유효 여부 (0: free, 1: active)
    int model_id;                // 커널이 부여한 모델의 id
    char model_name[32];         // 모델 이름 string

    int64_t priority;            // 현재 우선순위
    enum dev_type dev;           // 현재 op가 원하는 target dev
    uint32_t must_run;           // 다음 스케줄에서 최우선으로 이어서 실행해야 하는지

    int in_flight_ops;              // 모델이 현재 실행 중인지 여부
    uint32_t wait_flag;          // wake 조건 플래그

    wait_queue_head_t wq;        // 이 모델용 wait queue

    uint64_t deadline;             // 모델의 deadline (예정된 완료 시점)

    uint64_t dispatch_ts;             // 실제 실행 시작 시점
    uint32_t consecutive_grants;      // 연속으로 스케줄링된 횟수
    uint64_t total_grants;            // 누적 스케줄링 횟수
} model_ctx_t;

typedef struct {
    model_ctx_t running_models[NUM_MAX_MODELS];
    uint32_t table_count;
} model_table_t;

typedef struct {
    int gemmini_queue[NUM_MAX_GEMMINI_QUEUE];
    int head;
    int tail;
    int count;
} npu_queue_info_t;

extern model_table_t model_table;
extern spinlock_t nv_queue_lock;
extern struct mutex nv_sched_lock;
extern nv_queue_t nv_queue_0;
extern npu_queue_info_t npu_queue_info;
extern bool nv_retry_kick_for_npu;
extern bool nv_retry_kick_for_vpu;

void update_priorities(void);
void nv_kick_scheduler(void);
void nv_release_issue_owner(uint32_t model_id, enum dev_type dev);
int enqueue_nv_command(int model_idx);
int dequeue_nv_command(void);

#endif /* _LINUX_NV_QUEUE_H */

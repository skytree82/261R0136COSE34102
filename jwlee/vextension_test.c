#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <syscall.h>
#include <unistd.h>

#include "ggml.h"

#define GEMMINI_CONSUME_INIT 455
#define GEMMINI_CONSUME_EXIT 456
#define VEC_INST_ENQUEUE 458

struct ggml_compute_params {
    // ith = thread index, nth = number of threads
    int ith, nth;

    // work buffer for all threads
    size_t wsize;
    void * wdata;

    struct ggml_threadpool * threadpool;
};


static void ggml_compute_forward_rms_norm(const struct ggml_compute_params * params, struct ggml_tensor * dst);
static void ggml_compute_forward_rms_norm_f32(const struct ggml_compute_params * params, struct ggml_tensor * dst);
void init_tensor_f32(struct ggml_tensor * t, struct ggml_tensor * src0);

int main() {
    
    syscall(GEMMINI_CONSUME_INIT);

    struct ggml_tensor * dst = malloc(sizeof(struct ggml_tensor));
    struct ggml_tensor * src0 = malloc(sizeof(struct ggml_tensor));
    struct ggml_compute_params *params = malloc(sizeof(struct ggml_compute_params));

    params->ith = 5;
    params->nth = 5;
    params->wsize = sizeof(float);
    params->wdata = NULL;
    params->threadpool = NULL;

    init_tensor_f32(dst, src0);
    ggml_compute_forward_rms_norm(params, dst);


    sleep(50000);

    syscall(GEMMINI_CONSUME_EXIT);
    return 0;
}

static void ggml_compute_forward_rms_norm(
        const struct ggml_compute_params * params,
        struct ggml_tensor * dst) {

    const struct ggml_tensor * src0 = dst->src[0];

    switch (src0->type) {
        case GGML_TYPE_F32:
            {
                ggml_compute_forward_rms_norm_f32(params, dst);
            } break;
        default:
            {
                printf("fatal error");
            }
    }
}

static void ggml_compute_forward_rms_norm_f32(
        const struct ggml_compute_params * params,
        struct ggml_tensor * dst) {

    printf("param address: %p\n", params);
    printf("dst address: %p\n", dst);

    syscall(VEC_INST_ENQUEUE, (uint64_t)(uintptr_t) params, sizeof(struct ggml_compute_params), (uint64_t)(uintptr_t) dst, sizeof(struct ggml_tensor));

    return ;
}

void init_tensor_f32(struct ggml_tensor * t, struct ggml_tensor * src0) {
    memset(t, 0, sizeof(*t));
    memset(src0, 0, sizeof(*src0));

    src0->type = GGML_TYPE_F32;

    t->type = GGML_TYPE_F32;
    t->src[0] = src0;

    t->data = malloc(sizeof(float));
    src0->data  = malloc(sizeof(float));

    *(float*)t->data = 1.0f;
    *(float*)src0->data  = 2.0f;

    return ;
}

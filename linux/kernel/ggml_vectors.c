#include <linux/ggml_vectors.h>

// floating point type used to accumulate sums
typedef double ggml_float;

bool ggml_are_same_shape(const struct ggml_tensor * t0, const struct ggml_tensor * t1) {
    static_assert(GGML_MAX_DIMS == 4, "GGML_MAX_DIMS is not 4 - update this function");

    return
        (t0->ne[0] == t1->ne[0]) &&
        (t0->ne[1] == t1->ne[1]) &&
        (t0->ne[2] == t1->ne[2]) &&
        (t0->ne[3] == t1->ne[3]);
}

static void ggml_compute_forward_rms_norm_f32(
        const struct ggml_compute_params * params,
        struct ggml_tensor * dst) {

    const struct ggml_tensor * src0 = dst->src[0];

    GGML_ASSERT(ggml_are_same_shape(src0, dst));

    GGML_ASSERT(src0->nb[0] == sizeof(float));

    const int ith = params->ith;
    const int nth = params->nth;

    GGML_TENSOR_UNARY_OP_LOCALS

    float eps;
    memcpy(&eps, dst->op_params, sizeof(float));

    GGML_ASSERT(eps > 0.0f);

    // TODO: optimize
    for (int64_t i03 = 0; i03 < ne03; i03++) {
        for (int64_t i02 = 0; i02 < ne02; i02++) {
            for (int64_t i01 = ith; i01 < ne01; i01 += nth) {
                const float * x = (float *) ((char *) src0->data + i01*nb01 + i02*nb02 + i03*nb03);
                float * y = (float *) ((char *) dst->data + i01*nb1 + i02*nb2 + i03*nb3);

                // rvv 연산
#if defined(__riscv_v_intrinsic)
                // sum(x^2) using m8 accumulator like ggml_vec_dot_f32
                float sumf = 0.0f;

                size_t vl = __riscv_vsetvlmax_e32m8();

                vfloat32m1_t vs = __riscv_vfmv_v_f_f32m1(0.0f, 1);      // scalar init holder (VL=1)
                vfloat32m8_t vsum;                                      // vector accumulator
                vfloat32m8_t vx;

                // initialize accumulator (tail-undisturbed form to be safe like ggml)
                vsum = __riscv_vfmv_v_f_f32m8_tu(vsum, 0.0f, vl);

                for (int64_t i = 0; i < ne00; i += (int64_t)vl) {
                    vl = __riscv_vsetvl_e32m8((size_t)(ne00 - i));
                    vx = __riscv_vle32_v_f32m8_tu(vx, x + i, vl);

                    // vsum += vx * vx
                    vsum = __riscv_vfmacc_vv_f32m8_tu(vsum, vx, vx, vl);
                }

                // reduce once at the end: vs[0] = sum(vsum) + vs[0]
                vl = __riscv_vsetvlmax_e32m8();
                vs = __riscv_vfredusum_vs_f32m8_f32m1(vsum, vs, vl);
                sumf = __riscv_vfmv_f_s_f32m1_f32(vs);

                const float mean  = sumf / (float) ne00;
                const float scale = 1.0f / sqrtf(mean + eps);

                // y = x * scale
                for (int64_t i = 0; i < ne00; i += (int64_t)vl) {
                    vl = __riscv_vsetvl_e32m8((size_t)(ne00 - i));
                    vfloat32m8_t vx2 = __riscv_vle32_v_f32m8_tu(vx2, x + i, vl);
                    vfloat32m8_t vy2 = __riscv_vfmul_vf_f32m8(vx2, scale, vl);
                    __riscv_vse32_v_f32m8(y + i, vy2, vl);
                }

                // 스칼라 연산 + ggml 벡터 연산(rvv 아닌것)
#else
                ggml_float sum = 0.0;
                for (int64_t i00 = 0; i00 < ne00; i00++) {
                    sum += (ggml_float)(x[i00] * x[i00]);
                }

                const float mean = sum/ne00;

                // float * y = (float *) ((char *) dst->data + i01*nb1 + i02*nb2 + i03*nb3);

                memcpy(y, x, ne00 * sizeof(float));
                // for (int i00 = 0; i00 < ne00; i00++) {
                //     y[i00] = x[i00];
                // }

                const float scale = 1.0f/sqrtf(mean + eps);

                ggml_vec_scale_f32(ne00, y, scale);
#endif
            }
        }
    }
}
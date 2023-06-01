#include "encoder.h"
#include "ggml.h"
#include "util.h"

#include <cmath>

static int get_extra_padding_for_conv_1d(ggml_tensor * inp, float kernel_size, float stride, float padding_total) {
    float length = inp->ne[0];
    float n_frames = (length - kernel_size + padding_total) / stride + 1.0f;
    int ideal_length = (std::ceilf(n_frames) - 1) * stride + (kernel_size - padding_total);
    return ideal_length - length;
}

static struct ggml_tensor * pad_1d(ggml_context * ctx0, ggml_tensor * inp, int padding_left, int padding_right) {
    int length = inp->ne[0];
    int dim = inp->ne[1];
    ENCODEC_ASSERT(padding_left  >= 0);
    ENCODEC_ASSERT(padding_right >= 0);

    const int max_pad = std::max(padding_left, padding_right);
    int extra_pad = 0;

    if (length <= max_pad) {
        extra_pad = max_pad - length + 1;
        int padding[2] = {0, extra_pad};
        inp = ggml_pad_1d_constant(ctx0, inp, padding, 0);
    }

    int padding[2] = {padding_left, padding_right};
    struct ggml_tensor * padded = ggml_pad_1d_reflective(ctx0, inp, padding);

    const int end = padded->ne[0] - extra_pad;

    struct ggml_tensor *dest = ggml_view_2d(ctx0, padded, end, dim, inp->nb[1], 0);

    return dest;
}

struct ggml_tensor * strided_conv_1d(
            ggml_context * ctx0,
             ggml_tensor * inp,
             ggml_tensor * conv_w_v,
             ggml_tensor * conv_w_g,
             ggml_tensor * conv_b,
                     int   stride) {
    int kernel_size = conv_w_v->ne[0];
    int padding_total = kernel_size - stride;
    int extra_padding = get_extra_padding_for_conv_1d(inp, kernel_size, stride, padding_total);

    struct ggml_tensor * padded_inp = pad_1d(ctx0, inp, padding_total, extra_padding);

    // weight norm
    conv_w_v = ggml_l2_norm(ctx0, conv_w_v);
    conv_w_v = ggml_cont(ctx0, ggml_permute(ctx0, conv_w_v, 2, 1, 0, 3));

    conv_w_g = ggml_repeat(ctx0, conv_w_g, conv_w_v);

    struct ggml_tensor * conv_w = ggml_cont(ctx0,
                                    ggml_permute(ctx0,
                                        ggml_mul(ctx0, conv_w_v, conv_w_g), 2, 1, 0, 3));

    struct ggml_tensor * dst = ggml_conv_1d_1s(ctx0, conv_w, padded_inp);

    dst = ggml_transpose(ctx0, dst);
    dst = ggml_add(ctx0, ggml_repeat(ctx0, conv_b, dst), dst);
    dst = ggml_cont(ctx0, ggml_transpose(ctx0, dst));

    return dst;
}
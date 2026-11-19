// Qt-free box-blur kernel used by finn-shell's layer-surface shadow cache.
//
// Kept free of Qt so the exact code that ships can be timed by `make bench`
// on the host (which has no Qt/wlroots toolchain). `box_blur_argb` in
// shell_layout.hpp is the only production caller.
//
// Pixels are packed QRgb words, i.e. 0xAARRGGBB. Channel arithmetic is done on
// unpacked integers: averaging packed words directly would carry bits between
// the alpha and colour bytes.

#ifndef FINN_SHELL_BLUR_KERNEL_HPP
#define FINN_SHELL_BLUR_KERNEL_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace finn::shell::blur_kernel {

inline constexpr std::uint32_t make_argb(int a, int r, int g, int b) {
    return (static_cast<std::uint32_t>(a & 0xff) << 24) |
           (static_cast<std::uint32_t>(r & 0xff) << 16) |
           (static_cast<std::uint32_t>(g & 0xff) << 8) |
           static_cast<std::uint32_t>(b & 0xff);
}

inline void unpack_argb(std::uint32_t px, int& a, int& r, int& g, int& b) {
    a = static_cast<int>((px >> 24) & 0xff);
    r = static_cast<int>((px >> 16) & 0xff);
    g = static_cast<int>((px >> 8) & 0xff);
    b = static_cast<int>(px & 0xff);
}

// One separable pass over `length` pixels separated by `stride` words.
// Sliding-window sums make this O(length) instead of O(length * radius):
// a window moves one pixel at a time, adding the entering pixel and removing
// the leaving pixel. `src` and `dst` may overlap only when stride == 0, which
// never happens in our callers (the two passes ping-pong between buffers).
inline void box_blur_1d(const std::uint32_t* src, std::uint32_t* dst,
                        int stride, int length, int radius) {
    if (radius <= 0 || length <= 0) return;
    int sum_a = 0, sum_r = 0, sum_g = 0, sum_b = 0;
    int lo = 0, hi = -1;

    for (int i = 0; i < length; ++i) {
        const int want_lo = std::max(0, i - radius);
        const int want_hi = std::min(length - 1, i + radius);
        while (hi < want_hi) {
            int a, r, g, b;
            unpack_argb(src[++hi * stride], a, r, g, b);
            sum_a += a; sum_r += r; sum_g += g; sum_b += b;
        }
        while (lo < want_lo) {
            int a, r, g, b;
            unpack_argb(src[lo++ * stride], a, r, g, b);
            sum_a -= a; sum_r -= r; sum_g -= g; sum_b -= b;
        }
        const int n = hi - lo + 1;
        dst[i * stride] = make_argb(sum_a / n, sum_r / n, sum_g / n, sum_b / n);
    }
}

// Three separable passes approximate the Gaussian falloff of the elevation
// token. `buf` and `tmp` must each hold w * h packed pixels; the caller owns
// them so a redraw never allocates inside this function.
inline void box_blur_argb_u32(std::uint32_t* buf, std::uint32_t* tmp,
                              int w, int h, int radius) {
    if (radius <= 0 || w <= 0 || h <= 0) return;
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) {
            box_blur_1d(buf + static_cast<std::size_t>(y) * w,
                        tmp + static_cast<std::size_t>(y) * w, 1, w, radius);
        }
        for (int x = 0; x < w; ++x) {
            box_blur_1d(tmp + x, buf + x, w, h, radius);
        }
    }
}

}  // namespace finn::shell::blur_kernel

#endif  // FINN_SHELL_BLUR_KERNEL_HPP

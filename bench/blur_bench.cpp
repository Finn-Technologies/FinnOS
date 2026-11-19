// Host microbenchmark for the finn-shell shadow-cache blur. No Qt required:
// this includes the exact kernel that ships (src/shell/blur_kernel.hpp) and
// times it against the previous neighbour-scanning algorithm on packed
// 0xAARRGGBB buffers. `make bench` builds and runs it.
//
// Scope of the measurement: this times the pixel kernels only, on this host
// (compiler/CPU printed below). It does not time QImage row copies, QPainter
// mask generation or the compositor pass. Those are unchanged by this fix.
// VM/Qt end-to-end verification is still required before claiming a UX win.

#include "shell/blur_kernel.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

namespace {

using finn::shell::blur_kernel::box_blur_argb_u32;
using finn::shell::blur_kernel::make_argb;
using finn::shell::blur_kernel::unpack_argb;

// The algorithm this replaced: for every output pixel, rescan up to 2r+1
// neighbours. Kept here as the baseline so the speedup is reproducible.
void reference_blur(std::uint32_t* buf, std::uint32_t* tmp,
                    int w, int h, int radius) {
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int a = 0, r = 0, g = 0, b = 0, n = 0;
                for (int k = std::max(0, x - radius);
                     k <= std::min(w - 1, x + radius); ++k) {
                    int ca, cr, cg, cb;
                    unpack_argb(buf[static_cast<std::size_t>(y) * w + k],
                                ca, cr, cg, cb);
                    a += ca; r += cr; g += cg; b += cb; ++n;
                }
                tmp[static_cast<std::size_t>(y) * w + x] =
                    make_argb(a / n, r / n, g / n, b / n);
            }
        }
        for (int x = 0; x < w; ++x) {
            for (int y = 0; y < h; ++y) {
                int a = 0, r = 0, g = 0, b = 0, n = 0;
                for (int k = std::max(0, y - radius);
                     k <= std::min(h - 1, y + radius); ++k) {
                    int ca, cr, cg, cb;
                    unpack_argb(tmp[static_cast<std::size_t>(k) * w + x],
                                ca, cr, cg, cb);
                    a += ca; r += cr; g += cg; b += cb; ++n;
                }
                buf[static_cast<std::size_t>(y) * w + x] =
                    make_argb(a / n, r / n, g / n, b / n);
            }
        }
    }
}

struct Case {
    const char* name;
    int w;
    int h;
    int blur;  // token radius; the shadow pass uses blur / 3
};

double run(void (*fn)(std::uint32_t*, std::uint32_t*, int, int, int),
           const std::vector<std::uint32_t>& input, int w, int h, int radius,
           std::vector<std::uint32_t>& out, int repeats) {
    double best = 1e300;
    for (int rep = 0; rep < repeats; ++rep) {
        out = input;
        std::vector<std::uint32_t> tmp(static_cast<std::size_t>(w) * h);
        const auto t0 = std::chrono::steady_clock::now();
        fn(out.data(), tmp.data(), w, h, radius);
        const auto t1 = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    return best;
}

}  // namespace

int main() {
#if defined(__clang__)
    std::puts("compiler: clang " __VERSION__);
#elif defined(__GNUC__)
    std::puts("compiler: gcc " __VERSION__);
#else
    std::puts("compiler: unknown");
#endif
    std::puts("measurement: pixel kernel only, best of 5 runs, packed 0xAARRGGBB");

    const Case cases[] = {
        // Figma popovers: w+2*pad with pad = ceil(0.4 * blur).
        {"ControlCenter 444x300, blur_glass 35", 472, 328, 35},
        {"HomeMenu 585x420, blur_glass 35", 613, 448, 35},
        {"Notification 380x96, blur 25", 400, 116, 25},
        {"Dock pill 220x56, blur_glass 35", 248, 84, 35},
    };
    bool all_match = true;
    for (const Case& c : cases) {
        const int radius = std::max(1, c.blur / 3);
        const std::size_t n = static_cast<std::size_t>(c.w) * c.h;
        std::mt19937 rng(1234);
        std::vector<std::uint32_t> input(n);
        for (auto& px : input) px = rng();

        std::vector<std::uint32_t> got, want;
        const double old_ms = run(reference_blur, input, c.w, c.h, radius, want, 5);
        const double new_ms = run(box_blur_argb_u32, input, c.w, c.h, radius, got, 5);
        const bool match = got == want;
        all_match &= match;
        std::printf("%-44s r=%2d  old=%8.3f ms  new=%8.3f ms  speedup=%6.2fx  exact=%s\n",
                    c.name, radius, old_ms, new_ms,
                    new_ms > 0 ? old_ms / new_ms : 0.0,
                    match ? "yes" : "NO");
    }
    std::puts(all_match ? "result: kernels agree bit-for-bit" :
                          "result: MISMATCH - kernel changed the image");
    return all_match ? EXIT_SUCCESS : EXIT_FAILURE;
}

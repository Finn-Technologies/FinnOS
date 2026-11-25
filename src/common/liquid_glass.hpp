// Shared continuous SDF Liquid Glass refraction renderer used by both
// libfinnui (FinnSquircle) and finn-comp (CompositorBackdropItem).
//
// Computes exact rounded-rectangle signed distance and outward surface normal
// along the outer curved bevel (zero hard interior seam), bending the backdrop
// smoothly with subtle RGB chromatic dispersion and a directional Fresnel rim.

#ifndef FINN_COMMON_LIQUID_GLASS_HPP
#define FINN_COMMON_LIQUID_GLASS_HPP

#include <QImage>
#include <QRectF>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace finn::glass {

inline void sample_bilinear_rgb(const QImage& img, float fx, float fy,
                                float& r, float& g, float& b) {
    const int w = img.width();
    const int h = img.height();
    const float cx = std::clamp(fx, 0.0f, static_cast<float>(w - 1));
    const float cy = std::clamp(fy, 0.0f, static_cast<float>(h - 1));
    const int x0 = static_cast<int>(cx);
    const int y0 = static_cast<int>(cy);
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const float tx = cx - static_cast<float>(x0);
    const float ty = cy - static_cast<float>(y0);
    const float itx = 1.0f - tx;
    const float ity = 1.0f - ty;

    const auto* row0 = reinterpret_cast<const QRgb*>(img.constScanLine(y0));
    const auto* row1 = reinterpret_cast<const QRgb*>(img.constScanLine(y1));
    const QRgb c00 = row0[x0];
    const QRgb c10 = row0[x1];
    const QRgb c01 = row1[x0];
    const QRgb c11 = row1[x1];

    const float w00 = itx * ity;
    const float w10 = tx * ity;
    const float w01 = itx * ty;
    const float w11 = tx * ty;

    r = qRed(c00) * w00 + qRed(c10) * w10 + qRed(c01) * w01 + qRed(c11) * w11;
    g = qGreen(c00) * w00 + qGreen(c10) * w10 + qGreen(c01) * w01 + qGreen(c11) * w11;
    b = qBlue(c00) * w00 + qBlue(c10) * w10 + qBlue(c01) * w01 + qBlue(c11) * w11;
}

inline float sample_bilinear_ch(const QImage& img, float fx, float fy, int ch) {
    const int w = img.width();
    const int h = img.height();
    const float cx = std::clamp(fx, 0.0f, static_cast<float>(w - 1));
    const float cy = std::clamp(fy, 0.0f, static_cast<float>(h - 1));
    const int x0 = static_cast<int>(cx);
    const int y0 = static_cast<int>(cy);
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const float tx = cx - static_cast<float>(x0);
    const float ty = cy - static_cast<float>(y0);
    const float itx = 1.0f - tx;
    const float ity = 1.0f - ty;

    const auto* row0 = reinterpret_cast<const QRgb*>(img.constScanLine(y0));
    const auto* row1 = reinterpret_cast<const QRgb*>(img.constScanLine(y1));
    auto pick = [ch](QRgb c) -> float {
        if (ch == 0) return static_cast<float>(qRed(c));
        if (ch == 1) return static_cast<float>(qGreen(c));
        return static_cast<float>(qBlue(c));
    };
    return pick(row0[x0]) * (itx * ity) + pick(row0[x1]) * (tx * ity) +
           pick(row1[x0]) * (itx * ty) + pick(row1[x1]) * (tx * ty);
}

// Render a continuous Liquid Glass refracted surface from `backdrop` over `src_rect`
// into an output image of pixel size `(dst_w, dst_h)` with corner radius `radius`.
// When `flat_right` is true, the top-right and bottom-right corners remain flat (0px)
// so a split-pane sidebar attaches flush against the main content pane.
inline QImage render_liquid_glass(const QImage& backdrop,
                                  const QRectF& src_rect,
                                  int dst_w,
                                  int dst_h,
                                  double radius,
                                  bool dark_mode,
                                  bool flat_right = false) {
    if (backdrop.isNull() || dst_w <= 1 || dst_h <= 1 ||
        src_rect.width() <= 0.0 || src_rect.height() <= 0.0) {
        return {};
    }

    const QImage src = (backdrop.format() == QImage::Format_RGB32 ||
                        backdrop.format() == QImage::Format_ARGB32 ||
                        backdrop.format() == QImage::Format_ARGB32_Premultiplied)
                           ? backdrop
                           : backdrop.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    QImage out(dst_w, dst_h, QImage::Format_ARGB32_Premultiplied);
    const float fw = static_cast<float>(dst_w);
    const float fh = static_cast<float>(dst_h);
    const float hy = fh * 0.5f;
    const float r = std::clamp(static_cast<float>(radius), 0.0f, std::min(fw * 0.5f, hy));
    const float bevel = std::clamp(std::min(fw, fh) * 0.28f, 6.0f, 16.0f);
    // When flat_right is enabled, extend the virtual right edge past bevel + r so
    // the right side is completely flat and unrefracted where it joins the content pane.
    const float eff_fw = flat_right ? (fw + bevel + r + 8.0f) : fw;
    const float hx = eff_fw * 0.5f;

    const float sx0 = static_cast<float>(src_rect.x());
    const float sy0 = static_cast<float>(src_rect.y());
    const float scale_x = static_cast<float>(src_rect.width()) / fw;
    const float scale_y = static_cast<float>(src_rect.height()) / fh;

    // Max optical displacement at the outer curved rim (in source pixels)
    const float max_shift_x = std::min(16.0f, bevel * 0.85f) * scale_x;
    const float max_shift_y = std::min(16.0f, bevel * 0.85f) * scale_y;

    for (int y = 0; y < dst_h; ++y) {
        auto* dst_row = reinterpret_cast<QRgb*>(out.scanLine(y));
        const float py = static_cast<float>(y) + 0.5f;
        const float dy = py - hy;
        const float qy = std::abs(dy) - (hy - r);
        const float my = std::max(qy, 0.0f);
        const float sy_base = sy0 + py * scale_y;

        for (int x = 0; x < dst_w; ++x) {
            const float px = static_cast<float>(x) + 0.5f;
            const float dx = px - hx;
            const float qx = std::abs(dx) - (hx - r);
            const float mx = std::max(qx, 0.0f);
            const float out_d = std::sqrt(mx * mx + my * my);
            const float in_d = std::min(std::max(qx, qy), 0.0f);
            const float sdf = out_d + in_d - r;  // <= 0 inside

            const float sx_base = sx0 + px * scale_x;

            // Smooth anti-aliased edge alpha (handled additionally by QPainter clip)
            if (sdf > 1.0f) {
                dst_row[x] = 0;
                continue;
            }

            const float dist_in = std::max(0.0f, -sdf);
            float rf = 0.0f, gf = 0.0f, bf = 0.0f;

            if (dist_in >= bevel) {
                // Flat glass interior: zero seam, pure crisp blurred backdrop
                sample_bilinear_rgb(src, sx_base, sy_base, rf, gf, bf);
            } else {
                // Smooth C2 optical lens curvature along the rounded perimeter
                float nx = 0.0f;
                float ny = 0.0f;
                if (qx > 0.0f && qy > 0.0f && out_d > 1e-4f) {
                    nx = std::copysign(mx / out_d, dx);
                    ny = std::copysign(my / out_d, dy);
                } else if (qx > qy) {
                    nx = std::copysign(1.0f, dx);
                } else {
                    ny = std::copysign(1.0f, dy);
                }

                // Cubic smooth ramp from 0 at dist_in == bevel to 1 at rim (zero hard step!)
                const float t = 1.0f - (dist_in / bevel);
                const float lens = t * t * t;

                // Pure optical lens refraction along the curved rim with subtle RGB dispersion
                // (no painted white border glow — the refracted background itself defines the edge)
                const float ox = nx * lens * max_shift_x;
                const float oy = ny * lens * max_shift_y;
                rf = sample_bilinear_ch(src, sx_base - ox * 1.15f, sy_base - oy * 1.15f, 0);
                gf = sample_bilinear_ch(src, sx_base - ox * 1.00f, sy_base - oy * 1.00f, 1);
                bf = sample_bilinear_ch(src, sx_base - ox * 0.85f, sy_base - oy * 0.85f, 2);
                (void)dark_mode;
            }

            const int ri = std::clamp(static_cast<int>(rf + 0.5f), 0, 255);
            const int gi = std::clamp(static_cast<int>(gf + 0.5f), 0, 255);
            const int bi = std::clamp(static_cast<int>(bf + 0.5f), 0, 255);
            dst_row[x] = qRgba(ri, gi, bi, 255);
        }
    }
    return out;
}

}  // namespace finn::glass

#endif  // FINN_COMMON_LIQUID_GLASS_HPP

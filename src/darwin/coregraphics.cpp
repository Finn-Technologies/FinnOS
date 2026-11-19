#include "darwin/coregraphics.hpp"

#include "darwin/foundation.hpp"
#include "darwin/objc_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace finn::darwin {
namespace {

template <typename Fn>
ObjcImp to_imp(Fn fn) {
    ObjcImp out = nullptr;
    static_assert(sizeof(out) == sizeof(fn));
    std::memcpy(&out, &fn, sizeof(out));
    return out;
}

template <typename Fn>
void* to_ptr(Fn fn) {
    void* out = nullptr;
    static_assert(sizeof(out) == sizeof(fn));
    std::memcpy(&out, &fn, sizeof(out));
    return out;
}

// Darwin 64-bit CoreGraphics geometry ABI (CGFloat = double)
struct CGPoint {
    double x;
    double y;
};

struct CGSize {
    double width;
    double height;
};

struct CGRect {
    CGPoint origin;
    CGSize size;
};

// Exported global geometry & color space constants
CGPoint g_cg_point_zero{0.0, 0.0};
CGSize g_cg_size_zero{0.0, 0.0};
CGRect g_cg_rect_zero{{0.0, 0.0}, {0.0, 0.0}};
CGRect g_cg_rect_null{
    {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()},
    {0.0, 0.0}};
CGRect g_cg_rect_infinite{
    {-std::numeric_limits<double>::max() * 0.5, -std::numeric_limits<double>::max() * 0.5},
    {std::numeric_limits<double>::max(), std::numeric_limits<double>::max()}};

ObjcId g_cs_name_srgb = nullptr;
ObjcId g_cs_name_generic_rgb = nullptr;
ObjcId g_cs_name_device_rgb = nullptr;
ObjcId g_cs_name_display_p3 = nullptr;

// --- CoreGraphics CF/ObjC backed runtime objects ---
struct CGColorSpaceObj {
    ObjcObject base;
    std::string* name;
    std::size_t components;
};

struct CGColorObj {
    ObjcObject base;
    double rgba[4];
};

struct CGGState {
    double fill_r = 0.0;
    double fill_g = 0.0;
    double fill_b = 0.0;
    double fill_a = 1.0;
    double stroke_r = 0.0;
    double stroke_g = 0.0;
    double stroke_b = 0.0;
    double stroke_a = 1.0;
    double alpha = 1.0;
    double line_width = 1.0;
    double tx = 0.0;
    double ty = 0.0;
};

struct CGContextObj {
    ObjcObject base;
    std::uint8_t* data;
    std::vector<std::uint8_t>* owned_storage;
    std::size_t width;
    std::size_t height;
    std::size_t bits_per_component;
    std::size_t bytes_per_row;
    std::uint32_t bitmap_info;
    ObjcId color_space;
    std::vector<CGGState>* gstate_stack;
};

struct CALayerObj {
    ObjcObject base;
    CGRect frame;
    CGRect bounds;
    ObjcId background_color;
    double corner_radius;
    float opacity;
    bool hidden;
    bool masks_to_bounds;
    ObjcId superlayer;
    std::vector<ObjcId>* sublayers;
};

ObjcClass* g_cls_cgcolorspace = nullptr;
ObjcClass* g_cls_cgcolor = nullptr;
ObjcClass* g_cls_cgcontext = nullptr;
ObjcClass* g_cls_calayer = nullptr;

// --- Geometry functions ---
bool shim_CGRectIsNull(CGRect r) {
    return std::isinf(r.origin.x) || std::isinf(r.origin.y);
}

CGRect shim_CGRectStandardize(CGRect r) {
    if (shim_CGRectIsNull(r)) return g_cg_rect_null;
    if (r.size.width < 0.0) {
        r.origin.x += r.size.width;
        r.size.width = -r.size.width;
    }
    if (r.size.height < 0.0) {
        r.origin.y += r.size.height;
        r.size.height = -r.size.height;
    }
    return r;
}

bool shim_CGRectIsEmpty(CGRect r) {
    if (shim_CGRectIsNull(r)) return true;
    return r.size.width == 0.0 || r.size.height == 0.0 ||
           std::isnan(r.origin.x) || std::isnan(r.origin.y) ||
           std::isnan(r.size.width) || std::isnan(r.size.height);
}

bool shim_CGRectEqualToRect(CGRect r1, CGRect r2) {
    const bool n1 = shim_CGRectIsNull(r1);
    const bool n2 = shim_CGRectIsNull(r2);
    if (n1 && n2) return true;
    if (n1 || n2) return false;
    r1 = shim_CGRectStandardize(r1);
    r2 = shim_CGRectStandardize(r2);
    return r1.origin.x == r2.origin.x && r1.origin.y == r2.origin.y &&
           r1.size.width == r2.size.width && r1.size.height == r2.size.height;
}

CGRect shim_CGRectIntersection(CGRect r1, CGRect r2) {
    if (shim_CGRectIsNull(r1) || shim_CGRectIsNull(r2)) return g_cg_rect_null;
    r1 = shim_CGRectStandardize(r1);
    r2 = shim_CGRectStandardize(r2);
    const double x0 = std::max(r1.origin.x, r2.origin.x);
    const double y0 = std::max(r1.origin.y, r2.origin.y);
    const double x1 = std::min(r1.origin.x + r1.size.width, r2.origin.x + r2.size.width);
    const double y1 = std::min(r1.origin.y + r1.size.height, r2.origin.y + r2.size.height);
    if (x1 < x0 || y1 < y0) return g_cg_rect_null;
    return {{x0, y0}, {x1 - x0, y1 - y0}};
}

CGRect shim_CGRectUnion(CGRect r1, CGRect r2) {
    if (shim_CGRectIsNull(r1)) return shim_CGRectStandardize(r2);
    if (shim_CGRectIsNull(r2)) return shim_CGRectStandardize(r1);
    r1 = shim_CGRectStandardize(r1);
    r2 = shim_CGRectStandardize(r2);
    const double x0 = std::min(r1.origin.x, r2.origin.x);
    const double y0 = std::min(r1.origin.y, r2.origin.y);
    const double x1 = std::max(r1.origin.x + r1.size.width, r2.origin.x + r2.size.width);
    const double y1 = std::max(r1.origin.y + r1.size.height, r2.origin.y + r2.size.height);
    return {{x0, y0}, {x1 - x0, y1 - y0}};
}

bool shim_CGRectContainsPoint(CGRect r, CGPoint p) {
    if (shim_CGRectIsEmpty(r)) return false;
    r = shim_CGRectStandardize(r);
    return p.x >= r.origin.x && p.x < (r.origin.x + r.size.width) &&
           p.y >= r.origin.y && p.y < (r.origin.y + r.size.height);
}

bool shim_CGRectIntersectsRect(CGRect r1, CGRect r2) {
    return !shim_CGRectIsNull(shim_CGRectIntersection(r1, r2));
}

bool shim_CGRectContainsRect(CGRect r1, CGRect r2) {
    if (shim_CGRectIsNull(r1) || shim_CGRectIsNull(r2)) return false;
    return shim_CGRectEqualToRect(shim_CGRectUnion(r1, r2), r1);
}

CGRect shim_CGRectInset(CGRect r, double dx, double dy) {
    if (shim_CGRectIsNull(r)) return g_cg_rect_null;
    r = shim_CGRectStandardize(r);
    r.origin.x += dx;
    r.origin.y += dy;
    r.size.width -= 2.0 * dx;
    r.size.height -= 2.0 * dy;
    if (r.size.width < 0.0 || r.size.height < 0.0) return g_cg_rect_null;
    return r;
}

CGRect shim_CGRectOffset(CGRect r, double dx, double dy) {
    if (shim_CGRectIsNull(r)) return g_cg_rect_null;
    r.origin.x += dx;
    r.origin.y += dy;
    return r;
}

double shim_CGRectGetMinX(CGRect r) { return shim_CGRectStandardize(r).origin.x; }
double shim_CGRectGetMidX(CGRect r) { r = shim_CGRectStandardize(r); return r.origin.x + r.size.width * 0.5; }
double shim_CGRectGetMaxX(CGRect r) { r = shim_CGRectStandardize(r); return r.origin.x + r.size.width; }
double shim_CGRectGetMinY(CGRect r) { return shim_CGRectStandardize(r).origin.y; }
double shim_CGRectGetMidY(CGRect r) { r = shim_CGRectStandardize(r); return r.origin.y + r.size.height * 0.5; }
double shim_CGRectGetMaxY(CGRect r) { r = shim_CGRectStandardize(r); return r.origin.y + r.size.height; }
double shim_CGRectGetWidth(CGRect r) { return std::fabs(r.size.width); }
double shim_CGRectGetHeight(CGRect r) { return std::fabs(r.size.height); }

// --- CGColorSpace ---
void cgcolorspace_dtor(ObjcId obj) {
    auto* cs = reinterpret_cast<CGColorSpaceObj*>(obj);
    delete cs->name;
    cs->name = nullptr;
}

void* shim_CGColorSpaceCreateDeviceRGB() {
    auto* cs = reinterpret_cast<CGColorSpaceObj*>(
        finn_objc_alloc_object(g_cls_cgcolorspace, sizeof(CGColorSpaceObj)));
    cs->name = new std::string("kCGColorSpaceDeviceRGB");
    cs->components = 3;
    return cs;
}

void* shim_CGColorSpaceCreateWithName(ObjcId name_obj) {
    auto* cs = reinterpret_cast<CGColorSpaceObj*>(
        finn_objc_alloc_object(g_cls_cgcolorspace, sizeof(CGColorSpaceObj)));
    const std::string n = nsstring_to_utf8(name_obj);
    cs->name = new std::string(n.empty() ? "kCGColorSpaceSRGB" : n);
    cs->components = (n.find("Gray") != std::string::npos) ? 1 : 3;
    return cs;
}

void* shim_CGColorSpaceRetain(void* cs) {
    return finn_objc_retain(static_cast<ObjcId>(cs));
}

void shim_CGColorSpaceRelease(void* cs) {
    finn_objc_release(static_cast<ObjcId>(cs));
}

std::size_t shim_CGColorSpaceGetNumberOfComponents(void* cs) {
    if (!cs) return 0;
    return static_cast<const CGColorSpaceObj*>(cs)->components;
}

// --- CGColor ---
void* shim_CGColorCreateSRGB(double r, double g, double b, double a) {
    auto* c = reinterpret_cast<CGColorObj*>(
        finn_objc_alloc_object(g_cls_cgcolor, sizeof(CGColorObj)));
    c->rgba[0] = r;
    c->rgba[1] = g;
    c->rgba[2] = b;
    c->rgba[3] = a;
    return c;
}

const double* shim_CGColorGetComponents(void* color) {
    if (!color) return nullptr;
    return static_cast<const CGColorObj*>(color)->rgba;
}

std::size_t shim_CGColorGetNumberOfComponents(void* color) {
    return color ? 4 : 0;
}

void* shim_CGColorRetain(void* color) {
    return finn_objc_retain(static_cast<ObjcId>(color));
}

void shim_CGColorRelease(void* color) {
    finn_objc_release(static_cast<ObjcId>(color));
}

// --- CGBitmapContext & CGContext rasterizer ---
void cgcontext_dtor(ObjcId obj) {
    auto* ctx = reinterpret_cast<CGContextObj*>(obj);
    delete ctx->owned_storage;
    ctx->owned_storage = nullptr;
    ctx->data = nullptr;
    finn_objc_release(ctx->color_space);
    ctx->color_space = nullptr;
    delete ctx->gstate_stack;
    ctx->gstate_stack = nullptr;
}

std::uint8_t float_to_u8(double v) {
    const double clamped = std::clamp(v, 0.0, 1.0);
    return static_cast<std::uint8_t>(std::lround(clamped * 255.0));
}

bool is_bgra_layout(std::uint32_t bitmap_info) {
    const std::uint32_t alpha_info = bitmap_info & 0x1fu;
    const std::uint32_t byte_order = bitmap_info & 0x7000u;
    const bool alpha_first = (alpha_info == 2u || alpha_info == 4u || alpha_info == 6u);
    return alpha_first && (byte_order == 0x2000u);  // kCGBitmapByteOrder32Little
}

bool is_opaque_alpha_layout(std::uint32_t bitmap_info) {
    const std::uint32_t alpha_info = bitmap_info & 0x1fu;
    return alpha_info == 5u || alpha_info == 6u;  // NoneSkipLast / NoneSkipFirst
}

void blend_pixel_rgba8(CGContextObj* ctx, std::size_t px, std::size_t py,
                       double sr, double sg, double sb, double sa) {
    if (!ctx || !ctx->data || px >= ctx->width || py >= ctx->height) return;
    const std::size_t row = ctx->height - 1 - py;
    std::uint8_t* p = ctx->data + row * ctx->bytes_per_row + px * 4;

    const bool bgra = is_bgra_layout(ctx->bitmap_info);
    const bool force_opaque = is_opaque_alpha_layout(ctx->bitmap_info);
    const double a = std::clamp(sa, 0.0, 1.0);

    std::uint8_t out_r = 0, out_g = 0, out_b = 0, out_a = 255;
    if (a >= 1.0) {
        out_r = float_to_u8(sr);
        out_g = float_to_u8(sg);
        out_b = float_to_u8(sb);
        out_a = 255;
    } else if (a > 0.0) {
        const double dr = static_cast<double>(bgra ? p[2] : p[0]) / 255.0;
        const double dg = static_cast<double>(p[1]) / 255.0;
        const double db = static_cast<double>(bgra ? p[0] : p[2]) / 255.0;
        const double da = static_cast<double>(p[3]) / 255.0;
        const double inv = 1.0 - a;
        out_r = float_to_u8(sr * a + dr * inv);
        out_g = float_to_u8(sg * a + dg * inv);
        out_b = float_to_u8(sb * a + db * inv);
        out_a = force_opaque ? 255 : float_to_u8(a + da * inv);
    } else {
        return;
    }

    if (bgra) {
        p[0] = out_b;
        p[1] = out_g;
        p[2] = out_r;
        p[3] = force_opaque ? 255 : out_a;
    } else {
        p[0] = out_r;
        p[1] = out_g;
        p[2] = out_b;
        p[3] = force_opaque ? 255 : out_a;
    }
}

void* shim_CGBitmapContextCreate(void* data, std::size_t width, std::size_t height,
                                 std::size_t bits_per_component, std::size_t bytes_per_row,
                                 void* color_space, std::uint32_t bitmap_info) {
    if (width == 0 || height == 0 || bits_per_component != 8) return nullptr;
    const std::size_t min_stride = width * 4;
    const std::size_t stride = (bytes_per_row == 0) ? min_stride : bytes_per_row;
    if (stride < min_stride) return nullptr;

    auto* ctx = reinterpret_cast<CGContextObj*>(
        finn_objc_alloc_object(g_cls_cgcontext, sizeof(CGContextObj)));
    ctx->width = width;
    ctx->height = height;
    ctx->bits_per_component = bits_per_component;
    ctx->bytes_per_row = stride;
    ctx->bitmap_info = bitmap_info;
    ctx->color_space = finn_objc_retain(static_cast<ObjcId>(color_space));
    ctx->gstate_stack = new std::vector<CGGState>(1);

    if (data) {
        ctx->data = static_cast<std::uint8_t*>(data);
        ctx->owned_storage = nullptr;
    } else {
        ctx->owned_storage = new std::vector<std::uint8_t>(stride * height, 0);
        ctx->data = ctx->owned_storage->data();
    }
    return ctx;
}

void* shim_CGBitmapContextGetData(void* ctx_ptr) {
    if (!ctx_ptr) return nullptr;
    return static_cast<CGContextObj*>(ctx_ptr)->data;
}

std::size_t shim_CGBitmapContextGetWidth(void* ctx_ptr) {
    if (!ctx_ptr) return 0;
    return static_cast<const CGContextObj*>(ctx_ptr)->width;
}

std::size_t shim_CGBitmapContextGetHeight(void* ctx_ptr) {
    if (!ctx_ptr) return 0;
    return static_cast<const CGContextObj*>(ctx_ptr)->height;
}

std::size_t shim_CGBitmapContextGetBytesPerRow(void* ctx_ptr) {
    if (!ctx_ptr) return 0;
    return static_cast<const CGContextObj*>(ctx_ptr)->bytes_per_row;
}

std::size_t shim_CGBitmapContextGetBitsPerComponent(void* ctx_ptr) {
    if (!ctx_ptr) return 0;
    return static_cast<const CGContextObj*>(ctx_ptr)->bits_per_component;
}

std::size_t shim_CGBitmapContextGetBitsPerPixel(void* ctx_ptr) {
    if (!ctx_ptr) return 0;
    return static_cast<const CGContextObj*>(ctx_ptr)->bits_per_component * 4;
}

void shim_CGContextSetRGBFillColor(void* ctx_ptr, double r, double g, double b, double a) {
    if (!ctx_ptr) return;
    auto* ctx = static_cast<CGContextObj*>(ctx_ptr);
    if (!ctx->gstate_stack || ctx->gstate_stack->empty()) return;
    auto& st = ctx->gstate_stack->back();
    st.fill_r = r;
    st.fill_g = g;
    st.fill_b = b;
    st.fill_a = a;
}

void shim_CGContextSetRGBStrokeColor(void* ctx_ptr, double r, double g, double b, double a) {
    if (!ctx_ptr) return;
    auto* ctx = static_cast<CGContextObj*>(ctx_ptr);
    if (!ctx->gstate_stack || ctx->gstate_stack->empty()) return;
    auto& st = ctx->gstate_stack->back();
    st.stroke_r = r;
    st.stroke_g = g;
    st.stroke_b = b;
    st.stroke_a = a;
}

void shim_CGContextSetFillColorWithColor(void* ctx_ptr, void* color_ptr) {
    if (!ctx_ptr || !color_ptr) return;
    const auto* c = static_cast<const CGColorObj*>(color_ptr);
    shim_CGContextSetRGBFillColor(ctx_ptr, c->rgba[0], c->rgba[1], c->rgba[2], c->rgba[3]);
}

void shim_CGContextSetStrokeColorWithColor(void* ctx_ptr, void* color_ptr) {
    if (!ctx_ptr || !color_ptr) return;
    const auto* c = static_cast<const CGColorObj*>(color_ptr);
    shim_CGContextSetRGBStrokeColor(ctx_ptr, c->rgba[0], c->rgba[1], c->rgba[2], c->rgba[3]);
}

void shim_CGContextSetAlpha(void* ctx_ptr, double alpha) {
    if (!ctx_ptr) return;
    auto* ctx = static_cast<CGContextObj*>(ctx_ptr);
    if (ctx->gstate_stack && !ctx->gstate_stack->empty()) {
        ctx->gstate_stack->back().alpha = std::clamp(alpha, 0.0, 1.0);
    }
}

void shim_CGContextSetLineWidth(void* ctx_ptr, double w) {
    if (!ctx_ptr) return;
    auto* ctx = static_cast<CGContextObj*>(ctx_ptr);
    if (ctx->gstate_stack && !ctx->gstate_stack->empty()) {
        ctx->gstate_stack->back().line_width = std::max(0.0, w);
    }
}

void shim_CGContextTranslateCTM(void* ctx_ptr, double tx, double ty) {
    if (!ctx_ptr) return;
    auto* ctx = static_cast<CGContextObj*>(ctx_ptr);
    if (ctx->gstate_stack && !ctx->gstate_stack->empty()) {
        ctx->gstate_stack->back().tx += tx;
        ctx->gstate_stack->back().ty += ty;
    }
}

bool clip_rect_to_ctx(const CGContextObj* ctx, CGRect rect,
                      std::size_t* x0_out, std::size_t* y0_out,
                      std::size_t* x1_out, std::size_t* y1_out) {
    if (!ctx || !ctx->data || shim_CGRectIsEmpty(rect)) return false;
    rect = shim_CGRectStandardize(rect);
    const auto& st = ctx->gstate_stack->back();
    const double rx0 = rect.origin.x + st.tx;
    const double ry0 = rect.origin.y + st.ty;
    const double rx1 = rx0 + rect.size.width;
    const double ry1 = ry0 + rect.size.height;

    const long ix0 = std::max<long>(0, static_cast<long>(std::floor(rx0)));
    const long iy0 = std::max<long>(0, static_cast<long>(std::floor(ry0)));
    const long ix1 = std::min<long>(static_cast<long>(ctx->width), static_cast<long>(std::ceil(rx1)));
    const long iy1 = std::min<long>(static_cast<long>(ctx->height), static_cast<long>(std::ceil(ry1)));
    if (ix0 >= ix1 || iy0 >= iy1) return false;

    *x0_out = static_cast<std::size_t>(ix0);
    *y0_out = static_cast<std::size_t>(iy0);
    *x1_out = static_cast<std::size_t>(ix1);
    *y1_out = static_cast<std::size_t>(iy1);
    return true;
}

void shim_CGContextFillRect(void* ctx_ptr, CGRect rect) {
    if (!ctx_ptr) return;
    auto* ctx = static_cast<CGContextObj*>(ctx_ptr);
    if (!ctx->gstate_stack || ctx->gstate_stack->empty()) return;
    std::size_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!clip_rect_to_ctx(ctx, rect, &x0, &y0, &x1, &y1)) return;
    const auto& st = ctx->gstate_stack->back();
    const double eff_a = st.fill_a * st.alpha;
    for (std::size_t py = y0; py < y1; ++py) {
        for (std::size_t px = x0; px < x1; ++px) {
            blend_pixel_rgba8(ctx, px, py, st.fill_r, st.fill_g, st.fill_b, eff_a);
        }
    }
}

void shim_CGContextClearRect(void* ctx_ptr, CGRect rect) {
    if (!ctx_ptr) return;
    auto* ctx = static_cast<CGContextObj*>(ctx_ptr);
    if (!ctx->gstate_stack || ctx->gstate_stack->empty()) return;
    std::size_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!clip_rect_to_ctx(ctx, rect, &x0, &y0, &x1, &y1)) return;
    for (std::size_t py = y0; py < y1; ++py) {
        const std::size_t row = ctx->height - 1 - py;
        std::memset(ctx->data + row * ctx->bytes_per_row + x0 * 4, 0, (x1 - x0) * 4);
    }
}

void shim_CGContextStrokeRect(void* ctx_ptr, CGRect rect) {
    if (!ctx_ptr) return;
    auto* ctx = static_cast<CGContextObj*>(ctx_ptr);
    if (!ctx->gstate_stack || ctx->gstate_stack->empty()) return;
    std::size_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!clip_rect_to_ctx(ctx, rect, &x0, &y0, &x1, &y1)) return;
    const auto& st = ctx->gstate_stack->back();
    const double eff_a = st.stroke_a * st.alpha;
    const std::size_t lw = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(st.line_width)));
    for (std::size_t py = y0; py < y1; ++py) {
        for (std::size_t px = x0; px < x1; ++px) {
            const bool border = (px < x0 + lw) || (px + lw >= x1) ||
                                (py < y0 + lw) || (py + lw >= y1);
            if (border) {
                blend_pixel_rgba8(ctx, px, py, st.stroke_r, st.stroke_g, st.stroke_b, eff_a);
            }
        }
    }
}

void shim_CGContextSaveGState(void* ctx_ptr) {
    if (!ctx_ptr) return;
    auto* ctx = static_cast<CGContextObj*>(ctx_ptr);
    if (ctx->gstate_stack && !ctx->gstate_stack->empty()) {
        ctx->gstate_stack->push_back(ctx->gstate_stack->back());
    }
}

void shim_CGContextRestoreGState(void* ctx_ptr) {
    if (!ctx_ptr) return;
    auto* ctx = static_cast<CGContextObj*>(ctx_ptr);
    if (ctx->gstate_stack && ctx->gstate_stack->size() > 1) {
        ctx->gstate_stack->pop_back();
    }
}

void* shim_CGContextRetain(void* ctx_ptr) {
    return finn_objc_retain(static_cast<ObjcId>(ctx_ptr));
}

void shim_CGContextRelease(void* ctx_ptr) {
    finn_objc_release(static_cast<ObjcId>(ctx_ptr));
}

// --- QuartzCore CALayer ---
void calayer_dtor(ObjcId obj) {
    auto* l = reinterpret_cast<CALayerObj*>(obj);
    finn_objc_release(l->background_color);
    l->background_color = nullptr;
    if (l->sublayers) {
        for (ObjcId sub : *l->sublayers) {
            reinterpret_cast<CALayerObj*>(sub)->superlayer = nullptr;
            finn_objc_release(sub);
        }
        delete l->sublayers;
        l->sublayers = nullptr;
    }
}

ObjcId calayer_init(ObjcId self, ObjcSel) {
    auto* l = reinterpret_cast<CALayerObj*>(self);
    l->opacity = 1.0f;
    l->corner_radius = 0.0;
    l->hidden = false;
    l->masks_to_bounds = false;
    if (!l->sublayers) l->sublayers = new std::vector<ObjcId>();
    return self;
}

ObjcId calayer_layer(ObjcId, ObjcSel) {
    ObjcId obj = finn_objc_alloc_object(g_cls_calayer, sizeof(CALayerObj));
    return calayer_init(obj, nullptr);
}

CGRect calayer_frame(ObjcId self, ObjcSel) {
    return reinterpret_cast<const CALayerObj*>(self)->frame;
}

void calayer_setFrame(ObjcId self, ObjcSel, CGRect f) {
    auto* l = reinterpret_cast<CALayerObj*>(self);
    l->frame = f;
    l->bounds = {{0.0, 0.0}, f.size};
}

CGRect calayer_bounds(ObjcId self, ObjcSel) {
    return reinterpret_cast<const CALayerObj*>(self)->bounds;
}

void calayer_setBounds(ObjcId self, ObjcSel, CGRect b) {
    reinterpret_cast<CALayerObj*>(self)->bounds = b;
}

void* calayer_backgroundColor(ObjcId self, ObjcSel) {
    return reinterpret_cast<const CALayerObj*>(self)->background_color;
}

void calayer_setBackgroundColor(ObjcId self, ObjcSel, void* color) {
    auto* l = reinterpret_cast<CALayerObj*>(self);
    ObjcId next = static_cast<ObjcId>(color);
    if (l->background_color == next) return;
    finn_objc_retain(next);
    finn_objc_release(l->background_color);
    l->background_color = next;
}

double calayer_cornerRadius(ObjcId self, ObjcSel) {
    return reinterpret_cast<const CALayerObj*>(self)->corner_radius;
}

void calayer_setCornerRadius(ObjcId self, ObjcSel, double r) {
    reinterpret_cast<CALayerObj*>(self)->corner_radius = r;
}

float calayer_opacity(ObjcId self, ObjcSel) {
    return reinterpret_cast<const CALayerObj*>(self)->opacity;
}

void calayer_setOpacity(ObjcId self, ObjcSel, float op) {
    reinterpret_cast<CALayerObj*>(self)->opacity = op;
}

signed char calayer_isHidden(ObjcId self, ObjcSel) {
    return reinterpret_cast<const CALayerObj*>(self)->hidden ? 1 : 0;
}

void calayer_setHidden(ObjcId self, ObjcSel, signed char h) {
    reinterpret_cast<CALayerObj*>(self)->hidden = (h != 0);
}

signed char calayer_masksToBounds(ObjcId self, ObjcSel) {
    return reinterpret_cast<const CALayerObj*>(self)->masks_to_bounds ? 1 : 0;
}

void calayer_setMasksToBounds(ObjcId self, ObjcSel, signed char m) {
    reinterpret_cast<CALayerObj*>(self)->masks_to_bounds = (m != 0);
}

ObjcId calayer_superlayer(ObjcId self, ObjcSel) {
    return reinterpret_cast<const CALayerObj*>(self)->superlayer;
}

ObjcId calayer_sublayers(ObjcId self, ObjcSel) {
    auto* l = reinterpret_cast<const CALayerObj*>(self);
    ObjcClass* arr_cls = finn_objc_getClass("NSMutableArray");
    ObjcId arr = objc_send_name<ObjcId>(reinterpret_cast<ObjcId>(arr_cls), "array");
    if (l->sublayers) {
        for (ObjcId sub : *l->sublayers) {
            objc_send_name<void>(arr, "addObject:", sub);
        }
    }
    return finn_objc_autorelease(arr);
}

void calayer_addSublayer(ObjcId self, ObjcSel, ObjcId sub) {
    if (!self || !sub) return;
    auto* parent = reinterpret_cast<CALayerObj*>(self);
    auto* child = reinterpret_cast<CALayerObj*>(sub);
    if (!parent->sublayers) parent->sublayers = new std::vector<ObjcId>();
    parent->sublayers->push_back(finn_objc_retain(sub));
    child->superlayer = self;
}

void calayer_removeFromSuperlayer(ObjcId self, ObjcSel) {
    if (!self) return;
    auto* child = reinterpret_cast<CALayerObj*>(self);
    if (!child->superlayer) return;
    auto* parent = reinterpret_cast<CALayerObj*>(child->superlayer);
    child->superlayer = nullptr;
    if (parent->sublayers) {
        auto it = std::find(parent->sublayers->begin(), parent->sublayers->end(), self);
        if (it != parent->sublayers->end()) {
            parent->sublayers->erase(it);
            finn_objc_release(self);
        }
    }
}

void calayer_setNeedsDisplay(ObjcId, ObjcSel) {}

}  // namespace

void init_coregraphics() {
    if (g_cls_calayer) return;

    ObjcClass* nsobj = finn_objc_getClass("NSObject");

    g_cls_cgcolorspace = finn_objc_allocateClassPair(nsobj, "__CGColorSpace", sizeof(CGColorSpaceObj));
    g_cls_cgcolorspace->cpp_destructor = &cgcolorspace_dtor;
    finn_objc_registerClassPair(g_cls_cgcolorspace);

    g_cls_cgcolor = finn_objc_allocateClassPair(nsobj, "__CGColor", sizeof(CGColorObj));
    finn_objc_registerClassPair(g_cls_cgcolor);

    g_cls_cgcontext = finn_objc_allocateClassPair(nsobj, "__CGContext", sizeof(CGContextObj));
    g_cls_cgcontext->cpp_destructor = &cgcontext_dtor;
    finn_objc_registerClassPair(g_cls_cgcontext);

    g_cls_calayer = finn_objc_allocateClassPair(nsobj, "CALayer", sizeof(CALayerObj));
    g_cls_calayer->cpp_destructor = &calayer_dtor;
    finn_class_addClassMethod(g_cls_calayer, finn_sel_registerName("layer"), to_imp(&calayer_layer));
    finn_class_addMethod(g_cls_calayer, finn_sel_registerName("init"), to_imp(&calayer_init));
    finn_class_addMethod(g_cls_calayer, finn_sel_registerName("frame"), to_imp(&calayer_frame));
    finn_class_addMethod(g_cls_calayer, finn_sel_registerName("setFrame:"), to_imp(&calayer_setFrame));
    finn_class_addMethod(g_cls_calayer, finn_sel_registerName("bounds"), to_imp(&calayer_bounds));
    finn_class_addMethod(g_cls_calayer, finn_sel_registerName("setBounds:"), to_imp(&calayer_setBounds));
    finn_class_addMethod(g_cls_calayer, finn_sel_registerName("backgroundColor"), to_imp(&calayer_backgroundColor));
    finn_class_addMethod(g_cls_calayer, finn_sel_registerName("setBackgroundColor:"), to_imp(&calayer_setBackgroundColor));
    finn_class_addMethod(g_cls_calayer, finn_sel_registerName("cornerRadius"), to_imp(&calayer_cornerRadius));
    finn_class_addMethod(g_cls_calayer, finn_sel_registerName("setCornerRadius:"), to_imp(&calayer_setCornerRadius));
    finn_class_addMethod(g_cls_calayer, finn_sel_registerName("opacity"), to_imp(&calayer_opacity));
    finn_class_addMethod(g_cls_calayer, finn_sel_registerName("setOpacity:"), to_imp(&calayer_setOpacity));
    finn_class_addMethod(g_cls_calayer, finn_sel_registerName("isHidden"), to_imp(&calayer_isHidden));
    finn_class_addMethod(g_cls_calayer, finn_sel_registerName("setHidden:"), to_imp(&calayer_setHidden));
    finn_class_addMethod(g_cls_calayer, finn_sel_registerName("masksToBounds"), to_imp(&calayer_masksToBounds));
    finn_class_addMethod(g_cls_calayer, finn_sel_registerName("setMasksToBounds:"), to_imp(&calayer_setMasksToBounds));
    finn_class_addMethod(g_cls_calayer, finn_sel_registerName("superlayer"), to_imp(&calayer_superlayer));
    finn_class_addMethod(g_cls_calayer, finn_sel_registerName("sublayers"), to_imp(&calayer_sublayers));
    finn_class_addMethod(g_cls_calayer, finn_sel_registerName("addSublayer:"), to_imp(&calayer_addSublayer));
    finn_class_addMethod(g_cls_calayer, finn_sel_registerName("removeFromSuperlayer"), to_imp(&calayer_removeFromSuperlayer));
    finn_class_addMethod(g_cls_calayer, finn_sel_registerName("setNeedsDisplay"), to_imp(&calayer_setNeedsDisplay));
    finn_objc_registerClassPair(g_cls_calayer);

    g_cs_name_srgb = nsstring_from_utf8("kCGColorSpaceSRGB");
    g_cs_name_generic_rgb = nsstring_from_utf8("kCGColorSpaceGenericRGB");
    g_cs_name_device_rgb = nsstring_from_utf8("kCGColorSpaceDeviceRGB");
    g_cs_name_display_p3 = nsstring_from_utf8("kCGColorSpaceDisplayP3");
}

void register_coregraphics_symbols(std::unordered_map<std::string, void*>* table) {
    // Geometry constants & functions
    (*table)["_CGPointZero"] = &g_cg_point_zero;
    (*table)["_CGSizeZero"] = &g_cg_size_zero;
    (*table)["_CGRectZero"] = &g_cg_rect_zero;
    (*table)["_CGRectNull"] = &g_cg_rect_null;
    (*table)["_CGRectInfinite"] = &g_cg_rect_infinite;
    (*table)["_kCGColorSpaceSRGB"] = &g_cs_name_srgb;
    (*table)["_kCGColorSpaceGenericRGB"] = &g_cs_name_generic_rgb;
    (*table)["_kCGColorSpaceDeviceRGB"] = &g_cs_name_device_rgb;
    (*table)["_kCGColorSpaceDisplayP3"] = &g_cs_name_display_p3;

    (*table)["_CGRectIsEmpty"] = to_ptr(&shim_CGRectIsEmpty);
    (*table)["_CGRectIsNull"] = to_ptr(&shim_CGRectIsNull);
    (*table)["_CGRectStandardize"] = to_ptr(&shim_CGRectStandardize);
    (*table)["_CGRectEqualToRect"] = to_ptr(&shim_CGRectEqualToRect);
    (*table)["_CGRectIntersection"] = to_ptr(&shim_CGRectIntersection);
    (*table)["_CGRectUnion"] = to_ptr(&shim_CGRectUnion);
    (*table)["_CGRectContainsPoint"] = to_ptr(&shim_CGRectContainsPoint);
    (*table)["_CGRectContainsRect"] = to_ptr(&shim_CGRectContainsRect);
    (*table)["_CGRectIntersectsRect"] = to_ptr(&shim_CGRectIntersectsRect);
    (*table)["_CGRectInset"] = to_ptr(&shim_CGRectInset);
    (*table)["_CGRectOffset"] = to_ptr(&shim_CGRectOffset);
    (*table)["_CGRectGetMinX"] = to_ptr(&shim_CGRectGetMinX);
    (*table)["_CGRectGetMidX"] = to_ptr(&shim_CGRectGetMidX);
    (*table)["_CGRectGetMaxX"] = to_ptr(&shim_CGRectGetMaxX);
    (*table)["_CGRectGetMinY"] = to_ptr(&shim_CGRectGetMinY);
    (*table)["_CGRectGetMidY"] = to_ptr(&shim_CGRectGetMidY);
    (*table)["_CGRectGetMaxY"] = to_ptr(&shim_CGRectGetMaxY);
    (*table)["_CGRectGetWidth"] = to_ptr(&shim_CGRectGetWidth);
    (*table)["_CGRectGetHeight"] = to_ptr(&shim_CGRectGetHeight);

    // ColorSpace & Color
    (*table)["_CGColorSpaceCreateDeviceRGB"] = to_ptr(&shim_CGColorSpaceCreateDeviceRGB);
    (*table)["_CGColorSpaceCreateWithName"] = to_ptr(&shim_CGColorSpaceCreateWithName);
    (*table)["_CGColorSpaceRetain"] = to_ptr(&shim_CGColorSpaceRetain);
    (*table)["_CGColorSpaceRelease"] = to_ptr(&shim_CGColorSpaceRelease);
    (*table)["_CGColorSpaceGetNumberOfComponents"] = to_ptr(&shim_CGColorSpaceGetNumberOfComponents);
    (*table)["_CGColorCreateSRGB"] = to_ptr(&shim_CGColorCreateSRGB);
    (*table)["_CGColorCreateGenericRGB"] = to_ptr(&shim_CGColorCreateSRGB);
    (*table)["_CGColorGetComponents"] = to_ptr(&shim_CGColorGetComponents);
    (*table)["_CGColorGetNumberOfComponents"] = to_ptr(&shim_CGColorGetNumberOfComponents);
    (*table)["_CGColorRetain"] = to_ptr(&shim_CGColorRetain);
    (*table)["_CGColorRelease"] = to_ptr(&shim_CGColorRelease);

    // BitmapContext & CGContext drawing
    (*table)["_CGBitmapContextCreate"] = to_ptr(&shim_CGBitmapContextCreate);
    (*table)["_CGBitmapContextGetData"] = to_ptr(&shim_CGBitmapContextGetData);
    (*table)["_CGBitmapContextGetWidth"] = to_ptr(&shim_CGBitmapContextGetWidth);
    (*table)["_CGBitmapContextGetHeight"] = to_ptr(&shim_CGBitmapContextGetHeight);
    (*table)["_CGBitmapContextGetBytesPerRow"] = to_ptr(&shim_CGBitmapContextGetBytesPerRow);
    (*table)["_CGBitmapContextGetBitsPerComponent"] = to_ptr(&shim_CGBitmapContextGetBitsPerComponent);
    (*table)["_CGBitmapContextGetBitsPerPixel"] = to_ptr(&shim_CGBitmapContextGetBitsPerPixel);
    (*table)["_CGContextSetRGBFillColor"] = to_ptr(&shim_CGContextSetRGBFillColor);
    (*table)["_CGContextSetRGBStrokeColor"] = to_ptr(&shim_CGContextSetRGBStrokeColor);
    (*table)["_CGContextSetFillColorWithColor"] = to_ptr(&shim_CGContextSetFillColorWithColor);
    (*table)["_CGContextSetStrokeColorWithColor"] = to_ptr(&shim_CGContextSetStrokeColorWithColor);
    (*table)["_CGContextSetAlpha"] = to_ptr(&shim_CGContextSetAlpha);
    (*table)["_CGContextSetLineWidth"] = to_ptr(&shim_CGContextSetLineWidth);
    (*table)["_CGContextTranslateCTM"] = to_ptr(&shim_CGContextTranslateCTM);
    (*table)["_CGContextFillRect"] = to_ptr(&shim_CGContextFillRect);
    (*table)["_CGContextClearRect"] = to_ptr(&shim_CGContextClearRect);
    (*table)["_CGContextStrokeRect"] = to_ptr(&shim_CGContextStrokeRect);
    (*table)["_CGContextSaveGState"] = to_ptr(&shim_CGContextSaveGState);
    (*table)["_CGContextRestoreGState"] = to_ptr(&shim_CGContextRestoreGState);
    (*table)["_CGContextRetain"] = to_ptr(&shim_CGContextRetain);
    (*table)["_CGContextRelease"] = to_ptr(&shim_CGContextRelease);
}

}  // namespace finn::darwin

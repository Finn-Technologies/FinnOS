#ifndef FINNGPU_H
#define FINNGPU_H

#include <stdint.h>

/*
 * finngpu — the accelerated half of the FinnOS compositor.
 *
 * The hand-written CPU path works and stays: surfaces are rasterised into
 * buffers and memcpy'd into a dumb scanout buffer. That is a fine fallback
 * and a poor default. Almost all of what the shell draws is static from
 * frame to frame — a wallpaper, a dock, a panel — and the only per-frame
 * work is pasting them together, which is exactly what a texture unit is
 * for. Compositing on the GPU also makes the transform in a Control Center
 * open free, where the CPU path pays a bilinear resample of a third of a
 * megapixel per frame to do the same thing.
 *
 * Nothing here is required. If GBM or EGL is missing, or the driver has no
 * scanout-capable buffer, finngpu_open() fails with a reason and the
 * compositor keeps the software path. The capability is probed, never
 * assumed, and the renderer string it reports is the one the driver gave.
 *
 * Pixels are 0xAARRGGBB in memory — GBM_FORMAT_XRGB8888 — which is why the
 * fragment shader swaps the red and blue channels it reads.
 */

typedef struct finngpu finngpu;

/* Bring up GBM, EGL and a GLES2 context on the given DRM node, and create a
 * scanout-capable window surface. Returns NULL on any failure, having said
 * why on stderr. */
finngpu *finngpu_open(int drm_fd, int w, int h);
void     finngpu_close(finngpu *g);

const char *finngpu_renderer(const finngpu *g);

/* Textures hold XRGB8888 layers; the alpha channel is real for the chrome.
 * tex_update reallocates when the layer's size has changed, so a caller can
 * re-upload on a revision counter alone and never track dimensions. */
unsigned finngpu_tex(finngpu *g, const uint32_t *px, int w, int h);
void     finngpu_tex_update(finngpu *g, unsigned tex, const uint32_t *px,
                            int w, int h);
void     finngpu_tex_free(finngpu *g, unsigned tex);

/* One frame: begin, paste, end. */
int  finngpu_begin(finngpu *g);
void finngpu_paste(finngpu *g, unsigned tex,
                   float dx, float dy, float dw, float dh, float alpha);
/* The same paste with the closing dissolve: a warm, noisy disintegration. */
void finngpu_paste_dissolve(finngpu *g, unsigned tex,
                            float dx, float dy, float dw, float dh,
                            float alpha, float dust);
/* For a texture that already holds display-order channels — an offscreen
 * target rendered by this same code — rather than an XRGB8888 layer. Such a
 * texture is also the other way up: a plain texture's first row is its bottom
 * one, while the scanout surface starts at the top. */
void finngpu_paste_raw(finngpu *g, unsigned tex,
                       float dx, float dy, float dw, float dh, float alpha);
/* Renders, swaps, and hands back a DRM framebuffer id ready for a page flip.
 * Returns 0 if the frame could not be produced. */
int  finngpu_end(finngpu *g, uint32_t *out_fb);

/* Drop the buffer that has just left the screen. Call after the flip that
 * replaced it has landed, which is also what paces the compositor. */
void finngpu_release(finngpu *g);

/*
 * An offscreen colour target, for the one thing a stack of layers cannot
 * express: crossfading two whole composites. Alpha over an opaque base is a
 * crossfade only if the new composite is a single image, and drawing its
 * layers one at a time at the fade's value is not the same picture. Bind the
 * target, draw the composite, bind 0, and paste the result with
 * finngpu_paste_raw(). Returns 0 on success.
 */
int  finngpu_fbo(finngpu *g, unsigned *out_fbo, unsigned *out_tex, int w, int h);
void finngpu_fbo_free(finngpu *g, unsigned fbo, unsigned tex);
void finngpu_target(finngpu *g, unsigned fbo);   /* 0 selects the screen */

/* Copy the surface just drawn into dst (0xAARRGGBB, g's full size). Call it
 * after the last paste but before the swap: that is the only moment the frame
 * exists on this side of the display. Returns 0 on success, -1 on failure. */
int  finngpu_readback(finngpu *g, uint32_t *dst);

#endif

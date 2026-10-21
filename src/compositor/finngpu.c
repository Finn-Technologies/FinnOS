/*
 * finngpu — the accelerated half of the FinnOS compositor.
 *
 * GBM for the buffers, EGL for the platform, GLES2 for the work. There is no
 * toolkit and no window system above this: the compositor allocates
 * scanout-capable buffers, draws into them, and hands the result to KMS as a
 * framebuffer. That is the whole pipeline.
 *
 * The frame is four textured quads — wallpaper, dock, panel, cursor. The
 * wallpaper is static, the chrome changes only when the shell changes, and the
 * pointer changes only its position, so a frame costs the GPU a few triangles
 * and the CPU nothing at all. This is also what makes the Control Center's
 * transform and the appearance dissolve free rather than expensive.
 *
 * Everything here is optional. If any of it fails, finngpu_open() says why
 * and returns NULL, and the compositor keeps its hand-written software path.
 */
#define _GNU_SOURCE
#include "finngpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

/* how many live textures finngpu will track the size of at once */
#define FINNGPU_TEX_MAX 16

typedef struct { unsigned id; int w, h; } gpu_texinfo;

/*
 * Layers are 0xAARRGGBB words — on a little-endian machine, bytes B,G,R,A. A
 * GL_RGBA upload reads them byte for byte, so what lands in the sampler's .r
 * is the source's blue. Writing that straight back out would swap the
 * channels; the fragment shader swaps them a second time instead, which costs
 * nothing and keeps the buffer format the one thing everything agrees on.
 */
static const char *VS_SRC =
    "attribute vec2 a_pos;\n"
    "uniform vec4 u_rect;\n"
    "uniform vec2 u_vp;\n"
    "uniform float u_flip;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = vec2(a_pos.x, mix(a_pos.y, 1.0 - a_pos.y, u_flip));\n"
    "    vec2 p = u_rect.xy + a_pos * u_rect.zw;\n"
    "    gl_Position = vec4(p.x / u_vp.x * 2.0 - 1.0,\n"
    "                       1.0 - p.y / u_vp.y * 2.0, 0.0, 1.0);\n"
    "}\n";

static const char *FS_SRC =
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "uniform float u_alpha;\n"
    "uniform float u_swap;\n"
    "uniform float u_dissolve;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    vec4 c = texture2D(u_tex, v_uv);\n"
    "    vec3 rgb = mix(c.rgb, c.bgr, u_swap);\n"
    "    if (u_dissolve > 0.001) {\n"
    "        float n = fract(sin(dot(floor(v_uv * 512.0), vec2(12.9898, 78.233))) * 43758.5453);\n"
    "        if (n < u_dissolve) discard;\n"
    "        float edge = clamp((n - u_dissolve) * 8.0, 0.0, 1.0);\n"
    "        rgb = mix(vec3(0.87, 0.63, 0.27), rgb, edge);\n"
    "    }\n"
    "    gl_FragColor = vec4(rgb, c.a * u_alpha);\n"
    "}\n";

struct finngpu {
    int drm_fd, w, h;

    struct gbm_device  *gbm;
    struct gbm_surface *surf;
    EGLDisplay          dpy;
    EGLContext          ctx;
    EGLSurface          esurf;

    unsigned prog, vbo;
    int      u_rect, u_vp, u_alpha, u_tex, u_swap, u_flip, u_dissolve;

    /* the sizes of the textures we handed out, since GLES2 will not say */
    gpu_texinfo tex_info[FINNGPU_TEX_MAX];

    /* The buffer that is on screen, and the one it replaced — which can only
     * be freed once the flip that replaced it has landed. */
    struct gbm_bo *cur_bo, *old_bo;
    uint32_t       cur_fb, old_fb;

    char renderer[128];
};

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        GLsizei n = 0;
        glGetShaderInfoLog(s, sizeof log, &n, log);
        fprintf(stderr, "finngpu: shader compile failed: %.*s\n", (int)n, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

/* ---------------- bring-up ---------------- */

finngpu *finngpu_open(int drm_fd, int w, int h) {
    finngpu *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    g->drm_fd = drm_fd;
    g->w = w;
    g->h = h;

    g->gbm = gbm_create_device(drm_fd);
    if (!g->gbm) {
        fprintf(stderr, "finngpu: gbm_create_device: %s\n", strerror(errno));
        goto fail;
    }

    /*
     * XRGB8888 matches the DRM framebuffer format the rest of the compositor
     * already speaks, so a buffer from here is interchangeable with one of the
     * dumb buffers the software path would have used.
     */
    g->surf = gbm_surface_create(g->gbm, w, h, GBM_FORMAT_XRGB8888,
                                 GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!g->surf) {
        fprintf(stderr, "finngpu: no scanout-capable surface %dx%d\n", w, h);
        goto fail;
    }

    PFNEGLGETPLATFORMDISPLAYEXTPROC get_dpy =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    g->dpy = get_dpy ? get_dpy(EGL_PLATFORM_GBM_KHR, g->gbm, NULL)
                     : eglGetDisplay((EGLNativeDisplayType)g->gbm);
    if (g->dpy == EGL_NO_DISPLAY) {
        fprintf(stderr, "finngpu: no EGL display on the GBM device\n");
        goto fail;
    }

    EGLint emaj = 0, emin = 0;
    if (!eglInitialize(g->dpy, &emaj, &emin)) {
        fprintf(stderr, "finngpu: eglInitialize failed (0x%x)\n", eglGetError());
        goto fail;
    }
    eglBindAPI(EGL_OPENGL_ES_API);

    /* Pick the config whose native visual is the format we are scanning out,
     * rather than trusting the first one offered. */
    EGLint cfg_attrs[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 0,
        EGL_NONE
    };
    EGLConfig cfgs[64];
    EGLint ncfg = 0;
    if (!eglChooseConfig(g->dpy, cfg_attrs, cfgs, 64, &ncfg) || ncfg < 1) {
        fprintf(stderr, "finngpu: no usable EGL config\n");
        goto fail;
    }
    EGLConfig cfg = NULL;
    for (EGLint i = 0; i < ncfg; i++) {
        EGLint vid = 0;
        if (eglGetConfigAttrib(g->dpy, cfgs[i], EGL_NATIVE_VISUAL_ID, &vid) &&
            (uint32_t)vid == GBM_FORMAT_XRGB8888) { cfg = cfgs[i]; break; }
    }
    if (!cfg) {
        fprintf(stderr, "finngpu: no EGL config matching XRGB8888 scanout\n");
        goto fail;
    }

    g->esurf = eglCreateWindowSurface(g->dpy, cfg, g->surf, NULL);
    if (g->esurf == EGL_NO_SURFACE) {
        fprintf(stderr, "finngpu: eglCreateWindowSurface failed (0x%x)\n", eglGetError());
        goto fail;
    }

    EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    g->ctx = eglCreateContext(g->dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
    if (g->ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "finngpu: eglCreateContext failed (0x%x)\n", eglGetError());
        goto fail;
    }
    if (!eglMakeCurrent(g->dpy, g->esurf, g->esurf, g->ctx)) {
        fprintf(stderr, "finngpu: eglMakeCurrent failed (0x%x)\n", eglGetError());
        goto fail;
    }
    /* The KMS page flip is what paces this, not the GL swap. */
    eglSwapInterval(g->dpy, 0);

    GLuint vs = compile(GL_VERTEX_SHADER, VS_SRC);
    GLuint fs = compile(GL_FRAGMENT_SHADER, FS_SRC);
    if (!vs || !fs) { if (vs) glDeleteShader(vs); if (fs) glDeleteShader(fs); goto fail; }

    g->prog = glCreateProgram();
    glAttachShader(g->prog, vs);
    glAttachShader(g->prog, fs);
    glBindAttribLocation(g->prog, 0, "a_pos");
    glLinkProgram(g->prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = 0;
    glGetProgramiv(g->prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512];
        GLsizei n = 0;
        glGetProgramInfoLog(g->prog, sizeof log, &n, log);
        fprintf(stderr, "finngpu: program link failed: %.*s\n", (int)n, log);
        goto fail;
    }
    g->u_rect  = glGetUniformLocation(g->prog, "u_rect");
    g->u_vp    = glGetUniformLocation(g->prog, "u_vp");
    g->u_alpha = glGetUniformLocation(g->prog, "u_alpha");
    g->u_tex   = glGetUniformLocation(g->prog, "u_tex");
    g->u_swap  = glGetUniformLocation(g->prog, "u_swap");
    g->u_flip  = glGetUniformLocation(g->prog, "u_flip");
    g->u_dissolve = glGetUniformLocation(g->prog, "u_dissolve");

    /* one unit quad, reused for every paste */
    static const float quad[8] = { 0,0, 1,0, 0,1, 1,1 };
    glGenBuffers(1, &g->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);

    const char *rend = (const char *)glGetString(GL_RENDERER);
    snprintf(g->renderer, sizeof g->renderer, "%s", rend ? rend : "unknown");

    fprintf(stderr, "finngpu: GBM+EGL %d.%d up — renderer %s\n", emaj, emin, g->renderer);
    return g;

fail:
    finngpu_close(g);
    return NULL;
}

void finngpu_close(finngpu *g) {
    if (!g) return;
    if (g->dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(g->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g->ctx   != EGL_NO_CONTEXT) eglDestroyContext(g->dpy, g->ctx);
        if (g->esurf != EGL_NO_SURFACE) eglDestroySurface(g->dpy, g->esurf);
        eglTerminate(g->dpy);
    }
    if (g->surf) gbm_surface_destroy(g->surf);
    if (g->gbm)  gbm_device_destroy(g->gbm);
    free(g);
}

const char *finngpu_renderer(const finngpu *g) { return g->renderer; }

/* ---------------- textures ---------------- */

static void tex_params(void) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

/*
 * The chrome layers change size when the screen or the density does, and an
 * upload that assumes otherwise would clip silently. GLES2 cannot be asked how
 * big a texture is, so the size is recorded here when the texture is made.
 */
static gpu_texinfo *tex_info(finngpu *g, unsigned id) {
    for (int i = 0; i < FINNGPU_TEX_MAX; i++)
        if (g->tex_info[i].id == id) return &g->tex_info[i];
    return NULL;
}

static void tex_remember(finngpu *g, unsigned id, int w, int h) {
    gpu_texinfo *slot = tex_info(g, id);
    if (slot) { slot->w = w; slot->h = h; return; }
    for (int i = 0; i < FINNGPU_TEX_MAX; i++)
        if (g->tex_info[i].id == 0) {
            g->tex_info[i].id = id; g->tex_info[i].w = w; g->tex_info[i].h = h;
            return;
        }
}

static void tex_forget(finngpu *g, unsigned id) {
    gpu_texinfo *slot = tex_info(g, id);
    if (slot) { slot->id = 0; slot->w = slot->h = 0; }
}

unsigned finngpu_tex(finngpu *g, const uint32_t *px, int w, int h) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    tex_params();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    tex_remember(g, (unsigned)t, w, h);
    return (unsigned)t;
}

void finngpu_tex_update(finngpu *g, unsigned tex, const uint32_t *px, int w, int h) {
    if (!tex) return;
    glBindTexture(GL_TEXTURE_2D, tex);
    gpu_texinfo *ti = tex_info(g, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (!ti || ti->w != w || ti->h != h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        tex_remember(g, tex, w, h);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    }
}

void finngpu_tex_free(finngpu *g, unsigned tex) {
    if (!tex) return;
    tex_forget(g, tex);
    GLuint t = (GLuint)tex;
    glDeleteTextures(1, &t);
}

/* ---------------- frame ---------------- */

int finngpu_begin(finngpu *g) {
    if (!eglMakeCurrent(g->dpy, g->esurf, g->esurf, g->ctx)) return 0;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g->w, g->h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    /* Layers carry straight alpha, which is what this blend expects. */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g->prog);
    glUniform2f(g->u_vp, (float)g->w, (float)g->h);
    glUniform1i(g->u_tex, 0);
    glBindBuffer(GL_ARRAY_BUFFER, g->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    return 1;
}

void finngpu_target(finngpu *g, unsigned fbo) {
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);
    glViewport(0, 0, g->w, g->h);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

/*
 * Read the whole surface back, in the same 0xAARRGGBB the rest of the
 * compositor speaks. Taken before the swap, which is the only moment the
 * frame exists on this side of the display — and the reason a capture on the
 * accelerated path is the real frame rather than a CPU reconstruction of it.
 * glReadPixels hands back the bottom row first, so the rows are reversed on
 * the way in.
 */
void finngpu_readback(finngpu *g, uint32_t *dst) {
    int w = g->w, h = g->h;
    uint8_t *tmp = malloc((size_t)w * h * 4);
    if (!tmp) return;
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
    for (int y = 0; y < h; y++) {
        const uint8_t *s = tmp + (size_t)(h - 1 - y) * w * 4;
        uint32_t *d = dst + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            uint32_t r = s[x * 4 + 0], gg = s[x * 4 + 1], b = s[x * 4 + 2];
            d[x] = 0xFF000000u | (r << 16) | (gg << 8) | b;
        }
    }
    free(tmp);
}

int finngpu_fbo(finngpu *g, unsigned *out_fbo, unsigned *out_tex, int w, int h) {
    GLuint t = 0, f = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    tex_params();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glGenFramebuffers(1, &f);
    glBindFramebuffer(GL_FRAMEBUFFER, f);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (st != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "finngpu: no offscreen target (0x%x)\n", st);
        glDeleteFramebuffers(1, &f);
        glDeleteTextures(1, &t);
        return -1;
    }
    tex_remember(g, (unsigned)t, w, h);
    *out_fbo = (unsigned)f;
    *out_tex = (unsigned)t;
    return 0;
}

void finngpu_fbo_free(finngpu *g, unsigned fbo, unsigned tex) {
    if (fbo) { GLuint f = (GLuint)fbo; glDeleteFramebuffers(1, &f); }
    if (tex) { tex_forget(g, tex); GLuint t = (GLuint)tex; glDeleteTextures(1, &t); }
}

static void paste(finngpu *g, unsigned tex, float dx, float dy,
                  float dw, float dh, float alpha, int raw, float dissolve) {
    if (!tex || alpha <= 0.002f || dw <= 0.0f || dh <= 0.0f) return;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1f(g->u_alpha, alpha > 1.0f ? 1.0f : alpha);
    glUniform1f(g->u_swap, raw ? 0.0f : 1.0f);
    glUniform1f(g->u_dissolve, dissolve);
    /* A plain texture's first row is its bottom one, so an offscreen target
     * read back through the sampler is upside down unless it is flipped. The
     * scanout surface has a top-left origin and needs no such thing. */
    glUniform1f(g->u_flip, raw ? 1.0f : 0.0f);
    glUniform4f(g->u_rect, dx, dy, dw, dh);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void finngpu_paste(finngpu *g, unsigned tex,
                   float dx, float dy, float dw, float dh, float alpha) {
    paste(g, tex, dx, dy, dw, dh, alpha, 0, 0.0f);
}

void finngpu_paste_dissolve(finngpu *g, unsigned tex,
                            float dx, float dy, float dw, float dh,
                            float alpha, float dust) {
    paste(g, tex, dx, dy, dw, dh, alpha, 0, dust);
}

void finngpu_paste_raw(finngpu *g, unsigned tex,
                       float dx, float dy, float dw, float dh, float alpha) {
    paste(g, tex, dx, dy, dw, dh, alpha, 1, 0.0f);
}

/* A dma-buf backed BO has no GEM handle of its own, so the framebuffer is
 * built from its file descriptor; the handle path is the fallback. */
/*
 * GBM's own handle for the buffer: valid for as long as the buffer is, and not
 * ours to close.
 *
 * The other route — export a dma-buf with gbm_bo_get_fd(), import it with
 * drmPrimeFDToHandle(), then close the imported handle — is what every example
 * does, and it is unreliable here. On this driver gbm_bo_get_fd() starts
 * returning ENOENT for buffers it has already succeeded on a few frames
 * earlier, and the handle it hands over is the same GEM name the buffer itself
 * holds, so closing it leaves that buffer's own handle dangling. The result
 * was a framebuffer that could not be created and a compositor frozen on its
 * last good frame. One handle, borrowed rather than made, and no dma-buf.
 */
static uint32_t fb_for_bo(finngpu *g, struct gbm_bo *bo) {
    uint32_t handle = gbm_bo_get_handle(bo).u32;
    uint32_t handles[4] = { handle, 0, 0, 0 };
    uint32_t pitches[4] = { gbm_bo_get_stride(bo), 0, 0, 0 };
    uint32_t offsets[4] = { 0, 0, 0, 0 };
    uint32_t fb = 0;

    if (drmModeAddFB2(g->drm_fd, g->w, g->h, DRM_FORMAT_XRGB8888,
                      handles, pitches, offsets, &fb, 0) != 0) {
        static int said = 0;
        if (!said++)
            fprintf(stderr, "finngpu: no framebuffer for handle %u: %s\n",
                    handle, strerror(errno));
        return 0;
    }
    return fb;
}

int finngpu_end(finngpu *g, uint32_t *out_fb) {
    glFlush();
    if (!eglSwapBuffers(g->dpy, g->esurf)) {
        fprintf(stderr, "finngpu: eglSwapBuffers failed (0x%x)\n", eglGetError());
        return 0;
    }

    struct gbm_bo *bo = gbm_surface_lock_front_buffer(g->surf);
    if (!bo) { fprintf(stderr, "finngpu: no front buffer\n"); return 0; }

    uint32_t fb = fb_for_bo(g, bo);
    if (!fb) { gbm_surface_release_buffer(g->surf, bo); return 0; }

    /* the buffer that was current becomes the one to drop next round */
    g->old_bo = g->cur_bo;
    g->old_fb = g->cur_fb;
    g->cur_bo = bo;
    g->cur_fb = fb;

    *out_fb = fb;
    return 1;
}

void finngpu_release(finngpu *g) {
    if (g->old_fb) { drmModeRmFB(g->drm_fd, g->old_fb); g->old_fb = 0; }
    if (g->old_bo) { gbm_surface_release_buffer(g->surf, g->old_bo); g->old_bo = NULL; }
}

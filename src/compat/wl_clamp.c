// Minimal LD_PRELOAD compatibility shim for winewayland.drv on Qt6WaylandCompositor.
// Qt6WaylandCompositor advertises wl_data_device_manager v1; Wine 11's winewayland.drv
// binds v2 unconditionally, causing a fatal wl_display protocol error. This zero-overhead
// tail-call trampoline clamps only wl_registry_bind(wl_data_device_manager) to v1.

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>

struct wl_interface {
    const char *name;
};

__attribute__((visibility("hidden")))
void *finn_real_wl_proxy_marshal_flags = NULL;

__attribute__((constructor))
static void finn_init_wl_clamp(void) {
    finn_real_wl_proxy_marshal_flags = dlsym(RTLD_NEXT, "wl_proxy_marshal_flags");
}

__attribute__((visibility("hidden")))
int finn_should_clamp_wl(uint32_t opcode, const struct wl_interface *iface, uint32_t version) {
    return opcode == 0 && iface && iface->name && version > 1 &&
           strcmp(iface->name, "wl_data_device_manager") == 0;
}

#if defined(__aarch64__)
__asm__(
".global wl_proxy_marshal_flags\n"
".type wl_proxy_marshal_flags, %function\n"
"wl_proxy_marshal_flags:\n"
"  cbnz w1, .Lpassthrough\n"
"  cbz x2, .Lpassthrough\n"
"  cmp w3, #1\n"
"  bls .Lpassthrough\n"
"  stp x29, x30, [sp, #-96]!\n"
"  mov x29, sp\n"
"  stp x0, x1, [sp, #16]\n"
"  stp x2, x3, [sp, #32]\n"
"  stp x4, x5, [sp, #48]\n"
"  stp x6, x7, [sp, #64]\n"
"  mov w0, w1\n"
"  mov x1, x2\n"
"  mov w2, w3\n"
"  bl finn_should_clamp_wl\n"
"  mov w9, w0\n"
"  ldp x0, x1, [sp, #16]\n"
"  ldp x2, x3, [sp, #32]\n"
"  ldp x4, x5, [sp, #48]\n"
"  ldp x6, x7, [sp, #64]\n"
"  ldp x29, x30, [sp], #96\n"
"  cbz w9, .Lpassthrough\n"
"  mov w3, #1\n"
"  mov w7, #1\n"
".Lpassthrough:\n"
"  adrp x16, finn_real_wl_proxy_marshal_flags\n"
"  ldr x16, [x16, :lo12:finn_real_wl_proxy_marshal_flags]\n"
"  br x16\n"
);
#elif defined(__x86_64__)
__asm__(
".global wl_proxy_marshal_flags\n"
".type wl_proxy_marshal_flags, @function\n"
"wl_proxy_marshal_flags:\n"
"  testl %esi, %esi\n"
"  jnz .Lpassthrough64\n"
"  testq %rdx, %rdx\n"
"  jz .Lpassthrough64\n"
"  cmpl $1, %ecx\n"
"  jbe .Lpassthrough64\n"
"  pushq %rdi\n"
"  pushq %rsi\n"
"  pushq %rdx\n"
"  pushq %rcx\n"
"  pushq %r8\n"
"  pushq %r9\n"
"  movl %esi, %edi\n"
"  movq %rdx, %rsi\n"
"  movl %ecx, %edx\n"
"  call finn_should_clamp_wl\n"
"  movl %eax, %r10d\n"
"  popq %r9\n"
"  popq %r8\n"
"  popq %rcx\n"
"  popq %rdx\n"
"  popq %rsi\n"
"  popq %rdi\n"
"  testl %r10d, %r10d\n"
"  jz .Lpassthrough64\n"
"  movl $1, %ecx\n"
"  movl $1, 16(%rsp)\n"
".Lpassthrough64:\n"
"  movq finn_real_wl_proxy_marshal_flags(%rip), %r11\n"
"  jmp *%r11\n"
);
#endif

// Clean-room Objective-C message dispatch trampolines for aarch64 and x86_64.
// Preserves all integer, floating-point, SIMD, indirect-return, and stack
// arguments across method lookup, then tail-branches directly into the IMP.

#if defined(__APPLE__)
#define CSYM(name) _##name
#else
#define CSYM(name) name
#endif

.text

#if defined(__aarch64__)

.p2align 4
.globl CSYM(finn_objc_msgSend)
CSYM(finn_objc_msgSend):
    cbz     x0, L_msg_nil
    stp     x29, x30, [sp, #-224]!
    mov     x29, sp
    stp     x0, x1, [sp, #16]
    stp     x2, x3, [sp, #32]
    stp     x4, x5, [sp, #48]
    stp     x6, x7, [sp, #64]
    str     x8, [sp, #80]
    stp     q0, q1, [sp, #96]
    stp     q2, q3, [sp, #128]
    stp     q4, q5, [sp, #160]
    stp     q6, q7, [sp, #192]

    bl      CSYM(finn_objc_lookup_imp)
    mov     x16, x0

    ldp     q6, q7, [sp, #192]
    ldp     q4, q5, [sp, #160]
    ldp     q2, q3, [sp, #128]
    ldp     q0, q1, [sp, #96]
    ldr     x8, [sp, #80]
    ldp     x6, x7, [sp, #64]
    ldp     x4, x5, [sp, #48]
    ldp     x2, x3, [sp, #32]
    ldp     x0, x1, [sp, #16]
    ldp     x29, x30, [sp], #224

    cbz     x16, L_msg_nil
    br      x16

L_msg_nil:
    mov     x0, #0
    mov     x1, #0
    movi    v0.2d, #0
    movi    v1.2d, #0
    ret

.p2align 4
.globl CSYM(finn_objc_msgSendSuper2)
CSYM(finn_objc_msgSendSuper2):
    cbz     x0, L_msg_nil
    ldr     x9, [x0]
    cbz     x9, L_msg_nil

    stp     x29, x30, [sp, #-224]!
    mov     x29, sp
    stp     x0, x1, [sp, #16]
    stp     x2, x3, [sp, #32]
    stp     x4, x5, [sp, #48]
    stp     x6, x7, [sp, #64]
    str     x8, [sp, #80]
    stp     q0, q1, [sp, #96]
    stp     q2, q3, [sp, #128]
    stp     q4, q5, [sp, #160]
    stp     q6, q7, [sp, #192]

    bl      CSYM(finn_objc_lookup_super_imp)
    mov     x16, x0

    ldp     q6, q7, [sp, #192]
    ldp     q4, q5, [sp, #160]
    ldp     q2, q3, [sp, #128]
    ldp     q0, q1, [sp, #96]
    ldr     x8, [sp, #80]
    ldp     x6, x7, [sp, #64]
    ldp     x4, x5, [sp, #48]
    ldp     x2, x3, [sp, #32]
    ldp     x0, x1, [sp, #16]
    ldp     x29, x30, [sp], #224

    cbz     x16, L_msg_nil
    ldr     x0, [x0]
    br      x16

#elif defined(__x86_64__)

.p2align 4
.globl CSYM(finn_objc_msgSend)
.globl CSYM(finn_objc_msgSend_fpret)
CSYM(finn_objc_msgSend):
CSYM(finn_objc_msgSend_fpret):
    testq   %rdi, %rdi
    jz      L_msg_nil_x64
    pushq   %rbp
    movq    %rsp, %rbp
    subq    $192, %rsp
    movq    %rdi, 0(%rsp)
    movq    %rsi, 8(%rsp)
    movq    %rdx, 16(%rsp)
    movq    %rcx, 24(%rsp)
    movq    %r8,  32(%rsp)
    movq    %r9,  40(%rsp)
    movq    %rax, 48(%rsp)
    movdqu  %xmm0, 64(%rsp)
    movdqu  %xmm1, 80(%rsp)
    movdqu  %xmm2, 96(%rsp)
    movdqu  %xmm3, 112(%rsp)
    movdqu  %xmm4, 128(%rsp)
    movdqu  %xmm5, 144(%rsp)
    movdqu  %xmm6, 160(%rsp)
    movdqu  %xmm7, 176(%rsp)

    call    CSYM(finn_objc_lookup_imp)
    movq    %rax, %r11

    movdqu  176(%rsp), %xmm7
    movdqu  160(%rsp), %xmm6
    movdqu  144(%rsp), %xmm5
    movdqu  128(%rsp), %xmm4
    movdqu  112(%rsp), %xmm3
    movdqu  96(%rsp),  %xmm2
    movdqu  80(%rsp),  %xmm1
    movdqu  64(%rsp),  %xmm0
    movq    48(%rsp), %rax
    movq    40(%rsp), %r9
    movq    32(%rsp), %r8
    movq    24(%rsp), %rcx
    movq    16(%rsp), %rdx
    movq    8(%rsp),  %rsi
    movq    0(%rsp),  %rdi
    addq    $192, %rsp
    popq    %rbp

    testq   %r11, %r11
    jz      L_msg_nil_x64
    jmp     *%r11

L_msg_nil_x64:
    xorl    %eax, %eax
    xorl    %edx, %edx
    pxor    %xmm0, %xmm0
    pxor    %xmm1, %xmm1
    ret

.p2align 4
.globl CSYM(finn_objc_msgSendSuper2)
CSYM(finn_objc_msgSendSuper2):
    testq   %rdi, %rdi
    jz      L_msg_nil_x64
    cmpq    $0, (%rdi)
    jz      L_msg_nil_x64
    pushq   %rbp
    movq    %rsp, %rbp
    subq    $192, %rsp
    movq    %rdi, 0(%rsp)
    movq    %rsi, 8(%rsp)
    movq    %rdx, 16(%rsp)
    movq    %rcx, 24(%rsp)
    movq    %r8,  32(%rsp)
    movq    %r9,  40(%rsp)
    movq    %rax, 48(%rsp)
    movdqu  %xmm0, 64(%rsp)
    movdqu  %xmm1, 80(%rsp)
    movdqu  %xmm2, 96(%rsp)
    movdqu  %xmm3, 112(%rsp)
    movdqu  %xmm4, 128(%rsp)
    movdqu  %xmm5, 144(%rsp)
    movdqu  %xmm6, 160(%rsp)
    movdqu  %xmm7, 176(%rsp)

    call    CSYM(finn_objc_lookup_super_imp)
    movq    %rax, %r11

    movdqu  176(%rsp), %xmm7
    movdqu  160(%rsp), %xmm6
    movdqu  144(%rsp), %xmm5
    movdqu  128(%rsp), %xmm4
    movdqu  112(%rsp), %xmm3
    movdqu  96(%rsp),  %xmm2
    movdqu  80(%rsp),  %xmm1
    movdqu  64(%rsp),  %xmm0
    movq    48(%rsp), %rax
    movq    40(%rsp), %r9
    movq    32(%rsp), %r8
    movq    24(%rsp), %rcx
    movq    16(%rsp), %rdx
    movq    8(%rsp),  %rsi
    movq    0(%rsp),  %rdi
    addq    $192, %rsp
    popq    %rbp

    testq   %r11, %r11
    jz      L_msg_nil_x64
    movq    (%rdi), %rdi
    jmp     *%r11

#endif

#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",@progbits
#endif

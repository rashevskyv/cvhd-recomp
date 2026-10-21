// Our own user exception entry. libnx's __libnx_exception_entry never resumes the faulting
// instruction (it pivots to __libnx_exception_returnentry and ends in svcBreak). This one calls
// probe_fault(type, frame); when it returns 0 the thread resumes at the faulting instruction with
// the registers the kernel saved in the TLS frame (svcReturnFromException(0) copies that frame back).
// On entry: x0 = exception type, x1 = ThreadExceptionFrameA64* (in TLS); x0-x8, lr, sp, pc are in the
// frame, x9-x29 still hold the faulting thread's live values and must survive (switchbrew "SVC").
.section .text.__libnx_exception_entry, "ax", %progbits
.global __libnx_exception_entry
.type   __libnx_exception_entry, %function
.align 2
__libnx_exception_entry:
    cbz x1, 1f
    stp x29, x30, [sp, #-16]!
    stp x27, x28, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x17, x18, [sp, #-16]!
    stp x15, x16, [sp, #-16]!
    stp x13, x14, [sp, #-16]!
    stp x11, x12, [sp, #-16]!
    stp x9, x10, [sp, #-16]!
    sub sp, sp, #16
    bl probe_fault
    mov w19, w0
    add sp, sp, #16
    ldp x9, x10, [sp], #16
    ldp x11, x12, [sp], #16
    ldp x13, x14, [sp], #16
    ldp x15, x16, [sp], #16
    ldp x17, x18, [sp], #16
    mov w0, w19
    ldp x19, x20, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x27, x28, [sp], #16
    ldp x29, x30, [sp], #16
    cbnz w0, 1f
    mov w0, wzr
    bl svcReturnFromException
1:
    // Not ours: break so the crash report shows the registers.
    mov w0, wzr
    mov x1, xzr
    mov x2, xzr
    bl svcBreak
    b 1b

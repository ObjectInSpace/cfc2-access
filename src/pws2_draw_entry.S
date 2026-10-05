/* Windows x64 entry observer for the reviewed SSE-only native helper.
 * The original sees its own caller's untouched stack and return address.
 * FXSAVE64 also preserves MXCSR, x87 state and all sixteen XMM registers.
 * This does not promise preservation of AVX upper lanes or AVX-512 state.
 * Neither the pinned helper nor these baseline test builds uses AVX.
 */
.text
.globl pws2_draw_entry
.def pws2_draw_entry; .scl 2; .type 32; .endef
.seh_proc pws2_draw_entry
pws2_draw_entry:
    pushfq
    .seh_stackalloc 8
    pushq %rax
    .seh_pushreg %rax
    pushq %rcx
    .seh_pushreg %rcx
    pushq %rdx
    .seh_pushreg %rdx
    pushq %r8
    .seh_pushreg %r8
    pushq %r9
    .seh_pushreg %r9
    pushq %r10
    .seh_pushreg %r10
    pushq %r11
    .seh_pushreg %r11
    /* 32-byte shadow space, aligned 512-byte FX state, 8-byte alignment. */
    subq $0x228, %rsp
    .seh_stackalloc 0x228
    .seh_endprologue
    fxsave64 0x20(%rsp)
    /* RCX still holds the native matrix argument. */
    movq %rdi, %rdx
    movq %rbx, %r8
    movq 0x268(%rsp), %r9
    call pws2_draw_entry_observe
    fxrstor64 0x20(%rsp)
    addq $0x228, %rsp
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdx
    popq %rcx
    popq %rax
    popfq
    jmp *pws2_draw_entry_original(%rip)
.seh_endproc

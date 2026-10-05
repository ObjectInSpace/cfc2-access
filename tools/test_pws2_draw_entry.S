/* Synthetic caller/original; no game addresses are executed. */
.text
.globl pws2_test_call
.def pws2_test_call; .scl 2; .type 32; .endef
.seh_proc pws2_test_call
pws2_test_call:
    pushq %rbx
    .seh_pushreg %rbx
    pushq %rdi
    .seh_pushreg %rdi
    subq $0x28, %rsp
    .seh_stackalloc 0x28
    .seh_endprologue
    movq %rcx, %rdi
    movq %rdx, %rbx
    movq %r8, %rcx
    xorq %rax, %rax
    stc
    movq $0x11, %rax
    movq $0x33, %rdx
    movq $0x44, %r8
    movq $0x55, %r9
    movq $0x66, %r10
    movq $0x77, %r11
    movdqu pws2_test_xmm(%rip), %xmm0
    movdqu pws2_test_xmm+16(%rip), %xmm1
    movdqu pws2_test_xmm+32(%rip), %xmm2
    movdqu pws2_test_xmm+48(%rip), %xmm3
    movdqu pws2_test_xmm+64(%rip), %xmm4
    movdqu pws2_test_xmm+80(%rip), %xmm5
    fldl pws2_test_x87_input(%rip)
    call pws2_draw_entry
.globl pws2_test_after_call
pws2_test_after_call:
    fstpl pws2_test_x87_output(%rip)
    addq $0x28, %rsp
    popq %rdi
    popq %rbx
    ret
.seh_endproc

.globl pws2_test_original
.def pws2_test_original; .scl 2; .type 32; .endef
pws2_test_original:
    movq %rax, pws2_test_gpr(%rip)
    movq %rcx, pws2_test_gpr+8(%rip)
    movq %rdx, pws2_test_gpr+16(%rip)
    movq %r8, pws2_test_gpr+24(%rip)
    movq %r9, pws2_test_gpr+32(%rip)
    movq %r10, pws2_test_gpr+40(%rip)
    movq %r11, pws2_test_gpr+48(%rip)
    movq %rbx, pws2_test_gpr+56(%rip)
    movq %rdi, pws2_test_gpr+64(%rip)
    pushfq
    popq pws2_test_flags(%rip)
    movq (%rsp), %rax
    movq %rax, pws2_test_return(%rip)
    movdqu %xmm0, pws2_test_actual_xmm(%rip)
    movdqu %xmm1, pws2_test_actual_xmm+16(%rip)
    movdqu %xmm2, pws2_test_actual_xmm+32(%rip)
    movdqu %xmm3, pws2_test_actual_xmm+48(%rip)
    movdqu %xmm4, pws2_test_actual_xmm+64(%rip)
    movdqu %xmm5, pws2_test_actual_xmm+80(%rip)
    stmxcsr pws2_test_actual_mxcsr(%rip)
    fnstcw pws2_test_actual_x87_control(%rip)
    incl pws2_test_original_calls(%rip)
    movq $0x1234, %rax
    ret

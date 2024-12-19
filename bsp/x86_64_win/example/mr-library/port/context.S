.globl mr_hw_context_switch
mr_hw_context_switch:
    /* Save registers */
    leaq (%rsp), %rax
    movq %rax, 0(%rcx)                  /* RSP */
    movq %rbx, 8(%rcx)                  /* RBX */
    movq %rcx, 16(%rcx)                  /* RCX */
    movq %rdx, 24(%rcx)                  /* RDX */
    movq 0(%rax), %rax
    movq %rax, 32(%rcx)                  /* RIP */
    movq %rsi, 40(%rcx)                  /* RSI */
    movq %rdi, 48(%rcx)                  /* RDI */
    movq %rbp, 56(%rcx)                  /* RBP */
    movq %r8, 64(%rcx)                   /* R8 */
    movq %r9, 72(%rcx)                   /* R9 */
    movq %r12, 80(%rcx)                  /* R12 */
    movq %r13, 88(%rcx)                  /* R13 */
    movq %r14, 96(%rcx)                  /* R14 */
    movq %r15, 104(%rcx)                 /* R15 */
    xorq %rax, %rax

    /* Restore registers */
    movq 56(%rdx), %rbp                  /* RBP */
    movq 0(%rdx), %rsp                   /* RSP */
    movq 104(%rdx), %r15                 /* R15 */
    movq 96(%rdx), %r14                  /* R14 */
    movq 88(%rdx), %r13                  /* R13 */
    movq 80(%rdx), %r12                  /* R12 */
    movq 72(%rdx), %r9                   /* R9 */
    movq 64(%rdx), %r8                   /* R8 */
    movq 48(%rdx), %rdi                  /* RDI */
    movq 40(%rdx), %rsi                  /* RSI */
    movq 16(%rdx), %rcx                  /* RCX */
    movq 8(%rdx), %rbx                   /* RBX */
    leaq 8(%rsp), %rsp
    pushq 32(%rdx)                       /* RIP */
    movq 24(%rdx), %rdx                  /* RDX */
    ret

.globl mr_hw_context_switch_to
mr_hw_context_switch_to:
    /* Restore registers */
    movq 56(%rcx), %rbp                  /* RBP */
    movq 0(%rcx), %rsp                   /* RSP */
    movq 104(%rcx), %r15                 /* R15 */
    movq 96(%rcx), %r14                  /* R14 */
    movq 88(%rcx), %r13                  /* R13 */
    movq 80(%rcx), %r12                  /* R12 */
    movq 72(%rcx), %r9                   /* R9 */
    movq 64(%rcx), %r8                   /* R8 */
    movq 48(%rcx), %rdi                  /* RDI */
    movq 40(%rcx), %rsi                  /* RSI */
    movq 24(%rcx), %rdx                  /* RDX */
    movq 8(%rcx), %rbx                   /* RBX */
    leaq 8(%rsp), %rsp
    pushq 32(%rcx)                       /* RIP */
    movq 16(%rcx), %rcx                  /* RCX */
    ret

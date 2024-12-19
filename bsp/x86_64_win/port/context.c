#include <string.h>

typedef struct context {
    volatile void *rsp;
    volatile void *rbx;
    volatile void *rcx;
    volatile void *rdx;
    volatile void *rip;
    volatile void *rsi;
    volatile void *rdi;
    volatile void *rbp;
    volatile void *r8;
    volatile void *r9;
    volatile void *r12;
    volatile void *r13;
    volatile void *r14;
    volatile void *r15;
} context_t;

void mr_hw_cpu_relax(void) {
}

int mr_hw_irq_save(void) {
    return 0;
}

void mr_hw_irq_restore(int mask) {
    (void)mask;
}

void *mr_hw_stack_init(void *entry, void *stack_addr) {
    context_t *context;
    void *sp, **addr;

    /* Init context */
    context = (void *)((char *)stack_addr - sizeof(context_t));
    memset(context, 0, sizeof(context_t));

    /* Init SP */
    sp = (char *)context - sizeof(void *);
    addr = (void **)sp;
    *addr = (void *)entry;

    /* Init context */
    context->rsp = sp;
    context->rip = entry;
    return context;
}

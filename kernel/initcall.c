/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_initcall.h>
#include <libc/mr_atomic.h>

/* Kernel and user init status */
static mr_atomic_t __kstatus = MR_ATOMIC_INIT(MR_FALSE);
static mr_atomic_t __ustatus = MR_ATOMIC_INIT(MR_FALSE);

static void start(void) {
    /* User init start */
}
MR_INIT_EXPORT(start, "0.end");

static void end(void) {
    /* User init end */
}
MR_INIT_EXPORT(end, "9.end");

void mr_kernel_init(void) {
    mr_atomic_t inited;

    /* Check kernel init status */
    mr_atomic_init(&inited, MR_FALSE);
    if (!mr_atomic_compare_exchange(&__kstatus, &inited, MR_TRUE)) {
        /* Kernel already init */
        return;
    }

#if defined(MR_USE_MALLOC)
    /* Init malloc */
    extern void mr_libc_malloc_init(void);
    mr_libc_malloc_init();
#endif /* defined(MR_USE_MALLOC) */
#if defined(MR_USE_TIMER)
    /* Init timer */
    extern void mr_kernel_timer_init(void);
    mr_kernel_timer_init();
#endif /* defined(MR_USE_TIMER) */
#if defined(MR_USE_IRQ)
    /* Init irq */
    extern void mr_kernel_irq_init(void);
    mr_kernel_irq_init();
#endif /* defined(MR_USE_IRQ) */
}

void mr_user_init(void) {
    const mr_initcall_t *start = &__mr_initcall_start;
    const mr_initcall_t *end = &__mr_initcall_end;
    const mr_initcall_t *call;
    mr_atomic_t inited;

    /* Kernel */
    mr_kernel_init();

    /* Check user init status */
    mr_atomic_init(&inited, MR_FALSE);
    if (!mr_atomic_compare_exchange(&__ustatus, &inited, MR_TRUE)) {
        /* User already init */
        return;
    }

    /* Each init calls */
    for (call = start; call <= end; call++) {
        /* Call init call entry */
        call->entry();
    }
}

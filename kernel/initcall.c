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
static mr_atomic_t kstatus = MR_ATOMIC_INIT(MR_FALSE);
static mr_atomic_t ustatus = MR_ATOMIC_INIT(MR_FALSE);

MR_INLINE void start(void) {
}
MR_INIT_EXPORT(start, "0.end");

MR_INLINE void end(void) {
}
MR_INIT_EXPORT(end, "9.end");

void mr_kernel_init(void) {
    mr_atomic_t inited;

    /* Check kernel init status */
    mr_atomic_init(&inited, MR_FALSE);
    if (!mr_atomic_compare_exchange(&kstatus, &inited, MR_TRUE)) {
        /* Kernel already init */
        return;
    }

#if defined(MR_USE_KTIMER)
    /* Init ktimer */
    extern void mr_ktimer_kernel_init(void);
    mr_ktimer_kernel_init();
#endif /* defined(MR_USE_KTIMER) */
#if defined(MR_USE_KWORKQUEUE)
    /* Init kworkqueue */
    extern void mr_kworkqueue_kernel_init(void);
    mr_kworkqueue_kernel_init();
#endif /* defined(MR_USE_KWORKQUEUE) */
#if defined(MR_USE_IRQ)
    /* Init irq */
    extern void mr_irq_kernel_init(void);
    mr_irq_kernel_init();
#endif /* defined(MR_USE_IRQ) */
}

void mr_user_init(void) {
    const mr_initcall_t *call;
    mr_atomic_t inited;

    /* Kernel */
    mr_kernel_init();

    /* Check user init status */
    mr_atomic_init(&inited, MR_FALSE);
    if (!mr_atomic_compare_exchange(&ustatus, &inited, MR_TRUE)) {
        /* User already init */
        return;
    }

    /* Execute initcalls */
    for (call = &__mr_initcall_start; call <= &__mr_initcall_end; call++) {
        call->entry();
    }
}

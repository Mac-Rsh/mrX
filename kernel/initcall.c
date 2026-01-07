/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/kernel/mr_initcall.h>
#include <mrX/libc/mr_assert.h>
#include <mrX/libc/mr_types.h>

/* Kernel and user init status(atomics may be soft-implemented and not yet
 * initialize) */
static volatile mr_bool_t __initcall_kstatus = MR_FALSE;
static volatile mr_bool_t __initcall_ustatus = MR_FALSE;

static void start(void) {
    /* User init start */
}
MR_INIT_EXPORT(start, "0.end");

static void end(void) {
    /* User init end */
}
MR_INIT_EXPORT(end, "9.end");

void mr_kernel_init(void) {
    /* Check kernel init status */
    if (__initcall_kstatus) {
        /* Kernel already init */
        return;
    } else {
        __initcall_kstatus = MR_TRUE;
    }

#if defined(MR_USE_PLATFORM)
    /* Init platform */
    extern void mr_platform_init(void);
    mr_platform_init();
#endif /* defined(MR_USE_PLATFORM) */

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

    /* Check link address */
    MR_ASSERT(start < end);

    /* Kernel */
    mr_kernel_init();

    /* Check user init status */
    if (__initcall_ustatus) {
        /* User already init */
        return;
    } else {
        __initcall_ustatus = MR_TRUE;
    }

    /* Each init calls */
    for (call = start; call <= end; call++) {
        /* Call init call entry */
        call->entry();
    }
}

/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_atomic.h>
#include <kernel/mr_clock.h>
#include <kernel/mr_hooks.h>

static mr_atomic_t kclock;
#if !defined(MR_CFG_CLOCK_HOOK_SIZE)
#define MR_CFG_CLOCK_HOOK_SIZE          (4)
#endif /* !defined(MR_CFG_CLOCK_HOOK_SIZE) */
#if defined(MR_USE_THREAD) && (MR_CFG_CLOCK_HOOK_SIZE < 2)
#error "If use thread, clock hook size must >= 2"
#endif /* defined(MR_USE_THREAD) && (MR_CFG_CLOCK_HOOK_SIZE < 2) */
static mr_ptr_t khook[MR_CFG_CLOCK_HOOK_SIZE];
static mr_hooks_t khooks = MR_HOOKS_INIT(khook, MR_CFG_CLOCK_HOOK_SIZE);

MR_INLINE void clock_hooks_call(void) {
    void (*hook)(void);
    mr_size_t i;

    /* Each hooks */
    MR_HOOKS_EACH(i, &khooks, hook) {
        if (!hook) {
            continue;
        }

        /* Call hook */
        hook();
    }
}

void mr_clock_increase(mr_clock_t clock) {
    /* Clock increase */
    mr_atomic_fetch_add(&kclock, (mr_atomic_t)clock);

    /* Call hooks */
    clock_hooks_call();
}

mr_clock_t mr_clock_get(void) {
    /* Get kclock */
    return (mr_clock_t)mr_atomic_load(&kclock);
}

void mr_clock_set(mr_clock_t clock) {
    /* Set kclock */
    mr_atomic_store(&kclock, (mr_atomic_t)clock);
}

int mr_clock_hook_add(int index, void (*hook)(void)) {
    /* Add hook */
    return mr_hooks_add(&khooks, index, hook);
}

void mr_clock_hook_del(int index) {
    /* Delete hook */
    mr_hooks_del(&khooks, index);
}

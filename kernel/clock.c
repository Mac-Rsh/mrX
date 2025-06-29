/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mr-X/mr_clock.h>
#include <mr-X/mr_hook.h>
#include <libc/mr_assert.h>
#include <libc/mr_atomic.h>

/* Clock tick */
static mr_atomic_t __tick;
/* Clock hook entry */
#if !defined(MR_CFG_CLOCK_HOOK_SIZE)
#define MR_CFG_CLOCK_HOOK_SIZE          (2)
#endif /* !defined(MR_CFG_CLOCK_HOOK_SIZE) */
#if (MR_CFG_CLOCK_HOOK_SIZE < 2)
#error "MR_CFG_CLOCK_HOOK_SIZE must >= 2"
#endif /* (MR_CFG_CLOCK_HOOK_SIZE < 2) */
static mr_ptr_t __entry[MR_CFG_CLOCK_HOOK_SIZE];
/* Clock hook */
static mr_hook_t __hook = MR_HOOK_INIT(__entry, MR_CFG_CLOCK_HOOK_SIZE);

static mr_tick_t clock_hook(void) {
    mr_tick_t tickless, less;
    mr_clock_hook_t *entry;
    mr_size_t i;

    /* Init tickless */
    tickless = MR_TICK_MAX;

    /* Each clock hooks */
    MR_HOOK_EACH(i, entry, mr_clock_hook_t, &__hook) {
        if (!entry) {
            continue;
        }

        /* Call clock hook entry */
        less = entry();

        /* Update tickless */
        if ((less > 0) && (less < tickless)) {
            tickless = less;
        }
    }
    return tickless;
}

void mr_clock_increase(mr_tick_t tick) {
    /* Clock tick increase */
    mr_atomic_fetch_add(&__tick, (mr_atomic_t)tick);

    /* Call clock hooks */
    clock_hook();
}

mr_tick_t mr_clock_tick(void) {
    /* Get clock tick */
    return (mr_tick_t)mr_atomic_load(&__tick);
}

mr_tick_t mr_clock_tickless(void) {
    /* Get clock tickless */
    return clock_hook();
}

mr_err_t mr_clock_hook_add(mr_clock_hook_t *entry) {
    /* Check parameter */
    MR_ASSERT(entry != MR_NULL);

    /* Add hook to clock */
    return mr_hook_add(&__hook, entry);
}

/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_kclock.h>
#include <kernel/mr_khook.h>
#include <libc/mr_atomic.h>

static mr_atomic_t kclock;
#if !defined(MR_CFG_KCLOCK_HOOK_SIZE)
#define MR_CFG_KCLOCK_HOOK_SIZE         (8)
#endif /* !defined(MR_CFG_KCLOCK_HOOK_SIZE) */
#if defined(MR_USE_THREAD) && (MR_CFG_KCLOCK_HOOK_SIZE < 2)
#error "If use thread, clock hook size must >= 2"
#endif /* defined(MR_USE_THREAD) && (MR_CFG_KCLOCK_HOOK_SIZE < 2) */
static mr_ptr_t kentry[MR_CFG_KCLOCK_HOOK_SIZE];
static mr_khook_t khook = MR_KHOOK_INIT(kentry, MR_CFG_KCLOCK_HOOK_SIZE);

MR_INLINE void kclock_hook(void) {
    void (*entry)(void);
    mr_size_t i;

    /* Each hooks */
    MR_KHOOK_EACH(i, entry, &khook) {
        if (!entry) {
            continue;
        }

        /* Call entry */
        entry();
    }
}

void mr_kclock_increase(mr_tick_t tick) {
    /* Clock increase */
    mr_atomic_fetch_add(&kclock, (mr_atomic_t)tick);

    /* Call hooks */
    kclock_hook();
}

mr_tick_t mr_kclock_tick(void) {
    /* Get kclock */
    return (mr_tick_t)mr_atomic_load(&kclock);
}

mr_err_t mr_kclock_hook_add(void (*entry)(void)) {
    /* Add entry to khook */
    return mr_khook_add(&khook, entry);
}

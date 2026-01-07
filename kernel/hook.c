/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/kernel/mr_spinlock.h>
#include <mrX/kernel/mr_hook.h>
#include <mrX/libc/mr_assert.h>

/* Hook lock */
static mr_spinlock_t __hook_lock = MR_SPINLOCK_INIT();

mr_err_t mr_hook_add(mr_hook_t *hook, mr_ptr_t entry) {
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT(hook != MR_NULL);
    MR_ASSERT((hook->entry != MR_NULL) || (hook->size == 0));
    MR_ASSERT(entry != MR_NULL);

    /* Lock hook */
    mask = mr_spinlock_lock_irqsave(&__hook_lock);

    /* Add entry to hook */
    if (hook->next < hook->size) {
        hook->entry[hook->next++] = entry;
        ret = 0;
    } else {
        /* Out of space */
        ret = -MR_ENOMEM;
    }

    /* Unlock hook */
    mr_spinlock_unlock_irqrestore(&__hook_lock, mask);
    return ret;
}

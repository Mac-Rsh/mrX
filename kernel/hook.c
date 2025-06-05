/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_spinlock.h>
#include <kernel/mr_hook.h>
#include <libc/mr_assert.h>

/* Hook lock */
static mr_spinlock_t __lock = MR_SPINLOCK_INIT();

void mr_hook_init(mr_hook_t *hook, mr_ptr_t *entry, mr_size_t size) {
    /* Initialize hook */
    hook->size = entry ? size : 0;
    hook->entry = entry;
    hook->next = 0;
}

mr_err_t mr_hook_add(mr_hook_t *hook, mr_ptr_t entry) {
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT(entry != MR_NULL);

    /* Lock hook */
    mask = mr_spinlock_lock_irqsave(&__lock);

    /* Add entry to hook */
    if (hook->next < hook->size) {
        hook->entry[hook->next++] = entry;
        ret = 0;
    } else {
        /* Out of space */
        ret = -MR_ENOMEM;
    }

    /* Unlock hook */
    mr_spinlock_unlock_irqrestore(&__lock, mask);
    return ret;
}

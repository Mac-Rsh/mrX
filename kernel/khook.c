/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_khook.h>
#include <kernel/mr_kspinlock.h>

/* Khook lock */
static mr_kspinlock_t klock = MR_KSPINLOCK_INIT();

void mr_khook_init(mr_khook_t *khook, mr_ptr_t *entry, mr_size_t size) {
    /* Check arguments */
    if ((!khook) || ((!entry) && (size > 0))) {
        return;
    }

    /* Initialize khook */
    khook->entry = entry;
    khook->size = size;
    khook->next = 0;
}

mr_err_t mr_khook_add(mr_khook_t *khook, mr_ptr_t entry) {
    mr_err_t ret;
    int mask;

    /* Check arguments */
    if ((!khook) || (!entry)) {
        return -MR_EINVAL;
    }

    /* Lock khook */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Add entry to khook */
    if (khook->next < khook->size) {
        khook->entry[khook->next++] = entry;
        ret = 0;
    } else {
        /* Out of space */
        ret = -MR_ENOMEM;
    }

    /* Unlock khook */
    mr_kspinlock_unlock_irqrestore(&klock, mask);
    return ret;
}

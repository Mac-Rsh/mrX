/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_hooks.h>

void mr_hooks_init(mr_hooks_t *hooks, mr_ptr_t *hook, mr_size_t size) {
    /* Check arguments */
    if ((!hooks) || (!hook) || (!size)) {
        return;
    }

    /* Initialize hooks */
    mr_spinlock_init(&hooks->lock);
    hooks->hook = hook;
    hooks->size = size;
}

int mr_hooks_add(mr_hooks_t *hooks, int index, mr_ptr_t hook) {
    mr_size_t i;
    int ret;

    /* Check arguments */
    if ((!hooks) || (index < -1) || (index >= (int)hooks->size)) {
        return -MR_EINVAL;
    }

    /* Lock hooks */
    mr_spinlock_lock(&hooks->lock);

    /* Add hooks */
    if (index >= 0) {
        hooks->hook[index] = hook;
        ret = index;
        goto _exit;
    }

    /* Allocate hooks */
    for (i = 0; i < hooks->size; i++) {
        if (hooks->hook[i]) {
            continue;
        }

        /* Allocate success */
        hooks->hook[i] = hook;
        ret = (int)i;
        goto _exit;
    }

    /* Allocate fail */
    ret = -MR_ENOMEM;

_exit:
    /* Unlock hooks */
    mr_spinlock_unlock(&hooks->lock);
    return ret;
}

void mr_hooks_del(mr_hooks_t *hooks, int index) {
    /* Check arguments */
    if ((!hooks) || (index < 0) || (index >= (int)hooks->size)) {
        return;
    }

    /* Lock hooks */
    mr_spinlock_lock(&hooks->lock);

    /* Delete hooks */
    hooks->hook[index] = MR_NULL;

    /* Unlock hooks */
    mr_spinlock_unlock(&hooks->lock);
}

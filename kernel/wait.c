/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/kernel/mr_wait.h>
#if defined(MR_USE_WAIT)
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/libc/mr_malloc.h>

/* Waitable clazzes(static & dynamic) */
static void able_statik_release(mr_object_t *obj);
static void able_dynamic_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(waitable, able_statik_release, able_dynamic_release, MR_NULL);

/* Waiter clazzes(static & dynamic) */
static void wait_dynamic_release(mr_object_t *obj);
static void wait_statik_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(waiter, wait_statik_release, wait_dynamic_release, MR_NULL);

/* Wait lock */
static mr_spinlock_t __wait_lock = MR_SPINLOCK_INIT();

/* Wait event mask definition */
#define WAIT_TYPE_MASK                  (MR_WAIT_IN | MR_WAIT_OUT | MR_WAIT_ERR)
#define WAIT_MASK                       (~MR_WAIT_EDGE)

static mr_err_t waitable_init(mr_waitable_t *able, const mr_clazz_t *clazz,
                              mr_ptr_t check, mr_ptr_t param) {
    /* Init waitable */
    mr_atomic_init(&able->gen, 0);
    mr_list_init(&able->list);
    able->check = check;
    able->param = param;
    able->last = 0;

    /* Init object */
    return mr_object_init((mr_object_t *)able, clazz);
}

mr_err_t mr_waitable_init(mr_waitable_t *able, mr_waitable_check_t *check,
                          void *param) {
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(waitable);

    /* Check parameter */
    MR_ASSERT(able != MR_NULL);

    /* Init waitable */
    return waitable_init(able, clazz, check, param);
}

mr_waitable_t *mr_waitable_create(mr_waitable_check_t *check, void *param) {
    const mr_clazz_t *clazz = MR_CLAZZES_DYNAMIC(waitable);
    mr_waitable_t *able;
    mr_err_t ret;

    /* Create waitable */
    able = mr_malloc(sizeof(mr_waitable_t));
    if (!able) {
        return MR_NULL;
    }

    /* Init waitable */
    ret = waitable_init(able, clazz, check, param);
    if (ret != 0) {
        mr_free(able);
        return MR_NULL;
    }
    return able;
}

static void waitable_cancel_all(mr_waitable_t *able) {
    mr_waiter_t *waiter;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__wait_lock);

    /* Clear waiting list */
    while (!mr_list_is_empty(&able->list)) {
        waiter = MR_LIST_ENTRY(able->list.next, mr_waiter_t, list);

        /* Remove waiter from waiting list */
        mr_list_del(&waiter->list);
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__wait_lock, mask);
}

mr_err_t mr_waitable_del(mr_waitable_t *able) {
    /* Check parameter */
    MR_ASSERT((able != MR_NULL) && MR_OBJECT_IS_INITED(able));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(able, waitable));

    /* Remove waitable */
    waitable_cancel_all(able);

    /* Delete object */
    mr_object_del((mr_object_t *)able);
    return 0;
}

static mr_uint64_t waitable_check(mr_waitable_t *able) {
    mr_waitable_check_t *check;
    mr_uint64_t event;
    mr_atomic_t gen;
    void *param;

    /* Save waitable generation */
    gen = mr_atomic_fetch_add(&able->gen, 1);

    /* Save waitable check and param */
    check = (mr_waitable_check_t *)able->check;
    param = (void *)able->param;

    /* Call waitable check */
    if (check != MR_NULL) {
        event = check(able, param);
    } else {
        /* Not supported */
        return 0;
    }

    /* Check if waitable generation is changed */
    if (mr_atomic_load(&able->gen) != (gen + 1)) {
        /* If another check occurs during the check process, the last one shall
         * prevail */
        return 0;
    }

    /* Return waitable event */
    return event & WAIT_MASK;
}

static void waitable_handle(mr_waitable_t *able, mr_uint64_t event) {
    mr_uint64_t event_level, event_edge;
    mr_waiter_entry_t *entry;
    mr_waiter_t *waiter;
    mr_list_t oplist;
    void *param;
    int mask;

    /* Init operation list */
    mr_list_init(&oplist);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__wait_lock);

    /* Get edge and level event */
    event_edge = event & ~able->last;
    event_level = event;

    /* Update waitable last event */
    able->last = event;

    /* Replace the waiting list with the operation list (to prevent node removal
     * in the middle) */
    mr_list_replace(&oplist, &able->list);

    /* Check operation list */
    while (!mr_list_is_empty(&oplist)) {
        waiter = MR_LIST_ENTRY(oplist.next, mr_waiter_t, list);

        /* Move waiter to waiting list */
        mr_list_move(&waiter->list, &able->list);

        /* Check if waiter is triggered */
        event = (waiter->event & MR_WAIT_EDGE) ? event_edge : event_level;
        if (!(waiter->event & event)) {
            /* Not triggered */
            continue;
        }

        /* Save waiter entry and param */
        entry = (mr_waiter_entry_t *)waiter->entry;
        param = (void *)waiter->param;

        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__wait_lock, mask);

        /* Call waiter entry */
        entry(waiter, param, event);

        /* Lock */
        mask = mr_spinlock_lock_irqsave(&__wait_lock);
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__wait_lock, mask);
}

mr_err_t mr_waitable_complete(mr_waitable_t *able) {
    mr_uint64_t event;

    /* Check parameter */
    MR_ASSERT((able != MR_NULL) && MR_OBJECT_IS_INITED(able));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(able, waitable));

    /* Reduce unnecessary check operations */
    if (mr_list_is_empty(&able->list)) {
        return 0;
    }

    /* Get waitable event */
    event = waitable_check(able);

    /* Handle waitable event */
    waitable_handle(able, event);
    return 0;
}

mr_err_t mr_waitable_raise(mr_waitable_t *able, mr_uint64_t event) {
    /* Check parameter */
    MR_ASSERT((able != MR_NULL) && MR_OBJECT_IS_INITED(able));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(able, waitable));

    /* Raise waitable event */
    waitable_handle(able, event);
    return 0;
}

mr_err_t mr_waitable_cancel_all(mr_waitable_t *able) {
    /* Check parameter */
    MR_ASSERT((able != MR_NULL) && MR_OBJECT_IS_INITED(able));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(able, waitable));

    /* Cancel all waiters */
    waitable_cancel_all(able);
    return 0;
}

static mr_waitable_t *able_obj_release(mr_object_t *obj) {
    mr_waitable_t *able;

    /* Get waitable */
    able = MR_CONTAINER_OF(obj, mr_waitable_t, parent);

    /* Remove waitable */
    waitable_cancel_all(able);
    return able;
}

static void able_statik_release(mr_object_t *obj) {
    /* Release waitable */
    able_obj_release(obj);
}

static void able_dynamic_release(mr_object_t *obj) {
    mr_waitable_t *able;

    /* Release waitable */
    able = able_obj_release(obj);

    /* Free waitable */
    mr_free(able);
}

static mr_err_t waiter_init(mr_waiter_t *wait, const mr_clazz_t *clazz,
                            mr_ptr_t entry, mr_ptr_t param, mr_uint64_t event) {
    /* Init waiter */
    mr_list_init(&wait->list);
    wait->event = event;
    wait->entry = entry;
    wait->param = param;

    /* Init object */
    return mr_object_init((mr_object_t *)wait, clazz);
}

mr_err_t mr_waiter_init(mr_waiter_t *wait, mr_waiter_entry_t *entry,
                        void *param, mr_uint64_t event) {
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(waiter);

    /* Check parameter */
    MR_ASSERT(wait != MR_NULL);
    MR_ASSERT(entry != MR_NULL);

    /* Init waiter */
    return waiter_init(wait, clazz, entry, param, event);
}

mr_waiter_t *mr_waiter_create(mr_waiter_entry_t *entry, void *param,
                              mr_uint64_t event) {
    const mr_clazz_t *clazz = MR_CLAZZES_DYNAMIC(waiter);
    mr_waiter_t *wait;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(entry != MR_NULL);

    /* Create waiter */
    wait = mr_malloc(sizeof(mr_waiter_t));
    if (!wait) {
        return MR_NULL;
    }

    /* Init waiter */
    ret = waiter_init(wait, clazz, entry, param, event);
    if (ret != 0) {
        mr_free(wait);
        return MR_NULL;
    }
    return wait;
}

static void waiter_remove(mr_waiter_t *wait) {
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__wait_lock);

    /* Remove waiter from waiting list */
    mr_list_del(&wait->list);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__wait_lock, mask);
}

mr_err_t mr_waiter_del(mr_waiter_t *wait) {
    /* Check parameter */
    MR_ASSERT((wait != MR_NULL) && MR_OBJECT_IS_INITED(wait));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(wait, waiter));

    /* Remove waiter */
    waiter_remove(wait);

    /* Delete object */
    mr_object_del((mr_object_t *)wait);
    return 0;
}

mr_err_t mr_waiter_wait(mr_waiter_t *wait, mr_waitable_t *able) {
    mr_waiter_entry_t *entry;
    mr_uint64_t event;
    mr_err_t ret;
    void *param;
    int mask;

    /* Check parameter */
    MR_ASSERT((wait != MR_NULL) && MR_OBJECT_IS_INITED(wait));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(wait, waiter));
    MR_ASSERT((able != MR_NULL) && MR_OBJECT_IS_INITED(able));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(able, waitable));

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__wait_lock);

    /* Check if waiter is waiting */
    if (MR_WAITER_IS_WAITING(wait)) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Add waiter to waitable waiting list */
    mr_list_add(&able->list, &wait->list);

    /* Check if waiter is waiting for edge event */
    if (wait->event & MR_WAIT_EDGE) {
        /* Edge triggering is not related to the previous one */
        ret = 0;
        goto _exit;
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__wait_lock, mask);

    /* Get waitable event */
    event = waitable_check(able);

    /* Check if event is untyped */
    if ((event & WAIT_TYPE_MASK) == 0) {
        return 0;
    }

    /* Check if waiter is triggered */
    if (!(wait->event & event)) {
        /* Not triggered */
        return 0;
    }

    /* Save waiter entry and param */
    entry = (mr_waiter_entry_t *)wait->entry;
    param = (void *)wait->param;

    /* Call waiter entry */
    entry(wait, param, event);
    return 0;
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__wait_lock, mask);
    return ret;
}

mr_err_t mr_waiter_cancel(mr_waiter_t *wait) {
    /* Check parameter */
    MR_ASSERT((wait != MR_NULL) && MR_OBJECT_IS_INITED(wait));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(wait, waiter));

    /* Remove waiter */
    waiter_remove(wait);
    return 0;
}

mr_err_t mr_waiter_event_set(mr_waiter_t *wait, mr_uint64_t event) {
    int mask;

    /* Check parameter */
    MR_ASSERT((wait != MR_NULL) && MR_OBJECT_IS_INITED(wait));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(wait, waiter));

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__wait_lock);

    /* Remove waiter from waiting list */
    mr_list_del(&wait->list);

    /* Set waiter event */
    wait->event = event;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__wait_lock, mask);
    return 0;
}

static mr_waiter_t *wait_obj_release(mr_object_t *obj) {
    mr_waiter_t *waiter;

    /* Get waiter */
    waiter = MR_CONTAINER_OF(obj, mr_waiter_t, parent);

    /* Remove waiter */
    waiter_remove(waiter);
    return waiter;
}

static void wait_statik_release(mr_object_t *obj) {
    /* Release waiter */
    wait_obj_release(obj);
}

static void wait_dynamic_release(mr_object_t *obj) {
    mr_waiter_t *waiter;

    /* Release waiter */
    waiter = wait_obj_release(obj);

    /* Free waiter */
    mr_free(waiter);
}
#endif /* defined(MR_USE_WAIT) */

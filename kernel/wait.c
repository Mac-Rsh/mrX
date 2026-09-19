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

/* Wait cursor type */
typedef struct wait_cursor {
    mr_list_t list;
    mr_list_t *next;
} wait_cursor_t;

static mr_err_t waitable_init(mr_waitable_t *able, const mr_clazz_t *clazz) {
    /* Init waitable */
    mr_list_init(&able->cursors);
    mr_list_init(&able->list);

    /* Init object */
    return mr_object_init((mr_object_t *)able, clazz);
}

mr_err_t mr_waitable_init(mr_waitable_t *able) {
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(waitable);

    /* Check parameter */
    MR_ASSERT(able != MR_NULL);

    /* Init waitable */
    return waitable_init(able, clazz);
}

mr_waitable_t *mr_waitable_create(void) {
    const mr_clazz_t *clazz = MR_CLAZZES_DYNAMIC(waitable);
    mr_waitable_t *able;
    mr_err_t ret;

    /* Create waitable */
    able = mr_malloc(sizeof(mr_waitable_t));
    if (!able) {
        return MR_NULL;
    }

    /* Init waitable */
    ret = waitable_init(able, clazz);
    if (ret != 0) {
        mr_free(able);
        return MR_NULL;
    }
    return able;
}

static void waiter_remove_locked(mr_waiter_t *wait) {
    wait_cursor_t *cursor;
    mr_list_t *node;

    /* Check whether waiter is removed */
    if (!wait->able) {
        return;
    }

    /* Move cursors past the removed waiter */
    MR_LIST_FOR_EACH(node, &wait->able->cursors) {
        cursor = MR_LIST_ENTRY(node, wait_cursor_t, list);
        if (cursor->next == &wait->list) {
            cursor->next = wait->list.next;
        }
    }

    /* Remove waiter from waiting list */
    mr_list_del(&wait->list);
    wait->able = MR_NULL;
}

static void waitable_cancel_all(mr_waitable_t *able) {
    mr_waiter_t *waiter;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__wait_lock);

    /* Clear waiting list */
    while (!mr_list_is_empty(&able->list)) {
        waiter = MR_LIST_ENTRY(able->list.next, mr_waiter_t, list);
        waiter_remove_locked(waiter);
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__wait_lock, mask);
}

mr_err_t mr_waitable_del(mr_waitable_t *able) {
    /* Check parameter */
    MR_ASSERT((able != MR_NULL) && MR_OBJECT_IS_INITED(able));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(able, waitable));

    /* Cancel all waiters */
    waitable_cancel_all(able);

    /* Delete object */
    mr_object_del((mr_object_t *)able);
    return 0;
}

static void waitable_handle(mr_waitable_t *able, mr_wait_event_t event,
                            const void *data) {
    mr_wait_event_t event_edge;
    mr_waiter_entry_t *entry;
    wait_cursor_t cursor;
    mr_waiter_t *waiter;
    void *param;
    int mask;

    /* Init cursor */
    mr_list_init(&cursor.list);
    event &= (~MR_WAIT_EDGE);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__wait_lock);

    /* Add cursor */
    cursor.next = able->list.next;
    mr_list_add(&able->cursors, &cursor.list);

    /* Handle waiters */
    while (cursor.next != &able->list) {
        waiter = MR_LIST_ENTRY(cursor.next, mr_waiter_t, list);

        /* Advance before callback so removals can repair the cursor */
        cursor.next = waiter->list.next;

        /* Get edge or level event and update waiter state */
        if (waiter->event & MR_WAIT_EDGE) {
            event_edge = event & (~waiter->last);
        } else {
            event_edge = event;
        }
        waiter->last = event;

        /* Check whether waiter is triggered */
        if (!(waiter->event & event_edge)) {
            continue;
        }

        /* Save waiter entry and param */
        entry = (mr_waiter_entry_t *)waiter->entry;
        param = (void *)waiter->param;

        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__wait_lock, mask);

        /* Call waiter entry */
        entry(waiter, param, event_edge, data);

        /* Lock */
        mask = mr_spinlock_lock_irqsave(&__wait_lock);
    }

    /* Remove cursor */
    mr_list_del(&cursor.list);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__wait_lock, mask);
}

mr_err_t mr_waitable_raise(mr_waitable_t *able, mr_wait_event_t event,
                           const void *data) {

    /* Check parameter */
    MR_ASSERT((able != MR_NULL) && MR_OBJECT_IS_INITED(able));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(able, waitable));

    /* Raise waitable event */
    waitable_handle(able, event, data);
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

    /* Cancel all waiters */
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
                            mr_ptr_t entry, mr_ptr_t param,
                            mr_wait_event_t event) {
    /* Init waiter */
    mr_list_init(&wait->list);
    wait->event = event;
    wait->last = 0;
    wait->able = MR_NULL;
    wait->entry = entry;
    wait->param = param;

    /* Init object */
    return mr_object_init((mr_object_t *)wait, clazz);
}

mr_err_t mr_waiter_init(mr_waiter_t *wait, mr_waiter_entry_t *entry,
                        void *param, mr_wait_event_t event) {
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(waiter);

    /* Check parameter */
    MR_ASSERT(wait != MR_NULL);
    MR_ASSERT(entry != MR_NULL);

    /* Init waiter */
    return waiter_init(wait, clazz, entry, param, event);
}

mr_waiter_t *mr_waiter_create(mr_waiter_entry_t *entry, void *param,
                              mr_wait_event_t event) {
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
    waiter_remove_locked(wait);

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
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT((wait != MR_NULL) && MR_OBJECT_IS_INITED(wait));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(wait, waiter));
    MR_ASSERT((able != MR_NULL) && MR_OBJECT_IS_INITED(able));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(able, waitable));

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__wait_lock);

    /* Check whether waiter is waiting */
    if (MR_WAITER_IS_WAITING(wait)) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Add waiter to waiting list */
    wait->able = able;
    wait->last = 0;
    mr_list_add(&able->list, &wait->list);
    ret = 0;
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

mr_err_t mr_waiter_event_set(mr_waiter_t *wait, mr_wait_event_t event) {
    int mask;

    /* Check parameter */
    MR_ASSERT((wait != MR_NULL) && MR_OBJECT_IS_INITED(wait));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(wait, waiter));

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__wait_lock);

    /* Cancel waiter */
    waiter_remove_locked(wait);

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

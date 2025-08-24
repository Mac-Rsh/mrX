/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mr-X/mr_wait.h>
#if defined(MR_USE_WAIT)
#include <mr-X/mr_spinlock.h>
#include <libc/mr_malloc.h>

/* Waitable clazz(static & dynamic) */
static void able_obj_release1(mr_object_t *obj);
static void able_obj_release2(mr_object_t *obj);
static const mr_clazz_t __able_clazz1
    = MR_CLAZZ_INIT("waitable", able_obj_release1);
static const mr_clazz_t __able_clazz2
    = MR_CLAZZ_INIT("waitable", able_obj_release2);

/* Waiter clazz(static & dynamic) */
static void wait_obj_release1(mr_object_t *obj);
static void wait_obj_release2(mr_object_t *obj);
static const mr_clazz_t __wait_clazz1
    = MR_CLAZZ_INIT("waiter", wait_obj_release1);
static const mr_clazz_t __wait_clazz2
    = MR_CLAZZ_INIT("waiter", wait_obj_release2);

/* Wait lock */
static mr_spinlock_t __lock = MR_SPINLOCK_INIT();

/* Wait event mask definition */
#define WAIT_TYPE_MASK                  (0x70000000U)
#define WAIT_MASK                       (0x7fffffffU)

static mr_err_t waitable_init(mr_waitable_t *waitable, const mr_clazz_t *class,
                              mr_ptr_t check, mr_ptr_t param) {
    /* Init waitable */
    mr_atomic_init(&waitable->gen, 0);
    mr_list_init(&waitable->list);
    waitable->check = check;
    waitable->param = param;
    waitable->last = 0;

    /* Init object */
    return mr_object_init((mr_object_t *)waitable, class);
}

mr_err_t mr_waitable_init(mr_waitable_t *waitable, mr_waitable_check_t *check,
                          void *param) {
    /* Check parameter */
    MR_ASSERT((waitable != MR_NULL) && (!MR_OBJECT_IS_INITED(waitable)));
    MR_ASSERT(check != MR_NULL);

    /* Init waitable */
    return waitable_init(waitable, &__able_clazz1, check, param);
}

mr_waitable_t *mr_waitable_create(mr_waitable_check_t *check, void *param) {
    mr_waitable_t *waitable;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(check != MR_NULL);

    /* Create waitable */
    waitable = mr_malloc(sizeof(mr_waitable_t));
    if (!waitable) {
        return MR_NULL;
    }

    /* Init waitable */
    ret = waitable_init(waitable, &__able_clazz2, check, param);
    if (ret != 0) {
        mr_free(waitable);
        return MR_NULL;
    }
    return waitable;
}

static void waitable_remove(mr_waitable_t *waitable) {
    mr_waiter_t *waiter;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__lock);

    /* Clear waiting list */
    while (!mr_list_is_empty(&waitable->list)) {
        waiter = MR_LIST_ENTRY(waitable->list.next, mr_waiter_t, list);

        /* Remove waiter from waiting list */
        mr_list_del(&waiter->list);
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__lock, mask);
}

mr_err_t mr_waitable_del(mr_waitable_t *waitable) {
    /* Check parameter */
    MR_ASSERT((waitable != MR_NULL) && MR_OBJECT_IS_INITED(waitable));
    MR_ASSERT(MR_OBJECT_CLAZZ_IS_OR(waitable, &__able_clazz1, &__able_clazz2));

    /* Remove waitable */
    waitable_remove(waitable);

    /* Delete object */
    mr_object_del((mr_object_t *)waitable);
    return 0;
}

static mr_uint32_t waitable_check(mr_waitable_t *waitable) {
    mr_waitable_check_t *check;
    mr_uint32_t event;
    mr_atomic_t gen;
    void *param;

    /* Save waitable generation */
    gen = mr_atomic_fetch_add(&waitable->gen, 1);

    /* Save waitable check and param */
    check = (mr_waitable_check_t *)waitable->check;
    param = (void *)waitable->param;

    /* Call waitable check */
    event = check(waitable, param);

    /* Check if waitable generation is changed */
    if (mr_atomic_load(&waitable->gen) != (gen + 1)) {
        /* If another check occurs during the check process, the last one shall
         * prevail */
        return 0;
    }

    /* Return waitable event */
    return event & WAIT_MASK;
}

mr_err_t mr_waitable_complete(mr_waitable_t *waitable) {
    mr_uint32_t event, event_level, event_edge;
    mr_waiter_entry_t *entry;
    mr_waiter_t *waiter;
    mr_list_t oplist;
    void *param;
    int mask;

    /* Check parameter */
    MR_ASSERT((waitable != MR_NULL) && MR_OBJECT_IS_INITED(waitable));
    MR_ASSERT(MR_OBJECT_CLAZZ_IS_OR(waitable, &__able_clazz1, &__able_clazz2));

    /* Reduce unnecessary check operations */
    if (mr_list_is_empty(&waitable->list)) {
        return 0;
    }

    /* Get waitable event */
    event = waitable_check(waitable);
    if ((event & WAIT_TYPE_MASK) == 0) {
        /* No waitable event(untyped events will be discarded) */
        return 0;
    }

    /* Init operation list */
    mr_list_init(&oplist);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__lock);

    /* Get edge and level event */
    event_edge = event & ~waitable->last;
    event_level = event;

    /* Update waitable last event */
    waitable->last = event;

    /* Replace the waiting list with the operation list (to prevent node removal
     * in the middle) */
    mr_list_replace(&oplist, &waitable->list);

    /* Check operation list */
    while (!mr_list_is_empty(&oplist)) {
        waiter = MR_LIST_ENTRY(oplist.next, mr_waiter_t, list);

        /* Move waiter to waiting list */
        mr_list_move(&waiter->list, &waitable->list);

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
        mr_spinlock_unlock_irqrestore(&__lock, mask);

        /* Call waiter entry */
        entry(waiter, param, event);

        /* Lock */
        mask = mr_spinlock_lock_irqsave(&__lock);
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__lock, mask);
    return 0;
}

static mr_waitable_t *able_obj_release(mr_object_t *obj) {
    mr_waitable_t *waitable;

    /* Get waitable */
    waitable = MR_CONTAINER_OF(obj, mr_waitable_t, parent);

    /* Remove waitable */
    waitable_remove(waitable);
    return waitable;
}

static void able_obj_release1(mr_object_t *obj) {
    /* Release waitable */
    able_obj_release(obj);
}

static void able_obj_release2(mr_object_t *obj) {
    mr_waitable_t *waitable;

    /* Release waitable */
    waitable = able_obj_release(obj);

    /* Free waitable */
    mr_free(waitable);
}

static mr_err_t waiter_init(mr_waiter_t *waiter, const mr_clazz_t *class,
                            mr_ptr_t entry, mr_ptr_t param, mr_uint32_t event) {
    /* Init waiter */
    mr_list_init(&waiter->list);
    waiter->event = event;
    waiter->entry = entry;
    waiter->param = param;

    /* Init object */
    return mr_object_init((mr_object_t *)waiter, class);
}

mr_err_t mr_waiter_init(mr_waiter_t *waiter, mr_waiter_entry_t *entry,
                        void *param, mr_uint32_t event) {
    /* Check parameter */
    MR_ASSERT((waiter != MR_NULL) && (!MR_OBJECT_IS_INITED(waiter)));
    MR_ASSERT((entry != MR_NULL) && ((event & WAIT_TYPE_MASK) != 0));

    /* Init waiter */
    return waiter_init(waiter, &__wait_clazz1, entry, param, event);
}

mr_waiter_t *mr_waiter_create(mr_waiter_entry_t *entry, void *param,
                              mr_uint32_t event) {
    mr_waiter_t *waiter;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((entry != MR_NULL) && ((event & WAIT_TYPE_MASK) != 0));

    /* Create waiter */
    waiter = mr_malloc(sizeof(mr_waiter_t));
    if (!waiter) {
        return MR_NULL;
    }

    /* Init waiter */
    ret = waiter_init(waiter, &__wait_clazz2, entry, param, event);
    if (ret != 0) {
        mr_free(waiter);
        return MR_NULL;
    }
    return waiter;
}

static void waiter_remove(mr_waiter_t *waiter) {
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__lock);

    /* Remove waiter from waiting list */
    mr_list_del(&waiter->list);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__lock, mask);
}

mr_err_t mr_waiter_del(mr_waiter_t *waiter) {
    /* Check parameter */
    MR_ASSERT((waiter != MR_NULL) && MR_OBJECT_IS_INITED(waiter));
    MR_ASSERT(MR_OBJECT_CLAZZ_IS_OR(waiter, &__wait_clazz1, &__wait_clazz2));

    /* Remove waiter */
    waiter_remove(waiter);

    /* Delete object */
    mr_object_del((mr_object_t *)waiter);
    return 0;
}

mr_err_t mr_waiter_wait(mr_waiter_t *waiter, mr_waitable_t *waitable) {
    mr_waiter_entry_t *entry;
    mr_uint32_t event;
    mr_err_t ret;
    void *param;
    int mask;

    /* Check parameter */
    MR_ASSERT((waiter != MR_NULL) && MR_OBJECT_IS_INITED(waiter));
    MR_ASSERT(MR_OBJECT_CLAZZ_IS_OR(waiter, &__wait_clazz1, &__wait_clazz2));
    MR_ASSERT((waitable != MR_NULL) && MR_OBJECT_IS_INITED(waitable));
    MR_ASSERT(MR_OBJECT_CLAZZ_IS_OR(waitable, &__able_clazz1, &__able_clazz2));

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__lock);

    /* Check if waiter is waiting */
    if (MR_WAITER_IS_WAITING(waiter)) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Add waiter to waitable waiting list */
    mr_list_add(&waitable->list, &waiter->list);

    /* Check if waiter is waiting for edge event */
    if (waiter->event & MR_WAIT_EDGE) {
        /* Edge triggering is not related to the previous one */
        ret = 0;
        goto _exit;
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__lock, mask);

    /* Get waitable event */
    event = waitable_check(waitable);
    if ((event & WAIT_TYPE_MASK) == 0) {
        /* No waitable event(untyped events will be discarded) */
        return 0;
    }

    /* Check if waiter is triggered */
    if (!(waiter->event & event)) {
        /* Not triggered */
        return 0;
    }

    /* Save waiter entry and param */
    entry = (mr_waiter_entry_t *)waiter->entry;
    param = (void *)waiter->param;

    /* Call waiter entry */
    entry(waiter, param, event);
    return 0;
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__lock, mask);
    return ret;
}

mr_err_t mr_waiter_cancel(mr_waiter_t *waiter) {
    /* Check parameter */
    MR_ASSERT((waiter != MR_NULL) && MR_OBJECT_IS_INITED(waiter));
    MR_ASSERT(MR_OBJECT_CLAZZ_IS_OR(waiter, &__wait_clazz1, &__wait_clazz2));

    /* Remove waiter */
    waiter_remove(waiter);
    return 0;
}

mr_uint32_t mr_waiter_event(mr_waiter_t *waiter) {
    /* Check parameter */
    MR_ASSERT((waiter != MR_NULL) && MR_OBJECT_IS_INITED(waiter));
    MR_ASSERT(MR_OBJECT_CLAZZ_IS_OR(waiter, &__wait_clazz1, &__wait_clazz2));

    /* Return waiter event */
    return waiter->event;
}

mr_err_t mr_waiter_event_set(mr_waiter_t *waiter, mr_uint32_t event) {
    int mask;

    /* Check parameter */
    MR_ASSERT((waiter != MR_NULL) && MR_OBJECT_IS_INITED(waiter));
    MR_ASSERT(MR_OBJECT_CLAZZ_IS_OR(waiter, &__wait_clazz1, &__wait_clazz2));
    MR_ASSERT((event & WAIT_TYPE_MASK) != 0);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__lock);

    /* Remove waiter from waiting list */
    mr_list_del(&waiter->list);

    /* Set waiter event */
    waiter->event = event;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__lock, mask);
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

static void wait_obj_release1(mr_object_t *obj) {
    /* Release waiter */
    wait_obj_release(obj);
}

static void wait_obj_release2(mr_object_t *obj) {
    mr_waiter_t *waiter;

    /* Release waiter */
    waiter = wait_obj_release(obj);

    /* Free waiter */
    mr_free(waiter);
}
#endif /* defined(MR_USE_WAIT) */

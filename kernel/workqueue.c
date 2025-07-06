/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mr-X/mr_workqueue.h>
#if defined(MR_USE_WORKQUEUE)
#include <mr-X/mr_spinlock.h>
#include <libc/mr_malloc.h>

/* Workqueue class(static & dynamic) */
static void queue_obj_release1(mr_object_t *obj);
static void queue_obj_release2(mr_object_t *obj);
static const mr_class_t __queue_class1
    = MR_CLASS_INIT("workqueue", MR_NULL, queue_obj_release1);
static const mr_class_t __queue_class2
    = MR_CLASS_INIT("workqueue", MR_NULL, queue_obj_release2);

/* Work class(static & dynamic) */
static void work_obj_release1(mr_object_t *obj);
static void work_obj_release2(mr_object_t *obj);
static const mr_class_t __work_class1
    = MR_CLASS_INIT("work", MR_NULL, work_obj_release1);
static const mr_class_t __work_class2
    = MR_CLASS_INIT("work", MR_NULL, work_obj_release2);

/* Workqueue lock */
static mr_spinlock_t __lock = MR_SPINLOCK_INIT();

static void queue_wakeup(mr_workqueue_t *queue) {
    mr_atomic_t norun;

    /* Check if workqueue need wakeup */
    mr_atomic_init(&norun, MR_FALSE);
    if (!mr_atomic_compare_exchange(&queue->running, &norun, MR_TRUE)) {
        /* No need wakeup */
        return;
    }

    /* Get workqueue run-time(running) */
    mr_workqueue_get(queue);

    /* Wakeup workqueue */
#if defined(MR_USE_WORKQUEUE_HOOK)
    if (queue->hook.wakeup) {
        queue->hook.wakeup(queue, queue->hook.param);
    }
#endif /* defined(MR_USE_WORKQUEUE_HOOK) */
}

static void queue_timer_entry(mr_timer_t *timer, void *param) {
    mr_tick_t tickless, tick;
    mr_workqueue_t *queue;
    mr_work_t *work;
    int mask;

    /* Get workqueue */
    queue = mr_workqueue_get(param);
    if (!queue) {
        /* Workqueue is not exist */
        mr_timer_del(timer);
        return;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__lock);

    /* Check timeout work */
    for (tickless = 0, tick = mr_clock_tick();
         !mr_list_is_empty(&queue->tlist);) {
        work = MR_CONTAINER_OF(queue->tlist.next, mr_work_t, list);

        /* Check timeout tick */
        if ((tick - work->tick) >= (MR_TICK_MAX / 2)) {
            /* There will be no timeout work after that */
            tickless = work->tick - tick;
            break;
        }

        /* Move work to workqueue work list */
        mr_list_move(&work->list, &queue->list);
    }

    /* Check if workqueue need set timer tick */
    if ((tickless != 0) && (tickless != mr_timer_tick(&queue->timer))) {
        /* Set to optimal tick */
        mr_timer_tick_set(&queue->timer, tickless | MR_TIMER_PERIODIC);
        mr_timer_start(&queue->timer);

        /* Check if workqueue is stopped(timing) for the last time */
    } else if (mr_list_is_empty(&queue->tlist)) {
        /* Don't need timer */
        mr_timer_stop(&queue->timer);

        /* Put workqueue run-time(timing) */
        mr_workqueue_put(queue);
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__lock, mask);

    /* Wakeup workqueue */
    queue_wakeup(queue);

    /* Put workqueue */
    mr_workqueue_put(queue);
}

static mr_err_t workqueue_init(mr_workqueue_t *queue, const mr_class_t *class) {
    mr_err_t ret;

    /* Init workqueue timer */
    ret = mr_timer_init(&queue->timer, queue_timer_entry, queue, 1);
    if (ret != 0) {
        return ret;
    }

    /* Init workqueue */
    mr_atomic_init(&queue->running, MR_FALSE);
    mr_atomic_init(&queue->dying, MR_FALSE);
    mr_list_init(&queue->tlist);
    mr_list_init(&queue->list);
#if defined(MR_USE_WORKQUEUE_HOOK)
    queue->hook.suspend = MR_NULL;
    queue->hook.wakeup = MR_NULL;
    queue->hook.param = MR_NULL;
#endif /* defined(MR_USE_WORKQUEUE_HOOK) */

    /* Init object */
    ret = mr_object_init((mr_object_t *)queue, class);
    if (ret != 0) {
        mr_timer_del(&queue->timer);
        return ret;
    }
    return 0;
}

mr_err_t mr_workqueue_init(mr_workqueue_t *queue) {
    /* Check parameter */
    MR_ASSERT((queue != MR_NULL) && (!MR_OBJECT_IS_INITED(queue)));

    /* Init workqueue */
    return workqueue_init(queue, &__queue_class1);
}

mr_workqueue_t *mr_workqueue_create(void) {
    mr_workqueue_t *queue;
    mr_err_t ret;

    /* Create workqueue */
    queue = mr_malloc(sizeof(mr_workqueue_t));
    if (!queue) {
        return MR_NULL;
    }

    /* Init workqueue */
    ret = workqueue_init(queue, &__queue_class2);
    if (ret != 0) {
        mr_free(queue);
        return MR_NULL;
    }
    return queue;
}

mr_err_t mr_workqueue_del(mr_workqueue_t *queue) {
    /* Check parameter */
    MR_ASSERT((queue != MR_NULL) && MR_OBJECT_IS_INITED(queue));
    MR_ASSERT(MR_OBJECT_CLASS_IS_OR(queue, &__queue_class1, &__queue_class2));

    /* Mark as dying */
    mr_atomic_store(&queue->dying, MR_TRUE);

    /* Delete object */
    mr_object_del((mr_object_t *)queue);
    return 0;
}

static void queue_suspend(mr_workqueue_t *queue) {
    mr_atomic_t run;

    /* Check if workqueue need suspend */
    mr_atomic_init(&run, MR_TRUE);
    if (!mr_atomic_compare_exchange(&queue->running, &run, MR_FALSE)) {
        return;
    }

    /* Put workqueue run-time(running) */
    mr_workqueue_put(queue);

    /* Suspend workqueue */
#if defined(MR_USE_WORKQUEUE_HOOK)
    if (queue->hook.suspend) {
        queue->hook.suspend(queue, queue->hook.param);
    }
#endif /* defined(MR_USE_WORKQUEUE_HOOK) */
}

mr_err_t mr_workqueue_execute(mr_workqueue_t *queue) {
    mr_work_entry_t *entry;
    mr_work_t *work;
    void *param;
    int mask;

    /* Check parameter */
    MR_ASSERT((queue != MR_NULL) && MR_OBJECT_IS_INITED(queue));
    MR_ASSERT(MR_OBJECT_CLASS_IS_OR(queue, &__queue_class1, &__queue_class2));

    /* Reduce unnecessary lock operations */
    if (mr_list_is_empty(&queue->list)
#if defined(MR_USE_WORKQUEUE_HOOK)
        && (!queue->hook.suspend)
#endif /* defined(MR_USE_WORKQUEUE_HOOK) */
    ) {
        return 0;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__lock);

    /* Check workqueue work list */
    while (!mr_list_is_empty(&queue->list)) {
        work = MR_LIST_ENTRY(queue->list.next, mr_work_t, list);

        /* Remove work from running list */
        mr_list_del(&work->list);

        /* Save work entry and param */
        entry = (mr_work_entry_t *)work->entry;
        param = (void *)work->param;

        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__lock, mask);

        /* Call work entry */
        entry(work, param);

        /* Lock */
        mask = mr_spinlock_lock_irqsave(&__lock);
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__lock, mask);

    /* Suspend workqueue */
    queue_suspend(queue);
    return 0;
}

mr_err_t mr_workqueue_work(mr_workqueue_t *queue, mr_work_t *work) {
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT((queue != MR_NULL) && MR_OBJECT_IS_INITED(queue));
    MR_ASSERT(MR_OBJECT_CLASS_IS_OR(queue, &__queue_class1, &__queue_class2));
    MR_ASSERT((work != MR_NULL) && MR_OBJECT_IS_INITED(work));
    MR_ASSERT(MR_OBJECT_CLASS_IS_OR(work, &__work_class1, &__work_class2));

    /* Check if workqueue is dying */
    if (mr_atomic_load(&queue->dying)) {
        return -MR_ENOENT;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__lock);

    /* Check if work is running */
    if (MR_WORK_IS_RUNNING(work)) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Add work to running list */
    mr_list_add(&queue->list, &work->list);
    work->queue = queue;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__lock, mask);

    /* Wakeup workqueue */
    queue_wakeup(queue);
    return 0;
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__lock, mask);
    return ret;
}

mr_err_t mr_workqueue_delayed_work(mr_workqueue_t *queue, mr_work_t *work,
                                   mr_tick_t tick) {
    mr_list_t *l;
    mr_work_t *w;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT((queue != MR_NULL) && MR_OBJECT_IS_INITED(queue));
    MR_ASSERT(MR_OBJECT_CLASS_IS_OR(queue, &__queue_class1, &__queue_class2));
    MR_ASSERT((work != MR_NULL) && MR_OBJECT_IS_INITED(work));
    MR_ASSERT(MR_OBJECT_CLASS_IS_OR(work, &__work_class1, &__work_class2));
    MR_ASSERT(tick < (MR_TICK_MAX / 2));

    /* Check if tick is zero */
    if (tick == 0) {
        /* No delay */
        return mr_workqueue_work(queue, work);
    }

    /* Check if workqueue is dying */
    if (mr_atomic_load(&queue->dying)) {
        return -MR_ENOENT;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__lock);

    /* Check if work is running */
    if (MR_WORK_IS_RUNNING(work)) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Check if workqueue is started for the first time */
    if (mr_list_is_empty(&queue->tlist)) {
        /* Get workqueue run-time(timing) */
        mr_workqueue_get(queue);
    }

    /* Set timeout tick */
    tick = tick & ~MR_TIMER_PERIODIC;
    work->tick = mr_clock_tick() + tick;

    /* Insert work into workqueue timer list */
    MR_LIST_FOR_EACH(l, &queue->tlist) {
        w = MR_LIST_ENTRY(l, mr_work_t, list);

        /* Compare timeout tick */
        if ((work->tick - w->tick) < (MR_TICK_MAX / 2)) {
            continue;
        }

        /* Move work into position */
        mr_list_move(&work->list, &w->list);
        goto _set;
    }

    /* It is the last work */
    mr_list_move(&work->list, &queue->tlist);
_set:
    /* Check if work is first */
    if (&work->list == queue->tlist.next) {
        /* Set to optimal tick */
        tick = work->tick - mr_clock_tick();
        mr_timer_tick_set(&queue->timer, tick | MR_TIMER_PERIODIC);
        mr_timer_start(&queue->timer);
    }

    /* Mark work as in workqueue */
    work->queue = queue;
    ret = 0;
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__lock, mask);
    return ret;
}

#if defined(MR_USE_WORKQUEUE_HOOK)
mr_err_t mr_workqueue_hook_set(mr_workqueue_t *queue,
                               mr_workqueue_hook_t *hook) {
    /* Check parameter */
    MR_ASSERT((queue != MR_NULL) && MR_OBJECT_IS_INITED(queue));
    MR_ASSERT(MR_OBJECT_CLASS_IS_OR(queue, &__class1, &__class2));
    MR_ASSERT(hook != MR_NULL);

    /* Set workqueue hook */
    queue->hook = *hook;
    return 0;
}
#endif /* defined(MR_USE_WORKQUEUE_HOOK) */

static void queue_del(mr_workqueue_t *queue) {
    mr_work_t *work;
    int mask;

    /* Mark as dying */
    mr_atomic_store(&queue->dying, MR_TRUE);

    /* Delete workqueue timer */
    mr_timer_del(&queue->timer);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__lock);

    /* Remove work from workqueue timing list */
    while (!mr_list_is_empty(&queue->tlist)) {
        work = MR_LIST_ENTRY(&queue->tlist.next, mr_work_t, list);
        mr_list_del(&work->list);
    }

    /* Remove work from workqueue running list */
    while (!mr_list_is_empty(&queue->list)) {
        work = MR_LIST_ENTRY(&queue->list.next, mr_work_t, list);
        mr_list_del(&work->list);
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__lock, mask);
}

static mr_workqueue_t *queue_obj_release(mr_object_t *obj) {
    mr_workqueue_t *queue;

    /* Get workqueue */
    queue = MR_CONTAINER_OF(obj, mr_workqueue_t, parent);

    /* Delete workqueue */
    queue_del(queue);
    return queue;
}

static void queue_obj_release1(mr_object_t *obj) {
    /* Release workqueue */
    queue_obj_release(obj);
}

static void queue_obj_release2(mr_object_t *obj) {
    mr_workqueue_t *queue;

    /* Release workqueue */
    queue = queue_obj_release(obj);

    /* Free workqueue */
    mr_free(queue);
}

static mr_err_t work_init(mr_work_t *work, const mr_class_t *class,
                          mr_ptr_t entry, mr_ptr_t param) {
    /* Init work */
    mr_list_init(&work->list);
    work->queue = MR_NULL;
    work->entry = entry;
    work->param = param;

    /* Init object */
    return mr_object_init((mr_object_t *)work, class);
}

mr_err_t mr_work_init(mr_work_t *work, mr_work_entry_t *entry, void *param) {
    /* Check parameter */
    MR_ASSERT((work != MR_NULL) && (!MR_OBJECT_IS_INITED(work)));
    MR_ASSERT(entry != MR_NULL);

    /* Init work */
    return work_init(work, &__work_class1, entry, param);
}

mr_work_t *mr_work_create(mr_work_entry_t *entry, void *param) {
    mr_work_t *work;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(entry != MR_NULL);

    /* Create work */
    work = mr_malloc(sizeof(mr_work_t));
    if (!work) {
        return MR_NULL;
    }

    /* Init work */
    ret = work_init(work, &__work_class2, entry, param);
    if (ret) {
        mr_free(work);
        return MR_NULL;
    }
    return work;
}

mr_err_t mr_work_del(mr_work_t *work) {
    /* Check parameter */
    MR_ASSERT((work != MR_NULL) && MR_OBJECT_IS_INITED(work));
    MR_ASSERT(MR_OBJECT_CLASS_IS_OR(work, &__work_class1, &__work_class2));

    /* Cancel work */
    mr_work_cancel(work);

    /* Delete object */
    mr_object_del((mr_object_t *)work);
    return 0;
}

mr_err_t mr_work_schedule(mr_work_t *work) {
    mr_workqueue_t *queue;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((work != MR_NULL) && MR_OBJECT_IS_INITED(work));
    MR_ASSERT(MR_OBJECT_CLASS_IS_OR(work, &__work_class1, &__work_class2));

    /* Get workqueue */
    queue = mr_workqueue_get(work->queue);
    if (!queue) {
        return -MR_ENOENT;
    }

    /* Schedule work to the used workqueue */
    ret = mr_workqueue_work(queue, work);

    /* Put workqueue */
    mr_workqueue_put(queue);
    return ret;
}

mr_err_t mr_work_delayed_schedule(mr_work_t *work, mr_tick_t tick) {
    mr_workqueue_t *queue;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((work != MR_NULL) && MR_OBJECT_IS_INITED(work));
    MR_ASSERT(MR_OBJECT_CLASS_IS_OR(work, &__work_class1, &__work_class2));
    MR_ASSERT(tick < (MR_TICK_MAX / 2));

    /* Get workqueue */
    queue = mr_workqueue_get(work->queue);
    if (!queue) {
        return -MR_ENOENT;
    }

    /* Schedule work to the used workqueue */
    ret = mr_workqueue_delayed_work(queue, work, tick);

    /* Put workqueue */
    mr_workqueue_put(queue);
    return ret;
}

static void work_remove(mr_work_t *work) {
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__lock);

    /* Remove work from running list */
    mr_list_del(&work->list);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__lock, mask);
}

mr_err_t mr_work_cancel(mr_work_t *work) {
    /* Check parameter */
    MR_ASSERT((work != MR_NULL) && MR_OBJECT_IS_INITED(work));
    MR_ASSERT(MR_OBJECT_CLASS_IS_OR(work, &__work_class1, &__work_class2));

    /* Remove work */
    work_remove(work);
    return 0;
}

mr_err_t mr_work_bind(mr_work_t *work, mr_workqueue_t *queue) {
    /* Check parameter */
    MR_ASSERT((work != MR_NULL) && MR_OBJECT_IS_INITED(work));
    MR_ASSERT(MR_OBJECT_CLASS_IS_OR(work, &__work_class1, &__work_class2));
    MR_ASSERT((queue != MR_NULL) && MR_OBJECT_IS_INITED(queue));
    MR_ASSERT(MR_OBJECT_CLASS_IS_OR(queue, &__queue_class1, &__queue_class2));

    /* Set workqueue */
    work->queue = queue;
    return 0;
}

static mr_work_t *work_obj_release(mr_object_t *obj) {
    mr_work_t *work;

    /* Get work */
    work = MR_CONTAINER_OF(obj, mr_work_t, parent);

    /* Remove workqueue */
    work_remove(work);
    return work;
}

static void work_obj_release1(mr_object_t *obj) {
    /* Release work */
    work_obj_release(obj);
}

static void work_obj_release2(mr_object_t *obj) {
    mr_work_t *work;

    /* Release work */
    work = work_obj_release(obj);

    /* Free work */
    mr_free(work);
}
#endif /* defined(MR_USE_WORKQUEUE) */

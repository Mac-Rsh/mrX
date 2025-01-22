/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_initcall.h>
#include <kernel/mr_spinlock.h>
#include <kernel/mr_timer.h>
#include <libc/mr_malloc.h>

static mr_ktype_t ktype1, ktype2;
static mr_kset_t kroot = MR_KSET_INIT(kroot, MR_NULL);
static mr_list_t klist = MR_LIST_INIT(klist);
static mr_spinlock_t klock = MR_SPINLOCK_INIT();

MR_INLINE void timer_init(mr_timer_t *timer, mr_ptr_t entry, void *args,
                          mr_tick_t tick) {
    /* Init timer */
    mr_list_init(&timer->list);
    timer->init_tick = tick;
    timer->timeout_tick = 0;
    timer->entry = entry;
    timer->args = args;
}

MR_INLINE mr_err_t timer_init_add(mr_timer_t *timer, mr_ktype_t *ktype,
                                  const char *name, mr_ptr_t entry, void *args,
                                  mr_tick_t tick) {
    /* Init timer */
    timer_init(timer, entry, args, tick);

    /* Init kobject */
    mr_kobject_init((mr_kobject_t *)timer, ktype);

    /* Add kobject to kroot */
    return mr_kobject_add((mr_kobject_t *)timer, (mr_kobject_t *)&kroot, name);
}

mr_err_t mr_timer_init(mr_timer_t *timer, const char *name,
                       void (*entry)(mr_timer_t *, void *), void *args,
                       mr_tick_t tick) {
    /* Check arguments */
    if ((!timer) || (MR_KOBJECT_IS_INITED(timer)) || (!name) || (!entry)
        || (tick == 0)) {
        return -MR_EINVAL;
    }

    /* Init and add timer */
    return timer_init_add(timer, &ktype1, name, (mr_ptr_t)entry, args, tick);
}

mr_timer_t *mr_timer_create(const char *name,
                            void (*entry)(mr_timer_t *, void *), void *args,
                            mr_tick_t tick) {
    mr_timer_t *timer;
    mr_err_t ret;

    /* Check arguments */
    if ((!name) || (!entry) || (tick == 0)) {
        return MR_NULL;
    }

    /* Create timer */
    timer = mr_malloc(sizeof(mr_timer_t));
    if (!timer) {
        return MR_NULL;
    }

    /* Init and add timer */
    ret = timer_init_add(timer, &ktype2, name, (mr_ptr_t)entry, args, tick);
    if (ret != 0) {
        /* Free timer */
        mr_free(timer);
        return MR_NULL;
    }
    return timer;
}

MR_INLINE void timer_remove(mr_timer_t *timer) {
    /* Stop timer if not stopped */
    if (!mr_list_is_empty(&timer->list)) {
        mr_list_del(&timer->list);
    }
}

MR_INLINE void timer_del(mr_timer_t *timer) {
    int mask;

    /* Lock timer list */
    mask = mr_spinlock_lock_irqsave(&klock);

    /* Remove timer from timer list */
    timer_remove(timer);

    /* Unlock timer list */
    mr_spinlock_unlock_irqrestore(&klock, mask);
}

mr_err_t mr_timer_del(mr_timer_t *timer) {
    /* Get timer */
    timer = (mr_timer_t *)mr_kobject_get((mr_kobject_t *)timer);
    if (!timer) {
        return -MR_EINVAL;
    }

    /* Delete timer from timer list */
    timer_del(timer);

    /* Delete kobject */
    mr_kobject_del((mr_kobject_t *)timer);

    /* Put timer */
    mr_kobject_put((mr_kobject_t *)timer);
    return 0;
}

MR_INLINE void timer_insert(mr_timer_t *timer) {
    mr_tick_t tick;
    mr_timer_t *t;
    mr_list_t *l;

    /* Set timeout tick */
    tick = timer->init_tick & ~MR_TIMER_PERIODIC;
    timer->timeout_tick = (mr_tick_t)mr_clock_get() + tick;

    /* Insert timer into timer list */
    MR_LIST_FOR_EACH(l, &klist) {
        t = MR_LIST_ENTRY(l, mr_timer_t, list);
        if ((timer->timeout_tick - t->timeout_tick) < (MR_TICK_MAX / 2)) {
            continue;
        }

        /* Move timer into position */
        mr_list_move(&timer->list, &t->list);
        return;
    }

    /* It is the last timer */
    mr_list_move(&timer->list, &klist);
}

MR_INLINE void timer_start(mr_timer_t *timer) {
    int mask;

    /* Lock timer list */
    mask = mr_spinlock_lock_irqsave(&klock);

    /* Insert timer into timer list */
    timer_insert(timer);

    /* Unlock timer list */
    mr_spinlock_unlock_irqrestore(&klock, mask);
}

mr_err_t mr_timer_start(mr_timer_t *timer) {
    /* Get timer */
    timer = (mr_timer_t *)mr_kobject_get((mr_kobject_t *)timer);
    if (!timer) {
        return -MR_EINVAL;
    }

    /* Start timer */
    timer_start(timer);

    /* Put timer */
    mr_kobject_put((mr_kobject_t *)timer);
    return 0;
}

mr_err_t mr_timer_stop(mr_timer_t *timer) {
    /* Get timer */
    timer = (mr_timer_t *)mr_kobject_get((mr_kobject_t *)timer);
    if (!timer) {
        return -MR_EINVAL;
    }

    /* Delete timer from timer list */
    timer_del(timer);

    /* Put timer */
    mr_kobject_put((mr_kobject_t *)timer);
    return 0;
}

MR_INLINE void timer_tick_set(mr_timer_t *timer, mr_tick_t tick) {
    int mask;

    /* Lock timer list */
    mask = mr_spinlock_lock_irqsave(&klock);

    /* Set init tick */
    timer->init_tick = tick;

    /* Unlock timer list */
    mr_spinlock_unlock_irqrestore(&klock, mask);
}

mr_err_t mr_timer_tick_set(mr_timer_t *timer, mr_tick_t tick) {
    /* Check arguments */
    if (tick == 0) {
        return -MR_EINVAL;
    }

    /* Get timer */
    timer = (mr_timer_t *)mr_kobject_get((mr_kobject_t *)timer);
    if (!timer) {
        return -MR_EINVAL;
    }

    /* Delete timer from timer list */
    timer_del(timer);

    /* Set timer tick */
    timer_tick_set(timer, tick);

    /* Put timer */
    mr_kobject_put((mr_kobject_t *)timer);
    return 0;
}

MR_INLINE void timer_release1_kobj(mr_kobject_t *kobj) {
    mr_timer_t *timer;

    /* Get timer */
    timer = MR_CONTAINER_OF(kobj, mr_timer_t, parent);

    /* Delete timer from timer list */
    timer_del(timer);
}

MR_INLINE void timer_release2_kobj(mr_kobject_t *kobj) {
    mr_timer_t *timer;

    /* Get timer */
    timer = MR_CONTAINER_OF(kobj, mr_timer_t, parent);

    /* Delete timer from timer list */
    timer_del(timer);

    /* Free timer */
    mr_free(timer);
}

MR_INLINE void timer_timeout_check(void) {
    mr_timer_t *timer;
    mr_tick_t tick;
    mr_list_t list;
    int mask;

    /* Lock timer list */
    mask = mr_spinlock_lock_irqsave(&klock);

    /* Check timeout timer */
    while (!mr_list_is_empty(&klist)) {
        timer = MR_LIST_ENTRY(klist.next, mr_timer_t, list);

        /* Get current tick */
        tick = (mr_tick_t)mr_clock_get();
        if ((tick - timer->timeout_tick) >= (MR_TICK_MAX / 2)) {
            break;
        }

        /* Move timer into operation list */
        mr_list_init(&list);
        mr_list_move(&timer->list, &list);

        /* Unlock timer list */
        mr_spinlock_unlock_irqrestore(&klock, mask);

        /* Call timer entry */
        ((void (*)(mr_timer_t *, void *))timer->entry)(timer, timer->args);

        /* Lock timer list */
        mask = mr_spinlock_lock_irqsave(&klock);

        /* Timer is no longer yours to operate */
        if (mr_list_is_empty(&list)) {
            continue;
        }

        /* Tick is periodic */
        if (timer->init_tick & MR_TIMER_PERIODIC) {
            timer_insert(timer);
        }
    }

    /* Unlock timer list */
    mr_spinlock_unlock_irqrestore(&klock, mask);
}

MR_INLINE void mr_timer_kset_init(void) {
    mr_err_t ret;

    /* Register kset */
    ret = mr_kset_register(&kroot, "timer");
    if (ret != 0) {
        /* It won't hook the clock, so it won't start */
        return;
    }

    /* Init ktype */
    mr_ktype_init(&ktype1, timer_release1_kobj);
    mr_ktype_init(&ktype2, timer_release2_kobj);

    /* Register clock hook */
    mr_clock_hook_add(-1, timer_timeout_check);
}
MR_INIT_EXPORT(mr_timer_kset_init, MR_INIT_LEVEL_KERNEL);

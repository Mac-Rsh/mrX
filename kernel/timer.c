/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/kernel/mr_timer.h>
#if defined(MR_USE_TIMER)
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/libc/mr_malloc.h>

/* Timer clazzes(static & dynamic) */
static void timer_statik_release(mr_object_t *obj);
static void timer_dynamic_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(timer, timer_statik_release, timer_dynamic_release);

/* Timer timing list */
static mr_list_t __timer_list = MR_LIST_INIT(&__timer_list);
/* Timer lock */
static mr_spinlock_t __timer_lock = MR_SPINLOCK_INIT();

static mr_err_t timer_init(mr_timer_t *timer, const mr_clazz_t *clazz,
                           mr_ptr_t entry, mr_ptr_t param, mr_tick_t tick) {
    /* Init timer */
    mr_list_init(&timer->list);
    timer->itick = tick;
    timer->entry = entry;
    timer->param = param;

    /* Init object */
    return mr_object_init((mr_object_t *)timer, clazz);
}

mr_err_t mr_timer_init(mr_timer_t *timer, mr_timer_entry_t *entry, void *param,
                       mr_tick_t tick) {
    const mr_clazzes_t *clazzes = MR_CLAZZES_FIND(timer);
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(clazzes);

    /* Check parameter */
    MR_ASSERT((timer != MR_NULL) && (!MR_OBJECT_IS_INITED(timer)));
    MR_ASSERT((entry != MR_NULL) && (tick > 0));

    /* Init timer */
    return timer_init(timer, clazz, entry, param, tick);
}

mr_timer_t *mr_timer_create(mr_timer_entry_t *entry, void *param,
                            mr_tick_t tick) {
    const mr_clazzes_t *clazzes = MR_CLAZZES_FIND(timer);
    const mr_clazz_t *clazz = MR_CLAZZES_DYNAMIC(clazzes);
    mr_timer_t *timer;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((entry != MR_NULL) && (tick > 0));

    /* Create timer */
    timer = mr_malloc(sizeof(mr_timer_t));
    if (!timer) {
        return MR_NULL;
    }

    /* Init timer */
    ret = timer_init(timer, clazz, entry, param, tick);
    if (ret != 0) {
        mr_free(timer);
        return MR_NULL;
    }
    return timer;
}

static void timer_remove(mr_timer_t *timer) {
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__timer_lock);

    /* Remove timer from timing list */
    mr_list_del(&timer->list);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__timer_lock, mask);
}

mr_err_t mr_timer_del(mr_timer_t *timer) {
    /* Check parameter */
    MR_ASSERT((timer != MR_NULL) && MR_OBJECT_IS_INITED(timer));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(timer, timer));

    /* Remove timer */
    timer_remove(timer);

    /* Delete object */
    mr_object_del((mr_object_t *)timer);
    return 0;
}

static void timer_insert(mr_timer_t *timer) {
    mr_tick_t tick;
    mr_timer_t *t;
    mr_list_t *l;

    /* Set timeout tick */
    tick = timer->itick & ~MR_TIMER_PERIODIC;
    timer->ttick = mr_clock_tick() + tick;

    /* Insert timer into timing list */
    MR_LIST_FOR_EACH(l, &__timer_list) {
        t = MR_LIST_ENTRY(l, mr_timer_t, list);

        /* Compare timeout tick */
        if ((timer->ttick - t->ttick) < (MR_TICK_MAX / 2)) {
            continue;
        }

        /* Move timer into position */
        mr_list_move(&timer->list, &t->list);
        return;
    }

    /* It is the last timer */
    mr_list_move(&timer->list, &__timer_list);
}

mr_err_t mr_timer_start(mr_timer_t *timer) {
    int mask;

    /* Check parameter */
    MR_ASSERT((timer != MR_NULL) && MR_OBJECT_IS_INITED(timer));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(timer, timer));

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__timer_lock);

    /* Insert timer */
    timer_insert(timer);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__timer_lock, mask);
    return 0;
}

mr_err_t mr_timer_stop(mr_timer_t *timer) {
    /* Check parameter */
    MR_ASSERT((timer != MR_NULL) && MR_OBJECT_IS_INITED(timer));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(timer, timer));

    /* Remove timer */
    timer_remove(timer);
    return 0;
}

mr_err_t mr_timer_tick_set(mr_timer_t *timer, mr_tick_t tick) {
    int mask;

    /* Check parameter */
    MR_ASSERT((timer != MR_NULL) && MR_OBJECT_IS_INITED(timer));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(timer, timer));
    MR_ASSERT(tick > 0);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__timer_lock);

    /* Remove timer from timing list */
    mr_list_del(&timer->list);

    /* Set init tick */
    timer->itick = tick;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__timer_lock, mask);
    return 0;
}

static void timer_statik_release(mr_object_t *obj) {
    mr_timer_t *timer;

    /* Get timer */
    timer = MR_CONTAINER_OF(obj, mr_timer_t, parent);

    /* Remove timer */
    timer_remove(timer);
}

static void timer_dynamic_release(mr_object_t *obj) {
    /* Release timer */
    timer_statik_release(obj);

    /* Free timer */
    mr_free(obj);
}

static mr_tick_t timer_timeout_check(void) {
    mr_tick_t tless, tick;
    mr_timer_entry_t *entry;
    mr_timer_t *timer;
    mr_list_t oplist;
    void *param;
    int mask;

    /* Init operation list */
    mr_list_init(&oplist);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__timer_lock);

    /* Check timeout timer */
    for (tless = 0, tick = mr_clock_tick(); !mr_list_is_empty(&__timer_list);) {
        timer = MR_LIST_ENTRY(__timer_list.next, mr_timer_t, list);

        /* Check timeout tick */
        if ((tick - timer->ttick) >= (MR_TICK_MAX / 2)) {
            /* There will be no timeout timer after that */
            tless = timer->ttick - tick;
            break;
        }

        /* Move timer into operation list */
        mr_list_move(&timer->list, &oplist);

        /* Save timer entry and param */
        entry = (mr_timer_entry_t *)timer->entry;
        param = (void *)timer->param;

        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__timer_lock, mask);

        /* Call timer entry */
        entry(timer, param);

        /* Lock */
        mask = mr_spinlock_lock_irqsave(&__timer_lock);

        /* Check that the timer has not been moved */
        if (mr_list_is_empty(&oplist)) {
            /* Timer is no longer yours to operate */
            continue;
        }

        /* Tick is periodic */
        if (timer->itick & MR_TIMER_PERIODIC) {
            /* Restart timer */
            timer_insert(timer);
        }
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__timer_lock, mask);
    return tless;
}

void mr_kernel_timer_init(void) {
    mr_clock_hook_add(timer_timeout_check);
}
#endif /* defined(MR_USE_TIMER) */

/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_kworkqueue.h>
#if defined(MR_USE_KWORKQUEUE)
#include <kernel/mr_kspinlock.h>
#include <kernel/mr_initcall.h>
#include <libc/mr_malloc.h>

/* Kworkqueue type */
static mr_ktype_t ktype1, ktype2;
/* Kworkqueue lock */
static mr_kspinlock_t klock = MR_KSPINLOCK_INIT();
/* Kworkqueue set */
static mr_kset_t kroot = MR_KSET_INIT(&kroot, MR_NULL);

MR_INLINE void kqueue_wakeup(mr_kworkqueue_t *kqueue) {
    mr_atomic_t no_running;

    /* Check if kworkqueue need wakeup */
    mr_atomic_init(&no_running, MR_FALSE);
    if (!mr_atomic_compare_exchange(&kqueue->running, &no_running, MR_TRUE)) {
        /* No need wakeup */
        return;
    }

    /* Get kworkqueue run-time(work) */
    mr_kworkqueue_get(kqueue);

    /* Wakeup kworkqueue */
#if defined(MR_USE_KWORKQUEUE_HOOK)
    if (kqueue->wakeup) {
        ((mr_kworkqueue_hook_t *)kqueue->wakeup)(kqueue, kqueue->args);
    }
#endif /* defined(MR_USE_KWORKQUEUE_HOOK) */
}

MR_INLINE void kqueue_timer_entry(mr_ktimer_t *ktimer, void *args) {
    mr_kworkqueue_t *kqueue;
    mr_kwork_t *kwork;
    mr_tick_t tick;
    int mask;

    MR_UNUSED(args);

    /* Get kworkqueue */
    kqueue = mr_kworkqueue_get(MR_CONTAINER_OF(ktimer, mr_kworkqueue_t, timer));
    if (!kqueue) {
        /* Kworkqueue is not exist */
        mr_ktimer_del(ktimer);
        return;
    }

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Check timeout kwork */
    for (tick = 0; !mr_klist_is_empty(&kqueue->tlist);) {
        kwork = MR_CONTAINER_OF(kqueue->tlist.next, mr_kwork_t, list);

        /* Check timeout tick */
        if ((mr_kclock_tick() - kwork->tick) < (MR_TICK_MAX / 2)) {
            /* Move kwork to kworkqueue work list */
            mr_klist_move(&kwork->list, &kqueue->list);
            continue;
        }

        /* There will be no timeout kwork after that */
        tick = kwork->tick - mr_kclock_tick();
        break;
    }

    /* Check if kworkqueue need timeout */
    if ((tick != 0) && (tick != mr_ktimer_tick(&kqueue->timer))) {
        /* Set to optimal tick */
        mr_ktimer_tick_set(&kqueue->timer, tick | MR_KTIMER_PERIODIC);
        mr_ktimer_start(&kqueue->timer);

        /* Check if kworkqueue is stopped(delay-work) for the last time */
    } else if (mr_klist_is_empty(&kqueue->tlist)) {
        /* Don't need timer */
        mr_ktimer_stop(&kqueue->timer);

        /* Put kworkqueue run-time(delay-work) */
        mr_kworkqueue_put(kqueue);
    }

    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);

    /* Wakeup kworkqueue */
    kqueue_wakeup(kqueue);

    /* Put kworkqueue */
    mr_kworkqueue_put(kqueue);
}

MR_INLINE mr_err_t kqueue_init(mr_kworkqueue_t *kqueue, const char *name) {
    mr_err_t ret;

    /* Init kworkqueue timer */
    ret = mr_ktimer_init(&kqueue->timer, name, kqueue_timer_entry, MR_NULL, 1);
    if (ret != 0) {
        return ret;
    }

    /* Init kworkqueue */
    mr_atomic_init(&kqueue->running, MR_FALSE);
    mr_atomic_init(&kqueue->dying, MR_FALSE);
    mr_klist_init(&kqueue->tlist);
    mr_klist_init(&kqueue->list);
#if defined(MR_USE_KWORKQUEUE_HOOK)
    kqueue->wakeup = MR_NULL;
    kqueue->suspend = MR_NULL;
    kqueue->args = MR_NULL;
#endif /* defined(MR_USE_KWORKQUEUE_HOOK) */
    return 0;
}

MR_INLINE void kqueue_del(mr_kworkqueue_t *kqueue) {
    mr_kwork_t *kwork;
    int mask;

    /* Delete kworkqueue timer */
    mr_ktimer_del(&kqueue->timer);

    /* Mark as dying */
    mr_atomic_store(&kqueue->dying, MR_TRUE);

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Remove kwork from kworkqueue timer list */
    while (!mr_klist_is_empty(&kqueue->tlist)) {
        kwork = MR_KLIST_ENTRY(&kqueue->tlist.next, mr_kwork_t, list);
        mr_klist_del(&kwork->list);
    }

    /* Remove kwork from kworkqueue list */
    while (!mr_klist_is_empty(&kqueue->list)) {
        kwork = MR_KLIST_ENTRY(&kqueue->list.next, mr_kwork_t, list);
        mr_klist_del(&kwork->list);
    }

    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);
}

MR_INLINE mr_err_t kqueue_init_add(mr_kworkqueue_t *kqueue, mr_ktype_t *ktype,
                                   const char *name) {
    mr_err_t ret;

    /* Init kworkqueue */
    ret = kqueue_init(kqueue, name);
    if (ret != 0) {
        return ret;
    }

    /* Init kobject */
    mr_kobject_init((mr_kobject_t *)kqueue, ktype);

    /* Add kobject to kroot */
    ret = mr_kobject_add((mr_kobject_t *)kqueue, (mr_kobject_t *)&kroot, name);
    if (ret != 0) {
        kqueue_del(kqueue);
        return ret;
    }
    return 0;
}

mr_err_t mr_kworkqueue_init(mr_kworkqueue_t *kqueue, const char *name) {
    /* Check arguments */
    if ((!kqueue) || MR_KWORKQUEUE_IS_INITED(kqueue) || (!name)) {
        return -MR_EINVAL;
    }

    /* Init and add kworkqueue */
    return kqueue_init_add(kqueue, &ktype1, name);
}

mr_kworkqueue_t *mr_kworkqueue_create(const char *name) {
    mr_kworkqueue_t *kqueue;
    mr_err_t ret;

    /* Check arguments */
    if (!name) {
        return MR_NULL;
    }

    /* Create kworkqueue */
    kqueue = mr_malloc(sizeof(mr_kworkqueue_t));
    if (!kqueue) {
        return MR_NULL;
    }

    /* Init and add kworkqueue */
    ret = kqueue_init_add(kqueue, &ktype2, name);
    if (ret != 0) {
        mr_free(kqueue);
        return MR_NULL;
    }
    return kqueue;
}

mr_err_t mr_kworkqueue_del(mr_kworkqueue_t *kqueue) {
    /* Check arguments */
    if ((!kqueue) || (!MR_KWORKQUEUE_IS_INITED(kqueue))) {
        return -MR_EINVAL;
    }

    /* Mark as dying */
    mr_atomic_store(&kqueue->dying, MR_TRUE);

    /* Delete kobject */
    mr_kobject_del((mr_kobject_t *)kqueue);
    return 0;
}

MR_INLINE void kqueue_suspend(mr_kworkqueue_t *kqueue) {
    mr_atomic_t running;

    /* Check if kworkqueue need suspend */
    mr_atomic_init(&running, MR_TRUE);
    if (!mr_atomic_compare_exchange(&kqueue->running, &running, MR_FALSE)) {
        return;
    }

    /* Put kworkqueue run-time(work) */
    mr_kworkqueue_put(kqueue);

    /* Suspend kworkqueue */
#if defined(MR_USE_KWORKQUEUE_HOOK)
    if (kqueue->suspend) {
        ((mr_kworkqueue_hook_t *)kqueue->suspend)(kqueue, kqueue->args);
    }
#endif /* defined(MR_USE_KWORKQUEUE_HOOK) */
}

mr_err_t mr_kworkqueue_execute(mr_kworkqueue_t *kqueue) {
    mr_kwork_entry_t *entry;
    mr_kwork_t *kwork;
    void *args;
    int mask;

    /* Check arguments */
    if ((!kqueue) || (!MR_KWORKQUEUE_IS_INITED(kqueue))) {
        return -MR_EINVAL;
    }

    /* Reduce unnecessary lock operations */
    if (mr_klist_is_empty(&kqueue->list)
#if defined(MR_USE_KWORKQUEUE_HOOK)
        && (!kqueue->suspend)
#endif /* defined(MR_USE_KWORKQUEUE_HOOK) */
    ) {
        return 0;
    }

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Check kworkqueue work list */
    while (!mr_klist_is_empty(&kqueue->list)) {
        kwork = MR_KLIST_ENTRY(kqueue->list.next, mr_kwork_t, list);

        /* Remove kwork from kworkqueue list */
        mr_klist_del(&kwork->list);

        /* Check if kwork is deleted */
        if (!kwork->entry) {
            continue;
        }

        /* Save kwork entry and args */
        entry = kwork->entry;
        args = kwork->args;

        /* Unlock */
        mr_kspinlock_unlock_irqrestore(&klock, mask);

        /* Call kwork entry */
        entry(kwork, args);

        /* Lock */
        mask = mr_kspinlock_lock_irqsave(&klock);
    }

    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);

    /* Suspend kworkqueue */
    kqueue_suspend(kqueue);
    return 0;
}

mr_err_t mr_kworkqueue_work(mr_kworkqueue_t *kqueue, mr_kwork_t *kwork) {
    mr_err_t ret;
    int mask;

    /* Check arguments */
    if ((!kqueue) || (!MR_KWORKQUEUE_IS_INITED(kqueue)) || (!kwork)
        || (!MR_KWORK_IS_INITED(kwork))) {
        return -MR_EINVAL;
    }

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Check if kwork is working */
    if (MR_KWORK_IS_WORKING(kwork)) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Check if kworkqueue is dying */
    if (mr_atomic_load(&kqueue->dying)) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* Add kwork to kworkqueue list */
    mr_klist_add(&kqueue->list, &kwork->list);
    kwork->kqueue = kqueue;

    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);

    /* Wakeup kworkqueue */
    kqueue_wakeup(kqueue);
    return 0;
_exit:
    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);
    return ret;
}

mr_err_t mr_kworkqueue_delayed_work(mr_kworkqueue_t *kqueue, mr_kwork_t *kwork,
                                    mr_tick_t tick) {
    mr_klist_t *l;
    mr_kwork_t *w;
    mr_err_t ret;
    int mask;

    /* Check arguments */
    if ((!kqueue) || (!MR_KWORKQUEUE_IS_INITED(kqueue)) || (!kwork)
        || (!MR_KWORK_IS_INITED(kwork)) || (tick == 0)
        || (tick >= (MR_TICK_MAX / 2))) {
        return -MR_EINVAL;
    }

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Check if kwork is working */
    if (MR_KWORK_IS_WORKING(kwork)) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Check if kworkqueue is dying */
    if (mr_atomic_load(&kqueue->dying)) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* Check if kworkqueue is started for the first time */
    if (mr_klist_is_empty(&kqueue->tlist)) {
        /* Get kworkqueue run-time(delay-work) */
        mr_kworkqueue_get(kqueue);
    }

    /* Set timeout tick */
    tick = tick & ~MR_KTIMER_PERIODIC;
    kwork->tick = mr_kclock_tick() + tick;

    /* Insert kwork into kworkqueue timer list */
    MR_KLIST_FOR_EACH(l, &kqueue->tlist) {
        w = MR_KLIST_ENTRY(l, mr_kwork_t, list);
        if ((kwork->tick - w->tick) < (MR_TICK_MAX / 2)) {
            continue;
        }

        /* Move kwork into position */
        mr_klist_move(&kwork->list, &w->list);
        goto _set;
    }

    /* It is the last kwork */
    mr_klist_move(&kwork->list, &kqueue->tlist);
_set:
    /* Check if kwork is first */
    if (&kwork->list == kqueue->tlist.next) {
        /* Set to optimal tick */
        tick = kwork->tick - mr_kclock_tick();
        mr_ktimer_tick_set(&kqueue->timer, tick | MR_KTIMER_PERIODIC);
        mr_ktimer_start(&kqueue->timer);
    }

    /* Mark kwork as in kworkqueue */
    kwork->kqueue = kqueue;
    ret = 0;
_exit:
    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);
    return ret;
}

#if defined(MR_USE_KWORKQUEUE_HOOK)
mr_err_t mr_kworkqueue_hook_wakeup_set(mr_kworkqueue_t *kqueue,
                                       mr_kworkqueue_hook_t *hook) {
    /* Check arguments */
    if ((!kqueue) || (!MR_KWORKQUEUE_IS_INITED(kqueue))) {
        return -MR_EINVAL;
    }

    /* Set kworkqueue wakeup hook */
    kqueue->wakeup = hook;
    return 0;
}

mr_err_t mr_kworkqueue_hook_suspend_set(mr_kworkqueue_t *kqueue,
                                        mr_kworkqueue_hook_t *hook) {
    /* Check arguments */
    if ((!kqueue) || (!MR_KWORKQUEUE_IS_INITED(kqueue))) {
        return -MR_EINVAL;
    }

    /* Set kworkqueue suspend hook */
    kqueue->suspend = hook;
    return 0;
}

mr_err_t mr_kworkqueue_hook_args_set(mr_kworkqueue_t *kqueue, void *args) {
    /* Check arguments */
    if ((!kqueue) || (!MR_KWORKQUEUE_IS_INITED(kqueue))) {
        return -MR_EINVAL;
    }

    /* Set kworkqueue hook args */
    kqueue->args = args;
    return 0;
}
#endif /* defined(MR_USE_KWORKQUEUE_HOOK) */

mr_err_t mr_kwork_init(mr_kwork_t *kwork, mr_kwork_entry_t *entry, void *args) {
    /* Check arguments */
    if ((!kwork) || (!entry)) {
        return -MR_EINVAL;
    }

    /* Init kwork */
    mr_klist_init(&kwork->list);
    kwork->kqueue = MR_NULL;
    kwork->entry = entry;
    kwork->args = args;
    return 0;
}

mr_err_t mr_kwork_del(mr_kwork_t *kwork) {
    int mask;

    /* Check arguments */
    if ((!kwork) || (!MR_KWORK_IS_INITED(kwork))) {
        return -MR_EINVAL;
    }

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Check if kwork is working */
    if (MR_KWORK_IS_WORKING(kwork)) {
        /* Remove kwork from kworkqueue list */
        mr_klist_del(&kwork->list);
    }

    /* Mark kwork as deleted */
    kwork->entry = MR_NULL;

    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);
    return 0;
}

mr_err_t mr_kwork_schedule(mr_kwork_t *kwork) {
    mr_kworkqueue_t *kqueue;
    mr_err_t ret;

    /* Check arguments */
    if ((!kwork) || (!MR_KWORK_IS_INITED(kwork)) || (!kwork->kqueue)) {
        return -MR_EINVAL;
    }

    /* Get kworkqueue */
    kqueue = mr_kworkqueue_get(kwork->kqueue);
    if (!kqueue) {
        return -MR_ENOENT;
    }

    /* Schedule kwork to the used kworkqueue */
    ret = mr_kworkqueue_work(kqueue, kwork);

    /* Put kworkqueue */
    mr_kworkqueue_put(kqueue);
    return ret;
}

mr_err_t mr_kwork_delayed_schedule(mr_kwork_t *kwork, mr_tick_t tick) {
    mr_kworkqueue_t *kqueue;
    mr_err_t ret;

    /* Check arguments */
    if ((!kwork) || (!MR_KWORK_IS_INITED(kwork)) || (!kwork->kqueue)
        || (tick == 0) || (tick >= (MR_TICK_MAX / 2))) {
        return -MR_EINVAL;
    }

    /* Get kworkqueue */
    kqueue = mr_kworkqueue_get(kwork->kqueue);
    if (!kqueue) {
        return -MR_ENOENT;
    }

    /* Schedule kwork to the used kworkqueue */
    ret = mr_kworkqueue_delayed_work(kqueue, kwork, tick);

    /* Put kworkqueue */
    mr_kworkqueue_put(kqueue);
    return ret;
}

mr_kworkqueue_t *mr_kworkqueue_find(const char *name) {
    /* Lookup kworkqueue in kroot */
    return (mr_kworkqueue_t *)mr_kobject_lookup((mr_kobject_t *)&kroot, name);
}

MR_INLINE mr_kworkqueue_t *kqueue_kobj_release(mr_kobject_t *kobj) {
    mr_kworkqueue_t *kqueue;

    /* Get kworkqueue */
    kqueue = MR_CONTAINER_OF(kobj, mr_kworkqueue_t, parent);

    /* Delete kworkqueue */
    kqueue_del(kqueue);
    return kqueue;
}

MR_INLINE void kqueue_kobj_release1(mr_kobject_t *kobj) {
    /* Release kworkqueue */
    kqueue_kobj_release(kobj);
}

MR_INLINE void kqueue_kobj_release2(mr_kobject_t *kobj) {
    mr_kworkqueue_t *kqueue;

    /* Release kworkqueue */
    kqueue = kqueue_kobj_release(kobj);

    /* Free kworkqueue */
    mr_free(kqueue);
}

void mr_kworkqueue_kernel_init(void) {
    /* Init ktype */
    mr_ktype_init(&ktype1, "workqueue", kqueue_kobj_release1, MR_NULL);
    mr_ktype_init(&ktype2, "workqueue", kqueue_kobj_release2, MR_NULL);

    /* Register kset */
    mr_kset_register(&kroot, "workqueue");
}
#endif /* defined(MR_USE_KWORKQUEUE) */

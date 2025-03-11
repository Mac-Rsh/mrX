/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_kworkqueue.h>
#if defined(MR_USE_KWORKQUEUE) && defined(MR_USE_KTIMER)
#include <kernel/mr_initcall.h>
#include <kernel/mr_kspinlock.h>
#include <libc/mr_malloc.h>

/* Kworkqueue ktype */
static mr_ktype_t ktype1, ktype2;
/* Kworkqueue lock */
static mr_kspinlock_t klock = MR_KSPINLOCK_INIT();
/* Kworkqueue kset */
static mr_kset_t kroot = MR_KSET_INIT(&kroot, MR_NULL);

MR_INLINE void kqueue_timer_entry(mr_ktimer_t *ktimer, void *args) {
    mr_kworkqueue_t *kqueue;
    mr_kwork_t *kwork;
    mr_bool_t wakeup;
    mr_tick_t tick;
    int mask;

    /* Get kworkqueue */
    kqueue = mr_kworkqueue_get(MR_CONTAINER_OF(ktimer, mr_kworkqueue_t, timer));
    if (!kqueue) {
        /* Kworkqueue is not exist */
        return;
    }

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Check timeout kwork */
    for (wakeup = MR_FALSE, tick = 0; !mr_klist_is_empty(&kqueue->tlist);) {
        kwork = MR_CONTAINER_OF(kqueue->tlist.next, mr_kwork_t, list);

        /* Check timeout tick */
        if ((mr_kclock_tick() - kwork->tick) < (MR_TICK_MAX / 2)) {
            /* Check kworkqueue is started(work) for the first time */
            if (mr_klist_is_empty(&kqueue->list)) {
                /* Need wakeup */
                wakeup = MR_TRUE;
            }

            /* Move kwork to kworkqueue work list */
            mr_klist_move(&kwork->list, &kqueue->list);
            continue;
        }

        /* There will be no timeout kwork after that */
        tick = kwork->tick - mr_kclock_tick();
        break;
    }

    /* Check is need timer */
    if ((tick != 0) && (tick != MR_KTIMER_TICK(&kqueue->timer))) {
        /* Set to optimal tick */
        mr_ktimer_tick_set(&kqueue->timer, tick | MR_KTIMER_PERIODIC);
        mr_ktimer_start(&kqueue->timer);

        /* Check kworkqueue is stopped(delay-work) for the last time */
    } else if (mr_klist_is_empty(&kqueue->tlist)) {
        /* Don't need timer */
        mr_ktimer_stop(&kqueue->timer);

        /* Put kworkqueue run-time(delay-work) */
        mr_kworkqueue_put(kqueue);
    }

    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);

    /* Check is need wakeup */
    if (wakeup) {
        /* Get kworkqueue run-time(work) */
        mr_kworkqueue_get(kqueue);
#if defined(MR_USE_KWORKQUEUE_HOOK)
        if (kqueue->wakeup) {
            ((void (*)(mr_kworkqueue_t *))kqueue->wakeup)(kqueue);
        }
#endif /* defined(MR_USE_KWORKQUEUE_HOOK) */
    }

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
    mr_atomic_init(&kqueue->dying, MR_FALSE);
    mr_klist_init(&kqueue->tlist);
    mr_klist_init(&kqueue->list);
#if defined(MR_USE_KWORKQUEUE_HOOK)
    kqueue->wakeup = MR_NULL;
    kqueue->suspend = MR_NULL;
#endif /* defined(MR_USE_KWORKQUEUE_HOOK) */
    return 0;
}

MR_INLINE void kqueue_del(mr_kworkqueue_t *kqueue) {
    mr_kwork_t *w;
    int mask;

    /* Delete kworkqueue timer */
    mr_ktimer_del(&kqueue->timer);

    /* Mark as dying */
    mr_atomic_store(&kqueue->dying, MR_TRUE);

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Remove kwork from kworkqueue timer list */
    while (!mr_klist_is_empty(&kqueue->tlist)) {
        w = MR_KLIST_ENTRY(&kqueue->tlist.next, mr_kwork_t, list);
        mr_klist_del(&w->list);
    }

    /* Remove kwork from kworkqueue list */
    while (!mr_klist_is_empty(&kqueue->list)) {
        w = MR_KLIST_ENTRY(&kqueue->list.next, mr_kwork_t, list);
        mr_klist_del(&w->list);
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
    }
    return ret;
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

MR_INLINE void kqueue_execute(mr_kworkqueue_t *kqueue) {
    mr_kwork_t *kwork;
    mr_ptr_t entry;
    void *args;
    int mask;

    /* Reduce unnecessary lock operations */
    if (mr_klist_is_empty(&kqueue->list)
#if defined(MR_USE_KWORKQUEUE_HOOK)
        && (!kqueue->suspend)
#endif /* defined(MR_USE_KWORKQUEUE_HOOK) */
    ) {
        return;
    }

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Check kworkqueue work list */
    while (!mr_klist_is_empty(&kqueue->list)) {
        kwork = MR_KLIST_ENTRY(kqueue->list.next, mr_kwork_t, list);

        /* Remove kwork from kworkqueue list */
        mr_klist_del(&kwork->list);

        /* Save kwork entry and args */
        entry = kwork->entry;
        args = kwork->args;

        /* Unlock */
        mr_kspinlock_unlock_irqrestore(&klock, mask);

        /* Call kwork entry */
        ((void (*)(mr_kwork_t *, void *))entry)(kwork, args);

        /* Lock */
        mask = mr_kspinlock_lock_irqsave(&klock);
    }

    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);

    /* Put kworkqueue run-time(work) */
    mr_kworkqueue_put(kqueue);
#if defined(MR_USE_KWORKQUEUE_HOOK)
    if (kqueue->suspend) {
        ((void (*)(mr_kworkqueue_t *))kqueue->suspend)(kqueue);
    }
#endif /* defined(MR_USE_KWORKQUEUE_HOOK) */
}

mr_err_t mr_kworkqueue_execute(mr_kworkqueue_t *kqueue) {
    /* Check arguments */
    if ((!kqueue) || (!MR_KWORKQUEUE_IS_INITED(kqueue))) {
        return -MR_EINVAL;
    }

    /* Execute kworkqueue */
    kqueue_execute(kqueue);
    return 0;
}

mr_err_t mr_kwork_init(mr_kwork_t *kwork,
                       void (*entry)(mr_kwork_t *kwork, void *args),
                       void *args) {
    /* Check arguments */
    if ((!kwork) || (!entry)) {
        return -MR_EINVAL;
    }

    /* Init kwork */
    mr_klist_init(&kwork->list);
    kwork->entry = entry;
    kwork->args = args;
    return 0;
}

MR_INLINE void kwork_del(mr_kwork_t *kwork) {
    int mask;

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Check kwork is deleted */
    if (!mr_klist_is_empty(&kwork->list)) {
        /* Remove kwork from kworkqueue list */
        mr_klist_del(&kwork->list);
    }

    /* Mark kwork as deleted */
    kwork->entry = MR_NULL;

    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);
}

mr_err_t mr_kwork_del(mr_kwork_t *kwork) {
    /* Check arguments */
    if ((!kwork) || (!MR_KWORK_IS_INITED(kwork))) {
        return -MR_EINVAL;
    }

    /* Delete kwork */
    kwork_del(kwork);
    return 0;
}

MR_INLINE mr_err_t kqueue_work(mr_kworkqueue_t *kqueue, mr_kwork_t *kwork) {
    mr_bool_t wakeup;
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Check kwork is working */
    if (!mr_klist_is_empty(&kwork->list)) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Check kworkqueue is dying */
    if (mr_atomic_load(&kqueue->dying)) {
        ret = -MR_EINVAL;
        goto _exit;
    }

    /* Check kworkqueue is started(work) for the first time */
    if (mr_klist_is_empty(&kqueue->list)) {
        /* Need wakeup */
        wakeup = MR_TRUE;
    } else {
        wakeup = MR_FALSE;
    }

    /* Add kwork to kworkqueue list */
    mr_klist_add(&kqueue->list, &kwork->list);

    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);

    /* Check is need wakeup */
    if (wakeup) {
        /* Get kworkqueue run-time(work) */
        mr_kworkqueue_get(kqueue);
#if defined(MR_USE_KWORKQUEUE_HOOK)
        if (kqueue->wakeup) {
            /* Call kworkqueue wakeup hook */
            ((void (*)(mr_kworkqueue_t *))kqueue->wakeup)(kqueue);
        }
#endif /* defined(MR_USE_KWORKQUEUE_HOOK) */
    }
    return 0;
_exit:
    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);
    return ret;
}

mr_err_t mr_kworkqueue_work(mr_kworkqueue_t *kqueue, mr_kwork_t *kwork) {
    /* Check arguments */
    if ((!kqueue) || (!MR_KWORKQUEUE_IS_INITED(kqueue)) || (!kwork)
        || (!MR_KWORK_IS_INITED(kwork))) {
        return -MR_EINVAL;
    }

    /* Add kwork to kworkqueue */
    return kqueue_work(kqueue, kwork);
}

MR_INLINE mr_err_t kqueue_delayed_work(mr_kworkqueue_t *kqueue,
                                       mr_kwork_t *kwork, mr_tick_t tick) {
    mr_klist_t *l;
    mr_kwork_t *w;
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Check kwork is working */
    if (!mr_klist_is_empty(&kwork->list)) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Check kworkqueue is dying */
    if (mr_atomic_load(&kqueue->dying)) {
        ret = -MR_EINVAL;
        goto _exit;
    }

    /* Check kworkqueue is started for the first time */
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
    /* Check kwork is first */
    if (&kwork->list == kqueue->tlist.next) {
        /* Set to optimal tick */
        tick = kwork->tick - mr_kclock_tick();
        mr_ktimer_tick_set(&kqueue->timer, tick | MR_KTIMER_PERIODIC);
        mr_ktimer_start(&kqueue->timer);
    }
    ret = 0;
_exit:
    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);
    return ret;
}

mr_err_t mr_kworkqueue_delayed_work(mr_kworkqueue_t *kqueue, mr_kwork_t *kwork,
                                    mr_tick_t tick) {
    /* Check arguments */
    if ((!kqueue) || (!MR_KWORKQUEUE_IS_INITED(kqueue)) || (!kwork)
        || (!MR_KWORK_IS_INITED(kwork)) || (tick == 0)) {
        return -MR_EINVAL;
    }

    /* Add kwork to kworkqueue */
    return kqueue_delayed_work(kqueue, kwork, tick);
}

#if defined(MR_USE_KWORKQUEUE_HOOK)
mr_err_t mr_kworkqueue_wakeup_hook_set(mr_kworkqueue_t *kqueue,
                                       void (*hook)(mr_kworkqueue_t *)) {
    /* Check arguments */
    if ((!kqueue) || (!MR_KWORKQUEUE_IS_INITED(kqueue))) {
        return -MR_EINVAL;
    }

    /* Set kworkqueue wakeup hook */
    kqueue->wakeup = hook;
    return 0;
}

mr_err_t mr_kworkqueue_suspend_hook_set(mr_kworkqueue_t *kqueue,
                                        void (*hook)(mr_kworkqueue_t *)) {
    /* Check arguments */
    if ((!kqueue) || (!MR_KWORKQUEUE_IS_INITED(kqueue))) {
        return -MR_EINVAL;
    }

    /* Set kworkqueue suspend hook */
    kqueue->suspend = hook;
    return 0;
}
#endif /* defined(MR_USE_KWORKQUEUE_HOOK) */

mr_kworkqueue_t *mr_kworkqueue_find(const char *name) {
    /* Lookup kworkqueue in kroot */
    return (mr_kworkqueue_t *)mr_kobject_lookup((mr_kobject_t *)&kroot, name);
}

MR_INLINE mr_kworkqueue_t *kqueue_release_kobj(mr_kobject_t *kobj) {
    mr_kworkqueue_t *kqueue;

    /* Get kworkqueue */
    kqueue = MR_CONTAINER_OF(kobj, mr_kworkqueue_t, parent);

    /* Delete kworkqueue */
    kqueue_del(kqueue);
    return kqueue;
}

MR_INLINE void kqueue_release1_kobj(mr_kobject_t *kobj) {
    /* Release kworkqueue */
    kqueue_release_kobj(kobj);
}

MR_INLINE void kqueue_release2_kobj(mr_kobject_t *kobj) {
    mr_kworkqueue_t *kqueue;

    /* Release kworkqueue */
    kqueue = kqueue_release_kobj(kobj);

    /* Free kworkqueue */
    mr_free(kqueue);
}

MR_INLINE void mr_kworkqueue_kset_init(void) {
    static mr_kattr_t *attrs[] = {MR_NULL};
    mr_err_t ret;

    /* Register kset */
    ret = mr_kset_register(&kroot, "workqueue");
    if (ret != 0) {
        return;
    }

    /* Init ktype */
    mr_ktype_init(&ktype1, "workqueue", kqueue_release1_kobj, attrs);
    mr_ktype_init(&ktype2, "workqueue", kqueue_release2_kobj, attrs);
}
MR_INIT_EXPORT(mr_kworkqueue_kset_init, MR_INIT_LEVEL_KERNEL);
#endif /* defined(MR_USE_KWORKQUEUE) && defined(MR_USE_KTIMER) */

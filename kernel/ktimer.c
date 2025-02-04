/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_initcall.h>
#include <kernel/mr_ktimer.h>
#include <kernel/mr_spinlock.h>
#include <libc/mr_malloc.h>

static mr_ktype_t ktype1, ktype2;
static mr_kset_t kroot = MR_KSET_INIT(&kroot, MR_NULL);
static mr_klist_t klist = MR_KLIST_INIT(&klist);
static mr_spinlock_t klock = MR_SPINLOCK_INIT();

MR_INLINE void ktimer_init(mr_ktimer_t *ktimer, mr_ptr_t entry, void *args,
                           mr_tick_t tick) {
    /* Init ktimer */
    mr_klist_init(&ktimer->list);
    ktimer->init_tick = tick;
    ktimer->timeout_tick = 0;
    ktimer->entry = entry;
    ktimer->args = args;
}

MR_INLINE mr_err_t ktimer_init_add(mr_ktimer_t *ktimer, mr_ktype_t *ktype,
                                   const char *name, mr_ptr_t entry, void *args,
                                   mr_tick_t tick) {
    /* Init ktimer */
    ktimer_init(ktimer, entry, args, tick);

    /* Init kobject */
    mr_kobject_init((mr_kobject_t *)ktimer, ktype);

    /* Add kobject to kroot */
    return mr_kobject_add((mr_kobject_t *)ktimer, (mr_kobject_t *)&kroot, name);
}

mr_err_t mr_ktimer_init(mr_ktimer_t *ktimer, const char *name,
                        void (*entry)(mr_ktimer_t *, void *), void *args,
                        mr_tick_t tick) {
    /* Check arguments */
    if ((!ktimer) || (MR_KOBJECT_IS_INITED(ktimer)) || (!name) || (!entry)
        || (tick == 0)) {
        return -MR_EINVAL;
    }

    /* Init and add ktimer */
    return ktimer_init_add(ktimer, &ktype1, name, (mr_ptr_t)entry, args, tick);
}

mr_ktimer_t *mr_ktimer_create(const char *name,
                              void (*entry)(mr_ktimer_t *, void *), void *args,
                              mr_tick_t tick) {
    mr_ktimer_t *ktimer;
    mr_err_t ret;

    /* Check arguments */
    if ((!name) || (!entry) || (tick == 0)) {
        return MR_NULL;
    }

    /* Create ktimer */
    ktimer = mr_malloc(sizeof(mr_ktimer_t));
    if (!ktimer) {
        return MR_NULL;
    }

    /* Init and add ktimer */
    ret = ktimer_init_add(ktimer, &ktype2, name, (mr_ptr_t)entry, args, tick);
    if (ret != 0) {
        /* Free ktimer */
        mr_free(ktimer);
        return MR_NULL;
    }
    return ktimer;
}

MR_INLINE void ktimer_del(mr_ktimer_t *ktimer) {
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&klock);

    /* Already deleted */
    if (mr_klist_is_empty(&ktimer->list)) {
        goto _exit;
    }

    /* Remove ktimer from ktimer list */
    mr_klist_del(&ktimer->list);
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&klock, mask);
}

mr_err_t mr_ktimer_del(mr_ktimer_t *ktimer) {
    /* Check arguments */
    if ((!ktimer) || (!MR_KOBJECT_IS_INITED(ktimer))) {
        return -MR_EINVAL;
    }

    /* Delete ktimer */
    ktimer_del(ktimer);

    /* Delete kobject */
    mr_kobject_del((mr_kobject_t *)ktimer);
    return 0;
}

MR_INLINE void ktimer_insert(mr_ktimer_t *ktimer) {
    mr_tick_t tick;
    mr_ktimer_t *t;
    mr_klist_t *l;

    /* Set timeout tick */
    tick = ktimer->init_tick & ~MR_KTIMER_PERIODIC;
    ktimer->timeout_tick = mr_kclock_tick() + tick;

    /* Insert ktimer into ktimer list */
    MR_KLIST_FOR_EACH(l, &klist) {
        t = MR_KLIST_ENTRY(l, mr_ktimer_t, list);
        if ((ktimer->timeout_tick - t->timeout_tick) < (MR_TICK_MAX / 2)) {
            continue;
        }

        /* Move ktimer into position */
        mr_klist_move(&ktimer->list, &t->list);
        return;
    }

    /* It is the last ktimer */
    mr_klist_move(&ktimer->list, &klist);
}

MR_INLINE void ktimer_start(mr_ktimer_t *ktimer) {
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&klock);

    /* Insert ktimer into ktimer list */
    ktimer_insert(ktimer);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&klock, mask);
}

mr_err_t mr_ktimer_start(mr_ktimer_t *ktimer) {
    /* Check arguments */
    if ((!ktimer) || (!MR_KOBJECT_IS_INITED(ktimer))) {
        return -MR_EINVAL;
    }

    /* Start ktimer */
    ktimer_start(ktimer);
    return 0;
}

mr_err_t mr_ktimer_stop(mr_ktimer_t *ktimer) {
    /* Check arguments */
    if ((!ktimer) || (!MR_KOBJECT_IS_INITED(ktimer))) {
        return -MR_EINVAL;
    }

    /* Delete ktimer */
    ktimer_del(ktimer);
    return 0;
}

MR_INLINE void ktimer_tick_set(mr_ktimer_t *ktimer, mr_tick_t tick) {
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&klock);

    /* Set init tick */
    ktimer->init_tick = tick;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&klock, mask);
}

mr_err_t mr_ktimer_tick_set(mr_ktimer_t *ktimer, mr_tick_t tick) {
    /* Check arguments */
    if ((!ktimer) || (!MR_KOBJECT_IS_INITED(ktimer)) || (tick == 0)) {
        return -MR_EINVAL;
    }

    /* Delete ktimer */
    ktimer_del(ktimer);

    /* Set ktimer tick */
    ktimer_tick_set(ktimer, tick);
    return 0;
}

MR_INLINE void ktimer_release1_kobj(mr_kobject_t *kobj) {
    mr_ktimer_t *ktimer;

    /* Get ktimer */
    ktimer = MR_CONTAINER_OF(kobj, mr_ktimer_t, parent);

    /* Delete ktimer */
    ktimer_del(ktimer);
}

MR_INLINE void ktimer_release2_kobj(mr_kobject_t *kobj) {
    mr_ktimer_t *ktimer;

    /* Get ktimer */
    ktimer = MR_CONTAINER_OF(kobj, mr_ktimer_t, parent);

    /* Delete ktimer */
    ktimer_del(ktimer);

    /* Free ktimer */
    mr_free(ktimer);
}

MR_INLINE void ktimer_timeout_check(void) {
    mr_ktimer_t *ktimer;
    mr_klist_t list;
    mr_tick_t tick;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&klock);

    /* Check timeout ktimer */
    while (!mr_klist_is_empty(&klist)) {
        ktimer = MR_KLIST_ENTRY(klist.next, mr_ktimer_t, list);

        /* Get current tick */
        tick = mr_kclock_tick();
        if ((tick - ktimer->timeout_tick) >= (MR_TICK_MAX / 2)) {
            /* There will be no timeout ktimer after that */
            break;
        }

        /* Move ktimer into operation list */
        mr_klist_init(&list);
        mr_klist_move(&ktimer->list, &list);

        /* Unlock */
        mr_spinlock_unlock(&klock);

        /* Call ktimer entry */
        ((void (*)(mr_ktimer_t *, void *))ktimer->entry)(ktimer, ktimer->args);

        /* Lock */
        mr_spinlock_lock(&klock);

        /* Check that the ktimer has not been moved */
        if (mr_klist_is_empty(&list)) {
            /* Timer is no longer yours to operate */
            continue;
        }

        /* Tick is periodic */
        if (ktimer->init_tick & MR_KTIMER_PERIODIC) {
            /* Restart ktimer */
            ktimer_insert(ktimer);
        }
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&klock, mask);
}

MR_INLINE void mr_ktimer_kset_init(void) {
    mr_err_t ret;

    /* Register kset */
    ret = mr_kset_register(&kroot, "timer");
    if (ret != 0) {
        /* It won't entry the kclock, so it won't start */
        return;
    }

    /* Init ktype */
    mr_ktype_init(&ktype1, ktimer_release1_kobj);
    mr_ktype_init(&ktype2, ktimer_release2_kobj);

    /* Add kclock entry */
    mr_kclock_hook_add(ktimer_timeout_check);
}
MR_INIT_EXPORT(mr_ktimer_kset_init, MR_INIT_LEVEL_KERNEL);

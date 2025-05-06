/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_ktimer.h>
#if defined(MR_USE_KTIMER)
#include <kernel/mr_kspinlock.h>
#include <kernel/mr_initcall.h>
#include <libc/mr_malloc.h>
#if defined(MR_USE_KTIMER_ATTR)
#include <libc/mr_printf.h>
#include <libc/mr_scanf.h>
#include <libc/mr_string.h>
#endif /* defined(MR_USE_KTIMER_ATTR) */

/* Ktimer type */
static mr_ktype_t ktype1, ktype2;
/* Ktimer list */
static mr_klist_t klist = MR_KLIST_INIT(&klist);
/* Ktimer lock */
static mr_kspinlock_t klock = MR_KSPINLOCK_INIT();
/* Ktimer set */
static mr_kset_t kroot = MR_KSET_INIT(&kroot, MR_NULL);

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
                        mr_ktimer_entry_t *entry, void *args, mr_tick_t tick) {
    /* Check arguments */
    if ((!ktimer) || MR_KTIMER_IS_INITED(ktimer) || (!name) || (!entry)
        || (tick == 0) || (tick >= (MR_TICK_MAX / 2))) {
        return -MR_EINVAL;
    }

    /* Init and add ktimer */
    return ktimer_init_add(ktimer, &ktype1, name, entry, args, tick);
}

mr_ktimer_t *mr_ktimer_create(const char *name, mr_ktimer_entry_t *entry,
                              void *args, mr_tick_t tick) {
    mr_ktimer_t *ktimer;
    mr_err_t ret;

    /* Check arguments */
    if ((!name) || (!entry) || (tick == 0) || (tick >= (MR_TICK_MAX / 2))) {
        return MR_NULL;
    }

    /* Create ktimer */
    ktimer = mr_malloc(sizeof(mr_ktimer_t));
    if (!ktimer) {
        return MR_NULL;
    }

    /* Init and add ktimer */
    ret = ktimer_init_add(ktimer, &ktype2, name, entry, args, tick);
    if (ret != 0) {
        mr_free(ktimer);
        return MR_NULL;
    }
    return ktimer;
}

MR_INLINE void ktimer_del(mr_ktimer_t *ktimer) {
    int mask;

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Check if ktimer is deleted */
    if (!mr_klist_is_empty(&ktimer->list)) {
        /* Remove ktimer from timer list */
        mr_klist_del(&ktimer->list);
    }

    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);
}

mr_err_t mr_ktimer_del(mr_ktimer_t *ktimer) {
    /* Check arguments */
    if ((!ktimer) || (!MR_KTIMER_IS_INITED(ktimer))) {
        return -MR_EINVAL;
    }

    /* Delete ktimer */
    ktimer_del(ktimer);

    /* Delete kobject */
    mr_kobject_del((mr_kobject_t *)ktimer);
    return 0;
}

MR_INLINE void ktimer_insert(mr_ktimer_t *ktimer) {
    mr_ktimer_t *t;
    mr_tick_t tick;
    mr_klist_t *l;

    /* Set timeout tick */
    tick = ktimer->init_tick & ~MR_KTIMER_PERIODIC;
    ktimer->timeout_tick = mr_kclock_tick() + tick;

    /* Insert ktimer into timer list */
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

mr_err_t mr_ktimer_start(mr_ktimer_t *ktimer) {
    int mask;

    /* Check arguments */
    if ((!ktimer) || (!MR_KTIMER_IS_INITED(ktimer))) {
        return -MR_EINVAL;
    }

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Insert ktimer into timer list */
    ktimer_insert(ktimer);

    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);
    return 0;
}

mr_err_t mr_ktimer_stop(mr_ktimer_t *ktimer) {
    /* Check arguments */
    if ((!ktimer) || (!MR_KTIMER_IS_INITED(ktimer))) {
        return -MR_EINVAL;
    }

    /* Delete ktimer */
    ktimer_del(ktimer);
    return 0;
}

mr_err_t mr_ktimer_tick_set(mr_ktimer_t *ktimer, mr_tick_t tick) {
    int mask;

    /* Check arguments */
    if ((!ktimer) || (!MR_KTIMER_IS_INITED(ktimer)) || (tick == 0)) {
        return -MR_EINVAL;
    }

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Remove ktimer from timer list */
    if (!mr_klist_is_empty(&ktimer->list)) {
        mr_klist_del(&ktimer->list);
    }

    /* Set init tick */
    ktimer->init_tick = tick;

    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);
    return 0;
}

mr_ktimer_t *mr_ktimer_find(const char *name) {
    /* Lookup ktimer in kroot */
    return (mr_ktimer_t *)mr_kobject_lookup((mr_kobject_t *)&kroot, name);
}

MR_INLINE mr_ktimer_t *ktimer_kobj_release(mr_kobject_t *kobj) {
    mr_ktimer_t *ktimer;

    /* Get ktimer */
    ktimer = MR_CONTAINER_OF(kobj, mr_ktimer_t, parent);

    /* Delete ktimer */
    ktimer_del(ktimer);
    return ktimer;
}

MR_INLINE void ktimer_kobj_release1(mr_kobject_t *kobj) {
    /* Release ktimer */
    ktimer_kobj_release(kobj);
}

MR_INLINE void ktimer_kobj_release2(mr_kobject_t *kobj) {
    mr_ktimer_t *ktimer;

    /* Release ktimer */
    ktimer = ktimer_kobj_release(kobj);

    /* Free ktimer */
    mr_free(ktimer);
}

MR_INLINE void ktimer_timeout_check(void) {
    mr_ktimer_entry_t *entry;
    mr_ktimer_t *ktimer;
    mr_klist_t list;
    void *args;
    int mask;

    /* Operation list */
    mr_klist_init(&list);

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Check timeout ktimer */
    while (!mr_klist_is_empty(&klist)) {
        ktimer = MR_KLIST_ENTRY(klist.next, mr_ktimer_t, list);

        /* Check timeout tick */
        if ((mr_kclock_tick() - ktimer->timeout_tick) >= (MR_TICK_MAX / 2)) {
            /* There will be no timeout ktimer after that */
            break;
        }

        /* Move ktimer into operation list */
        mr_klist_move(&ktimer->list, &list);

        /* Save ktimer entry and args */
        entry = ktimer->entry;
        args = ktimer->args;

        /* Unlock */
        mr_kspinlock_unlock_irqrestore(&klock, mask);

        /* Call ktimer entry */
        entry(ktimer, args);

        /* Lock */
        mask = mr_kspinlock_lock_irqsave(&klock);

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
    mr_kspinlock_unlock_irqrestore(&klock, mask);
}

#if defined(MR_USE_KTIMER_ATTR)
MR_INLINE mr_ssize_t ktimer_attr_show(struct mr_kobject *kobj,
                                      struct mr_kattr *attr, char *buf,
                                      mr_size_t size) {
    mr_uint32_t tick, mode, status;
    mr_ktimer_t *ktimer;

    /* Get ktimer */
    ktimer = MR_CONTAINER_OF(kobj, mr_ktimer_t, parent);
    tick = ktimer->init_tick & ~MR_KTIMER_PERIODIC;
    mode = ((ktimer->init_tick & MR_KTIMER_PERIODIC) != 0);
    status = !mr_klist_is_empty(&ktimer->list);

    /* Show tick */
    if (!mr_strcmp(attr->name, "tick")) {
        return mr_snprintf(buf, size, "%u", tick);
    }

    /* Show mode */
    if (!mr_strcmp(attr->name, "mode")) {
        return mr_snprintf(buf, size, "%u", mode);
    }

    /* Show status */
    return mr_snprintf(buf, size, "%u", status);
}

MR_INLINE mr_ssize_t ktimer_attr_store(struct mr_kobject *kobj,
                                       struct mr_kattr *attr, const char *buf,
                                       mr_size_t size) {
    mr_uint32_t tick, mode;
    mr_ktimer_t *ktimer;
    int input;

    MR_UNUSED(size);

    /* Get ktimer */
    ktimer = MR_CONTAINER_OF(kobj, mr_ktimer_t, parent);
    tick = ktimer->init_tick & ~MR_KTIMER_PERIODIC;
    mode = ktimer->init_tick & MR_KTIMER_PERIODIC;

    /* Parse 'input' */
    if (mr_sscanf(buf, "%d", &input) != 1) {
        return -MR_EINVAL;
    }

    /* Store tick */
    if (!mr_strcmp(attr->name, "tick")) {
        /* Check 'input' */
        if (input <= 0) {
            return -MR_EINVAL;
        }

        /* Set ktimer tick */
        tick = (mr_uint32_t)input | mode;
        return mr_ktimer_tick_set(ktimer, tick);
    }

    /* Store mode */
    if (!mr_strcmp(attr->name, "mode")) {
        if ((input != 0) && (input != 1)) {
            return -MR_EINVAL;
        }

        /* Set ktimer mode */
        mode = tick | (input ? MR_KTIMER_PERIODIC : 0);
        return mr_ktimer_tick_set(ktimer, mode);
    }

    /* Store status */
    if (input == 0) {
        return mr_ktimer_stop(ktimer);
    }
    if (input == 1) {
        return mr_ktimer_start(ktimer);
    }
    return -MR_EINVAL;
}
#endif /* defined(MR_USE_KTIMER_ATTR) */

void mr_ktimer_kernel_init(void) {
#if defined(MR_USE_KTIMER_ATTR)
    static mr_kattr_t attr[]
        = {MR_KATTR_INIT("tick", ktimer_attr_show, ktimer_attr_store),
           MR_KATTR_INIT("mode", ktimer_attr_show, ktimer_attr_store),
           MR_KATTR_INIT("status", ktimer_attr_show, ktimer_attr_store)};
    static mr_kattr_t *attrs[] = {&attr[0], &attr[1], &attr[2], MR_NULL};
#else
    static mr_kattr_t *attrs[] = {MR_NULL};
#endif /* defined(MR_USE_KTIMER_ATTR) */
    mr_err_t ret;

    /* Init ktype */
    mr_ktype_init(&ktype1, "timer", ktimer_kobj_release1, attrs);
    mr_ktype_init(&ktype2, "timer", ktimer_kobj_release2, attrs);

    /* Register kset */
    ret = mr_kset_register(&kroot, "timer");
    if (ret != 0) {
        /* It won't hook the kclock, so it won't start */
        return;
    }

    /* Add kclock hook */
    mr_kclock_hook_add(ktimer_timeout_check);
}
#endif /* defined(MR_USE_KTIMER) */

/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/modules/xvm/mr_xvm_isa.h>
#if defined(MR_USE_MODULE_XVM)
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/libc/mr_string.h>

/* XVM ISA clazzes(static only) */
static void xvm_isa_statik_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(xvm_isa, xvm_isa_statik_release, MR_NULL, MR_NULL);

/* XVM ISA lock */
static mr_spinlock_t __xvm_isa_lock = MR_SPINLOCK_INIT();
/* XVM ISA list */
static mr_xvm_isa_t *__xvm_isa_list = MR_NULL;

static mr_err_t xvm_isa_init(mr_xvm_isa_t *isa, const mr_clazz_t *clazz,
                             const char *name, const mr_xvm_isa_ops_t *ops,
                             mr_xvm_isa_target_t *target) {
    /* Init XVM ISA */
    isa->name = name;
    isa->next = MR_NULL;
    isa->ops = ops;
    isa->target = target;

    /* Init object */
    return mr_object_init((mr_object_t *)isa, clazz);
}

mr_err_t mr_xvm_isa_init(mr_xvm_isa_t *isa, const char *name,
                         const mr_xvm_isa_ops_t *ops,
                         mr_xvm_isa_target_t *target) {
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(xvm_isa);

    /* Check parameter */
    MR_ASSERT(isa != MR_NULL);
    MR_ASSERT(name != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Init XVM ISA */
    return xvm_isa_init(isa, clazz, name, ops, target);
}

static mr_bool_t xvm_isa_is_registered(mr_xvm_isa_t *isa) {
    mr_xvm_isa_t *cur;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xvm_isa_lock);

    /* Check whether XVM ISA already registered */
    for (cur = __xvm_isa_list; cur; cur = cur->next) {
        if (cur == isa) {
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__xvm_isa_lock, mask);

            /* Already registered */
            return MR_TRUE;
        }
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xvm_isa_lock, mask);
    return MR_FALSE;
}

mr_err_t mr_xvm_isa_del(mr_xvm_isa_t *isa) {
    /* Check parameter */
    MR_ASSERT((isa != MR_NULL) && MR_OBJECT_IS_INITED(isa));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(isa, xvm_isa));

    /* Check whether XVM ISA already registered */
    if (xvm_isa_is_registered(isa)) {
        return -MR_EBUSY;
    }

    /* Delete object */
    mr_object_del((mr_object_t *)isa);
    return 0;
}

static mr_err_t xvm_isa_add(mr_xvm_isa_t *isa) {
    mr_xvm_isa_t *existing;
    mr_xvm_isa_t **link;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xvm_isa_lock);

    /* Check whether XVM ISA already registered */
    for (link = &__xvm_isa_list; *link; link = &(*link)->next) {
        existing = *link;

        /* Already registered */
        if (mr_strcmp(existing->name, isa->name) == 0) {
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__xvm_isa_lock, mask);
            return -MR_EEXIST;
        }
    }

    /* Add to the list */
    isa->next = MR_NULL;
    *link = isa;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xvm_isa_lock, mask);
    return 0;
}

mr_err_t mr_xvm_isa_register(mr_xvm_isa_t *isa) {
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((isa != MR_NULL) && MR_OBJECT_IS_INITED(isa));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(isa, xvm_isa));

    /* Get XVM ISA */
    isa = mr_xvm_isa_get(isa);
    if (!isa) {
        return -MR_ENOENT;
    }

    /* Add XVM ISA */
    ret = xvm_isa_add(isa);
    if (ret != 0) {
        /* Put XVM ISA */
        mr_xvm_isa_put(isa);
        return ret;
    }
    return 0;
}

static mr_err_t xvm_isa_remove(mr_xvm_isa_t *isa) {
    mr_xvm_isa_t **link;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xvm_isa_lock);

    /* Find the registered entry by address. A never-registered object may have
     * an arbitrary next pointer, so do not trust isa->next here */
    for (link = &__xvm_isa_list; *link; link = &(*link)->next) {
        if (*link == isa) {
            break;
        }
    }

    /* Not registered */
    if (!(*link)) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__xvm_isa_lock, mask);
        return -MR_ENOENT;
    }

    /* Remove from the list */
    *link = isa->next;
    isa->next = MR_NULL;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xvm_isa_lock, mask);
    return 0;
}

mr_err_t mr_xvm_isa_unregister(mr_xvm_isa_t *isa) {
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((isa != MR_NULL) && MR_OBJECT_IS_INITED(isa));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(isa, xvm_isa));

    /* Remove XVM ISA */
    ret = xvm_isa_remove(isa);
    if (ret != 0) {
        return ret;
    }

    /* Put XVM ISA */
    mr_xvm_isa_put(isa);
    return 0;
}

mr_xvm_isa_t *mr_xvm_isa_find(const char *name) {
    mr_xvm_isa_t *isa;
    int mask;

    /* Check parameter */
    MR_ASSERT(name != MR_NULL);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xvm_isa_lock);

    /* Find the XVM ISA */
    for (isa = __xvm_isa_list; isa; isa = isa->next) {
        if (mr_strcmp(isa->name, name) == 0) {
            /* Get XVM ISA */
            isa = mr_xvm_isa_get(isa);

            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__xvm_isa_lock, mask);
            return isa;
        }
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xvm_isa_lock, mask);
    return MR_NULL;
}

static void xvm_isa_statik_release(mr_object_t *obj) {
    mr_xvm_isa_t *isa;

    /* Release XVM ISA */
    isa = MR_CONTAINER_OF(obj, mr_xvm_isa_t, parent);

    /* Detach from the list if still linked */
    xvm_isa_remove(isa);
}
#endif /* defined(MR_USE_MODULE_XVM) */

/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_irq.h>
#if defined(MR_USE_IRQ)
#include <kernel/mr_kspinlock.h>
#include <libc/mr_malloc.h>

/* Irq type */
static mr_ktype_t ktype1, ktype2;
/* Irq lock */
static mr_kspinlock_t klock = MR_KSPINLOCK_INIT();
/* Irq descriptor table */
#if !defined(MR_CFG_IRQ_TABLE_SIZE)
#define MR_CFG_IRQ_TABLE_SIZE           (32)
#endif /* !defined(MR_CFG_IRQ_TABLE_SIZE) */
static mr_irq_desc_t ktable[MR_CFG_IRQ_TABLE_SIZE];
/* Irq set */
static mr_kset_t kroot = MR_KSET_INIT(&kroot, MR_NULL);

/* Irq flags definition */
#define IRQ_FLAG_TYPE_MASK              (0x0f)
/* Irq action pending definition */
#define IRQ_ACTION_PENDING              (1U << 31)

MR_INLINE mr_err_t irq_init(mr_irq_t *irq, mr_uint32_t irq_start,
                            mr_uint32_t irq_end, mr_ptr_t ops, mr_ptr_t args) {
    mr_uint32_t i;
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Add irq to table */
    for (i = irq_start; i <= irq_end; i++) {
        /* Check if irq is conflict */
        if (ktable[i].irq) {
            /* Irq conflict */
            for (irq_end = i, i = irq_start; i < irq_end; i++) {
                ktable[i].irq = MR_NULL;
            }
            ret = -MR_EEXIST;
            goto _exit;
        }

        /* Init irq descriptor */
        mr_atomic_init(&ktable[i].depth, 1);
        ktable[i].irq = irq;
    }

    /* Init irq */
    mr_atomic_init(&irq->dying, MR_FALSE);
    irq->irq_start = irq_start;
    irq->irq_end = irq_end;
    irq->args = args;
    irq->ops = ops;
    ret = 0;
_exit:
    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);
    return ret;
}

MR_INLINE void irq_action_free(mr_irq_action_t *action) {
    /* Free irq action */
    mr_free(action);
}

MR_INLINE void irq_depth_disable(mr_uint32_t irq, mr_irq_desc_t *desc) {
    mr_atomic_t last;

    /* Increase depth */
    last = mr_atomic_fetch_add(&desc->depth, 1);
    if (last != 0) {
        /* Depth does not change from 0 to 1 */
        if (!desc->action) {
            /* Reset depth if no irq action */
            last += 1;
            mr_atomic_compare_exchange(&desc->depth, &last, 1);
        }
        return;
    }

    /* Disable irq */
    desc->irq->ops->disable(irq, desc->irq->args);
    desc->irq->ops->mask(irq, desc->irq->args);
}

MR_INLINE void irq_del(mr_irq_t *irq) {
    mr_irq_action_t *action;
    mr_uint32_t i;
    int mask;

    /* Mark as dying */
    mr_atomic_store(&irq->dying, MR_TRUE);

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Remove irq from table */
    for (i = irq->irq_start; i <= irq->irq_end; i++) {
        /* Free irq actions */
        while (ktable[i].action) {
            /* Remove irq action from irq descriptor */
            action = ktable[i].action;
            ktable[i].action = action->next;
            irq_action_free(action);
        }

        /* Disable irq */
        irq_depth_disable(i, &ktable[i]);
        ktable[i].irq = MR_NULL;
    }

    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);
}

MR_INLINE mr_err_t irq_init_add(mr_irq_t *irq, mr_ktype_t *ktype,
                                const char *name, mr_uint32_t irq_start,
                                mr_uint32_t irq_end, mr_ptr_t ops,
                                mr_ptr_t args) {
    mr_err_t ret;

    /* Init irq */
    ret = irq_init(irq, irq_start, irq_end, ops, args);
    if (ret != 0) {
        return ret;
    }

    /* Init kobject */
    mr_kobject_init((mr_kobject_t *)irq, ktype);

    /* Add kobject to kroot */
    ret = mr_kobject_add((mr_kobject_t *)irq, (mr_kobject_t *)&kroot, name);
    if (ret != 0) {
        irq_del(irq);
        return ret;
    }
    return 0;
}

mr_err_t mr_irq_init(mr_irq_t *irq, const char *name, mr_uint32_t irq_start,
                     mr_uint32_t irq_end, mr_irq_ops_t *ops, void *args) {
    /* Check arguments */
    if ((!irq) || MR_IRQ_IS_INITED(irq) || (!name)
        || (irq_start >= MR_CFG_IRQ_TABLE_SIZE) || (irq_start > irq_end)
        || (irq_end >= MR_CFG_IRQ_TABLE_SIZE) || (!ops)) {
        return -MR_EINVAL;
    }

    /* Init and add irq */
    return irq_init_add(irq, &ktype1, name, irq_start, irq_end, ops, args);
}

mr_irq_t *mr_irq_create(const char *name, mr_uint32_t irq_start,
                        mr_uint32_t irq_end, mr_irq_ops_t *ops, void *args) {
    mr_irq_t *irq;
    mr_err_t ret;

    /* Check arguments */
    if ((!name) || (irq_start >= MR_CFG_IRQ_TABLE_SIZE) || (irq_start > irq_end)
        || (irq_end >= MR_CFG_IRQ_TABLE_SIZE) || (!ops)) {
        return MR_NULL;
    }

    /* Create irq */
    irq = mr_malloc(sizeof(mr_irq_t));
    if (!irq) {
        return MR_NULL;
    }

    /* Init and add irq */
    ret = irq_init_add(irq, &ktype2, name, irq_start, irq_end, ops, args);
    if (ret != 0) {
        mr_free(irq);
        return MR_NULL;
    }
    return irq;
}

mr_err_t mr_irq_del(mr_irq_t *irq) {
    /* Check arguments */
    if ((!irq) || (!MR_IRQ_IS_INITED(irq))) {
        return -MR_EINVAL;
    }

    /* Mark as dying */
    mr_atomic_store(&irq->dying, MR_TRUE);

    /* Delete kobject */
    mr_kobject_del((mr_kobject_t *)irq);
    return 0;
}

MR_INLINE mr_irq_action_t *irq_action_alloc(mr_uint32_t irq,
                                            mr_irq_entry_t *entry,
                                            mr_ptr_t owner) {
    mr_irq_action_t *action;

    /* Create irq action */
    action = mr_malloc(sizeof(mr_irq_action_t));
    if (!action) {
        return MR_NULL;
    }

    /* Init irq action */
    action->next = MR_NULL;
    action->irq = irq;
    action->entry = entry;
    action->owner = owner;
    return action;
}

MR_INLINE void irq_depth_enable(mr_uint32_t irq, mr_irq_desc_t *desc) {
    mr_atomic_t last;

    /* Decrease depth */
    last = mr_atomic_fetch_sub(&desc->depth, 1);
    if (last != 1) {
        /* Depth does not change from 1 to 0 */
        if (last <= 0) {
            /* No depth when enabled */
            last -= 1;
            mr_atomic_compare_exchange(&desc->depth, &last, 0);
        }
        return;
    }

    /* Enable irq */
    desc->irq->ops->unmask(irq, desc->irq->args);
    desc->irq->ops->enable(irq, desc->irq->args);
}

MR_INLINE mr_err_t irq_request(mr_uint32_t irq, mr_uint32_t flags,
                               mr_irq_action_t *action) {
    mr_irq_action_t **a;
    mr_irq_desc_t *desc;
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Check if irq is dying */
    desc = &ktable[irq];
    if ((!desc->irq) || mr_atomic_load(&desc->irq->dying)) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* Check irq descriptor action */
    if (desc->action) {
        /* Check irq share flag */
        if (!(desc->flags & MR_IRQ_SHARED)) {
            /* If the share isn't set up, it can't be configured */
            ret = -MR_EBUSY;
            goto _exit;
        }

        /* Check irq mode(shared must be the same) */
        if (desc->flags != flags) {
            ret = -MR_EINVAL;
            goto _exit;
        }
    } else {
        /* Set irq type(edge or level) */
        desc->irq->ops->type_set(irq, flags & IRQ_FLAG_TYPE_MASK,
                                 desc->irq->args);

        /* Set irq default priority(highest) */
        desc->irq->ops->priority_set(irq, 0, desc->irq->args);

        /* Set irq descriptor flags */
        desc->flags = flags;
    }

    /* Insert irq action to irq descriptor */
    for (a = &desc->action; *a; a = &(*a)->next) {
        /* Check if irq action is exist */
        if ((*a)->owner != action->owner) {
            continue;
        }

        /* Irq action is exist */
        ret = -MR_EEXIST;
        goto _exit;
    }
    *a = action;

    /* Enable irq */
    irq_depth_enable(irq, desc);
    ret = 0;
_exit:
    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);
    return ret;
}

mr_err_t mr_irq_request(mr_uint32_t irq, mr_irq_entry_t *entry, void *owner,
                        mr_uint32_t flags) {
    mr_irq_action_t *action;
    mr_err_t ret;

    /* Check arguments */
    if ((irq >= MR_CFG_IRQ_TABLE_SIZE) || (!entry) || (!owner)) {
        return -MR_EINVAL;
    }

    /* Create irq action */
    action = irq_action_alloc(irq, entry, owner);
    if (!action) {
        return -MR_ENOMEM;
    }

    /* Request irq */
    ret = irq_request(irq, flags, action);
    if (ret != 0) {
        irq_action_free(action);
        return ret;
    }
    return 0;
}

mr_err_t mr_irq_free(mr_uint32_t irq, void *owner) {
    mr_irq_action_t *action, **a;
    mr_irq_desc_t *desc;
    mr_err_t ret;
    int mask;

    /* Check arguments */
    if ((irq >= MR_CFG_IRQ_TABLE_SIZE) || (!owner)) {
        return -MR_EINVAL;
    }

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Check if irq is dying */
    desc = &ktable[irq];
    if ((!desc->irq) || mr_atomic_load(&desc->irq->dying)) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* Remove irq action from irq descriptor */
    for (action = MR_NULL, a = &desc->action; *a; a = &(*a)->next) {
        /* Match owner */
        if (owner != (*a)->owner) {
            continue;
        }

        /* Check if irq action is pending */
        if ((*a)->irq & IRQ_ACTION_PENDING) {
            /* Mark irq action free, defer free */
            (*a)->owner = MR_NULL;
            ret = 0;
            goto _exit;
        }

        /* Remove irq action from irq descriptor */
        action = *a;
        *a = action->next;
        break;
    }

    /* Check irq action */
    if (!action) {
        /* Irq action is not exist */
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* Free irq action */
    irq_action_free(action);

    /* Disable irq if no irq action */
    if (!desc->action) {
        irq_depth_disable(irq, desc);
    }
    ret = 0;
_exit:
    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);
    return ret;
}

mr_err_t mr_irq_enable(mr_uint32_t irq) {
    mr_irq_desc_t *desc;
    mr_err_t ret;
    int mask;

    /* Check arguments */
    if (irq >= MR_CFG_IRQ_TABLE_SIZE) {
        return -MR_EINVAL;
    }

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Check if irq is dying */
    desc = &ktable[irq];
    if ((!desc->irq) || mr_atomic_load(&desc->irq->dying)) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* Enable irq */
    irq_depth_enable(irq, desc);
    ret = 0;
_exit:
    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);
    return ret;
}

mr_err_t mr_irq_disable(mr_uint32_t irq) {
    mr_irq_desc_t *desc;
    mr_err_t ret;
    int mask;

    /* Check arguments */
    if (irq >= MR_CFG_IRQ_TABLE_SIZE) {
        return -MR_EINVAL;
    }

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Check if irq is dying */
    desc = &ktable[irq];
    if ((!desc->irq) || mr_atomic_load(&desc->irq->dying)) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* Disable irq */
    irq_depth_disable(irq, desc);
    ret = 0;
_exit:
    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);
    return ret;
}

mr_err_t mr_irq_priority_set(mr_uint32_t irq, mr_uint8_t priority) {
    mr_irq_desc_t *desc;
    mr_err_t ret;
    int mask;

    /* Check arguments */
    if (irq >= MR_CFG_IRQ_TABLE_SIZE) {
        return -MR_EINVAL;
    }

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Check if irq is dying */
    desc = &ktable[irq];
    if ((!desc->irq) || mr_atomic_load(&desc->irq->dying)) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* Set irq priority */
    desc->irq->ops->priority_set(irq, priority, desc->irq->args);
    ret = 0;
_exit:
    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);
    return ret;
}

mr_uint8_t mr_irq_priority(mr_uint32_t irq) {
    mr_irq_desc_t *desc;
    mr_uint8_t priority;
    int mask;

    /* Check arguments */
    if (irq >= MR_CFG_IRQ_TABLE_SIZE) {
        return (mr_uint8_t)(-1);
    }

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Get irq descriptor */
    desc = &ktable[irq];
    if ((!desc->irq) || (!desc->action)) {
        priority = (mr_uint8_t)(-1);
        goto _exit;
    }

    /* Get irq priority */
    priority = (mr_uint8_t)desc->priority;
_exit:
    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);
    return priority;
}

mr_irq_return_t mr_irq_handle(mr_uint32_t irq) {
    mr_irq_action_t **a, *action, action2;
    mr_irq_entry_t *entry;
    mr_irq_return_t ret;
    mr_irq_desc_t *desc;
    void *owner;
    int mask;

    /* Check arguments */
    if (irq >= MR_CFG_IRQ_TABLE_SIZE) {
        return MR_IRQ_NONE;
    }

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Check if irq is dying */
    desc = &ktable[irq];
    if ((!desc->irq) || mr_atomic_load(&desc->irq->dying)) {
        ret = MR_IRQ_NONE;
        goto _exit;
    }

    /* Get irq run-time */
    mr_irq_get(desc->irq);

    /* Shield irq mask */
    desc->irq->ops->mask(irq, desc->irq->args);
    if (desc->flags & MR_IRQ_EDGE_BOTH) {
        /* First ack if irq mode is edge */
        desc->irq->ops->ack(irq, desc->irq->args);
    }

    /* Handle irq action */
    for (action = desc->action; action; action = action->next) {
        /* Check if irq action is being freed */
        if (!action->owner) {
            continue;
        }

        /* Mark irq action pending */
        action->irq |= IRQ_ACTION_PENDING;
        entry = action->entry;
        owner = action->owner;

        /* Unlock */
        mr_kspinlock_unlock_irqrestore(&klock, mask);

        /* Call irq action entry */
        ret = entry(irq, owner);

        /* Lock */
        mask = mr_kspinlock_lock_irqsave(&klock);

        /* Mark irq action not pending */
        action->irq &= ~IRQ_ACTION_PENDING;

        /* Check if irq action is being freed */
        if (!action->owner) {
            /* Free irq action */
            for (a = &desc->action; *a; a = &(*a)->next) {
                /* Match irq action */
                if (*a != action) {
                    continue;
                }

                /* Remove irq action from irq descriptor(copy to keep the
                 * structure unchanged) */
                *a = action2.next = action->next;
                irq_action_free(action);
                action = &action2;
                break;
            }
        }

        /* Check if irq action is handled */
        if (ret == MR_IRQ_HANDLED) {
            if (desc->flags & (MR_IRQ_LEVEL_HIGH | MR_IRQ_LEVEL_LOW)) {
                /* Then ack if irq mode is level */
                desc->irq->ops->ack(irq, desc->irq->args);
            }
            break;
        }
    }

    /* Put irq run-time */
    mr_irq_put(desc->irq);

    /* Check if irq is enabled */
    if (mr_atomic_load(&desc->depth) == 0) {
        /* Unmask irq */
        desc->irq->ops->unmask(irq, desc->irq->args);
    }

    /* Put irq run-time */
    mr_irq_put(desc->irq);
_exit:
    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);
    return ret;
}

mr_irq_t *mr_irq_find(const char *name) {
    /* Lookup irq in kroot */
    return (mr_irq_t *)mr_kobject_lookup((mr_kobject_t *)&kroot, name);
}

MR_INLINE mr_irq_t *irq_kobj_release(mr_kobject_t *kobj) {
    mr_irq_t *irq;

    /* Get irq */
    irq = MR_CONTAINER_OF(kobj, mr_irq_t, parent);

    /* Delete irq */
    irq_del(irq);
    return irq;
}

MR_INLINE void irq_kobj_release1(mr_kobject_t *kobj) {
    /* Release irq */
    irq_kobj_release(kobj);
}

MR_INLINE void irq_kobj_release2(mr_kobject_t *kobj) {
    mr_irq_t *irq;

    /* Release irq */
    irq = irq_kobj_release(kobj);

    /* Free irq */
    mr_free(irq);
}

void mr_irq_kernel_init(void) {
    /* Init ktype */
    mr_ktype_init(&ktype1, "irq", irq_kobj_release1, MR_NULL);
    mr_ktype_init(&ktype2, "irq", irq_kobj_release2, MR_NULL);

    /* Register kset */
    mr_kset_register(&kroot, "irq");
}
#endif /* defined(MR_USE_IRQ) */

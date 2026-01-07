/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/kernel/mr_irq.h>
#if defined(MR_USE_IRQ)
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/kernel/mr_slab.h>
#include <mrX/libc/mr_malloc.h>

/* Irq clazzes(static & dynamic) */
static void irq_dynamic_release(mr_object_t *obj);
static void irq_statik_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(irq, irq_statik_release, irq_dynamic_release);

#if defined(MR_USE_IRQ_DEFER_HOOK)
static const mr_irq_defer_hook_t *__irq_hook = MR_NULL;
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */
/* Irq lock */
static mr_spinlock_t __irq_lock = MR_SPINLOCK_INIT();
/* Irq descriptor table */
#if !defined(MR_CFG_IRQ_TABLE_SIZE)
#define MR_CFG_IRQ_TABLE_SIZE           (32)
#endif /* !defined(MR_CFG_IRQ_TABLE_SIZE) */
static mr_irq_desc_t __irq_table[MR_CFG_IRQ_TABLE_SIZE];
#if defined(MR_USE_IRQ_DEFER)
/* Irq defer workqueue */
static mr_workqueue_t __irq_defer_queue;
#endif /* defined(MR_USE_IRQ_DEFER) */

/* Irq flags definition */
#define IRQ_FLAG_TYPE_MASK              (0x0fU)
/* Irq priority default definition */
#define IRQ_PRIORITY_DEFAULT            (0)

static mr_err_t irq_table_insert(mr_irq_t *irq, mr_uint32_t irq_start,
                                 mr_uint32_t irq_end) {
    mr_uint32_t i;

    /* Add irq to table */
    for (i = irq_start; i <= irq_end; i++) {
        /* Check if irq is conflict */
        if (__irq_table[i].irq) {
            /* Irq conflict */
            for (irq_end = i, i = irq_start; i < irq_end; i++) {
                __irq_table[i].irq = MR_NULL;
            }
            return -MR_EEXIST;
        }

        /* Init irq descriptor */
        mr_atomic_init(&__irq_table[i].depth, 1);
        __irq_table[i].irq = irq;
    }
    return 0;
}

static void irq_action_free(mr_irq_action_t *action) {
#if defined(MR_USE_IRQ_DEFER)
    /* Delete irq action defer work */
    mr_work_del(&action->defer);
#endif /* defined(MR_USE_IRQ_DEFER) */

    /* Free irq action */
    mr_free(action);
}

static void irq_depth_disable(mr_uint32_t irq, mr_irq_desc_t *desc) {
    mr_atomic_t last;

    /* Increase depth */
    last = mr_atomic_fetch_add(&desc->depth, 1);
    if (last != 0) {
        /* Depth does not change from 0 to 1 */
        if (!desc->action) {
            /* Reset depth if no irq action */
            mr_atomic_store(&desc->depth, 1);
        }
        return;
    }

    /* Disable irq */
    desc->irq->ops->disable(irq, desc->irq->priv);
    desc->irq->ops->mask(irq, desc->irq->priv);
}

static void irq_table_remove(mr_irq_t *irq) {
    mr_irq_action_t *action;
    mr_uint32_t i;

    /* Remove irq from table */
    for (i = irq->istart; i <= irq->iend; i++) {
        /* Free irq actions */
        while (__irq_table[i].action) {
            /* Remove irq action from irq descriptor */
            action = __irq_table[i].action;
            __irq_table[i].action = action->next;
            irq_action_free(action);
        }

        /* Disable irq */
        irq_depth_disable(i, &__irq_table[i]);
        __irq_table[i].irq = MR_NULL;
    }
}

static mr_err_t irq_init(mr_irq_t *irq, const mr_clazz_t *clazz,
                         mr_uint32_t irq_start, mr_uint32_t irq_end,
                         const mr_irq_ops_t *ops, mr_ptr_t priv) {
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__irq_lock);

    /* Insert irq to table */
    ret = irq_table_insert(irq, irq_start, irq_end);
    if (ret != 0) {
        goto _exit;
    }

    /* Init irq */
    mr_atomic_init(&irq->dying, MR_FALSE);
    irq->istart = irq_start;
    irq->iend = irq_end;
    irq->ops = ops;
    irq->priv = priv;

    /* Init object */
    ret = mr_object_init((mr_object_t *)irq, clazz);
    if (ret != 0) {
        irq_table_remove(irq);
        goto _exit;
    }
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__irq_lock, mask);
    return ret;
}

mr_err_t mr_irq_init(mr_irq_t *irq, mr_uint32_t irq_start, mr_uint32_t irq_end,
                     const mr_irq_ops_t *ops, void *priv) {
    const mr_clazzes_t *clazzes = MR_CLAZZES_FIND(irq);
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(clazzes);

    /* Check parameter */
    MR_ASSERT((irq != MR_NULL) && (!MR_OBJECT_IS_INITED(irq)));
    MR_ASSERT(irq_start < MR_CFG_IRQ_TABLE_SIZE);
    MR_ASSERT(irq_end < MR_CFG_IRQ_TABLE_SIZE);
    MR_ASSERT(irq_start <= irq_end);
    MR_ASSERT(ops != MR_NULL);

    /* Init workqueue */
    return irq_init(irq, clazz, irq_start, irq_end, ops, priv);
}

mr_irq_t *mr_irq_create(mr_uint32_t irq_start, mr_uint32_t irq_end,
                        const mr_irq_ops_t *ops, void *priv) {
    const mr_clazzes_t *clazzes = MR_CLAZZES_FIND(irq);
    const mr_clazz_t *clazz = MR_CLAZZES_DYNAMIC(clazzes);
    mr_irq_t *irq;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(irq_start < MR_CFG_IRQ_TABLE_SIZE);
    MR_ASSERT(irq_end < MR_CFG_IRQ_TABLE_SIZE);
    MR_ASSERT(irq_start <= irq_end);
    MR_ASSERT(ops != MR_NULL);

    /* Create irq */
    irq = mr_malloc(sizeof(mr_irq_t));
    if (!irq) {
        return MR_NULL;
    }

    /* Init irq */
    ret = irq_init(irq, clazz, irq_start, irq_end, ops, priv);
    if (ret != 0) {
        mr_free(irq);
        return MR_NULL;
    }
    return irq;
}

mr_err_t mr_irq_del(mr_irq_t *irq) {
    /* Check parameter */
    MR_ASSERT((irq != MR_NULL) && MR_OBJECT_IS_INITED(irq));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(irq, irq));

    /* Mark irq as dying */
    mr_atomic_store(&irq->dying, MR_TRUE);

    /* Delete object */
    mr_object_del((mr_object_t *)irq);
    return 0;
}

#if defined(MR_USE_IRQ_DEFER)
static void irq_action_defer_entry(mr_work_t *work, void *param) {
    mr_irq_action_t *action;
    mr_irq_entry_t *entry;
    mr_uint32_t irq;
    void *owner;
    int mask;

    MR_IGNORE(work);

    /* Get irq action */
    action = (mr_irq_action_t *)param;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__irq_lock);

    /* Save irq action entry, owner and irq */
    entry = (mr_irq_entry_t *)action->defer_entry;
    owner = (void *)action->owner;
    irq = action->irq;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__irq_lock, mask);

    /* Check if irq action was freed */
    if (!owner) {
        return;
    }

    /* Call irq action defer entry */
    entry(irq, owner);
}
#endif /* defined(MR_USE_IRQ_DEFER) */

static mr_irq_action_t *irq_action_alloc(mr_uint32_t irq, mr_irq_entry_t *entry,
                                         mr_ptr_t owner,
                                         mr_irq_entry_t *defer_entry) {
    mr_irq_action_t *action;
    mr_err_t ret;

    /* Create irq action */
    action = mr_malloc(sizeof(mr_irq_action_t));
    if (!action) {
        return MR_NULL;
    }

#if defined(MR_USE_IRQ_DEFER)
    /* Init irq action defer work */
    ret = mr_work_init(&action->defer, irq_action_defer_entry, action);
    if (ret != 0) {
        mr_free(action);
        return MR_NULL;
    }
    action->defer_entry = defer_entry;
#else
    MR_IGNORE(defer_entry);
    MR_IGNORE(ret);
#endif /* MR_USE_IRQ_DEFER */

    /* Init irq action */
    action->irq = irq & ((1U << 31) - 1);
    action->pending = MR_FALSE;
    action->next = MR_NULL;
    action->entry = entry;
    action->owner = owner;
    return action;
}

static void irq_type_set(mr_uint32_t irq, mr_irq_desc_t *desc,
                         mr_uint8_t type) {
    /* Set irq type */
    desc->irq->ops->type_set(irq, type, desc->irq->priv);
}

static void irq_priority_set(mr_uint32_t irq, mr_irq_desc_t *desc,
                             mr_uint8_t priority) {
    /* Set irq priority */
    desc->irq->ops->priority_set(irq, priority, desc->irq->priv);
    desc->priority = priority;
}

static void irq_depth_enable(mr_uint32_t irq, mr_irq_desc_t *desc) {
    mr_atomic_t last;

    /* Decrease depth */
    last = mr_atomic_fetch_sub(&desc->depth, 1);
    if (last != 1) {
        /* Depth does not change from 1 to 0 */
        if (last <= 0) {
            /* No depth when enabled */
            mr_atomic_store(&desc->depth, 0);
        }
        return;
    }

    /* Enable irq */
    desc->irq->ops->unmask(irq, desc->irq->priv);
    desc->irq->ops->enable(irq, desc->irq->priv);
}

static mr_err_t irq_request(mr_uint32_t irq, mr_uint16_t flags,
                            mr_irq_action_t *action) {
    mr_irq_action_t **a;
    mr_irq_desc_t *desc;
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__irq_lock);

    /* Check if irq is dying */
    desc = &__irq_table[irq];
    if ((!desc->irq) || mr_atomic_load(&desc->irq->dying)) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* Check if irq descriptor action is exist */
    if (!desc->action) {
        /* Set irq type */
        irq_type_set(irq, desc, flags & IRQ_FLAG_TYPE_MASK);

        /* Set irq default priority */
        irq_priority_set(irq, desc, IRQ_PRIORITY_DEFAULT);

        /* Set irq descriptor flags */
        desc->flags = flags;
    } else {
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
    mr_spinlock_unlock_irqrestore(&__irq_lock, mask);
    return ret;
}

static mr_err_t irq_defer_request(mr_uint32_t irq, mr_irq_entry_t *entry,
                                  mr_ptr_t owner, mr_uint16_t flags,
                                  mr_irq_entry_t *defer_entry) {
    mr_irq_action_t *action;
    mr_err_t ret;

    /* Create irq action */
    action = irq_action_alloc(irq, entry, owner, defer_entry);
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

mr_err_t mr_irq_request(mr_uint32_t irq, mr_irq_entry_t *entry, void *owner,
                        mr_uint16_t flags) {
    /* Check parameter */
    MR_ASSERT(irq < MR_CFG_IRQ_TABLE_SIZE);
    MR_ASSERT((entry != MR_NULL) && (owner != MR_NULL));
    MR_ASSERT(flags != 0);

    /* Request irq */
    return irq_defer_request(irq, entry, owner, flags, MR_NULL);
}

mr_err_t mr_irq_defer_request(mr_uint32_t irq, mr_irq_entry_t *entry,
                              void *owner, mr_uint16_t flags,
                              mr_irq_entry_t *defer_entry) {
    /* Check parameter */
    MR_ASSERT(irq < MR_CFG_IRQ_TABLE_SIZE);
    MR_ASSERT((entry != MR_NULL) && (owner != MR_NULL));
    MR_ASSERT(flags != 0);
    MR_ASSERT(defer_entry != MR_NULL);

    /* Request defer irq */
    return irq_defer_request(irq, entry, owner, flags, defer_entry);
}

mr_err_t mr_irq_free(mr_uint32_t irq, void *owner) {
    mr_irq_action_t *action, **a;
    mr_irq_desc_t *desc;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT(irq < MR_CFG_IRQ_TABLE_SIZE);
    MR_ASSERT(owner != MR_NULL);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__irq_lock);

    /* Check if irq is dying */
    desc = &__irq_table[irq];
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
        if ((*a)->pending) {
            /* Mark irq action, defer free */
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
    mr_spinlock_unlock_irqrestore(&__irq_lock, mask);
    return ret;
}

mr_err_t mr_irq_enable(mr_uint32_t irq) {
    mr_irq_desc_t *desc;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT(irq < MR_CFG_IRQ_TABLE_SIZE);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__irq_lock);

    /* Check if irq is dying */
    desc = &__irq_table[irq];
    if ((!desc->irq) || mr_atomic_load(&desc->irq->dying)) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* Enable irq */
    irq_depth_enable(irq, desc);
    ret = 0;
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__irq_lock, mask);
    return ret;
}

mr_err_t mr_irq_disable(mr_uint32_t irq) {
    mr_irq_desc_t *desc;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT(irq < MR_CFG_IRQ_TABLE_SIZE);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__irq_lock);

    /* Check if irq is dying */
    desc = &__irq_table[irq];
    if ((!desc->irq) || mr_atomic_load(&desc->irq->dying)) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* Disable irq */
    irq_depth_disable(irq, desc);
    ret = 0;
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__irq_lock, mask);
    return ret;
}

int mr_irq_type(mr_uint32_t irq) {
    mr_irq_desc_t *desc;
    int mask, ret;

    /* Check parameter */
    MR_ASSERT(irq < MR_CFG_IRQ_TABLE_SIZE);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__irq_lock);

    /* Check if irq is not exist */
    desc = &__irq_table[irq];
    if (!desc->irq) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* Get irq type */
    ret = (mr_uint8_t)(desc->flags & IRQ_FLAG_TYPE_MASK);
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__irq_lock, mask);
    return ret;
}

mr_err_t mr_irq_priority_set(mr_uint32_t irq, mr_uint8_t priority) {
    mr_irq_desc_t *desc;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT(irq < MR_CFG_IRQ_TABLE_SIZE);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__irq_lock);

    /* Check if irq is dying */
    desc = &__irq_table[irq];
    if ((!desc->irq) || mr_atomic_load(&desc->irq->dying)) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* Set irq priority */
    irq_priority_set(irq, desc, priority);
    ret = 0;
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__irq_lock, mask);
    return ret;
}

int mr_irq_priority(mr_uint32_t irq) {
    mr_irq_desc_t *desc;
    int mask, ret;

    /* Check parameter */
    MR_ASSERT(irq < MR_CFG_IRQ_TABLE_SIZE);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__irq_lock);

    /* Check if irq is not exist */
    desc = &__irq_table[irq];
    if (!desc->irq) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* Get irq priority */
    ret = (mr_uint8_t)desc->priority;
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__irq_lock, mask);
    return ret;
}

mr_irq_return_t mr_irq_handle(mr_uint32_t irq) {
    mr_irq_action_t **a, *action, action2;
    mr_irq_entry_t *entry;
    mr_irq_return_t ret;
    mr_irq_desc_t *desc;
    void *owner;
    int mask;

    /* Check parameter */
    MR_ASSERT(irq < MR_CFG_IRQ_TABLE_SIZE);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__irq_lock);

    /* Check if irq is dying */
    desc = &__irq_table[irq];
    if ((!desc->irq) || mr_atomic_load(&desc->irq->dying)) {
        ret = MR_IRQ_NONE;
        goto _exit;
    }

    /* Get irq run-time */
    mr_irq_get(desc->irq);

    /* Mask irq */
    desc->irq->ops->mask(irq, desc->irq->priv);

    /* Handle irq action */
    for (ret = MR_IRQ_NONE, action = desc->action; action;
         action = action->next) {
        /* Check if irq action is being freed */
        if (!action->owner) {
            continue;
        }

        /* Mark irq action pending */
        action->pending = MR_TRUE;

        /* Save irq action entry and owner */
        entry = (mr_irq_entry_t *)action->entry;
        owner = (void *)action->owner;

        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__irq_lock, mask);

        /* Call irq action entry */
        ret = entry(irq, owner);

        /* Lock */
        mask = mr_spinlock_lock_irqsave(&__irq_lock);

        /* Mark irq action not pending */
        action->pending = MR_FALSE;

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
                action2.next = *a = action->next;
#if defined(MR_USE_IRQ_DEFER)
                /* To prevent entering the defer branch after a break */
                action2.defer_entry = MR_NULL;
#endif /* defined(MR_USE_IRQ_DEFER) */
                irq_action_free(action);
                action = &action2;
                break;
            }
        }

        /* Check if irq action is handled */
        if (ret == MR_IRQ_HANDLED) {
            break;
        }

        /* Check if irq action is deferred */
        if (ret == MR_IRQ_DEFERRED) {
#if defined(MR_USE_IRQ_DEFER)
            /* Check if irq action supports deferring */
            if (action->defer_entry) {
                /* Add irq action to the defer workqueue */
                mr_workqueue_work(&__irq_defer_queue, &action->defer);
            } else
#endif /* defined(MR_USE_IRQ_DEFER) */
            {
                /* Not support deferring */
                ret = MR_IRQ_HANDLED;
            }
            break;
        }
    }

    /* Unmask irq if irq action is enabled */
    if (mr_atomic_load(&desc->depth) == 0) {
        desc->irq->ops->unmask(irq, desc->irq->priv);
    }

    /* Put irq run-time */
    mr_irq_put(desc->irq);
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__irq_lock, mask);
    return ret;
}

#if defined(MR_USE_IRQ_DEFER)
void mr_irq_defer_execute(void) {
    mr_workqueue_t *queue;

    /* Get irq workqueue */
    queue = mr_workqueue_get(&__irq_defer_queue);
    if (!queue) {
        return;
    }

    /* Execute irq workqueue */
    mr_workqueue_execute(queue);

    /* Put irq workqueue */
    mr_workqueue_put(queue);
}
#endif /* defined(MR_USE_IRQ_DEFER) */

#if defined(MR_USE_IRQ_DEFER_HOOK)
static void irq_workqueue_hook_wakeup(mr_workqueue_t *queue, void *param) {
    const mr_irq_defer_hook_t *hook;

    MR_IGNORE(queue);

    /* Check irq defer wakeup hook */
    hook = *(const mr_irq_defer_hook_t **)param;
    if (!hook->wakeup) {
        return;
    }

    /* Call irq defer wakeup hook */
    hook->wakeup(hook->param);
}

static void irq_workqueue_hook_suspend(mr_workqueue_t *queue, void *param) {
    const mr_irq_defer_hook_t *hook;

    MR_IGNORE(queue);

    /* Check irq defer suspend hook */
    hook = *(const mr_irq_defer_hook_t **)param;
    if (!hook->suspend) {
        return;
    }

    /* Call irq defer suspend hook */
    hook->suspend(hook->param);
}

mr_err_t mr_irq_defer_hook_set(const mr_irq_defer_hook_t *hook) {
    /* Check parameter */
    MR_ASSERT(hook != MR_NULL);

    /* Set irq defer hook */
    __irq_hook = hook;
    return 0;
}
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */

static void irq_del(mr_irq_t *irq) {
    int mask;

    /* Mark irq as dying */
    mr_atomic_store(&irq->dying, MR_TRUE);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__irq_lock);

    /* Remove irq from table */
    irq_table_remove(irq);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__irq_lock, mask);
}

static void irq_statik_release(mr_object_t *obj) {
    mr_irq_t *irq;

    /* Get irq */
    irq = MR_CONTAINER_OF(obj, mr_irq_t, parent);

    /* Delete irq */
    irq_del(irq);
}

static void irq_dynamic_release(mr_object_t *obj) {
    /* Release irq */
    irq_statik_release(obj);

    /* Free irq */
    mr_free(obj);
}

void mr_kernel_irq_init(void) {
#if defined(MR_USE_IRQ_DEFER_HOOK)
    static const mr_workqueue_hook_t queue_hook
        = {.wakeup = irq_workqueue_hook_wakeup,
           .suspend = irq_workqueue_hook_suspend,
           .param = &__irq_hook};
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */

#if defined(MR_USE_IRQ_DEFER)
    /* Init irq defer workqueue */
    mr_workqueue_init(&__irq_defer_queue);
#endif /* defined(MR_USE_IRQ_DEFER) */
#if defined(MR_USE_IRQ_DEFER_HOOK)
    mr_workqueue_hook_set(&__irq_defer_queue, &queue_hook);
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */
}
#endif /* defined(MR_USE_IRQ) */

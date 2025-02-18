/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/kos/mr_kthread.h>
#if defined(MR_USE_KOS)
#include <kernel/mr_initcall.h>
#include <libc/mr_malloc.h>
#include <libc/mr_string.h>

/* Kthread ktype */
static mr_ktype_t ktype1, ktype2;
/* Kthread kset */
static mr_kset_t kroot = MR_KSET_INIT(&kroot, MR_NULL);

/* Kthread ops macro */
#define KOS_KTHREAD_OPS(_kos)           ((_kos)->type->kth_type->ops)

MR_INLINE mr_err_t kthread_res_alloc(mr_kthread_t *kth, mr_size_t size) {
    void *res;

    /* Alloc resource */
    if (size <= sizeof(kth->ires)) {
        /* Use inline resource if possible */
        res = kth->ires;
    } else {
        res = mr_malloc(size);
        if (!res) {
            return -MR_ENOMEM;
        }
    }

    /* Init resource */
    mr_memset(res, 0, size);
    kth->res = res;
    return 0;
}

MR_INLINE void kthread_res_free(mr_kthread_t *kth) {
    const void *res;

    /* If resource is inline or takeover, not need to free */
    res = ((kth->res != kth->ires) && (kth->entry)) ? kth->res : MR_NULL;
    if (res) {
        mr_free((void *)res);
    }
}

MR_INLINE mr_bool_t kthread_is_takeover(mr_kthread_t *kth) {
    return (kth->res != kth->ires) && (!kth->entry);
}

MR_INLINE mr_err_t kthread_init(mr_kthread_t *kth, const char *name,
                                mr_ptr_t entry, mr_ptr_t args, void *stack,
                                mr_size_t stack_size, mr_uint32_t priority,
                                mr_tick_t tick, mr_kos_t *kos) {
    mr_kthread_type_t *type;
    mr_err_t ret;

    /* Get kthread kth_type and check priority */
    type = kos->type->kth_type;
    if ((!type) || (priority >= type->priority_max)) {
        return -MR_EINVAL;
    }

    /* Non-takeover kthread need to initialize resources */
    if (!kthread_is_takeover(kth)) {
        /* Alloc resource */
        ret = kthread_res_alloc(kth, type->size);
        if (ret != 0) {
            return ret;
        }

        /* Init kthread resource */
        ret = KOS_KTHREAD_OPS(kos)->init((void *)kth->res, name, kth, stack,
                                         stack_size, priority, tick);
        if (ret != 0) {
            kthread_res_free(kth);
            return ret;
        }
    }

    /* Init kthread */
    kth->priority = priority;
    kth->tick = tick;
    kth->entry = entry;
    kth->args = args;
    kth->os = mr_kos_get(kos);
    return 0;
}

MR_INLINE mr_err_t kthread_del(mr_kthread_t *kth) {
    /* Takeover kthread do not need to delete */
    if (kthread_is_takeover(kth)) {
        return 0;
    }

    /* Delete kthread resource */
    return KOS_KTHREAD_OPS(kth->os)->del((void *)kth->res);
}

MR_INLINE mr_err_t kthread_init_add(mr_kthread_t *kth, mr_ktype_t *ktype,
                                    const char *name, mr_ptr_t entry,
                                    mr_ptr_t args, void *stack,
                                    mr_size_t stack_size, mr_uint32_t priority,
                                    mr_tick_t tick) {
    mr_kos_t *kos;
    mr_err_t ret;

    /* Get kos */
    kos = mr_kos_get(mr_kos_find());
    if (!kos) {
        return -MR_ENOSYS;
    }

    /* Init kthread */
    ret = kthread_init(kth, name, entry, args, stack, stack_size, priority,
                       tick, kos);
    if (ret != 0) {
        goto _exit;
    }

    /* Init kobject */
    mr_kobject_init((mr_kobject_t *)kth, ktype);

    /* Add kobject to kroot */
    ret = mr_kobject_add((mr_kobject_t *)kth, (mr_kobject_t *)&kroot, name);
    if (ret != 0) {
        mr_kos_put(kos);
        kthread_del(kth);
        kthread_res_free(kth);
        goto _exit;
    }

_exit:
    /* Put kos */
    mr_kos_put(kos);
    return ret;
}

mr_err_t mr_kthread_init(mr_kthread_t *kth, const char *name,
                         void (*entry)(mr_kthread_t *kth, void *args),
                         void *args, void *stack, mr_size_t stack_size,
                         mr_uint32_t priority, mr_tick_t tick) {
    /* Check arguments */
    if ((!kth) || (MR_KOBJECT_IS_INITED(kth)) || (!name) || (!entry) || (!stack)
        || (!tick)) {
        return -MR_EINVAL;
    }

    /* Init and add kthread */
    return kthread_init_add(kth, &ktype1, name, (mr_ptr_t)entry, args, stack,
                            stack_size, priority, tick);
}

mr_kthread_t *mr_kthread_create(const char *name,
                                void (*entry)(mr_kthread_t *kth, void *args),
                                void *args, mr_size_t stack_size,
                                mr_uint32_t priority, mr_tick_t tick) {
    mr_kthread_t *kth;
    mr_uint8_t *stack;
    mr_err_t ret;

    /* Check arguments */
    if ((!name) || (!entry) || (!tick)) {
        return MR_NULL;
    }

    /* Create kthread */
    kth = mr_malloc(sizeof(mr_kthread_t) + stack_size);
    if (!kth) {
        return MR_NULL;
    }

    /* Init and add kthread */
    stack = (mr_uint8_t *)kth + sizeof(mr_kthread_t);
    ret = kthread_init_add(kth, &ktype2, name, (mr_ptr_t)entry, args, stack,
                           stack_size, priority, tick);
    if (ret != 0) {
        mr_free(kth);
        return MR_NULL;
    }
    return kth;
}

MR_INLINE mr_err_t kthread_takeover(mr_kthread_t *kth, const char *name,
                                    const void *res, mr_uint32_t priority,
                                    mr_tick_t tick) {
    mr_err_t ret;

    /* Takeover kthread resource */
    kth->res = res;

    /* Init and add kthread */
    ret = kthread_init_add(kth, &ktype1, name, MR_NULL, MR_NULL, MR_NULL, 0,
                           priority, tick);
    if (ret == 0) {
        /* Prevents run-time release */
        mr_kthread_get(kth);
    }
    return ret;
}

mr_err_t mr_kthread_takeover(mr_kthread_t *kth, const char *name,
                             const void *res, mr_uint32_t priority,
                             mr_tick_t tick) {
    /* Check arguments */
    if ((!kth) || (MR_KOBJECT_IS_INITED(kth)) || (!name) || (!res)) {
        return -MR_EINVAL;
    }

    /* Takeover kthread */
    return kthread_takeover(kth, name, res, priority, tick);
}

mr_err_t mr_kthread_del(mr_kthread_t *kth) {
    mr_err_t ret;

    /* Check arguments */
    if ((!kth) || (!MR_KOBJECT_IS_INITED(kth))) {
        return -MR_EINVAL;
    }

    /* Delete kthread */
    ret = kthread_del(kth);
    if (ret != 0) {
        return ret;
    }

    /* Delete kobject */
    mr_kobject_del((mr_kobject_t *)kth);
    return 0;
}

MR_INLINE mr_err_t kthread_startup(mr_kthread_t *kth) {
    mr_err_t ret;

    /* Prevents run-time release */
    mr_kthread_get(kth);

    /* Startup kthread resource */
    ret = KOS_KTHREAD_OPS(kth->os)->startup((void *)kth->res);
    if (ret != 0) {
        mr_kthread_put(kth);
        return ret;
    }
    return 0;
}

mr_err_t mr_kthread_startup(mr_kthread_t *kth) {
    /* Check arguments */
    if ((!kth) || (!MR_KOBJECT_IS_INITED(kth))) {
        return -MR_EINVAL;
    }

    /* Start kthread */
    return kthread_startup(kth);
}

MR_INLINE mr_err_t kthread_resume(mr_kthread_t *kth) {
    /* Resume kthread resource */
    return KOS_KTHREAD_OPS(kth->os)->resume((void *)kth->res);
}

mr_err_t mr_kthread_resume(mr_kthread_t *kth) {
    /* Check arguments */
    if ((!kth) || (!MR_KOBJECT_IS_INITED(kth))) {
        return -MR_EINVAL;
    }

    /* Resume kthread */
    return kthread_resume(kth);
}

MR_INLINE mr_err_t kthread_yield(void) {
    mr_kos_t *kos;
    mr_err_t ret;

    /* Get kos */
    kos = mr_kos_get(mr_kos_find());
    if (!kos) {
        return -MR_ENOSYS;
    }

    /* Yield kthread resource */
    ret = KOS_KTHREAD_OPS(kos)->yield();

    /* Put kos */
    mr_kos_put(kos);
    return ret;
}

mr_err_t mr_kthread_yield(void) {
    /* Yield kthread */
    return kthread_yield();
}

MR_INLINE mr_err_t kthread_suspend(mr_kthread_t *kth) {
    /* Suspend kthread resource */
    return KOS_KTHREAD_OPS(kth->os)->suspend((void *)kth->res);
}

mr_err_t mr_kthread_suspend(mr_kthread_t *kth) {
    /* Check arguments */
    if ((!kth) || (!MR_KOBJECT_IS_INITED(kth))) {
        return -MR_EINVAL;
    }

    /* Suspend kthread */
    return kthread_suspend(kth);
}

MR_INLINE mr_err_t kthread_sleep(mr_tick_t tick) {
    mr_kos_t *kos;
    mr_err_t ret;

    /* Get kos */
    kos = mr_kos_get(mr_kos_find());
    if (!kos) {
        return -MR_ENOSYS;
    }

    /* Sleep kthread resource */
    ret = KOS_KTHREAD_OPS(kos)->sleep(tick);

    /* Put kos */
    mr_kos_put(kos);
    return ret;
}

mr_err_t mr_kthread_sleep(mr_tick_t tick) {
    /* Sleep kthread */
    return kthread_sleep(tick);
}

MR_INLINE mr_err_t kthread_exit(mr_kthread_t *kth) {
    /* Exit kthread resource */
    return KOS_KTHREAD_OPS(kth->os)->exit((void *)kth->res);
}

mr_err_t mr_kthread_exit(mr_kthread_t *kth) {
    /* Check arguments */
    if ((!kth) || (!MR_KOBJECT_IS_INITED(kth))) {
        return -MR_EINVAL;
    }

    /* Exit kthread */
    return kthread_exit(kth);
}

MR_INLINE mr_kthread_t *kthread_self(void) {
    mr_kthread_t *ret;
    mr_kos_t *kos;

    /* Get kos */
    kos = mr_kos_get(mr_kos_find());
    if (!kos) {
        return MR_NULL;
    }

    /* Get kthread self */
    ret = KOS_KTHREAD_OPS(kos)->self();

    /* Put kos */
    mr_kos_put(kos);
    return ret;
}

mr_kthread_t *mr_kthread_self(void) {
    /* Get kthread self */
    return kthread_self();
}

void mr_kthread_cleanup(mr_kthread_t *kth) {
    /* Check arguments */
    if ((!kth) || (!MR_KOBJECT_IS_INITED(kth))) {
        return;
    }

    /* Free runtime */
    mr_kthread_put(kth);
}

void mr_kthread_entry(mr_kthread_t *kth) {
    /* Check arguments */
    if ((!kth) || (!MR_KOBJECT_IS_INITED(kth)) || (!kth->entry)) {
        return;
    }

    /* Call kthread entry */
    ((void (*)(mr_kthread_t *, void *))kth->entry)(kth, kth->args);
}

mr_kthread_t *mr_kthread_find(const char *name) {
    /* Lookup kthread in kroot */
    return (mr_kthread_t *)mr_kobject_lookup((mr_kobject_t *)&kroot, name);
}

MR_INLINE mr_kthread_t *kthread_release_kobj(mr_kobject_t *kobj) {
    mr_kthread_t *kth;

    /* Get kthread */
    kth = MR_CONTAINER_OF(kobj, mr_kthread_t, parent);

    /* Free kthread resource */
    kthread_res_free(kth);

    /* Put kos */
    mr_kos_put(kth->os);
    return kth;
}

MR_INLINE void kthread_release1_kobj(mr_kobject_t *kobj) {
    /* Release kthread */
    kthread_release_kobj(kobj);
}

MR_INLINE void kthread_release2_kobj(mr_kobject_t *kobj) {
    mr_kthread_t *kth;

    /* Release kthread */
    kth = kthread_release_kobj(kobj);

    /* Free kthread */
    mr_free(kth);
}

MR_INLINE void mr_kthread_kset_init(void) {
    /* Register kset */
    mr_kset_register(&kroot, "thread");

    /* Init ktype */
    mr_ktype_init(&ktype1, kthread_release1_kobj);
    mr_ktype_init(&ktype2, kthread_release2_kobj);
}
MR_INIT_EXPORT(mr_kthread_kset_init, MR_INIT_LEVEL_KERNEL);
#endif /* defined(MR_USE_KOS) */

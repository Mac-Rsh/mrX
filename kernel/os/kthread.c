/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/os/mr_kthread.h>
#if defined(MR_USE_KOS)
#include <kernel/mr_initcall.h>
#include <libc/mr_malloc.h>
#include <port/mr_port_thread.h>

/* Kthread ktype */
static mr_ktype_t ktype1, ktype2;
/* Kthread kset */
static mr_kset_t kroot = MR_KSET_INIT(&kroot, MR_NULL);

MR_INLINE mr_err_t kthread_init(mr_kthread_t *kth, const char *name,
                                mr_ptr_t entry, mr_ptr_t args,
                                mr_size_t stack_size, mr_uint32_t priority,
                                mr_tick_t tick) {
    mr_ptr_t thread;

    /* Create kthread */
    if (!MR_KTHREAD_IS_TAKEOVER(kth)) {
        thread = mr_port_thread_create(name, kth, stack_size, priority, tick);
        if (!thread) {
            return -MR_ENOMEM;
        }
    } else {
        /* Takeover kthread do not need to delete */
        thread = kth->thread;
    }

    /* Init kthread */
    kth->thread = thread;
    kth->entry = entry;
    kth->args = args;
    return 0;
}

MR_INLINE mr_err_t kthread_del(mr_kthread_t *kth) {
    /* Takeover kthread do not need to delete */
    if (MR_KTHREAD_IS_TAKEOVER(kth)) {
        return 0;
    }

    /* Delete kthread */
    return mr_port_thread_del(kth->thread);
}

MR_INLINE mr_err_t kthread_init_add(mr_kthread_t *kth, mr_ktype_t *ktype,
                                    const char *name, mr_ptr_t entry,
                                    mr_ptr_t args, mr_size_t stack_size,
                                    mr_uint32_t priority, mr_tick_t tick) {
    mr_err_t ret;

    /* Init kthread */
    ret = kthread_init(kth, name, entry, args, stack_size, priority, tick);
    if (ret != 0) {
        return ret;
    }

    /* Init kobject */
    mr_kobject_init((mr_kobject_t *)kth, ktype);

    /* Add kobject to kroot */
    ret = mr_kobject_add((mr_kobject_t *)kth, (mr_kobject_t *)&kroot, name);
    if (ret != 0) {
        kthread_del(kth);
    }
    return ret;
}

mr_err_t mr_kthread_init(mr_kthread_t *kth, const char *name,
                         void (*entry)(mr_kthread_t *kth, void *args),
                         void *args, mr_size_t stack_size, mr_uint32_t priority,
                         mr_tick_t tick) {
    /* Check arguments */
    if ((!kth) || MR_KTHREAD_IS_INITED(kth) || (!name) || (!entry) || (!tick)) {
        return -MR_EINVAL;
    }

    /* Init and add kthread */
    return kthread_init_add(kth, &ktype1, name, (mr_ptr_t)entry, args,
                            stack_size, priority, tick);
}

mr_kthread_t *mr_kthread_create(const char *name,
                                void (*entry)(mr_kthread_t *kth, void *args),
                                void *args, mr_size_t stack_size,
                                mr_uint32_t priority, mr_tick_t tick) {
    mr_kthread_t *kth;
    mr_err_t ret;

    /* Check arguments */
    if ((!name) || (!entry) || (!tick)) {
        return MR_NULL;
    }

    /* Create kthread */
    kth = mr_malloc(sizeof(mr_kthread_t));
    if (!kth) {
        return MR_NULL;
    }

    /* Init and add kthread */
    ret = kthread_init_add(kth, &ktype2, name, (mr_ptr_t)entry, args,
                           stack_size, priority, tick);
    if (ret != 0) {
        mr_free(kth);
        return MR_NULL;
    }
    return kth;
}

MR_INLINE mr_err_t kthread_takeover(mr_kthread_t *kth, const char *name,
                                    void *thread) {
    /* Takeover kthread */
    kth->thread = thread;

    /* Init and add kthread */
    return kthread_init_add(kth, &ktype1, name, MR_NULL, MR_NULL, 0, 0, 0);
}

mr_err_t mr_kthread_takeover(mr_kthread_t *kth, const char *name,
                             void *thread) {
    /* Check arguments */
    if ((!kth) || MR_KTHREAD_IS_INITED(kth) || (!name) || (!thread)) {
        return -MR_EINVAL;
    }

    /* Takeover kthread */
    return kthread_takeover(kth, name, thread);
}

mr_err_t mr_kthread_del(mr_kthread_t *kth) {
    /* Check arguments */
    if ((!kth) || (!MR_KTHREAD_IS_INITED(kth))) {
        return -MR_EINVAL;
    }

    /* Delete kobject */
    mr_kobject_del((mr_kobject_t *)kth);
    return 0;
}

mr_err_t mr_kthread_startup(mr_kthread_t *kth) {
    /* Check arguments */
    if ((!kth) || (!MR_KTHREAD_IS_INITED(kth))) {
        return -MR_EINVAL;
    }

    /* Start kthread */
    return mr_port_thread_startup(kth->thread);
}

mr_err_t mr_kthread_resume(mr_kthread_t *kth) {
    /* Check arguments */
    if ((!kth) || (!MR_KTHREAD_IS_INITED(kth))) {
        return -MR_EINVAL;
    }

    /* Resume kthread */
    return mr_port_thread_resume(kth->thread);
}

mr_err_t mr_kthread_yield(void) {
    /* Yield kthread */
    return mr_port_thread_yield();
}

mr_err_t mr_kthread_suspend(mr_kthread_t *kth) {
    /* Check arguments */
    if ((!kth) || (!MR_KTHREAD_IS_INITED(kth))) {
        return -MR_EINVAL;
    }

    /* Suspend kthread */
    return mr_port_thread_suspend(kth);
}

mr_err_t mr_kthread_sleep(mr_tick_t tick) {
    /* Sleep kthread */
    return mr_port_thread_sleep(tick);
}

mr_kthread_t *mr_kthread_self(void) {
    /* Get kthread self */
    return mr_port_thread_self();
}

void mr_kthread_entry(mr_kthread_t *kth) {
    /* Check arguments */
    if ((!kth) || (!MR_KTHREAD_IS_INITED(kth))
        || (MR_KTHREAD_IS_TAKEOVER(kth))) {
        return;
    }

    /* Call kthread entry */
    ((void (*)(mr_kthread_t *, void *))kth->entry)(kth, kth->args);

    /* Auto delete kthread after entry */
    mr_kthread_del(kth);
}

mr_kthread_t *mr_kthread_find(const char *name) {
    /* Lookup kthread in kroot */
    return (mr_kthread_t *)mr_kobject_lookup((mr_kobject_t *)&kroot, name);
}

MR_INLINE mr_kthread_t *kthread_release_kobj(mr_kobject_t *kobj) {
    mr_kthread_t *kth;

    /* Get kthread */
    kth = MR_CONTAINER_OF(kobj, mr_kthread_t, parent);

    /* Delete kthread */
    kthread_del(kth);
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

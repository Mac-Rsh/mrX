/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/modules/xvm/mr_xvm.h>
#if defined(MR_USE_MODULE_XVM)
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>

/* XVM clazzes(static & dynamic) */
static void xvm_statik_release(mr_object_t *obj);
static void xvm_dynamic_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(xvm, xvm_statik_release, xvm_dynamic_release, MR_NULL);

/* XVM ISA clazzes(static only) */
static void xvm_isa_statik_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(xvm_isa, xvm_isa_statik_release, MR_NULL, MR_NULL);

/* XVM lock */
static mr_spinlock_t __xvm_lock = MR_SPINLOCK_INIT();
/* XVM ISA list */
static mr_xvm_isa_t *__xvm_isa_list = MR_NULL;

static mr_err_t xvm_init(mr_xvm_t *xvm, const mr_clazz_t *clazz,
                         const char *isa_name, void *data,
                         mr_size_t data_size) {
    mr_xvm_isa_t *isa;
    mr_err_t ret;

    /* Get XVM ISA */
    isa = mr_xvm_isa_find(isa_name);
    if (!isa) {
        return -MR_ENOENT;
    }

    /* Init XVM */
    mr_atomic_init(&xvm->status, MR_XVM_STATUS_INIT);
    mr_atomic_store(&xvm->flags, 0);
    xvm->datamem.size = data_size;
    xvm->datamem.data = data;
    xvm->bin.image = MR_NULL;
    xvm->bin.entry = 0x0;
    xvm->bin.size = 0;
    xvm->isa = isa;

    /* Init object */
    ret = mr_object_init((mr_object_t *)xvm, clazz);
    if (ret != 0) {
        mr_xvm_isa_put(isa);
        xvm->isa = MR_NULL;
        return ret;
    }
    return 0;
}

mr_err_t mr_xvm_init(mr_xvm_t *xvm, const char *isa_name, void *data,
                     mr_size_t data_size) {
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(xvm);

    /* Check parameter */
    MR_ASSERT(xvm != MR_NULL);
    MR_ASSERT((data != MR_NULL) && (data_size > 0));
    MR_ASSERT(isa_name != MR_NULL);

    /* Init XVM */
    return xvm_init(xvm, clazz, isa_name, data, data_size);
}

mr_xvm_t *mr_xvm_create(const char *isa_name, mr_size_t data_size) {
    const mr_clazz_t *clazz = MR_CLAZZES_DYNAMIC(xvm);
    mr_xvm_t *xvm;
    mr_err_t ret;
    void *data;

    /* Check parameter */
    MR_ASSERT(isa_name != MR_NULL);
    MR_ASSERT(data_size > 0);

    /* Create XVM */
    xvm = mr_malloc(sizeof(mr_xvm_t) + data_size);
    if (!xvm) {
        return MR_NULL;
    }

    /* Init XVM */
    data = (mr_uint8_t *)xvm + sizeof(mr_xvm_t);
    ret = xvm_init(xvm, clazz, isa_name, data, data_size);
    if (ret != 0) {
        mr_free(xvm);
        return MR_NULL;
    }
    return xvm;
}

mr_err_t mr_xvm_del(mr_xvm_t *xvm) {
    /* Check parameter */
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));

    /* Mark XVM as dying */
    mr_atomic_fetch_or(&xvm->flags, MR_XVM_FLAG_DYING);

    /* Delete object */
    mr_object_del((mr_object_t *)xvm);
    return 0;
}

static mr_err_t xvm_lock(mr_xvm_t *xvm) {
    mr_atomic_t flags, lock;

    /* Get current flags */
    flags = mr_atomic_load(&xvm->flags);

    do {
        /* Check if xvm is dying */
        if (flags & MR_XVM_FLAG_DYING) {
            return -MR_ENOENT;
        }

        /* Check if xvm is locked */
        if (flags & MR_XVM_FLAG_LOCK) {
            return -MR_EBUSY;
        }

        /* Try to mark xvm as locked */
        lock = flags | MR_XVM_FLAG_LOCK;
    } while (!mr_atomic_compare_exchange(&xvm->flags, &flags, lock));
    return 0;
}

static void xvm_unlock(mr_xvm_t *xvm) {
    /* Unmark xvm as locked */
    mr_atomic_fetch_and(&xvm->flags, ~MR_XVM_FLAG_LOCK);
}

mr_err_t mr_xvm_load(mr_xvm_t *xvm, const void *image, mr_size_t image_size,
                     mr_uint64_t entry) {
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));
    MR_ASSERT(xvm->isa->ops->load != MR_NULL);
    MR_ASSERT((image != MR_NULL) && (image_size > 0));
    MR_ASSERT(entry >= image_size);

    /* Lock */
    ret = xvm_lock(xvm);
    if (ret != 0) {
        return ret;
    }

    /* Load XVM binary */
    mr_memset(xvm->datamem.data, 0, xvm->datamem.size);
    xvm->bin.size = image_size;
    xvm->bin.image = image;
    xvm->bin.entry = entry;
    ret = xvm->isa->ops->load(xvm);
    if (ret != 0) {
        goto _exit;
    }

    /* Set XVM status */
    mr_atomic_store(&xvm->status, MR_XVM_STATUS_RUNNING);
    ret = 0;
_exit:
    /* Unlock */
    xvm_unlock(xvm);
    return ret;
}

mr_xvm_return_t mr_xvm_execute(mr_xvm_t *xvm, mr_xvm_event_t *event,
                               mr_uint32_t step) {
    mr_xvm_return_t exec_ret;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));
    MR_ASSERT(xvm->isa->ops->execute != MR_NULL);
    MR_ASSERT(event != MR_NULL);

    /* Lock */
    ret = xvm_lock(xvm);
    if (ret != 0) {
        /* Check if XVM is dying */
        if (ret == (-MR_ENOENT)) {
            return MR_XVM_EXIT;
        }
        event->none.step = 0;
        return MR_XVM_NONE;
    }

    /* Check if xvm is ready */
    if (mr_atomic_load(&xvm->status) != MR_XVM_STATUS_RUNNING) {
        event->none.step = 0;
        exec_ret = MR_XVM_NONE;
        goto _exit;
    }

    /* Execute XVM */
    exec_ret = xvm->isa->ops->execute(xvm, event, step);
    if (exec_ret == MR_XVM_NONE) {
        /* Continue */
    } else if (exec_ret == MR_XVM_SYSCALL) {
        /* Syscall */
        mr_atomic_store(&xvm->status, MR_XVM_STATUS_PAUSED);
    } else if (exec_ret == MR_XVM_EXCEPT) {
        /* Exception */
        mr_atomic_store(&xvm->status, MR_XVM_STATUS_EXCEPT);
    } else {
        /* End */
        mr_atomic_store(&xvm->status, MR_XVM_STATUS_ENDED);
    }
_exit:
    /* Unlock */
    xvm_unlock(xvm);
    return exec_ret;
}

mr_ssize_t mr_xvm_datamem_load(mr_xvm_t *xvm, mr_uint64_t addr, void *datamem,
                               mr_size_t size) {
    mr_bool_t writable;
    mr_ssize_t ret;
    void *memory;

    /* Check parameter */
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));
    MR_ASSERT(xvm->isa->ops->mem_map != MR_NULL);
    MR_ASSERT((datamem != MR_NULL) || (size == 0));

    /* Map data memory */
    ret = xvm->isa->ops->mem_map(xvm, addr, &memory, &writable);
    if (ret < 0) {
        return ret;
    }

    /* Copy data memory to user */
    size = MR_MIN(size, (mr_size_t)ret);
    mr_memcpy(datamem, memory, size);
    return (mr_ssize_t)size;
}

mr_ssize_t mr_xvm_datamem_store(mr_xvm_t *xvm, mr_uint64_t addr,
                                const void *datamem, mr_size_t size) {
    mr_bool_t writable;
    mr_ssize_t ret;
    void *memory;

    /* Check parameter */
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));
    MR_ASSERT(xvm->isa->ops->mem_map != MR_NULL);
    MR_ASSERT((datamem != MR_NULL) || (size == 0));

    /* Map data memory */
    ret = xvm->isa->ops->mem_map(xvm, addr, &memory, &writable);
    if (ret < 0) {
        return ret;
    }

    /* Check if the data memory region is writable */
    if (!writable) {
        return -MR_EPERM;
    }

    /* Copy data memory from user */
    size = MR_MIN(size, (mr_size_t)ret);
    mr_memcpy(memory, datamem, size);
    return (mr_ssize_t)size;
}

static mr_xvm_t *xvm_obj_release(mr_object_t *obj) {
    mr_xvm_t *xvm;

    /* Release XVM */
    xvm = MR_CONTAINER_OF(obj, mr_xvm_t, parent);

    /* Put XVM ISA */
    if (xvm->isa) {
        mr_xvm_isa_put(xvm->isa);
        xvm->isa = MR_NULL;
    }
    return xvm;
}

static void xvm_statik_release(mr_object_t *obj) {
    /* Release XVM */
    xvm_obj_release(obj);
}

static void xvm_dynamic_release(mr_object_t *obj) {
    mr_xvm_t *xvm;

    /* Release XVM */
    xvm = xvm_obj_release(obj);

    /* Free XVM */
    mr_free(xvm);
}

static mr_err_t xvm_isa_init(mr_xvm_isa_t *isa, const mr_clazz_t *clazz,
                             const char *name, const mr_xvm_ops_t *ops) {
    /* Init XVM ISA */
    isa->name = name;
    isa->next = MR_NULL;
    isa->ops = ops;

    /* Init object */
    return mr_object_init((mr_object_t *)isa, clazz);
}

mr_err_t mr_xvm_isa_init(mr_xvm_isa_t *isa, const char *name,
                         const mr_xvm_ops_t *ops) {
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(xvm_isa);

    /* Check parameter */
    MR_ASSERT(isa != MR_NULL);
    MR_ASSERT(name != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Init XVM ISA */
    return xvm_isa_init(isa, clazz, name, ops);
}

static mr_bool_t xvm_isa_is_registered(mr_xvm_isa_t *isa) {
    mr_xvm_isa_t *cur;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__xvm_lock);

    /* Check if XVM ISA already registered */
    for (cur = __xvm_isa_list; cur; cur = cur->next) {
        if (cur == isa) {
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__xvm_lock, mask);

            /* Already registered */
            return MR_TRUE;
        }
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xvm_lock, mask);
    return MR_FALSE;
}

mr_err_t mr_xvm_isa_del(mr_xvm_isa_t *isa) {
    /* Check parameter */
    MR_ASSERT((isa != MR_NULL) && MR_OBJECT_IS_INITED(isa));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(isa, xvm_isa));

    /* Check if XVM ISA already registered */
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
    mask = mr_spinlock_lock_irqsave(&__xvm_lock);

    /* Check if XVM ISA already registered */
    for (link = &__xvm_isa_list; *link; link = &(*link)->next) {
        existing = *link;

        /* Already registered */
        if (mr_strcmp(existing->name, isa->name) == 0) {
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__xvm_lock, mask);
            return -MR_EEXIST;
        }
    }

    /* Add to the list */
    isa->next = MR_NULL;
    *link = isa;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xvm_lock, mask);
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
    mask = mr_spinlock_lock_irqsave(&__xvm_lock);

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
        mr_spinlock_unlock_irqrestore(&__xvm_lock, mask);
        return -MR_ENOENT;
    }

    /* Remove from the list */
    *link = isa->next;
    isa->next = MR_NULL;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xvm_lock, mask);
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
    mask = mr_spinlock_lock_irqsave(&__xvm_lock);

    /* Find the XVM ISA */
    for (isa = __xvm_isa_list; isa; isa = isa->next) {
        if (mr_strcmp(isa->name, name) == 0) {
            /* Get XVM ISA */
            isa = mr_xvm_isa_get(isa);

            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__xvm_lock, mask);
            return isa;
        }
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__xvm_lock, mask);
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

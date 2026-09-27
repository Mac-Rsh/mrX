/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/modules/xvm/mr_xvm.h>
#if defined(MR_USE_MODULE_XVM)
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>

/* XVM clazzes(static & dynamic) */
static void xvm_statik_release(mr_object_t *obj);
static void xvm_dynamic_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(xvm, xvm_statik_release, xvm_dynamic_release, MR_NULL);

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
#if defined(MR_USE_MODULE_XVM_DEBUG)
    /* Init XVM debug */
    mr_xvm_debug_init(xvm);
#endif /* defined(MR_USE_MODULE_XVM_DEBUG) */

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

    /* Check allocation-size overflow */
    if (data_size > (MR_SIZE_MAX - sizeof(*xvm))) {
        return MR_NULL;
    }

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
        /* Check whether xvm is dying */
        if (flags & MR_XVM_FLAG_DYING) {
            return -MR_ENOENT;
        }

        /* Check whether xvm is locked */
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

#if defined(MR_USE_MODULE_XVM_DEBUG)
    /* Clear all breakpoint */
    mr_xvm_debug_break_clear(xvm);
#endif /* defined(MR_USE_MODULE_XVM_DEBUG) */

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
        /* Check whether XVM is dying */
        if (ret == (-MR_ENOENT)) {
            return MR_XVM_EXIT;
        }
        event->none.step = 0;
        return MR_XVM_NONE;
    }

    /* Check whether xvm is ready */
    if (mr_atomic_load(&xvm->status) != MR_XVM_STATUS_RUNNING) {
        event->none.step = 0;
        exec_ret = MR_XVM_NONE;
        goto _exit;
    }

#if defined(MR_USE_MODULE_XVM_DEBUG)
    /* Clamp the step when a probe is attached */
    if (mr_xvm_debug_probe_enter(xvm, &step, event)) {
        exec_ret = MR_XVM_NONE;
        goto _exit;
    }
#endif /* defined(MR_USE_MODULE_XVM_DEBUG) */

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

#if defined(MR_USE_MODULE_XVM_DEBUG)
    /* Hold debug stops and hide them from the runtime */
    exec_ret = mr_xvm_debug_probe_leave(xvm, exec_ret, event);
#endif /* defined(MR_USE_MODULE_XVM_DEBUG) */
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

    /* Check whether the data memory region is writable */
    if (!writable) {
        return -MR_EPERM;
    }

    /* Copy data memory from user */
    size = MR_MIN(size, (mr_size_t)ret);
    mr_memcpy(memory, datamem, size);
    return (mr_ssize_t)size;
}

mr_ssize_t mr_xvm_datamem_map(mr_xvm_t *xvm, mr_uint64_t addr, void **datamem,
                              mr_bool_t *writable) {
    /* Check parameter */
    MR_ASSERT((xvm != MR_NULL) && MR_OBJECT_IS_INITED(xvm));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(xvm, xvm));
    MR_ASSERT(xvm->isa->ops->mem_map != MR_NULL);
    MR_ASSERT(writable != MR_NULL);
    MR_ASSERT(datamem != MR_NULL);

    /* Map data memory */
    return xvm->isa->ops->mem_map(xvm, addr, datamem, writable);
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
#endif /* defined(MR_USE_MODULE_XVM) */

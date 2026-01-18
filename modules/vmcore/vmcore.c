/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/vmcore/mr_vmcore.h>
#if defined(MR_USE_VMCORE)
#include <mrX/libc/mr_malloc.h>

/* VMCore clazzes(static & dynamic) */
static void vmcore_statik_release(mr_object_t *obj);
static void vmcore_dynamic_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(vmcore, vmcore_statik_release, vmcore_dynamic_release);

static mr_err_t vmcore_env_init(mr_vmcore_t *vmcore) {
    /* Check if vmcore needs init */
    if ((!vmcore->clazz) || (!vmcore->clazz->init)) {
        return 0;
    }

    /* Init vmcore environment */
    return vmcore->clazz->init(vmcore);
}

static mr_err_t vmcore_init(mr_vmcore_t *vmcore, const mr_clazz_t *clazz,
                            const mr_vmcore_clazz_t *vclazz, void *memory,
                            mr_uint32_t memory_size) {
    mr_err_t ret;

    /* Init vmcore */
    vmcore->state = MR_VMCORE_STATE_INIT;
    mr_atomic_init(&vmcore->flags, 0);
    vmcore->memory_size = memory_size;
    vmcore->memory = memory;
    vmcore->image = MR_NULL;
    vmcore->clazz = vclazz;
    vmcore->error = 0;

    /* Init vmcore environment */
    ret = vmcore_env_init(vmcore);
    if (ret != 0) {
        return ret;
    }

    /* Init object */
    return mr_object_init((mr_object_t *)vmcore, clazz);
}

mr_err_t mr_vmcore_init(mr_vmcore_t *vmcore, const mr_vmcore_clazz_t *vclazz,
                        void *memory, mr_uint32_t memory_size) {
    const mr_clazzes_t *clazzes = MR_CLAZZES_FIND(vmcore);
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(clazzes);

    /* Check parameter */
    MR_ASSERT((vmcore != MR_NULL) && (!MR_OBJECT_IS_INITED(vmcore)));
    MR_ASSERT((memory != MR_NULL) && (memory_size > 0));
    MR_ASSERT(vclazz != MR_NULL);

    /* Init vmcore */
    return vmcore_init(vmcore, clazz, vclazz, memory, memory_size);
}

mr_vmcore_t *mr_vmcore_create(const mr_vmcore_clazz_t *vclazz,
                              mr_uint32_t memory_size) {
    const mr_clazzes_t *clazzes = MR_CLAZZES_FIND(vmcore);
    const mr_clazz_t *clazz = MR_CLAZZES_DYNAMIC(clazzes);
    mr_vmcore_t *vmcore;
    mr_uint8_t *stack;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(vclazz != MR_NULL);
    MR_ASSERT(memory_size > 0);

    /* Create vmcore */
    vmcore = mr_malloc(sizeof(mr_vmcore_t) + memory_size);
    if (!vmcore) {
        return MR_NULL;
    }

    /* Init vmcore */
    stack = ((mr_uint8_t *)vmcore + sizeof(mr_vmcore_t));
    ret = vmcore_init(vmcore, clazz, vclazz, stack, memory_size);
    if (ret != 0) {
        mr_free(vmcore);
        return MR_NULL;
    }
    return vmcore;
}

mr_err_t mr_vmcore_del(mr_vmcore_t *vmcore) {
    /* Check parameter */
    MR_ASSERT((vmcore != MR_NULL) && MR_OBJECT_IS_INITED(vmcore));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(vmcore, vmcore));

    /* Mark vmcore as dying */
    mr_atomic_fetch_or(&vmcore->flags, MR_VMCORE_FLAG_DYING);

    /* Delete object */
    mr_object_del((mr_object_t *)vmcore);
    return 0;
}

static mr_err_t vmcore_lock(mr_vmcore_t *vmcore) {
    mr_atomic_t flags, lock;

    /* Get current flags */
    flags = mr_atomic_load(&vmcore->flags);

    do {
        /* Check if vmcore is dying */
        if (flags & MR_VMCORE_FLAG_DYING) {
            return -MR_ENOENT;
        }

        /* Check if vmcore is locked */
        if (flags & MR_VMCORE_FLAG_LOCK) {
            return -MR_EBUSY;
        }

        /* Try to mark vmcore as locked */
        lock = flags | MR_VMCORE_FLAG_LOCK;
    } while (!mr_atomic_compare_exchange(&vmcore->flags, &flags, lock));
    return 0;
}

static void vmcore_unlock(mr_vmcore_t *vmcore) {
    /* Unmark vmcore as locked */
    mr_atomic_fetch_add(&vmcore->flags, ~MR_VMCORE_FLAG_LOCK);
}

mr_err_t mr_vmcore_load(mr_vmcore_t *vmcore, const void *image,
                        mr_uint32_t image_size, mr_uint32_t load_addr) {
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((vmcore != MR_NULL) && MR_OBJECT_IS_INITED(vmcore));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(vmcore, vmcore));
    MR_ASSERT((image != MR_NULL) || (image_size == 0));

    /* Check if image is too large */
    if (image_size >= vmcore->memory_size) {
        return -MR_ENOMEM;
    }

    /* Lock vmcore */
    ret = vmcore_lock(vmcore);
    if (ret != 0) {
        return ret;
    }

    /* Check if vmcore environment is clean */
    if (vmcore->state != MR_VMCORE_STATE_INIT) {
        /* Reinit vmcore environment */
        ret = vmcore_env_init(vmcore);
        if (ret != 0) {
            goto _exit;
        }
    }

    /* Load image */
    vmcore->image = image;
    vmcore->image_size = image_size;
    vmcore->load_addr = load_addr;
    ret = vmcore->clazz->load(vmcore);
    if (ret != 0) {
        goto _exit;
    }

    /* Set vmcore state */
    vmcore->state = MR_VMCORE_STATE_PAUSED;
    ret = 0;
_exit:
    /* Unlock vmcore */
    vmcore_unlock(vmcore);
    return ret;
}

mr_err_t mr_vmcore_execute(mr_vmcore_t *vmcore, mr_uint32_t step) {
    /* Check parameter */
    MR_ASSERT((vmcore != MR_NULL) && MR_OBJECT_IS_INITED(vmcore));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(vmcore, vmcore));

    /* Execute vmcore */
    return vmcore->clazz->execute(vmcore, step);
}

static void vmcore_env_deinit(mr_vmcore_t *vmcore) {
    /* Check if vmcore needs deinit */
    if ((!vmcore->clazz) || (!vmcore->clazz->deinit)) {
        return;
    }

    /* Deinit vmcore */
    vmcore->clazz->deinit(vmcore);
}

static mr_vmcore_t *vmcore_obj_release(mr_object_t *obj) {
    mr_vmcore_t *vmcore;

    /* Get vmcore */
    vmcore = MR_CONTAINER_OF(obj, mr_vmcore_t, parent);

    /* Remove vmcore */
    vmcore_env_deinit(vmcore);
    return vmcore;
}

static void vmcore_statik_release(mr_object_t *obj) {
    /* Release vmcore */
    vmcore_obj_release(obj);
}

static void vmcore_dynamic_release(mr_object_t *obj) {
    mr_vmcore_t *vmcore;

    /* Release vmcore */
    vmcore = vmcore_obj_release(obj);

    /* Free vmcore */
    mr_free(vmcore);
}
#endif /* defined(MR_USE_VMCORE) */

/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/vmc/mr_vmc.h>
#if defined(MR_USE_VMC)
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>

/* VMC clazzes(static & dynamic) */
static void vmc_statik_release(mr_object_t *obj);
static void vmc_dynamic_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(vmc, vmc_statik_release, vmc_dynamic_release);

static mr_err_t vmc_init(mr_vmc_t *vmc, const mr_clazz_t *clazz,
                         const mr_vmc_clazz_t *vclazz, void *memory,
                         mr_uint32_t memory_size) {
    /* Init VMC */
    vmc->state = MR_VMC_STATE_INIT;
    mr_atomic_init(&vmc->flags, 0);
    vmc->prop.memory = (mr_uint8_t *)memory;
    vmc->prop.memory_size = memory_size;
    vmc->prop.image = MR_NULL;
    vmc->prop.image_size = 0;
    vmc->prop.storage_addr = 0x0;
    vmc->prop.entry_addr = 0x0;
    vmc->prop.vm_regs_size = 0;
    vmc->prop.vm_memory_size = 0;
    vmc->clazz = vclazz;

    /* Init object */
    return mr_object_init((mr_object_t *)vmc, clazz);
}

mr_err_t mr_vmc_init(mr_vmc_t *vmc, const mr_vmc_clazz_t *vclazz, void *memory,
                     mr_uint32_t memory_size) {
    const mr_clazzes_t *clazzes = MR_CLAZZES_FIND(vmc);
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(clazzes);

    /* Check parameter */
    MR_ASSERT((vmc != MR_NULL) && (!MR_OBJECT_IS_INITED(vmc)));
    MR_ASSERT((memory != MR_NULL) && (memory_size > 0));
    MR_ASSERT(vclazz != MR_NULL);

    /* Init VMC */
    return vmc_init(vmc, clazz, vclazz, memory, memory_size);
}

mr_vmc_t *mr_vmc_create(const mr_vmc_clazz_t *vclazz, mr_uint32_t memory_size) {
    const mr_clazzes_t *clazzes = MR_CLAZZES_FIND(vmc);
    const mr_clazz_t *clazz = MR_CLAZZES_DYNAMIC(clazzes);
    mr_uint8_t *memory;
    mr_vmc_t *vmc;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(vclazz != MR_NULL);
    MR_ASSERT(memory_size > 0);

    /* Create VMC */
    vmc = mr_malloc(sizeof(mr_vmc_t) + memory_size);
    if (!vmc) {
        return MR_NULL;
    }

    /* Init VMC */
    memory = ((mr_uint8_t *)vmc + sizeof(mr_vmc_t));
    ret = vmc_init(vmc, clazz, vclazz, memory, memory_size);
    if (ret != 0) {
        mr_free(vmc);
        return MR_NULL;
    }
    return vmc;
}

mr_err_t mr_vmc_del(mr_vmc_t *vmc) {
    /* Check parameter */
    MR_ASSERT((vmc != MR_NULL) && MR_OBJECT_IS_INITED(vmc));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(vmc, vmc));

    /* Mark VMC as dying */
    mr_atomic_fetch_or(&vmc->flags, MR_VMC_FLAG_DYING);

    /* Delete object */
    mr_object_del((mr_object_t *)vmc);
    return 0;
}

static mr_err_t vmc_lock(mr_vmc_t *vmc) {
    mr_atomic_t flags, lock;

    /* Get current flags */
    flags = mr_atomic_load(&vmc->flags);

    do {
        /* Check if VMC is dying */
        if (flags & MR_VMC_FLAG_DYING) {
            return -MR_ENOENT;
        }

        /* Check if VMC is locked */
        if (flags & MR_VMC_FLAG_LOCK) {
            return -MR_EBUSY;
        }

        /* Try to mark VMC as locked */
        lock = flags | MR_VMC_FLAG_LOCK;
    } while (!mr_atomic_compare_exchange(&vmc->flags, &flags, lock));
    return 0;
}

static void vmc_unlock(mr_vmc_t *vmc) {
    /* Unmark VMC as locked */
    mr_atomic_fetch_and(&vmc->flags, ~MR_VMC_FLAG_LOCK);
}

mr_err_t mr_vmc_load(mr_vmc_t *vmc, const void *image, mr_uint32_t image_size,
                     mr_uint32_t storage_addr, mr_uint32_t entry_addr) {
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((vmc != MR_NULL) && MR_OBJECT_IS_INITED(vmc));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(vmc, vmc));
    MR_ASSERT((image != MR_NULL) || (image_size == 0));

    /* Lock VMC */
    ret = vmc_lock(vmc);
    if (ret != 0) {
        return ret;
    }

    /* Load VMC program image */
    vmc->prop.image = (const mr_uint8_t *)image;
    vmc->prop.image_size = image_size;
    vmc->prop.storage_addr = storage_addr;
    vmc->prop.entry_addr = entry_addr;
    ret = vmc->clazz->load(vmc, &vmc->prop);
    if (ret != 0) {
        goto _exit;
    }

    /* Set VMC state */
    vmc->state = MR_VMC_STATE_RUNNING;
    ret = 0;
_exit:
    /* Unlock VMC */
    vmc_unlock(vmc);
    return ret;
}

mr_vmc_return_t mr_vmc_execute(mr_vmc_t *vmc, mr_vmc_event_t *event,
                               mr_uint32_t instr_step) {
    mr_vmc_return_t vret;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((vmc != MR_NULL) && MR_OBJECT_IS_INITED(vmc));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(vmc, vmc));
    MR_ASSERT(event != MR_NULL);

    /* Reduce unnecessary lock operations */
    if (vmc->state != MR_VMC_STATE_RUNNING) {
        return MR_VMC_NONE;
    }

    /* Lock VMC */
    ret = vmc_lock(vmc);
    if (ret != 0) {
        /* Check if VMC is dying */
        if (ret == (-MR_ENOENT)) {
            return MR_VMC_END;
        }
        return MR_VMC_NONE;
    }

    /* Check if VMC is ready */
    if (mr_atomic_load(&vmc->state) != MR_VMC_STATE_RUNNING) {
        vret = MR_VMC_NONE;
        goto _exit;
    }

    /* Execute VMC */
    vret = vmc->clazz->exec(vmc, &vmc->prop, event, instr_step);
    if (vret == MR_VMC_NONE) {
        /* Continue */
    } else if (vret == MR_VMC_SYSCALL) {
        /* Wait for syscall */
        mr_atomic_store(&vmc->state, MR_VMC_STATE_PAUSED);
    } else if (vret == MR_VMC_EXCEPT) {
        /* Wait for exception */
        mr_atomic_store(&vmc->state, MR_VMC_STATE_ERROR);
    } else {
        /* Mark VMC as ended */
        mr_atomic_store(&vmc->state, MR_VMC_STATE_ENDED);
    }
_exit:
    /* Unlock VMC */
    vmc_unlock(vmc);
    return vret;
}

mr_uint32_t mr_vmc_memcpy_from(mr_vmc_t *vmc, mr_uint32_t vaddr, void *memory,
                               mr_uint32_t size) {
    mr_uint32_t off;

    /* Check parameter */
    MR_ASSERT((vmc != MR_NULL) && MR_OBJECT_IS_INITED(vmc));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(vmc, vmc));
    MR_ASSERT((memory != MR_NULL) || (size == 0));

    /* Image */
    off = vaddr - vmc->prop.entry_addr;
    if (off < vmc->prop.image_size) {
        size = MR_MIN(size, vmc->prop.image_size - off);
        mr_memcpy(memory, &vmc->prop.image[off], size);
        return size;
    }

    /* Memory */
    off = vaddr - vmc->prop.storage_addr;
    if (off < vmc->prop.vm_memory_size) {
        size = MR_MIN(size, vmc->prop.vm_memory_size - off);
        mr_memcpy(memory, &vmc->prop.vm_memory[off], size);
        return size;
    }
    return 0;
}

mr_uint32_t mr_vmc_memcpy_to(mr_vmc_t *vmc, mr_uint32_t vaddr,
                             const void *memory, mr_uint32_t size) {
    mr_uint32_t off;

    /* Check parameter */
    MR_ASSERT((vmc != MR_NULL) && MR_OBJECT_IS_INITED(vmc));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(vmc, vmc));
    MR_ASSERT((memory != MR_NULL) || (size == 0));

    /* Image */
    off = vaddr - vmc->prop.entry_addr;
    if (off < vmc->prop.image_size) {
        /* Read-only */
        return 0;
    }

    /* Memory */
    off = vaddr - vmc->prop.storage_addr;
    if (off < vmc->prop.vm_memory_size) {
        size = MR_MIN(size, vmc->prop.vm_memory_size - off);
        mr_memcpy(&vmc->prop.vm_memory[off], memory, size);
        return size;
    }
    return 0;
}

mr_uint32_t mr_vmc_memmap(mr_vmc_t *vmc, mr_uint32_t vaddr,
                          mr_uint32_t **memory) {
    mr_uint32_t off;

    /* Check parameter */
    MR_ASSERT((vmc != MR_NULL) && MR_OBJECT_IS_INITED(vmc));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(vmc, vmc));
    MR_ASSERT(memory != MR_NULL);

    /* Image */
    off = vaddr - vmc->prop.entry_addr;
    if (off < vmc->prop.image_size) {
        *memory = (mr_uint32_t *)&vmc->prop.image[off];
        return vmc->prop.image_size - off;
    }

    /* Memory */
    off = vaddr - vmc->prop.storage_addr;
    if (off < vmc->prop.vm_memory_size) {
        *memory = (mr_uint32_t *)&vmc->prop.vm_memory[off];
        return vmc->prop.vm_memory_size - off;
    }
    return 0;
}

static mr_vmc_t *vmc_obj_release(mr_object_t *obj) {
    mr_vmc_t *vmc;

    /* Get VMC */
    vmc = MR_CONTAINER_OF(obj, mr_vmc_t, parent);

    /* Remove VMC */
    return vmc;
}

static void vmc_statik_release(mr_object_t *obj) {
    /* Release VMC */
    vmc_obj_release(obj);
}

static void vmc_dynamic_release(mr_object_t *obj) {
    mr_vmc_t *vmc;

    /* Release VMC */
    vmc = vmc_obj_release(obj);

    /* Free VMC */
    mr_free(vmc);
}
#endif /* defined(MR_USE_VMC) */

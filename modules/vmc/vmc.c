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
    vmc->env.memory = memory;
    vmc->env.memory_size = memory_size;
    vmc->env.image = MR_NULL;
    vmc->env.image_size = 0;
    vmc->env.storage_addr = 0x0;
    vmc->env.entry_addr = 0x0;
    vmc->clazz = vclazz;
    vmc->error = 0;

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
    vmc->env.image = image;
    vmc->env.image_size = image_size;
    vmc->env.storage_addr = storage_addr;
    vmc->env.entry_addr = entry_addr;
    ret = vmc->clazz->load(vmc, &vmc->env);
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
    const mr_atomic_t flags_clear = MR_ATOMIC_INIT(~MR_VMC_FLAG_REQ_PAUSE);
    mr_vmc_return_t vret;
    mr_uint32_t flags;
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
            /* Mark VMC as ended */
            vmc->state = MR_VMC_STATE_ENDED;
            return MR_VMC_END;
        }
        return MR_VMC_NONE;
    }

    /* Check if VMC is running */
    if (vmc->state != MR_VMC_STATE_RUNNING) {
        vret = MR_VMC_NONE;
        goto _exit;
    }

    /* Execute VMC */
    vret = vmc->clazz->exec(vmc, &vmc->env, event, instr_step);
    if (vret == MR_VMC_NONE) {
        /* Check if VMC has any requests */
        flags = (mr_uint32_t)mr_atomic_fetch_and(&vmc->flags, flags_clear);
        if (flags & MR_VMC_FLAG_REQ_PAUSE) {
            vmc->state = MR_VMC_STATE_PAUSED;
        }
    } else if (vret == MR_VMC_SYSCALL) {
        vmc->state = MR_VMC_STATE_PAUSED;
    } else if (vret == MR_VMC_EXCEPT) {
        vmc->state = MR_VMC_STATE_ERROR;
    } else if (vret == MR_VMC_END) {
        vmc->state = MR_VMC_STATE_ENDED;
    }
_exit:
    /* Unlock VMC */
    vmc_unlock(vmc);
    return vret;
}

mr_err_t mr_vmc_pause(mr_vmc_t *vmc) {
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((vmc != MR_NULL) && MR_OBJECT_IS_INITED(vmc));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(vmc, vmc));

    /* Lock VMC */
    ret = vmc_lock(vmc);
    if (ret != 0) {
        /* Check if VMC is locked */
        if (ret == (-MR_EBUSY)) {
            /* Request VMC to pause */
            mr_atomic_fetch_or(&vmc->flags, MR_VMC_FLAG_REQ_PAUSE);
            return 0;
        }
        return ret;
    }

    /* Check if VMC can be paused */
    if (vmc->state != MR_VMC_STATE_RUNNING) {
        ret = -MR_EPERM;
        goto _exit;
    }

    /* Set VMC state */
    vmc->state = MR_VMC_STATE_PAUSED;
    ret = 0;
_exit:
    /* Unlock VMC */
    vmc_unlock(vmc);
    return ret;
}

mr_err_t mr_vmc_resume(mr_vmc_t *vmc) {
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((vmc != MR_NULL) && MR_OBJECT_IS_INITED(vmc));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(vmc, vmc));

    /* Lock VMC */
    ret = vmc_lock(vmc);
    if (ret != 0) {
        return ret;
    }

    /* Check if VMC can be resumed */
    if ((vmc->state != MR_VMC_STATE_PAUSED)
        && (vmc->state != MR_VMC_STATE_ERROR)) {
        ret = -MR_EPERM;
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

mr_err_t mr_vmc_debug(mr_vmc_t *vmc, mr_uint32_t **reg_addr,
                      mr_uint32_t *reg_size, mr_uint32_t **memory_addr,
                      mr_uint32_t *memory_size) {
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((vmc != MR_NULL) && MR_OBJECT_IS_INITED(vmc));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(vmc, vmc));
    MR_ASSERT((reg_addr != MR_NULL) && (reg_size != MR_NULL));
    MR_ASSERT((memory_addr != MR_NULL) && (memory_size != MR_NULL));

    /* Lock VMC */
    ret = vmc_lock(vmc);
    if (ret != 0) {
        return ret;
    }

    /* Get VMC debug information */
    ret = vmc->clazz->debug(vmc, &vmc->env, reg_addr, reg_size, memory_addr,
                            memory_size);

    /* Unlock VMC */
    vmc_unlock(vmc);
    return ret;
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

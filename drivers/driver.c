/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <drivers/mr_driver.h>
#if defined(MR_USE_DRIVER)
#include <mr-X/mr_initcall.h>
#include <libc/mr_malloc.h>

/* Driver clazz(dynamic & static) */
static void driver_release_dyn(mr_object_t *obj);
static void driver_release(mr_object_t *obj);
MR_CLAZZ_DYNAMIC_DEFINE(driver, driver_release_dyn);
MR_CLAZZ_DEFINE(driver, driver_release);

/* Driver start/defer/end sections */
MR_DRIVER_INSTALL(start, 0, MR_NULL, MR_NULL, MR_NULL, MR_NULL, "0.end");
MR_DRIVER_INSTALL(defer, 0, MR_NULL, MR_NULL, MR_NULL, MR_NULL, "8.end");
MR_DRIVER_INSTALL(end, 0, MR_NULL, MR_NULL, MR_NULL, MR_NULL, "9.end");

static mr_err_t driver_init(mr_driver_t *drv, const mr_clazz_t *clazz,
                            mr_uint16_t type, const mr_driver_dops_t *dops,
                            mr_ptr_t data, mr_ptr_t priv, mr_ptr_t ops) {
    /* Init driver */
    mr_atomic_init(&drv->flags, 0);
    drv->type = type;
    drv->state = 0;
    drv->dops = dops;
    drv->data = data;
    drv->priv = priv;
    drv->ops = ops;

    /* Init object */
    return mr_object_init((mr_object_t *)drv, clazz);
}

mr_err_t mr_driver_init(mr_driver_t *drv, mr_uint16_t type,
                        const mr_driver_dops_t *dops, void *data, void *priv,
                        void *ops) {
    const mr_clazz_t *clazz = MR_CLAZZ_FIND(driver);

    /* Check parameter */
    MR_ASSERT((drv != MR_NULL) && (!MR_OBJECT_IS_INITED(drv)));
    MR_ASSERT((type != MR_DRIVER_TYPE_NONE) && (dops != MR_NULL));

    /* Init driver */
    return driver_init(drv, clazz, type, dops, data, priv, ops);
}

mr_driver_t *mr_driver_create(mr_uint16_t type, const mr_driver_dops_t *dops,
                              void *data, void *priv, void *ops) {
    const mr_clazz_t *clazz = MR_CLAZZ_DYNAMIC_FIND(driver);
    mr_driver_t *drv;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((type != MR_DRIVER_TYPE_NONE) && (dops != MR_NULL));

    /* Create driver */
    drv = mr_malloc(sizeof(mr_driver_t));
    if (!drv) {
        return MR_NULL;
    }

    /* Init driver */
    ret = driver_init(drv, clazz, type, dops, data, priv, ops);
    if (ret != 0) {
        mr_free(drv);
        return MR_NULL;
    }
    return drv;
}

mr_err_t mr_driver_del(mr_driver_t *drv) {
    /* Check parameter */
    MR_ASSERT((drv != MR_NULL) && MR_OBJECT_IS_INITED(drv));
    MR_ASSERT(MR_OBJECT_CLAZZ_IS_NAME(drv, driver));

    /* Mark driver as dying */
    mr_atomic_fetch_or(&drv->flags, MR_DRIVER_FLAG_DYING);

    /* Delete object */
    mr_object_del((mr_object_t *)drv);
    return 0;
}

static mr_err_t driver_lock(mr_driver_t *drv) {
    mr_atomic_t flags;

    /* Get current flags */
    flags = mr_atomic_load(&drv->flags);

    do {
        /* Check if driver is dying */
        if (flags & MR_DRIVER_FLAG_DYING) {
            return -MR_ENOENT;
        }

        /* Check if driver is locked */
        if (flags & MR_DRIVER_FLAG_LOCK) {
            return -MR_EBUSY;
        }

        /* Try to mark driver as locked */
    } while (!mr_atomic_compare_exchange(&drv->flags, &flags,
                                         flags | MR_DRIVER_FLAG_LOCK));
    return 0;
}

static void driver_unlock(mr_driver_t *drv) {
    /* Unmark driver as locked */
    mr_atomic_fetch_and(&drv->flags, ~MR_DRIVER_FLAG_LOCK);
}

mr_err_t mr_driver_setup(mr_driver_t *drv) {
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((drv != MR_NULL) && MR_OBJECT_IS_INITED(drv));
    MR_ASSERT(MR_OBJECT_CLAZZ_IS_NAME(drv, driver));

    /* Lock driver */
    ret = driver_lock(drv);
    if (ret != 0) {
        return ret;
    }

    /* Check if driver is set up */
    if (drv->flags & MR_DRIVER_FLAG_SETUP) {
        ret = 0;
        goto _exit;
    }

    /* Check if driver is need init */
    if ((!drv->dops) || (!drv->dops->init)) {
        goto _set;
    }

    /* Setup driver */
    ret = drv->dops->init(drv);
    if (ret == 0) {
_set:
        /* Mark driver as setup */
        mr_atomic_fetch_or(&drv->flags, MR_DRIVER_FLAG_SETUP);
    }

    /* Set driver state */
    mr_driver_state_set(drv, ret);
_exit:
    /* Unlock driver */
    driver_unlock(drv);
    return ret;
}

mr_err_t mr_driver_shutdown(mr_driver_t *drv) {
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((drv != MR_NULL) && MR_OBJECT_IS_INITED(drv));
    MR_ASSERT(MR_OBJECT_CLAZZ_IS_NAME(drv, driver));

    /* Lock driver */
    ret = driver_lock(drv);
    if (ret != 0) {
        return ret;
    }

    /* Check if driver is set up */
    if (!(drv->flags & MR_DRIVER_FLAG_SETUP)) {
        ret = 0;
        goto _exit;
    }

    /* Check if driver is need deinit */
    if ((!drv->dops) || (!drv->dops->deinit)) {
        goto _unset;
    }

    /* Shutdown driver */
    ret = drv->dops->deinit(drv);
    if (ret == 0) {
_unset:
        /* Mark driver as shutdown */
        mr_atomic_fetch_and(&drv->flags, ~MR_DRIVER_FLAG_SETUP);
    }

    /* Set driver state */
    mr_driver_state_set(drv, ret);
_exit:
    /* Unlock driver */
    driver_unlock(drv);
    return ret;
}

void mr_driver_state_set(mr_driver_t *drv, mr_err_t errcode) {
    /* Check parameter */
    MR_ASSERT((drv != MR_NULL) && MR_OBJECT_IS_INITED(drv));
    MR_ASSERT(MR_OBJECT_CLAZZ_IS_NAME(drv, driver));
    MR_ASSERT(errcode <= 0);

    /* Limit error code(0 ~ 255) */
    if (errcode < 0) {
        errcode = -errcode;
    }
    if (errcode > (mr_err_t)MR_UINT8_MAX) {
        errcode = MR_UINT8_MAX;
    }

    /* Set driver state */
    drv->state = (mr_uint8_t)errcode;
}

mr_err_t mr_driver_state(mr_driver_t *drv) {
    /* Check parameter */
    MR_ASSERT((drv != MR_NULL) && MR_OBJECT_IS_INITED(drv));
    MR_ASSERT(MR_OBJECT_CLAZZ_IS_NAME(drv, driver));

    /* Get driver state */
    return -((mr_err_t)drv->state);
}

mr_driver_t *mr_driver_find(const char *name) {
    const mr_driver_entry_t *start = &__mr_driver_entry_start;
    const mr_driver_entry_t *end = &__mr_driver_entry_end;
    const mr_driver_entry_t *entry;

    for (entry = start + 1; entry < end; entry++) {
        MR_ASSERT(entry->name != MR_NULL);

        /* Match driver */
        if (mr_strcmp(entry->name, name) != 0) {
            continue;
        }

        /* Found */
        return entry->driver;
    }

    /* Not found */
    return MR_NULL;
}

static void driver_del(mr_driver_t *drv) {
    /* Check if driver is need deinit */
    if ((!drv->dops) || (!drv->dops->deinit)) {
        return;
    }

    /* Deinit driver */
    drv->dops->deinit(drv);
}

static mr_driver_t *driver_obj_release(mr_object_t *obj) {
    mr_driver_t *drv;

    /* Get driver */
    drv = MR_CONTAINER_OF(obj, mr_driver_t, parent);

    /* Delete driver */
    driver_del(drv);
    return drv;
}

static void driver_release(mr_object_t *obj) {
    /* Release driver */
    driver_obj_release(obj);
}

static void driver_release_dyn(mr_object_t *obj) {
    mr_driver_t *drv;

    /* Release driver */
    drv = driver_obj_release(obj);

    /* Free driver */
    mr_free(drv);
}

static void mr_drivers_init(void) {
    const mr_driver_entry_t *start = &__mr_driver_entry_start;
    const mr_driver_entry_t *end = &__mr_driver_entry_defer;
    const mr_driver_entry_t *entry;

    for (entry = start + 1; entry < end; entry++) {
        MR_ASSERT(entry->driver != MR_NULL);

        /* Setup driver */
        mr_driver_setup(entry->driver);
    }
}
MR_INIT_EXPORT(mr_drivers_init, MR_INIT_LEVEL_DRIVER);
#endif /* defined(MR_USE_DRIVER) */

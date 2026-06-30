/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/mr_driver.h>
#if defined(MR_USE_DRIVER)
#include <mrX/kernel/mr_initcall.h>
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>

/* Driver clazzes(static & dynamic) */
static void driver_dynamic_release(mr_object_t *obj);
static void driver_statik_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(driver, driver_statik_release, driver_dynamic_release,
                  MR_NULL);

/* Driver start/defer/end sections */
MR_DRIVER_INSTALL(start, MR_NULL, 0, MR_NULL, MR_NULL, MR_NULL, "0.end");
MR_DRIVER_INSTALL(defer, MR_NULL, 0, MR_NULL, MR_NULL, MR_NULL, "8.end");
MR_DRIVER_INSTALL(end, MR_NULL, 0, MR_NULL, MR_NULL, MR_NULL, "9.end");

static mr_err_t driver_init(mr_driver_t *drv, const mr_clazz_t *clazz,
                            const mr_driver_clazz_t *dclazz, mr_uint16_t type,
                            mr_ptr_t data, mr_ptr_t priv, mr_ptr_t ops) {
    /* Init driver */
    mr_atomic_init(&drv->flags, 0);
    drv->clazz = dclazz;
    drv->type = type;
    drv->state = 0;
    drv->data = data;
    drv->priv = priv;
    drv->ops = ops;

    /* Init object */
    return mr_object_init((mr_object_t *)drv, clazz);
}

mr_err_t mr_driver_init(mr_driver_t *drv, const mr_driver_clazz_t *dclazz,
                        mr_uint16_t type, void *data, void *priv, void *ops) {
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(driver);

    /* Check parameter */
    MR_ASSERT((drv != MR_NULL) && (!MR_OBJECT_IS_INITED(drv)));
    MR_ASSERT((type != MR_DRIVER_TYPE_NONE) && (dclazz != MR_NULL));

    /* Init driver */
    return driver_init(drv, clazz, dclazz, type, data, priv, ops);
}

mr_driver_t *mr_driver_create(const mr_driver_clazz_t *dclazz, mr_uint16_t type,
                              void *data, void *priv, void *ops) {
    const mr_clazz_t *clazz = MR_CLAZZES_DYNAMIC(driver);
    mr_driver_t *drv;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((type != MR_DRIVER_TYPE_NONE) && (dclazz != MR_NULL));

    /* Create driver */
    drv = mr_malloc(sizeof(mr_driver_t));
    if (!drv) {
        return MR_NULL;
    }

    /* Init driver */
    ret = driver_init(drv, clazz, dclazz, type, data, priv, ops);
    if (ret != 0) {
        mr_free(drv);
        return MR_NULL;
    }
    return drv;
}

mr_err_t mr_driver_del(mr_driver_t *drv) {
    /* Check parameter */
    MR_ASSERT((drv != MR_NULL) && MR_OBJECT_IS_INITED(drv));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(drv, driver));

    /* Mark driver as dying */
    mr_atomic_fetch_or(&drv->flags, MR_DRIVER_FLAG_DYING);

    /* Delete object */
    mr_object_del((mr_object_t *)drv);
    return 0;
}

static mr_err_t driver_lock(mr_driver_t *drv) {
    mr_atomic_t flags, lock;

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
        lock = flags | MR_DRIVER_FLAG_LOCK;
    } while (!mr_atomic_compare_exchange(&drv->flags, &flags, lock));
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
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(drv, driver));

    /* Lock driver */
    ret = driver_lock(drv);
    if (ret != 0) {
        return ret;
    }

    /* Check if driver is setup */
    if (mr_atomic_load(&drv->flags) & MR_DRIVER_FLAG_SETUP) {
        ret = 0;
        goto _exit;
    }

    /* Check if driver need init */
    if ((!drv->clazz) || (!drv->clazz->init)) {
        ret = 0;
        goto _set;
    }

    /* Setup driver */
    ret = drv->clazz->init(drv);
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

mr_bool_t mr_driver_is_ready(mr_driver_t *drv) {
    /* Check parameter */
    if ((!drv) || (!MR_OBJECT_IS_INITED(drv))) {
        return MR_FALSE;
    }
    if (!MR_OBJECT_IS_CLAZZES(drv, driver)) {
        return MR_FALSE;
    }

    /* Check if driver is setup */
    if (!(mr_atomic_load(&drv->flags) & MR_DRIVER_FLAG_SETUP)) {
        return MR_FALSE;
    }

    /* State is ready */
    return drv->state == 0;
}

mr_err_t mr_driver_shutdown(mr_driver_t *drv) {
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((drv != MR_NULL) && MR_OBJECT_IS_INITED(drv));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(drv, driver));

    /* Lock driver */
    ret = driver_lock(drv);
    if (ret != 0) {
        return ret;
    }

    /* Check if driver is setup */
    if (!(mr_atomic_load(&drv->flags) & MR_DRIVER_FLAG_SETUP)) {
        ret = 0;
        goto _exit;
    }

    /* Check if driver need deinit */
    if ((!drv->clazz) || (!drv->clazz->deinit)) {
        ret = 0;
        goto _unset;
    }

    /* Shutdown driver */
    ret = drv->clazz->deinit(drv);
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

mr_driver_t *mr_driver_find(const char *name) {
    const mr_driver_entry_t *start = &__mr_driver_entry_start;
    const mr_driver_entry_t *end = &__mr_driver_entry_end;
    const mr_driver_entry_t *entry;

    /* Check parameter */
    MR_ASSERT((name != MR_NULL) && (name[0] != '\0'));

    /* Find the driver */
    for (entry = start + 1; entry < end; entry++) {
        if (mr_strcmp(entry->name, name) == 0) {
            /* Get driver */
            return mr_driver_get(entry->drv);
        }
    }

    /* Not found */
    return MR_NULL;
}

static void driver_del(mr_driver_t *drv) {
    /* Check if driver is setup */
    if (!(mr_atomic_load(&drv->flags) & MR_DRIVER_FLAG_SETUP)) {
        return;
    }

    /* Check if driver need deinit */
    if ((!drv->clazz) || (!drv->clazz->deinit)) {
        return;
    }

    /* Deinit driver */
    drv->clazz->deinit(drv);
}

static mr_driver_t *driver_obj_release(mr_object_t *obj) {
    mr_driver_t *drv;

    /* Get driver */
    drv = MR_CONTAINER_OF(obj, mr_driver_t, parent);

    /* Delete driver */
    driver_del(drv);
    return drv;
}

static void driver_statik_release(mr_object_t *obj) {
    /* Release driver */
    driver_obj_release(obj);
}

static void driver_dynamic_release(mr_object_t *obj) {
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
        MR_ASSERT(entry->drv != MR_NULL);

        /* Setup driver */
        mr_driver_setup(entry->drv);
    }
}
MR_INIT_EXPORT(mr_drivers_init, MR_INIT_LEVEL_DRIVER);
#endif /* defined(MR_USE_DRIVER) */

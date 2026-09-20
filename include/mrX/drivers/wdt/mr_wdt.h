/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-17        MacRsh        First version
 */

#ifndef __MR_DRIVER_WDT_H__
#define __MR_DRIVER_WDT_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_DRIVER_WDT)
#include <mrX/drivers/mr_driver.h>
#endif /* defined(MR_USE_DRIVER_WDT) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Drivers-WDT
 * @{
 */

#if defined(MR_USE_DRIVER_WDT)
/* WDT wait event definition */
#define MR_WDT_WAIT_PRETIMEOUT          (1U << 0)
#define MR_WDT_WAIT_TIMEOUT             (1U << 1)

/* WDT configuration type */
typedef struct mr_wdt_config {
    mr_uint64_t feed_min;
    mr_uint64_t feed_max;
    mr_uint64_t pretimeout;
} mr_wdt_config_t;

/* WDT data type */
MR_DRIVER_CLAZZES_EXPORT(wdt);
typedef struct mr_wdt_data {
    mr_waitable_t event;
} mr_wdt_data_t;

/* WDT operations type */
typedef struct mr_wdt_ops {
    mr_err_t (*configure)(const mr_driver_t *drv,
                          const mr_wdt_config_t *config);
    mr_err_t (*configuration)(const mr_driver_t *drv, mr_wdt_config_t *config);
    mr_err_t (*start)(const mr_driver_t *drv);
    mr_err_t (*stop)(const mr_driver_t *drv);
    mr_err_t (*feed)(const mr_driver_t *drv);
    mr_err_t (*is_busy)(const mr_driver_t *drv, mr_bool_t *busy);
    mr_err_t (*event_control)(const mr_driver_t *drv, mr_uint32_t events,
                              mr_bool_t enable);
} mr_wdt_ops_t;

/**
 * @brief This macro function defines a WDT driver.
 *
 * @param _name The WDT driver name.
 * @param _data The WDT driver data.
 * @param _priv The WDT private data.
 * @param _ops The WDT operations.
 */
#define MR_WDT_DEFINE(_name, _data, _priv, _ops)                               \
    MR_DRIVER_DEFINE(_name, MR_DRIVER_CLAZZES_STATIK(wdt), MR_DRIVER_TYPE_WDT, \
                     _data, _priv, _ops)

/**
 * @brief This macro function installs a WDT driver.
 *
 * @param _name The WDT driver name.
 * @param _data The WDT driver data.
 * @param _priv The WDT private data.
 * @param _ops The WDT operations.
 * @param _level The init level.
 */
#define MR_WDT_INSTALL(_name, _data, _priv, _ops, _level)                      \
    MR_DRIVER_INSTALL(_name, MR_DRIVER_CLAZZES_STATIK(wdt),                    \
                      MR_DRIVER_TYPE_WDT, _data, _priv, _ops, _level)

/**
 * @brief This function initializes a WDT driver.
 *
 * @param drv The WDT driver.
 * @param data The WDT driver data.
 * @param priv The WDT private data.
 * @param ops The WDT operations.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_wdt_init(mr_driver_t *drv, mr_wdt_data_t *data,
                               void *priv, mr_wdt_ops_t *ops) {
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_STATIK(wdt);

    /* Check parameter */
    MR_ASSERT(drv != MR_NULL);
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Init WDT driver */
    return mr_driver_init(drv, dclazz, MR_DRIVER_TYPE_WDT, data, priv, ops);
}

/**
 * @brief This function creates a WDT driver.
 *
 * @param data The WDT driver data.
 * @param priv The WDT private data.
 * @param ops The WDT operations.
 * @return The WDT driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_wdt_create(mr_wdt_data_t *data, void *priv,
                                     mr_wdt_ops_t *ops) {
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_DYNAMIC(wdt);

    /* Check parameter */
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Create WDT driver */
    return mr_driver_create(dclazz, MR_DRIVER_TYPE_WDT, data, priv, ops);
}

/**
 * @brief This function deletes a WDT driver.
 *
 * @param drv The WDT driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_wdt_del(mr_driver_t *drv) {
    /* Check parameter */
    MR_DRIVER_ASSERT_TYPE(drv, MR_DRIVER_TYPE_WDT);

    /* Delete driver */
    return mr_driver_del(drv);
}

/**
 * @brief This function configures a WDT.
 *
 * @param drv The WDT driver.
 * @param config The WDT configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_wdt_configure(mr_driver_t *drv, const mr_wdt_config_t *config);

/**
 * @brief This function gets a WDT configuration.
 *
 * @param drv The WDT driver.
 * @param config The WDT configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_wdt_configuration(mr_driver_t *drv, mr_wdt_config_t *config);

/**
 * @brief This function starts a WDT.
 *
 * @param drv The WDT driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_wdt_start(mr_driver_t *drv) {
    const mr_wdt_ops_t *wdt_ops;
    mr_bool_t busy;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_WDT);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get WDT operations */
    wdt_ops = (const mr_wdt_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(wdt_ops->start != MR_NULL);
    MR_ASSERT(wdt_ops->is_busy != MR_NULL);

    /* Check whether WDT is already running */
    ret = wdt_ops->is_busy(drv, &busy);
    if (ret != 0) {
        return ret;
    }
    if (busy) {
        return -MR_EBUSY;
    }

    /* Start WDT */
    return wdt_ops->start(drv);
}

/**
 * @brief This function stops a WDT.
 *
 * @param drv The WDT driver.
 * @return 0 on success, a negative error code on failure.
 *
 * @note An already stopped WDT succeeds. A running WDT returns -MR_ENOSYS
 * when the backend does not support stop.
 */
MR_INLINE mr_err_t mr_wdt_stop(mr_driver_t *drv) {
    const mr_wdt_ops_t *wdt_ops;
    mr_bool_t busy;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_WDT);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get WDT operations */
    wdt_ops = (const mr_wdt_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(wdt_ops->is_busy != MR_NULL);

    /* Check whether WDT is running */
    ret = wdt_ops->is_busy(drv, &busy);
    if (ret != 0) {
        return ret;
    }
    if (!busy) {
        return 0;
    }

    /* Check whether stop is supported */
    if (!wdt_ops->stop) {
        return -MR_ENOSYS;
    }

    /* Stop WDT */
    return wdt_ops->stop(drv);
}

/**
 * @brief This function feeds a WDT.
 *
 * @param drv The WDT driver.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Returns -MR_EINVAL when the WDT is not running.
 */
MR_INLINE mr_err_t mr_wdt_feed(mr_driver_t *drv) {
    const mr_wdt_ops_t *wdt_ops;
    mr_bool_t busy;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_WDT);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get WDT operations */
    wdt_ops = (const mr_wdt_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(wdt_ops->feed != MR_NULL);
    MR_ASSERT(wdt_ops->is_busy != MR_NULL);

    /* Check whether WDT is running */
    ret = wdt_ops->is_busy(drv, &busy);
    if (ret != 0) {
        return ret;
    }
    if (!busy) {
        return -MR_EINVAL;
    }

    /* Feed WDT */
    return wdt_ops->feed(drv);
}

/**
 * @brief This function checks if a WDT is busy.
 *
 * @param drv The WDT driver.
 * @param busy The busy state output.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_wdt_is_busy(mr_driver_t *drv, mr_bool_t *busy) {
    const mr_wdt_ops_t *wdt_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_WDT);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(busy != MR_NULL);

    /* Get WDT operations */
    wdt_ops = (const mr_wdt_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(wdt_ops->is_busy != MR_NULL);

    /* Get WDT state */
    return wdt_ops->is_busy(drv, busy);
}

/**
 * @brief This function attaches a WDT event.
 *
 * @param drv The WDT driver.
 * @param entry The WDT event entry.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_wdt_event_attach(mr_driver_t *drv, mr_waiter_t *entry) {
    mr_wdt_data_t *wdt_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_WDT);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(entry != MR_NULL);

    /* Get WDT data */
    wdt_data = (mr_wdt_data_t *)MR_DRIVER_DATA(drv);

    /* Attach WDT event */
    return mr_waiter_wait(entry, &wdt_data->event);
}

/**
 * @brief This function enables WDT events.
 *
 * @param drv The WDT driver.
 * @param events The WDT event mask.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_wdt_event_enable(mr_driver_t *drv, mr_uint32_t events) {
    const mr_wdt_ops_t *wdt_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_WDT);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get WDT operations */
    wdt_ops = (const mr_wdt_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether event control is supported */
    if (!wdt_ops->event_control) {
        return -MR_ENOSYS;
    }

    /* Enable WDT events */
    return wdt_ops->event_control(drv, events, MR_TRUE);
}

/**
 * @brief This function disables WDT events.
 *
 * @param drv The WDT driver.
 * @param events The WDT event mask.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_wdt_event_disable(mr_driver_t *drv, mr_uint32_t events) {
    const mr_wdt_ops_t *wdt_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_WDT);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get WDT operations */
    wdt_ops = (const mr_wdt_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether event control is supported */
    if (!wdt_ops->event_control) {
        return -MR_ENOSYS;
    }

    /* Disable WDT events */
    return wdt_ops->event_control(drv, events, MR_FALSE);
}

/**
 * @brief This function raises WDT events.
 *
 * @param drv The WDT driver.
 * @param events The WDT events.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_wdt_event_raise(mr_driver_t *drv, mr_uint32_t events) {
    mr_wdt_data_t *wdt_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_WDT);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get WDT data */
    wdt_data = (mr_wdt_data_t *)MR_DRIVER_DATA(drv);

    /* Raise WDT events */
    return mr_waitable_raise(&wdt_data->event, (mr_wait_event_t)events,
                             MR_NULL);
}

/**
 * @brief This function gets a WDT driver.
 *
 * @param drv The WDT driver.
 * @return The WDT driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_wdt_get(mr_driver_t *drv) {
    /* Get driver reference */
    return mr_driver_get(drv);
}

/**
 * @brief This function puts a WDT driver.
 *
 * @param drv The WDT driver.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_wdt_put(mr_driver_t *drv) {
    /* Put driver reference */
    return mr_driver_put(drv);
}

/* ================================ HardTree ================================ */

/**
 * @brief This macro function installs a WDT instance through hardtree.
 *
 * @param _id The instance id.
 * @param _priv The WDT private data.
 * @param _ops The WDT operations.
 */
#define MR_HT_WDT_INSTALL(_id, _priv, _ops)                                    \
    static mr_wdt_data_t MR_CAT(__mr_wdt_data_, _id) = {0};                    \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(wdt),                   \
                         MR_DRIVER_TYPE_WDT, &MR_CAT(__mr_wdt_data_, _id),     \
                         _priv, _ops)
#endif /* defined(MR_USE_DRIVER_WDT) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_WDT_H__ */

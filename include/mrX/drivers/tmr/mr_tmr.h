/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-14        MacRsh        First version
 */

#ifndef __MR_DRIVER_TMR_H__
#define __MR_DRIVER_TMR_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_DRIVER_TMR)
#include <mrX/drivers/mr_driver.h>
#endif /* defined(MR_USE_DRIVER_TMR) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup TMR
 * @{
 */

#if defined(MR_USE_DRIVER_TMR)
/* TMR wait event definition */
#define MR_TMR_WAIT_TIMEOUT             (1U << 0)

/* TMR signal definition */
#define MR_TMR_SIGNAL_UPDATE            (1U << 0)

/* TMR mode definition */
typedef enum mr_tmr_mode {
    MR_TMR_MODE_PERIODIC,
    MR_TMR_MODE_ONESHOT,
} mr_tmr_mode_t;

/* TMR configuration type */
typedef struct mr_tmr_config {
    mr_uint64_t period;
    mr_uint8_t signal: 3;
    mr_uint8_t mode: 1;
} mr_tmr_config_t;

/* TMR data type */
MR_DRIVER_CLAZZES_EXPORT(tmr);
typedef struct mr_tmr_data {
    mr_waitable_t event;
} mr_tmr_data_t;

/* TMR operations type */
typedef struct mr_tmr_ops {
    mr_err_t (*configure)(const mr_driver_t *drv,
                          const mr_tmr_config_t *config);
    mr_err_t (*configuration)(const mr_driver_t *drv, mr_tmr_config_t *config);
    mr_err_t (*start)(const mr_driver_t *drv);
    mr_err_t (*stop)(const mr_driver_t *drv);
    mr_err_t (*is_busy)(const mr_driver_t *drv, mr_bool_t *busy);
    mr_err_t (*event_control)(const mr_driver_t *drv, mr_uint32_t events,
                              mr_bool_t enable);
} mr_tmr_ops_t;

/**
 * @brief This macro function defines a TMR driver.
 *
 * @param _name The TMR driver name.
 * @param _data The TMR driver data.
 * @param _priv The TMR private data.
 * @param _ops The TMR operations.
 */
#define MR_TMR_DEFINE(_name, _data, _priv, _ops)                               \
    MR_DRIVER_DEFINE(_name, MR_DRIVER_CLAZZES_STATIK(tmr), MR_DRIVER_TYPE_TMR, \
                     _data, _priv, _ops)

/**
 * @brief This macro function installs a TMR driver.
 *
 * @param _name The TMR driver name.
 * @param _data The TMR driver data.
 * @param _priv The TMR private data.
 * @param _ops The TMR operations.
 * @param _level The init level.
 */
#define MR_TMR_INSTALL(_name, _data, _priv, _ops, _level)                      \
    MR_DRIVER_INSTALL(_name, MR_DRIVER_CLAZZES_STATIK(tmr),                    \
                      MR_DRIVER_TYPE_TMR, _data, _priv, _ops, _level)

/**
 * @brief This function initializes a TMR driver.
 *
 * @param drv The TMR driver.
 * @param data The TMR driver data.
 * @param priv The TMR private data.
 * @param ops The TMR operations.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_tmr_init(mr_driver_t *drv, mr_tmr_data_t *data,
                               void *priv, mr_tmr_ops_t *ops) {
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_STATIK(tmr);

    /* Check parameter */
    MR_ASSERT(drv != MR_NULL);
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Init TMR driver */
    return mr_driver_init(drv, dclazz, MR_DRIVER_TYPE_TMR, data, priv, ops);
}

/**
 * @brief This function creates a TMR driver.
 *
 * @param data The TMR driver data.
 * @param priv The TMR private data.
 * @param ops The TMR operations.
 * @return The TMR driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_tmr_create(mr_tmr_data_t *data, void *priv,
                                     mr_tmr_ops_t *ops) {
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_DYNAMIC(tmr);

    /* Check parameter */
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Create TMR driver */
    return mr_driver_create(dclazz, MR_DRIVER_TYPE_TMR, data, priv, ops);
}

/**
 * @brief This function deletes a TMR driver.
 *
 * @param drv The TMR driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_tmr_del(mr_driver_t *drv) {
    /* Check parameter */
    MR_DRIVER_ASSERT_TYPE(drv, MR_DRIVER_TYPE_TMR);

    /* Delete driver */
    return mr_driver_del(drv);
}

/**
 * @brief This function configures a TMR.
 *
 * @param drv The TMR driver.
 * @param config The TMR configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_tmr_configure(mr_driver_t *drv, const mr_tmr_config_t *config);

/**
 * @brief This function gets a TMR configuration.
 *
 * @param drv The TMR driver.
 * @param config The TMR configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_tmr_configuration(mr_driver_t *drv, mr_tmr_config_t *config);

/**
 * @brief This function starts a TMR.
 *
 * @param drv The TMR driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_tmr_start(mr_driver_t *drv) {
    const mr_tmr_ops_t *hw_ops;
    mr_bool_t busy;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_TMR);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get TMR operations */
    hw_ops = (const mr_tmr_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(hw_ops->start != MR_NULL);
    MR_ASSERT(hw_ops->is_busy != MR_NULL);

    /* Check whether TMR is already running */
    ret = hw_ops->is_busy(drv, &busy);
    if (ret != 0) {
        return ret;
    }
    if (busy) {
        return -MR_EBUSY;
    }

    /* Start TMR */
    return hw_ops->start(drv);
}

/**
 * @brief This function stops a TMR.
 *
 * @param drv The TMR driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_tmr_stop(mr_driver_t *drv) {
    const mr_tmr_ops_t *hw_ops;
    mr_bool_t busy;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_TMR);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get TMR operations */
    hw_ops = (const mr_tmr_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(hw_ops->stop != MR_NULL);
    MR_ASSERT(hw_ops->is_busy != MR_NULL);

    /* Check whether TMR is running */
    ret = hw_ops->is_busy(drv, &busy);
    if (ret != 0) {
        return ret;
    }
    if (!busy) {
        return 0;
    }

    /* Stop TMR */
    return hw_ops->stop(drv);
}

/**
 * @brief This function checks if a TMR is busy.
 *
 * @param drv The TMR driver.
 * @param busy The busy state output.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_tmr_is_busy(mr_driver_t *drv, mr_bool_t *busy) {
    const mr_tmr_ops_t *hw_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_TMR);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(busy != MR_NULL);

    /* Get TMR operations */
    hw_ops = (const mr_tmr_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(hw_ops->is_busy != MR_NULL);

    /* Get TMR state */
    return hw_ops->is_busy(drv, busy);
}

/**
 * @brief This function attaches a TMR event.
 *
 * @param drv The TMR driver.
 * @param entry The TMR event entry.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_tmr_event_attach(mr_driver_t *drv, mr_waiter_t *entry) {
    mr_tmr_data_t *hw_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_TMR);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(entry != MR_NULL);

    /* Get TMR data */
    hw_data = (mr_tmr_data_t *)MR_DRIVER_DATA(drv);

    /* Attach TMR event */
    return mr_waiter_wait(entry, &hw_data->event);
}

/**
 * @brief This function enables TMR events.
 *
 * @param drv The TMR driver.
 * @param events The TMR event mask.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_tmr_event_enable(mr_driver_t *drv, mr_uint32_t events) {
    const mr_tmr_ops_t *hw_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_TMR);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get TMR operations */
    hw_ops = (const mr_tmr_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether event control is supported */
    if (!hw_ops->event_control) {
        return -MR_ENOSYS;
    }

    /* Enable TMR events */
    return hw_ops->event_control(drv, events, MR_TRUE);
}

/**
 * @brief This function disables TMR events.
 *
 * @param drv The TMR driver.
 * @param events The TMR event mask.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_tmr_event_disable(mr_driver_t *drv, mr_uint32_t events) {
    const mr_tmr_ops_t *hw_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_TMR);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get TMR operations */
    hw_ops = (const mr_tmr_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether event control is supported */
    if (!hw_ops->event_control) {
        return -MR_ENOSYS;
    }

    /* Disable TMR events */
    return hw_ops->event_control(drv, events, MR_FALSE);
}

/**
 * @brief This function raises TMR events.
 *
 * @param drv The TMR driver.
 * @param events The TMR events.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_tmr_event_raise(mr_driver_t *drv, mr_uint32_t events) {
    mr_tmr_data_t *hw_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_TMR);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get TMR data */
    hw_data = (mr_tmr_data_t *)MR_DRIVER_DATA(drv);

    /* Raise TMR events */
    return mr_waitable_raise(&hw_data->event, (mr_wait_event_t)events, MR_NULL);
}

/**
 * @brief This function gets a TMR driver.
 *
 * @param drv The TMR driver.
 * @return The TMR driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_tmr_get(mr_driver_t *drv) {
    /* Get driver reference */
    return mr_driver_get(drv);
}

/**
 * @brief This function puts a TMR driver.
 *
 * @param drv The TMR driver.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_tmr_put(mr_driver_t *drv) {
    /* Put driver reference */
    return mr_driver_put(drv);
}

/* ================================ HardTree ================================ */

/**
 * @brief This macro function installs a TMR instance through hardtree.
 *
 * @param _id The instance id.
 * @param _priv The TMR private data.
 * @param _ops The TMR operations.
 */
#define MR_HT_TMR_INSTALL(_id, _priv, _ops)                                    \
    static mr_tmr_data_t MR_CAT(__mr_tmr_data_, _id) = {0};                    \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(tmr),                   \
                         MR_DRIVER_TYPE_TMR, &MR_CAT(__mr_tmr_data_, _id),     \
                         _priv, _ops)
#endif /* defined(MR_USE_DRIVER_TMR) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_TMR_H__ */

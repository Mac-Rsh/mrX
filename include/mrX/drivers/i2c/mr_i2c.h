/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-17        MacRsh        First version
 */

#ifndef __MR_DRIVER_I2C_H__
#define __MR_DRIVER_I2C_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_DRIVER_I2C)
#include <mrX/drivers/mr_driver.h>
#endif /* defined(MR_USE_DRIVER_I2C) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup I2C
 * @{
 */

#if defined(MR_USE_DRIVER_I2C)
/* I2C wait event definition */
#define MR_I2C_WAIT_RX_START            (1U << 0)
#define MR_I2C_WAIT_RX_READY            (1U << 1)
#define MR_I2C_WAIT_TX_START            (1U << 2)
#define MR_I2C_WAIT_TX_READY            (1U << 3)
#define MR_I2C_WAIT_STOP                (1U << 4)
#define MR_I2C_WAIT_ERROR               (1U << 5)

/* I2C message flag definition */
#define MR_I2C_MSG_WRITE                0U
#define MR_I2C_MSG_READ                 (1U << 0)
#define MR_I2C_MSG_STOP                 (1U << 1)
#define MR_I2C_MSG_NOSTOP               (1U << 2)
#define MR_I2C_MSG_ADDR10               (1U << 3)

/* I2C role definition */
typedef enum mr_i2c_role {
    MR_I2C_ROLE_MASTER,
    MR_I2C_ROLE_SLAVE,
} mr_i2c_role_t;

/* I2C configuration type */
typedef struct mr_i2c_config {
    mr_uint32_t clock_rate;
    mr_uint16_t own_addr;
    mr_uint8_t addr10: 1;
    mr_uint8_t role: 1;
} mr_i2c_config_t;

/* I2C message type */
typedef struct mr_i2c_msg {
    mr_uint16_t addr;
    mr_uint16_t flags;
    mr_uint8_t *buf;
    mr_size_t len;
} mr_i2c_msg_t;

/* I2C data type */
MR_DRIVER_CLAZZES_EXPORT(i2c);
typedef struct mr_i2c_data {
    mr_waitable_t event;
} mr_i2c_data_t;

/* I2C operations type */
typedef struct mr_i2c_ops {
    mr_err_t (*configure)(const mr_driver_t *drv,
                          const mr_i2c_config_t *config);
    mr_err_t (*configuration)(const mr_driver_t *drv, mr_i2c_config_t *config);
    mr_ssize_t (*transfer)(const mr_driver_t *drv, const mr_i2c_msg_t *msgs,
                           mr_size_t num);
    mr_ssize_t (*slave_read)(const mr_driver_t *drv, void *buf, mr_size_t size);
    mr_ssize_t (*slave_write)(const mr_driver_t *drv, const void *buf,
                              mr_size_t size);
    mr_err_t (*event_control)(const mr_driver_t *drv, mr_uint32_t events,
                              mr_bool_t enable);
} mr_i2c_ops_t;

/**
 * @brief This macro function defines an I2C driver.
 *
 * @param _name The I2C driver name.
 * @param _data The I2C driver data.
 * @param _priv The I2C private data.
 * @param _ops The I2C operations.
 */
#define MR_I2C_DEFINE(_name, _data, _priv, _ops)                                \
    MR_DRIVER_DEFINE(_name, MR_DRIVER_CLAZZES_STATIK(i2c), MR_DRIVER_TYPE_I2C,  \
                     _data, _priv, _ops)

/**
 * @brief This macro function installs an I2C driver.
 *
 * @param _name The I2C driver name.
 * @param _data The I2C driver data.
 * @param _priv The I2C private data.
 * @param _ops The I2C operations.
 * @param _level The init level.
 */
#define MR_I2C_INSTALL(_name, _data, _priv, _ops, _level)                       \
    MR_DRIVER_INSTALL(_name, MR_DRIVER_CLAZZES_STATIK(i2c),                     \
                      MR_DRIVER_TYPE_I2C, _data, _priv, _ops, _level)

/**
 * @brief This function initializes an I2C driver.
 *
 * @param drv The I2C driver.
 * @param data The I2C driver data.
 * @param priv The I2C private data.
 * @param ops The I2C operations.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_i2c_init(mr_driver_t *drv, mr_i2c_data_t *data, void *priv,
                               mr_i2c_ops_t *ops) {
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_STATIK(i2c);

    /* Check parameter */
    MR_ASSERT(drv != MR_NULL);
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Init I2C driver */
    return mr_driver_init(drv, dclazz, MR_DRIVER_TYPE_I2C, data, priv, ops);
}

/**
 * @brief This function creates an I2C driver.
 *
 * @param data The I2C driver data.
 * @param priv The I2C private data.
 * @param ops The I2C operations.
 * @return The I2C driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_i2c_create(mr_i2c_data_t *data, void *priv,
                                     mr_i2c_ops_t *ops) {
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_DYNAMIC(i2c);

    /* Check parameter */
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Create I2C driver */
    return mr_driver_create(dclazz, MR_DRIVER_TYPE_I2C, data, priv, ops);
}

/**
 * @brief This function deletes an I2C driver.
 *
 * @param drv The I2C driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_i2c_del(mr_driver_t *drv) {
    /* Check parameter */
    MR_DRIVER_ASSERT_TYPE(drv, MR_DRIVER_TYPE_I2C);

    /* Delete driver */
    return mr_driver_del(drv);
}

/**
 * @brief This function configures an I2C.
 *
 * @param drv The I2C driver.
 * @param config The I2C configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_i2c_configure(mr_driver_t *drv, const mr_i2c_config_t *config);

/**
 * @brief This function gets an I2C configuration.
 *
 * @param drv The I2C driver.
 * @param config The I2C configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_i2c_configuration(mr_driver_t *drv, mr_i2c_config_t *config);

/**
 * @brief This function transfers I2C master messages.
 *
 * @param drv The I2C driver.
 * @param msgs The I2C message array.
 * @param num The number of messages.
 * @return The number of completed messages on success, a negative error code on
 * failure.
 */
mr_ssize_t mr_i2c_transfer(mr_driver_t *drv, const mr_i2c_msg_t *msgs,
                           mr_size_t num);

/**
 * @brief This function reads received slave data from an I2C controller.
 *
 * @param drv The I2C driver.
 * @param buf The receive buffer.
 * @param size The receive size in bytes.
 * @return The actual size of read data on success, a negative error code on
 * failure.
 */
MR_INLINE mr_ssize_t mr_i2c_slave_read(mr_driver_t *drv, void *buf,
                                       mr_size_t size) {
    const mr_i2c_ops_t *i2c_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_I2C);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT((buf != MR_NULL) || (size == 0U));

    /* Get I2C operations */
    i2c_ops = (const mr_i2c_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether operation is supported */
    if (!i2c_ops->slave_read) {
        return -MR_ENOSYS;
    }

    /* Handle empty transfer */
    if (size == 0U) {
        return 0;
    }

    /* Read slave data */
    return i2c_ops->slave_read(drv, buf, size);
}

/**
 * @brief This function writes response data to an I2C slave controller.
 *
 * @param drv The I2C driver.
 * @param buf The transmit buffer.
 * @param size The transmit size in bytes.
 * @return The actual size of written data on success, a negative error code on
 * failure.
 */
MR_INLINE mr_ssize_t mr_i2c_slave_write(mr_driver_t *drv, const void *buf,
                                        mr_size_t size) {
    const mr_i2c_ops_t *i2c_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_I2C);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT((buf != MR_NULL) || (size == 0U));

    /* Get I2C operations */
    i2c_ops = (const mr_i2c_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether operation is supported */
    if (!i2c_ops->slave_write) {
        return -MR_ENOSYS;
    }

    /* Handle empty transfer */
    if (size == 0U) {
        return 0;
    }

    /* Write slave data */
    return i2c_ops->slave_write(drv, buf, size);
}

/**
 * @brief This function enables I2C events.
 *
 * @param drv The I2C driver.
 * @param events The I2C event mask.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_i2c_event_enable(mr_driver_t *drv, mr_uint32_t events) {
    const mr_i2c_ops_t *i2c_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_I2C);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get I2C operations */
    i2c_ops = (const mr_i2c_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether event control is supported */
    if (!i2c_ops->event_control) {
        return -MR_ENOSYS;
    }

    /* Enable I2C events */
    return i2c_ops->event_control(drv, events, MR_TRUE);
}

/**
 * @brief This function disables I2C events.
 *
 * @param drv The I2C driver.
 * @param events The I2C event mask.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_i2c_event_disable(mr_driver_t *drv, mr_uint32_t events) {
    const mr_i2c_ops_t *i2c_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_I2C);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get I2C operations */
    i2c_ops = (const mr_i2c_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether event control is supported */
    if (!i2c_ops->event_control) {
        return -MR_ENOSYS;
    }

    /* Disable I2C events */
    return i2c_ops->event_control(drv, events, MR_FALSE);
}

/**
 * @brief This function attaches an I2C event.
 *
 * @param drv The I2C driver.
 * @param entry The I2C event entry.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_i2c_event_attach(mr_driver_t *drv, mr_waiter_t *entry) {
    mr_i2c_data_t *i2c_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_I2C);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(entry != MR_NULL);

    /* Get I2C data */
    i2c_data = (mr_i2c_data_t *)MR_DRIVER_DATA(drv);

    /* Attach I2C event */
    return mr_waiter_wait(entry, &i2c_data->event);
}

/**
 * @brief This function raises I2C events.
 *
 * @param drv The I2C driver.
 * @param events The I2C events.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_i2c_event_raise(mr_driver_t *drv, mr_uint32_t events) {
    mr_i2c_data_t *i2c_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_I2C);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get I2C data */
    i2c_data = (mr_i2c_data_t *)MR_DRIVER_DATA(drv);

    /* Raise I2C events */
    return mr_waitable_raise(&i2c_data->event, (mr_wait_event_t)events, MR_NULL);
}

/**
 * @brief This function gets an I2C driver.
 *
 * @param drv The I2C driver.
 * @return The I2C driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_i2c_get(mr_driver_t *drv) {
    /* Get driver reference */
    return mr_driver_get(drv);
}

/**
 * @brief This function puts an I2C driver.
 *
 * @param drv The I2C driver.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_i2c_put(mr_driver_t *drv) {
    /* Put driver reference */
    return mr_driver_put(drv);
}

/* ================================ HardTree ================================ */

/**
 * @brief This macro function installs an I2C instance through hardtree.
 *
 * @param _id The instance id.
 * @param _priv The I2C private data.
 * @param _ops The I2C operations.
 */
#define MR_HT_I2C_INSTALL(_id, _priv, _ops)                                     \
    static mr_i2c_data_t MR_CAT(__mr_i2c_data_, _id) = {0};                     \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(i2c),                    \
                         MR_DRIVER_TYPE_I2C, &MR_CAT(__mr_i2c_data_, _id),      \
                         _priv, _ops)
#endif /* defined(MR_USE_DRIVER_I2C) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_I2C_H__ */

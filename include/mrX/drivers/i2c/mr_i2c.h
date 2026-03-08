/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
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
/* I2C message flag definition */
#define MR_I2C_MSG_START                (1U << 0)
#define MR_I2C_MSG_STOP                 (1U << 1)
#define MR_I2C_MSG_WRITE                (0U << 2)
#define MR_I2C_MSG_READ                 (1U << 2)
#define MR_I2C_MSG_RESTART              MR_I2C_MSG_START
#define MR_I2C_MSG_ADDR_BITS_10         (1U << 11)

/* I2C address bits type */
typedef enum mr_i2c_addr_bits {
    MR_I2C_ADDR_BITS_7,
    MR_I2C_ADDR_BITS_10,
} mr_i2c_addr_bits_t;

/* I2C configuration type */
typedef struct mr_i2c_config {
    mr_uint32_t baud_rate: 25;
} mr_i2c_config_t;

/* I2C message type */
typedef struct mr_i2c_msg {
    mr_uint16_t flags;
    mr_uint16_t addr;
    mr_uint32_t size;
    void *buf;
} mr_i2c_msg_t;

/* I2C operations mode definition */
typedef enum mr_i2c_ops_mode {
    MR_I2C_OPS_MODE_POLL,
    MR_I2C_OPS_MODE_INT,
} mr_i2c_ops_mode_t;

/* I2C data type */
MR_DRIVER_CLAZZES_EXPORT(i2c);
typedef struct mr_i2c_data {
    int (*xfer)(mr_driver_t *drv, mr_i2c_msg_t *msg, mr_uint32_t msgs_num);
    mr_uint32_t xfer_ops_mode: 2;
    mr_i2c_config_t config;
    mr_waitable_t irq;
} mr_i2c_data_t;

/* I2C operations type */
typedef struct mr_i2c_ops {
    mr_err_t (*configure)(const mr_driver_t *drv,
                          const mr_i2c_config_t *config);
    int (*xfer)(const mr_driver_t *drv, mr_i2c_msg_t *msg);
    mr_err_t (*recover)(const mr_driver_t *drv);
} mr_i2c_ops_t;

/**
 * @brief This macro function initializes an I2C data.
 *
 * @param _xfer_ops_mode The I2C transfer operations mode.
 * @param _baud_rate The I2C baud rate.
 */
#define MR_I2C_DATA_INIT(_xfer_ops_mode, _baud_rate)                           \
    {                                                                          \
        .xfer_ops_mode = (_xfer_ops_mode),                                     \
        .config = {.baud_rate = (_baud_rate)},                                 \
    }

/**
 * @brief This function initializes an I2C driver.
 *
 * @param drv The I2C driver.
 * @param data The I2C driver data.
 * @param priv The I2C private data.
 * @param ops The I2C operations.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_i2c_init(mr_driver_t *drv, mr_i2c_data_t *data,
                               void *priv, mr_i2c_ops_t *ops) {
    const mr_driver_clazzes_t *dclazzes = MR_DRIVER_CLAZZES_FIND(i2c);
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_STATIK(dclazzes);

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
    const mr_driver_clazzes_t *dclazzes = MR_DRIVER_CLAZZES_FIND(i2c);
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_STATIK(dclazzes);

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
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_I2C);

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
mr_err_t mr_i2c_configure(mr_driver_t *drv, mr_i2c_config_t *config);

/**
 * @brief This function gets an I2C configuration.
 *
 * @param drv The I2C driver.
 * @param config The I2C configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_i2c_configuration(mr_driver_t *drv, mr_i2c_config_t *config);

/**
 * @brief This function transfers an I2C messages.
 *
 * @param drv The I2C driver.
 * @param msgs The I2C messages.
 * @param msgs_num The number of I2C messages.
 * @return The number of bytes transferred on success, a negative error code on
 * failure.
 */
MR_INLINE int mr_i2c_xfer(mr_driver_t *drv, mr_i2c_msg_t *msgs,
                          mr_uint32_t msgs_num) {
    mr_i2c_data_t *ic_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_I2C);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT((msgs != MR_NULL) || (msgs_num == 0));

    /* Get I2C data */
    ic_data = (mr_i2c_data_t *)MR_DRIVER_DATA(drv);

    /* Transfer I2C messages */
    return ic_data->xfer(drv, msgs, msgs_num);
}

/**
 * @brief This function writes an I2C data.
 *
 * @param drv The I2C driver.
 * @param addr The I2C address.
 * @param buf The I2C buffer.
 * @param size The I2C buffer size.
 * @return The number of bytes written on success, a negative error code on
 * failure.
 */
MR_INLINE int mr_i2c_write(mr_driver_t *drv, mr_uint16_t addr, const void *buf,
                           mr_uint32_t size) {
    mr_i2c_msg_t msg;

    /* Write I2C message */
    msg.flags = MR_I2C_MSG_WRITE | MR_I2C_MSG_STOP;
    if (addr & MR_I2C_MSG_ADDR_BITS_10) {
        msg.flags |= MR_I2C_MSG_ADDR_BITS_10;
        addr &= 0x3ff;
    } else {
        addr &= 0x7f;
    }
    msg.buf = (void *)buf;
    msg.size = size;
    msg.addr = addr;
    return mr_i2c_xfer(drv, &msg, 1);
}

/**
 * @brief This function reads an I2C data.
 *
 * @param drv The I2C driver.
 * @param addr The I2C address.
 * @param buf The I2C buffer.
 * @param size The I2C buffer size.
 * @return The number of bytes read on success, a negative error code on
 * failure.
 */
MR_INLINE int mr_i2c_read(mr_driver_t *drv, mr_uint16_t addr, void *buf,
                          mr_uint32_t size) {
    mr_i2c_msg_t msg;

    /* Read I2C message */
    msg.flags = MR_I2C_MSG_READ | MR_I2C_MSG_STOP;
    if (addr & MR_I2C_MSG_ADDR_BITS_10) {
        msg.flags |= MR_I2C_MSG_ADDR_BITS_10;
        addr &= 0x3ff;
    } else {
        addr &= 0x7f;
    }
    msg.buf = (void *)buf;
    msg.size = size;
    msg.addr = addr;
    return mr_i2c_xfer(drv, &msg, 1);
}

/**
 * @brief This function recovers an I2C.
 *
 * @param drv The I2C driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_i2c_recover(mr_driver_t *drv) {
    const mr_i2c_ops_t *ic_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_I2C);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get I2C data */
    ic_ops = (mr_i2c_ops_t *)MR_DRIVER_OPS(drv);

    /* Recover I2C */
    return ic_ops->recover(drv);
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
#define MR_HT_INST_I2C_INSTALL(_id, _priv, _ops)                               \
    static mr_i2c_data_t __mr_i2c_data_##_id = MR_I2C_DATA_INIT(               \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), xfer_ops_mode,              \
                           MR_I2C_OPS_MODE_POLL),                              \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), baud_rate, 100000));        \
    MR_HT_INST_DRIVER_INSTALL(                                                 \
        _id, MR_DRIVER_CLAZZES_STATIK(MR_DRIVER_CLAZZES_FIND(i2c)),            \
        MR_DRIVER_TYPE_I2C, &__mr_i2c_data_##_id, _priv, _ops)
#endif /* defined(MR_USE_DRIVER_I2C) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_I2C_H__ */

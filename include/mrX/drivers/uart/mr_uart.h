/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_DRIVER_UART_H__
#define __MR_DRIVER_UART_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_DRIVER_UART)
#include <mrX/drivers/mr_driver.h>
#endif /* defined(MR_USE_DRIVER_UART) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup UART
 * @{
 */

#if defined(MR_USE_DRIVER_UART)
/* UART wait event type definition */
#define MR_UART_WAIT_TX                 (MR_WAIT_OUT)
#define MR_UART_WAIT_TX_COMPLETE        (MR_WAIT_OUT | (1ULL << 0))
#define MR_UART_WAIT_RX                 (MR_WAIT_IN)
#define MR_UART_WAIT_RX_COMPLETE        (MR_WAIT_IN | (1ULL << 1))

/* UART data bits definition */
typedef enum mr_uart_data_bits {
    MR_UART_DATA_BITS_5,
    MR_UART_DATA_BITS_6,
    MR_UART_DATA_BITS_7,
    MR_UART_DATA_BITS_8,
} mr_uart_data_bits_t;

/* UART stop bits definition */
typedef enum mr_uart_stop_bits {
    MR_UART_STOP_BITS_0_5,
    MR_UART_STOP_BITS_1,
    MR_UART_STOP_BITS_1_5,
    MR_UART_STOP_BITS_2,
} mr_uart_stop_bits_t;

/* UART parity definition */
typedef enum mr_uart_parity {
    MR_UART_PARITY_NONE,
    MR_UART_PARITY_ODD,
    MR_UART_PARITY_EVEN,
} mr_uart_parity_t;

/* UART bits order definition */
typedef enum mr_uart_bits_order {
    MR_UART_BITS_ORDER_LSB,
    MR_UART_BITS_ORDER_MSB,
} mr_uart_bits_order_t;

/* UART NRZ mode definition */
typedef enum mr_uart_nrz {
    MR_UART_NRZ_NONE,
    MR_UART_NRZ_INVERTED,
} mr_uart_nrz_t;

/* UART flow control definition */
typedef enum mr_uart_flow_control {
    MR_UART_FLOW_CTRL_NONE,
    MR_UART_FLOW_CTRL_RTS_CTS,
    MR_UART_FLOW_CTRL_DTR_DSR,
    MR_UART_FLOW_CTRL_RS485,
} mr_uart_flow_control_t;

/* UART configuration type */
typedef struct mr_uart_config {
    mr_uint32_t baud_rate;
    mr_uint32_t data_bits: 2;
    mr_uint32_t stop_bits: 2;
    mr_uint32_t parity: 2;
    mr_uint32_t bits_order: 1;
    mr_uint32_t nrz: 1;
    mr_uint32_t flow_ctrl: 2;
} mr_uart_config_t;

/* UART data type */
MR_DRIVER_CLAZZES_EXPORT(uart);
typedef struct mr_uart_data {
    mr_uart_config_t config;
    mr_waitable_t irq;
} mr_uart_data_t;

/* UART operations type */
typedef struct mr_uart_ops {
    mr_err_t (*configure)(const mr_driver_t *drv,
                          const mr_uart_config_t *config);
    mr_err_t (*write)(const mr_driver_t *drv, mr_uint8_t data);
    mr_err_t (*read)(const mr_driver_t *drv, mr_uint8_t *data);
#if defined(MR_USE_DRIVER_UART_ASYNC)
    mr_err_t (*tx_async_control)(const mr_driver_t *drv, mr_bool_t enable,
                                 const mr_uint8_t *buf, mr_size_t size);
    mr_err_t (*tx_async_is_busy)(const mr_driver_t *drv, mr_bool_t *busy);
    mr_err_t (*tx_async_offset)(const mr_driver_t *drv, mr_size_t *offset);
    mr_err_t (*rx_async_control)(const mr_driver_t *drv, mr_bool_t enable,
                                 mr_bool_t circular, mr_uint8_t *buf,
                                 mr_size_t size);
    mr_err_t (*rx_async_is_busy)(const mr_driver_t *drv, mr_bool_t *busy);
    mr_err_t (*rx_async_offset)(const mr_driver_t *drv, mr_size_t *offset);
#endif /* defined(MR_USE_DRIVER_UART_ASYNC) */
} mr_uart_ops_t;

/**
 * @brief This macro function initializes an UART data.
 *
 * @param _baud_rate The UART baud rate.
 * @param _data_bits The UART data bits.
 * @param _stop_bits The UART stop bits.
 * @param _parity The UART parity.
 * @param _bits_order The UART bits order.
 * @param _nrz The UART NRZ mode.
 * @param _flow_ctrl The UART flow control.
 */
#define MR_UART_DATA_INIT(_baud_rate, _data_bits, _stop_bits, _parity,         \
                          _bits_order, _nrz, _flow_ctrl)                       \
    {                                                                          \
        .config = {.baud_rate = (_baud_rate),                                  \
                   .data_bits = (_data_bits),                                  \
                   .stop_bits = (_stop_bits),                                  \
                   .parity = (_parity),                                        \
                   .bits_order = (_bits_order),                                \
                   .nrz = (_nrz),                                              \
                   .flow_ctrl = (_flow_ctrl),},                                \
    }

/**
 * @brief This macro function defines a UART driver.
 *
 * @param _name The UART driver name.
 * @param _data The UART driver data.
 * @param _priv The UART private data.
 * @param _ops The UART operations.
 */
#define MR_UART_DEFINE(_name, _data, _priv, _ops)                              \
    MR_DRIVER_DEFINE(_name,                                                    \
                     MR_DRIVER_CLAZZES_STATIK(MR_DRIVER_CLAZZES_FIND(uart)),   \
                     MR_DRIVER_TYPE_UART, _data, _priv, _ops)

/**
 * @brief This macro function installs a UART driver.
 *
 * @param _name The UART driver name.
 * @param _data The UART driver data.
 * @param _priv The UART private data.
 * @param _ops The UART operations.
 * @param _level The init level.
 */
#define MR_UART_INSTALL(_name, _data, _priv, _ops, _level)                     \
    MR_DRIVER_INSTALL(_name,                                                   \
                      MR_DRIVER_CLAZZES_STATIK(MR_DRIVER_CLAZZES_FIND(uart)),  \
                      MR_DRIVER_TYPE_UART, _data, _priv, _ops, _level)

/**
 * @brief This function initializes a UART driver.
 *
 * @param drv The UART driver.
 * @param data The UART driver data.
 * @param priv The UART private data.
 * @param ops The UART operations.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_uart_init(mr_driver_t *drv, mr_uart_data_t *data,
                                void *priv, mr_uart_ops_t *ops) {
    const mr_driver_clazzes_t *dclazzes = MR_DRIVER_CLAZZES_FIND(uart);
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_STATIK(dclazzes);

    /* Check parameter */
    MR_ASSERT(drv != MR_NULL);
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Init UART driver */
    return mr_driver_init(drv, dclazz, MR_DRIVER_TYPE_UART, data, priv, ops);
}

/**
 * @brief This function creates a UART driver.
 *
 * @param data The UART driver data.
 * @param priv The UART private data.
 * @param ops The UART operations.
 * @return The UART driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_uart_create(mr_uart_data_t *data, void *priv,
                                      mr_uart_ops_t *ops) {
    const mr_driver_clazzes_t *dclazzes = MR_DRIVER_CLAZZES_FIND(uart);
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_DYNAMIC(dclazzes);

    /* Check parameter */
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Create UART driver */
    return mr_driver_create(dclazz, MR_DRIVER_TYPE_UART, data, priv, ops);
}

/**
 * @brief This function deletes a UART driver.
 *
 * @param drv The UART driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_uart_del(mr_driver_t *drv) {
    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);

    /* Delete driver */
    return mr_driver_del(drv);
}

/**
 * @brief This function configures a UART.
 *
 * @param drv The UART driver.
 * @param config The UART configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_uart_configure(mr_driver_t *drv, mr_uart_config_t *config);

/**
 * @brief This function gets a UART configuration.
 *
 * @param drv The UART driver.
 * @param config The UART configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_uart_configuration(mr_driver_t *drv, mr_uart_config_t *config);

/**
 * @brief This function writes an UART data.
 *
 * @param drv The UART driver.
 * @param buf The UART buffer.
 * @param size The UART buffer size.
 * @return The actual size of written data on success, a negative error code on
 * failure.
 */
MR_INLINE mr_ssize_t mr_uart_write(mr_driver_t *drv, const void *buf,
                                   mr_size_t size) {
    const mr_uart_ops_t *ur_ops;
    mr_size_t count;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT((buf != MR_NULL) || (size == 0));

    /* Get UART operations */
    ur_ops = (mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Write UART data */
    for (count = 0; count < size; count++) {
        do {
            ret = ur_ops->write(drv, ((const mr_uint8_t *)buf)[count]);
        } while (ret == (-MR_EAGAIN));
        if (ret != 0) {
            return (count > 0) ? (mr_ssize_t)count : ret;
        }
    }
    return (mr_ssize_t)count;
}

/**
 * @brief This function reads an UART data.
 *
 * @param drv The UART driver.
 * @param buf The UART buffer.
 * @param size The UART buffer size.
 * @return The actual size of read data on success, a negative error code on
 * failure.
 */
MR_INLINE mr_ssize_t mr_uart_read(mr_driver_t *drv, void *buf, mr_size_t size) {
    const mr_uart_ops_t *ur_ops;
    mr_size_t count;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT((buf != MR_NULL) || (size == 0));

    /* Get UART operations */
    ur_ops = (mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Read UART data */
    for (count = 0; count < size; count++) {
        do {
            ret = ur_ops->read(drv, &((mr_uint8_t *)buf)[count]);
        } while (ret == (-MR_EAGAIN));
        if (ret != 0) {
            return (count > 0) ? (mr_ssize_t)count : ret;
        }
    }
    return (mr_ssize_t)count;
}

#if defined(MR_USE_DRIVER_UART_ASYNC)
/**
 * @brief This function writes an UART data asynchronously.
 *
 * @param drv The UART driver.
 * @param buf The UART buffer.
 * @param size The UART buffer size.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_uart_write_async(mr_driver_t *drv, const void *buf,
                                       mr_size_t size) {
    const mr_uart_ops_t *ur_ops;
    mr_bool_t busy;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT((buf != MR_NULL) || (size == 0));

    /* Get UART operations */
    ur_ops = (mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if UART TX async is busy */
    if (ur_ops->tx_async_is_busy) {
        ret = ur_ops->tx_async_is_busy(drv, &busy);
        if (ret != 0) {
            return ret;
        }
        if (busy) {
            return -MR_EBUSY;
        }
    } else {
        /* Not supported */
        return -MR_ENOSYS;
    }

    /* Start UART TX async transfer */
    return ur_ops->tx_async_control(drv, MR_TRUE, buf, size);
}

/**
 * @brief This function reads an UART data asynchronously.
 *
 * @param drv The UART driver.
 * @param buf The UART buffer.
 * @param size The UART buffer size.
 * @param circular The circular mode.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_uart_read_async(mr_driver_t *drv, void *buf,
                                      mr_size_t size, mr_bool_t circular) {
    const mr_uart_ops_t *ur_ops;
    mr_bool_t busy;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT((buf != MR_NULL) || (size == 0));

    /* Get UART operations */
    ur_ops = (mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if UART RX async is busy */
    if (ur_ops->rx_async_is_busy) {
        ret = ur_ops->rx_async_is_busy(drv, &busy);
        if (ret != 0) {
            return ret;
        }
        if (busy) {
            return -MR_EBUSY;
        }
    } else {
        /* Not supported */
        return -MR_ENOSYS;
    }

    /* Start UART RX async transfer */
    return ur_ops->rx_async_control(drv, MR_TRUE, circular, buf, size);
}

/**
 * @brief This function checks if UART TX is busy.
 *
 * @param drv The UART driver.
 * @return MR_TRUE if TX is busy, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_uart_tx_is_busy(mr_driver_t *drv) {
    const mr_uart_ops_t *ur_ops;
    mr_bool_t busy;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get UART operations */
    ur_ops = (mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if operation is supported */
    if (!ur_ops->tx_async_is_busy) {
        return MR_FALSE;
    }

    /* Get TX busy status */
    ret = ur_ops->tx_async_is_busy(drv, &busy);
    if (ret != 0) {
        return MR_FALSE;
    }
    return busy;
}

/**
 * @brief This function checks if UART RX is busy.
 *
 * @param drv The UART driver.
 * @return MR_TRUE if RX is busy, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_uart_rx_is_busy(mr_driver_t *drv) {
    const mr_uart_ops_t *ur_ops;
    mr_bool_t busy;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get UART operations */
    ur_ops = (mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if operation is supported */
    if (!ur_ops->rx_async_is_busy) {
        return MR_FALSE;
    }

    /* Get RX busy status */
    ret = ur_ops->rx_async_is_busy(drv, &busy);
    if (ret != 0) {
        return MR_FALSE;
    }
    return busy;
}

/**
 * @brief This function aborts a UART async write.
 *
 * @param drv The UART driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_uart_write_abort(mr_driver_t *drv) {
    const mr_uart_ops_t *ur_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get UART operations */
    ur_ops = (mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if operation is supported */
    if (!ur_ops->tx_async_control) {
        return -MR_ENOSYS;
    }

    /* Abort UART TX async transfer */
    return ur_ops->tx_async_control(drv, MR_FALSE, MR_NULL, 0);
}

/**
 * @brief This function aborts a UART async read.
 *
 * @param drv The UART driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_uart_read_abort(mr_driver_t *drv) {
    const mr_uart_ops_t *ur_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get UART operations */
    ur_ops = (mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if operation is supported */
    if (!ur_ops->rx_async_control) {
        return -MR_ENOSYS;
    }

    /* Abort UART RX async transfer */
    return ur_ops->rx_async_control(drv, MR_FALSE, MR_FALSE, MR_NULL, 0);
}

/**
 * @brief This function gets the current TX async offset.
 *
 * @param drv The UART driver.
 * @param offset The UART TX async offset.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_uart_tx_offset(mr_driver_t *drv, mr_size_t *offset) {
    const mr_uart_ops_t *ur_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(offset != MR_NULL);

    /* Get UART operations */
    ur_ops = (mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if operation is supported */
    if (!ur_ops->tx_async_offset) {
        return -MR_ENOSYS;
    }

    /* Get UART TX async offset */
    return ur_ops->tx_async_offset(drv, offset);
}

/**
 * @brief This function gets the current RX async offset.
 *
 * @param drv The UART driver.
 * @param offset The UART RX async offset.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_uart_rx_offset(mr_driver_t *drv, mr_size_t *offset) {
    const mr_uart_ops_t *ur_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(offset != MR_NULL);

    /* Get UART operations */
    ur_ops = (mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if operation is supported */
    if (!ur_ops->rx_async_offset) {
        return -MR_ENOSYS;
    }

    /* Get UART RX async offset */
    return ur_ops->rx_async_offset(drv, offset);
}
#endif /* defined(MR_USE_DRIVER_UART_ASYNC) */

/**
 * @brief This function attaches a UART interrupt.
 *
 * @param drv The UART driver.
 * @param entry The uart interrupt entry.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_uart_irq_attach(mr_driver_t *drv, mr_waiter_t *entry) {
    mr_uart_data_t *ur_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(entry != MR_NULL);

    /* Get UART data */
    ur_data = (mr_uart_data_t *)MR_DRIVER_DATA(drv);

    /* Attach UART irq */
    return mr_waiter_wait(entry, &ur_data->irq);
}

/**
 * @brief This function handles a UART interrupt.
 *
 * @param drv The UART driver.
 * @param events The interrupt events.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_uart_irq_handle(mr_driver_t *drv, mr_uint64_t events) {
    mr_uart_data_t *ur_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get UART data */
    ur_data = (mr_uart_data_t *)MR_DRIVER_DATA(drv);

    /* Raise UART irq events */
    return mr_waitable_raise(&ur_data->irq, events);
}

/**
 * @brief This function puts a UART character in fifo mode.
 *
 * @param drv The UART driver.
 * @param c The character.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_uart_putchar(mr_driver_t *drv, char c) {
    mr_ssize_t ret;

    /* Putchar */
    ret = mr_uart_write(drv, &c, sizeof(c));
    if (ret < 0) {
        return (mr_err_t)ret;
    }
    return (ret == sizeof(c)) ? 0 : -MR_EAGAIN;
}

/**
 * @brief This function gets a UART driver.
 *
 * @param drv The UART driver.
 * @return The UART driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_uart_get(mr_driver_t *drv) {
    /* Get driver reference */
    return mr_driver_get(drv);
}

/**
 * @brief This function puts a UART driver.
 *
 * @param drv The UART driver.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_uart_put(mr_driver_t *drv) {
    /* Put driver reference */
    return mr_driver_put(drv);
}

/* ================================ HardTree ================================ */

/**
 * @brief This macro function installs a UART instance through hardtree.
 *
 * @param _id The instance id.
 * @param _priv The UART private data.
 * @param _ops The UART operations.
 */
#define MR_HT_INST_UART_INSTALL(_id, _priv, _ops)                              \
    static mr_uart_data_t MR_CAT(__mr_uart_data_, _id) = MR_UART_DATA_INIT(    \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), baud_rate, 115200),         \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), data_bits,                  \
                           MR_UART_DATA_BITS_8),                               \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), stop_bits,                  \
                           MR_UART_STOP_BITS_1),                               \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), parity,                     \
                           MR_UART_PARITY_NONE),                               \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), bits_order,                 \
                           MR_UART_BITS_ORDER_LSB),                            \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), nrz, MR_UART_NRZ_NONE),     \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), flow_ctrl,                  \
                           MR_UART_FLOW_CTRL_NONE));                           \
    MR_HT_INST_DRIVER_INSTALL(                                                 \
        _id, MR_DRIVER_CLAZZES_STATIK(MR_DRIVER_CLAZZES_FIND(uart)),           \
        MR_DRIVER_TYPE_UART, &MR_CAT(__mr_uart_data_, _id), _priv, _ops)
#endif /* defined(MR_USE_DRIVER_UART) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_UART_H__ */

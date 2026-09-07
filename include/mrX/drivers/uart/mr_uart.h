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
#define MR_UART_WAIT_RX_STREAM_HALF     (1U << 0)
#define MR_UART_WAIT_RX_STREAM_FULL     (1U << 1)
#define MR_UART_WAIT_RX_IDLE            (1U << 2)
#define MR_UART_WAIT_TX_STREAM_HALF     (1U << 3)
#define MR_UART_WAIT_TX_STREAM_FULL     (1U << 4)
#define MR_UART_WAIT_TX_COMPLETE        (1U << 5)
#define MR_UART_WAIT_RX_ERROR           (1U << 6)
#define MR_UART_WAIT_TX_ERROR           (1U << 7)

/* UART data bits definition */
typedef enum mr_uart_data_bits {
    MR_UART_DATA_BITS_5 = 5,
    MR_UART_DATA_BITS_6,
    MR_UART_DATA_BITS_7,
    MR_UART_DATA_BITS_8,
} mr_uart_data_bits_t;

/* UART stop bits definition */
typedef enum mr_uart_stop_bits {
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

/* UART flow control definition */
typedef enum mr_uart_flow_control {
    MR_UART_FLOW_CONTROL_NONE,
    MR_UART_FLOW_CONTROL_RTS,
    MR_UART_FLOW_CONTROL_CTS,
    MR_UART_FLOW_CONTROL_RTS_CTS,
} mr_uart_flow_control_t;

/* UART configuration type */
typedef struct mr_uart_config {
    mr_uint32_t baud_rate;
    mr_uint8_t data_bits;
    mr_uint8_t stop_bits;
    mr_uint8_t parity;
    mr_uint8_t flow_control;
} mr_uart_config_t;

/* UART data type */
MR_DRIVER_CLAZZES_EXPORT(uart);
typedef struct mr_uart_data {
    mr_waitable_t event;
} mr_uart_data_t;

/* UART operations type */
typedef struct mr_uart_ops {
    mr_err_t (*configure)(const mr_driver_t *drv,
                          const mr_uart_config_t *config);
    mr_err_t (*configuration)(const mr_driver_t *drv, mr_uart_config_t *config);
    mr_ssize_t (*read)(const mr_driver_t *drv, void *buf, mr_size_t size);
    mr_ssize_t (*write)(const mr_driver_t *drv, const void *buf,
                        mr_size_t size);
#if defined(MR_USE_DRIVER_UART_STREAM)
    mr_err_t (*rx_stream_start)(const mr_driver_t *drv, void *buf,
                                mr_size_t size, mr_bool_t circular);
    mr_err_t (*rx_stream_stop)(const mr_driver_t *drv);
    mr_err_t (*rx_stream_is_busy)(const mr_driver_t *drv, mr_bool_t *busy);
    mr_err_t (*rx_stream_offset)(const mr_driver_t *drv, mr_size_t *offset);
    mr_err_t (*tx_stream_start)(const mr_driver_t *drv, const void *buf,
                                mr_size_t size, mr_bool_t circular);
    mr_err_t (*tx_stream_stop)(const mr_driver_t *drv);
    mr_err_t (*tx_stream_is_busy)(const mr_driver_t *drv, mr_bool_t *busy);
    mr_err_t (*tx_stream_offset)(const mr_driver_t *drv, mr_size_t *offset);
#endif /* defined(MR_USE_DRIVER_UART_STREAM) */
} mr_uart_ops_t;

/**
 * @brief This macro function defines a UART driver.
 *
 * @param _name The UART driver name.
 * @param _data The UART driver data.
 * @param _priv The UART private data.
 * @param _ops The UART operations.
 */
#define MR_UART_DEFINE(_name, _data, _priv, _ops)                              \
    MR_DRIVER_DEFINE(_name, MR_DRIVER_CLAZZES_STATIK(uart),                    \
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
    MR_DRIVER_INSTALL(_name, MR_DRIVER_CLAZZES_STATIK(uart),                   \
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
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_STATIK(uart);

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
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_DYNAMIC(uart);

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
mr_err_t mr_uart_configure(mr_driver_t *drv, const mr_uart_config_t *config);

/**
 * @brief This function gets a UART configuration.
 *
 * @param drv The UART driver.
 * @param config The UART configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_uart_configuration(mr_driver_t *drv, mr_uart_config_t *config);

/**
 * @brief This function reads UART data.
 *
 * @param drv The UART driver.
 * @param buf The receive buffer.
 * @param size The buffer size in bytes.
 * @return The actual size of read data on success, a negative error code
 * on failure.
 */
MR_INLINE mr_ssize_t mr_uart_read(mr_driver_t *drv, void *buf, mr_size_t size) {
    const mr_uart_ops_t *ua_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT((buf != MR_NULL) || (size == 0));

    /* Get UART operations */
    ua_ops = (const mr_uart_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(ua_ops->read != MR_NULL);

    /* Handle empty transfer */
    if (size == 0) {
        return 0;
    }

    /* Read UART data */
    return ua_ops->read(drv, buf, size);
}

/**
 * @brief This function writes UART data.
 *
 * @param drv The UART driver.
 * @param buf The transmit buffer.
 * @param size The buffer size in bytes.
 * @return The actual size of written data on success, a negative error code
 * on failure.
 */
MR_INLINE mr_ssize_t mr_uart_write(mr_driver_t *drv, const void *buf,
                                   mr_size_t size) {
    const mr_uart_ops_t *ua_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT((buf != MR_NULL) || (size == 0));

    /* Get UART operations */
    ua_ops = (const mr_uart_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(ua_ops->write != MR_NULL);

    /* Handle empty transfer */
    if (size == 0) {
        return 0;
    }

    /* Write UART data */
    return ua_ops->write(drv, buf, size);
}

/**
 * @brief This function writes a UART character.
 *
 * @param drv The UART driver.
 * @param ch The UART character.
 * @return The actual size of written data on success, a negative error code
 * on failure.
 */
MR_INLINE mr_ssize_t mr_uart_putchar(mr_driver_t *drv, mr_uint8_t ch) {
    /* Write UART character */
    return mr_uart_write(drv, &ch, sizeof(ch));
}

/**
 * @brief This function reads a UART character.
 *
 * @param drv The UART driver.
 * @param ch The UART character.
 * @return The actual size of read data on success, a negative error code
 * on failure.
 */
MR_INLINE mr_ssize_t mr_uart_getchar(mr_driver_t *drv, mr_uint8_t *ch) {
    /* Check parameter */
    MR_ASSERT(ch != MR_NULL);

    /* Read UART character */
    return mr_uart_read(drv, ch, sizeof(*ch));
}

#if defined(MR_USE_DRIVER_UART_STREAM)
/**
 * @brief This function starts a UART RX stream.
 *
 * @param drv The UART driver.
 * @param buf The RX buffer.
 * @param size The buffer size in bytes.
 * @param circular The circular mode.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_uart_rx_stream_start(mr_driver_t *drv, void *buf,
                                           mr_size_t size, mr_bool_t circular) {
    const mr_uart_ops_t *ua_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(buf != MR_NULL);
    MR_ASSERT(size != 0);

    /* Get UART operations */
    ua_ops = (const mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if stream operations are supported */
    if ((!ua_ops->rx_stream_start) || (!ua_ops->rx_stream_stop)
        || (!ua_ops->rx_stream_is_busy) || (!ua_ops->rx_stream_offset)) {
        return -MR_ENOSYS;
    }

    /* Start UART stream */
    return ua_ops->rx_stream_start(drv, buf, size, circular);
}

/**
 * @brief This function checks if a UART RX stream is busy.
 *
 * @param drv The UART driver.
 * @param busy The stream busy status, valid on success.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_uart_rx_stream_is_busy(mr_driver_t *drv,
                                             mr_bool_t *busy) {
    const mr_uart_ops_t *ua_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(busy != MR_NULL);

    /* Get UART operations */
    ua_ops = (const mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if operation is supported */
    if (!ua_ops->rx_stream_is_busy) {
        return -MR_ENOSYS;
    }

    /* Get stream busy status */
    return ua_ops->rx_stream_is_busy(drv, busy);
}

/**
 * @brief This function stops a UART RX stream.
 *
 * @param drv The UART driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_uart_rx_stream_stop(mr_driver_t *drv) {
    const mr_uart_ops_t *ua_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get UART operations */
    ua_ops = (const mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if operation is supported */
    if (!ua_ops->rx_stream_stop) {
        return -MR_ENOSYS;
    }

    /* Stop UART stream */
    return ua_ops->rx_stream_stop(drv);
}

/**
 * @brief This function gets the UART RX stream offset.
 *
 * @param drv The UART driver.
 * @param offset The UART RX stream offset.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_uart_rx_stream_offset(mr_driver_t *drv,
                                            mr_size_t *offset) {
    const mr_uart_ops_t *ua_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(offset != MR_NULL);

    /* Get UART operations */
    ua_ops = (const mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if operation is supported */
    if (!ua_ops->rx_stream_offset) {
        return -MR_ENOSYS;
    }

    /* Get UART stream offset */
    return ua_ops->rx_stream_offset(drv, offset);
}

/**
 * @brief This function starts a UART TX stream.
 *
 * @param drv The UART driver.
 * @param buf The TX buffer.
 * @param size The buffer size in bytes.
 * @param circular The circular mode.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_uart_tx_stream_start(mr_driver_t *drv, const void *buf,
                                           mr_size_t size, mr_bool_t circular) {
    const mr_uart_ops_t *ua_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(buf != MR_NULL);
    MR_ASSERT(size != 0);

    /* Get UART operations */
    ua_ops = (const mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if stream operations are supported */
    if ((!ua_ops->tx_stream_start) || (!ua_ops->tx_stream_stop)
        || (!ua_ops->tx_stream_is_busy) || (!ua_ops->tx_stream_offset)) {
        return -MR_ENOSYS;
    }

    /* Start UART stream */
    return ua_ops->tx_stream_start(drv, buf, size, circular);
}

/**
 * @brief This function checks if a UART TX stream is busy.
 *
 * @param drv The UART driver.
 * @param busy The stream busy status, valid on success.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_uart_tx_stream_is_busy(mr_driver_t *drv,
                                             mr_bool_t *busy) {
    const mr_uart_ops_t *ua_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(busy != MR_NULL);

    /* Get UART operations */
    ua_ops = (const mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if operation is supported */
    if (!ua_ops->tx_stream_is_busy) {
        return -MR_ENOSYS;
    }

    /* Get stream busy status */
    return ua_ops->tx_stream_is_busy(drv, busy);
}

/**
 * @brief This function stops a UART TX stream.
 *
 * @param drv The UART driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_uart_tx_stream_stop(mr_driver_t *drv) {
    const mr_uart_ops_t *ua_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get UART operations */
    ua_ops = (const mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if operation is supported */
    if (!ua_ops->tx_stream_stop) {
        return -MR_ENOSYS;
    }

    /* Stop UART stream */
    return ua_ops->tx_stream_stop(drv);
}

/**
 * @brief This function gets the UART TX stream offset.
 *
 * @param drv The UART driver.
 * @param offset The UART TX stream offset.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_uart_tx_stream_offset(mr_driver_t *drv,
                                            mr_size_t *offset) {
    const mr_uart_ops_t *ua_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(offset != MR_NULL);

    /* Get UART operations */
    ua_ops = (const mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if operation is supported */
    if (!ua_ops->tx_stream_offset) {
        return -MR_ENOSYS;
    }

    /* Get UART stream offset */
    return ua_ops->tx_stream_offset(drv, offset);
}
#endif /* defined(MR_USE_DRIVER_UART_STREAM) */

/**
 * @brief This function attaches a UART event.
 *
 * @param drv The UART driver.
 * @param entry The UART event entry.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_uart_event_attach(mr_driver_t *drv, mr_waiter_t *entry) {
    mr_uart_data_t *ua_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(entry != MR_NULL);

    /* Get UART data */
    ua_data = (mr_uart_data_t *)MR_DRIVER_DATA(drv);

    /* Attach UART event */
    return mr_waiter_wait(entry, &ua_data->event);
}

/**
 * @brief This function raises UART events.
 *
 * @param drv The UART driver.
 * @param events The UART events.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_uart_event_raise(mr_driver_t *drv,
                                       mr_wait_event_t events) {
    mr_uart_data_t *ua_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get UART data */
    ua_data = (mr_uart_data_t *)MR_DRIVER_DATA(drv);

    /* Raise UART events */
    return mr_waitable_raise(&ua_data->event, events, MR_NULL);
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
#define MR_HT_UART_INSTALL(_id, _priv, _ops)                                   \
    static mr_uart_data_t MR_CAT(__mr_uart_data_, _id) = {0};                  \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(uart),                  \
                         MR_DRIVER_TYPE_UART, &MR_CAT(__mr_uart_data_, _id),   \
                         _priv, _ops)
#endif /* defined(MR_USE_DRIVER_UART) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_UART_H__ */

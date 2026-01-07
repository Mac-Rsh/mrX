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
#include <mrX/kernel/mr_fifo.h>
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
#define MR_UART_WAIT_TX_COMPLETE        (1ULL << 0)
#define MR_UART_WAIT_RX_OVERFLOW        (1ULL << 1)

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

/* UART irq type definition */
typedef enum mr_uart_irq_type {
    MR_UART_IRQ_TYPE_TX_INT,
    MR_UART_IRQ_TYPE_RX_INT,
} mr_uart_irq_type;

/* UART operations mode definition */
typedef enum mr_uart_ops_mode {
    MR_UART_OPS_MODE_POLL,
    MR_UART_OPS_MODE_INT,
} mr_uart_ops_mode_t;

/* UART configuration type */
typedef struct mr_uart_config {
    mr_uint32_t baud_rate: 25;
    mr_uint32_t data_bits: 2;
    mr_uint32_t stop_bits: 2;
    mr_uint32_t parity: 2;
    mr_uint32_t bits_order: 1;
} mr_uart_config_t;

/* UART fifo type */
typedef struct mr_uart_fifo {
    mr_uint32_t size;
    mr_fifo_t fifo;
} mr_uart_fifo_t;

/* UART data type */
MR_DRIVER_CLAZZES_EXPORT(uart);
typedef struct mr_uart_data {
    int (*tx)(mr_driver_t *drv, const mr_uint8_t *buf, mr_uint32_t size);
    int (*rx)(mr_driver_t *drv, mr_uint8_t *buf, mr_uint32_t size);
    mr_uint16_t tx_ops_mode: 2;
    mr_uint16_t rx_ops_mode: 2;
    mr_uart_config_t config;
    mr_uart_fifo_t tx_fifo;
    mr_uart_fifo_t rx_fifo;
    mr_waitable_t irq;
} mr_uart_data_t;

/* UART operations type */
typedef struct mr_uart_ops {
    mr_err_t (*configure)(const mr_driver_t *drv,
                          const mr_uart_config_t *config);
    mr_err_t (*write)(const mr_driver_t *drv, mr_uint8_t data);
    mr_err_t (*read)(const mr_driver_t *drv, mr_uint8_t *data);
    mr_err_t (*tx_int_control)(const mr_driver_t *drv, mr_bool_t enable);
    mr_err_t (*tx_int_is_busy)(const mr_driver_t *drv, mr_bool_t *busy);
    mr_err_t (*rx_int_control)(const mr_driver_t *drv, mr_bool_t enable);
    mr_err_t (*rx_int_is_busy)(const mr_driver_t *drv, mr_bool_t *busy);
} mr_uart_ops_t;

/**
 * @brief This macro function initializes an UART data.
 *
 * @param _tx_ops_mode The UART TX operations mode.
 * @param _rx_ops_mode The UART RX operations mode.
 * @param _baud_rate The UART baud rate.
 * @param _data_bits The UART data bits.
 * @param _stop_bits The UART stop bits.
 * @param _parity The UART parity.
 * @param _bits_order The UART bits order.
 * @param _tx_fifo_size The UART TX buffer size.
 * @param _rx_fifo_size The UART RX buffer size.
 */
#define MR_UART_DATA_INIT(_tx_ops_mode, _rx_ops_mode, _baud_rate, _data_bits,  \
                          _stop_bits, _parity, _bits_order, _tx_fifo_size,     \
                          _rx_fifo_size)                                       \
    {                                                                          \
        .tx_ops_mode = (_tx_ops_mode),                                         \
        .rx_ops_mode = (_rx_ops_mode),                                         \
        .config = {.baud_rate = (_baud_rate),                                  \
                   .data_bits = (_data_bits),                                  \
                   .stop_bits = (_stop_bits),                                  \
                   .parity = (_parity),                                        \
                   .bits_order = (_bits_order)},                               \
        .tx_fifo = {.size = (_tx_fifo_size)},                                  \
        .rx_fifo = {.size = (_rx_fifo_size)},                                  \
    }

/**
 * @brief This macro function defines an UART driver.
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
 * @brief This macro function installs an UART driver.
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
 * @brief This function initializes an UART driver.
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
 * @brief This function creates an UART driver.
 *
 * @param data The UART driver data.
 * @param priv The UART private data.
 * @param ops The UART operations.
 * @return The UART driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_uart_create(mr_uart_data_t *data, void *priv,
                                      mr_uart_ops_t *ops) {
    const mr_driver_clazzes_t *dclazzes = MR_DRIVER_CLAZZES_FIND(uart);
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_STATIK(dclazzes);

    /* Check parameter */
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Create UART driver */
    return mr_driver_create(dclazz, MR_DRIVER_TYPE_UART, data, priv, ops);
}

/**
 * @brief This function deletes an UART driver.
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
 * @brief This function configures an UART.
 *
 * @param drv The UART driver.
 * @param config The UART configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_uart_configure(mr_driver_t *drv, mr_uart_config_t *config);

/**
 * @brief This function gets an UART configuration.
 *
 * @param drv The UART driver.
 * @param config The UART configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_uart_configuration(mr_driver_t *drv, mr_uart_config_t *config);

/**
 * @brief This function configures an UART TX fifo.
 *
 * @param drv The UART driver.
 * @param want The UART TX fifo size.
 * @return The actual size of UART TX fifo on success, a negative error code on
 * failure.
 */
int mr_uart_tx_fifo_configure(mr_driver_t *drv, mr_uint32_t want);

/**
 * @brief This function gets an UART TX fifo configuration.
 *
 * @param drv The UART driver.
 * @param actual The UART TX fifo size.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_uart_tx_fifo_configuration(mr_driver_t *drv, mr_uint32_t *actual);

/**
 * @brief This function configures an UART RX fifo.
 *
 * @param drv The UART driver.
 * @param want The UART RX fifo size.
 * @return The actual size of UART RX fifo on success, a negative error code on
 * failure.
 */
int mr_uart_rx_fifo_configure(mr_driver_t *drv, mr_uint32_t want);

/**
 * @brief This function gets an UART RX fifo configuration.
 *
 * @param drv The UART driver.
 * @param actual The UART RX fifo size.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_uart_rx_fifo_configuration(mr_driver_t *drv, mr_uint32_t *actual);

/**
 * @brief This function writes an UART data.
 *
 * @param drv The UART driver.
 * @param buf The UART buffer.
 * @param size The UART buffer size.
 * @return The actual size of written data on success, a negative error code on
 * failure.
 */
MR_INLINE int mr_uart_write(mr_driver_t *drv, const void *buf,
                            mr_uint32_t size) {
    mr_uart_data_t *ur_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT((buf != MR_NULL) || (size == 0));

    /* Get UART data */
    ur_data = (mr_uart_data_t *)MR_DRIVER_DATA(drv);

    /* Write UART data */
    return ur_data->tx(drv, buf, size);
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
MR_INLINE int mr_uart_read(mr_driver_t *drv, void *buf, mr_uint32_t size) {
    mr_uart_data_t *ur_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT((buf != MR_NULL) || (size == 0));

    /* Get UART data */
    ur_data = (mr_uart_data_t *)MR_DRIVER_DATA(drv);

    /* Read UART data */
    return ur_data->rx(drv, buf, size);
}

/**
 * @brief This function puts an UART character in fifo mode.
 *
 * @param drv The UART driver.
 * @param c The character.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_uart_putchar(mr_driver_t *drv, char c) {
    int ret;

    /* Putchar */
    ret = mr_uart_write(drv, &c, sizeof(c));
    if (ret < 0) {
        return ret;
    }
    return (ret == sizeof(c)) ? 0 : -MR_ENOMEM;
}

/**
 * @brief This function gets an UART character in fifo mode.
 *
 * @param drv The UART driver.
 * @return The character on success, a negative error code on failure.
 */
MR_INLINE int mr_uart_getchar(mr_driver_t *drv) {
    int ret, c;

    /* Getchar */
    ret = mr_uart_read(drv, &c, sizeof(char));
    if (ret < 0) {
        return ret;
    }
    return (ret == sizeof(char)) ? (char)c : -MR_EAGAIN;
}

/**
 * @brief This function attaches an UART interrupt.
 *
 * @param drv The UART driver.
 * @param entry The UART interrupt entry.
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
 * @brief This function handles an UART interrupt.
 *
 * @param drv The UART driver.
 * @param irq_type The UART interrupt type.
 * @param irq_data The UART interrupt data.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_uart_irq_handle(mr_driver_t *drv, mr_uint32_t irq_type,
                            mr_uint32_t irq_data);

/**
 * @brief This function gets an UART driver.
 *
 * @param drv The UART driver.
 * @return The UART driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_uart_get(mr_driver_t *drv) {
    /* Get driver reference */
    return mr_driver_get(drv);
}

/**
 * @brief This function puts an UART driver.
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
 * @brief This macro function installs an UART instance through hardtree.
 *
 * @param _id The instance id.
 * @param _priv The UART private data.
 * @param _ops The UART operations.
 */
#define MR_HT_INST_UART_INSTALL(_id, _priv, _ops)                              \
    static mr_uart_data_t __mr_uart_data_##_id = MR_UART_DATA_INIT(            \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), tx_ops_mode,                \
                           MR_UART_OPS_MODE_POLL),                             \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), rx_ops_mode,                \
                           MR_UART_OPS_MODE_POLL),                             \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), baud_rate, 115200),         \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), data_bits,                  \
                           MR_UART_DATA_BITS_8),                               \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), stop_bits,                  \
                           MR_UART_STOP_BITS_1),                               \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), parity,                     \
                           MR_UART_PARITY_NONE),                               \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), bits_order,                 \
                           MR_UART_BITS_ORDER_LSB),                            \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), tx_fifo_size, 4),           \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), rx_fifo_size, 16));         \
    MR_HT_INST_DRIVER_INSTALL(                                                 \
        _id, MR_DRIVER_CLAZZES_STATIK(MR_DRIVER_CLAZZES_FIND(uart)),           \
        MR_DRIVER_TYPE_UART, &__mr_uart_data_##_id, _priv, _ops)
#endif /* defined(MR_USE_DRIVER_UART) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_UART_H__ */

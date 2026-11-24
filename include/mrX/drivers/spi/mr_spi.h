/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_DRIVER_SPI_H__
#define __MR_DRIVER_SPI_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_DRIVER_SPI)
#include <mrX/drivers/mr_driver.h>
#endif /* defined(MR_USE_DRIVER_SPI) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Drivers-SPI
 * @{
 */

#if defined(MR_USE_DRIVER_SPI)
/* SPI wait event definition */
#define MR_SPI_WAIT_SELECT              (1U << 0)
#define MR_SPI_WAIT_RX_READY            (1U << 1)
#define MR_SPI_WAIT_TX_READY            (1U << 2)
#define MR_SPI_WAIT_DESELECT            (1U << 3)
#define MR_SPI_WAIT_ERROR               (1U << 4)
#define MR_SPI_WAIT_RX_STREAM_HALF      (1U << 5)
#define MR_SPI_WAIT_RX_STREAM_FULL      (1U << 6)
#define MR_SPI_WAIT_TX_STREAM_HALF      (1U << 7)
#define MR_SPI_WAIT_TX_STREAM_FULL      (1U << 8)
#define MR_SPI_WAIT_STREAM_ERROR        (1U << 9)
#define MR_SPI_WAIT_STREAM_DONE         (1U << 10)

/* SPI mode definition */
typedef enum mr_spi_mode {
    MR_SPI_MODE_0,
    MR_SPI_MODE_1,
    MR_SPI_MODE_2,
    MR_SPI_MODE_3,
} mr_spi_mode_t;

/* SPI bit order definition */
typedef enum mr_spi_bit_order {
    MR_SPI_BIT_ORDER_MSB,
    MR_SPI_BIT_ORDER_LSB,
} mr_spi_bit_order_t;

/* SPI role definition */
typedef enum mr_spi_role {
    MR_SPI_ROLE_MASTER,
    MR_SPI_ROLE_SLAVE,
} mr_spi_role_t;

/* SPI configuration type */
typedef struct mr_spi_config {
    mr_uint32_t clock_rate;
    mr_uint8_t data_bits;
    mr_uint8_t mode: 2;
    mr_uint8_t bit_order: 1;
    mr_uint8_t role: 1;
} mr_spi_config_t;

/* SPI message type */
typedef struct mr_spi_msg {
    const void *tx_buf;
    mr_size_t tx_len;
    void *rx_buf;
    mr_size_t rx_len;
} mr_spi_msg_t;

#if defined(MR_USE_DRIVER_SPI_STREAM)
/* SPI stream configuration type */
typedef struct mr_spi_stream_config {
    mr_bool_t circular;
} mr_spi_stream_config_t;
#endif /* defined(MR_USE_DRIVER_SPI_STREAM) */

/* SPI event data type */
typedef struct mr_spi_event_data {
    mr_uint32_t cs;
} mr_spi_event_data_t;

/* SPI data type */
MR_DRIVER_CLAZZES_EXPORT(spi);
typedef struct mr_spi_data {
    mr_waitable_t event;
} mr_spi_data_t;

/* SPI operations type */
typedef struct mr_spi_ops {
    mr_err_t (*configure)(const mr_driver_t *drv, mr_uint32_t cs,
                          const mr_spi_config_t *config);
    mr_err_t (*configuration)(const mr_driver_t *drv, mr_uint32_t cs,
                              mr_spi_config_t *config);
    mr_ssize_t (*transfer)(const mr_driver_t *drv, mr_uint32_t cs,
                           const mr_spi_msg_t *msgs, mr_size_t num);
#if defined(MR_USE_DRIVER_SPI_STREAM)
    mr_err_t (*stream_start)(const mr_driver_t *drv, mr_uint32_t cs,
                             const mr_spi_msg_t *msg,
                             const mr_spi_stream_config_t *config);
    mr_err_t (*stream_stop)(const mr_driver_t *drv);
    mr_err_t (*stream_is_busy)(const mr_driver_t *drv, mr_bool_t *busy);
    mr_err_t (*rx_stream_offset)(const mr_driver_t *drv, mr_size_t *offset);
    mr_err_t (*tx_stream_offset)(const mr_driver_t *drv, mr_size_t *offset);
#endif /* defined(MR_USE_DRIVER_SPI_STREAM) */
    mr_ssize_t (*slave_read)(const mr_driver_t *drv, mr_uint32_t cs, void *buf,
                             mr_size_t size);
    mr_ssize_t (*slave_write)(const mr_driver_t *drv, mr_uint32_t cs,
                              const void *buf, mr_size_t size);
    mr_err_t (*event_control)(const mr_driver_t *drv, mr_uint32_t cs,
                              mr_uint32_t events, mr_bool_t enable);
} mr_spi_ops_t;

/**
 * @brief This macro function defines an SPI driver.
 *
 * @param _name The SPI driver name.
 * @param _data The SPI driver data.
 * @param _priv The SPI private data.
 * @param _ops The SPI operations.
 */
#define MR_SPI_DEFINE(_name, _data, _priv, _ops)                               \
    MR_DRIVER_DEFINE(_name, MR_DRIVER_CLAZZES_STATIK(spi), MR_DRIVER_TYPE_SPI, \
                     _data, _priv, _ops)

/**
 * @brief This macro function installs an SPI driver.
 *
 * @param _name The SPI driver name.
 * @param _data The SPI driver data.
 * @param _priv The SPI private data.
 * @param _ops The SPI operations.
 * @param _level The init level.
 */
#define MR_SPI_INSTALL(_name, _data, _priv, _ops, _level)                      \
    MR_DRIVER_INSTALL(_name, MR_DRIVER_CLAZZES_STATIK(spi),                    \
                      MR_DRIVER_TYPE_SPI, _data, _priv, _ops, _level)

/**
 * @brief This function initializes an SPI driver.
 *
 * @param drv The SPI driver.
 * @param data The SPI driver data.
 * @param priv The SPI private data.
 * @param ops The SPI operations.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_spi_init(mr_driver_t *drv, mr_spi_data_t *data,
                               void *priv, mr_spi_ops_t *ops) {
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_STATIK(spi);

    /* Check parameter */
    MR_ASSERT(drv != MR_NULL);
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Init SPI driver */
    return mr_driver_init(drv, dclazz, MR_DRIVER_TYPE_SPI, data, priv, ops);
}

/**
 * @brief This function creates an SPI driver.
 *
 * @param data The SPI driver data.
 * @param priv The SPI private data.
 * @param ops The SPI operations.
 * @return The SPI driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_spi_create(mr_spi_data_t *data, void *priv,
                                     mr_spi_ops_t *ops) {
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_DYNAMIC(spi);

    /* Check parameter */
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Create SPI driver */
    return mr_driver_create(dclazz, MR_DRIVER_TYPE_SPI, data, priv, ops);
}

/**
 * @brief This function deletes an SPI driver.
 *
 * @param drv The SPI driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_spi_del(mr_driver_t *drv) {
    /* Check parameter */
    MR_DRIVER_ASSERT_TYPE(drv, MR_DRIVER_TYPE_SPI);

    /* Delete driver */
    return mr_driver_del(drv);
}

/**
 * @brief This function configures an SPI chip-select index.
 *
 * @param drv The SPI driver.
 * @param cs The chip-select index.
 * @param config The SPI configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_spi_configure(mr_driver_t *drv, mr_uint32_t cs,
                          const mr_spi_config_t *config);

/**
 * @brief This function gets an SPI chip-select index configuration.
 *
 * @param drv The SPI driver.
 * @param cs The chip-select index.
 * @param config The SPI configuration output.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_spi_configuration(mr_driver_t *drv, mr_uint32_t cs,
                              mr_spi_config_t *config);

/**
 * @brief This function transfers SPI messages through a chip-select index.
 *
 * @param drv The SPI driver.
 * @param cs The chip-select index.
 * @param msgs The SPI message array.
 * @param num The number of messages.
 * @return The number of completed messages on success, a negative error code
 * on failure.
 *
 * @note Chip select remains active for the complete message array. Each
 * message clocks max(tx_len, rx_len) bytes. A NULL or exhausted TX buffer
 * transmits all-one frames, and a NULL or exhausted RX buffer discards
 * received frames.
 */
mr_ssize_t mr_spi_transfer(mr_driver_t *drv, mr_uint32_t cs,
                           const mr_spi_msg_t *msgs, mr_size_t num);

#if defined(MR_USE_DRIVER_SPI_STREAM)
/**
 * @brief This function starts a single-message SPI stream.
 *
 * @param drv The SPI driver.
 * @param cs The chip-select index.
 * @param msg The SPI message.
 * @param config The SPI stream configuration.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_spi_stream_start(mr_driver_t *drv, mr_uint32_t cs,
                                       const mr_spi_msg_t *msg,
                                       const mr_spi_stream_config_t *config) {
    const mr_spi_ops_t *spi_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_SPI);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(msg != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get SPI operations */
    spi_ops = (const mr_spi_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether operation is supported */
    if (!spi_ops->stream_start) {
        return -MR_ENOSYS;
    }

    /* Start SPI stream */
    return spi_ops->stream_start(drv, cs, msg, config);
}

/**
 * @brief This function checks if an SPI stream is busy.
 *
 * @param drv The SPI driver.
 * @param busy The stream busy status, valid on success.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_spi_stream_is_busy(mr_driver_t *drv, mr_bool_t *busy) {
    const mr_spi_ops_t *spi_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_SPI);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(busy != MR_NULL);

    /* Get SPI operations */
    spi_ops = (const mr_spi_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether operation is supported */
    if (!spi_ops->stream_is_busy) {
        return -MR_ENOSYS;
    }

    /* Get stream busy status */
    return spi_ops->stream_is_busy(drv, busy);
}

/**
 * @brief This function stops an SPI stream.
 *
 * @param drv The SPI driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_spi_stream_stop(mr_driver_t *drv) {
    const mr_spi_ops_t *spi_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_SPI);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get SPI operations */
    spi_ops = (const mr_spi_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether operation is supported */
    if (!spi_ops->stream_stop) {
        return -MR_ENOSYS;
    }

    /* Stop SPI stream */
    return spi_ops->stream_stop(drv);
}

/**
 * @brief This function gets the SPI RX stream offset.
 *
 * @param drv The SPI driver.
 * @param offset The SPI RX stream byte offset.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_spi_rx_stream_offset(mr_driver_t *drv,
                                           mr_size_t *offset) {
    const mr_spi_ops_t *spi_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_SPI);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(offset != MR_NULL);

    /* Get SPI operations */
    spi_ops = (const mr_spi_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether operation is supported */
    if (!spi_ops->rx_stream_offset) {
        return -MR_ENOSYS;
    }

    /* Get SPI RX stream offset */
    return spi_ops->rx_stream_offset(drv, offset);
}

/**
 * @brief This function gets the SPI TX stream offset.
 *
 * @param drv The SPI driver.
 * @param offset The SPI TX stream byte offset.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_spi_tx_stream_offset(mr_driver_t *drv,
                                           mr_size_t *offset) {
    const mr_spi_ops_t *spi_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_SPI);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(offset != MR_NULL);

    /* Get SPI operations */
    spi_ops = (const mr_spi_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether operation is supported */
    if (!spi_ops->tx_stream_offset) {
        return -MR_ENOSYS;
    }

    /* Get SPI TX stream offset */
    return spi_ops->tx_stream_offset(drv, offset);
}
#endif /* defined(MR_USE_DRIVER_SPI_STREAM) */

/**
 * @brief This function reads received slave data from an SPI controller.
 *
 * @param drv The SPI driver.
 * @param cs The chip-select index.
 * @param buf The receive buffer.
 * @param size The receive size in bytes.
 * @return The actual size of read data on success, a negative error code on
 * failure.
 */
MR_INLINE mr_ssize_t mr_spi_slave_read(mr_driver_t *drv, mr_uint32_t cs,
                                       void *buf, mr_size_t size) {
    const mr_spi_ops_t *spi_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_SPI);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT((buf != MR_NULL) || (size == 0U));

    /* Get SPI operations */
    spi_ops = (const mr_spi_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether operation is supported */
    if (!spi_ops->slave_read) {
        return -MR_ENOSYS;
    }

    /* Handle empty transfer */
    if (size == 0U) {
        return 0;
    }

    /* Read SPI slave data */
    return spi_ops->slave_read(drv, cs, buf, size);
}

/**
 * @brief This function writes response data to an SPI slave controller.
 *
 * @param drv The SPI driver.
 * @param cs The chip-select index.
 * @param buf The transmit buffer.
 * @param size The transmit size in bytes.
 * @return The actual size of written data on success, a negative error code on
 * failure.
 */
MR_INLINE mr_ssize_t mr_spi_slave_write(mr_driver_t *drv, mr_uint32_t cs,
                                        const void *buf, mr_size_t size) {
    const mr_spi_ops_t *spi_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_SPI);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT((buf != MR_NULL) || (size == 0U));

    /* Get SPI operations */
    spi_ops = (const mr_spi_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether operation is supported */
    if (!spi_ops->slave_write) {
        return -MR_ENOSYS;
    }

    /* Handle empty transfer */
    if (size == 0U) {
        return 0;
    }

    /* Write SPI slave data */
    return spi_ops->slave_write(drv, cs, buf, size);
}

/**
 * @brief This function enables SPI events for a chip-select index.
 *
 * @param drv The SPI driver.
 * @param cs The chip-select index.
 * @param events The SPI event mask.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_spi_event_enable(mr_driver_t *drv, mr_uint32_t cs,
                                       mr_uint32_t events) {
    const mr_spi_ops_t *spi_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_SPI);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get SPI operations */
    spi_ops = (const mr_spi_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether event control is supported */
    if (!spi_ops->event_control) {
        return -MR_ENOSYS;
    }

    /* Enable SPI events */
    return spi_ops->event_control(drv, cs, events, MR_TRUE);
}

/**
 * @brief This function disables SPI events for a chip-select index.
 *
 * @param drv The SPI driver.
 * @param cs The chip-select index.
 * @param events The SPI event mask.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_spi_event_disable(mr_driver_t *drv, mr_uint32_t cs,
                                        mr_uint32_t events) {
    const mr_spi_ops_t *spi_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_SPI);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get SPI operations */
    spi_ops = (const mr_spi_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether event control is supported */
    if (!spi_ops->event_control) {
        return -MR_ENOSYS;
    }

    /* Disable SPI events */
    return spi_ops->event_control(drv, cs, events, MR_FALSE);
}

/**
 * @brief This function attaches an SPI event waiter.
 *
 * @param drv The SPI driver.
 * @param entry The SPI event waiter.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_spi_event_attach(mr_driver_t *drv, mr_waiter_t *entry) {
    mr_spi_data_t *spi_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_SPI);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(entry != MR_NULL);

    /* Get SPI data */
    spi_data = (mr_spi_data_t *)MR_DRIVER_DATA(drv);

    /* Attach SPI event */
    return mr_waiter_wait(entry, &spi_data->event);
}

/**
 * @brief This function raises SPI events synchronously.
 *
 * @param drv The SPI driver.
 * @param events The SPI event mask.
 * @param data The SPI event data.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_spi_event_raise(mr_driver_t *drv, mr_uint32_t events,
                                      const mr_spi_event_data_t *data) {
    mr_spi_data_t *spi_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_SPI);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(data != MR_NULL);

    /* Get SPI data */
    spi_data = (mr_spi_data_t *)MR_DRIVER_DATA(drv);

    /* Raise SPI events */
    return mr_waitable_raise(&spi_data->event, (mr_wait_event_t)events,
                             (void *)data);
}

/**
 * @brief This function gets an SPI driver.
 *
 * @param drv The SPI driver.
 * @return The SPI driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_spi_get(mr_driver_t *drv) {
    /* Get driver reference */
    return mr_driver_get(drv);
}

/**
 * @brief This function puts an SPI driver.
 *
 * @param drv The SPI driver.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_spi_put(mr_driver_t *drv) {
    /* Put driver reference */
    return mr_driver_put(drv);
}

/* ================================ HardTree ================================ */

/**
 * @brief This macro function installs an SPI instance through HardTree.
 *
 * @param _id The instance id.
 * @param _priv The SPI private data.
 * @param _ops The SPI operations.
 */
#define MR_HT_SPI_INSTALL(_id, _priv, _ops)                                    \
    static mr_spi_data_t MR_CAT(__mr_spi_data_, _id) = {0};                    \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(spi),                   \
                         MR_DRIVER_TYPE_SPI, &MR_CAT(__mr_spi_data_, _id),     \
                         _priv, _ops)

/* HardTree SPI specification type */
typedef struct mr_ht_spi_spec {
    mr_driver_t *drv;
    mr_uint32_t cs;
} mr_ht_spi_spec_t;

/**
 * @brief This function configures a HardTree SPI specification.
 *
 * @param spec The SPI specification.
 * @param config The SPI configuration.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_spi_configure(mr_ht_spi_spec_t *spec,
                                       const mr_spi_config_t *config) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Configure SPI chip-select index */
    return mr_spi_configure(spec->drv, spec->cs, config);
}

/**
 * @brief This function gets a HardTree SPI specification configuration.
 *
 * @param spec The SPI specification.
 * @param config The SPI configuration output.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_spi_configuration(mr_ht_spi_spec_t *spec,
                                           mr_spi_config_t *config) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Get SPI chip-select index configuration */
    return mr_spi_configuration(spec->drv, spec->cs, config);
}

/**
 * @brief This function transfers messages through a HardTree SPI
 * specification.
 *
 * @param spec The SPI specification.
 * @param msgs The SPI message array.
 * @param num The number of messages.
 * @return The number of completed messages on success, a negative error code
 * on failure.
 */
MR_INLINE mr_ssize_t mr_ht_spi_transfer(mr_ht_spi_spec_t *spec,
                                        const mr_spi_msg_t *msgs,
                                        mr_size_t num) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Transfer SPI messages */
    return mr_spi_transfer(spec->drv, spec->cs, msgs, num);
}

#if defined(MR_USE_DRIVER_SPI_STREAM)
/**
 * @brief This function starts a stream through a HardTree SPI specification.
 *
 * @param spec The SPI specification.
 * @param msg The SPI message.
 * @param config The SPI stream configuration.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t
    mr_ht_spi_stream_start(mr_ht_spi_spec_t *spec, const mr_spi_msg_t *msg,
                           const mr_spi_stream_config_t *config) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Start SPI stream */
    return mr_spi_stream_start(spec->drv, spec->cs, msg, config);
}

/**
 * @brief This function checks if the controller stream referenced by a
 * HardTree SPI specification is busy.
 *
 * @param spec The SPI specification.
 * @param busy The stream busy status, valid on success.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_spi_stream_is_busy(mr_ht_spi_spec_t *spec,
                                            mr_bool_t *busy) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Check SPI stream busy status */
    return mr_spi_stream_is_busy(spec->drv, busy);
}

/**
 * @brief This function stops the controller stream referenced by a HardTree
 * SPI specification.
 *
 * @param spec The SPI specification.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_spi_stream_stop(mr_ht_spi_spec_t *spec) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Stop SPI stream */
    return mr_spi_stream_stop(spec->drv);
}

/**
 * @brief This function gets the controller RX stream offset referenced by a
 * HardTree SPI specification.
 *
 * @param spec The SPI specification.
 * @param offset The SPI RX stream byte offset.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_spi_rx_stream_offset(mr_ht_spi_spec_t *spec,
                                              mr_size_t *offset) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Get SPI RX stream offset */
    return mr_spi_rx_stream_offset(spec->drv, offset);
}

/**
 * @brief This function gets the controller TX stream offset referenced by a
 * HardTree SPI specification.
 *
 * @param spec The SPI specification.
 * @param offset The SPI TX stream byte offset.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_spi_tx_stream_offset(mr_ht_spi_spec_t *spec,
                                              mr_size_t *offset) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Get SPI TX stream offset */
    return mr_spi_tx_stream_offset(spec->drv, offset);
}
#endif /* defined(MR_USE_DRIVER_SPI_STREAM) */

/**
 * @brief This function reads slave data through a HardTree SPI specification.
 *
 * @param spec The SPI specification.
 * @param buf The receive buffer.
 * @param size The receive size in bytes.
 * @return The actual size of read data on success, a negative error code on
 * failure.
 */
MR_INLINE mr_ssize_t mr_ht_spi_slave_read(mr_ht_spi_spec_t *spec, void *buf,
                                          mr_size_t size) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Read SPI slave data */
    return mr_spi_slave_read(spec->drv, spec->cs, buf, size);
}

/**
 * @brief This function writes slave data through a HardTree SPI specification.
 *
 * @param spec The SPI specification.
 * @param buf The transmit buffer.
 * @param size The transmit size in bytes.
 * @return The actual size of written data on success, a negative error code on
 * failure.
 */
MR_INLINE mr_ssize_t mr_ht_spi_slave_write(mr_ht_spi_spec_t *spec,
                                           const void *buf, mr_size_t size) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Write SPI slave data */
    return mr_spi_slave_write(spec->drv, spec->cs, buf, size);
}

/**
 * @brief This function enables events for a HardTree SPI specification.
 *
 * @param spec The SPI specification.
 * @param events The SPI event mask.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_spi_event_enable(mr_ht_spi_spec_t *spec,
                                          mr_uint32_t events) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Enable SPI events */
    return mr_spi_event_enable(spec->drv, spec->cs, events);
}

/**
 * @brief This function disables events for a HardTree SPI specification.
 *
 * @param spec The SPI specification.
 * @param events The SPI event mask.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_spi_event_disable(mr_ht_spi_spec_t *spec,
                                           mr_uint32_t events) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Disable SPI events */
    return mr_spi_event_disable(spec->drv, spec->cs, events);
}

/**
 * @brief This function attaches an event waiter through a HardTree SPI
 * specification.
 *
 * @param spec The SPI specification.
 * @param entry The SPI event waiter.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_spi_event_attach(mr_ht_spi_spec_t *spec,
                                          mr_waiter_t *entry) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Attach SPI event */
    return mr_spi_event_attach(spec->drv, entry);
}

/**
 * @brief This macro function creates an SPI specification from HardTree
 * properties.
 *
 * @param _node The node.
 * @param _prop The property name.
 * @return The initialized mr_ht_spi_spec_t structure.
 *
 * @note Property cells: [phandle, cs].
 */
#define MR_HT_NODE_SPI_SPEC(_node, _prop)                                      \
    __MR_HT_NODE_SPI_SPEC_IMPL(0, _node, _prop)
#define __MR_HT_NODE_SPI_SPEC_IMPL(_elem, _node, _prop)                        \
    {                                                                          \
        .drv                                                                   \
        = MR_HT_DRIVER_FIND(MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 0)),     \
        .cs = MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 1),                    \
    }

/**
 * @brief This macro function creates an SPI specification from HardTree
 * properties or returns a default initializer if the property does not exist.
 *
 * @param _node The node.
 * @param _prop The property name.
 * @param ... The default initializer, e.g. `{0}`.
 * @return The initialized mr_ht_spi_spec_t structure or default initializer.
 */
#define MR_HT_NODE_SPI_SPEC_OR(_node, _prop, ...)                              \
    MR_MACRO_IF(MR_HT_NODE_HAS_PROP(_node, _prop),                             \
                (MR_HT_NODE_SPI_SPEC(_node, _prop)), (__VA_ARGS__))

/**
 * @brief This macro function creates SPI specifications from a HardTree
 * property array.
 *
 * @param _node The node.
 * @param _prop The property name.
 *
 * @note Property cells: [phandle, cs].
 */
#define MR_HT_NODE_SPI_SPEC_FOREACH(_node, _prop)                              \
    MR_HT_NODE_PROP_ELEM_FOREACH(_node, _prop, __MR_HT_NODE_SPI_SPEC_IMPL)

/**
 * @brief This macro function creates an SPI specification from HardTree driver
 * properties.
 *
 * @param _id The driver instance id.
 * @param _prop The property name.
 * @return The initialized mr_ht_spi_spec_t structure.
 *
 * @note Property cells: [phandle, cs].
 */
#define MR_HT_SPI_SPEC(_id, _prop)                                             \
    MR_HT_NODE_SPI_SPEC(MR_HT_DRIVER(_id), _prop)

/**
 * @brief This macro function creates an SPI specification from HardTree driver
 * properties or returns a default initializer if the property does not exist.
 *
 * @param _id The driver instance id.
 * @param _prop The property name.
 * @param ... The default initializer, e.g. `{0}`.
 * @return The initialized mr_ht_spi_spec_t structure or default initializer.
 */
#define MR_HT_SPI_SPEC_OR(_id, _prop, ...)                                     \
    MR_HT_NODE_SPI_SPEC_OR(MR_HT_DRIVER(_id), _prop, __VA_ARGS__)

/**
 * @brief This macro function creates SPI specifications from a HardTree driver
 * property array.
 *
 * @param _id The driver instance id.
 * @param _prop The property name.
 *
 * @note Property cells: [phandle, cs].
 */
#define MR_HT_SPI_SPEC_FOREACH(_id, _prop)                                     \
    MR_HT_NODE_SPI_SPEC_FOREACH(MR_HT_DRIVER(_id), _prop)
#endif /* defined(MR_USE_DRIVER_SPI) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_SPI_H__ */

/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_DRIVER_ETH_H__
#define __MR_DRIVER_ETH_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_DRIVER_ETH)
#include <mrX/drivers/mr_driver.h>
#endif /* defined(MR_USE_DRIVER_ETH) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Drivers-ETH
 * @{
 */

#if defined(MR_USE_DRIVER_ETH)
/* ETH wait event definition */
#define MR_ETH_WAIT_RX_READY            (1U << 0)
#define MR_ETH_WAIT_TX_DONE             (1U << 1)
#define MR_ETH_WAIT_RX_ERROR            (1U << 2)
#define MR_ETH_WAIT_TX_ERROR            (1U << 3)

/* ETH frame definition */
#define MR_ETH_ADDR_LEN                 (6U)
#define MR_ETH_HEADER_LEN               (14U)
#define MR_ETH_MTU_MAX                  (1500U)
#define MR_ETH_FRAME_MIN                (60U)
#define MR_ETH_FRAME_MAX                (MR_ETH_HEADER_LEN + MR_ETH_MTU_MAX)

/* ETH duplex definition */
typedef enum mr_eth_duplex {
    MR_ETH_DUPLEX_AUTO,
    MR_ETH_DUPLEX_HALF,
    MR_ETH_DUPLEX_FULL,
} mr_eth_duplex_t;

/* ETH speed definition */
typedef enum mr_eth_speed {
    MR_ETH_SPEED_OFF,
    MR_ETH_SPEED_AUTO,
    MR_ETH_SPEED_10M,
    MR_ETH_SPEED_100M,
    MR_ETH_SPEED_1000M,
    MR_ETH_SPEED_2_5G,
} mr_eth_speed_t;

/* ETH loopback definition */
typedef enum mr_eth_loopback {
    MR_ETH_LOOPBACK_NONE,
    MR_ETH_LOOPBACK_MAC,
    MR_ETH_LOOPBACK_PHY,
} mr_eth_loopback_t;

/* ETH flow type */
typedef struct mr_eth_flow {
    mr_uint8_t rx: 1;
    mr_uint8_t tx: 1;
} mr_eth_flow_t;

/* ETH checksum type */
typedef struct mr_eth_csum {
    mr_uint8_t rx: 1;
    mr_uint8_t tx: 1;
} mr_eth_csum_t;

/* ETH configuration type */
typedef struct mr_eth_config {
    mr_uint8_t mac[MR_ETH_ADDR_LEN];
    mr_uint8_t loopback: 2;
    mr_uint8_t duplex: 2;
    mr_uint8_t speed: 3;
    mr_eth_flow_t flow;
    mr_eth_csum_t csum;
    mr_uint16_t mtu;
} mr_eth_config_t;

/* ETH statistics type */
typedef struct mr_eth_stats {
    mr_uint64_t rx_frames;
    mr_uint64_t tx_frames;
    mr_uint64_t rx_bytes;
    mr_uint64_t tx_bytes;
    mr_uint64_t rx_errors;
    mr_uint64_t tx_errors;
} mr_eth_stats_t;

/* ETH link type */
typedef struct mr_eth_link {
    mr_eth_duplex_t duplex;
    mr_eth_speed_t speed;
    mr_eth_stats_t stats;
    mr_eth_flow_t flow;
    mr_bool_t up;
} mr_eth_link_t;

#if defined(MR_USE_DRIVER_ETH_STREAM)
/* ETH frame type */
typedef struct mr_eth_frame {
    mr_size_t capacity;
    mr_err_t status;
    mr_size_t size;
    void *owner;
    void *buf;
} mr_eth_frame_t;
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */

/* ETH data type */
MR_DRIVER_CLAZZES_EXPORT(eth);
typedef struct mr_eth_data {
    mr_eth_config_t config;
    mr_eth_stats_t stats;
    mr_waitable_t event;
} mr_eth_data_t;

/* ETH operations type */
typedef struct mr_eth_ops {
    mr_err_t (*configure)(const mr_driver_t *drv,
                          const mr_eth_config_t *config);
    mr_err_t (*link)(const mr_driver_t *drv, mr_eth_link_t *link);
    mr_ssize_t (*read)(const mr_driver_t *drv, void *buf, mr_size_t size);
    mr_ssize_t (*write)(const mr_driver_t *drv, const void *buf,
                        mr_size_t size);
#if defined(MR_USE_DRIVER_ETH_STREAM)
    mr_err_t (*rx_stream_start)(const mr_driver_t *drv);
    mr_err_t (*rx_stream_stop)(const mr_driver_t *drv);
    mr_err_t (*rx_stream_is_busy)(const mr_driver_t *drv, mr_bool_t *busy);
    mr_err_t (*rx_stream_submit)(const mr_driver_t *drv, mr_eth_frame_t *frame);
    mr_err_t (*tx_stream_start)(const mr_driver_t *drv);
    mr_err_t (*tx_stream_stop)(const mr_driver_t *drv);
    mr_err_t (*tx_stream_is_busy)(const mr_driver_t *drv, mr_bool_t *busy);
    mr_err_t (*tx_stream_submit)(const mr_driver_t *drv, mr_eth_frame_t *frame);
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
} mr_eth_ops_t;

/**
 * @brief This macro function initializes ETH data.
 *
 * @param _mtu The ETH MTU.
 * @param _speed The ETH speed.
 * @param ... The brace-enclosed ETH MAC address initializer.
 */
#define MR_ETH_DATA_INIT(_mtu, _speed, ...)                                    \
    {                                                                          \
        .config = {                                                            \
            .mac = __VA_ARGS__,                                                \
            .mtu = (_mtu),                                                     \
            .speed = (_speed),                                                 \
            .duplex = MR_ETH_DUPLEX_AUTO,                                      \
            .loopback = MR_ETH_LOOPBACK_NONE,                                  \
            .flow = {0},                                                       \
            .csum = {0},                                                       \
        },                                                                     \
    }

/**
 * @brief This macro function defines an ETH driver.
 *
 * @param _name The ETH driver name.
 * @param _data The ETH driver data.
 * @param _priv The ETH private data.
 * @param _ops The ETH operations.
 */
#define MR_ETH_DEFINE(_name, _data, _priv, _ops)                               \
    MR_DRIVER_DEFINE(_name, MR_DRIVER_CLAZZES_STATIK(eth), MR_DRIVER_TYPE_ETH, \
                     _data, _priv, _ops)

/**
 * @brief This macro function installs an ETH driver.
 *
 * @param _name The ETH driver name.
 * @param _data The ETH driver data.
 * @param _priv The ETH private data.
 * @param _ops The ETH operations.
 * @param _level The init level.
 */
#define MR_ETH_INSTALL(_name, _data, _priv, _ops, _level)                      \
    MR_DRIVER_INSTALL(_name, MR_DRIVER_CLAZZES_STATIK(eth),                    \
                      MR_DRIVER_TYPE_ETH, _data, _priv, _ops, _level)

/**
 * @brief This function initializes an ETH driver.
 *
 * @param drv The ETH driver.
 * @param data The ETH driver data.
 * @param priv The ETH private data.
 * @param ops The ETH operations.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_eth_init(mr_driver_t *drv, mr_eth_data_t *data,
                               void *priv, mr_eth_ops_t *ops) {
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_STATIK(eth);

    /* Check parameter */
    MR_ASSERT(drv != MR_NULL);
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Init ETH driver */
    return mr_driver_init(drv, dclazz, MR_DRIVER_TYPE_ETH, data, priv, ops);
}

/**
 * @brief This function creates an ETH driver.
 *
 * @param data The ETH driver data allocated with mr_malloc().
 * @param priv The ETH private data.
 * @param ops The ETH operations.
 * @return The ETH driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_eth_create(mr_eth_data_t *data, void *priv,
                                     mr_eth_ops_t *ops) {
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_DYNAMIC(eth);

    /* Check parameter */
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Create ETH driver */
    return mr_driver_create(dclazz, MR_DRIVER_TYPE_ETH, data, priv, ops);
}

/**
 * @brief This function deletes an ETH driver.
 *
 * @param drv The ETH driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_eth_del(mr_driver_t *drv) {
    /* Check parameter */
    MR_DRIVER_ASSERT_TYPE(drv, MR_DRIVER_TYPE_ETH);

    /* Delete driver */
    return mr_driver_del(drv);
}

/**
 * @brief This function configures an ETH driver.
 *
 * @param drv The ETH driver.
 * @param config The ETH configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_eth_configure(mr_driver_t *drv, const mr_eth_config_t *config);

/**
 * @brief This function gets the ETH configuration.
 *
 * @param drv The ETH driver.
 * @param config The ETH configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_eth_configuration(mr_driver_t *drv, mr_eth_config_t *config);

/**
 * @brief This function gets the ETH link status.
 *
 * @param drv The ETH driver.
 * @param link The ETH link state and statistics.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_eth_link(mr_driver_t *drv, mr_eth_link_t *link) {
    const mr_eth_ops_t *eth_ops;
    mr_eth_data_t *eth_data;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ETH);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(link != MR_NULL);

    /* Get ETH operations and data */
    eth_ops = (const mr_eth_ops_t *)MR_DRIVER_OPS(drv);
    eth_data = (mr_eth_data_t *)MR_DRIVER_DATA(drv);
    MR_ASSERT(eth_ops->link != MR_NULL);

    /* Get ETH link status */
    ret = eth_ops->link(drv, link);
    if (ret != 0) {
        return ret;
    }

    /* Get ETH statistics */
    link->stats = eth_data->stats;
    return 0;
}

/**
 * @brief This function reads one complete ETH frame with padding and without
 * FCS.
 *
 * @param drv The ETH driver.
 * @param buf The caller-owned receive buffer.
 * @param size The receive buffer capacity.
 * @return The frame length in bytes on success, 0 for an empty request or
 * when no frame is available, or a negative error code on failure.
 */
mr_ssize_t mr_eth_read(mr_driver_t *drv, void *buf, mr_size_t size);

/**
 * @brief This function writes one complete ETH frame without FCS.
 *
 * @param drv The ETH driver.
 * @param buf The caller-owned transmit buffer, reusable on return.
 * @param size The frame length.
 * @return The accepted frame length in bytes on success, 0 for an empty
 * request, or a negative error code on failure.
 */
mr_ssize_t mr_eth_write(mr_driver_t *drv, const void *buf, mr_size_t size);

#if defined(MR_USE_DRIVER_ETH_STREAM)
/**
 * @brief This function starts an ETH RX stream.
 *
 * @param drv The ETH driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_eth_rx_stream_start(mr_driver_t *drv) {
    const mr_eth_ops_t *eth_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ETH);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get ETH operations */
    eth_ops = (const mr_eth_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether operation is supported */
    if (!eth_ops->rx_stream_start) {
        return -MR_ENOSYS;
    }

    /* Start ETH RX stream */
    return eth_ops->rx_stream_start(drv);
}

/**
 * @brief This function stops an ETH RX stream and returns outstanding frames.
 *
 * @param drv The ETH driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_eth_rx_stream_stop(mr_driver_t *drv) {
    const mr_eth_ops_t *eth_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ETH);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get ETH operations */
    eth_ops = (const mr_eth_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether operation is supported */
    if (!eth_ops->rx_stream_stop) {
        return -MR_ENOSYS;
    }

    /* Stop ETH RX stream */
    return eth_ops->rx_stream_stop(drv);
}

/**
 * @brief This function checks if an ETH RX stream is enabled.
 *
 * @param drv The ETH driver.
 * @param busy The stream enabled status, valid on success.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_eth_rx_stream_is_busy(mr_driver_t *drv, mr_bool_t *busy) {
    const mr_eth_ops_t *eth_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ETH);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(busy != MR_NULL);

    /* Get ETH operations */
    eth_ops = (const mr_eth_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether operation is supported */
    if (!eth_ops->rx_stream_is_busy) {
        return -MR_ENOSYS;
    }

    /* Get stream busy status */
    return eth_ops->rx_stream_is_busy(drv, busy);
}

/**
 * @brief This function submits an empty frame to the ETH RX stream.
 *
 * @param drv The ETH driver.
 * @param frame The frame descriptor and writable buffer.
 * @return 0 on success, a negative error code on failure.
 * @note A successful submission transfers ownership of the frame and buffer
 * until the frame is returned through Wait.
 */
MR_INLINE mr_err_t mr_eth_rx_stream_submit(mr_driver_t *drv,
                                           mr_eth_frame_t *frame) {
    const mr_eth_ops_t *eth_ops;
    mr_eth_data_t *eth_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ETH);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(frame != MR_NULL);

    /* Get ETH operations and data */
    eth_ops = (const mr_eth_ops_t *)MR_DRIVER_OPS(drv);
    eth_data = (mr_eth_data_t *)MR_DRIVER_DATA(drv);

    /* Check whether RX stream is supported */
    if (!eth_ops->rx_stream_submit) {
        return -MR_ENOSYS;
    }

    /* Check frame capacity and buffer */
    if ((frame->capacity < (MR_ETH_HEADER_LEN + eth_data->config.mtu))
        || (frame->capacity < MR_ETH_FRAME_MIN)) {
        return -MR_ERANGE;
    }
    if (!frame->buf) {
        return -MR_EINVAL;
    }

    /* Submit RX frame */
    return eth_ops->rx_stream_submit(drv, frame);
}

/**
 * @brief This function starts an ETH TX stream.
 *
 * @param drv The ETH driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_eth_tx_stream_start(mr_driver_t *drv) {
    const mr_eth_ops_t *eth_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ETH);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get ETH operations */
    eth_ops = (const mr_eth_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether operation is supported */
    if (!eth_ops->tx_stream_start) {
        return -MR_ENOSYS;
    }

    /* Start ETH TX stream */
    return eth_ops->tx_stream_start(drv);
}

/**
 * @brief This function stops an ETH TX stream and returns outstanding frames.
 *
 * @param drv The ETH driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_eth_tx_stream_stop(mr_driver_t *drv) {
    const mr_eth_ops_t *eth_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ETH);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get ETH operations */
    eth_ops = (const mr_eth_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether operation is supported */
    if (!eth_ops->tx_stream_stop) {
        return -MR_ENOSYS;
    }

    /* Stop ETH TX stream */
    return eth_ops->tx_stream_stop(drv);
}

/**
 * @brief This function checks if an ETH TX stream is enabled.
 *
 * @param drv The ETH driver.
 * @param busy The stream enabled status, valid on success.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_eth_tx_stream_is_busy(mr_driver_t *drv, mr_bool_t *busy) {
    const mr_eth_ops_t *eth_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ETH);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(busy != MR_NULL);

    /* Get ETH operations */
    eth_ops = (const mr_eth_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether operation is supported */
    if (!eth_ops->tx_stream_is_busy) {
        return -MR_ENOSYS;
    }

    /* Get stream busy status */
    return eth_ops->tx_stream_is_busy(drv, busy);
}

/**
 * @brief This function submits a complete frame to the ETH TX stream.
 *
 * @param drv The ETH driver.
 * @param frame The frame containing 14 to (14 + MTU) bytes, excluding FCS.
 * @return 0 on success, a negative error code on failure.
 * @note A successful submission transfers ownership of the frame and buffer
 * until the frame is returned through Wait.
 */
MR_INLINE mr_err_t mr_eth_tx_stream_submit(mr_driver_t *drv,
                                           mr_eth_frame_t *frame) {
    const mr_eth_ops_t *eth_ops;
    mr_eth_data_t *eth_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ETH);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(frame != MR_NULL);

    /* Get ETH operations and data */
    eth_ops = (const mr_eth_ops_t *)MR_DRIVER_OPS(drv);
    eth_data = (mr_eth_data_t *)MR_DRIVER_DATA(drv);

    /* Check whether TX stream is supported */
    if (!eth_ops->tx_stream_submit) {
        return -MR_ENOSYS;
    }

    /* Check frame size and buffer */
    if ((frame->size > (MR_ETH_HEADER_LEN + eth_data->config.mtu))
        || (frame->size < MR_ETH_HEADER_LEN)
        || (frame->size > frame->capacity)) {
        return -MR_ERANGE;
    }
    if (!frame->buf) {
        return -MR_EINVAL;
    }

    /* Submit TX frame */
    return eth_ops->tx_stream_submit(drv, frame);
}
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */

/**
 * @brief This function attaches an ETH event waiter.
 *
 * @param drv The ETH driver.
 * @param entry The ETH event waiter.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_eth_event_attach(mr_driver_t *drv, mr_waiter_t *entry) {
    mr_eth_data_t *eth_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ETH);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(entry != MR_NULL);

    /* Get ETH data */
    eth_data = (mr_eth_data_t *)MR_DRIVER_DATA(drv);

    /* Attach ETH event */
    return mr_waiter_wait(entry, &eth_data->event);
}

/**
 * @brief This function raises ETH events synchronously through Wait.
 *
 * @param drv The ETH driver.
 * @param events The ETH event mask.
 * @param data The completed frame, or MR_NULL if no frame completed.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_eth_event_raise(mr_driver_t *drv, mr_uint32_t events,
                                      const void *data) {
#if defined(MR_USE_DRIVER_ETH_STREAM)
    const mr_eth_frame_t *frame;
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
    mr_eth_data_t *eth_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ETH);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get ETH data */
    eth_data = (mr_eth_data_t *)MR_DRIVER_DATA(drv);

    /* Count stream completions before notifying consumers */
#if defined(MR_USE_DRIVER_ETH_STREAM)
    if ((events & MR_ETH_WAIT_RX_READY) != 0U) {
        if (data != MR_NULL) {
            frame = (const mr_eth_frame_t *)data;

            /* Count received frame */
            eth_data->stats.rx_bytes += (mr_uint64_t)frame->size;
            eth_data->stats.rx_frames++;
        }
    } else if ((events & MR_ETH_WAIT_TX_DONE) != 0U) {
        if (data != MR_NULL) {
            frame = (const mr_eth_frame_t *)data;

            /* Count transmitted frame */
            eth_data->stats.tx_bytes += (mr_uint64_t)frame->size;
            eth_data->stats.tx_frames++;
        }
    } else if ((events & MR_ETH_WAIT_RX_ERROR) != 0U) {
        if (data != MR_NULL) {
            frame = (const mr_eth_frame_t *)data;

            /* Ignore normal cancellation from stream stop or shutdown */
            if (frame->status != (-MR_EPERM)) {
                eth_data->stats.rx_errors++;
            }
        } else {
            /* Count receive error */
            eth_data->stats.rx_errors++;
        }
    } else if ((events & MR_ETH_WAIT_TX_ERROR) != 0U) {
        if (data != MR_NULL) {
            frame = (const mr_eth_frame_t *)data;

            /* Ignore normal cancellation from stream stop or shutdown */
            if (frame->status != (-MR_EPERM)) {
                eth_data->stats.tx_errors++;
            }
        } else {
            /* Count transmit error */
            eth_data->stats.tx_errors++;
        }
    }
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */

    /* Raise ETH events */
    return mr_waitable_raise(&eth_data->event, (mr_wait_event_t)events, data);
}

/* ================================ HardTree ================================ */

/**
 * @brief This macro function installs an ETH instance through hardtree.
 *
 * @param _id The instance id.
 * @param _priv The ETH private data.
 * @param _ops The ETH operations.
 */
#define MR_HT_ETH_INSTALL(_id, _priv, _ops)                                    \
    static mr_eth_data_t MR_CAT(__mr_eth_data_, _id) = {                       \
        .config = {                                                            \
            .mac = MR_HT_DRIVER_PROP(_id, mac),                                \
            .loopback = MR_HT_DRIVER_PROP_OR(                                  \
                _id, loopback, MR_ETH_LOOPBACK_NONE),                          \
            .duplex = MR_HT_DRIVER_PROP_OR(_id, duplex, MR_ETH_DUPLEX_AUTO),   \
            .speed = MR_HT_DRIVER_PROP_OR(_id, speed, MR_ETH_SPEED_AUTO),      \
            .flow = {                                                          \
                .rx = MR_HT_DRIVER_PROP_OR(_id, flow_rx, 0),                   \
                .tx = MR_HT_DRIVER_PROP_OR(_id, flow_tx, 0),                   \
            },                                                                 \
            .csum = {                                                          \
                .rx = MR_HT_DRIVER_PROP_OR(_id, csum_rx, 0),                   \
                .tx = MR_HT_DRIVER_PROP_OR(_id, csum_tx, 0),                   \
            },                                                                 \
            .mtu = MR_HT_DRIVER_PROP_OR(_id, mtu, MR_ETH_MTU_MAX),             \
        },                                                                     \
    };                                                                         \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(eth),                   \
                         MR_DRIVER_TYPE_ETH, &MR_CAT(__mr_eth_data_, _id),     \
                         _priv, _ops)
#endif /* defined(MR_USE_DRIVER_ETH) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_ETH_H__ */

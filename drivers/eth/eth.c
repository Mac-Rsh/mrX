/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/eth/mr_eth.h>
#if defined(MR_USE_DRIVER_ETH)
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>

/* ETH driver clazzes(static & dynamic) */
static mr_err_t eth_init(const mr_driver_t *drv);
static mr_err_t eth_deinit(const mr_driver_t *drv);
static void eth_release(const mr_driver_t *drv);
MR_DRIVER_CLAZZES_DEFINE(eth, eth_init, eth_deinit, eth_release);

static mr_err_t eth_check_config(const mr_eth_config_t *config) {
    static const mr_uint8_t zero_mac[MR_ETH_ADDR_LEN] = {0};

    /* Reject zero and multicast MAC addresses */
    if ((mr_memcmp(config->mac, zero_mac, sizeof(zero_mac)) == 0)
        || ((config->mac[0] & 1U) != 0U)) {
        return -MR_EINVAL;
    }
    if ((mr_uint32_t)config->loopback > MR_ETH_LOOPBACK_PHY) {
        return -MR_EINVAL;
    }
    if ((mr_uint32_t)config->duplex > MR_ETH_DUPLEX_FULL) {
        return -MR_EINVAL;
    }
    if ((mr_uint32_t)config->speed > MR_ETH_SPEED_2_5G) {
        return -MR_EINVAL;
    }
    if ((config->mtu == 0U) || (config->mtu > MR_ETH_MTU_MAX)) {
        return -MR_ERANGE;
    }
    return 0;
}

mr_err_t mr_eth_configure(mr_driver_t *drv, const mr_eth_config_t *config) {
    const mr_eth_ops_t *eth_ops;
    mr_eth_data_t *eth_data;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ETH);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get ETH operations and data */
    eth_ops = (const mr_eth_ops_t *)MR_DRIVER_OPS(drv);
    eth_data = (mr_eth_data_t *)MR_DRIVER_DATA(drv);
    MR_ASSERT(eth_ops->configure != MR_NULL);

    /* Check ETH configuration */
    ret = eth_check_config(config);
    if (ret != 0) {
        return ret;
    }

    /* Configure ETH */
    ret = eth_ops->configure(drv, config);
    if (ret != 0) {
        return ret;
    }

    /* Save ETH configuration */
    mr_memset(&eth_data->stats, 0, sizeof(eth_data->stats));
    eth_data->config = *config;
    return 0;
}

mr_err_t mr_eth_configuration(mr_driver_t *drv, mr_eth_config_t *config) {
    mr_eth_data_t *eth_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ETH);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get ETH data */
    eth_data = (mr_eth_data_t *)MR_DRIVER_DATA(drv);

    /* Get ETH configuration */
    *config = eth_data->config;
    return 0;
}

mr_ssize_t mr_eth_read(mr_driver_t *drv, void *buf, mr_size_t size) {
    const mr_eth_ops_t *eth_ops;
    mr_eth_data_t *eth_data;
    mr_ssize_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ETH);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT((buf != MR_NULL) || (size == 0U));

    /* Get ETH operations */
    eth_ops = (const mr_eth_ops_t *)MR_DRIVER_OPS(drv);
    eth_data = (mr_eth_data_t *)MR_DRIVER_DATA(drv);
    MR_ASSERT(eth_ops->read != MR_NULL);

    /* Check receive buffer size */
    if (size == 0U) {
        return 0;
    }
    if (size < MR_ETH_FRAME_MIN) {
        return -MR_ERANGE;
    }

    /* Read ETH frame and account for completed traffic */
    ret = eth_ops->read(drv, buf, size);
    if (ret > 0) {
        eth_data->stats.rx_bytes += (mr_uint64_t)ret;
        eth_data->stats.rx_frames++;
    } else if ((ret < 0) && (ret != -MR_EAGAIN) && (ret != -MR_EBUSY)
               && (ret != -MR_EPERM)) {
        eth_data->stats.rx_errors++;
    }
    return ret;
}

mr_ssize_t mr_eth_write(mr_driver_t *drv, const void *buf, mr_size_t size) {
    const mr_eth_ops_t *eth_ops;
    mr_eth_data_t *eth_data;
    mr_ssize_t result;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ETH);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT((buf != MR_NULL) || (size == 0U));

    /* Get ETH operations and data */
    eth_ops = (const mr_eth_ops_t *)MR_DRIVER_OPS(drv);
    eth_data = (mr_eth_data_t *)MR_DRIVER_DATA(drv);
    MR_ASSERT(eth_ops->write != MR_NULL);

    /* Check frame size */
    if (size == 0U) {
        return 0;
    }
    if ((size > (MR_ETH_HEADER_LEN + eth_data->config.mtu))
        || (size < MR_ETH_HEADER_LEN)) {
        return -MR_ERANGE;
    }

    /* Write ETH frame and account for completed traffic */
    result = eth_ops->write(drv, buf, size);
    if (result > 0) {
        eth_data->stats.tx_frames++;
        eth_data->stats.tx_bytes += (mr_uint64_t)result;
    } else if ((result < 0) && (result != -MR_EAGAIN) && (result != -MR_EBUSY)
               && (result != -MR_EPERM)) {
        eth_data->stats.tx_errors++;
    }
    return result;
}

static mr_err_t eth_init(const mr_driver_t *drv) {
    mr_eth_data_t *eth_data;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get ETH data */
    eth_data = (mr_eth_data_t *)MR_DRIVER_DATA(drv);

    /* Init ETH event */
    ret = mr_waitable_init(&eth_data->event);
    if (ret != 0) {
        return ret;
    }
    mr_memset(&eth_data->stats, 0, sizeof(eth_data->stats));
    return 0;
}

static mr_err_t eth_deinit(const mr_driver_t *drv) {
    const mr_eth_ops_t *eth_ops;
    mr_eth_data_t *eth_data;
    mr_eth_config_t config;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get ETH operations and data */
    eth_ops = (const mr_eth_ops_t *)MR_DRIVER_OPS(drv);
    eth_data = (mr_eth_data_t *)MR_DRIVER_DATA(drv);
    MR_ASSERT(eth_ops->configure != MR_NULL);

    /* Disable ETH port */
    config = eth_data->config;
    config.speed = MR_ETH_SPEED_OFF;
    ret = eth_ops->configure(drv, &config);
    if ((ret != 0) && !MR_DRIVER_IS_DYING(drv)) {
        return ret;
    }

    /* Deinit ETH event */
    mr_waitable_del(&eth_data->event);
    return 0;
}

static void eth_release(const mr_driver_t *drv) {
    /* Free ETH data */
    mr_free(MR_DRIVER_DATA(drv));
}
#endif /* defined(MR_USE_DRIVER_ETH) */

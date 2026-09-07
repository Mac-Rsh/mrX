/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/uart/mr_uart.h>
#if defined(MR_USE_DRIVER_UART)
#include <mrX/libc/mr_malloc.h>

/* UART driver clazzes(static & dynamic) */
static mr_err_t uart_init(const mr_driver_t *drv);
static mr_err_t uart_deinit(const mr_driver_t *drv);
static void uart_release(const mr_driver_t *drv);
MR_DRIVER_CLAZZES_DEFINE(uart, uart_init, uart_deinit, uart_release);

mr_err_t mr_uart_configure(mr_driver_t *drv, const mr_uart_config_t *config) {
    const mr_uart_ops_t *ua_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get UART operations */
    ua_ops = (const mr_uart_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(ua_ops->configure != MR_NULL);

    /* Check configuration */
    if ((config->baud_rate == 0) || (config->data_bits < MR_UART_DATA_BITS_5)
        || (config->data_bits > MR_UART_DATA_BITS_8)
        || (config->stop_bits > MR_UART_STOP_BITS_2)
        || (config->parity > MR_UART_PARITY_EVEN)
        || (config->flow_control > MR_UART_FLOW_CONTROL_RTS_CTS)) {
        return -MR_EINVAL;
    }

    /* Configure UART */
    return ua_ops->configure(drv, config);
}

mr_err_t mr_uart_configuration(mr_driver_t *drv, mr_uart_config_t *config) {
    const mr_uart_ops_t *ua_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get UART operations */
    ua_ops = (const mr_uart_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(ua_ops->configuration != MR_NULL);

    /* Get UART configuration */
    return ua_ops->configuration(drv, config);
}

static mr_err_t uart_init(const mr_driver_t *drv) {
    mr_uart_data_t *ua_data;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get UART data */
    ua_data = (mr_uart_data_t *)MR_DRIVER_DATA(drv);

    /* Init UART event */
    ret = mr_waitable_init(&ua_data->event);
    if (ret != 0) {
        return ret;
    }
    return 0;
}

static mr_err_t uart_deinit(const mr_driver_t *drv) {
    mr_uart_data_t *ua_data;
#if defined(MR_USE_DRIVER_UART_STREAM)
    const mr_uart_ops_t *ua_ops;
    mr_err_t ret;
#endif /* defined(MR_USE_DRIVER_UART_STREAM) */

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get UART data */
    ua_data = (mr_uart_data_t *)MR_DRIVER_DATA(drv);

#if defined(MR_USE_DRIVER_UART_STREAM)
    /* Stop the streams before deleting the event */
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    ua_ops = (const mr_uart_ops_t *)MR_DRIVER_OPS(drv);
    if (ua_ops->rx_stream_stop) {
        ret = ua_ops->rx_stream_stop(drv);
        if (ret != 0) {
            return ret;
        }
    }
    if (ua_ops->tx_stream_stop) {
        ret = ua_ops->tx_stream_stop(drv);
        if (ret != 0) {
            return ret;
        }
    }
#endif /* defined(MR_USE_DRIVER_UART_STREAM) */

    /* Deinit UART event */
    mr_waitable_del(&ua_data->event);
    return 0;
}

static void uart_release(const mr_driver_t *drv) {
    /* Free UART data */
    mr_free(MR_DRIVER_DATA(drv));
}
#endif /* defined(MR_USE_DRIVER_UART) */

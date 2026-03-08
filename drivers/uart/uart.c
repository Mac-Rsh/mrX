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
static mr_err_t uart_statik_deinit(const mr_driver_t *drv);
static mr_err_t uart_dynamic_deinit(const mr_driver_t *drv);
MR_DRIVER_CLAZZES_DEFINE(uart, uart_init, uart_statik_deinit,
                         uart_dynamic_deinit);

mr_err_t mr_uart_configure(mr_driver_t *drv, mr_uart_config_t *config) {
    mr_uart_data_t *ur_data;
    mr_uart_ops_t *ur_ops;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get UART data and operations */
    ur_data = (mr_uart_data_t *)MR_DRIVER_DATA(drv);
    ur_ops = (mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Configure UART */
    ret = ur_ops->configure(drv, config);
    if (ret == 0) {
        ur_data->config = *config;
    }
    return ret;
}

mr_err_t mr_uart_configuration(mr_driver_t *drv, mr_uart_config_t *config) {
    mr_uart_data_t *ur_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get UART data */
    ur_data = (mr_uart_data_t *)MR_DRIVER_DATA(drv);

    /* Get UART configuration */
    *config = ur_data->config;
    return 0;
}

static mr_err_t uart_init(const mr_driver_t *drv) {
    const mr_uart_ops_t *ur_ops;
    mr_uart_data_t *ur_data;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get UART data and operations */
    ur_data = (mr_uart_data_t *)MR_DRIVER_DATA(drv);
    ur_ops = (mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Init UART irq */
    ret = mr_waitable_init(&ur_data->irq, MR_NULL, (void *)drv);
    if (ret != 0) {
        return ret;
    }

    /* Configure UART with default configuration */
    ret = ur_ops->configure(drv, &ur_data->config);
    if (ret != 0) {
        /* Mark configuration as invalid */
        ur_data->config.baud_rate = 0;
    }
    return 0;
}

static mr_err_t uart_statik_deinit(const mr_driver_t *drv) {
    const mr_uart_config_t config = {.baud_rate = 0};
    const mr_uart_ops_t *ur_ops;
    mr_uart_data_t *ur_data;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get UART data and operations */
    ur_data = (mr_uart_data_t *)MR_DRIVER_DATA(drv);
    ur_ops = (mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Deinit UART configuration */
    ret = ur_ops->configure(drv, &config);
    if (ret != 0) {
        return ret;
    }

    /* Deinit UART irq */
    mr_waitable_del(&ur_data->irq);
    return 0;
}

static mr_err_t uart_dynamic_deinit(const mr_driver_t *drv) {
    mr_err_t ret;

    /* Deinit UART */
    ret = uart_statik_deinit(drv);
    if (ret != 0) {
        return ret;
    }

    /* Free UART data */
    mr_free(MR_DRIVER_DATA(drv));
    return 0;
}
#endif /* defined(MR_USE_DRIVER_UART) */

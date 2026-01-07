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

static mr_err_t uart_fifo_alloc(mr_uart_fifo_t *fifo, mr_uint32_t want) {
    mr_uint32_t actual;

    /* Free UART fifo */
    mr_fifo_free(&fifo->fifo);
    fifo->size = 0;

    /* Allocate UART fifo */
    actual = mr_fifo_alloc(&fifo->fifo, want, 1);
    if (actual < want) {
        return -MR_ENOMEM;
    }
    fifo->size = actual;
    return 0;
}

static void uart_fifo_free(mr_uart_fifo_t *fifo) {
    /* Free UART fifo */
    mr_fifo_free(&fifo->fifo);
    fifo->size = 0;
}

static int uart_tx_fifo_configure(mr_driver_t *drv, mr_uint32_t want) {
    const mr_uart_ops_t *ur_ops;
    mr_uart_data_t *ur_data;
    mr_err_t ret;

    /* Get UART data and operations */
    ur_data = (mr_uart_data_t *)MR_DRIVER_DATA(drv);
    ur_ops = (mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if UART TX supports INT */
    if ((!ur_ops->tx_int_control) || (!ur_ops->tx_int_is_busy)) {
        return -MR_EPERM;
    }

    /* Disable UART TX INT */
    ret = ur_ops->tx_int_control(drv, MR_FALSE);
    if (ret != 0) {
        return ret;
    }

    /* Allocate UART TX fifo */
    ret = uart_fifo_alloc(&ur_data->tx_fifo, want);
    if (ret != 0) {
        return ret;
    }
    return (int)ur_data->tx_fifo.size;
}

int mr_uart_tx_fifo_configure(mr_driver_t *drv, mr_uint32_t want) {
    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Configure UART TX fifo */
    return uart_tx_fifo_configure(drv, want);
}

mr_err_t mr_uart_tx_fifo_configuration(mr_driver_t *drv, mr_uint32_t *actual) {
    mr_uart_data_t *ur_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get UART data */
    ur_data = (mr_uart_data_t *)MR_DRIVER_DATA(drv);

    /* Get UART TX fifo configuration */
    *actual = ur_data->tx_fifo.size;
    return 0;
}

static int uart_rx_fifo_configure(mr_driver_t *drv, mr_uint32_t want) {
    const mr_uart_ops_t *ur_ops;
    mr_uart_data_t *ur_data;
    mr_err_t ret;

    /* Get UART data and operations */
    ur_data = (mr_uart_data_t *)MR_DRIVER_DATA(drv);
    ur_ops = (mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if UART RX supports INT */
    if ((!ur_ops->rx_int_control) || (!ur_ops->rx_int_is_busy)) {
        return -MR_EPERM;
    }

    /* Disable UART RX INT */
    ret = ur_ops->rx_int_control(drv, MR_FALSE);
    if (ret != 0) {
        return ret;
    }

    /* Allocate UART RX fifo */
    ret = uart_fifo_alloc(&ur_data->rx_fifo, want);
    if (ret != 0) {
        return ret;
    }

    /* Enable UART RX INT */
    if (ur_data->rx_fifo.size > 0) {
        ret = ur_ops->rx_int_control(drv, MR_TRUE);
        if (ret != 0) {
            uart_fifo_free(&ur_data->rx_fifo);
            return ret;
        }
    }
    return (int)ur_data->rx_fifo.size;
}

int mr_uart_rx_fifo_configure(mr_driver_t *drv, mr_uint32_t want) {
    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Configure UART RX fifo */
    return uart_rx_fifo_configure(drv, want);
}

mr_err_t mr_uart_rx_fifo_configuration(mr_driver_t *drv, mr_uint32_t *actual) {
    mr_uart_data_t *ur_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_UART);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get UART data */
    ur_data = (mr_uart_data_t *)MR_DRIVER_DATA(drv);

    /* Get UART RX fifo configuration */
    *actual = ur_data->rx_fifo.size;
    return 0;
}

static int uart_tx_poll(mr_driver_t *drv, const mr_uint8_t *buf,
                        mr_uint32_t size) {
    const mr_uart_ops_t *ur_ops;
    mr_uint32_t count;
    mr_err_t ret;

    /* Get UART operations */
    ur_ops = (mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Write UART data */
    for (count = 0; count < size; count++) {
        do {
            ret = ur_ops->write(drv, buf[count]);
        } while (ret == (-MR_EAGAIN));
        if (ret != 0) {
            return (count > 0) ? (int)count : ret;
        }
    }
    return (int)count;
}

static int uart_tx_fifo(mr_driver_t *drv, const mr_uint8_t *buf,
                        mr_uint32_t size) {
    const mr_uart_ops_t *ur_ops;
    mr_uart_data_t *ur_data;
    mr_uint32_t count;
    mr_bool_t is_busy;
    mr_err_t ret;

    /* Get UART data and operations */
    ur_data = (mr_uart_data_t *)MR_DRIVER_DATA(drv);
    ur_ops = (mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Write UART data to fifo */
    count = mr_fifo_write(&ur_data->tx_fifo.fifo, buf, size);
    if (count == 0) {
        return 0;
    }

    /* Enable UART TX INT */
    ret = ur_ops->tx_int_is_busy(drv, &is_busy);
    if (ret != 0) {
        return ret;
    }
    if (!is_busy) {
        ur_ops->tx_int_control(drv, MR_TRUE);
    }
    return (int)count;
}

static int uart_rx_poll(mr_driver_t *drv, mr_uint8_t *buf, mr_uint32_t size) {
    const mr_uart_ops_t *ur_ops;
    mr_uint32_t count;
    mr_err_t ret;

    /* Get UART operations */
    ur_ops = (mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Read UART data */
    for (count = 0; count < size; count++) {
        do {
            ret = ur_ops->read(drv, &buf[count]);
        } while (ret == (-MR_EAGAIN));
        if (ret != 0) {
            return (count > 0) ? (int)count : ret;
        }
    }
    return (int)count;
}

static int uart_rx_fifo(mr_driver_t *drv, mr_uint8_t *buf, mr_uint32_t size) {
    mr_uart_data_t *ur_data;

    /* Get UART data */
    ur_data = (mr_uart_data_t *)MR_DRIVER_DATA(drv);

    /* Read UART data from fifo */
    return (int)mr_fifo_read(&ur_data->rx_fifo.fifo, buf, size);
}

mr_err_t mr_uart_irq_handle(mr_driver_t *drv, mr_uint32_t irq_type,
                            mr_uint32_t irq_data) {
    mr_uint32_t count, actual, i;
    const mr_uart_ops_t *ur_ops;
    mr_uart_data_t *ur_data;
    mr_uint64_t event;
    mr_uint8_t *buf;
    mr_err_t ret;

    /* Get UART data and operations */
    ur_data = (mr_uart_data_t *)MR_DRIVER_DATA(drv);
    ur_ops = (mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    switch (irq_type) {
        case MR_UART_IRQ_TYPE_TX_INT: {
            /* Check if all data has been written */
            if (mr_fifo_is_empty(&ur_data->tx_fifo.fifo)) {
                /* Disable UART TX INT */
                ur_ops->tx_int_control(drv, MR_FALSE);

                /* Raise UART TX complete event */
                event = MR_WAIT_OUT | MR_UART_WAIT_TX_COMPLETE;
                mr_waitable_raise(&ur_data->irq, event);
                return 0;
            }

            for (count = 0; count < irq_data; count += actual) {
                /* Claim buffer from fifo */
                actual = mr_fifo_read_claim(&ur_data->tx_fifo.fifo, 0,
                                            (void **)&buf, irq_data - count);
                if (actual == 0) {
                    /* All data has been written */
                    goto _exit;
                }

                /* Write buffer to UART */
                for (i = 0; i < actual; i++) {
                    ret = ur_ops->write(drv, buf[i]);
                    if (ret != 0) {
                        mr_fifo_read_finish(&ur_data->tx_fifo.fifo, i);
                        if (ret == (-MR_EAGAIN)) {
                            /* TX buffer is full */
                            goto _exit;
                        }
                        return ret;
                    }
                }

                /* Mark buffer as finished */
                mr_fifo_read_finish(&ur_data->tx_fifo.fifo, actual);
            }
            break;
        }
        case MR_UART_IRQ_TYPE_RX_INT: {
            for (count = 0; count < irq_data; count += actual) {
                /* Claim buffer from fifo */
                actual = mr_fifo_write_claim(&ur_data->rx_fifo.fifo, 0,
                                             (void **)&buf, irq_data - count);
                if (actual == 0) {
                    /* Raise UART RX overflow event */
                    event = MR_WAIT_OUT | MR_UART_WAIT_RX_OVERFLOW;
                    mr_waitable_raise(&ur_data->irq, event);

                    /* Skip one byte if fifo is still full */
                    if (mr_fifo_is_full(&ur_data->rx_fifo.fifo)) {
                        mr_fifo_skip(&ur_data->rx_fifo.fifo, 1);
                    }
                    continue;
                }

                /* Read buffer from UART */
                for (i = 0; i < actual; i++) {
                    ret = ur_ops->read(drv, &buf[i]);
                    if (ret != 0) {
                        mr_fifo_write_finish(&ur_data->rx_fifo.fifo, i);
                        if (ret == (-MR_EAGAIN)) {
                            /* RX buffer is empty */
                            goto _exit;
                        }
                        return ret;
                    }
                }

                /* Mark buffer as finished */
                mr_fifo_write_finish(&ur_data->rx_fifo.fifo, actual);
            }
            break;
        }
        default: {
            return -MR_EINVAL;
        }
    }
_exit:
    /* Complete UART waitable */
    mr_waitable_complete(&ur_data->irq);
    return 0;
}

static mr_uint64_t uart_waitable_check(mr_waitable_t *waitable, void *param) {
    mr_uart_data_t *ur_data;
    mr_uint64_t event;

    /* Get UART data */
    ur_data = (mr_uart_data_t *)MR_DRIVER_DATA((mr_driver_t *)param);
    MR_IGNORE(waitable);
    event = 0;

    /* Check if UART TX fifo is not full */
    if (!mr_fifo_is_full(&ur_data->tx_fifo.fifo)) {
        /* Can write */
        event |= MR_WAIT_OUT;
    }
    /* Check if UART RX fifo is not empty */
    if (!mr_fifo_is_empty(&ur_data->rx_fifo.fifo)) {
        /* Can read */
        event |= MR_WAIT_IN;
    }
    return event;
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

    /* Init UART TX&RX operations */
    if (ur_data->tx_ops_mode == MR_UART_OPS_MODE_INT) {
        if ((!ur_ops->tx_int_control) || (!ur_ops->tx_int_is_busy)) {
            return -MR_ENOSYS;
        }
        ur_data->tx = uart_tx_fifo;
    } else {
        ur_data->tx = uart_tx_poll;
    }
    if (ur_data->rx_ops_mode == MR_UART_OPS_MODE_INT) {
        if ((!ur_ops->rx_int_control) || (!ur_ops->rx_int_is_busy)) {
            return -MR_ENOSYS;
        }
        ur_data->rx = uart_rx_fifo;
    } else {
        ur_data->rx = uart_rx_poll;
    }

    /* Init UART TX&RX fifo */
    mr_fifo_init(&ur_data->tx_fifo.fifo, MR_NULL, 0, 1);
    mr_fifo_init(&ur_data->rx_fifo.fifo, MR_NULL, 0, 1);

    /* Init UART irq */
    ret = mr_waitable_init(&ur_data->irq, uart_waitable_check, (void *)drv);
    if (ret != 0) {
        return ret;
    }

    /* Configure UART with default configuration */
    ret = ur_ops->configure(drv, &ur_data->config);
    if (ret != 0) {
        /* Mark configuration as invalid */
        ur_data->config.baud_rate = 0;
    }

    /* Configure UART TX&RX fifo with default size */
    uart_tx_fifo_configure((mr_driver_t *)drv, ur_data->tx_fifo.size);
    uart_rx_fifo_configure((mr_driver_t *)drv, ur_data->rx_fifo.size);
    return 0;
}

static mr_err_t uart_statik_deinit(const mr_driver_t *drv) {
    const mr_uart_config_t config = {.baud_rate = 0};
    const mr_uart_ops_t *ur_ops;
    mr_uart_data_t *ur_data;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get UART data and operations */
    ur_data = (mr_uart_data_t *)MR_DRIVER_DATA(drv);
    ur_ops = (mr_uart_ops_t *)MR_DRIVER_OPS(drv);

    /* Deinit UART configuration */
    ret = ur_ops->configure(drv, &config);
    if (ret != 0) {
        return ret;
    }

    /* Free UART fifo */
    mr_fifo_free(&ur_data->tx_fifo.fifo);
    mr_fifo_free(&ur_data->rx_fifo.fifo);

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

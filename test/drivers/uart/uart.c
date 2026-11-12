/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_DRIVER_UART)
#include <mrX/drivers/uart/mr_uart.h>
#include <mrX/libc/mr_malloc.h>

static mr_uart_config_t uart_config;
static mr_size_t uart_rx_size;
static mr_size_t uart_tx_size;
static mr_bool_t uart_rx_busy;
static mr_bool_t uart_tx_busy;
static mr_err_t uart_rx_stream_stop_ret;
static mr_err_t uart_tx_stream_stop_ret;
#if defined(MR_USE_DRIVER_UART_STREAM)
static mr_size_t uart_rx_offset;
static mr_size_t uart_tx_offset;
static int uart_rx_stream_stop_count;
static int uart_tx_stream_stop_count;
#endif /* defined(MR_USE_DRIVER_UART_STREAM) */
static mr_uint32_t uart_events;
static mr_bool_t uart_event_enable_state;

static mr_err_t uart_configure(const mr_driver_t *drv,
                               const mr_uart_config_t *config) {
    MR_IGNORE(drv);
    uart_config = *config;
    return 0;
}

static mr_err_t uart_configuration(const mr_driver_t *drv,
                                   mr_uart_config_t *config) {
    MR_IGNORE(drv);
    *config = uart_config;
    return 0;
}

static mr_ssize_t uart_read(const mr_driver_t *drv, void *buf, mr_size_t size) {
    MR_IGNORE(drv);
    uart_rx_size = size;
    ((mr_uint8_t *)buf)[0] = 0x5aU;
    return (mr_ssize_t)size;
}

static mr_ssize_t uart_write(const mr_driver_t *drv, const void *buf,
                             mr_size_t size) {
    MR_IGNORE(drv);
    MR_IGNORE(buf);
    uart_tx_size = size;
    return (mr_ssize_t)size;
}

#if defined(MR_USE_DRIVER_UART_STREAM)
static mr_err_t uart_rx_stream_start(const mr_driver_t *drv, void *buf,
                                     mr_size_t size,
                                     const mr_uart_rx_stream_config_t *config) {
    MR_IGNORE(drv);
    MR_IGNORE(buf);
    MR_IGNORE(config);
    uart_rx_size = size;
    uart_rx_busy = MR_TRUE;
    return 0;
}

static mr_err_t uart_rx_stream_stop(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    uart_rx_stream_stop_count++;
    if (uart_rx_stream_stop_ret != 0) {
        return uart_rx_stream_stop_ret;
    }
    uart_rx_busy = MR_FALSE;
    return 0;
}

static mr_err_t uart_rx_stream_is_busy(const mr_driver_t *drv,
                                       mr_bool_t *busy) {
    MR_IGNORE(drv);
    *busy = uart_rx_busy;
    return 0;
}

static mr_err_t uart_rx_stream_offset(const mr_driver_t *drv,
                                      mr_size_t *offset) {
    MR_IGNORE(drv);
    *offset = uart_rx_offset;
    return 0;
}

static mr_err_t uart_tx_stream_start(const mr_driver_t *drv, const void *buf,
                                     mr_size_t size,
                                     const mr_uart_tx_stream_config_t *config) {
    MR_IGNORE(drv);
    MR_IGNORE(buf);
    MR_IGNORE(config);
    uart_tx_size = size;
    uart_tx_busy = MR_TRUE;
    return 0;
}

static mr_err_t uart_tx_stream_stop(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    uart_tx_stream_stop_count++;
    if (uart_tx_stream_stop_ret != 0) {
        return uart_tx_stream_stop_ret;
    }
    uart_tx_busy = MR_FALSE;
    return 0;
}

static mr_err_t uart_tx_stream_is_busy(const mr_driver_t *drv,
                                       mr_bool_t *busy) {
    MR_IGNORE(drv);
    *busy = uart_tx_busy;
    return 0;
}

static mr_err_t uart_tx_stream_offset(const mr_driver_t *drv,
                                      mr_size_t *offset) {
    MR_IGNORE(drv);
    *offset = uart_tx_offset;
    return 0;
}
#endif /* defined(MR_USE_DRIVER_UART_STREAM) */

static mr_err_t uart_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                   mr_bool_t enable) {
    MR_IGNORE(drv);
    uart_events = events;
    uart_event_enable_state = enable;
    return 0;
}

static const mr_uart_ops_t uart_ops = {
    .configure = uart_configure,
    .configuration = uart_configuration,
    .read = uart_read,
    .write = uart_write,
#if defined(MR_USE_DRIVER_UART_STREAM)
    .rx_stream_start = uart_rx_stream_start,
    .rx_stream_stop = uart_rx_stream_stop,
    .rx_stream_is_busy = uart_rx_stream_is_busy,
    .rx_stream_offset = uart_rx_stream_offset,
    .tx_stream_start = uart_tx_stream_start,
    .tx_stream_stop = uart_tx_stream_stop,
    .tx_stream_is_busy = uart_tx_stream_is_busy,
    .tx_stream_offset = uart_tx_stream_offset,
#endif /* defined(MR_USE_DRIVER_UART_STREAM) */
    .event_control = uart_event_control,
};

static mr_driver_t *uart_create(const mr_uart_ops_t *ops) {
    mr_uart_data_t *data = mr_malloc(sizeof(*data));

    if (data == MR_NULL) {
        return MR_NULL;
    }
    return mr_uart_create(data, MR_NULL, (mr_uart_ops_t *)ops);
}

static void uart_waiter_entry(mr_waiter_t *waiter, void *param,
                              mr_wait_event_t event, const void *data) {
    int *count = (int *)param;

    MR_IGNORE(waiter);
    MR_IGNORE(event);
    MR_IGNORE(data);
    (*count)++;
}

MR_TEST(uart, configuration_and_direct_io) {
    mr_driver_t *drv;
    mr_uart_config_t config = {
        .baud_rate = 115200U,
        .data_bits = MR_UART_DATA_BITS_8,
        .stop_bits = MR_UART_STOP_BITS_1,
        .parity = MR_UART_PARITY_NONE,
        .flow_ctrl = MR_UART_FLOW_CTRL_NONE,
        .mode = MR_UART_MODE_TX_RX,
    };
    mr_uart_config_t saved = {0};
    mr_uint8_t value = 0x11U;

    uart_rx_size = 99U;
    uart_tx_size = 99U;
    drv = uart_create(&uart_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_uart_configure(drv, &config), 0);
    MR_TEST_ASSERT_EQ_INT(mr_uart_configuration(drv, &saved), 0);
    MR_TEST_ASSERT_EQ_UINT(saved.baud_rate, 115200U);
    MR_TEST_ASSERT_EQ_INT(mr_uart_write(drv, &value, 1U), 1);
    MR_TEST_ASSERT_EQ_UINT(uart_tx_size, 1U);
    MR_TEST_ASSERT_EQ_INT(mr_uart_read(drv, &value, 1U), 1);
    MR_TEST_ASSERT_EQ_INT(value, 0x5aU);
    MR_TEST_ASSERT_EQ_INT(mr_uart_putchar(drv, 0x33U), 1);
    MR_TEST_ASSERT_EQ_INT(mr_uart_getchar(drv, &value), 1);
    MR_TEST_ASSERT_EQ_INT(mr_uart_write(drv, MR_NULL, 0U), 0);
    MR_TEST_ASSERT_EQ_INT(mr_uart_read(drv, MR_NULL, 0U), 0);
    mr_uart_del(drv);
}

#if defined(MR_USE_DRIVER_UART_STREAM)
MR_TEST(uart, stream_paths_and_optional_operations) {
    mr_driver_t *drv;
    mr_uart_ops_t optional_ops = uart_ops;
    mr_uint8_t buffer[8] = {0};
    mr_uart_rx_stream_config_t rx_config = {.circular = MR_TRUE};
    mr_uart_tx_stream_config_t tx_config = {.circular = MR_FALSE};
    mr_bool_t busy;
    mr_size_t offset;

    uart_rx_busy = MR_FALSE;
    uart_tx_busy = MR_FALSE;
    uart_rx_offset = 3U;
    uart_tx_offset = 5U;
    uart_rx_stream_stop_ret = 0;
    uart_tx_stream_stop_ret = 0;
    uart_rx_stream_stop_count = 0;
    uart_tx_stream_stop_count = 0;
    drv = uart_create(&uart_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_uart_rx_stream_start(drv, buffer, sizeof(buffer), &rx_config), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_uart_tx_stream_start(drv, buffer, sizeof(buffer), &tx_config), 0);
    MR_TEST_ASSERT_EQ_INT(mr_uart_rx_stream_is_busy(drv, &busy), 0);
    MR_TEST_ASSERT_EQ_INT(busy, MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_uart_tx_stream_is_busy(drv, &busy), 0);
    MR_TEST_ASSERT_EQ_INT(busy, MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_uart_rx_stream_offset(drv, &offset), 0);
    MR_TEST_ASSERT_EQ_UINT(offset, 3U);
    MR_TEST_ASSERT_EQ_INT(mr_uart_tx_stream_offset(drv, &offset), 0);
    MR_TEST_ASSERT_EQ_UINT(offset, 5U);
    MR_TEST_ASSERT_EQ_INT(mr_uart_rx_stream_stop(drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_uart_tx_stream_stop(drv), 0);

    optional_ops.rx_stream_start = MR_NULL;
    optional_ops.tx_stream_start = MR_NULL;
    drv->ops = (mr_ptr_t)&optional_ops;
    MR_TEST_ASSERT_EQ_INT(
        mr_uart_rx_stream_start(drv, buffer, sizeof(buffer), &rx_config),
        -MR_ENOSYS);
    MR_TEST_ASSERT_EQ_INT(
        mr_uart_tx_stream_start(drv, buffer, sizeof(buffer), &tx_config),
        -MR_ENOSYS);
    drv->ops = (mr_ptr_t)&uart_ops;
    mr_uart_del(drv);
}

MR_TEST(uart, stream_stop_errors_on_shutdown_and_delete) {
    mr_driver_t *drv;

    uart_rx_stream_stop_ret = -MR_EIO;
    uart_tx_stream_stop_ret = 0;
    uart_rx_stream_stop_count = 0;
    uart_tx_stream_stop_count = 0;
    drv = uart_create(&uart_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    /* Normal shutdown propagates the first stream stop error */
    MR_TEST_ASSERT_EQ_INT(mr_driver_shutdown(drv), -MR_EIO);
    MR_TEST_ASSERT_EQ_INT(uart_rx_stream_stop_count, 1);
    MR_TEST_ASSERT_EQ_INT(uart_tx_stream_stop_count, 0);
    MR_TEST_ASSERT_EQ_INT(mr_driver_is_ready(drv), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(mr_driver_state(drv), -MR_EIO);
    MR_TEST_ASSERT_NE_INT(MR_DRIVER_IS_SETUP(drv), 0);

    /* A later TX stop error is also propagated */
    uart_rx_stream_stop_ret = 0;
    uart_tx_stream_stop_ret = -MR_EIO;
    MR_TEST_ASSERT_EQ_INT(mr_driver_shutdown(drv), -MR_EIO);
    MR_TEST_ASSERT_EQ_INT(uart_rx_stream_stop_count, 2);
    MR_TEST_ASSERT_EQ_INT(uart_tx_stream_stop_count, 1);
    MR_TEST_ASSERT_EQ_INT(mr_driver_is_ready(drv), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(mr_driver_state(drv), -MR_EIO);
    MR_TEST_ASSERT_NE_INT(MR_DRIVER_IS_SETUP(drv), 0);

    /* Deletion ignores backend stop errors once the driver is dying */
    uart_rx_stream_stop_ret = -MR_EIO;
    MR_TEST_ASSERT_EQ_INT(mr_uart_del(drv), 0);
    MR_TEST_ASSERT_EQ_INT(uart_rx_stream_stop_count, 3);
    MR_TEST_ASSERT_EQ_INT(uart_tx_stream_stop_count, 2);
    uart_rx_stream_stop_ret = 0;
    uart_tx_stream_stop_ret = 0;
}
#endif /* defined(MR_USE_DRIVER_UART_STREAM) */

MR_TEST(uart, static_reference) {
    mr_uart_ops_t ops = uart_ops;
    mr_uart_data_t data = {0};
    mr_driver_t drv;

    uart_rx_busy = MR_FALSE;
    uart_tx_busy = MR_FALSE;
    uart_rx_stream_stop_ret = 0;
    uart_tx_stream_stop_ret = 0;
    MR_TEST_ASSERT_EQ_INT(mr_uart_init(&drv, &data, MR_NULL, &ops), 0);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(&drv), 0);
    MR_TEST_ASSERT_EQ_PTR(mr_uart_get(&drv), &drv);
    MR_TEST_EXPECT_EQ_INT(mr_uart_del(&drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_uart_put(&drv), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(&drv), MR_FALSE);
}

MR_TEST(uart, event_paths) {
    mr_driver_t *drv;
    mr_uart_ops_t optional_ops = uart_ops;
    mr_waiter_t waiter;
    int event_count = 0;

    drv = uart_create(&uart_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_waiter_init(&waiter, uart_waiter_entry,
                                         &event_count, MR_UART_WAIT_RX_READY),
                          0);
    MR_TEST_ASSERT_EQ_INT(mr_uart_event_attach(drv, &waiter), 0);
    MR_TEST_ASSERT_EQ_INT(mr_uart_event_enable(drv, MR_UART_WAIT_RX_READY), 0);
    MR_TEST_ASSERT_EQ_UINT(uart_events, MR_UART_WAIT_RX_READY);
    MR_TEST_ASSERT_EQ_INT(uart_event_enable_state, MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_uart_event_disable(drv, MR_UART_WAIT_TX_DONE), 0);
    MR_TEST_ASSERT_EQ_INT(uart_event_enable_state, MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(mr_uart_event_raise(drv, MR_UART_WAIT_RX_IDLE), 0);
    MR_TEST_ASSERT_EQ_INT(event_count, 0);
    MR_TEST_ASSERT_EQ_INT(mr_uart_event_raise(drv, MR_UART_WAIT_RX_READY), 0);
    MR_TEST_ASSERT_EQ_INT(event_count, 1);
    MR_TEST_ASSERT_EQ_INT(mr_waiter_del(&waiter), 0);
    optional_ops.event_control = MR_NULL;
    drv->ops = (mr_ptr_t)&optional_ops;
    MR_TEST_ASSERT_EQ_INT(mr_uart_event_enable(drv, MR_UART_WAIT_RX_READY),
                          -MR_ENOSYS);
    drv->ops = (mr_ptr_t)&uart_ops;
    mr_uart_del(drv);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_DRIVER_UART) */

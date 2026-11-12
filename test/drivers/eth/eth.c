/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include <mrX/drivers/eth/mr_eth.h>
#include <mrX/test/mr_test.h>

#if defined(MR_USE_TEST) && defined(MR_USE_DRIVER_ETH)
#include <mrX/libc/mr_malloc.h>

typedef struct eth_test_port {
    mr_eth_speed_t speed;
    mr_bool_t link_up;
    mr_err_t configure_result;
    mr_err_t off_result;
    mr_uint32_t configurations;
    mr_uint32_t disables;
    mr_uint32_t reads;
    mr_uint32_t writes;
#if defined(MR_USE_DRIVER_ETH_STREAM)
    mr_eth_frame_t *rx;
    mr_eth_frame_t *tx;
    mr_bool_t rx_busy;
    mr_bool_t tx_busy;
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
} eth_test_port_t;

#if defined(MR_USE_DRIVER_ETH_STREAM)
static void eth_test_complete(mr_driver_t *drv, mr_eth_frame_t **slot,
                              mr_wait_event_t event, mr_err_t status) {
    mr_eth_frame_t *frame = *slot;

    *slot = MR_NULL;
    frame->status = status;
    if (event == MR_ETH_WAIT_RX_READY) {
        frame->size = MR_ETH_FRAME_MIN;
    }
    mr_eth_event_raise(drv, event, frame);
}
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */

static mr_err_t eth_test_configure(const mr_driver_t *drv,
                                   const mr_eth_config_t *config) {
    eth_test_port_t *port = MR_DRIVER_PRIV(drv);

    port->configurations++;
    if (config->speed != MR_ETH_SPEED_OFF) {
        if (port->configure_result != 0) {
            return port->configure_result;
        }
        port->speed = config->speed;
        return 0;
    }
    port->disables++;
    if (port->off_result != 0) {
        return port->off_result;
    }
    port->speed = MR_ETH_SPEED_OFF;
#if defined(MR_USE_DRIVER_ETH_STREAM)
    port->rx_busy = MR_FALSE;
    port->tx_busy = MR_FALSE;
    if (port->rx != MR_NULL) {
        eth_test_complete((mr_driver_t *)drv, &port->rx, MR_ETH_WAIT_RX_ERROR,
                          -MR_EPERM);
    }
    if (port->tx != MR_NULL) {
        eth_test_complete((mr_driver_t *)drv, &port->tx, MR_ETH_WAIT_TX_ERROR,
                          -MR_EPERM);
    }
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
    return 0;
}

static mr_err_t eth_test_link(const mr_driver_t *drv, mr_eth_link_t *link) {
    eth_test_port_t *port = MR_DRIVER_PRIV(drv);

    link->up = (port->speed != MR_ETH_SPEED_OFF) && port->link_up;
    link->speed = link->up ? port->speed : MR_ETH_SPEED_OFF;
    link->duplex = link->up ? MR_ETH_DUPLEX_FULL : MR_ETH_DUPLEX_AUTO;
    link->flow = (mr_eth_flow_t){0};
    return 0;
}

static mr_ssize_t eth_test_read(const mr_driver_t *drv, void *buf,
                                mr_size_t size) {
    eth_test_port_t *port = MR_DRIVER_PRIV(drv);

    MR_IGNORE(buf);
    MR_IGNORE(size);
    port->reads++;
#if defined(MR_USE_DRIVER_ETH_STREAM)
    if (port->rx_busy) {
        return -MR_EBUSY;
    }
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
    return 0;
}

static mr_ssize_t eth_test_write(const mr_driver_t *drv, const void *buf,
                                 mr_size_t size) {
    eth_test_port_t *port = MR_DRIVER_PRIV(drv);

    MR_IGNORE(buf);
    port->writes++;
#if defined(MR_USE_DRIVER_ETH_STREAM)
    if (port->tx_busy) {
        return -MR_EBUSY;
    }
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
    if (port->speed == MR_ETH_SPEED_OFF) {
        return -MR_EPERM;
    }
    return (mr_ssize_t)size;
}

#if defined(MR_USE_DRIVER_ETH_STREAM)
static mr_err_t eth_test_submit(mr_eth_frame_t **slot, mr_eth_frame_t *frame) {
    if (*slot == frame) {
        return -MR_EBUSY;
    }
    if (*slot != MR_NULL) {
        return -MR_EAGAIN;
    }
    *slot = frame;
    return 0;
}

static mr_err_t eth_test_rx_submit(const mr_driver_t *drv,
                                   mr_eth_frame_t *frame) {
    eth_test_port_t *port = MR_DRIVER_PRIV(drv);

    if ((port->speed == MR_ETH_SPEED_OFF) || !port->rx_busy) {
        return -MR_EPERM;
    }
    return eth_test_submit(&port->rx, frame);
}

static mr_err_t eth_test_tx_submit(const mr_driver_t *drv,
                                   mr_eth_frame_t *frame) {
    eth_test_port_t *port = MR_DRIVER_PRIV(drv);

    if ((port->speed == MR_ETH_SPEED_OFF) || !port->tx_busy) {
        return -MR_EPERM;
    }
    if (!port->link_up) {
        return -MR_EAGAIN;
    }
    return eth_test_submit(&port->tx, frame);
}

static mr_err_t eth_test_stream_start(const eth_test_port_t *port,
                                      mr_bool_t *busy) {
    if (port->speed == MR_ETH_SPEED_OFF) {
        return -MR_EPERM;
    }
    if (*busy) {
        return -MR_EBUSY;
    }
    *busy = MR_TRUE;
    return 0;
}

static mr_err_t eth_test_rx_start(const mr_driver_t *drv) {
    eth_test_port_t *port = MR_DRIVER_PRIV(drv);

    return eth_test_stream_start(port, &port->rx_busy);
}

static mr_err_t eth_test_tx_start(const mr_driver_t *drv) {
    eth_test_port_t *port = MR_DRIVER_PRIV(drv);

    return eth_test_stream_start(port, &port->tx_busy);
}

static mr_err_t eth_test_rx_stop(const mr_driver_t *drv) {
    eth_test_port_t *port = MR_DRIVER_PRIV(drv);

    port->rx_busy = MR_FALSE;
    if (port->rx != MR_NULL) {
        eth_test_complete((mr_driver_t *)drv, &port->rx, MR_ETH_WAIT_RX_ERROR,
                          -MR_EPERM);
    }
    return 0;
}

static mr_err_t eth_test_tx_stop(const mr_driver_t *drv) {
    eth_test_port_t *port = MR_DRIVER_PRIV(drv);

    port->tx_busy = MR_FALSE;
    if (port->tx != MR_NULL) {
        eth_test_complete((mr_driver_t *)drv, &port->tx, MR_ETH_WAIT_TX_ERROR,
                          -MR_EPERM);
    }
    return 0;
}

static mr_err_t eth_test_rx_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    eth_test_port_t *port = MR_DRIVER_PRIV(drv);

    *busy = port->rx_busy;
    return 0;
}

static mr_err_t eth_test_tx_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    eth_test_port_t *port = MR_DRIVER_PRIV(drv);

    *busy = port->tx_busy;
    return 0;
}
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */

static const mr_eth_ops_t eth_test_ops = {
    .configure = eth_test_configure,
    .link = eth_test_link,
    .read = eth_test_read,
    .write = eth_test_write,
#if defined(MR_USE_DRIVER_ETH_STREAM)
    .rx_stream_start = eth_test_rx_start,
    .rx_stream_stop = eth_test_rx_stop,
    .rx_stream_is_busy = eth_test_rx_is_busy,
    .rx_stream_submit = eth_test_rx_submit,
    .tx_stream_start = eth_test_tx_start,
    .tx_stream_stop = eth_test_tx_stop,
    .tx_stream_is_busy = eth_test_tx_is_busy,
    .tx_stream_submit = eth_test_tx_submit,
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
};

MR_TEST(eth, direct_io_and_port_lifecycle) {
    mr_uint8_t buf[MR_ETH_FRAME_MIN] = {0};
    mr_eth_data_t data
        = MR_ETH_DATA_INIT(46, MR_ETH_SPEED_AUTO, {2, 0, 0, 0, 0, 1});
    mr_eth_config_t config = data.config;
    mr_eth_config_t applied;
    eth_test_port_t port = {.configure_result = -MR_EIO};
    mr_eth_ops_t ops = eth_test_ops;
    mr_driver_t drv;
    mr_eth_link_t link;

    MR_TEST_ASSERT_EQ_INT(mr_eth_init(&drv, &data, &port, &ops), 0);
    MR_TEST_EXPECT_EQ_INT(config.speed, MR_ETH_SPEED_AUTO);
    MR_TEST_EXPECT_EQ_INT(config.loopback, MR_ETH_LOOPBACK_NONE);
    config.speed = 6U;
    MR_TEST_EXPECT_EQ_INT(mr_eth_configure(&drv, &config), -MR_EINVAL);
    config.speed = MR_ETH_SPEED_AUTO;
    config.loopback = 3U;
    MR_TEST_EXPECT_EQ_INT(mr_eth_configure(&drv, &config), -MR_EINVAL);
    config.loopback = MR_ETH_LOOPBACK_NONE;
    config.speed = MR_ETH_SPEED_10M;
    MR_TEST_ASSERT_EQ_INT(mr_eth_configure(&drv, &config), 0);
    MR_TEST_EXPECT_EQ_UINT(port.configurations, 0);
    MR_TEST_EXPECT_EQ_INT(mr_driver_setup(&drv), -MR_EIO);
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(&drv), MR_FALSE);
    MR_TEST_EXPECT_EQ_UINT(port.disables, 0);

    port.configure_result = 0;
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(&drv), 0);
    MR_TEST_EXPECT_EQ_INT(port.speed, MR_ETH_SPEED_10M);
    MR_TEST_EXPECT_EQ_INT(mr_eth_link(&drv, &link), 0);
    MR_TEST_EXPECT_EQ_INT(link.up, MR_FALSE);
    config.speed = MR_ETH_SPEED_2_5G;
    port.configure_result = -MR_ENOSYS;
    MR_TEST_EXPECT_EQ_INT(mr_eth_configure(&drv, &config), -MR_ENOSYS);
    MR_TEST_ASSERT_EQ_INT(mr_eth_configuration(&drv, &applied), 0);
    MR_TEST_EXPECT_EQ_INT(applied.speed, MR_ETH_SPEED_10M);
    port.configure_result = 0;
    config.speed = MR_ETH_SPEED_100M;
    MR_TEST_ASSERT_EQ_INT(mr_eth_configure(&drv, &config), 0);
    MR_TEST_EXPECT_EQ_INT(port.speed, MR_ETH_SPEED_100M);
    MR_TEST_EXPECT_EQ_INT(mr_eth_read(&drv, MR_NULL, 0), 0);
    MR_TEST_EXPECT_EQ_INT(mr_eth_read(&drv, buf, sizeof(buf) - 1), -MR_ERANGE);
    MR_TEST_EXPECT_EQ_INT(mr_eth_read(&drv, buf, sizeof(buf)), 0);
    MR_TEST_EXPECT_EQ_UINT(port.reads, 1);
    MR_TEST_EXPECT_EQ_INT(mr_eth_write(&drv, MR_NULL, 0), 0);
    MR_TEST_EXPECT_EQ_INT(mr_eth_write(&drv, buf, MR_ETH_HEADER_LEN - 1),
                          -MR_ERANGE);
    MR_TEST_EXPECT_EQ_INT(mr_eth_write(&drv, buf, sizeof(buf)), sizeof(buf));
    MR_TEST_EXPECT_EQ_UINT(port.writes, 1);
    config.speed = MR_ETH_SPEED_OFF;
    MR_TEST_ASSERT_EQ_INT(mr_eth_configure(&drv, &config), 0);
    MR_TEST_EXPECT_EQ_INT(mr_eth_write(&drv, buf, sizeof(buf)), -MR_EPERM);
    MR_TEST_ASSERT_EQ_INT(mr_eth_configuration(&drv, &applied), 0);
    MR_TEST_EXPECT_EQ_INT(applied.speed, MR_ETH_SPEED_OFF);
    config.speed = MR_ETH_SPEED_1000M;
    MR_TEST_ASSERT_EQ_INT(mr_eth_configure(&drv, &config), 0);
    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(&drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(&drv), 0);
    MR_TEST_EXPECT_EQ_INT(port.speed, MR_ETH_SPEED_OFF);
    MR_TEST_EXPECT_EQ_UINT(port.disables, 2);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(&drv), 0);
    MR_TEST_EXPECT_EQ_INT(port.speed, MR_ETH_SPEED_1000M);
    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(&drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_eth_del(&drv), 0);
}

MR_TEST(eth, create_and_reject_invalid_config) {
    mr_eth_data_t *data;
    mr_eth_config_t config;
    eth_test_port_t port = {0};
    mr_eth_ops_t ops = eth_test_ops;
    mr_driver_t *drv;

    data = mr_malloc(sizeof(*data));
    MR_TEST_ASSERT_NE_PTR(data, MR_NULL);
    *data = (mr_eth_data_t)MR_ETH_DATA_INIT(MR_ETH_MTU_MAX, MR_ETH_SPEED_AUTO,
                                            {0, 0, 0, 0, 0, 0});
    drv = mr_eth_create(data, &port, &ops);
    if (drv == MR_NULL) {
        mr_free(data);
        MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    }
    MR_TEST_EXPECT_EQ_INT(mr_driver_setup(drv), -MR_EINVAL);
    MR_TEST_EXPECT_EQ_UINT(port.configurations, 0U);

    config = data->config;
    config.mac[0] = 3U;
    MR_TEST_EXPECT_EQ_INT(mr_eth_configure(drv, &config), -MR_EINVAL);
    config.mac[0] = 2U;
    config.mtu = MR_ETH_MTU_MAX + 1U;
    MR_TEST_EXPECT_EQ_INT(mr_eth_configure(drv, &config), -MR_ERANGE);
    config.mtu = MR_ETH_MTU_MAX;
    MR_TEST_EXPECT_EQ_INT(mr_eth_configure(drv, &config), 0);
    MR_TEST_EXPECT_EQ_UINT(port.configurations, 0U);
    MR_TEST_EXPECT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_EXPECT_EQ_UINT(port.configurations, 1U);
    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(drv), 0);
    MR_TEST_EXPECT_EQ_UINT(port.disables, 1U);
    MR_TEST_EXPECT_EQ_INT(mr_eth_del(drv), 0);
}

#if defined(MR_USE_DRIVER_ETH_STREAM)
typedef struct eth_test_events {
    mr_driver_t *drv;
    const mr_eth_frame_t *frame[4];
    mr_wait_event_t event[4];
    mr_err_t status[4];
    mr_err_t resubmit[4];
    mr_size_t count;
} eth_test_events_t;

static void eth_test_event(mr_waiter_t *waiter, void *param,
                           mr_wait_event_t event, const void *data) {
    eth_test_events_t *events = param;
    const mr_eth_frame_t *frame = data;
    mr_size_t index = events->count++;

    MR_IGNORE(waiter);
    if ((index >= MR_ARRAY_SIZE(events->frame)) || (frame == MR_NULL)) {
        return;
    }
    events->frame[index] = frame;
    events->event[index] = event;
    events->status[index] = frame->status;
    if (event == MR_ETH_WAIT_RX_ERROR) {
        events->resubmit[index]
            = mr_eth_rx_stream_submit(events->drv, (mr_eth_frame_t *)frame);
    } else if (event == MR_ETH_WAIT_TX_ERROR) {
        events->resubmit[index]
            = mr_eth_tx_stream_submit(events->drv, (mr_eth_frame_t *)frame);
    }
}

MR_TEST(eth, optional_stream_directions) {
    mr_uint8_t buf[MR_ETH_FRAME_MIN] = {0};
    mr_eth_data_t data
        = MR_ETH_DATA_INIT(46, MR_ETH_SPEED_AUTO, {2, 0, 0, 0, 0, 1});
    mr_eth_frame_t frame = {.buf = buf, .capacity = sizeof(buf)};
    mr_eth_ops_t ops = eth_test_ops;
    eth_test_port_t port = {0};
    mr_driver_t drv;
    mr_bool_t busy;

    ops.rx_stream_submit = MR_NULL;
    ops.rx_stream_start = MR_NULL;
    ops.rx_stream_stop = MR_NULL;
    ops.rx_stream_is_busy = MR_NULL;
    MR_TEST_ASSERT_EQ_INT(mr_eth_init(&drv, &data, &port, &ops), 0);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(&drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_eth_rx_stream_submit(&drv, &frame), -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_eth_rx_stream_start(&drv), -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_eth_rx_stream_stop(&drv), -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_eth_rx_stream_is_busy(&drv, &busy), -MR_ENOSYS);
    MR_TEST_ASSERT_EQ_INT(mr_eth_tx_stream_start(&drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_eth_tx_stream_is_busy(&drv, &busy), 0);
    MR_TEST_EXPECT_EQ_INT(busy, MR_TRUE);
    frame.size = MR_ETH_HEADER_LEN;
    MR_TEST_EXPECT_EQ_INT(mr_eth_tx_stream_submit(&drv, &frame), -MR_EAGAIN);
    MR_TEST_EXPECT_EQ_INT(mr_eth_tx_stream_stop(&drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_eth_tx_stream_is_busy(&drv, &busy), 0);
    MR_TEST_EXPECT_EQ_INT(busy, MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(mr_eth_tx_stream_submit(&drv, &frame), -MR_EPERM);
    MR_TEST_ASSERT_EQ_INT(mr_driver_shutdown(&drv), 0);

    ops = eth_test_ops;
    ops.tx_stream_submit = MR_NULL;
    ops.tx_stream_start = MR_NULL;
    ops.tx_stream_stop = MR_NULL;
    ops.tx_stream_is_busy = MR_NULL;
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(&drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_eth_tx_stream_submit(&drv, &frame), -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_eth_tx_stream_start(&drv), -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_eth_tx_stream_stop(&drv), -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_eth_tx_stream_is_busy(&drv, &busy), -MR_ENOSYS);
    MR_TEST_ASSERT_EQ_INT(mr_eth_rx_stream_start(&drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_eth_rx_stream_is_busy(&drv, &busy), 0);
    MR_TEST_EXPECT_EQ_INT(busy, MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_eth_rx_stream_submit(&drv, &frame), 0);
    MR_TEST_EXPECT_EQ_PTR(port.rx, &frame);
    MR_TEST_ASSERT_EQ_INT(mr_eth_rx_stream_stop(&drv), 0);
    MR_TEST_EXPECT_EQ_PTR(port.rx, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_eth_rx_stream_is_busy(&drv, &busy), 0);
    MR_TEST_EXPECT_EQ_INT(busy, MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(mr_eth_rx_stream_submit(&drv, &frame), -MR_EPERM);
    MR_TEST_EXPECT_EQ_INT(mr_eth_read(&drv, buf, sizeof(buf)), 0);
    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(&drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_eth_del(&drv), 0);
}

MR_TEST(eth, consecutive_rx_completions) {
    mr_uint8_t buffers[2][MR_ETH_FRAME_MIN] = {{0}};
    mr_eth_frame_t frames[] = {
        {.capacity = sizeof(buffers[0]), .buf = buffers[0]},
        {.capacity = sizeof(buffers[1]), .buf = buffers[1]},
    };
    mr_eth_data_t data
        = MR_ETH_DATA_INIT(46, MR_ETH_SPEED_AUTO, {2, 0, 0, 0, 0, 1});
    mr_eth_ops_t ops = eth_test_ops;
    eth_test_port_t port = {0};
    mr_driver_t drv;
    eth_test_events_t events = {.drv = &drv};
    mr_waiter_t waiter;
    mr_size_t i;

    MR_TEST_ASSERT_EQ_INT(mr_eth_init(&drv, &data, &port, &ops), 0);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(&drv), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_waiter_init(&waiter, eth_test_event, &events, MR_ETH_WAIT_RX_READY),
        0);
    MR_TEST_ASSERT_EQ_INT(mr_eth_event_attach(&drv, &waiter), 0);
    MR_TEST_ASSERT_EQ_INT(mr_eth_rx_stream_start(&drv), 0);

    for (i = 0; i < MR_ARRAY_SIZE(frames); i++) {
        MR_TEST_ASSERT_EQ_INT(mr_eth_rx_stream_submit(&drv, &frames[i]), 0);
        eth_test_complete(&drv, &port.rx, MR_ETH_WAIT_RX_READY, 0);
        MR_TEST_EXPECT_EQ_UINT(events.count, i + 1U);
        MR_TEST_EXPECT_EQ_PTR(events.frame[i], &frames[i]);
        MR_TEST_EXPECT_EQ_UINT(events.event[i], MR_ETH_WAIT_RX_READY);
    }

    MR_TEST_EXPECT_EQ_INT(mr_eth_rx_stream_stop(&drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_waiter_del(&waiter), 0);
    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(&drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_eth_del(&drv), 0);
}

MR_TEST(eth, stream_ownership_and_shutdown_retry) {
    mr_uint8_t rx_buf[MR_ETH_FRAME_MIN] = {0};
    mr_uint8_t tx_buf[MR_ETH_FRAME_MIN] = {0};
    mr_uint8_t spare_buf[MR_ETH_FRAME_MIN] = {0};
    mr_eth_data_t data
        = MR_ETH_DATA_INIT(46, MR_ETH_SPEED_AUTO, {2, 0, 0, 0, 0, 1});
    mr_eth_frame_t rx = {.buf = rx_buf, .capacity = sizeof(rx_buf) - 1};
    mr_eth_frame_t tx = {.buf = tx_buf, .capacity = sizeof(tx_buf)};
    mr_eth_frame_t spare = {.buf = spare_buf, .capacity = sizeof(spare_buf)};
    mr_eth_ops_t ops = eth_test_ops;
    eth_test_port_t port = {0};
    mr_driver_t drv;
    eth_test_events_t events = {.drv = &drv};
    mr_waiter_t waiter;

    rx.owner = &port;
    MR_TEST_ASSERT_EQ_INT(mr_eth_init(&drv, &data, &port, &ops), 0);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(&drv), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_waiter_init(&waiter, eth_test_event, &events,
                       MR_ETH_WAIT_RX_READY | MR_ETH_WAIT_RX_ERROR
                           | MR_ETH_WAIT_TX_DONE | MR_ETH_WAIT_TX_ERROR),
        0);
    MR_TEST_ASSERT_EQ_INT(mr_eth_event_attach(&drv, &waiter), 0);
    MR_TEST_EXPECT_EQ_INT(mr_eth_rx_stream_submit(&drv, &rx), -MR_ERANGE);
    MR_TEST_EXPECT_EQ_PTR(port.rx, MR_NULL);
    rx.capacity = sizeof(rx_buf);
    MR_TEST_EXPECT_EQ_INT(mr_eth_rx_stream_submit(&drv, &rx), -MR_EPERM);
    MR_TEST_EXPECT_EQ_PTR(port.rx, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(mr_eth_read(&drv, spare_buf, sizeof(spare_buf)), 0);
    MR_TEST_EXPECT_EQ_UINT(port.reads, 1);
    MR_TEST_ASSERT_EQ_INT(mr_eth_rx_stream_start(&drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_eth_rx_stream_start(&drv), -MR_EBUSY);
    MR_TEST_ASSERT_EQ_INT(mr_eth_rx_stream_submit(&drv, &rx), 0);
    MR_TEST_EXPECT_EQ_PTR(port.rx, &rx);
    MR_TEST_EXPECT_EQ_INT(mr_eth_read(&drv, spare_buf, sizeof(spare_buf)),
                          -MR_EBUSY);
    MR_TEST_EXPECT_EQ_UINT(port.reads, 2);
    MR_TEST_EXPECT_EQ_INT(mr_eth_rx_stream_submit(&drv, &rx), -MR_EBUSY);
    MR_TEST_EXPECT_EQ_INT(mr_eth_rx_stream_submit(&drv, &spare), -MR_EAGAIN);
    MR_TEST_EXPECT_EQ_INT(mr_eth_tx_stream_submit(&drv, &tx), -MR_ERANGE);
    tx.size = MR_ETH_HEADER_LEN;
    MR_TEST_EXPECT_EQ_INT(mr_eth_tx_stream_submit(&drv, &tx), -MR_EPERM);
    MR_TEST_EXPECT_EQ_PTR(port.tx, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(mr_eth_write(&drv, spare_buf, sizeof(spare_buf)),
                          sizeof(spare_buf));
    MR_TEST_ASSERT_EQ_INT(mr_eth_tx_stream_start(&drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_eth_tx_stream_submit(&drv, &tx), -MR_EAGAIN);
    MR_TEST_EXPECT_EQ_PTR(port.tx, MR_NULL);

    port.link_up = MR_TRUE;
    MR_TEST_ASSERT_EQ_INT(mr_eth_tx_stream_submit(&drv, &tx), 0);
    MR_TEST_EXPECT_EQ_INT(mr_eth_write(&drv, spare_buf, sizeof(spare_buf)),
                          -MR_EBUSY);
    eth_test_complete(&drv, &port.rx, MR_ETH_WAIT_RX_READY, 0);
    MR_TEST_EXPECT_EQ_UINT(events.count, 1);
    MR_TEST_EXPECT_EQ_PTR(events.frame[0], &rx);
    MR_TEST_EXPECT_EQ_UINT(events.event[0], MR_ETH_WAIT_RX_READY);
    MR_TEST_EXPECT_EQ_INT(events.status[0], 0);
    MR_TEST_EXPECT_EQ_UINT(rx.size, MR_ETH_FRAME_MIN);
    MR_TEST_EXPECT_EQ_PTR(rx.buf, rx_buf);
    MR_TEST_EXPECT_EQ_PTR(rx.owner, &port);
    MR_TEST_ASSERT_EQ_INT(mr_eth_rx_stream_submit(&drv, &rx), 0);
    eth_test_complete(&drv, &port.tx, MR_ETH_WAIT_TX_DONE, 0);
    MR_TEST_EXPECT_EQ_UINT(events.count, 2);
    MR_TEST_EXPECT_EQ_PTR(events.frame[1], &tx);
    MR_TEST_EXPECT_EQ_UINT(events.event[1], MR_ETH_WAIT_TX_DONE);
    MR_TEST_EXPECT_EQ_INT(events.status[1], 0);
    MR_TEST_ASSERT_EQ_INT(mr_eth_tx_stream_submit(&drv, &tx), 0);

    port.off_result = -MR_ETIMEDOUT;
    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(&drv), -MR_ETIMEDOUT);
    MR_TEST_EXPECT_EQ_PTR(waiter.able, &data.event);
    MR_TEST_EXPECT_EQ_PTR(port.rx, &rx);
    MR_TEST_EXPECT_EQ_PTR(port.tx, &tx);
    MR_TEST_EXPECT_EQ_UINT(events.count, 2);
    MR_TEST_EXPECT_EQ_INT(mr_eth_write(&drv, spare_buf, sizeof(spare_buf)),
                          -MR_EBUSY);
    MR_TEST_EXPECT_EQ_INT(mr_eth_rx_stream_submit(&drv, &spare), -MR_EAGAIN);

    port.off_result = 0;
    MR_TEST_ASSERT_EQ_INT(mr_driver_shutdown(&drv), 0);
    MR_TEST_EXPECT_EQ_UINT(port.disables, 2);
    MR_TEST_EXPECT_EQ_UINT(events.count, 4);
    MR_TEST_EXPECT_EQ_PTR(events.frame[2], &rx);
    MR_TEST_EXPECT_EQ_PTR(events.frame[3], &tx);
    MR_TEST_EXPECT_EQ_UINT(events.event[2], MR_ETH_WAIT_RX_ERROR);
    MR_TEST_EXPECT_EQ_UINT(events.event[3], MR_ETH_WAIT_TX_ERROR);
    MR_TEST_EXPECT_EQ_INT(events.status[2], -MR_EPERM);
    MR_TEST_EXPECT_EQ_INT(events.status[3], -MR_EPERM);
    MR_TEST_EXPECT_EQ_INT(events.resubmit[2], -MR_EPERM);
    MR_TEST_EXPECT_EQ_INT(events.resubmit[3], -MR_EPERM);
    MR_TEST_EXPECT_EQ_PTR(waiter.able, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(mr_waiter_del(&waiter), 0);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(&drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_eth_write(&drv, tx_buf, sizeof(tx_buf)),
                          sizeof(tx_buf));
    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(&drv), 0);
    MR_TEST_EXPECT_EQ_UINT(events.count, 4);
    MR_TEST_EXPECT_EQ_INT(mr_eth_del(&drv), 0);
}
#endif /* defined(MR_USE_DRIVER_ETH_STREAM) */
#endif /* defined(MR_USE_TEST) && defined(MR_USE_DRIVER_ETH) */

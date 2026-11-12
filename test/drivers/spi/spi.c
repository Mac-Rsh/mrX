/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_DRIVER_SPI)
#include <mrX/drivers/spi/mr_spi.h>
#include <mrX/libc/mr_malloc.h>

typedef struct test_spi_context {
    mr_spi_config_t config;
    const mr_spi_msg_t *msgs;
    const void *buf;
    mr_size_t count;
    mr_uint32_t cs;
    mr_uint32_t events;
    mr_bool_t event_enabled;
    mr_err_t error;
#if defined(MR_USE_DRIVER_SPI_STREAM)
    mr_spi_stream_config_t stream_config;
    mr_bool_t busy;
#endif /* defined(MR_USE_DRIVER_SPI_STREAM) */
} test_spi_context_t;

static mr_err_t test_spi_configure(const mr_driver_t *drv, mr_uint32_t cs,
                                   const mr_spi_config_t *config) {
    test_spi_context_t *ctx = MR_DRIVER_PRIV(drv);

    ctx->cs = cs;
    if (ctx->error == 0) {
        ctx->config = *config;
    }
    return ctx->error;
}

static mr_err_t test_spi_configuration(const mr_driver_t *drv, mr_uint32_t cs,
                                       mr_spi_config_t *config) {
    test_spi_context_t *ctx = MR_DRIVER_PRIV(drv);

    ctx->cs = cs;
    *config = ctx->config;
    return 0;
}

static mr_ssize_t test_spi_transfer(const mr_driver_t *drv, mr_uint32_t cs,
                                    const mr_spi_msg_t *msgs, mr_size_t num) {
    test_spi_context_t *ctx = MR_DRIVER_PRIV(drv);

    ctx->cs = cs;
    ctx->msgs = msgs;
    ctx->count = num;
    return (ctx->error != 0) ? ctx->error : (mr_ssize_t)num - 1;
}

#if defined(MR_USE_DRIVER_SPI_STREAM)
static mr_err_t test_spi_stream_start(const mr_driver_t *drv, mr_uint32_t cs,
                                      const mr_spi_msg_t *msg,
                                      const mr_spi_stream_config_t *config) {
    test_spi_context_t *ctx = MR_DRIVER_PRIV(drv);

    ctx->cs = cs;
    ctx->msgs = msg;
    ctx->stream_config = *config;
    ctx->busy = MR_TRUE;
    return 0;
}

static mr_err_t test_spi_stream_stop(const mr_driver_t *drv) {
    test_spi_context_t *ctx = MR_DRIVER_PRIV(drv);

    if (ctx->error == 0) {
        ctx->busy = MR_FALSE;
    }
    return ctx->error;
}

static mr_err_t test_spi_stream_is_busy(const mr_driver_t *drv,
                                        mr_bool_t *busy) {
    test_spi_context_t *ctx = MR_DRIVER_PRIV(drv);

    *busy = ctx->busy;
    return 0;
}

static mr_err_t test_spi_rx_stream_offset(const mr_driver_t *drv,
                                          mr_size_t *offset) {
    MR_IGNORE(drv);
    *offset = 4U;
    return 0;
}

static mr_err_t test_spi_tx_stream_offset(const mr_driver_t *drv,
                                          mr_size_t *offset) {
    MR_IGNORE(drv);
    *offset = 7U;
    return 0;
}
#endif /* defined(MR_USE_DRIVER_SPI_STREAM) */

static mr_ssize_t test_spi_slave_read(const mr_driver_t *drv, mr_uint32_t cs,
                                      void *buf, mr_size_t size) {
    test_spi_context_t *ctx = MR_DRIVER_PRIV(drv);

    ctx->cs = cs;
    ctx->count = size;
    *(mr_uint8_t *)buf = 0xa5U;
    return 1;
}

static mr_ssize_t test_spi_slave_write(const mr_driver_t *drv, mr_uint32_t cs,
                                       const void *buf, mr_size_t size) {
    test_spi_context_t *ctx = MR_DRIVER_PRIV(drv);

    ctx->cs = cs;
    ctx->buf = buf;
    ctx->count = size;
    return (ctx->error != 0) ? ctx->error : (mr_ssize_t)size;
}

static mr_err_t test_spi_event_control(const mr_driver_t *drv, mr_uint32_t cs,
                                       mr_uint32_t events, mr_bool_t enable) {
    test_spi_context_t *ctx = MR_DRIVER_PRIV(drv);

    ctx->cs = cs;
    ctx->events = events;
    ctx->event_enabled = enable;
    return ctx->error;
}

static void test_spi_waiter(mr_waiter_t *waiter, void *param,
                            mr_wait_event_t event, const void *data) {
    const mr_spi_event_data_t *payload = data;
    test_spi_context_t *ctx = param;

    MR_IGNORE(waiter);
    ctx->count++;
    ctx->events = event;
    ctx->cs = payload->cs;
}

static const mr_spi_ops_t __test_spi_ops = {
    .configure = test_spi_configure,
    .configuration = test_spi_configuration,
    .transfer = test_spi_transfer,
#if defined(MR_USE_DRIVER_SPI_STREAM)
    .stream_start = test_spi_stream_start,
    .stream_stop = test_spi_stream_stop,
    .stream_is_busy = test_spi_stream_is_busy,
    .rx_stream_offset = test_spi_rx_stream_offset,
    .tx_stream_offset = test_spi_tx_stream_offset,
#endif /* defined(MR_USE_DRIVER_SPI_STREAM) */
    .slave_read = test_spi_slave_read,
    .slave_write = test_spi_slave_write,
    .event_control = test_spi_event_control,
};

static mr_spi_data_t __test_spi_defined_data, __test_spi_installed_data;
static const mr_spi_ops_t __test_spi_empty_ops;

MR_SPI_DEFINE(test_spi_defined, &__test_spi_defined_data, MR_NULL,
              &__test_spi_empty_ops);
MR_SPI_INSTALL(test_spi_installed, &__test_spi_installed_data, MR_NULL,
               &__test_spi_empty_ops, MR_DRIVER_LEVEL_DEFER);

MR_TEST(spi, configuration_and_transfer) {
    mr_spi_config_t config, saved;
    test_spi_context_t ctx;
    mr_spi_msg_t msgs[2];
    mr_spi_data_t data;
    mr_driver_t drv;

    ctx = (test_spi_context_t){0};
    msgs[0] = (mr_spi_msg_t){.tx_buf = "ab", .tx_len = 2U};
    msgs[1] = (mr_spi_msg_t){.tx_buf = "c", .tx_len = 1U};
    config = (mr_spi_config_t){
        .clock_rate = 4000000U,
        .data_bits = 8U,
        .mode = MR_SPI_MODE_3,
        .bit_order = MR_SPI_BIT_ORDER_LSB,
        .role = MR_SPI_ROLE_MASTER,
    };
    saved = (mr_spi_config_t){0};
    MR_TEST_ASSERT_EQ_INT(
        mr_spi_init(&drv, &data, &ctx, (mr_spi_ops_t *)&__test_spi_ops), 0);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(&drv), 0);

    MR_TEST_EXPECT_EQ_INT(mr_spi_configure(&drv, 3U, &config), 0);
    MR_TEST_EXPECT_EQ_UINT(ctx.cs, 3U);
    MR_TEST_EXPECT_EQ_INT(mr_spi_configuration(&drv, 7U, &saved), 0);
    MR_TEST_EXPECT_EQ_UINT(ctx.cs, 7U);
    MR_TEST_EXPECT_EQ_UINT(saved.clock_rate, 4000000U);
    MR_TEST_EXPECT_EQ_UINT(saved.data_bits, 8U);
    MR_TEST_EXPECT_EQ_UINT(saved.mode, MR_SPI_MODE_3);
    MR_TEST_EXPECT_EQ_UINT(saved.bit_order, MR_SPI_BIT_ORDER_LSB);
    MR_TEST_EXPECT_EQ_UINT(saved.role, MR_SPI_ROLE_MASTER);

    MR_TEST_EXPECT_EQ_INT(mr_spi_transfer(&drv, 3U, msgs, 2U), 1);
    MR_TEST_EXPECT_EQ_PTR(ctx.msgs, msgs);
    MR_TEST_EXPECT_EQ_UINT(ctx.count, 2U);
    MR_TEST_EXPECT_EQ_UINT(ctx.cs, 3U);
    MR_TEST_EXPECT_EQ_INT(mr_spi_transfer(&drv, 7U, MR_NULL, 0U), 0);
    MR_TEST_EXPECT_EQ_UINT(ctx.cs, 3U);
    ctx.error = -MR_EIO;
    MR_TEST_EXPECT_EQ_INT(mr_spi_transfer(&drv, 3U, msgs, 2U), -MR_EIO);
    MR_TEST_EXPECT_EQ_INT(mr_spi_configure(&drv, 3U, &config), -MR_EIO);

    MR_TEST_EXPECT_EQ_INT(mr_spi_del(&drv), 0);
}

MR_TEST(spi, slave_events_and_references) {
    mr_spi_event_data_t payload;
    test_spi_context_t ctx;
    mr_ht_spi_spec_t spec;
    mr_spi_data_t *data;
    mr_uint8_t bytes[2];
    mr_waiter_t waiter;
    mr_driver_t *drv;
    mr_err_t ret;

    ctx = (test_spi_context_t){0};
    bytes[0] = 0;
    bytes[1] = 0;
    payload.cs = 9U;
    data = mr_malloc(sizeof(*data));
    MR_TEST_ASSERT_NE_PTR(data, MR_NULL);
    drv = mr_spi_create(data, &ctx, (mr_spi_ops_t *)&__test_spi_ops);
    if (!drv) {
        mr_free(data);
        MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    }
    ret = mr_driver_setup(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    spec = (mr_ht_spi_spec_t){.drv = drv, .cs = 5U};

    MR_TEST_EXPECT_EQ_INT(mr_ht_spi_slave_read(&spec, bytes, sizeof(bytes)), 1);
    MR_TEST_EXPECT_EQ_UINT(bytes[0], 0xa5U);
    MR_TEST_EXPECT_EQ_UINT(ctx.cs, 5U);
    MR_TEST_EXPECT_EQ_UINT(ctx.count, sizeof(bytes));
    MR_TEST_EXPECT_EQ_INT(mr_spi_slave_read(drv, 6U, MR_NULL, 0U), 0);
    MR_TEST_EXPECT_EQ_UINT(ctx.cs, 5U);
    MR_TEST_EXPECT_EQ_INT(mr_ht_spi_slave_write(&spec, bytes, sizeof(bytes)),
                          2);
    MR_TEST_EXPECT_EQ_PTR(ctx.buf, bytes);
    ctx.error = -MR_EBUSY;
    MR_TEST_EXPECT_EQ_INT(mr_spi_slave_write(drv, 6U, MR_NULL, 0U), 0);
    MR_TEST_EXPECT_EQ_INT(mr_spi_slave_write(drv, 6U, bytes, 1U), -MR_EBUSY);
    ctx.error = 0;

    ctx.count = 0;
    ret = mr_waiter_init(&waiter, test_spi_waiter, &ctx, MR_SPI_WAIT_RX_READY);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    MR_TEST_EXPECT_EQ_INT(mr_ht_spi_event_attach(&spec, &waiter), 0);
    MR_TEST_EXPECT_EQ_INT(mr_ht_spi_event_enable(&spec, MR_SPI_WAIT_RX_READY),
                          0);
    MR_TEST_EXPECT_EQ_UINT(ctx.cs, 5U);
    MR_TEST_EXPECT_EQ_UINT(ctx.events, MR_SPI_WAIT_RX_READY);
    MR_TEST_EXPECT_EQ_INT(ctx.event_enabled, MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(mr_spi_event_raise(drv, MR_SPI_WAIT_ERROR, &payload),
                          0);
    MR_TEST_EXPECT_EQ_UINT(ctx.count, 0U);
    MR_TEST_EXPECT_EQ_INT(
        mr_spi_event_raise(drv, MR_SPI_WAIT_RX_READY, &payload), 0);
    MR_TEST_EXPECT_EQ_UINT(ctx.count, 1U);
    MR_TEST_EXPECT_EQ_UINT(ctx.cs, 9U);
    MR_TEST_EXPECT_EQ_UINT(ctx.events, MR_SPI_WAIT_RX_READY);
    MR_TEST_EXPECT_EQ_INT(mr_ht_spi_event_disable(&spec, MR_SPI_WAIT_RX_READY),
                          0);
    MR_TEST_EXPECT_EQ_UINT(ctx.cs, 5U);
    MR_TEST_EXPECT_EQ_INT(ctx.event_enabled, MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(mr_waiter_del(&waiter), 0);

    MR_TEST_EXPECT_EQ_PTR(mr_spi_get(drv), drv);
    MR_TEST_EXPECT_EQ_INT(mr_spi_put(drv), MR_FALSE);

_exit:
    MR_TEST_EXPECT_EQ_INT(mr_spi_del(drv), 0);
}

MR_TEST(spi, optional_operations) {
#if defined(MR_USE_DRIVER_SPI_STREAM)
    mr_spi_stream_config_t config;
    mr_spi_msg_t msg;
    mr_size_t offset;
    mr_bool_t busy;
#endif /* defined(MR_USE_DRIVER_SPI_STREAM) */
    mr_driver_t *drv, *registered;

    drv = MR_DRIVER_FIND(test_spi_defined);
    MR_TEST_EXPECT_EQ_PTR(mr_driver_find("test_spi_defined"), MR_NULL);
    registered = mr_driver_find("test_spi_installed");
    MR_TEST_ASSERT_NE_PTR(registered, MR_NULL);
    MR_TEST_EXPECT_EQ_PTR(registered, MR_DRIVER_FIND(test_spi_installed));
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(registered), MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(mr_spi_put(registered), MR_FALSE);

    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_spi_transfer(drv, 0U, MR_NULL, 0U), -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_spi_slave_read(drv, 0U, MR_NULL, 0U), -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_spi_slave_write(drv, 0U, MR_NULL, 0U), -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_spi_event_enable(drv, 0U, MR_SPI_WAIT_SELECT),
                          -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_spi_event_disable(drv, 0U, MR_SPI_WAIT_SELECT),
                          -MR_ENOSYS);
#if defined(MR_USE_DRIVER_SPI_STREAM)
    config = (mr_spi_stream_config_t){0};
    msg = (mr_spi_msg_t){0};
    MR_TEST_EXPECT_EQ_INT(mr_spi_stream_start(drv, 0U, &msg, &config),
                          -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_spi_stream_stop(drv), -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_spi_stream_is_busy(drv, &busy), -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_spi_rx_stream_offset(drv, &offset), -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_spi_tx_stream_offset(drv, &offset), -MR_ENOSYS);
#endif /* defined(MR_USE_DRIVER_SPI_STREAM) */
    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(drv), 0);
}

#if defined(MR_USE_DRIVER_SPI_STREAM)
MR_TEST(spi, stream_dispatch) {
    mr_spi_stream_config_t config;
    test_spi_context_t ctx;
    mr_ht_spi_spec_t spec;
    mr_spi_data_t data;
    mr_spi_msg_t msg;
    mr_size_t offset;
    mr_driver_t drv;
    mr_bool_t busy;

    ctx = (test_spi_context_t){0};
    config = (mr_spi_stream_config_t){.circular = MR_TRUE};
    msg = (mr_spi_msg_t){.tx_buf = "ab", .tx_len = 2U};
    busy = MR_FALSE;
    offset = 0U;
    MR_TEST_ASSERT_EQ_INT(
        mr_spi_init(&drv, &data, &ctx, (mr_spi_ops_t *)&__test_spi_ops), 0);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(&drv), 0);
    spec = (mr_ht_spi_spec_t){.drv = &drv, .cs = 5U};
    MR_TEST_EXPECT_EQ_INT(mr_ht_spi_stream_start(&spec, &msg, &config), 0);
    MR_TEST_EXPECT_EQ_PTR(ctx.msgs, &msg);
    MR_TEST_EXPECT_EQ_UINT(ctx.cs, 5U);
    MR_TEST_EXPECT_EQ_INT(ctx.stream_config.circular, MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(mr_ht_spi_stream_is_busy(&spec, &busy), 0);
    MR_TEST_EXPECT_EQ_INT(busy, MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(mr_ht_spi_rx_stream_offset(&spec, &offset), 0);
    MR_TEST_EXPECT_EQ_UINT(offset, 4U);
    MR_TEST_EXPECT_EQ_INT(mr_ht_spi_tx_stream_offset(&spec, &offset), 0);
    MR_TEST_EXPECT_EQ_UINT(offset, 7U);
    ctx.error = -MR_EIO;
    MR_TEST_EXPECT_EQ_INT(mr_ht_spi_stream_stop(&spec), -MR_EIO);
    MR_TEST_EXPECT_EQ_INT(ctx.busy, MR_TRUE);
    ctx.error = 0;
    MR_TEST_EXPECT_EQ_INT(mr_ht_spi_stream_stop(&spec), 0);
    MR_TEST_EXPECT_EQ_INT(mr_ht_spi_stream_is_busy(&spec, &busy), 0);
    MR_TEST_EXPECT_EQ_INT(busy, MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(mr_spi_del(&drv), 0);
}
#endif /* defined(MR_USE_DRIVER_SPI_STREAM) */

#define MR_HT_COMPAT test_driver_spi
#if MR_HT_HAS_DRIVER()
static test_spi_context_t __test_spi_hardtree_ctx;

MR_HT_DRIVER_FOREACH(MR_HT_SPI_INSTALL, &__test_spi_hardtree_ctx,
                     &__test_spi_ops);

static mr_ht_spi_spec_t __test_spi_specs[] = {
    MR_HT_SPI_SPEC_FOREACH(0, specs),
};

MR_TEST(spi, hardtree_specifications) {
    mr_spi_config_t config, saved;
    mr_ht_spi_spec_t spec;
    mr_spi_msg_t msgs[2];

    __test_spi_hardtree_ctx = (test_spi_context_t){0};
    spec = (mr_ht_spi_spec_t)MR_HT_SPI_SPEC(0, specs);
    config = (mr_spi_config_t){.clock_rate = 1000000U, .data_bits = 8U};
    saved = (mr_spi_config_t){0};
    msgs[0] = (mr_spi_msg_t){.tx_buf = "a", .tx_len = 1U};
    msgs[1] = (mr_spi_msg_t){.tx_buf = "b", .tx_len = 1U};
    MR_TEST_EXPECT_EQ_UINT(MR_ARRAY_SIZE(__test_spi_specs), 2U);
    MR_TEST_EXPECT_EQ_PTR(spec.drv, MR_HT_DRIVER_FIND(MR_HT_LABEL(spi_test)));
    MR_TEST_EXPECT_EQ_PTR(__test_spi_specs[1].drv, spec.drv);
    MR_TEST_EXPECT_EQ_UINT(spec.cs, 3U);
    MR_TEST_EXPECT_EQ_UINT(__test_spi_specs[0].cs, 3U);
    MR_TEST_EXPECT_EQ_UINT(__test_spi_specs[1].cs, 7U);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(spec.drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_ht_spi_configure(&spec, &config), 0);
    MR_TEST_EXPECT_EQ_UINT(__test_spi_hardtree_ctx.cs, 3U);
    MR_TEST_EXPECT_EQ_INT(mr_ht_spi_configuration(&__test_spi_specs[1], &saved),
                          0);
    MR_TEST_EXPECT_EQ_UINT(saved.clock_rate, 1000000U);
    MR_TEST_EXPECT_EQ_UINT(__test_spi_hardtree_ctx.cs, 7U);
    MR_TEST_EXPECT_EQ_INT(mr_ht_spi_transfer(&spec, msgs, MR_ARRAY_SIZE(msgs)),
                          1);
    MR_TEST_EXPECT_EQ_UINT(__test_spi_hardtree_ctx.cs, 3U);
    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(spec.drv), 0);
}
#endif /* MR_HT_HAS_DRIVER() */
#undef MR_HT_COMPAT

#endif /* defined(MR_USE_TEST) && defined(MR_USE_DRIVER_SPI) */

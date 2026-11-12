/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_DRIVER_WDT)
#include <mrX/drivers/wdt/mr_wdt.h>
#include <mrX/libc/mr_malloc.h>

static mr_wdt_config_t wdt_config;
static mr_bool_t wdt_busy;
static int wdt_start_count;
static int wdt_stop_count;
static int wdt_feed_count;
static mr_uint32_t wdt_events;
static mr_bool_t wdt_event_enable_state;

static mr_err_t wdt_configure(const mr_driver_t *drv,
                              const mr_wdt_config_t *config) {
    MR_IGNORE(drv);
    wdt_config = *config;
    return 0;
}

static mr_err_t wdt_configuration(const mr_driver_t *drv,
                                  mr_wdt_config_t *config) {
    MR_IGNORE(drv);
    *config = wdt_config;
    return 0;
}

static mr_err_t wdt_start(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    wdt_start_count++;
    wdt_busy = MR_TRUE;
    return 0;
}

static mr_err_t wdt_stop(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    wdt_stop_count++;
    wdt_busy = MR_FALSE;
    return 0;
}

static mr_err_t wdt_feed(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    wdt_feed_count++;
    return 0;
}

static mr_err_t wdt_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    MR_IGNORE(drv);
    *busy = wdt_busy;
    return 0;
}

static mr_err_t wdt_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable) {
    MR_IGNORE(drv);
    wdt_events = events;
    wdt_event_enable_state = enable;
    return 0;
}

static const mr_wdt_ops_t wdt_ops = {
    .configure = wdt_configure,
    .configuration = wdt_configuration,
    .start = wdt_start,
    .stop = wdt_stop,
    .feed = wdt_feed,
    .is_busy = wdt_is_busy,
    .event_control = wdt_event_control,
};

static mr_driver_t *wdt_create(const mr_wdt_ops_t *ops) {
    mr_wdt_data_t *data = mr_malloc(sizeof(*data));

    if (data == MR_NULL) {
        return MR_NULL;
    }
    return mr_wdt_create(data, MR_NULL, (mr_wdt_ops_t *)ops);
}

static void wdt_waiter_entry(mr_waiter_t *waiter, void *param,
                             mr_wait_event_t event, const void *data) {
    int *count = (int *)param;

    MR_IGNORE(waiter);
    MR_IGNORE(event);
    MR_IGNORE(data);
    (*count)++;
}

MR_TEST(wdt, configuration_and_state_transitions) {
    mr_driver_t *drv;
    mr_wdt_config_t config = {
        .feed_min = 10U,
        .feed_max = 1000U,
        .pretimeout = 900U,
    };
    mr_wdt_config_t saved = {0};
    mr_bool_t busy = MR_FALSE;

    wdt_busy = MR_FALSE;
    wdt_start_count = 0;
    wdt_stop_count = 0;
    wdt_feed_count = 0;
    drv = wdt_create(&wdt_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_wdt_configure(drv, &config), 0);
    MR_TEST_ASSERT_EQ_INT(mr_wdt_configuration(drv, &saved), 0);
    MR_TEST_ASSERT_EQ_UINT(saved.feed_max, 1000U);
    MR_TEST_EXPECT_EQ_INT(mr_wdt_is_busy(drv, &busy), 0);
    MR_TEST_EXPECT_EQ_INT(busy, MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(mr_wdt_feed(drv), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_wdt_start(drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_wdt_is_busy(drv, &busy), 0);
    MR_TEST_EXPECT_EQ_INT(busy, MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_wdt_start(drv), -MR_EBUSY);
    MR_TEST_ASSERT_EQ_INT(mr_wdt_configure(drv, &config), -MR_EBUSY);
    MR_TEST_ASSERT_EQ_INT(mr_wdt_feed(drv), 0);
    MR_TEST_ASSERT_EQ_INT(wdt_feed_count, 1);
    MR_TEST_ASSERT_EQ_INT(mr_wdt_stop(drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_wdt_stop(drv), 0);
    MR_TEST_ASSERT_EQ_INT(wdt_stop_count, 1);
    mr_wdt_del(drv);
}

MR_TEST(wdt, static_reference) {
    mr_wdt_ops_t ops = wdt_ops;
    mr_wdt_data_t data = {0};
    mr_driver_t drv;

    MR_TEST_ASSERT_EQ_INT(mr_wdt_init(&drv, &data, MR_NULL, &ops), 0);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(&drv), 0);
    MR_TEST_ASSERT_EQ_PTR(mr_wdt_get(&drv), &drv);
    MR_TEST_EXPECT_EQ_INT(mr_wdt_del(&drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_wdt_put(&drv), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(&drv), MR_FALSE);
}

MR_TEST(wdt, optional_stop_and_event_paths) {
    mr_driver_t *drv;
    mr_wdt_ops_t optional_ops = wdt_ops;
    mr_waiter_t waiter;
    int event_count = 0;

    wdt_busy = MR_FALSE;
    drv = wdt_create(&wdt_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_wdt_start(drv), 0);
    optional_ops.stop = MR_NULL;
    optional_ops.event_control = MR_NULL;
    drv->ops = (mr_ptr_t)&optional_ops;
    MR_TEST_ASSERT_EQ_INT(mr_wdt_stop(drv), -MR_ENOSYS);
    MR_TEST_ASSERT_EQ_INT(mr_wdt_event_enable(drv, MR_WDT_WAIT_PRETIMEOUT),
                          -MR_ENOSYS);
    drv->ops = (mr_ptr_t)&wdt_ops;
    MR_TEST_ASSERT_EQ_INT(mr_waiter_init(&waiter, wdt_waiter_entry,
                                         &event_count, MR_WDT_WAIT_PRETIMEOUT),
                          0);
    MR_TEST_ASSERT_EQ_INT(mr_wdt_event_attach(drv, &waiter), 0);
    MR_TEST_ASSERT_EQ_INT(mr_wdt_event_enable(drv, MR_WDT_WAIT_PRETIMEOUT), 0);
    MR_TEST_ASSERT_EQ_UINT(wdt_events, MR_WDT_WAIT_PRETIMEOUT);
    MR_TEST_ASSERT_EQ_INT(wdt_event_enable_state, MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_wdt_event_disable(drv, MR_WDT_WAIT_PRETIMEOUT), 0);
    MR_TEST_ASSERT_EQ_INT(wdt_event_enable_state, MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(mr_wdt_event_raise(drv, MR_WDT_WAIT_PRETIMEOUT), 0);
    MR_TEST_ASSERT_EQ_INT(event_count, 1);
    MR_TEST_ASSERT_EQ_INT(mr_waiter_del(&waiter), 0);
    mr_wdt_del(drv);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_DRIVER_WDT) */

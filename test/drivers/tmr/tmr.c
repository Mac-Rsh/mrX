/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_DRIVER_TMR)
#include <mrX/drivers/tmr/mr_tmr.h>
#include <mrX/libc/mr_malloc.h>

static mr_tmr_config_t tmr_config;
static mr_bool_t tmr_busy;
static mr_err_t tmr_busy_ret;
static int tmr_start_count;
static int tmr_stop_count;
static mr_uint32_t tmr_events;
static mr_bool_t tmr_event_enable_state;

static mr_err_t tmr_configure(const mr_driver_t *drv,
                              const mr_tmr_config_t *config) {
    MR_IGNORE(drv);
    tmr_config = *config;
    return 0;
}

static mr_err_t tmr_configuration(const mr_driver_t *drv,
                                  mr_tmr_config_t *config) {
    MR_IGNORE(drv);
    *config = tmr_config;
    return 0;
}

static mr_err_t tmr_start(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    tmr_start_count++;
    tmr_busy = MR_TRUE;
    return 0;
}

static mr_err_t tmr_stop(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    tmr_stop_count++;
    tmr_busy = MR_FALSE;
    return 0;
}

static mr_err_t tmr_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    MR_IGNORE(drv);
    if (tmr_busy_ret == 0) {
        *busy = tmr_busy;
    }
    return tmr_busy_ret;
}

static mr_err_t tmr_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable) {
    MR_IGNORE(drv);
    tmr_events = events;
    tmr_event_enable_state = enable;
    return 0;
}

static const mr_tmr_ops_t tmr_ops = {
    .configure = tmr_configure,
    .configuration = tmr_configuration,
    .start = tmr_start,
    .stop = tmr_stop,
    .is_busy = tmr_is_busy,
    .event_control = tmr_event_control,
};

static mr_driver_t *tmr_create(const mr_tmr_ops_t *ops) {
    mr_tmr_data_t *data = mr_malloc(sizeof(*data));

    if (data == MR_NULL) {
        return MR_NULL;
    }
    return mr_tmr_create(data, MR_NULL, (mr_tmr_ops_t *)ops);
}

static void tmr_waiter_entry(mr_waiter_t *waiter, void *param,
                             mr_wait_event_t event, const void *data) {
    int *count = (int *)param;

    MR_IGNORE(waiter);
    MR_IGNORE(event);
    MR_IGNORE(data);
    (*count)++;
}

MR_TEST(tmr, configuration_and_state_transitions) {
    mr_driver_t *drv;
    mr_tmr_config_t config = {
        .period = 1000U,
        .signal = MR_TMR_SIGNAL_UPDATE,
        .mode = MR_TMR_MODE_PERIODIC,
    };
    mr_tmr_config_t saved = {0};
    mr_bool_t busy;

    tmr_busy = MR_FALSE;
    tmr_busy_ret = 0;
    tmr_start_count = 0;
    tmr_stop_count = 0;
    drv = tmr_create(&tmr_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_tmr_configure(drv, &config), 0);
    MR_TEST_ASSERT_EQ_INT(mr_tmr_configuration(drv, &saved), 0);
    MR_TEST_ASSERT_EQ_UINT(saved.period, 1000U);
    MR_TEST_ASSERT_EQ_INT(mr_tmr_start(drv), 0);
    MR_TEST_ASSERT_EQ_INT(tmr_start_count, 1);
    MR_TEST_ASSERT_EQ_INT(mr_tmr_start(drv), -MR_EBUSY);
    MR_TEST_ASSERT_EQ_INT(mr_tmr_configure(drv, &config), -MR_EBUSY);
    MR_TEST_ASSERT_EQ_INT(mr_tmr_is_busy(drv, &busy), 0);
    MR_TEST_ASSERT_EQ_INT(busy, MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_tmr_stop(drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_tmr_stop(drv), 0);
    MR_TEST_ASSERT_EQ_INT(tmr_stop_count, 1);
    tmr_busy_ret = -MR_EIO;
    MR_TEST_ASSERT_EQ_INT(mr_tmr_start(drv), -MR_EIO);
    mr_tmr_del(drv);
}

MR_TEST(tmr, event_paths) {
    mr_driver_t *drv;
    mr_tmr_ops_t optional_ops = tmr_ops;
    mr_waiter_t waiter;
    int event_count = 0;

    tmr_busy_ret = 0;
    drv = tmr_create(&tmr_ops);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_INT(mr_waiter_init(&waiter, tmr_waiter_entry,
                                         &event_count, MR_TMR_WAIT_TIMEOUT),
                          0);
    MR_TEST_ASSERT_EQ_INT(mr_tmr_event_attach(drv, &waiter), 0);
    MR_TEST_ASSERT_EQ_INT(mr_tmr_event_enable(drv, MR_TMR_WAIT_TIMEOUT), 0);
    MR_TEST_ASSERT_EQ_UINT(tmr_events, MR_TMR_WAIT_TIMEOUT);
    MR_TEST_ASSERT_EQ_INT(tmr_event_enable_state, MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_tmr_event_disable(drv, MR_TMR_WAIT_TIMEOUT), 0);
    MR_TEST_ASSERT_EQ_INT(tmr_event_enable_state, MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(mr_tmr_event_raise(drv, MR_TMR_WAIT_TIMEOUT), 0);
    MR_TEST_ASSERT_EQ_INT(event_count, 1);
    MR_TEST_ASSERT_EQ_INT(mr_waiter_del(&waiter), 0);
    optional_ops.event_control = MR_NULL;
    drv->ops = (mr_ptr_t)&optional_ops;
    MR_TEST_ASSERT_EQ_INT(mr_tmr_event_enable(drv, MR_TMR_WAIT_TIMEOUT),
                          -MR_ENOSYS);
    drv->ops = (mr_ptr_t)&tmr_ops;
    mr_tmr_del(drv);
}

#endif /* defined(MR_USE_TEST) && defined(MR_USE_DRIVER_TMR) */

/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_DRIVER_PWM)
#include <mrX/drivers/pwm/mr_pwm.h>
#include <mrX/libc/mr_malloc.h>

typedef struct test_pwm_context {
    mr_pwm_channel_config_t channel_config;
    mr_pwm_signal_config_t signal_config;
    mr_pwm_fault_config_t fault_config;
    mr_pwm_event_data_t event_data;
    mr_pwm_config_t config;
    mr_pwm_update_t update;
    mr_pwm_state_t state;
    mr_pwm_signal_t signal;
    mr_size_t update_count;
    mr_uint32_t update_calls;
    mr_uint32_t event_count;
    mr_uint32_t stop_mask;
    mr_uint32_t channel;
    mr_uint32_t events;
    mr_uint32_t fault;
    mr_bool_t signal_enabled;
    mr_bool_t event_enabled;
    mr_err_t state_error;
} test_pwm_context_t;

static mr_err_t test_pwm_configure(const mr_driver_t *drv,
                                   const mr_pwm_config_t *config) {
    test_pwm_context_t *ctx = MR_DRIVER_PRIV(drv);

    ctx->config = *config;
    return 0;
}

static mr_err_t test_pwm_configuration(const mr_driver_t *drv,
                                       mr_pwm_config_t *config) {
    test_pwm_context_t *ctx = MR_DRIVER_PRIV(drv);

    *config = ctx->config;
    return 0;
}

static mr_err_t
    test_pwm_channel_configure(const mr_driver_t *drv, mr_uint32_t channel,
                               const mr_pwm_channel_config_t *config) {
    test_pwm_context_t *ctx = MR_DRIVER_PRIV(drv);

    ctx->channel = channel;
    ctx->channel_config = *config;
    return 0;
}

static mr_err_t
    test_pwm_channel_configuration(const mr_driver_t *drv, mr_uint32_t channel,
                                   mr_pwm_channel_config_t *config) {
    test_pwm_context_t *ctx = MR_DRIVER_PRIV(drv);

    ctx->channel = channel;
    *config = ctx->channel_config;
    return 0;
}

static mr_err_t test_pwm_update(const mr_driver_t *drv,
                                const mr_pwm_update_t *updates,
                                mr_size_t count) {
    test_pwm_context_t *ctx = MR_DRIVER_PRIV(drv);

    ctx->update_calls++;
    ctx->update_count = count;
    if (count != 0U) {
        ctx->update = updates[count - 1U];
    }
    return 0;
}

static mr_err_t test_pwm_start(const mr_driver_t *drv, mr_uint32_t channels) {
    test_pwm_context_t *ctx = MR_DRIVER_PRIV(drv);

    ctx->state.running_mask |= channels;
    return 0;
}

static mr_err_t test_pwm_stop(const mr_driver_t *drv, mr_uint32_t channels) {
    test_pwm_context_t *ctx = MR_DRIVER_PRIV(drv);

    ctx->stop_mask = channels;
    ctx->state.running_mask &= ~channels;
    return 0;
}

static mr_err_t test_pwm_state(const mr_driver_t *drv, mr_pwm_state_t *state) {
    test_pwm_context_t *ctx = MR_DRIVER_PRIV(drv);

    if (ctx->state_error == 0) {
        *state = ctx->state;
    }
    return ctx->state_error;
}

static mr_err_t test_pwm_fault_configure(const mr_driver_t *drv,
                                         mr_uint32_t fault,
                                         const mr_pwm_fault_config_t *config) {
    test_pwm_context_t *ctx = MR_DRIVER_PRIV(drv);

    ctx->fault = fault;
    ctx->fault_config = *config;
    return 0;
}

static mr_err_t test_pwm_fault_configuration(const mr_driver_t *drv,
                                             mr_uint32_t fault,
                                             mr_pwm_fault_config_t *config) {
    test_pwm_context_t *ctx = MR_DRIVER_PRIV(drv);

    ctx->fault = fault;
    *config = ctx->fault_config;
    return 0;
}

static mr_err_t test_pwm_fault_clear(const mr_driver_t *drv,
                                     mr_uint32_t faults) {
    test_pwm_context_t *ctx = MR_DRIVER_PRIV(drv);

    ctx->state.fault_mask &= ~faults;
    return 0;
}

static mr_err_t
    test_pwm_signal_configure(const mr_driver_t *drv, mr_pwm_signal_t signal,
                              const mr_pwm_signal_config_t *config) {
    test_pwm_context_t *ctx = MR_DRIVER_PRIV(drv);

    ctx->signal = signal;
    ctx->signal_config = *config;
    return 0;
}

static mr_err_t test_pwm_signal_configuration(const mr_driver_t *drv,
                                              mr_pwm_signal_t signal,
                                              mr_pwm_signal_config_t *config) {
    test_pwm_context_t *ctx = MR_DRIVER_PRIV(drv);

    ctx->signal = signal;
    *config = ctx->signal_config;
    return 0;
}

static mr_err_t test_pwm_signal_control(const mr_driver_t *drv,
                                        mr_pwm_signal_t signal,
                                        mr_bool_t enable) {
    test_pwm_context_t *ctx = MR_DRIVER_PRIV(drv);

    ctx->signal = signal;
    ctx->signal_enabled = enable;
    return 0;
}

static mr_err_t test_pwm_event_control(const mr_driver_t *drv,
                                       mr_uint32_t events, mr_bool_t enable) {
    test_pwm_context_t *ctx = MR_DRIVER_PRIV(drv);

    ctx->events = events;
    ctx->event_enabled = enable;
    return 0;
}

static void test_pwm_waiter(mr_waiter_t *waiter, void *param,
                            mr_wait_event_t event, const void *data) {
    const mr_pwm_event_data_t *payload = data;
    test_pwm_context_t *ctx = param;

    MR_IGNORE(waiter);
    ctx->event_count++;
    ctx->events = event;
    ctx->event_data = payload ? *payload : (mr_pwm_event_data_t){0};
}

static const mr_pwm_ops_t __test_pwm_ops = {
    .configure = test_pwm_configure,
    .configuration = test_pwm_configuration,
    .channel_configure = test_pwm_channel_configure,
    .channel_configuration = test_pwm_channel_configuration,
    .update = test_pwm_update,
    .start = test_pwm_start,
    .stop = test_pwm_stop,
    .state = test_pwm_state,
    .fault_configure = test_pwm_fault_configure,
    .fault_configuration = test_pwm_fault_configuration,
    .fault_clear = test_pwm_fault_clear,
    .signal_configure = test_pwm_signal_configure,
    .signal_configuration = test_pwm_signal_configuration,
    .signal_control = test_pwm_signal_control,
    .event_control = test_pwm_event_control,
};

static mr_pwm_data_t __test_pwm_defined_data = MR_PWM_DATA_INIT(5U, 4U, 2U);
static mr_pwm_data_t __test_pwm_installed_data = MR_PWM_DATA_INIT(5U, 4U, 2U);
static const mr_pwm_ops_t __test_pwm_empty_ops;

MR_PWM_DEFINE(test_pwm_defined, &__test_pwm_defined_data, MR_NULL,
              &__test_pwm_empty_ops);
MR_PWM_INSTALL(test_pwm_installed, &__test_pwm_installed_data, MR_NULL,
               &__test_pwm_empty_ops, MR_DRIVER_LEVEL_DEFER);

MR_TEST(pwm, controller_and_channel_configuration) {
    mr_pwm_channel_config_t channel, saved_channel;
    mr_pwm_config_t config, saved;
    test_pwm_context_t ctx;
    mr_pwm_state_t state;
    mr_pwm_data_t data;
    mr_driver_t drv;

    data = (mr_pwm_data_t)MR_PWM_DATA_INIT(5U, 4U, 2U);
    ctx = (test_pwm_context_t){0};
    config = (mr_pwm_config_t){.period = 1000U,
                               .cycles = 8U,
                               .alignment = MR_PWM_ALIGN_CENTER,
                               .update_mode = MR_PWM_UPDATE_PERIOD};
    channel = (mr_pwm_channel_config_t){
        .pulse = 500U,
        .phase = 10U,
        .comp = 1U,
    };
    saved = (mr_pwm_config_t){0};
    saved_channel = (mr_pwm_channel_config_t){0};
    state = (mr_pwm_state_t){0};
    MR_TEST_ASSERT_EQ_INT(
        mr_pwm_init(&drv, &data, &ctx, (mr_pwm_ops_t *)&__test_pwm_ops), 0);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(&drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_configure(&drv, &config), 0);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_configuration(&drv, &saved), 0);
    MR_TEST_EXPECT_EQ_UINT(saved.period, 1000U);
    MR_TEST_EXPECT_EQ_UINT(saved.cycles, 8U);
    MR_TEST_EXPECT_EQ_UINT(saved.alignment, MR_PWM_ALIGN_CENTER);
    MR_TEST_EXPECT_EQ_UINT(saved.update_mode, MR_PWM_UPDATE_PERIOD);
    config.period = 0U;
    MR_TEST_EXPECT_EQ_INT(mr_pwm_configure(&drv, &config), -MR_EINVAL);
    config.period = 2000U;
    config.alignment = 3U;
    MR_TEST_EXPECT_EQ_INT(mr_pwm_configure(&drv, &config), -MR_EINVAL);
    config.alignment = MR_PWM_ALIGN_EDGE_UP;

    MR_TEST_EXPECT_EQ_INT(mr_pwm_channel_configure(&drv, 2U, &channel), 0);
    MR_TEST_EXPECT_EQ_UINT(ctx.channel, 2U);
    MR_TEST_EXPECT_EQ_INT(
        mr_pwm_channel_configuration(&drv, 2U, &saved_channel), 0);
    MR_TEST_EXPECT_EQ_UINT(saved_channel.pulse, 500U);
    MR_TEST_EXPECT_EQ_UINT(saved_channel.phase, 10U);
    MR_TEST_EXPECT_EQ_UINT(saved_channel.comp, 1U);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_channel_configure(&drv, 0U, &channel),
                          -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_channel_configure(&drv, 1U, &channel),
                          -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(
        mr_pwm_channel_configuration(&drv, 32U, &saved_channel), -MR_EINVAL);
    channel.comp_idle = 3U;
    MR_TEST_EXPECT_EQ_INT(mr_pwm_channel_configure(&drv, 2U, &channel),
                          -MR_EINVAL);
    channel.comp_idle = MR_PWM_IDLE_HIGH_Z;

    ctx.state_error = -MR_EIO;
    MR_TEST_EXPECT_EQ_INT(mr_pwm_state(&drv, &state), -MR_EIO);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_configure(&drv, &config), -MR_EIO);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_channel_configure(&drv, 2U, &channel),
                          -MR_EIO);
    ctx.state_error = 0;
    MR_TEST_EXPECT_EQ_INT(mr_pwm_start(&drv, 4U), 0);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_configure(&drv, &config), -MR_EBUSY);
    MR_TEST_EXPECT_EQ_UINT(ctx.config.period, 1000U);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_channel_configure(&drv, 2U, &channel),
                          -MR_EBUSY);
    channel.comp = 0U;
    MR_TEST_EXPECT_EQ_INT(mr_pwm_channel_configure(&drv, 0U, &channel), 0);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_stop(&drv, 4U), 0);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_del(&drv), 0);
}

MR_TEST(pwm, update_and_run_mask) {
    mr_pwm_update_t updates[2];
    test_pwm_context_t ctx;
    mr_pwm_state_t state;
    mr_pwm_data_t data;
    mr_driver_t drv;

    data = (mr_pwm_data_t)MR_PWM_DATA_INIT(0U, 0U, 0U);
    ctx = (test_pwm_context_t){0};
    state = (mr_pwm_state_t){0};
    updates[0] = (mr_pwm_update_t){.channel = 0U, .pulse = 100U};
    updates[1] = (mr_pwm_update_t){.channel = 32U, .pulse = 200U};
    MR_TEST_ASSERT_EQ_INT(
        mr_pwm_init(&drv, &data, &ctx, (mr_pwm_ops_t *)&__test_pwm_ops), 0);
    MR_TEST_EXPECT_EQ_INT(mr_driver_setup(&drv), -MR_EINVAL);
    data.channel_mask = 5U;
    data.comp_mask = 2U;
    MR_TEST_EXPECT_EQ_INT(mr_driver_setup(&drv), -MR_EINVAL);
    data.comp_mask = 4U;
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(&drv), 0);

    MR_TEST_EXPECT_EQ_INT(mr_pwm_update(&drv, MR_NULL, 0U), 0);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_update(&drv, updates, MR_ARRAY_SIZE(updates)),
                          -MR_EINVAL);
    MR_TEST_EXPECT_EQ_UINT(ctx.update_calls, 0U);
    updates[1].channel = 2U;
    MR_TEST_EXPECT_EQ_INT(mr_pwm_update(&drv, updates, MR_ARRAY_SIZE(updates)),
                          0);
    MR_TEST_EXPECT_EQ_UINT(ctx.update_calls, 1U);
    MR_TEST_EXPECT_EQ_UINT(ctx.update_count, 2U);
    MR_TEST_EXPECT_EQ_UINT(ctx.update.channel, 2U);
    MR_TEST_EXPECT_EQ_UINT(ctx.update.pulse, 200U);

    MR_TEST_EXPECT_EQ_INT(mr_pwm_start(&drv, 0U), -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_start(&drv, 3U), -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_stop(&drv, 0U), -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_stop(&drv, 3U), -MR_EINVAL);
    ctx.state_error = -MR_EIO;
    MR_TEST_EXPECT_EQ_INT(mr_pwm_start(&drv, 4U), -MR_EIO);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_stop(&drv, 4U), -MR_EIO);
    ctx.state_error = 0;
    MR_TEST_EXPECT_EQ_INT(mr_pwm_start(&drv, 4U), 0);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_start(&drv, 5U), -MR_EBUSY);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_state(&drv, &state), 0);
    MR_TEST_EXPECT_EQ_UINT(state.running_mask, 4U);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_stop(&drv, 5U), 0);
    MR_TEST_EXPECT_EQ_UINT(ctx.stop_mask, 4U);
    ctx.stop_mask = 0U;
    MR_TEST_EXPECT_EQ_INT(mr_pwm_stop(&drv, 4U), 0);
    MR_TEST_EXPECT_EQ_UINT(ctx.stop_mask, 0U);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_del(&drv), 0);
}

MR_TEST(pwm, faults_signals_events_and_references) {
    mr_pwm_signal_config_t signal, saved_signal;
    mr_pwm_fault_config_t fault, saved_fault;
    mr_pwm_event_data_t payload;
    test_pwm_context_t ctx;
    mr_ht_pwm_spec_t spec;
    mr_pwm_state_t state;
    mr_pwm_data_t *data;
    mr_waiter_t waiter;
    mr_driver_t *drv;
    mr_err_t ret;

    ctx = (test_pwm_context_t){0};
    ctx.state.fault_mask = 2U;
    fault = (mr_pwm_fault_config_t){
        .filter = 5U, .mode = MR_PWM_FAULT_LATCHED, .enable = 1U};
    saved_fault = (mr_pwm_fault_config_t){0};
    signal = (mr_pwm_signal_config_t){.offset = 33U};
    saved_signal = (mr_pwm_signal_config_t){0};
    state = (mr_pwm_state_t){0};
    payload = (mr_pwm_event_data_t){.channel_mask = 4U, .fault_mask = 2U};
    data = mr_malloc(sizeof(*data));
    MR_TEST_ASSERT_NE_PTR(data, MR_NULL);
    *data = (mr_pwm_data_t)MR_PWM_DATA_INIT(5U, 4U, 2U);
    drv = mr_pwm_create(data, &ctx, (mr_pwm_ops_t *)&__test_pwm_ops);
    if (!drv) {
        mr_free(data);
        MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    }
    ret = mr_driver_setup(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    spec = (mr_ht_pwm_spec_t){.drv = drv, .channel = 2U};

    MR_TEST_EXPECT_EQ_INT(mr_pwm_fault_configure(drv, 0U, &fault), -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_fault_configure(drv, 1U, &fault), 0);
    MR_TEST_EXPECT_EQ_UINT(ctx.fault, 1U);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_fault_configuration(drv, 32U, &saved_fault),
                          -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_fault_configuration(drv, 1U, &saved_fault), 0);
    MR_TEST_EXPECT_EQ_UINT(saved_fault.filter, 5U);
    MR_TEST_EXPECT_EQ_UINT(saved_fault.mode, MR_PWM_FAULT_LATCHED);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_fault_clear(drv, 3U), -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(mr_ht_pwm_state(&spec, &state), 0);
    MR_TEST_EXPECT_EQ_UINT(state.fault_mask, 2U);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_fault_clear(drv, 2U), 0);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_state(drv, &state), 0);
    MR_TEST_EXPECT_EQ_UINT(state.fault_mask, 0U);

    MR_TEST_EXPECT_EQ_INT(
        mr_pwm_signal_configure(drv, MR_PWM_SIGNAL_PERIOD, &signal),
        -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(
        mr_pwm_signal_configuration(drv, (mr_pwm_signal_t)2, &saved_signal),
        -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_signal_enable(drv, (mr_pwm_signal_t)2),
                          -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(
        mr_ht_pwm_signal_configure(&spec, MR_PWM_SIGNAL_COMPARE, &signal), 0);
    MR_TEST_EXPECT_EQ_INT(mr_ht_pwm_signal_configuration(
                              &spec, MR_PWM_SIGNAL_COMPARE, &saved_signal),
                          0);
    MR_TEST_EXPECT_EQ_UINT(saved_signal.offset, 33U);
    MR_TEST_EXPECT_EQ_INT(mr_ht_pwm_signal_enable(&spec, MR_PWM_SIGNAL_COMPARE),
                          0);
    MR_TEST_EXPECT_EQ_INT(ctx.signal, MR_PWM_SIGNAL_COMPARE);
    MR_TEST_EXPECT_EQ_INT(ctx.signal_enabled, MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(mr_ht_pwm_signal_disable(&spec, MR_PWM_SIGNAL_PERIOD),
                          0);
    MR_TEST_EXPECT_EQ_INT(ctx.signal, MR_PWM_SIGNAL_PERIOD);
    MR_TEST_EXPECT_EQ_INT(ctx.signal_enabled, MR_FALSE);

    ret = mr_waiter_init(&waiter, test_pwm_waiter, &ctx, MR_PWM_WAIT_FAULT);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        goto _exit;
    }
    MR_TEST_EXPECT_EQ_INT(mr_ht_pwm_event_attach(&spec, &waiter), 0);
    MR_TEST_EXPECT_EQ_INT(mr_ht_pwm_event_enable(&spec, MR_PWM_WAIT_FAULT), 0);
    MR_TEST_EXPECT_EQ_UINT(ctx.events, MR_PWM_WAIT_FAULT);
    MR_TEST_EXPECT_EQ_INT(ctx.event_enabled, MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_event_raise(drv, MR_PWM_WAIT_PERIOD, MR_NULL),
                          0);
    MR_TEST_EXPECT_EQ_UINT(ctx.event_count, 0U);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_event_raise(drv, MR_PWM_WAIT_FAULT, &payload),
                          0);
    MR_TEST_EXPECT_EQ_UINT(ctx.event_count, 1U);
    MR_TEST_EXPECT_EQ_UINT(ctx.events, MR_PWM_WAIT_FAULT);
    MR_TEST_EXPECT_EQ_UINT(ctx.event_data.channel_mask, 4U);
    MR_TEST_EXPECT_EQ_UINT(ctx.event_data.fault_mask, 2U);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_event_raise(drv, MR_PWM_WAIT_FAULT, MR_NULL),
                          0);
    MR_TEST_EXPECT_EQ_UINT(ctx.event_count, 2U);
    MR_TEST_EXPECT_EQ_UINT(ctx.event_data.channel_mask, 0U);
    MR_TEST_EXPECT_EQ_INT(mr_ht_pwm_event_disable(&spec, MR_PWM_WAIT_FAULT), 0);
    MR_TEST_EXPECT_EQ_INT(ctx.event_enabled, MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(mr_waiter_del(&waiter), 0);

    MR_TEST_EXPECT_EQ_PTR(mr_pwm_get(drv), drv);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_put(drv), MR_FALSE);

_exit:
    MR_TEST_EXPECT_EQ_INT(mr_pwm_del(drv), 0);
}

MR_TEST(pwm, optional_operations) {
    mr_pwm_signal_config_t signal;
    mr_pwm_fault_config_t fault;
    mr_driver_t *drv, *registered;

    signal = (mr_pwm_signal_config_t){0};
    fault = (mr_pwm_fault_config_t){0};
    drv = MR_DRIVER_FIND(test_pwm_defined);
    MR_TEST_EXPECT_EQ_PTR(mr_driver_find("test_pwm_defined"), MR_NULL);
    registered = mr_driver_find("test_pwm_installed");
    MR_TEST_ASSERT_NE_PTR(registered, MR_NULL);
    MR_TEST_EXPECT_EQ_PTR(registered, MR_DRIVER_FIND(test_pwm_installed));
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(registered), MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_put(registered), MR_FALSE);

    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_update(drv, MR_NULL, 0U), -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_fault_configure(drv, 1U, &fault), -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_fault_configuration(drv, 1U, &fault),
                          -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_fault_clear(drv, 2U), -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(
        mr_pwm_signal_configure(drv, MR_PWM_SIGNAL_PERIOD, &signal),
        -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(
        mr_pwm_signal_configuration(drv, MR_PWM_SIGNAL_PERIOD, &signal),
        -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_signal_enable(drv, MR_PWM_SIGNAL_PERIOD),
                          -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_signal_disable(drv, MR_PWM_SIGNAL_PERIOD),
                          -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_event_enable(drv, MR_PWM_WAIT_PERIOD),
                          -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_pwm_event_disable(drv, MR_PWM_WAIT_PERIOD),
                          -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(drv), 0);
}

#define MR_HT_COMPAT test_driver_pwm
#if MR_HT_HAS_DRIVER()
static test_pwm_context_t __test_pwm_hardtree_ctx;

MR_HT_DRIVER_FOREACH(MR_HT_PWM_INSTALL, &__test_pwm_hardtree_ctx,
                     &__test_pwm_ops);

static mr_ht_pwm_spec_t __test_pwm_specs[] = {
    MR_HT_PWM_SPEC_FOREACH(0, specs),
};

MR_TEST(pwm, hardtree_specifications) {
    mr_pwm_channel_config_t channel, saved_channel;
    mr_pwm_config_t config, saved;
    mr_ht_pwm_spec_t spec, *comp;
    mr_pwm_state_t state;

    __test_pwm_hardtree_ctx = (test_pwm_context_t){0};
    spec = (mr_ht_pwm_spec_t)MR_HT_PWM_SPEC(0, specs);
    comp = &__test_pwm_specs[1];
    config = (mr_pwm_config_t){.period = 1000U};
    channel = (mr_pwm_channel_config_t){.pulse = 50U, .comp = 1U};
    saved = (mr_pwm_config_t){0};
    saved_channel = (mr_pwm_channel_config_t){0};
    state = (mr_pwm_state_t){0};
    MR_TEST_EXPECT_EQ_UINT(MR_ARRAY_SIZE(__test_pwm_specs), 2U);
    MR_TEST_EXPECT_EQ_PTR(spec.drv, MR_HT_DRIVER_FIND(MR_HT_LABEL(pwm_test)));
    MR_TEST_EXPECT_EQ_PTR(comp->drv, spec.drv);
    MR_TEST_EXPECT_EQ_UINT(spec.channel, 0U);
    MR_TEST_EXPECT_EQ_UINT(__test_pwm_specs[0].channel, 0U);
    MR_TEST_EXPECT_EQ_UINT(comp->channel, 2U);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(spec.drv), 0);

    MR_TEST_EXPECT_EQ_INT(mr_ht_pwm_configure(&spec, &config), 0);
    MR_TEST_EXPECT_EQ_INT(mr_ht_pwm_configuration(comp, &saved), 0);
    MR_TEST_EXPECT_EQ_UINT(saved.period, 1000U);
    MR_TEST_EXPECT_EQ_INT(mr_ht_pwm_channel_configure(&spec, &channel),
                          -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_ht_pwm_channel_configure(comp, &channel), 0);
    MR_TEST_EXPECT_EQ_INT(mr_ht_pwm_channel_configuration(comp, &saved_channel),
                          0);
    MR_TEST_EXPECT_EQ_UINT(saved_channel.pulse, 50U);
    MR_TEST_EXPECT_EQ_UINT(__test_pwm_hardtree_ctx.channel, 2U);
    MR_TEST_EXPECT_EQ_INT(mr_ht_pwm_start(comp), 0);
    MR_TEST_EXPECT_EQ_INT(mr_ht_pwm_update(comp, 60U, 7U), 0);
    MR_TEST_EXPECT_EQ_UINT(__test_pwm_hardtree_ctx.update.channel, 2U);
    MR_TEST_EXPECT_EQ_UINT(__test_pwm_hardtree_ctx.update.pulse, 60U);
    MR_TEST_EXPECT_EQ_UINT(__test_pwm_hardtree_ctx.update.phase, 7U);
    MR_TEST_EXPECT_EQ_INT(mr_ht_pwm_state(&spec, &state), 0);
    MR_TEST_EXPECT_EQ_UINT(state.running_mask, 4U);
    MR_TEST_EXPECT_EQ_INT(mr_ht_pwm_stop(comp), 0);
    spec.channel = 32U;
    MR_TEST_EXPECT_EQ_INT(mr_ht_pwm_start(&spec), -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(mr_ht_pwm_stop(&spec), -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(spec.drv), 0);
}
#endif /* MR_HT_HAS_DRIVER() */
#undef MR_HT_COMPAT

#endif /* defined(MR_USE_TEST) && defined(MR_USE_DRIVER_PWM) */

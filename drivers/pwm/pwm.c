/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/pwm/mr_pwm.h>
#if defined(MR_USE_DRIVER_PWM)
#include <mrX/libc/mr_malloc.h>

/* PWM driver clazzes(static & dynamic) */
static mr_err_t pwm_init(const mr_driver_t *drv);
static mr_err_t pwm_deinit(const mr_driver_t *drv);
static void pwm_release(const mr_driver_t *drv);
MR_DRIVER_CLAZZES_DEFINE(pwm, pwm_init, pwm_deinit, pwm_release);

static mr_bool_t pwm_mask_valid(mr_uint32_t requested, mr_uint32_t available) {
    return (requested != 0U) && ((requested & ~available) == 0U);
}

static mr_bool_t pwm_index_valid(mr_uint32_t index, mr_uint32_t available) {
    return (index < 32U) && ((available & (1U << index)) != 0U);
}

mr_err_t mr_pwm_configure(mr_driver_t *drv, const mr_pwm_config_t *config) {
    const mr_pwm_ops_t *pwm_ops;
    mr_pwm_state_t state;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_PWM);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get PWM operations */
    pwm_ops = (const mr_pwm_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(pwm_ops->configure != MR_NULL);
    MR_ASSERT(pwm_ops->state != MR_NULL);

    /* Check configuration */
    if ((config->period == 0U) || (config->alignment > MR_PWM_ALIGN_CENTER)) {
        return -MR_EINVAL;
    }

    /* Check whether any PWM channel is running */
    ret = pwm_ops->state(drv, &state);
    if (ret != 0) {
        return ret;
    }
    if (state.running_mask != 0U) {
        return -MR_EBUSY;
    }

    /* Configure PWM controller */
    return pwm_ops->configure(drv, config);
}

mr_err_t mr_pwm_configuration(mr_driver_t *drv, mr_pwm_config_t *config) {
    const mr_pwm_ops_t *pwm_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_PWM);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get PWM operations */
    pwm_ops = (const mr_pwm_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(pwm_ops->configuration != MR_NULL);

    /* Get PWM controller configuration */
    return pwm_ops->configuration(drv, config);
}

mr_err_t mr_pwm_channel_configure(mr_driver_t *drv, mr_uint32_t channel,
                                  const mr_pwm_channel_config_t *config) {
    const mr_pwm_ops_t *pwm_ops;
    mr_pwm_data_t *pwm_data;
    mr_pwm_state_t state;
    mr_uint32_t channel_bit;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_PWM);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get PWM operations and data */
    pwm_ops = (const mr_pwm_ops_t *)MR_DRIVER_OPS(drv);
    pwm_data = (mr_pwm_data_t *)MR_DRIVER_DATA(drv);
    MR_ASSERT(pwm_ops->channel_configure != MR_NULL);
    MR_ASSERT(pwm_ops->state != MR_NULL);

    /* Check whether channel exists */
    if (!pwm_index_valid(channel, pwm_data->channel_mask)) {
        return -MR_EINVAL;
    }
    channel_bit = 1U << channel;

    /* Check channel configuration */
    if ((config->idle > MR_PWM_IDLE_HIGH_Z)
        || (config->comp && (config->comp_idle > MR_PWM_IDLE_HIGH_Z))) {
        return -MR_EINVAL;
    }

    /* Check whether complementary output exists */
    if (config->comp && ((pwm_data->comp_mask & channel_bit) == 0U)) {
        return -MR_ENOSYS;
    }

    /* Check whether PWM channel is running */
    ret = pwm_ops->state(drv, &state);
    if (ret != 0) {
        return ret;
    }
    if ((state.running_mask & channel_bit) != 0U) {
        return -MR_EBUSY;
    }

    /* Configure PWM channel */
    return pwm_ops->channel_configure(drv, channel, config);
}

mr_err_t mr_pwm_channel_configuration(mr_driver_t *drv, mr_uint32_t channel,
                                      mr_pwm_channel_config_t *config) {
    const mr_pwm_ops_t *pwm_ops;
    mr_pwm_data_t *pwm_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_PWM);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get PWM operations and data */
    pwm_ops = (const mr_pwm_ops_t *)MR_DRIVER_OPS(drv);
    pwm_data = (mr_pwm_data_t *)MR_DRIVER_DATA(drv);
    MR_ASSERT(pwm_ops->channel_configuration != MR_NULL);

    /* Check whether channel exists */
    if (!pwm_index_valid(channel, pwm_data->channel_mask)) {
        return -MR_EINVAL;
    }

    /* Get PWM channel configuration */
    return pwm_ops->channel_configuration(drv, channel, config);
}

mr_err_t mr_pwm_update(mr_driver_t *drv, const mr_pwm_update_t *updates,
                       mr_size_t count) {
    const mr_pwm_ops_t *pwm_ops;
    mr_pwm_data_t *pwm_data;
    mr_size_t i;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_PWM);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT((updates != MR_NULL) || (count == 0U));

    /* Get PWM operations and data */
    pwm_ops = (const mr_pwm_ops_t *)MR_DRIVER_OPS(drv);
    pwm_data = (mr_pwm_data_t *)MR_DRIVER_DATA(drv);

    /* Check whether operation is supported */
    if (!pwm_ops->update) {
        return -MR_ENOSYS;
    }

    /* Check update transaction */
    if (count == 0U) {
        /* An empty transaction performs no update */
        return 0;
    }
    for (i = 0U; i < count; i++) {
        if (!pwm_index_valid(updates[i].channel, pwm_data->channel_mask)) {
            return -MR_EINVAL;
        }
    }

    /* Update PWM channels */
    return pwm_ops->update(drv, updates, count);
}

mr_err_t mr_pwm_start(mr_driver_t *drv, mr_uint32_t channels) {
    const mr_pwm_ops_t *pwm_ops;
    mr_pwm_data_t *pwm_data;
    mr_pwm_state_t state;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_PWM);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get PWM operations and data */
    pwm_ops = (const mr_pwm_ops_t *)MR_DRIVER_OPS(drv);
    pwm_data = (mr_pwm_data_t *)MR_DRIVER_DATA(drv);
    MR_ASSERT(pwm_ops->start != MR_NULL);
    MR_ASSERT(pwm_ops->state != MR_NULL);

    /* Check channel mask */
    if (!pwm_mask_valid(channels, pwm_data->channel_mask)) {
        return -MR_EINVAL;
    }

    /* Check whether any requested channel is already running */
    ret = pwm_ops->state(drv, &state);
    if (ret != 0) {
        return ret;
    }
    if ((state.running_mask & channels) != 0U) {
        return -MR_EBUSY;
    }

    /* Start PWM channels */
    return pwm_ops->start(drv, channels);
}

mr_err_t mr_pwm_stop(mr_driver_t *drv, mr_uint32_t channels) {
    const mr_pwm_ops_t *pwm_ops;
    mr_pwm_data_t *pwm_data;
    mr_pwm_state_t state;
    mr_uint32_t running;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_PWM);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get PWM operations and data */
    pwm_ops = (const mr_pwm_ops_t *)MR_DRIVER_OPS(drv);
    pwm_data = (mr_pwm_data_t *)MR_DRIVER_DATA(drv);
    MR_ASSERT(pwm_ops->stop != MR_NULL);
    MR_ASSERT(pwm_ops->state != MR_NULL);

    /* Check channel mask */
    if (!pwm_mask_valid(channels, pwm_data->channel_mask)) {
        return -MR_EINVAL;
    }

    /* Select requested channels that are currently running */
    ret = pwm_ops->state(drv, &state);
    if (ret != 0) {
        return ret;
    }
    running = state.running_mask & channels;
    if (running == 0U) {
        return 0;
    }

    /* Stop PWM channels */
    return pwm_ops->stop(drv, running);
}

mr_err_t mr_pwm_fault_configure(mr_driver_t *drv, mr_uint32_t fault,
                                const mr_pwm_fault_config_t *config) {
    const mr_pwm_ops_t *pwm_ops;
    mr_pwm_data_t *pwm_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_PWM);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get PWM operations and data */
    pwm_ops = (const mr_pwm_ops_t *)MR_DRIVER_OPS(drv);
    pwm_data = (mr_pwm_data_t *)MR_DRIVER_DATA(drv);

    /* Check whether operation is supported */
    if (!pwm_ops->fault_configure) {
        return -MR_ENOSYS;
    }

    /* Check whether fault input exists */
    if (!pwm_index_valid(fault, pwm_data->fault_mask)) {
        return -MR_EINVAL;
    }

    /* Configure PWM fault input */
    return pwm_ops->fault_configure(drv, fault, config);
}

mr_err_t mr_pwm_fault_configuration(mr_driver_t *drv, mr_uint32_t fault,
                                    mr_pwm_fault_config_t *config) {
    const mr_pwm_ops_t *pwm_ops;
    mr_pwm_data_t *pwm_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_PWM);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get PWM operations and data */
    pwm_ops = (const mr_pwm_ops_t *)MR_DRIVER_OPS(drv);
    pwm_data = (mr_pwm_data_t *)MR_DRIVER_DATA(drv);

    /* Check whether operation is supported */
    if (!pwm_ops->fault_configuration) {
        return -MR_ENOSYS;
    }

    /* Check whether fault input exists */
    if (!pwm_index_valid(fault, pwm_data->fault_mask)) {
        return -MR_EINVAL;
    }

    /* Get PWM fault input configuration */
    return pwm_ops->fault_configuration(drv, fault, config);
}

mr_err_t mr_pwm_fault_clear(mr_driver_t *drv, mr_uint32_t faults) {
    const mr_pwm_ops_t *pwm_ops;
    mr_pwm_data_t *pwm_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_PWM);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get PWM operations and data */
    pwm_ops = (const mr_pwm_ops_t *)MR_DRIVER_OPS(drv);
    pwm_data = (mr_pwm_data_t *)MR_DRIVER_DATA(drv);

    /* Check whether operation is supported */
    if (!pwm_ops->fault_clear) {
        return -MR_ENOSYS;
    }

    /* Check fault mask */
    if (!pwm_mask_valid(faults, pwm_data->fault_mask)) {
        return -MR_EINVAL;
    }

    /* Clear PWM fault inputs */
    return pwm_ops->fault_clear(drv, faults);
}

mr_err_t mr_pwm_signal_configure(mr_driver_t *drv, mr_pwm_signal_t signal,
                                 const mr_pwm_signal_config_t *config) {
    const mr_pwm_ops_t *pwm_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_PWM);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get PWM operations */
    pwm_ops = (const mr_pwm_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether operation is supported */
    if (!pwm_ops->signal_configure) {
        return -MR_ENOSYS;
    }

    /* Check signal and configuration */
    if (((signal != MR_PWM_SIGNAL_PERIOD) && (signal != MR_PWM_SIGNAL_COMPARE))
        || ((signal == MR_PWM_SIGNAL_PERIOD) && (config->offset != 0U))) {
        return -MR_EINVAL;
    }

    /* Configure PWM signal */
    return pwm_ops->signal_configure(drv, signal, config);
}

mr_err_t mr_pwm_signal_configuration(mr_driver_t *drv, mr_pwm_signal_t signal,
                                     mr_pwm_signal_config_t *config) {
    const mr_pwm_ops_t *pwm_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_PWM);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get PWM operations */
    pwm_ops = (const mr_pwm_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether operation is supported */
    if (!pwm_ops->signal_configuration) {
        return -MR_ENOSYS;
    }

    /* Check signal */
    if ((signal != MR_PWM_SIGNAL_PERIOD) && (signal != MR_PWM_SIGNAL_COMPARE)) {
        return -MR_EINVAL;
    }

    /* Get PWM signal configuration */
    return pwm_ops->signal_configuration(drv, signal, config);
}

static mr_err_t pwm_init(const mr_driver_t *drv) {
    mr_pwm_data_t *pwm_data;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get PWM data */
    pwm_data = (mr_pwm_data_t *)MR_DRIVER_DATA(drv);

    /* Check PWM instance resources */
    if ((pwm_data->channel_mask == 0U)
        || ((pwm_data->comp_mask & ~pwm_data->channel_mask) != 0U)) {
        return -MR_EINVAL;
    }

    /* Init PWM event */
    ret = mr_waitable_init(&pwm_data->event);
    if (ret != 0) {
        return ret;
    }
    return 0;
}

static mr_err_t pwm_deinit(const mr_driver_t *drv) {
    mr_pwm_data_t *pwm_data;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get PWM data */
    pwm_data = (mr_pwm_data_t *)MR_DRIVER_DATA(drv);

    /* Deinit PWM event */
    mr_waitable_del(&pwm_data->event);
    return 0;
}

static void pwm_release(const mr_driver_t *drv) {
    /* Free PWM data */
    mr_free(MR_DRIVER_DATA(drv));
}
#endif /* defined(MR_USE_DRIVER_PWM) */

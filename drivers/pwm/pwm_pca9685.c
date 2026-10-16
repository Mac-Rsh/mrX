/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/pwm/mr_pwm.h>
#if defined(MR_USE_DRIVER_PWM) && defined(MR_USE_DRIVER_I2C)
#include <mrX/drivers/i2c/mr_i2c.h>
#include <mrX/kernel/mr_clock.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    pwm_pca9685
#if MR_HT_HAS_DRIVER()

/* Device definition */
#define PCA9685_ADDR_MIN                0x40U
#define PCA9685_ADDR_MAX                0x77U
#define PCA9685_ADDR_ALL_CALL           0x70U
#define PCA9685_OSCILLATOR_MIN          1000000U
#define PCA9685_OSCILLATOR_MAX          50000000U
#define PCA9685_CHANNEL_COUNT           16U
#define PCA9685_COUNTER_TICKS           4096U
#define PCA9685_REG_MODE1               0x00U
#define PCA9685_REG_MODE2               0x01U
#define PCA9685_REG_LED0_ON_L           0x06U
#define PCA9685_REG_ALL_LED_ON_L        0xfaU
#define PCA9685_REG_PRESCALE            0xfeU
#define PCA9685_MODE1_SLEEP_AI          0x30U
#define PCA9685_MODE1_AI                0x20U
#define PCA9685_MODE1_RESTART_AI        0xa0U
#define PCA9685_MODE2_OUTDRV            0x04U
#define PCA9685_FULL_BIT                0x10U

/* Driver PWM private data type */
typedef struct drv_pwm_pca9685_priv {
    mr_driver_t *i2c;
    mr_uint32_t addr;
    mr_uint32_t oscillator_hz;
    mr_pwm_config_t config;
    mr_pwm_channel_config_t channels[PCA9685_CHANNEL_COUNT];
    mr_uint8_t states[PCA9685_CHANNEL_COUNT];
    mr_uint32_t configured_mask;
    mr_uint32_t running_mask;
    mr_bool_t ready;
} drv_pwm_pca9685_priv_t;

/* Driver PWM operations */
static mr_err_t drv_configure(const mr_driver_t *drv,
                              const mr_pwm_config_t *config);
static mr_err_t drv_configuration(const mr_driver_t *drv,
                                  mr_pwm_config_t *config);
static mr_err_t drv_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_pwm_channel_config_t *config);
static mr_err_t drv_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_pwm_channel_config_t *config);
static mr_err_t drv_update(const mr_driver_t *drv,
                           const mr_pwm_update_t *updates, mr_size_t count);
static mr_err_t drv_start(const mr_driver_t *drv, mr_uint32_t channels);
static mr_err_t drv_stop(const mr_driver_t *drv, mr_uint32_t channels);
static mr_err_t drv_state(const mr_driver_t *drv, mr_pwm_state_t *state);
MR_UNUSED static const mr_pwm_ops_t __drv_pwm_pca9685_ops = {
    .configure = drv_configure,
    .configuration = drv_configuration,
    .channel_configure = drv_channel_configure,
    .channel_configuration = drv_channel_configuration,
    .update = drv_update,
    .start = drv_start,
    .stop = drv_stop,
    .state = drv_state,
    .fault_configure = MR_NULL,
    .fault_configuration = MR_NULL,
    .fault_clear = MR_NULL,
    .signal_configure = MR_NULL,
    .signal_configuration = MR_NULL,
    .signal_control = MR_NULL,
    .event_control = MR_NULL,
};

static mr_err_t pca9685_reg_write(drv_pwm_pca9685_priv_t *priv, mr_uint8_t reg,
                                  mr_uint8_t value) {
    mr_uint8_t tx[2];
    mr_i2c_msg_t msg;
    mr_ssize_t ret;

    /* Build register write frame */
    tx[0] = reg;
    tx[1] = value;

    /* Transfer frame */
    msg.addr = (mr_uint16_t)priv->addr;
    msg.flags = MR_I2C_MSG_STOP;
    msg.buf = tx;
    msg.len = sizeof(tx);
    ret = mr_i2c_transfer(priv->i2c, &msg, 1U);
    if (ret < 0) {
        return (mr_err_t)ret;
    }
    if (ret != 1) {
        return -MR_EIO;
    }
    return 0;
}

static mr_uint32_t pca9685_time_to_ticks(mr_uint64_t time, mr_uint64_t period) {
    return (mr_uint32_t)((time * PCA9685_COUNTER_TICKS + period / 2U) / period);
}

static mr_uint64_t pca9685_ticks_to_time(mr_uint32_t ticks,
                                         mr_uint64_t period) {
    return (ticks * period + PCA9685_COUNTER_TICKS / 2U)
         / PCA9685_COUNTER_TICKS;
}

/* Stage every channel, then commit all outputs on the same I2C STOP */
static mr_err_t pca9685_commit(drv_pwm_pca9685_priv_t *priv) {
    mr_uint8_t tx[1U + PCA9685_CHANNEL_COUNT * 4U], *out;
    mr_uint32_t channel, width, on, off;
    mr_i2c_msg_t msg;
    mr_ssize_t ret;

    /* Initialize every channel to full-off */
    tx[0] = PCA9685_REG_LED0_ON_L;
    for (channel = 0U; channel < PCA9685_CHANNEL_COUNT; channel++) {
        out = &tx[1U + channel * 4U];
        out[0] = 0U;
        out[1] = 0U;
        out[2] = 0U;
        out[3] = PCA9685_FULL_BIT;
        if (((priv->running_mask & (1U << channel)) == 0U)
            || (priv->states[channel] == MR_PWM_OUTPUT_INACTIVE)) {
            continue;
        }

        /* Handle constant output states */
        width = pca9685_time_to_ticks(priv->channels[channel].pulse,
                                      priv->config.period);
        if ((priv->states[channel] == MR_PWM_OUTPUT_ACTIVE)
            || (width == PCA9685_COUNTER_TICKS)) {
            out[1] = PCA9685_FULL_BIT;
            out[3] = 0U;
            continue;
        }
        if (width == 0U) {
            continue;
        }

        /* Encode phase and width */
        on = pca9685_time_to_ticks(priv->channels[channel].phase,
                                   priv->config.period);
        off = (on + width) & (PCA9685_COUNTER_TICKS - 1U);
        out[0] = (mr_uint8_t)on;
        out[1] = (mr_uint8_t)(on >> 8);
        out[2] = (mr_uint8_t)off;
        out[3] = (mr_uint8_t)(off >> 8);
    }

    /* Commit all channel registers in one transaction */
    msg.addr = (mr_uint16_t)priv->addr;
    msg.flags = MR_I2C_MSG_STOP;
    msg.buf = tx;
    msg.len = sizeof(tx);
    ret = mr_i2c_transfer(priv->i2c, &msg, 1U);
    if (ret < 0) {
        return (mr_err_t)ret;
    }
    if (ret != 1) {
        return -MR_EIO;
    }
    return 0;
}

static mr_err_t drv_configure(const mr_driver_t *drv,
                              const mr_pwm_config_t *config) {
    drv_pwm_pca9685_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint64_t divider, period;
    mr_uint32_t channel;
    mr_uint8_t reg;
    mr_err_t ret;

    /* Check I2C bus */
    if ((priv->i2c == MR_NULL) || !MR_DRIVER_IS_SETUP(priv->i2c)
        || MR_DRIVER_IS_DYING(priv->i2c)) {
        return -MR_ENODEV;
    }
    if (!MR_DRIVER_IS_TYPE(priv->i2c, MR_DRIVER_TYPE_I2C)) {
        return -MR_EINVAL;
    }

    /* Check controller configuration */
    if ((config->cycles != 0U) || (config->alignment != MR_PWM_ALIGN_EDGE_UP)
        || (config->update_mode != MR_PWM_UPDATE_IMMEDIATE)) {
        return -MR_ENOSYS;
    }

    /* Check chip configuration */
    if ((priv->addr < PCA9685_ADDR_MIN) || (priv->addr > PCA9685_ADDR_MAX)
        || (priv->addr == PCA9685_ADDR_ALL_CALL)
        || (priv->oscillator_hz < PCA9685_OSCILLATOR_MIN)
        || (priv->oscillator_hz > PCA9685_OSCILLATOR_MAX)) {
        return -MR_EINVAL;
    }

    /* Compute the nearest valid prescaler */
    if (config->period > 1048576000000000ULL / priv->oscillator_hz) {
        return -MR_ERANGE;
    }
    divider = (config->period * priv->oscillator_hz + 2048000000000ULL)
            / 4096000000000ULL;
    if ((divider < 4U) || (divider > 256U)) {
        return -MR_ERANGE;
    }
    period = (4096000000000ULL * divider + priv->oscillator_hz / 2U)
           / priv->oscillator_hz;

    /* Program sleep mode and prescaler */
    priv->ready = MR_FALSE;
    ret = pca9685_reg_write(priv, PCA9685_REG_MODE1, PCA9685_MODE1_SLEEP_AI);
    if (ret != 0) {
        return ret;
    }
    ret = pca9685_reg_write(priv, PCA9685_REG_MODE2, PCA9685_MODE2_OUTDRV);
    if (ret != 0) {
        return ret;
    }
    ret = pca9685_reg_write(priv, PCA9685_REG_PRESCALE,
                            (mr_uint8_t)(divider - 1U));
    if (ret != 0) {
        return ret;
    }

    /* Clear global output overrides */
    for (reg = PCA9685_REG_ALL_LED_ON_L; reg < PCA9685_REG_ALL_LED_ON_L + 4U;
         reg++) {
        ret = pca9685_reg_write(priv, reg, 0U);
        if (ret != 0) {
            return ret;
        }
    }

    /* Reset cached channels */
    priv->config = *config;
    priv->config.period = period;
    priv->configured_mask = 0U;
    priv->running_mask = 0U;
    for (channel = 0U; channel < PCA9685_CHANNEL_COUNT; channel++) {
        priv->channels[channel] = (mr_pwm_channel_config_t){0};
        priv->states[channel] = MR_PWM_OUTPUT_PWM;
    }
    ret = pca9685_commit(priv);
    if (ret != 0) {
        return ret;
    }

    /* Wake and restart oscillator */
    ret = pca9685_reg_write(priv, PCA9685_REG_MODE1, PCA9685_MODE1_AI);
    if (ret != 0) {
        return ret;
    }
    mr_clock_busy_wait(500U);
    ret = pca9685_reg_write(priv, PCA9685_REG_MODE1, PCA9685_MODE1_RESTART_AI);
    if (ret != 0) {
        return ret;
    }

    priv->ready = MR_TRUE;
    return 0;
}

static mr_err_t drv_configuration(const mr_driver_t *drv,
                                  mr_pwm_config_t *config) {
    drv_pwm_pca9685_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Read cached configuration */
    if (!priv->ready) {
        return -MR_ENODEV;
    }
    *config = priv->config;
    return 0;
}

static mr_err_t pca9685_channel_normalize(drv_pwm_pca9685_priv_t *priv,
                                          mr_pwm_channel_config_t *config) {
    mr_uint32_t phase, width;

    /* Check channel configuration */
    if ((config->pulse > priv->config.period)
        || (config->phase >= priv->config.period)) {
        return -MR_EINVAL;
    }
    if (config->comp || (config->dead_rise != 0U) || (config->dead_fall != 0U)
        || (config->polarity != MR_PWM_POLARITY_ACTIVE_HIGH)
        || (config->idle != MR_PWM_IDLE_INACTIVE)) {
        return -MR_ENOSYS;
    }

    /* Quantize timing */
    width = pca9685_time_to_ticks(config->pulse, priv->config.period);
    phase = pca9685_time_to_ticks(config->phase, priv->config.period);
    if (phase >= PCA9685_COUNTER_TICKS) {
        phase = PCA9685_COUNTER_TICKS - 1U;
    }
    config->pulse = pca9685_ticks_to_time(width, priv->config.period);
    config->phase = pca9685_ticks_to_time(phase, priv->config.period);
    config->comp_polarity = MR_PWM_POLARITY_ACTIVE_HIGH;
    config->comp_idle = MR_PWM_IDLE_INACTIVE;
    return 0;
}

static mr_err_t drv_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_pwm_channel_config_t *config) {
    drv_pwm_pca9685_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_pwm_channel_config_t result;
    mr_err_t ret;

    /* Require controller configuration */
    if (!priv->ready) {
        return -MR_ENODEV;
    }

    /* Check and quantize channel configuration */
    result = *config;
    ret = pca9685_channel_normalize(priv, &result);
    if (ret != 0) {
        return ret;
    }

    /* Cache channel configuration */
    priv->channels[channel] = result;
    priv->states[channel] = MR_PWM_OUTPUT_PWM;
    priv->configured_mask |= 1U << channel;
    return 0;
}

static mr_err_t drv_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_pwm_channel_config_t *config) {
    drv_pwm_pca9685_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Read cached channel configuration */
    if (!priv->ready) {
        return -MR_ENODEV;
    }
    if ((priv->configured_mask & (1U << channel)) == 0U) {
        return -MR_ENODEV;
    }

    *config = priv->channels[channel];
    return 0;
}

static mr_err_t drv_update(const mr_driver_t *drv,
                           const mr_pwm_update_t *updates, mr_size_t count) {
    drv_pwm_pca9685_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t channel, seen, bit;
    drv_pwm_pca9685_priv_t next;
    mr_size_t i;
    mr_err_t ret;

    /* Require controller configuration */
    if (!priv->ready) {
        return -MR_ENODEV;
    }
    next = *priv;
    seen = 0U;

    /* Validate and stage the entire transaction */
    for (i = 0U; i < count; i++) {
        channel = updates[i].channel;
        bit = 1U << channel;
        if ((seen & bit) != 0U) {
            return -MR_EINVAL;
        }
        if ((priv->configured_mask & bit) == 0U) {
            return -MR_ENODEV;
        }
        if (updates[i].state == MR_PWM_OUTPUT_HIGH_Z) {
            return -MR_ENOSYS;
        }

        next.channels[channel].pulse = updates[i].pulse;
        next.channels[channel].phase = updates[i].phase;
        next.states[channel] = updates[i].state;
        ret = pca9685_channel_normalize(&next, &next.channels[channel]);
        if (ret != 0) {
            return ret;
        }
        seen |= bit;
    }

    /* Commit staged outputs */
    ret = pca9685_commit(&next);
    if (ret != 0) {
        return ret;
    }

    /* Publish software state only after a successful commit */
    *priv = next;
    return 0;
}

static mr_err_t drv_start(const mr_driver_t *drv, mr_uint32_t channels) {
    drv_pwm_pca9685_priv_t *priv = MR_DRIVER_PRIV(drv);
    drv_pwm_pca9685_priv_t next;
    mr_err_t ret;

    /* Require configured channels */
    if (!priv->ready) {
        return -MR_ENODEV;
    }
    if ((channels & ~priv->configured_mask) != 0U) {
        return -MR_ENODEV;
    }

    /* Commit running channels */
    next = *priv;
    next.running_mask = priv->running_mask | channels;
    ret = pca9685_commit(&next);
    if (ret != 0) {
        return ret;
    }

    priv->running_mask = next.running_mask;
    return 0;
}

static mr_err_t drv_stop(const mr_driver_t *drv, mr_uint32_t channels) {
    drv_pwm_pca9685_priv_t *priv = MR_DRIVER_PRIV(drv);
    drv_pwm_pca9685_priv_t next;
    mr_err_t ret;

    /* Skip inactive controller */
    if (!priv->ready) {
        return 0;
    }

    /* Commit stopped channels */
    next = *priv;
    next.running_mask = priv->running_mask & ~channels;
    ret = pca9685_commit(&next);
    if (ret != 0) {
        return ret;
    }

    priv->running_mask = next.running_mask;
    return 0;
}

static mr_err_t drv_state(const mr_driver_t *drv, mr_pwm_state_t *state) {
    drv_pwm_pca9685_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Read cached state */
    state->running_mask = priv->running_mask;
    state->fault_mask = 0U;
    return 0;
}

/* Instance install */
#define PWM_PCA9685_HT_PWM_INSTALL(_id, _ops)                                  \
    static drv_pwm_pca9685_priv_t MR_CAT(__drv_pwm_pca9685_priv_, _id) = {     \
        .i2c = MR_HT_DRIVER_FIND(MR_HT_DRIVER_PROP(_id, i2c)),                 \
        .addr = MR_HT_DRIVER_PROP(_id, addr),                                  \
        .oscillator_hz = MR_HT_DRIVER_PROP_OR(_id, oscillator_hz, 25000000U),  \
    };                                                                         \
    static mr_pwm_data_t MR_CAT(__drv_pwm_pca9685_data_, _id)                  \
        = MR_PWM_DATA_INIT(MR_HT_DRIVER_PROP_OR(_id, channel_mask, 0xffffU)    \
                               & 0xffffU,                                      \
                           0U, 0U);                                            \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(pwm),                   \
                         MR_DRIVER_TYPE_PWM,                                   \
                         &MR_CAT(__drv_pwm_pca9685_data_, _id),                \
                         &MR_CAT(__drv_pwm_pca9685_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(PWM_PCA9685_HT_PWM_INSTALL, &__drv_pwm_pca9685_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_PWM) && defined(MR_USE_DRIVER_I2C) */

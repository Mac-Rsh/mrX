/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_PWM) && defined(MR_USE_DRIVER_GPIO)
#include <mrX/drivers/pwm/mr_pwm.h>
#include <mrX/port/mr_port_irq.h>
#include "stm32h5xx_hal_tim.h"

/* HardTree compatible */
#define MR_HT_COMPAT                    st_stm32h5_pwm
#if MR_HT_HAS_DRIVER()

/* Driver PWM operations */
static mr_err_t pwm_configure(const mr_driver_t *drv,
                              const mr_pwm_config_t *config);
static mr_err_t pwm_configuration(const mr_driver_t *drv,
                                  mr_pwm_config_t *config);
static mr_err_t pwm_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_pwm_channel_config_t *config);
static mr_err_t pwm_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_pwm_channel_config_t *config);
static mr_err_t pwm_update(const mr_driver_t *drv,
                           const mr_pwm_update_t *updates, mr_size_t count);
static mr_err_t pwm_start(const mr_driver_t *drv, mr_uint32_t channels);
static mr_err_t pwm_stop(const mr_driver_t *drv, mr_uint32_t channels);
static mr_err_t pwm_state(const mr_driver_t *drv, mr_pwm_state_t *state);
static mr_err_t pwm_signal_configure(const mr_driver_t *drv,
                                     mr_pwm_signal_t signal,
                                     const mr_pwm_signal_config_t *config);
static mr_err_t pwm_signal_configuration(const mr_driver_t *drv,
                                         mr_pwm_signal_t signal,
                                         mr_pwm_signal_config_t *config);
static mr_err_t pwm_signal_control(const mr_driver_t *drv,
                                   mr_pwm_signal_t signal, mr_bool_t enable);
MR_UNUSED static const mr_pwm_ops_t __pwm_ops = {
    .configure = pwm_configure,
    .configuration = pwm_configuration,
    .channel_configure = pwm_channel_configure,
    .channel_configuration = pwm_channel_configuration,
    .update = pwm_update,
    .start = pwm_start,
    .stop = pwm_stop,
    .state = pwm_state,
    .fault_configure = MR_NULL,
    .fault_configuration = MR_NULL,
    .fault_clear = MR_NULL,
    .signal_configure = pwm_signal_configure,
    .signal_configuration = pwm_signal_configuration,
    .signal_control = pwm_signal_control,
    .event_control = MR_NULL,
};

/* PWM channel type */
typedef struct pwm_channel {
    mr_ht_gpio_spec_t gpio;
    uint32_t hal_channel;
    uint32_t alternate;
    uint32_t compare;
    mr_uint32_t index;
    mr_pwm_channel_config_t config;
    mr_bool_t configured;
} pwm_channel_t;

/* PWM private data type */
typedef struct pwm_priv {
    TIM_HandleTypeDef handle;
    void (*clk_enable)(void);
    pwm_channel_t *channels;
    mr_size_t channels_num;
    mr_pwm_config_t config;
    mr_uint32_t running_mask;
    mr_bool_t configured;
    mr_bool_t signal_enabled;
    mr_uint8_t signal;
} pwm_priv_t;

/* PWM time conversion definition */
#define PWM_NANOSECONDS                 (1000000000ULL)

/* PWM prescaler limit definition */
#define PWM_PRESCALER_MAX               (65536ULL)
#define PWM_PSC_SEARCH_LIMIT            (64ULL)

static mr_bool_t pwm_is_apb2(TIM_TypeDef *instance) {
#if defined(TIM1)
    if (instance == TIM1) {
        return MR_TRUE;
    }
#endif /* defined(TIM1) */
#if defined(TIM8)
    if (instance == TIM8) {
        return MR_TRUE;
    }
#endif /* defined(TIM8) */
#if defined(TIM15)
    if (instance == TIM15) {
        return MR_TRUE;
    }
#endif /* defined(TIM15) */
#if defined(TIM16)
    if (instance == TIM16) {
        return MR_TRUE;
    }
#endif /* defined(TIM16) */
#if defined(TIM17)
    if (instance == TIM17) {
        return MR_TRUE;
    }
#endif /* defined(TIM17) */
    return MR_FALSE;
}

static mr_uint64_t pwm_clock(TIM_TypeDef *instance) {
    mr_uint32_t ppre, apb_div;
    mr_uint64_t pclk;

    /* Get the timer's APB clock and prescaler */
    if (pwm_is_apb2(instance)) {
        pclk = HAL_RCC_GetPCLK2Freq();
        ppre = (RCC->CFGR2 & RCC_CFGR2_PPRE2) >> RCC_CFGR2_PPRE2_Pos;
    } else {
        pclk = HAL_RCC_GetPCLK1Freq();
        ppre = (RCC->CFGR2 & RCC_CFGR2_PPRE1) >> RCC_CFGR2_PPRE1_Pos;
    }

    /* Apply the timer clock multiplier selected by TIMPRE */
    if (ppre < 4U) {
        return pclk;
    }
    apb_div = 1U << (ppre - 3U);
    if ((RCC->CFGR1 & RCC_CFGR1_TIMPRE) == 0U) {
        return pclk * 2U;
    }
    if (apb_div <= 4U) {
        return (mr_uint64_t)HAL_RCC_GetHCLKFreq();
    }
    return pclk * 4U;
}

static mr_err_t pwm_nanoseconds_to_ticks(mr_uint64_t clock,
                                         mr_uint64_t nanoseconds,
                                         mr_uint64_t *ticks) {
    mr_uint64_t seconds, remainder;

    /* Split whole seconds to avoid overflowing the multiplication */
    seconds = nanoseconds / PWM_NANOSECONDS;
    remainder = nanoseconds % PWM_NANOSECONDS;
    if (seconds > (MR_UINT64_MAX / clock)) {
        return -MR_ERANGE;
    }
    *ticks = seconds * clock;
    remainder
        = ((remainder * clock) + (PWM_NANOSECONDS / 2U)) / PWM_NANOSECONDS;
    if (*ticks > (MR_UINT64_MAX - remainder)) {
        return -MR_ERANGE;
    }
    *ticks += remainder;
    return 0;
}

static mr_uint64_t pwm_ticks_to_nanoseconds(mr_uint64_t clock,
                                            mr_uint64_t ticks) {
    mr_uint64_t seconds, remainder;

    /* Convert whole seconds and remaining ticks separately */
    seconds = ticks / clock;
    remainder = ticks % clock;

    return (seconds * PWM_NANOSECONDS)
         + (((remainder * PWM_NANOSECONDS) + (clock / 2U)) / clock);
}

static mr_err_t pwm_make_period(TIM_TypeDef *instance, mr_uint64_t period,
                                mr_pwm_alignment_t alignment,
                                uint32_t *prescaler, uint32_t *arr,
                                mr_uint64_t *actual) {
    mr_uint64_t actual_ticks, error, best_psc, best_counts, best_error;
    mr_uint64_t clock, counter_max, factor, ticks;
    mr_uint64_t psc_min, search_end, psc, counts;
    mr_err_t ret;

    /* Convert the requested period to timer clock ticks */
    clock = pwm_clock(instance);
    best_psc = 0U;
    best_counts = 0U;
    best_error = MR_UINT64_MAX;
    if (clock == 0U) {
        return -MR_ERANGE;
    }
    ret = pwm_nanoseconds_to_ticks(clock, period, &ticks);
    if ((ret != 0) || (ticks == 0U)) {
        return (ret != 0) ? ret : -MR_ERANGE;
    }

    factor = (alignment == MR_PWM_ALIGN_CENTER) ? 2U : 1U;
    /* Reserve ARR + 1 in CCR so 100% duty cannot wrap to zero */
    counter_max = IS_TIM_32B_COUNTER_INSTANCE(instance) ? ((1ULL << 32) - 1U)
                                                        : ((1ULL << 16) - 1U);
    if (ticks > (counter_max * PWM_PRESCALER_MAX * factor)) {
        return -MR_ERANGE;
    }

    /* Search supported prescalers for the closest representable period */
    psc_min = (ticks + (counter_max * factor) - 1U) / (counter_max * factor);
    if (psc_min == 0U) {
        psc_min = 1U;
    }
    search_end = psc_min + PWM_PSC_SEARCH_LIMIT - 1U;
    if (search_end > PWM_PRESCALER_MAX) {
        search_end = PWM_PRESCALER_MAX;
    }
    for (psc = psc_min; psc <= search_end; psc++) {
        counts = (ticks + ((psc * factor) / 2U)) / (psc * factor);
        if ((counts == 0U) || (counts > counter_max)) {
            continue;
        }
        actual_ticks = counts * psc * factor;
        error = (actual_ticks >= ticks) ? (actual_ticks - ticks)
                                        : (ticks - actual_ticks);
        if (error < best_error) {
            best_error = error;
            best_psc = psc;
            best_counts = counts;
            if (error == 0U) {
                break;
            }
        }
    }
    if (best_counts == 0U) {
        return -MR_ERANGE;
    }

    /* Return timer settings and the resulting period */
    *prescaler = (uint32_t)(best_psc - 1U);
    *arr = (uint32_t)(best_counts - 1U);
    *actual = pwm_ticks_to_nanoseconds(clock, best_counts * best_psc * factor);
    return 0;
}

static mr_err_t pwm_make_pulse(const pwm_priv_t *priv, mr_uint64_t pulse,
                               uint32_t *compare, mr_uint64_t *actual) {
    mr_uint64_t clock, divider, factor, ticks, counts;
    mr_err_t ret;

    /* Convert the pulse width using the configured timer period */
    clock = pwm_clock(priv->handle.Instance);
    divider = (mr_uint64_t)priv->handle.Init.Prescaler + 1U;
    factor = (priv->config.alignment == MR_PWM_ALIGN_CENTER) ? 2U : 1U;
    if (clock == 0U) {
        return -MR_ERANGE;
    }
    if (pulse > priv->config.period) {
        return -MR_EINVAL;
    }
    ret = pwm_nanoseconds_to_ticks(clock, pulse, &ticks);
    if (ret != 0) {
        return ret;
    }
    counts = (ticks + ((divider * factor) / 2U)) / (divider * factor);
    if (counts > ((mr_uint64_t)priv->handle.Init.Period + 1U)) {
        return -MR_ERANGE;
    }

    /* Return the compare value and resulting pulse width */
    *compare = (uint32_t)counts;
    *actual = pwm_ticks_to_nanoseconds(clock, counts * divider * factor);
    return 0;
}

static pwm_channel_t *pwm_channel_find(pwm_priv_t *priv, mr_uint32_t index) {
    mr_size_t i;

    /* Find the logical channel in the board mapping */
    for (i = 0U; i < priv->channels_num; i++) {
        if (priv->channels[i].index == index) {
            return &priv->channels[i];
        }
    }
    return MR_NULL;
}

static mr_bool_t pwm_hal_channel_valid(uint32_t channel) {
    return (channel == TIM_CHANNEL_1) || (channel == TIM_CHANNEL_2)
        || (channel == TIM_CHANNEL_3) || (channel == TIM_CHANNEL_4);
}

static void pwm_compare_write_immediate(pwm_priv_t *priv,
                                        const pwm_channel_t *output,
                                        uint32_t compare) {
    /* Bypass preload when the output must change immediately */
    __HAL_TIM_DISABLE_OCxPRELOAD(&priv->handle, output->hal_channel);
    __HAL_TIM_SET_COMPARE(&priv->handle, output->hal_channel, compare);
    __HAL_TIM_ENABLE_OCxPRELOAD(&priv->handle, output->hal_channel);
}

static mr_err_t pwm_configure(const mr_driver_t *drv,
                              const mr_pwm_config_t *config) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    uint32_t prescaler, period;
    mr_pwm_config_t actual;
    mr_err_t ret;
    mr_size_t i;

    /* Check timer and configuration support */
    if (!IS_TIM_INSTANCE(priv->handle.Instance)) {
        return -MR_EINVAL;
    }
    if ((config->cycles != 0U)
        || (config->update_mode != MR_PWM_UPDATE_PERIOD)) {
        return -MR_ENOSYS;
    }
    if (priv->clk_enable == MR_NULL) {
        return -MR_ENODEV;
    }
    if (priv->signal_enabled) {
        return -MR_EBUSY;
    }

    /* Calculate timer settings from the requested period */
    actual = *config;
    priv->clk_enable();
    ret = pwm_make_period(priv->handle.Instance, config->period,
                          (mr_pwm_alignment_t)config->alignment, &prescaler,
                          &period, &actual.period);
    if (ret != 0) {
        return ret;
    }

    /* Configure the timer counter and requested alignment */
    priv->handle.Init.Prescaler = prescaler;
    priv->handle.Init.Period = period;
    priv->handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    /* Repetition 1 makes center-aligned updates occur once per period */
    priv->handle.Init.RepetitionCounter
        = (config->alignment == MR_PWM_ALIGN_CENTER) ? 1U : 0U;
    priv->handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    switch (config->alignment) {
        case MR_PWM_ALIGN_EDGE_UP: {
            priv->handle.Init.CounterMode = TIM_COUNTERMODE_UP;
            break;
        }
        case MR_PWM_ALIGN_EDGE_DOWN: {
            priv->handle.Init.CounterMode = TIM_COUNTERMODE_DOWN;
            break;
        }
        case MR_PWM_ALIGN_CENTER: {
            priv->handle.Init.CounterMode = TIM_COUNTERMODE_CENTERALIGNED1;
            break;
        }
        default: {
            return -MR_EINVAL;
        }
    }

    /* Initialize the timer before publishing the new configuration */
    ret = mr_stm32h5_hal_status(HAL_TIM_PWM_Init(&priv->handle));
    if (ret != 0) {
        return ret;
    }
    /* Invalidate channel settings derived from the previous time base */
    priv->config = actual;
    priv->configured = MR_TRUE;
    for (i = 0U; i < priv->channels_num; i++) {
        priv->channels[i].configured = MR_FALSE;
    }
    return 0;
}

static mr_err_t pwm_configuration(const mr_driver_t *drv,
                                  mr_pwm_config_t *config) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Return only a configuration that has been applied to hardware */
    if (!priv->configured) {
        return -MR_ENODEV;
    }
    *config = priv->config;
    return 0;
}

static mr_err_t pwm_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_pwm_channel_config_t *config) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_pwm_channel_config_t actual;
    pwm_channel_t *output;
    TIM_OC_InitTypeDef oc;
    uint32_t compare;
    mr_err_t ret;

    /* Check timer and channel support */
    if (!priv->configured) {
        return -MR_ENODEV;
    }
    if (priv->running_mask != 0U) {
        return -MR_EBUSY;
    }
    output = pwm_channel_find(priv, channel);
    if (output == MR_NULL) {
        return -MR_EINVAL;
    }
    if (!pwm_hal_channel_valid(output->hal_channel)
        || !IS_TIM_CCX_CHANNEL(priv->handle.Instance, output->hal_channel)) {
        return -MR_ENOSYS;
    }
    if (config->comp || (config->dead_rise != 0U) || (config->dead_fall != 0U)
        || (config->phase != 0U)) {
        return -MR_ENOSYS;
    }

    /* Convert the requested pulse width to a compare value */
    actual = *config;
    ret = pwm_make_pulse(priv, config->pulse, &compare, &actual.pulse);
    if (ret != 0) {
        return ret;
    }

    /* Route the timer channel through the board-selected alternate function */
    ret = mr_stm32h5_gpio_pin_alternate(&output->gpio, output->alternate,
                                        MR_GPIO_PULL_NONE, MR_GPIO_OTYPE_PP);
    if (ret != 0) {
        return ret;
    }

    /* Configure the channel output mode and polarity */
    oc = (TIM_OC_InitTypeDef){0};
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = compare;
    oc.OCPolarity = (config->polarity == MR_PWM_POLARITY_ACTIVE_HIGH)
                      ? TIM_OCPOLARITY_HIGH
                      : TIM_OCPOLARITY_LOW;
    oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    oc.OCIdleState = (config->idle == MR_PWM_IDLE_ACTIVE)
                       ? TIM_OCIDLESTATE_SET
                       : TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;

    /* Apply the channel configuration */
    ret = mr_stm32h5_hal_status(
        HAL_TIM_PWM_ConfigChannel(&priv->handle, &oc, output->hal_channel));
    if (ret != 0) {
        return ret;
    }

    /* Save the normalized configuration */
    output->config = actual;
    output->compare = compare;
    output->configured = MR_TRUE;
    return 0;
}

static mr_err_t pwm_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_pwm_channel_config_t *config) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    pwm_channel_t *output;

    /* Find the channel and return its saved configuration */
    output = pwm_channel_find(priv, channel);
    if ((output == MR_NULL) || !output->configured) {
        return -MR_ENODEV;
    }
    *config = output->config;
    return 0;
}

static mr_err_t pwm_update(const mr_driver_t *drv,
                           const mr_pwm_update_t *updates, mr_size_t count) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    pwm_channel_t *output;
    mr_uint64_t actual;
    uint32_t compare;
    int irq_state;
    mr_err_t ret;

    /* Check that the timer can accept this update */
    if (!priv->configured) {
        return -MR_ENODEV;
    }
    if (count == 0U) {
        return -MR_EINVAL;
    }
    if (count > 1U) {
        return -MR_ENOSYS;
    }

    /* Validate the complete transaction before changing any register */
    output = pwm_channel_find(priv, updates[0].channel);
    if ((output == MR_NULL) || !output->configured) {
        return -MR_ENODEV;
    }
    if ((updates[0].phase != 0U) || (updates[0].state != MR_PWM_OUTPUT_PWM)) {
        return -MR_ENOSYS;
    }
    ret = pwm_make_pulse(priv, updates[0].pulse, &compare, &actual);
    if (ret != 0) {
        return ret;
    }

    /* Apply the compare update atomically with respect to timer interrupts */
    irq_state = mr_port_irq_disable();
    if ((priv->running_mask & (1UL << updates[0].channel)) != 0U) {
        __HAL_TIM_SET_COMPARE(&priv->handle, output->hal_channel, compare);
    }
    output->compare = compare;
    output->config.pulse = actual;
    mr_port_irq_enable(irq_state);
    return 0;
}

static mr_err_t pwm_start(const mr_driver_t *drv, mr_uint32_t channels) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    pwm_channel_t *output;
    uint32_t enable_mask;
    mr_uint32_t index;
    int irq_state;

    /* Validate all requested outputs and prepare their enable mask */
    enable_mask = 0U;
    if (!priv->configured) {
        return -MR_ENODEV;
    }
    if (priv->running_mask != 0U) {
        return -MR_ENOSYS;
    }
    for (index = 0U; index < 32U; index++) {
        if ((channels & (1UL << index)) == 0U) {
            continue;
        }
        output = pwm_channel_find(priv, index);
        if ((output == MR_NULL) || !output->configured
            || !pwm_hal_channel_valid(output->hal_channel)) {
            return -MR_ENODEV;
        }
        enable_mask |= TIM_CCER_CC1E << output->hal_channel;
    }

    /* Load compare values before enabling outputs to avoid a stale pulse */
    irq_state = mr_port_irq_disable();
    for (index = 0U; index < 32U; index++) {
        if ((channels & (1UL << index)) != 0U) {
            output = pwm_channel_find(priv, index);
            pwm_compare_write_immediate(priv, output, output->compare);
        }
    }
    if (priv->running_mask == 0U) {
        __HAL_TIM_SET_COUNTER(&priv->handle,
                              (priv->config.alignment == MR_PWM_ALIGN_EDGE_DOWN)
                                  ? priv->handle.Init.Period
                                  : 0U);
        __HAL_TIM_CLEAR_FLAG(&priv->handle, TIM_FLAG_UPDATE);
    }
    /* Enable the selected outputs before starting the shared counter */
    SET_BIT(priv->handle.Instance->CCER, enable_mask);
    if (IS_TIM_BREAK_INSTANCE(priv->handle.Instance)) {
        __HAL_TIM_MOE_ENABLE(&priv->handle);
    }
    __HAL_TIM_ENABLE(&priv->handle);
    priv->running_mask |= channels;
    mr_port_irq_enable(irq_state);
    return 0;
}

static mr_err_t pwm_stop(const mr_driver_t *drv, mr_uint32_t channels) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    uint32_t enable_mask, disable_mask;
    pwm_channel_t *output;
    mr_uint32_t index;
    int irq_state;

    /* Validate every requested channel before applying any idle state */
    enable_mask = 0U;
    disable_mask = 0U;
    for (index = 0U; index < 32U; index++) {
        if ((channels & (1UL << index)) == 0U) {
            continue;
        }
        output = pwm_channel_find(priv, index);
        if (output == MR_NULL) {
            return -MR_EINVAL;
        }
    }

    /* Apply idle values and disable channels that need no driven idle level */
    irq_state = mr_port_irq_disable();
    for (index = 0U; index < 32U; index++) {
        if ((channels & (1UL << index)) == 0U) {
            continue;
        }
        output = pwm_channel_find(priv, index);
        switch (output->config.idle) {
            case MR_PWM_IDLE_INACTIVE: {
                pwm_compare_write_immediate(priv, output, 0U);
                enable_mask |= TIM_CCER_CC1E << output->hal_channel;
                break;
            }
            case MR_PWM_IDLE_ACTIVE: {
                pwm_compare_write_immediate(priv, output,
                                            priv->handle.Init.Period + 1U);
                enable_mask |= TIM_CCER_CC1E << output->hal_channel;
                break;
            }
            default: {
                disable_mask |= TIM_CCER_CC1E << output->hal_channel;
                break;
            }
        }
    }

    /* Preserve driven idle levels while disabling unneeded outputs */
    CLEAR_BIT(priv->handle.Instance->CCER, disable_mask);
    SET_BIT(priv->handle.Instance->CCER, enable_mask);
    priv->running_mask &= ~channels;
    if (priv->running_mask == 0U) {
        __HAL_TIM_DISABLE(&priv->handle);
    }
    mr_port_irq_enable(irq_state);
    return 0;
}

static mr_err_t pwm_state(const mr_driver_t *drv, mr_pwm_state_t *state) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Report active channels; this backend has no fault input */
    state->running_mask = priv->running_mask;
    state->fault_mask = 0U;
    return 0;
}

static mr_err_t pwm_signal_configure(const mr_driver_t *drv,
                                     mr_pwm_signal_t signal,
                                     const mr_pwm_signal_config_t *config) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Validate the timer state and supported signal type */
    if (!priv->configured) {
        return -MR_ENODEV;
    }
    if (signal == MR_PWM_SIGNAL_PERIOD) {
        return (config->offset == 0U) ? 0 : -MR_EINVAL;
    }

    /* COMPARE needs a reserved channel and direction-specific TRGO/TRGO2 */
    return (signal == MR_PWM_SIGNAL_COMPARE) ? -MR_ENOSYS : -MR_EINVAL;
}

static mr_err_t pwm_signal_configuration(const mr_driver_t *drv,
                                         mr_pwm_signal_t signal,
                                         mr_pwm_signal_config_t *config) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Return the fixed period-signal offset supported by this backend */
    if (!priv->configured) {
        return -MR_ENODEV;
    }
    if (signal == MR_PWM_SIGNAL_PERIOD) {
        config->offset = 0U;
        return 0;
    }
    return (signal == MR_PWM_SIGNAL_COMPARE) ? -MR_ENOSYS : -MR_EINVAL;
}

static mr_err_t pwm_signal_control(const mr_driver_t *drv,
                                   mr_pwm_signal_t signal, mr_bool_t enable) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    TIM_MasterConfigTypeDef master;
    mr_err_t ret;

    /* Check timer and signal support */
    if (!priv->configured) {
        return -MR_ENODEV;
    }
    if (signal != MR_PWM_SIGNAL_PERIOD) {
        return (signal == MR_PWM_SIGNAL_COMPARE) ? -MR_ENOSYS : -MR_EINVAL;
    }
    if (enable && priv->signal_enabled && (priv->signal != signal)) {
        return -MR_EBUSY;
    }
    if (!enable && (!priv->signal_enabled || (priv->signal != signal))) {
        return 0;
    }
    if (!IS_TIM_MASTER_INSTANCE(priv->handle.Instance)) {
        return -MR_ENOSYS;
    }

    /* Enable or reset the timer update trigger */
    master = (TIM_MasterConfigTypeDef){0};
    master.MasterOutputTrigger = enable ? TIM_TRGO_UPDATE : TIM_TRGO_RESET;
    master.MasterOutputTrigger2 = TIM_TRGO2_RESET;
    master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    ret = mr_stm32h5_hal_status(
        HAL_TIMEx_MasterConfigSynchronization(&priv->handle, &master));
    if (ret == 0) {
        priv->signal_enabled = enable;
        priv->signal = (mr_uint8_t)signal;
    }
    return ret;
}

/* Instance install with a board channel map and timer state */
#define ST_HT_PWM_INSTALL(_id, _ops)                                           \
    static void MR_CAT(__pwm_clk_enable_, _id)(void) {                         \
        MR_HT_DRIVER_PROP(_id, st_clken)();                                    \
    }                                                                          \
    static pwm_channel_t MR_CAT(__pwm_channel_, _id)[] = {                     \
        MR_HT_DRIVER_PROP_ELEM_FOREACH(_id, st_channels,                       \
                                       __ST_HT_PWM_CHANNEL_IMPL)               \
    };                                                                         \
    static pwm_priv_t MR_CAT(__pwm_priv_, _id) = {                             \
        .handle = {                                                            \
            .Instance = MR_HT_DRIVER_PROP(_id, st_timx),                       \
        },                                                                     \
        .clk_enable = MR_CAT(__pwm_clk_enable_, _id),                          \
        .channels = MR_CAT(__pwm_channel_, _id),                               \
        .channels_num = MR_ARRAY_SIZE(MR_CAT(__pwm_channel_, _id)),            \
    };                                                                         \
    MR_HT_PWM_INSTALL(_id, &MR_CAT(__pwm_priv_, _id), _ops)

/* HardTree channel tuple mapping */
#define __ST_HT_PWM_CHANNEL_IMPL(_elem, _node, _prop)                          \
    {                                                                          \
        .gpio = {                                                              \
            .drv =                                                             \
                MR_HT_DRIVER_FIND(                                             \
                    MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 2)),             \
            .pin =                                                             \
                (mr_uint8_t)MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 3),      \
        },                                                                     \
        .hal_channel = MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 1),           \
        .alternate = MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 4),             \
        .index = MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 0),                 \
    }
MR_HT_DRIVER_FOREACH(ST_HT_PWM_INSTALL, &__pwm_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_PWM) && defined(MR_USE_DRIVER_GPIO) */

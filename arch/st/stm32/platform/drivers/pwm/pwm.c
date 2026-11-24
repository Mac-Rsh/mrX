/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_PWM)
#include <mrX/drivers/gpio/mr_gpio.h>
#include <mrX/drivers/pwm/mr_pwm.h>
#include <mrX/port/mr_port_irq.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    st_stm32_pwm
#if MR_HT_HAS_DRIVER()
#if !defined(MR_USE_DRIVER_GPIO)
#error "STM32 PWM requires the GPIO driver"
#endif /* !defined(MR_USE_DRIVER_GPIO) */

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

/* PWM capability definition */
#if defined(IS_TIM_MASTER_INSTANCE) && defined(TIM_TRGO_UPDATE)                \
    && defined(TIM_TRGO_RESET) && defined(TIM_MASTERSLAVEMODE_DISABLE)
#define STM32_PWM_HAS_MASTER
#endif /* PWM master capability */

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
} pwm_priv_t;

/* PWM channel definition */
#define PWM_CHANNEL_BITS                (32U)

/* PWM time conversion definition */
#define PWM_NANOSECONDS                 (1000000000ULL)

/* PWM prescaler definition */
#define PWM_PRESCALER_MAX               (65536ULL)
#define PWM_PSC_SEARCH_LIMIT            (64ULL)

#if defined(RCC_CLOCKTYPE_PCLK2)
static mr_bool_t pwm_is_apb2(const TIM_TypeDef *instance) {
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
#if defined(TIM9)
    if (instance == TIM9) {
        return MR_TRUE;
    }
#endif /* defined(TIM9) */
#if defined(TIM10)
    if (instance == TIM10) {
        return MR_TRUE;
    }
#endif /* defined(TIM10) */
#if defined(TIM11)
    if (instance == TIM11) {
        return MR_TRUE;
    }
#endif /* defined(TIM11) */
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
#if defined(TIM19)
    if (instance == TIM19) {
        return MR_TRUE;
    }
#endif /* defined(TIM19) */
#if defined(TIM20)
    if (instance == TIM20) {
        return MR_TRUE;
    }
#endif /* defined(TIM20) */
#if defined(TIM21)
    if (instance == TIM21) {
        return MR_TRUE;
    }
#endif /* defined(TIM21) */
#if defined(TIM22)
    if (instance == TIM22) {
        return MR_TRUE;
    }
#endif /* defined(TIM22) */
    return MR_FALSE;
}
#endif /* defined(RCC_CLOCKTYPE_PCLK2) */

static mr_uint32_t pwm_apb_divider(mr_uint32_t divider) {
#if defined(RCC_HCLK_DIV1)
    if (divider == RCC_HCLK_DIV1) {
        return 1U;
    }
#endif /* defined(RCC_HCLK_DIV1) */
#if defined(RCC_HCLK_DIV2)
    if (divider == RCC_HCLK_DIV2) {
        return 2U;
    }
#endif /* defined(RCC_HCLK_DIV2) */
#if defined(RCC_HCLK_DIV4)
    if (divider == RCC_HCLK_DIV4) {
        return 4U;
    }
#endif /* defined(RCC_HCLK_DIV4) */
#if defined(RCC_HCLK_DIV8)
    if (divider == RCC_HCLK_DIV8) {
        return 8U;
    }
#endif /* defined(RCC_HCLK_DIV8) */
#if defined(RCC_HCLK_DIV16)
    if (divider == RCC_HCLK_DIV16) {
        return 16U;
    }
#endif /* defined(RCC_HCLK_DIV16) */
    return 0U;
}

static mr_bool_t pwm_timpre_enabled(void) {
#if defined(RCC_CFGR1_TIMPRE)
    return ((RCC->CFGR1 & RCC_CFGR1_TIMPRE) != 0U);
#elif defined(RCC_DCKCFGR_TIMPRE)
    return ((RCC->DCKCFGR & RCC_DCKCFGR_TIMPRE) != 0U);
#elif defined(RCC_DCKCFGR1_TIMPRE)
    return ((RCC->DCKCFGR1 & RCC_DCKCFGR1_TIMPRE) != 0U);
#elif defined(RCC_CFGR_TIMPRE)
    return ((RCC->CFGR & RCC_CFGR_TIMPRE) != 0U);
#else
    return MR_FALSE;
#endif
}

static mr_uint64_t pwm_apb_clock(const TIM_TypeDef *instance,
                                 mr_uint32_t *divider) {
    RCC_ClkInitTypeDef clock;
    uint32_t latency;

    /* Get clock configuration */
    HAL_RCC_GetClockConfig(&clock, &latency);

#if defined(RCC_CLOCKTYPE_PCLK2)
    /* Get APB2 clock */
    if (pwm_is_apb2(instance)) {
        *divider = pwm_apb_divider(clock.APB2CLKDivider);
        return HAL_RCC_GetPCLK2Freq();
    }
#endif /* defined(RCC_CLOCKTYPE_PCLK2) */

    /* Get APB1 clock */
    *divider = pwm_apb_divider(clock.APB1CLKDivider);
    return HAL_RCC_GetPCLK1Freq();
}

static mr_uint64_t pwm_clock(const TIM_TypeDef *instance) {
    mr_uint64_t pclk;
    mr_uint32_t divider;

    /* Get APB clock */
    pclk = pwm_apb_clock(instance, &divider);
    if ((pclk == 0ULL) || (divider == 0U)) {
        return 0ULL;
    }

    /* Check APB divider */
    if (divider == 1U) {
        return pclk;
    }

    /* Check TIMPRE */
    if (!pwm_timpre_enabled()) {
        return pclk * 2ULL;
    }

    /* Calculate PWM clock */
    if (divider <= 4U) {
        return HAL_RCC_GetHCLKFreq();
    }
    return pclk * 4ULL;
}

static mr_uint64_t pwm_counter_max(const TIM_TypeDef *instance) {
    /* Reserve ARR + 1 for 100% duty */
#if defined(IS_TIM_32B_COUNTER_INSTANCE)
    if (IS_TIM_32B_COUNTER_INSTANCE(instance)) {
        return (1ULL << 32) - 1ULL;
    }
#else
    MR_IGNORE(instance);
#endif /* defined(IS_TIM_32B_COUNTER_INSTANCE) */
    return (1ULL << 16) - 1ULL;
}

static mr_err_t pwm_period_ticks(mr_uint64_t clock, mr_uint64_t period,
                                 mr_uint64_t *ticks) {
    mr_uint64_t seconds, remainder, value;

    /* Split period */
    remainder = period % PWM_NANOSECONDS;
    seconds = period / PWM_NANOSECONDS;
    if (seconds > (MR_UINT64_MAX / clock)) {
        return -MR_ERANGE;
    }

    /* Calculate whole-second ticks */
    value = seconds * clock;

    /* Calculate sub-second ticks */
    remainder *= clock;
    remainder += PWM_NANOSECONDS / 2ULL;
    remainder /= PWM_NANOSECONDS;
    if (value > (MR_UINT64_MAX - remainder)) {
        return -MR_ERANGE;
    }

    /* Calculate target ticks */
    *ticks = value + remainder;
    if (*ticks == 0ULL) {
        return -MR_ERANGE;
    }
    return 0;
}

static mr_uint64_t pwm_ticks_period(mr_uint64_t clock, mr_uint64_t ticks) {
    mr_uint64_t seconds, remainder, period;

    /* Split ticks */
    seconds = ticks / clock;
    remainder = ticks % clock;

    /* Calculate whole-second period */
    period = seconds * PWM_NANOSECONDS;

    /* Calculate sub-second period */
    remainder *= PWM_NANOSECONDS;
    remainder += clock / 2ULL;
    remainder /= clock;

    /* Calculate period */
    return period + remainder;
}

static mr_uint64_t pwm_tick_error(mr_uint64_t target, mr_uint64_t actual) {
    /* Calculate tick error */
    if (actual >= target) {
        return actual - target;
    }
    return target - actual;
}

static mr_err_t pwm_find_period(mr_uint64_t ticks, mr_uint64_t counter_max,
                                mr_uint64_t factor, mr_uint64_t *prescaler,
                                mr_uint64_t *counts) {
    mr_uint64_t best_error, search_end, psc_min;
    mr_uint64_t actual_ticks, divider, error;
    mr_uint64_t psc, value;

    /* Calculate prescaler range */
    divider = counter_max * factor;
    psc_min = (ticks + divider - 1ULL) / divider;
    if (psc_min == 0ULL) {
        psc_min = 1ULL;
    }
    search_end = psc_min + PWM_PSC_SEARCH_LIMIT - 1ULL;
    if (search_end > PWM_PRESCALER_MAX) {
        search_end = PWM_PRESCALER_MAX;
    }

    /* Find best period */
    *prescaler = 0ULL;
    *counts = 0ULL;
    best_error = MR_UINT64_MAX;
    for (psc = psc_min; psc <= search_end; psc++) {
        /* Calculate candidate counts */
        divider = psc * factor;
        value = (ticks + (divider / 2ULL)) / divider;
        if ((value == 0ULL) || (value > counter_max)) {
            continue;
        }

        /* Calculate candidate error */
        actual_ticks = value * divider;
        error = pwm_tick_error(ticks, actual_ticks);
        if (error >= best_error) {
            continue;
        }

        /* Update best period */
        best_error = error;
        *prescaler = psc;
        *counts = value;
        if (error == 0ULL) {
            break;
        }
    }

    /* Check result */
    if (*counts == 0ULL) {
        return -MR_ERANGE;
    }
    return 0;
}

static mr_err_t pwm_make_period(const TIM_TypeDef *instance, mr_uint64_t period,
                                mr_pwm_alignment_t alignment,
                                uint32_t *prescaler, uint32_t *arr,
                                mr_uint64_t *actual) {
    mr_uint64_t clock, counter_max, factor, ticks;
    mr_uint64_t psc, counts, actual_ticks;
    mr_err_t ret;

    /* Get PWM limits */
    clock = pwm_clock(instance);
    counter_max = pwm_counter_max(instance);
    factor = (alignment == MR_PWM_ALIGN_CENTER) ? 2ULL : 1ULL;
    if (clock == 0ULL) {
        return -MR_ERANGE;
    }

    /* Calculate target ticks */
    ret = pwm_period_ticks(clock, period, &ticks);
    if (ret != 0) {
        return ret;
    }
    if (ticks > (counter_max * PWM_PRESCALER_MAX * factor)) {
        return -MR_ERANGE;
    }

    /* Find PWM period */
    ret = pwm_find_period(ticks, counter_max, factor, &psc, &counts);
    if (ret != 0) {
        return ret;
    }

    /* Set PWM period */
    *prescaler = (uint32_t)(psc - 1ULL);
    *arr = (uint32_t)(counts - 1ULL);

    /* Calculate actual period */
    actual_ticks = counts * psc * factor;
    *actual = pwm_ticks_period(clock, actual_ticks);
    return 0;
}

static mr_err_t pwm_make_pulse(const pwm_priv_t *priv, mr_uint64_t pulse,
                               uint32_t *compare, mr_uint64_t *actual) {
    mr_uint64_t clock, divider, factor, ticks, counts;
    mr_err_t ret;

    /* Get PWM clock */
    clock = pwm_clock(priv->handle.Instance);
    if (clock == 0ULL) {
        return -MR_ERANGE;
    }

    /* Check pulse */
    if (pulse > priv->config.period) {
        return -MR_EINVAL;
    }

    /* Calculate pulse ticks */
    ticks = 0ULL;
    if (pulse != 0ULL) {
        ret = pwm_period_ticks(clock, pulse, &ticks);
        if (ret != 0) {
            return ret;
        }
    }

    /* Calculate compare */
    divider = (mr_uint64_t)priv->handle.Init.Prescaler + 1ULL;
    factor = (priv->config.alignment == MR_PWM_ALIGN_CENTER) ? 2ULL : 1ULL;
    divider *= factor;
    counts = (ticks + (divider / 2ULL)) / divider;
    if (counts > ((mr_uint64_t)priv->handle.Init.Period + 1ULL)) {
        return -MR_ERANGE;
    }

    /* Set pulse */
    *compare = (uint32_t)counts;
    *actual = pwm_ticks_period(clock, counts * divider);
    return 0;
}

static pwm_channel_t *pwm_channel_find(pwm_priv_t *priv, mr_uint32_t index) {
    mr_size_t i;

    /* Find PWM channel */
    for (i = 0U; i < priv->channels_num; i++) {
        if (priv->channels[i].index == index) {
            return &priv->channels[i];
        }
    }
    return MR_NULL;
}

static uint32_t pwm_channel_enable(uint32_t channel) {
    /* Translate HAL channel */
    if (channel == TIM_CHANNEL_1) {
        return TIM_CCER_CC1E;
    }
#if defined(TIM_CHANNEL_2) && defined(TIM_CCER_CC2E)
    if (channel == TIM_CHANNEL_2) {
        return TIM_CCER_CC2E;
    }
#endif /* defined(TIM_CHANNEL_2) && defined(TIM_CCER_CC2E) */
#if defined(TIM_CHANNEL_3) && defined(TIM_CCER_CC3E)
    if (channel == TIM_CHANNEL_3) {
        return TIM_CCER_CC3E;
    }
#endif /* defined(TIM_CHANNEL_3) && defined(TIM_CCER_CC3E) */
#if defined(TIM_CHANNEL_4) && defined(TIM_CCER_CC4E)
    if (channel == TIM_CHANNEL_4) {
        return TIM_CCER_CC4E;
    }
#endif /* defined(TIM_CHANNEL_4) && defined(TIM_CCER_CC4E) */
    return 0U;
}

static mr_bool_t pwm_channel_supported(const pwm_priv_t *priv,
                                       uint32_t channel) {
    /* Check HAL channel */
    if (pwm_channel_enable(channel) == 0U) {
        return MR_FALSE;
    }

    /* Check TMR channel */
#if defined(IS_TIM_CCX_CHANNEL)
    if (!IS_TIM_CCX_CHANNEL(priv->handle.Instance, channel)) {
        return MR_FALSE;
    }
#else
    MR_IGNORE(priv);
#endif /* defined(IS_TIM_CCX_CHANNEL) */
    return MR_TRUE;
}

static mr_err_t pwm_check_resources(const mr_driver_t *drv) {
    const mr_pwm_data_t *data = MR_DRIVER_DATA(drv);
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t channel_mask, enable_mask;
    uint32_t enable;
    mr_size_t i;

    /* Check PWM resources */
    if ((priv->handle.Instance == MR_NULL) || (priv->clk_enable == MR_NULL)) {
        return -MR_ENODEV;
    }
    if ((priv->channels == MR_NULL) || (priv->channels_num == 0U)
        || (data == MR_NULL)) {
        return -MR_ENODEV;
    }
    if (priv->channels_num > PWM_CHANNEL_BITS) {
        return -MR_EINVAL;
    }
#if defined(IS_TIM_INSTANCE)
    if (!IS_TIM_INSTANCE(priv->handle.Instance)) {
        return -MR_EINVAL;
    }
#endif /* defined(IS_TIM_INSTANCE) */

    /* Check PWM channels */
    channel_mask = 0U;
    enable_mask = 0U;
    for (i = 0U; i < priv->channels_num; i++) {
        /* Check logical channel */
        if (priv->channels[i].index >= PWM_CHANNEL_BITS) {
            return -MR_EINVAL;
        }
        if ((channel_mask & (1UL << priv->channels[i].index)) != 0U) {
            return -MR_EINVAL;
        }

        /* Check hardware channel */
        if (!pwm_channel_supported(priv, priv->channels[i].hal_channel)) {
            return -MR_ENOSYS;
        }
        enable = pwm_channel_enable(priv->channels[i].hal_channel);
        if ((enable_mask & enable) != 0U) {
            return -MR_EINVAL;
        }

        /* Update channel masks */
        channel_mask |= 1UL << priv->channels[i].index;
        enable_mask |= enable;
    }

    /* Check channel mask */
    if (channel_mask != data->channel_mask) {
        return -MR_EINVAL;
    }
    return 0;
}

static void pwm_compare_write_immediate(pwm_priv_t *priv,
                                        const pwm_channel_t *output,
                                        uint32_t compare) {
    /* Write compare immediately */
    __HAL_TIM_DISABLE_OCxPRELOAD(&priv->handle, output->hal_channel);
    __HAL_TIM_SET_COMPARE(&priv->handle, output->hal_channel, compare);
    __HAL_TIM_ENABLE_OCxPRELOAD(&priv->handle, output->hal_channel);
}

static mr_err_t pwm_make_init(const pwm_priv_t *priv,
                              const mr_pwm_config_t *config,
                              TIM_Base_InitTypeDef *init,
                              mr_pwm_config_t *actual) {
    uint32_t prescaler, period, counter_mode, repetition;
    mr_err_t ret;

    /* Check configuration */
    if (config->cycles != 0U) {
        return -MR_ENOSYS;
    }
    if (config->update_mode != MR_PWM_UPDATE_PERIOD) {
        return -MR_ENOSYS;
    }

    /* Translate alignment */
    switch (config->alignment) {
        case MR_PWM_ALIGN_EDGE_UP: {
            counter_mode = TIM_COUNTERMODE_UP;
            repetition = 0U;
            break;
        }
        case MR_PWM_ALIGN_EDGE_DOWN: {
            counter_mode = TIM_COUNTERMODE_DOWN;
            repetition = 0U;
            break;
        }
        case MR_PWM_ALIGN_CENTER: {
            counter_mode = TIM_COUNTERMODE_CENTERALIGNED1;
            repetition = 1U;
            break;
        }
        default: {
            return -MR_EINVAL;
        }
    }

    /* Calculate PWM period */
    *actual = *config;
    ret = pwm_make_period(priv->handle.Instance, config->period,
                          (mr_pwm_alignment_t)config->alignment, &prescaler,
                          &period, &actual->period);
    if (ret != 0) {
        return ret;
    }

    /* Build PWM init */
    *init = (TIM_Base_InitTypeDef){0};
    init->Prescaler = prescaler;
    init->CounterMode = counter_mode;
    init->Period = period;
    init->ClockDivision = TIM_CLOCKDIVISION_DIV1;
    init->RepetitionCounter = repetition;
#if defined(TIM_AUTORELOAD_PRELOAD_ENABLE)
    init->AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
#endif /* defined(TIM_AUTORELOAD_PRELOAD_ENABLE) */
    return 0;
}

static mr_err_t pwm_configure(const mr_driver_t *drv,
                              const mr_pwm_config_t *config) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    TIM_Base_InitTypeDef init;
    mr_pwm_config_t actual;
    mr_size_t i;
    mr_err_t ret;

    /* Check PWM resources */
    ret = pwm_check_resources(drv);
    if (ret != 0) {
        return ret;
    }

    /* Check PWM state */
    if ((priv->running_mask != 0U) || priv->signal_enabled) {
        return -MR_EBUSY;
    }

    /* Build PWM init */
    ret = pwm_make_init(priv, config, &init, &actual);
    if (ret != 0) {
        return ret;
    }

    /* Enable PWM clock */
    priv->clk_enable();

    /* Init PWM */
    priv->handle.Init = init;
    ret = mr_stm32_hal_status(HAL_TIM_PWM_Init(&priv->handle));
    if (ret != 0) {
        return ret;
    }

    /* Update PWM state */
    priv->config = actual;
    priv->running_mask = 0U;
    priv->configured = MR_TRUE;
    for (i = 0U; i < priv->channels_num; i++) {
        priv->channels[i].configured = MR_FALSE;
    }
    return 0;
}

static mr_err_t pwm_configuration(const mr_driver_t *drv,
                                  mr_pwm_config_t *config) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Get PWM configuration */
    if (!priv->configured) {
        return -MR_ENODEV;
    }
    *config = priv->config;
    return 0;
}

static mr_err_t pwm_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_pwm_channel_config_t *config) {
    TIM_OC_InitTypeDef oc = (TIM_OC_InitTypeDef){0};
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_pwm_channel_config_t actual;
    pwm_channel_t *output;
    uint32_t compare;
    mr_err_t ret;

    /* Check PWM state */
    if (!priv->configured) {
        return -MR_ENODEV;
    }
    if (priv->running_mask != 0U) {
        return -MR_EBUSY;
    }

    /* Find PWM channel */
    output = pwm_channel_find(priv, channel);
    if (output == MR_NULL) {
        return -MR_EINVAL;
    }

    /* Check channel capability */
    if (!pwm_channel_supported(priv, output->hal_channel)) {
        return -MR_ENOSYS;
    }

    /* Check configuration */
    if (config->comp) {
        return -MR_ENOSYS;
    }
    if ((config->dead_rise != 0U) || (config->dead_fall != 0U)) {
        return -MR_ENOSYS;
    }
    if (config->phase != 0U) {
        return -MR_ENOSYS;
    }

    /* Calculate compare */
    actual = *config;
    ret = pwm_make_pulse(priv, config->pulse, &compare, &actual.pulse);
    if (ret != 0) {
        return ret;
    }

    /* Configure GPIO */
    ret = mr_stm32_gpio_pin_alternate(&output->gpio, output->alternate,
                                      MR_GPIO_PULL_NONE, MR_GPIO_OTYPE_PP);
    if (ret != 0) {
        return ret;
    }

    /* Build PWM channel */
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

    /* Configure PWM channel */
    ret = mr_stm32_hal_status(
        HAL_TIM_PWM_ConfigChannel(&priv->handle, &oc, output->hal_channel));
    if (ret != 0) {
        return ret;
    }

    /* Update channel state */
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

    /* Get channel configuration */
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
    mr_err_t ret;
    int mask;

    /* Check PWM state */
    if (!priv->configured) {
        return -MR_ENODEV;
    }

    /* Check transaction */
    if (count == 0U) {
        return 0;
    }
    if (count > 1U) {
        return -MR_ENOSYS;
    }

    /* Find PWM channel */
    output = pwm_channel_find(priv, updates[0].channel);
    if ((output == MR_NULL) || !output->configured) {
        return -MR_ENODEV;
    }

    /* Check update */
    if (updates[0].phase != 0U) {
        return -MR_ENOSYS;
    }
    if ((updates[0].state != MR_PWM_OUTPUT_PWM)
        || (updates[0].comp_state != MR_PWM_OUTPUT_PWM)) {
        return -MR_ENOSYS;
    }

    /* Calculate compare */
    ret = pwm_make_pulse(priv, updates[0].pulse, &compare, &actual);
    if (ret != 0) {
        return ret;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Update PWM channel */
    if ((priv->running_mask & (1UL << updates[0].channel)) != 0U) {
        __HAL_TIM_SET_COMPARE(&priv->handle, output->hal_channel, compare);
    }
    output->compare = compare;
    output->config.pulse = actual;

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t pwm_start(const mr_driver_t *drv, mr_uint32_t channels) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    uint32_t enable, enable_mask;
    pwm_channel_t *output;
    mr_uint32_t index;
    int mask;

    /* Check PWM state */
    if (!priv->configured) {
        return -MR_ENODEV;
    }
    if (priv->running_mask != 0U) {
        return -MR_ENOSYS;
    }

    /* Check PWM channels */
    enable_mask = 0U;
    for (index = 0U; index < PWM_CHANNEL_BITS; index++) {
        if ((channels & (1UL << index)) == 0U) {
            continue;
        }

        /* Find PWM channel */
        output = pwm_channel_find(priv, index);
        if ((output == MR_NULL) || !output->configured) {
            return -MR_ENODEV;
        }

        /* Get channel enable */
        enable = pwm_channel_enable(output->hal_channel);
        if (enable == 0U) {
            return -MR_ENOSYS;
        }
        enable_mask |= enable;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Load compare values */
    for (index = 0U; index < PWM_CHANNEL_BITS; index++) {
        if ((channels & (1UL << index)) == 0U) {
            continue;
        }
        output = pwm_channel_find(priv, index);
        pwm_compare_write_immediate(priv, output, output->compare);
    }

    /* Init counter */
    __HAL_TIM_SET_COUNTER(&priv->handle,
                          (priv->config.alignment == MR_PWM_ALIGN_EDGE_DOWN)
                              ? priv->handle.Init.Period
                              : 0U);
    __HAL_TIM_CLEAR_FLAG(&priv->handle, TIM_FLAG_UPDATE);

    /* Start PWM */
    SET_BIT(priv->handle.Instance->CCER, enable_mask);
#if defined(IS_TIM_BREAK_INSTANCE)
    if (IS_TIM_BREAK_INSTANCE(priv->handle.Instance)) {
        __HAL_TIM_MOE_ENABLE(&priv->handle);
    }
#endif /* defined(IS_TIM_BREAK_INSTANCE) */
    __HAL_TIM_ENABLE(&priv->handle);
    priv->running_mask = channels;

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t pwm_stop(const mr_driver_t *drv, mr_uint32_t channels) {
    uint32_t enable, enable_mask, disable_mask;
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    pwm_channel_t *output;
    mr_uint32_t index;
    int mask;

    /* Check PWM channels */
    enable_mask = 0U;
    disable_mask = 0U;
    for (index = 0U; index < PWM_CHANNEL_BITS; index++) {
        if ((channels & (1UL << index)) == 0U) {
            continue;
        }

        /* Find PWM channel */
        output = pwm_channel_find(priv, index);
        if (output == MR_NULL) {
            return -MR_EINVAL;
        }

        /* Get channel enable */
        enable = pwm_channel_enable(output->hal_channel);
        if (enable == 0U) {
            return -MR_ENOSYS;
        }
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Apply idle state */
    for (index = 0U; index < PWM_CHANNEL_BITS; index++) {
        if ((channels & (1UL << index)) == 0U) {
            continue;
        }
        output = pwm_channel_find(priv, index);
        enable = pwm_channel_enable(output->hal_channel);
        switch (output->config.idle) {
            case MR_PWM_IDLE_INACTIVE: {
                pwm_compare_write_immediate(priv, output, 0U);
                enable_mask |= enable;
                break;
            }
            case MR_PWM_IDLE_ACTIVE: {
                pwm_compare_write_immediate(priv, output,
                                            priv->handle.Init.Period + 1U);
                enable_mask |= enable;
                break;
            }
            default: {
                disable_mask |= enable;
                break;
            }
        }
    }

    /* Stop PWM */
    CLEAR_BIT(priv->handle.Instance->CCER, disable_mask);
    SET_BIT(priv->handle.Instance->CCER, enable_mask);
    priv->running_mask &= ~channels;
    if (priv->running_mask == 0U) {
        __HAL_TIM_DISABLE(&priv->handle);
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t pwm_state(const mr_driver_t *drv, mr_pwm_state_t *state) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Get PWM state */
    state->running_mask = priv->running_mask;
    state->fault_mask = 0U;
    return 0;
}

static mr_bool_t pwm_period_signal_supported(const pwm_priv_t *priv) {
    /* Check TMR capability */
#if defined(STM32_PWM_HAS_MASTER)
    return IS_TIM_MASTER_INSTANCE(priv->handle.Instance) ? MR_TRUE : MR_FALSE;
#else
    MR_IGNORE(priv);
    return MR_FALSE;
#endif /* defined(STM32_PWM_HAS_MASTER) */
}

static mr_err_t pwm_signal_configure(const mr_driver_t *drv,
                                     mr_pwm_signal_t signal,
                                     const mr_pwm_signal_config_t *config) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check PWM state */
    if (!priv->configured) {
        return -MR_ENODEV;
    }

    /* Check signal */
    if (signal != MR_PWM_SIGNAL_PERIOD) {
        return (signal == MR_PWM_SIGNAL_COMPARE) ? -MR_ENOSYS : -MR_EINVAL;
    }
    if (!pwm_period_signal_supported(priv)) {
        return -MR_ENOSYS;
    }

    /* Check configuration */
    return (config->offset == 0U) ? 0 : -MR_EINVAL;
}

static mr_err_t pwm_signal_configuration(const mr_driver_t *drv,
                                         mr_pwm_signal_t signal,
                                         mr_pwm_signal_config_t *config) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check PWM state */
    if (!priv->configured) {
        return -MR_ENODEV;
    }

    /* Check signal */
    if (signal != MR_PWM_SIGNAL_PERIOD) {
        return (signal == MR_PWM_SIGNAL_COMPARE) ? -MR_ENOSYS : -MR_EINVAL;
    }
    if (!pwm_period_signal_supported(priv)) {
        return -MR_ENOSYS;
    }

    /* Get signal configuration */
    config->offset = 0U;
    return 0;
}

static mr_err_t pwm_signal_control(const mr_driver_t *drv,
                                   mr_pwm_signal_t signal, mr_bool_t enable) {
#if defined(STM32_PWM_HAS_MASTER)
    TIM_MasterConfigTypeDef master = (TIM_MasterConfigTypeDef){0};
    mr_err_t ret;
#endif /* defined(STM32_PWM_HAS_MASTER) */
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check PWM state */
    if (!priv->configured) {
        return -MR_ENODEV;
    }

    /* Check signal */
    if (signal != MR_PWM_SIGNAL_PERIOD) {
        return (signal == MR_PWM_SIGNAL_COMPARE) ? -MR_ENOSYS : -MR_EINVAL;
    }
    if (!pwm_period_signal_supported(priv)) {
        return -MR_ENOSYS;
    }
    if ((enable != MR_FALSE) == priv->signal_enabled) {
        return 0;
    }

#if defined(STM32_PWM_HAS_MASTER)
    /* Configure PWM signal */
    master.MasterOutputTrigger = enable ? TIM_TRGO_UPDATE : TIM_TRGO_RESET;
    master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    ret = mr_stm32_hal_status(
        HAL_TIMEx_MasterConfigSynchronization(&priv->handle, &master));
    if (ret != 0) {
        return ret;
    }

    /* Update signal state */
    priv->signal_enabled = (enable != MR_FALSE);
    return 0;
#else
    MR_IGNORE(enable);
    return -MR_ENOSYS;
#endif /* defined(STM32_PWM_HAS_MASTER) */
}

/* Instance install */
#define ST_HT_PWM_INSTALL(_id, _ops)                                           \
    static void MR_CAT(__pwm_clk_enable_, _id)(void) {                         \
        MR_HT_DRIVER_PROP(_id, st_clken)();                                    \
    }                                                                          \
    static pwm_channel_t MR_CAT(__pwm_channel_, _id)[]                         \
        = {MR_HT_DRIVER_PROP_ELEM_FOREACH(_id, st_channels,                    \
                                          __ST_HT_PWM_CHANNEL_IMPL)};          \
    static pwm_priv_t MR_CAT(__pwm_priv_, _id) = {                             \
        .handle = {                                                            \
            .Instance = MR_HT_DRIVER_PROP(_id, st_timx),                       \
        },                                                                     \
        .clk_enable = MR_CAT(__pwm_clk_enable_, _id),                          \
        .channels = MR_CAT(__pwm_channel_, _id),                               \
        .channels_num = MR_HT_DRIVER_PROP_ELEM_NUM(_id, st_channels),          \
    };                                                                         \
    MR_HT_PWM_INSTALL(_id, &MR_CAT(__pwm_priv_, _id), _ops)

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
#endif /* defined(MR_USE_DRIVER_PWM) */

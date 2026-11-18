/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_TMR)
#include <mrX/drivers/tmr/mr_tmr.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    st_stm32_tmr
#if MR_HT_HAS_DRIVER()

/* TMR capability definition */
#if defined(IS_TIM_MASTER_INSTANCE)
#define STM32_TMR_HAS_MASTER
#endif /* defined(IS_TIM_MASTER_INSTANCE) */

/* Driver TMR operations */
static mr_err_t tmr_configure(const mr_driver_t *drv,
                              const mr_tmr_config_t *config);
static mr_err_t tmr_configuration(const mr_driver_t *drv,
                                  mr_tmr_config_t *config);
static mr_err_t tmr_start(const mr_driver_t *drv);
static mr_err_t tmr_stop(const mr_driver_t *drv);
static mr_err_t tmr_is_busy(const mr_driver_t *drv, mr_bool_t *busy);
static mr_err_t tmr_signal_control(const mr_driver_t *drv,
                                   mr_tmr_signal_t signal, mr_bool_t enable);
static mr_err_t tmr_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable);
MR_UNUSED static const mr_tmr_ops_t __tmr_ops = {
    .configure = tmr_configure,
    .configuration = tmr_configuration,
    .start = tmr_start,
    .stop = tmr_stop,
    .is_busy = tmr_is_busy,
    .signal_control = tmr_signal_control,
    .event_control = tmr_event_control,
};

/* TMR private data type */
typedef struct tmr_priv {
    TIM_HandleTypeDef handle;
    void (*clk_enable)(void);
    IRQn_Type irqn;
    mr_uint32_t irq_priority;
    mr_driver_t *driver;
    mr_tmr_config_t config;
    mr_uint8_t signal_enabled: 1;
    mr_uint8_t event_enabled: 1;
} tmr_priv_t;

/* TMR time conversion definition */
#define TMR_NANOSECONDS                 (1000000000ULL)

/* TMR prescaler definition */
#define TMR_PRESCALER_MAX               (65536ULL)
#define TMR_PSC_SEARCH_LIMIT            (64ULL)

#if defined(RCC_CLOCKTYPE_PCLK2)
static mr_bool_t tmr_is_apb2(const TIM_TypeDef *instance) {
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

static mr_uint32_t tmr_apb_divider(mr_uint32_t divider) {
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

static mr_bool_t tmr_timpre_enabled(void) {
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

static mr_uint64_t tmr_apb_clock(const TIM_TypeDef *instance,
                                 mr_uint32_t *divider) {
    RCC_ClkInitTypeDef clock;
    uint32_t latency;

    /* Get clock configuration */
    HAL_RCC_GetClockConfig(&clock, &latency);

#if defined(RCC_CLOCKTYPE_PCLK2)
    /* Get APB2 clock */
    if (tmr_is_apb2(instance)) {
        *divider = tmr_apb_divider(clock.APB2CLKDivider);
        return HAL_RCC_GetPCLK2Freq();
    }
#endif /* defined(RCC_CLOCKTYPE_PCLK2) */

    /* Get APB1 clock */
    *divider = tmr_apb_divider(clock.APB1CLKDivider);
    return HAL_RCC_GetPCLK1Freq();
}

static mr_uint64_t tmr_clock(const TIM_TypeDef *instance) {
    mr_uint64_t pclk;
    mr_uint32_t divider;

    /* Get APB clock */
    pclk = tmr_apb_clock(instance, &divider);
    if ((pclk == 0ULL) || (divider == 0U)) {
        return 0ULL;
    }

    /* Check APB divider */
    if (divider == 1U) {
        return pclk;
    }

    /* Check TIMPRE */
    if (!tmr_timpre_enabled()) {
        return pclk * 2ULL;
    }

    /* Calculate TMR clock */
    if (divider <= 4U) {
        return HAL_RCC_GetHCLKFreq();
    }
    return pclk * 4ULL;
}

static mr_uint64_t tmr_counter_max(const TIM_TypeDef *instance) {
#if defined(IS_TIM_32B_COUNTER_INSTANCE)
    if (IS_TIM_32B_COUNTER_INSTANCE(instance)) {
        return (1ULL << 32);
    }
#else
    MR_IGNORE(instance);
#endif /* defined(IS_TIM_32B_COUNTER_INSTANCE) */
    return (1ULL << 16);
}

static mr_bool_t tmr_hw_busy(tmr_priv_t *priv) {
    /* Check TMR state */
    if (priv->driver == MR_NULL) {
        return MR_FALSE;
    }

    /* Check hardware state */
    if ((priv->handle.Instance->CR1 & TIM_CR1_CEN) != 0U) {
        return MR_TRUE;
    }

    /* Restore HAL state after one-shot */
    if ((priv->handle.Instance->CR1 & TIM_CR1_OPM) != 0U) {
        HAL_TIM_Base_Stop(&priv->handle);
    }
    return MR_FALSE;
}

static mr_err_t tmr_period_ticks(mr_uint64_t clock, mr_uint64_t period,
                                 mr_uint64_t *ticks) {
    mr_uint64_t seconds, remainder, value;

    /* Split period */
    remainder = period % TMR_NANOSECONDS;
    seconds = period / TMR_NANOSECONDS;
    if (seconds > (MR_UINT64_MAX / clock)) {
        return -MR_ERANGE;
    }

    /* Calculate whole-second ticks */
    value = seconds * clock;

    /* Calculate sub-second ticks */
    remainder *= clock;
    remainder += TMR_NANOSECONDS / 2ULL;
    remainder /= TMR_NANOSECONDS;
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

static mr_uint64_t tmr_ticks_period(mr_uint64_t clock, mr_uint64_t ticks) {
    mr_uint64_t seconds, remainder, period;

    /* Split ticks */
    seconds = ticks / clock;
    remainder = ticks % clock;

    /* Calculate whole-second period */
    period = seconds * TMR_NANOSECONDS;

    /* Calculate sub-second period */
    remainder *= TMR_NANOSECONDS;
    remainder += clock / 2ULL;
    remainder /= clock;

    /* Calculate period */
    return period + remainder;
}

static mr_uint64_t tmr_tick_error(mr_uint64_t target, mr_uint64_t actual) {
    /* Calculate tick error */
    if (actual >= target) {
        return actual - target;
    }
    return target - actual;
}

static mr_err_t tmr_find_period(mr_uint64_t ticks, mr_uint64_t counter_max,
                                mr_uint64_t *prescaler, mr_uint64_t *counts) {
    mr_uint64_t best_error, search_end, psc_min;
    mr_uint64_t actual_ticks, error, psc;
    mr_uint64_t value;

    /* Calculate prescaler range */
    psc_min = (ticks + counter_max - 1ULL) / counter_max;
    search_end = psc_min + TMR_PSC_SEARCH_LIMIT - 1ULL;
    if (search_end > TMR_PRESCALER_MAX) {
        search_end = TMR_PRESCALER_MAX;
    }

    /* Find best period */
    *prescaler = 0ULL;
    *counts = 0ULL;
    best_error = MR_UINT64_MAX;
    for (psc = psc_min; psc <= search_end; psc++) {
        /* Calculate candidate counts */
        value = (ticks + (psc / 2ULL)) / psc;
        if ((value == 0ULL) || (value > counter_max)) {
            continue;
        }

        /* Calculate candidate error */
        actual_ticks = value * psc;
        error = tmr_tick_error(ticks, actual_ticks);
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

static mr_err_t tmr_make_period(const TIM_TypeDef *instance, mr_uint64_t period,
                                uint32_t *prescaler, uint32_t *arr,
                                mr_uint64_t *actual) {
    mr_uint64_t timer_clock, counter_max, ticks;
    mr_uint64_t psc, counts, actual_ticks;
    mr_err_t ret;

    /* Get TMR limits */
    timer_clock = tmr_clock(instance);
    counter_max = tmr_counter_max(instance);
    if (timer_clock == 0ULL) {
        return -MR_ERANGE;
    }

    /* Calculate target ticks */
    ret = tmr_period_ticks(timer_clock, period, &ticks);
    if (ret != 0) {
        return ret;
    }
    if (ticks > (counter_max * TMR_PRESCALER_MAX)) {
        return -MR_ERANGE;
    }

    /* Find TMR period */
    ret = tmr_find_period(ticks, counter_max, &psc, &counts);
    if (ret != 0) {
        return ret;
    }

    /* Set TMR period */
    *prescaler = (uint32_t)(psc - 1ULL);
    *arr = (uint32_t)(counts - 1ULL);

    /* Calculate actual period */
    actual_ticks = counts * psc;
    *actual = tmr_ticks_period(timer_clock, actual_ticks);
    return 0;
}

static void tmr_set_mode(tmr_priv_t *priv, mr_tmr_mode_t mode) {
    /* Set TMR mode */
    if (mode == MR_TMR_MODE_ONESHOT) {
        SET_BIT(priv->handle.Instance->CR1, TIM_CR1_OPM);
    } else {
        CLEAR_BIT(priv->handle.Instance->CR1, TIM_CR1_OPM);
    }
}

static mr_err_t tmr_configure(const mr_driver_t *drv,
                              const mr_tmr_config_t *config) {
    tmr_priv_t *priv = MR_DRIVER_PRIV(drv);
    uint32_t prescaler, period;
    mr_uint64_t actual_period;
    mr_err_t ret;

    /* Check parameter */
    if ((config->period == 0ULL) || (config->mode > MR_TMR_MODE_ONESHOT)) {
        return -MR_EINVAL;
    }

    /* Check TMR resources */
    if ((priv->handle.Instance == MR_NULL) || (priv->clk_enable == MR_NULL)) {
        return -MR_ENODEV;
    }

    /* Check signal state */
    if (priv->signal_enabled) {
        return -MR_EBUSY;
    }

    /* Enable TMR clock */
    priv->clk_enable();

    /* Check TMR state */
    if (tmr_hw_busy(priv)) {
        return -MR_EBUSY;
    }

    /* Calculate TMR period */
    ret = tmr_make_period(priv->handle.Instance, config->period, &prescaler,
                          &period, &actual_period);
    if (ret != 0) {
        return ret;
    }

    /* Init TMR */
    priv->handle.Init.Prescaler = prescaler;
    priv->handle.Init.CounterMode = TIM_COUNTERMODE_UP;
    priv->handle.Init.Period = period;
    priv->handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
#if defined(TIM_AUTORELOAD_PRELOAD_DISABLE)
    priv->handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
#endif /* defined(TIM_AUTORELOAD_PRELOAD_DISABLE) */
    ret = mr_stm32_hal_status(HAL_TIM_Base_Init(&priv->handle));
    if (ret != 0) {
        return ret;
    }

    /* Set TMR mode */
    tmr_set_mode(priv, config->mode);

    /* Save TMR configuration */
    priv->config = *config;
    priv->config.period = actual_period;

    /* Init TMR IRQ */
    if (priv->driver == MR_NULL) {
        priv->driver = (mr_driver_t *)drv;
        HAL_NVIC_SetPriority(priv->irqn, priv->irq_priority, 0U);
        HAL_NVIC_EnableIRQ(priv->irqn);
    }
    return 0;
}

static mr_err_t tmr_configuration(const mr_driver_t *drv,
                                  mr_tmr_config_t *config) {
    tmr_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check TMR state */
    if (priv->driver == MR_NULL) {
        return -MR_ENODEV;
    }

    /* Get TMR configuration */
    *config = priv->config;
    return 0;
}

static mr_err_t tmr_start(const mr_driver_t *drv) {
    tmr_priv_t *priv = MR_DRIVER_PRIV(drv);
    HAL_StatusTypeDef status;

    /* Check TMR state */
    if (priv->driver == MR_NULL) {
        return -MR_ENODEV;
    }

    /* Set TMR mode */
    tmr_set_mode(priv, priv->config.mode);

    /* Reset TMR */
    __HAL_TIM_SET_COUNTER(&priv->handle, 0U);
    __HAL_TIM_CLEAR_FLAG(&priv->handle, TIM_FLAG_UPDATE);

    /* Start TMR */
    if (priv->event_enabled) {
        status = HAL_TIM_Base_Start_IT(&priv->handle);
    } else {
        __HAL_TIM_DISABLE_IT(&priv->handle, TIM_IT_UPDATE);
        status = HAL_TIM_Base_Start(&priv->handle);
    }
    return mr_stm32_hal_status(status);
}

static mr_err_t tmr_stop(const mr_driver_t *drv) {
    tmr_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check TMR state */
    if (priv->driver == MR_NULL) {
        return -MR_ENODEV;
    }

    /* Stop TMR */
    return mr_stm32_hal_status(HAL_TIM_Base_Stop_IT(&priv->handle));
}

static mr_err_t tmr_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    tmr_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Get TMR state */
    *busy = tmr_hw_busy(priv);
    return 0;
}

static mr_err_t tmr_signal_control(const mr_driver_t *drv,
                                   mr_tmr_signal_t signal, mr_bool_t enable) {
    tmr_priv_t *priv = MR_DRIVER_PRIV(drv);
#if defined(STM32_TMR_HAS_MASTER)
    TIM_MasterConfigTypeDef master;
    mr_err_t ret;
#endif /* defined(STM32_TMR_HAS_MASTER) */

    /* Check parameter */
    if (signal != MR_TMR_SIGNAL_PERIOD) {
        return -MR_EINVAL;
    }

    /* Check TMR state */
    if (priv->driver == MR_NULL) {
        return -MR_ENODEV;
    }

#if defined(STM32_TMR_HAS_MASTER)
    /* Check TMR capability */
    if (!IS_TIM_MASTER_INSTANCE(priv->handle.Instance)) {
        return -MR_ENOSYS;
    }

    /* Configure TMR signal */
    master = (TIM_MasterConfigTypeDef){0};
    master.MasterOutputTrigger = enable ? TIM_TRGO_UPDATE : TIM_TRGO_RESET;
    master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    ret = mr_stm32_hal_status(
        HAL_TIMEx_MasterConfigSynchronization(&priv->handle, &master));
    if (ret == 0) {
        priv->signal_enabled = (enable != MR_FALSE);
    }
    return ret;
#else
    MR_IGNORE(enable);
    return -MR_ENOSYS;
#endif /* defined(STM32_TMR_HAS_MASTER) */
}

static mr_err_t tmr_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable) {
    tmr_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check parameter */
    if ((events == 0U) || ((events & ~MR_TMR_WAIT_TIMEOUT) != 0U)) {
        return -MR_EINVAL;
    }

    /* Check TMR state */
    if (priv->driver == MR_NULL) {
        return -MR_ENODEV;
    }
    if (tmr_hw_busy(priv)) {
        return -MR_EBUSY;
    }

    /* Update event state */
    priv->event_enabled = (enable != MR_FALSE);
    return 0;
}

static void tmr_irq(tmr_priv_t *priv) {
    mr_bool_t update_flag, update_source, update_pending;

    /* Check update flag */
    update_flag = (__HAL_TIM_GET_FLAG(&priv->handle, TIM_FLAG_UPDATE) != 0U);

    /* Check update source */
    update_source
        = (__HAL_TIM_GET_IT_SOURCE(&priv->handle, TIM_IT_UPDATE) != 0U);

    /* Check update event */
    update_pending = update_flag && update_source;

    /* Handle TMR IRQ */
    HAL_TIM_IRQHandler(&priv->handle);

    /* Raise TMR event */
    if (update_pending && priv->event_enabled && (priv->driver != MR_NULL)) {
        mr_tmr_event_raise(priv->driver, MR_TMR_WAIT_TIMEOUT);
    }
}

/* Instance install */
#define ST_HT_TMR_INSTALL(_id, _ops)                                           \
    static void MR_CAT(__tmr_clk_enable_, _id)(void) {                         \
        MR_HT_DRIVER_PROP(_id, st_clken)();                                    \
    }                                                                          \
    static tmr_priv_t MR_CAT(__tmr_priv_, _id) = {                             \
        .handle = {                                                            \
            .Instance = MR_HT_DRIVER_PROP(_id, st_timx),                       \
        },                                                                     \
        .clk_enable = MR_CAT(__tmr_clk_enable_, _id),                          \
        .irqn = MR_HT_DRIVER_PROP(_id, st_irqn),                               \
        .irq_priority = MR_HT_DRIVER_PROP(_id, st_irq_priority),               \
    };                                                                         \
    void MR_HT_DRIVER_PROP(_id, st_irq_handler)(void) {                        \
        tmr_irq(&MR_CAT(__tmr_priv_, _id));                                    \
    }                                                                          \
    MR_HT_TMR_INSTALL(_id, &MR_CAT(__tmr_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(ST_HT_TMR_INSTALL, &__tmr_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_TMR) */

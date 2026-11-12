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
#include "stm32h5xx_hal_tim.h"

/* HardTree compatible */
#define MR_HT_COMPAT                    st_stm32h5_tmr
#if MR_HT_HAS_DRIVER()

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
    mr_driver_t *drv;
    mr_tmr_config_t config;
    mr_uint8_t signal_enabled: 1;
    mr_uint8_t event_enabled: 1;
} tmr_priv_t;

/* TMR time conversion definition */
#define TMR_NANOSECONDS                 (1000000000ULL)

/* TMR prescaler limit definition */
#define TMR_PRESCALER_MAX               (65536ULL)
#define TMR_PSC_SEARCH_LIMIT            (64ULL)

static mr_bool_t tmr_is_apb2(TIM_TypeDef *instance) {
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

static mr_bool_t tmr_hw_busy(tmr_priv_t *priv) {
    mr_bool_t busy;

    /* Reconcile HAL state after hardware clears CEN in one-pulse mode */
    busy = (priv->drv != MR_NULL)
        && ((priv->handle.Instance->CR1 & TIM_CR1_CEN) != 0U);
    if ((priv->drv != MR_NULL) && !busy
        && ((priv->handle.Instance->CR1 & TIM_CR1_OPM) != 0U)) {
        HAL_TIM_Base_Stop(&priv->handle);
    }
    return busy;
}

static mr_uint64_t tmr_clock(TIM_TypeDef *instance) {
    mr_uint32_t ppre, apb_div;
    mr_uint64_t pclk;

    /* Get the APB clock and prescaler for this timer */
    if (tmr_is_apb2(instance)) {
        pclk = HAL_RCC_GetPCLK2Freq();
        ppre = (RCC->CFGR2 & RCC_CFGR2_PPRE2) >> RCC_CFGR2_PPRE2_Pos;
    } else {
        pclk = HAL_RCC_GetPCLK1Freq();
        ppre = (RCC->CFGR2 & RCC_CFGR2_PPRE1) >> RCC_CFGR2_PPRE1_Pos;
    }

    /* Apply the APB prescaler and TIMPRE clock rules */
    if (ppre < 4U) {
        /* APB /1: the timer kernel clock equals PCLK */
        return pclk;
    }
    apb_div = 1U << (ppre - 3U);
    if ((RCC->CFGR1 & RCC_CFGR1_TIMPRE) == 0U) {
        return pclk * 2U;
    }
    /* TIMPRE: HCLK when APB is /1, /2 or /4, otherwise 4x PCLK */
    if (apb_div <= 4U) {
        return (mr_uint64_t)HAL_RCC_GetHCLKFreq();
    }
    return pclk * 4U;
}

static mr_err_t tmr_make_period(TIM_TypeDef *instance, mr_uint64_t period,
                                uint32_t *prescaler, uint32_t *arr,
                                mr_uint64_t *actual) {
    mr_uint64_t timer_clock, counter_max, ticks, actual_ticks;
    mr_uint64_t psc, psc_min, search_end, counts, error;
    mr_uint64_t best_psc, best_counts, best_error;
    mr_uint64_t seconds, remainder;

    /* Determine the timer clock and hardware counter range */
    best_psc = 0U;
    best_counts = 0U;
    best_error = MR_UINT64_MAX;
    timer_clock = tmr_clock(instance);
    counter_max
        = IS_TIM_32B_COUNTER_INSTANCE(instance) ? (1ULL << 32) : (1ULL << 16);
    if (timer_clock == 0U) {
        return -MR_ERANGE;
    }

    /* Split the period to avoid overflowing the nanosecond product */
    seconds = period / TMR_NANOSECONDS;
    remainder = period % TMR_NANOSECONDS;
    if (seconds > (MR_UINT64_MAX / timer_clock)) {
        return -MR_ERANGE;
    }
    ticks = seconds * timer_clock;
    remainder = ((remainder * timer_clock) + (TMR_NANOSECONDS / 2U))
        / TMR_NANOSECONDS;
    if (ticks > (MR_UINT64_MAX - remainder)) {
        return -MR_ERANGE;
    }
    ticks += remainder;
    if ((ticks == 0U) || (ticks > (counter_max * TMR_PRESCALER_MAX))) {
        return -MR_ERANGE;
    }

    /* Search from the smallest valid divider for the lowest tick error */
    psc_min = (ticks + counter_max - 1U) / counter_max;
    search_end = psc_min + TMR_PSC_SEARCH_LIMIT - 1U;
    if (search_end > TMR_PRESCALER_MAX) {
        search_end = TMR_PRESCALER_MAX;
    }
    for (psc = psc_min; psc <= search_end; psc++) {
        counts = (ticks + (psc / 2U)) / psc;
        if ((counts == 0U) || (counts > counter_max)) {
            continue;
        }

        actual_ticks = counts * psc;
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

    /* Return the register values and their actual period */
    *prescaler = (uint32_t)(best_psc - 1U);
    *arr = (uint32_t)(best_counts - 1U);
    actual_ticks = best_counts * best_psc;
    seconds = actual_ticks / timer_clock;
    remainder = actual_ticks % timer_clock;
    *actual
        = (seconds * TMR_NANOSECONDS)
        + (((remainder * TMR_NANOSECONDS) + (timer_clock / 2U)) / timer_clock);
    return 0;
}

static mr_err_t tmr_configure(const mr_driver_t *drv,
                              const mr_tmr_config_t *config) {
    tmr_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t prescaler, period;
    mr_uint64_t actual_period;
    mr_err_t ret;

    /* Check parameter */
    if (!IS_TIM_INSTANCE(priv->handle.Instance) || (config->period == 0U)
        || (config->mode > MR_TMR_MODE_ONESHOT)) {
        return -MR_EINVAL;
    }
    if (priv->clk_enable == MR_NULL) {
        return -MR_ENODEV;
    }
    if (priv->signal_enabled) {
        return -MR_EBUSY;
    }

    /* Restore the timer clock before querying or programming hardware */
    priv->clk_enable();

    /* Check whether the timer is running */
    if (tmr_hw_busy(priv)) {
        return -MR_EBUSY;
    }

    /* Convert nanoseconds to PSC/ARR */
    ret = tmr_make_period(priv->handle.Instance, config->period, &prescaler,
                          &period, &actual_period);
    if (ret != 0) {
        return ret;
    }

    /* Init the time base */
    priv->handle.Init.Prescaler = prescaler;
    priv->handle.Init.CounterMode = TIM_COUNTERMODE_UP;
    priv->handle.Init.Period = period;
    priv->handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    priv->handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    ret = mr_stm32h5_hal_status(HAL_TIM_Base_Init(&priv->handle));
    if (ret != 0) {
        return ret;
    }

    /* HAL base initialization does not configure one-pulse mode */
    if (config->mode == MR_TMR_MODE_ONESHOT) {
        SET_BIT(priv->handle.Instance->CR1, TIM_CR1_OPM);
    } else {
        CLEAR_BIT(priv->handle.Instance->CR1, TIM_CR1_OPM);
    }

    /* Save the configuration applied to hardware */
    priv->config = *config;
    priv->config.period = actual_period;

    /* Set the IRQ owner before enabling the line */
    if (priv->drv == MR_NULL) {
        priv->drv = (mr_driver_t *)drv;
        HAL_NVIC_SetPriority(priv->irqn, priv->irq_priority, 0U);
        HAL_NVIC_EnableIRQ(priv->irqn);
    }
    return 0;
}

static mr_err_t tmr_configuration(const mr_driver_t *drv,
                                  mr_tmr_config_t *config) {
    tmr_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check that the time base has been initialized */
    if (priv->drv == MR_NULL) {
        return -MR_ENODEV;
    }

    /* Get the configuration */
    *config = priv->config;
    return 0;
}

static mr_err_t tmr_start(const mr_driver_t *drv) {
    tmr_priv_t *priv = MR_DRIVER_PRIV(drv);
    HAL_StatusTypeDef status;

    /* Check configuration */
    if (priv->drv == MR_NULL) {
        return -MR_ENODEV;
    }

    /* Restore the configured mode */
    if (priv->config.mode == MR_TMR_MODE_ONESHOT) {
        SET_BIT(priv->handle.Instance->CR1, TIM_CR1_OPM);
    } else {
        CLEAR_BIT(priv->handle.Instance->CR1, TIM_CR1_OPM);
    }

    /* Reload from 0 so the first period is complete */
    __HAL_TIM_SET_COUNTER(&priv->handle, 0U);
    __HAL_TIM_CLEAR_FLAG(&priv->handle, TIM_FLAG_UPDATE);
    /* Enable update IRQs only for a timeout subscription */
    if (priv->event_enabled) {
        status = HAL_TIM_Base_Start_IT(&priv->handle);
    } else {
        __HAL_TIM_DISABLE_IT(&priv->handle, TIM_IT_UPDATE);
        status = HAL_TIM_Base_Start(&priv->handle);
    }
    return mr_stm32h5_hal_status(status);
}

static mr_err_t tmr_stop(const mr_driver_t *drv) {
    tmr_priv_t *priv = MR_DRIVER_PRIV(drv);

    return mr_stm32h5_hal_status(HAL_TIM_Base_Stop_IT(&priv->handle));
}

static mr_err_t tmr_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    tmr_priv_t *priv = MR_DRIVER_PRIV(drv);

    *busy = tmr_hw_busy(priv);
    return 0;
}

static mr_err_t tmr_signal_control(const mr_driver_t *drv,
                                   mr_tmr_signal_t signal, mr_bool_t enable) {
    tmr_priv_t *priv = MR_DRIVER_PRIV(drv);
    TIM_MasterConfigTypeDef master;
    mr_err_t ret;

    /* Check signal and hardware capability */
    if (signal != MR_TMR_SIGNAL_PERIOD) {
        return -MR_EINVAL;
    }
    if (priv->drv == MR_NULL) {
        return -MR_ENODEV;
    }
    if (!IS_TIM_MASTER_INSTANCE(priv->handle.Instance)) {
        return -MR_ENOSYS;
    }

    /* Route update events without changing counter state */
    master = (TIM_MasterConfigTypeDef){0};
    master.MasterOutputTrigger = enable ? TIM_TRGO_UPDATE : TIM_TRGO_RESET;
    master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    ret = mr_stm32h5_hal_status(
        HAL_TIMEx_MasterConfigSynchronization(&priv->handle, &master));
    if (ret == 0) {
        priv->signal_enabled = (enable != MR_FALSE);
    }
    return ret;
}

static mr_err_t tmr_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable) {
    tmr_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check parameter */
    if ((events == 0U) || ((events & ~MR_TMR_WAIT_TIMEOUT) != 0U)) {
        return -MR_EINVAL;
    }

    /* Check whether the timer is running */
    if (tmr_hw_busy(priv)) {
        return -MR_EBUSY;
    }
    /* Apply the subscription on the next start */
    priv->event_enabled = (enable != MR_FALSE);
    return 0;
}

static void tmr_irq(tmr_priv_t *priv) {
    mr_bool_t update_pending;

    if (priv == MR_NULL) {
        return;
    }

    /* Capture UIF before HAL_TIM_IRQHandler clears it */
    update_pending
        = ((__HAL_TIM_GET_FLAG(&priv->handle, TIM_FLAG_UPDATE) != 0U)
           && (__HAL_TIM_GET_IT_SOURCE(&priv->handle, TIM_IT_UPDATE) != 0U));
    HAL_TIM_IRQHandler(&priv->handle);

    /* Deliver only subscribed update events */
    if (update_pending && priv->event_enabled && (priv->drv != MR_NULL)) {
        mr_tmr_event_raise(priv->drv, MR_TMR_WAIT_TIMEOUT);
    }
}

/* Instance install with per-timer state and IRQ handler */
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

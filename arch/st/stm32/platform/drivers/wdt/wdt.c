/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_WDT)
#include <mrX/drivers/wdt/mr_wdt.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    st_stm32_wdt
#if MR_HT_HAS_DRIVER()

/* Driver WDT operations */
static mr_err_t wdt_configure(const mr_driver_t *drv,
                              const mr_wdt_config_t *config);
static mr_err_t wdt_configuration(const mr_driver_t *drv,
                                  mr_wdt_config_t *config);
static mr_err_t wdt_start(const mr_driver_t *drv);
static mr_err_t wdt_feed(const mr_driver_t *drv);
static mr_err_t wdt_is_busy(const mr_driver_t *drv, mr_bool_t *busy);
static mr_err_t wdt_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable);
MR_UNUSED static const mr_wdt_ops_t __wdt_ops = {
    .configure = wdt_configure,
    .configuration = wdt_configuration,
    .start = wdt_start,
    .stop = MR_NULL,
    .feed = wdt_feed,
    .is_busy = wdt_is_busy,
    .event_control = wdt_event_control,
};

/* WDT capability definition */
#if defined(IWDG_EWCR_EWIE) && defined(IWDG_EWCR_EWIT)
#define STM32_WDT_HAS_EWI
#endif /* defined(IWDG_EWCR_EWIE) && defined(IWDG_EWCR_EWIT) */

#if defined(STM32_WDT_HAS_EWI)
/* WDT write access definition */
#if defined(IWDG_ENABLE_WRITE_ACCESS) && defined(IWDG_DISABLE_WRITE_ACCESS)
#define WDT_WRITE_ENABLE(_priv)                                                \
    IWDG_ENABLE_WRITE_ACCESS(&(_priv)->handle)
#define WDT_WRITE_DISABLE(_priv)                                               \
    IWDG_DISABLE_WRITE_ACCESS(&(_priv)->handle)
#elif defined(IWDG_KEY_WRITE_ACCESS_ENABLE)                                    \
    && defined(IWDG_KEY_WRITE_ACCESS_DISABLE)
#define WDT_WRITE_ENABLE(_priv)                                                \
    WRITE_REG((_priv)->handle.Instance->KR, IWDG_KEY_WRITE_ACCESS_ENABLE)
#define WDT_WRITE_DISABLE(_priv)                                               \
    WRITE_REG((_priv)->handle.Instance->KR, IWDG_KEY_WRITE_ACCESS_DISABLE)
#else
#error "STM32 IWDG EWI requires write access support"
#endif
#endif /* defined(STM32_WDT_HAS_EWI) */

/* WDT prescaler type */
typedef struct wdt_prescaler {
    mr_uint32_t value;
    mr_uint32_t divider;
} wdt_prescaler_t;

/* WDT private data type */
typedef struct wdt_priv {
    IWDG_HandleTypeDef handle;
#if defined(STM32_WDT_HAS_EWI)
    IRQn_Type irqn;
    mr_uint32_t irq_priority;
#endif /* defined(STM32_WDT_HAS_EWI) */
    mr_driver_t *driver;
    mr_uint32_t prescaler;
    mr_uint32_t divider;
    mr_uint32_t ticks;
#if defined(STM32_WDT_HAS_EWI)
    mr_uint32_t ewit;
    mr_uint8_t event_enabled: 1;
#endif /* defined(STM32_WDT_HAS_EWI) */
    volatile mr_bool_t running;
} wdt_priv_t;

/* WDT reload definition */
#if defined(IWDG_RLR_RL)
#define WDT_RELOAD_MAX                  ((mr_uint64_t)IWDG_RLR_RL)
#else
#define WDT_RELOAD_MAX                  (4095ULL)
#endif /* defined(IWDG_RLR_RL) */

/* WDT event mask definition */
#define WDT_EVENT_MASK                  MR_WDT_WAIT_PRETIMEOUT

/* WDT prescaler map */
static const wdt_prescaler_t __wdt_prescaler[] = {
    {IWDG_PRESCALER_4, 4U},
    {IWDG_PRESCALER_8, 8U},
    {IWDG_PRESCALER_16, 16U},
    {IWDG_PRESCALER_32, 32U},
    {IWDG_PRESCALER_64, 64U},
    {IWDG_PRESCALER_128, 128U},
    {IWDG_PRESCALER_256, 256U},
#if defined(IWDG_PRESCALER_512)
    {IWDG_PRESCALER_512, 512U},
#endif /* defined(IWDG_PRESCALER_512) */
#if defined(IWDG_PRESCALER_1024)
    {IWDG_PRESCALER_1024, 1024U},
#endif /* defined(IWDG_PRESCALER_1024) */
};

static mr_uint64_t wdt_ticks(mr_uint64_t timeout, mr_uint32_t divider) {
    mr_uint64_t denominator, numerator, ticks;

    /* Calculate tick denominator */
    denominator = (mr_uint64_t)divider * 1000ULL;

    /* Calculate tick numerator */
    numerator = timeout * (mr_uint64_t)LSI_VALUE;

    /* Calculate watchdog ticks */
    ticks = numerator / denominator;
    if ((numerator % denominator) != 0ULL) {
        ticks++;
    }
    return ticks;
}

static mr_err_t wdt_quantize_timeout(mr_uint64_t timeout,
                                     mr_uint32_t *prescaler,
                                     mr_uint32_t *divider, mr_uint64_t *ticks) {
    mr_uint64_t value;
    mr_size_t i;

    /* Check parameter */
    if ((timeout == 0ULL) || ((mr_uint64_t)LSI_VALUE == 0ULL)
        || (timeout > (MR_UINT64_MAX / (mr_uint64_t)LSI_VALUE))) {
        return -MR_EINVAL;
    }

    /* Find watchdog prescaler */
    for (i = 0U; i < MR_ARRAY_SIZE(__wdt_prescaler); i++) {
        value = wdt_ticks(timeout, __wdt_prescaler[i].divider);
        if ((value != 0ULL) && (value <= (WDT_RELOAD_MAX + 1ULL))) {
            *prescaler = __wdt_prescaler[i].value;
            *divider = __wdt_prescaler[i].divider;
            *ticks = value;
            return 0;
        }
    }
    return -MR_EINVAL;
}

static mr_err_t wdt_configure(const mr_driver_t *drv,
                              const mr_wdt_config_t *config) {
    wdt_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t prescaler, divider;
#if defined(STM32_WDT_HAS_EWI)
    mr_uint64_t pre_ticks;
#endif /* defined(STM32_WDT_HAS_EWI) */
    mr_uint64_t ticks;
    mr_err_t ret;

    /* Check parameter */
    if (config->feed_min != 0ULL) {
        return -MR_EINVAL;
    }
#if !defined(STM32_WDT_HAS_EWI)
    if (config->pretimeout != 0ULL) {
        return -MR_ENOSYS;
    }
#endif /* !defined(STM32_WDT_HAS_EWI) */

    /* Quantize timeout */
    ret = wdt_quantize_timeout(config->feed_max, &prescaler, &divider, &ticks);
    if (ret != 0) {
        return ret;
    }

#if defined(STM32_WDT_HAS_EWI)
    /* Quantize pretimeout */
    pre_ticks = 0ULL;
    if (config->pretimeout != 0ULL) {
        if ((config->pretimeout >= config->feed_max)
            || (config->pretimeout
                > (MR_UINT64_MAX / (mr_uint64_t)LSI_VALUE))) {
            return -MR_EINVAL;
        }
        pre_ticks = wdt_ticks(config->pretimeout, divider);
        if ((pre_ticks < 2ULL) || (pre_ticks >= ticks)) {
            return -MR_EINVAL;
        }
    }
#endif /* defined(STM32_WDT_HAS_EWI) */

    /* Save WDT configuration */
    priv->prescaler = prescaler;
    priv->divider = divider;
    priv->ticks = (mr_uint32_t)ticks;
#if defined(STM32_WDT_HAS_EWI)
    priv->ewit = (pre_ticks != 0ULL) ? (mr_uint32_t)(pre_ticks - 1ULL) : 0U;
#endif /* defined(STM32_WDT_HAS_EWI) */
    return 0;
}

static mr_err_t wdt_configuration(const mr_driver_t *drv,
                                  mr_wdt_config_t *config) {
    wdt_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check WDT state */
    if (priv->ticks == 0U) {
        return -MR_ENODEV;
    }

    /* Get WDT configuration */
    config->feed_max = (mr_uint64_t)priv->ticks * priv->divider * 1000ULL
                     / (mr_uint64_t)LSI_VALUE;
    config->feed_min = 0ULL;
#if defined(STM32_WDT_HAS_EWI)
    config->pretimeout = (mr_uint64_t)(priv->ewit + (priv->ewit != 0U))
                       * priv->divider * 1000ULL / (mr_uint64_t)LSI_VALUE;
#else
    config->pretimeout = 0ULL;
#endif /* defined(STM32_WDT_HAS_EWI) */
    return 0;
}

static mr_err_t wdt_start(const mr_driver_t *drv) {
    wdt_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;

    /* Check WDT state */
    if ((priv->handle.Instance == MR_NULL) || (priv->ticks == 0U)) {
        return -MR_ENODEV;
    }
    if (priv->running) {
        return -MR_EBUSY;
    }

    /* Init WDT */
    priv->handle.Init.Prescaler = priv->prescaler;
    priv->handle.Init.Reload = priv->ticks - 1U;
#if defined(IWDG_WINDOW_DISABLE)
    priv->handle.Init.Window = IWDG_WINDOW_DISABLE;
#endif /* defined(IWDG_WINDOW_DISABLE) */
#if defined(STM32_WDT_HAS_EWI)
    priv->handle.Init.EWI = priv->ewit;
#endif /* defined(STM32_WDT_HAS_EWI) */
    ret = mr_stm32_hal_status(HAL_IWDG_Init(&priv->handle));
    if (ret != 0) {
        return ret;
    }

    /* Update WDT state */
    priv->driver = (mr_driver_t *)drv;
    priv->running = MR_TRUE;

#if defined(STM32_WDT_HAS_EWI)
    /* Restore WDT event state */
    if ((priv->ewit != 0U) && !priv->event_enabled) {
        WDT_WRITE_ENABLE(priv);
        CLEAR_BIT(priv->handle.Instance->EWCR, IWDG_EWCR_EWIE);
        WDT_WRITE_DISABLE(priv);
    }

    /* Init WDT IRQ */
    if (priv->ewit != 0U) {
        HAL_NVIC_SetPriority(priv->irqn, priv->irq_priority, 0U);
        HAL_NVIC_EnableIRQ(priv->irqn);
    }
#endif /* defined(STM32_WDT_HAS_EWI) */
    return 0;
}

static mr_err_t wdt_feed(const mr_driver_t *drv) {
    wdt_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check WDT state */
    if (!priv->running) {
        return -MR_ENODEV;
    }

    /* Feed WDT */
    return mr_stm32_hal_status(HAL_IWDG_Refresh(&priv->handle));
}

static mr_err_t wdt_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    wdt_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Get WDT state */
    *busy = priv->running;
    return 0;
}

static mr_err_t wdt_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable) {
#if defined(STM32_WDT_HAS_EWI)
    wdt_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check parameter */
    if ((events == 0U) || ((events & ~WDT_EVENT_MASK) != 0U)) {
        return -MR_EINVAL;
    }

    /* Check WDT state */
    if (priv->ewit == 0U) {
        return -MR_ENOSYS;
    }

    /* Update event state */
    priv->event_enabled = (enable != MR_FALSE);
    if (!priv->running) {
        return 0;
    }

    /* Control pretimeout interrupt */
    WDT_WRITE_ENABLE(priv);
    if (enable) {
        SET_BIT(priv->handle.Instance->EWCR, IWDG_EWCR_EWIE);
    } else {
        CLEAR_BIT(priv->handle.Instance->EWCR, IWDG_EWCR_EWIE);
    }
    WDT_WRITE_DISABLE(priv);
    return 0;
#else
    MR_IGNORE(drv);
    MR_IGNORE(events);
    MR_IGNORE(enable);
    return -MR_ENOSYS;
#endif /* defined(STM32_WDT_HAS_EWI) */
}

#if defined(STM32_WDT_HAS_EWI)
void HAL_IWDG_EarlyWakeupCallback(IWDG_HandleTypeDef *hiwdg) {
    wdt_priv_t *priv = MR_CONTAINER_OF(hiwdg, wdt_priv_t, handle);

    /* Raise WDT pretimeout event */
    if (priv->running && priv->event_enabled && (priv->driver != MR_NULL)) {
        mr_wdt_event_raise(priv->driver, MR_WDT_WAIT_PRETIMEOUT);
    }
}
#endif /* defined(STM32_WDT_HAS_EWI) */

/* Instance install */
#define ST_HT_WDT_INSTALL(_id, _ops)                                           \
    static wdt_priv_t MR_CAT(__wdt_priv_, _id) = {                             \
        .handle = {                                                            \
            .Instance = MR_HT_DRIVER_PROP(_id, st_iwdgx),                      \
        },                                                                     \
        __ST_HT_WDT_IRQ_INIT(_id)                                              \
    };                                                                         \
    __ST_HT_WDT_IRQ_INSTALL(_id)                                               \
    MR_HT_WDT_INSTALL(_id, &MR_CAT(__wdt_priv_, _id), _ops)
#if defined(STM32_WDT_HAS_EWI)
#define __ST_HT_WDT_IRQ_INIT(_id)                                              \
    .irqn = MR_HT_DRIVER_PROP(_id, st_irqn),                                   \
    .irq_priority = MR_HT_DRIVER_PROP(_id, st_irq_priority),
#define __ST_HT_WDT_IRQ_INSTALL(_id)                                           \
    void MR_HT_DRIVER_PROP(_id, st_irq_handler)(void) {                        \
        HAL_IWDG_IRQHandler(&MR_CAT(__wdt_priv_, _id).handle);                 \
    }
#else
#define __ST_HT_WDT_IRQ_INIT(_id)
#define __ST_HT_WDT_IRQ_INSTALL(_id)
#endif /* defined(STM32_WDT_HAS_EWI) */
MR_HT_DRIVER_FOREACH(ST_HT_WDT_INSTALL, &__wdt_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_WDT) */

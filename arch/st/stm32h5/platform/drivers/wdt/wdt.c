/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-20        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_WDT)
#include <mrX/drivers/wdt/mr_wdt.h>
#include "stm32h5xx_hal_iwdg.h"

/* HardTree compatible */
#define MR_HT_COMPAT                    st_stm32h5_wdt
#if MR_HT_HAS_DRIVER()

/* WDT operations */
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

typedef struct wdt_priv {
    IWDG_HandleTypeDef handle;
    void (*clk_enable)(void);
    IRQn_Type irqn;
    mr_uint32_t irq_priority;
    mr_driver_t *drv;
    mr_uint32_t prescaler;
    mr_uint32_t ticks;
    mr_uint32_t ewit;
    mr_atomic_t running;
    mr_bool_t configured;
} wdt_priv_t;

#define WDT_LSI_KHZ                     (32ULL)
#define WDT_PRESCALER_MAX               (1024U)
#define WDT_RELOAD_MAX                  (4095U)
#define WDT_TIMEOUT_MAX                                                        \
    (4096ULL * WDT_PRESCALER_MAX * 1000ULL / (WDT_LSI_KHZ * 1000ULL))
#define WDT_TICKS(_ms, _p)                                                     \
    (((_ms) * WDT_LSI_KHZ + (_p) - 1ULL) / (_p))
#define WDT_EVENT_MASK                  (MR_WDT_WAIT_PRETIMEOUT)

static mr_err_t wdt_quantize_timeout(mr_uint64_t timeout,
                                     mr_uint32_t *prescaler,
                                     mr_uint64_t *ticks) {
    mr_uint64_t divider = 4ULL;
    mr_uint32_t reg = 0U;

    /* Check parameter */
    if ((timeout == 0ULL) || (timeout > WDT_TIMEOUT_MAX)) {
        return -MR_EINVAL;
    }

    /* Pick the smallest prescaler that keeps reload in range */
    while (WDT_TICKS(timeout, divider) > WDT_RELOAD_MAX) {
        divider *= 2ULL;
        reg++;
        if (divider > WDT_PRESCALER_MAX) {
            return -MR_EINVAL;
        }
    }
    *prescaler = reg;
    *ticks = WDT_TICKS(timeout, divider);
    return 0;
}

static mr_err_t wdt_configure(const mr_driver_t *drv,
                              const mr_wdt_config_t *config) {
    wdt_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint64_t ticks, pre_ticks;
    mr_uint32_t prescaler;
    mr_err_t ret;

    /* Window is disabled: feeding is allowed at any time */
    if (config->feed_min != 0ULL) {
        return -MR_EINVAL;
    }
    ret = wdt_quantize_timeout(config->feed_max, &prescaler, &ticks);
    if (ret != 0) {
        return ret;
    }

    /* EWI must sit strictly inside the timeout and leave one reset tick */
    pre_ticks = 0ULL;
    if (config->pretimeout != 0ULL) {
        pre_ticks = WDT_TICKS(config->pretimeout, (4ULL << prescaler));
        if ((pre_ticks < 2ULL) || (pre_ticks >= ticks)) {
            return -MR_EINVAL;
        }
    }

    priv->prescaler = prescaler;
    priv->ticks = (mr_uint32_t)ticks;
    priv->ewit = (pre_ticks != 0ULL) ? (mr_uint32_t)(pre_ticks - 1ULL) : 0U;
    priv->configured = MR_TRUE;
    return 0;
}

static mr_err_t wdt_configuration(const mr_driver_t *drv,
                                  mr_wdt_config_t *config) {
    wdt_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check parameter */
    if (!priv->configured) {
        return -MR_EINVAL;
    }

    /* Report the quantized values actually used by the hardware */
    config->feed_min = 0ULL;
    config->feed_max = (mr_uint64_t)priv->ticks * (4ULL << priv->prescaler)
                       / WDT_LSI_KHZ;
    if (priv->ewit != 0U) {
        config->pretimeout = (mr_uint64_t)(priv->ewit + 1U)
                             * (4ULL << priv->prescaler) / WDT_LSI_KHZ;
    } else {
        config->pretimeout = 0ULL;
    }
    return 0;
}

static mr_err_t wdt_start(const mr_driver_t *drv) {
    wdt_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;

    /* Check parameter */
    if (!priv->configured) {
        return -MR_EINVAL;
    }
    if (priv->clk_enable == MR_NULL) {
        return -MR_ENODEV;
    }

    /* LSI is forced on by hardware once the IWDG starts */
    priv->clk_enable();
    priv->handle.Init.Prescaler = priv->prescaler;
    priv->handle.Init.Reload = priv->ticks - 1U;
    priv->handle.Init.Window = IWDG_WINDOW_DISABLE;
    priv->handle.Init.EWI = priv->ewit;
    ret = mr_stm32h5_hal_status(HAL_IWDG_Init(&priv->handle));
    if (ret != 0) {
        return ret;
    }
    if (priv->ewit != 0U) {
        HAL_NVIC_SetPriority(priv->irqn, priv->irq_priority, 0U);
        HAL_NVIC_EnableIRQ(priv->irqn);
    }
    priv->drv = (mr_driver_t *)drv;
    mr_atomic_store(&priv->running, 1);
    return 0;
}

static mr_err_t wdt_feed(const mr_driver_t *drv) {
    wdt_priv_t *priv = MR_DRIVER_PRIV(drv);

    return mr_stm32h5_hal_status(HAL_IWDG_Refresh(&priv->handle));
}

static mr_err_t wdt_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    wdt_priv_t *priv = MR_DRIVER_PRIV(drv);

    *busy = (mr_atomic_load(&priv->running) != 0);
    return 0;
}

static mr_err_t wdt_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable) {
    wdt_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* IWDG timeout resets the chip; only PRETIMEOUT is controllable */
    if ((events & ~WDT_EVENT_MASK) != 0U) {
        return -MR_EINVAL;
    }
    if (priv->ewit == 0U) {
        return -MR_ENOSYS;
    }

    IWDG_ENABLE_WRITE_ACCESS(&priv->handle);
    if (enable) {
        priv->handle.Instance->EWCR |= IWDG_EWCR_EWIE;
    } else {
        priv->handle.Instance->EWCR &= ~IWDG_EWCR_EWIE;
    }
    IWDG_DISABLE_WRITE_ACCESS(&priv->handle);
    return 0;
}

void HAL_IWDG_EarlyWakeupCallback(IWDG_HandleTypeDef *hiwdg) {
    wdt_priv_t *priv = MR_CONTAINER_OF(hiwdg, wdt_priv_t, handle);

    if ((mr_atomic_load(&priv->running) != 0) && (priv->drv != MR_NULL)) {
        (void)mr_wdt_event_raise(priv->drv, MR_WDT_WAIT_PRETIMEOUT);
    }
}

#define ST_HT_WDT_INSTALL(_id, _ops)                                           \
    static void MR_CAT(__wdt_clk_enable_, _id)(void) {                         \
        if (!__HAL_RCC_RTC_IS_CLK_ENABLED()) {                                 \
            __HAL_RCC_RTC_CLK_ENABLE();                                        \
        }                                                                      \
    }                                                                          \
    static wdt_priv_t MR_CAT(__wdt_priv_, _id)                                 \
        = {.handle.Instance = MR_HT_DRIVER_PROP(_id, st_iwdgx),                \
           .clk_enable = MR_CAT(__wdt_clk_enable_, _id),                       \
           .irqn = MR_HT_DRIVER_PROP(_id, st_irqn),                            \
           .irq_priority = MR_HT_DRIVER_PROP(_id, st_irq_priority)};           \
    void MR_HT_DRIVER_PROP(_id, st_irq_handler)(void) {                        \
        HAL_IWDG_IRQHandler(&MR_CAT(__wdt_priv_, _id).handle);                 \
    }                                                                          \
    MR_HT_WDT_INSTALL(_id, &MR_CAT(__wdt_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(ST_HT_WDT_INSTALL, &__wdt_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_WDT) */

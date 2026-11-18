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
#include <mrX/port/mr_port_irq.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    wch_ch32_wwdg
#if MR_HT_HAS_DRIVER()

/* Driver WDT operations */
static mr_err_t wwdg_configure(const mr_driver_t *drv,
                               const mr_wdt_config_t *config);
static mr_err_t wwdg_configuration(const mr_driver_t *drv,
                                   mr_wdt_config_t *config);
static mr_err_t wwdg_start(const mr_driver_t *drv);
static mr_err_t wwdg_feed(const mr_driver_t *drv);
static mr_err_t wwdg_is_busy(const mr_driver_t *drv, mr_bool_t *busy);
static mr_err_t wwdg_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                   mr_bool_t enable);
MR_UNUSED static const mr_wdt_ops_t __wwdg_ops = {
    .configure = wwdg_configure,
    .configuration = wwdg_configuration,
    .start = wwdg_start,
    .stop = MR_NULL,
    .feed = wwdg_feed,
    .is_busy = wwdg_is_busy,
    .event_control = wwdg_event_control,
};

/* Window watchdog register definition */
#define WWDG_START_BIT                  (1U << 7)
#define WWDG_EWI_BIT                    (1U << 9)
#define WWDG_COUNTER_MASK               (0x7fU)
#define WWDG_LAST_COUNT                 (0x40U)

/* Window watchdog timing definition */
#define WWDG_MAX_TICKS                  (64U)
#define WWDG_PRESCALERS                 (4U)
#define WWDG_BASE_DIV                   (4096U)
#define WWDG_MS_PER_SECOND              (1000ULL)

/* Window watchdog private data type */
typedef struct wwdg_priv {
    void (*clk_enable)(void);
    mr_wdt_config_t config;
    mr_uint8_t counter, window, prescaler, irq_priority;
    mr_bool_t has_irq, events, running;
} wwdg_priv_t;

static mr_uint32_t wwdg_clock(void) {
#if defined(MR_USE_CH32V10X) || defined(MR_USE_CH32V20X)                       \
    || defined(MR_USE_CH32V205) || defined(MR_USE_CH32V30X)                    \
    || defined(MR_USE_CH32V4X7)
    RCC_ClocksTypeDef clocks;

    /* Get APB1 clock */
    RCC_GetClocksFreq(&clocks);
    return clocks.PCLK1_Frequency;
#else
    return SystemCoreClock;
#endif /* CH32 EVT SDK family */
}

static mr_err_t wwdg_configure(const mr_driver_t *drv,
                               const mr_wdt_config_t *config) {
    wwdg_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint64_t unit, elapsed, ticks, warning, feed_min, feed_max;
    mr_uint32_t hz, prescaler;
    mr_uint8_t counter, window;
    int mask;

    /* Check parameter */
    hz = wwdg_clock();
    unit = WWDG_BASE_DIV * WWDG_MS_PER_SECOND;
    if ((hz == 0U) || (config->feed_max == 0U)
        || (config->feed_min >= config->feed_max)
        || (config->feed_max > (unit * 8U * WWDG_MAX_TICKS) / hz)) {
        return -MR_EINVAL;
    }
    if ((config->pretimeout != 0U) && !priv->has_irq) {
        return -MR_ENOSYS;
    }

    /* Quantize to safe whole-millisecond feed and warning limits */
    for (prescaler = 0U; prescaler < WWDG_PRESCALERS; prescaler++, unit *= 2U) {
        ticks = (config->feed_max * hz + unit - 1U) / unit;
        elapsed = (config->feed_min * hz + unit - 1U) / unit;
        /* The maximum reload value needs at least one tick before feeding */
        if ((ticks == WWDG_MAX_TICKS) && (elapsed == 0U)) {
            elapsed = 1U;
        }
        warning = unit / hz;
        feed_min = (elapsed * unit + hz - 1U) / hz;
        /* Feeding at the reset deadline itself is too late */
        feed_max = (ticks * unit - 1U) / hz;
        if ((ticks <= WWDG_MAX_TICKS) && (elapsed < ticks)
            && (feed_min <= feed_max)
            && ((config->pretimeout == 0U)
                || ((ticks > 1U) && (config->pretimeout <= warning)))) {
            break;
        }
    }
    if (prescaler == WWDG_PRESCALERS) {
        return -MR_ERANGE;
    }

    /* Calculate counter and window */
    counter = (mr_uint8_t)(WWDG_LAST_COUNT - 1U + ticks);
    window = (mr_uint8_t)(counter + 1U - elapsed);

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check WDT state */
    if (priv->running) {
        mr_port_irq_enable(mask);
        return -MR_EBUSY;
    }

    /* Save WDT configuration */
    priv->config.feed_min = feed_min;
    priv->config.feed_max = feed_max;
    priv->config.pretimeout = (config->pretimeout != 0U) ? warning : 0U;
    priv->counter = counter;
    priv->window = window;
    priv->prescaler = (mr_uint8_t)prescaler;

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t wwdg_configuration(const mr_driver_t *drv,
                                   mr_wdt_config_t *config) {
    const wwdg_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check WDT state */
    if (priv->counter == 0U) {
        mr_port_irq_enable(mask);
        return -MR_ENODEV;
    }

    /* Get WDT configuration */
    *config = priv->config;

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t wwdg_start(const mr_driver_t *drv) {
    wwdg_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t cfgr;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check WDT state */
    if (priv->running || (priv->counter == 0U)) {
        mr_port_irq_enable(mask);
        return priv->running ? -MR_EBUSY : -MR_ENODEV;
    }

    /* Enable WWDG clock */
    priv->clk_enable();

    /* Start WDT */
    cfgr = ((mr_uint32_t)priv->prescaler << 7) | priv->window;
    if (priv->config.pretimeout != 0U) {
        cfgr |= WWDG_EWI_BIT;
    }
    WWDG->CFGR = cfgr;
    WWDG->STATR = 0U;
    priv->running = MR_TRUE;
    WWDG->CTLR = WWDG_START_BIT | priv->counter;

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t wwdg_feed(const mr_driver_t *drv) {
    const wwdg_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t current;
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check feed window */
    current = WWDG->CTLR & WWDG_COUNTER_MASK;
    if (current < WWDG_LAST_COUNT) {
        ret = -MR_ETIMEDOUT;
    } else if (current >= priv->window) {
        ret = -MR_EAGAIN;
    } else {
        /* Feed WDT */
        WWDG->CTLR = WWDG_START_BIT | priv->counter;
        ret = 0;
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

static mr_err_t wwdg_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    const wwdg_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Get WDT state */
    mask = mr_port_irq_disable();
    *busy = priv->running;
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t wwdg_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                    mr_bool_t enable) {
    wwdg_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Check parameter */
    if ((events != MR_WDT_WAIT_PRETIMEOUT) || !priv->has_irq) {
        return -MR_ENOSYS;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check pretimeout */
    if (enable && (priv->config.pretimeout == 0U)) {
        mr_port_irq_enable(mask);
        return -MR_EINVAL;
    }

    /* Update event state */
    priv->events = enable;
    /* EWI is sticky until reset, mask delivery at the interrupt controller */
    if (enable) {
        NVIC_SetPriority(WWDG_IRQn, priv->irq_priority);
        NVIC_EnableIRQ(WWDG_IRQn);
    } else {
        NVIC_DisableIRQ(WWDG_IRQn);
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

MR_UNUSED static void wwdg_irq(mr_driver_t *drv) {
    const mr_wdt_data_t *data = MR_DRIVER_DATA(drv);
    const wwdg_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Handle pretimeout event */
    if (priv->running && (WWDG->STATR != 0U)) {
        WWDG->STATR = 0U;
        if (priv->events && mr_driver_is_ready(drv)
            && MR_OBJECT_IS_INITED(&data->event)) {
            /* Raise WDT event */
            mr_wdt_event_raise(drv, MR_WDT_WAIT_PRETIMEOUT);
        }
    }

    /* Unlock */
    mr_port_irq_enable(mask);
}

/* Instance install */
#define CH32_HT_WWDG_INSTALL(_id, _ops)                                        \
    extern void MR_HT_DRIVER_PROP(_id, wch_clken)(void);                       \
    static wwdg_priv_t MR_CAT(__wwdg_priv_, _id) = {                           \
        .clk_enable = MR_HT_DRIVER_PROP(_id, wch_clken),                       \
        .irq_priority = MR_HT_DRIVER_PROP_OR(_id, wch_irq_priority, 0U),       \
        .has_irq = MR_HT_DRIVER_HAS_PROP(_id, wch_pretimeout_irq),             \
    };                                                                         \
    __CH32_HT_WWDG_IRQ(_id)                                                    \
    MR_HT_WDT_INSTALL(_id, &MR_CAT(__wwdg_priv_, _id), _ops)
#define __CH32_HT_WWDG_IRQ(_id)                                                \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, wch_pretimeout_irq),                \
                (MR_CH32_INTERRUPT void WWDG_IRQHandler(void) {                \
                     wwdg_irq(MR_HT_DRIVER_FIND(MR_HT_DRIVER(_id)));           \
                 }),                                                           \
                ())
MR_HT_DRIVER_FOREACH(CH32_HT_WWDG_INSTALL, &__wwdg_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_WDT) */

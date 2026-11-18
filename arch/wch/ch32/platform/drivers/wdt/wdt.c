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
#include <mrX/port/mr_port_clock.h>
#include <mrX/port/mr_port_irq.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    wch_ch32_iwdg
#if MR_HT_HAS_DRIVER()

/* Driver WDT operations */
static mr_err_t wdt_configure(const mr_driver_t *drv,
                              const mr_wdt_config_t *config);
static mr_err_t wdt_configuration(const mr_driver_t *drv,
                                  mr_wdt_config_t *config);
static mr_err_t wdt_start(const mr_driver_t *drv);
static mr_err_t wdt_feed(const mr_driver_t *drv);
static mr_err_t wdt_is_busy(const mr_driver_t *drv, mr_bool_t *busy);
MR_UNUSED static const mr_wdt_ops_t __wdt_ops = {
    .configure = wdt_configure,
    .configuration = wdt_configuration,
    .start = wdt_start,
    .stop = MR_NULL,
    .feed = wdt_feed,
    .is_busy = wdt_is_busy,
    .event_control = MR_NULL,
};

/* IWDG clock frequency definition */
#if defined(MR_USE_CH32V00X) || defined(MR_USE_CH32VM00X)
#define WDT_LSI_KHZ                     (128U)
#else
#define WDT_LSI_KHZ                     (40U)
#endif /* defined(MR_USE_CH32V00X) || defined(MR_USE_CH32VM00X) */

/* IWDG counter limit definition */
#define WDT_MAX_TICKS                   (4096U)
#define WDT_MAX_DIVIDER                 (256U)

/* IWDG control key definition */
#define WDT_KEY_UNLOCK                  (0x5555U)
#define WDT_KEY_START                   (0xCCCCU)
#define WDT_KEY_RELOAD                  (0xAAAAU)

/* IWDG synchronization limit definition */
#define WDT_SYNC_ATTEMPTS               (100U)

/* IWDG software start option definition */
#define WDT_OPTION_SOFTWARE             (1U << 2)

/* WDT private data type */
typedef struct wdt_priv {
    mr_uint16_t ticks;
    mr_uint8_t prescaler;
    mr_bool_t running;
} wdt_priv_t;

static mr_err_t wdt_configure(const mr_driver_t *drv,
                              const mr_wdt_config_t *config) {
    wdt_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t divider, cycles, ticks;
    mr_uint8_t prescaler;
    int mask;

    /* Check parameter */
    if ((config->feed_min != 0U) || (config->pretimeout != 0U)) {
        return -MR_ENOSYS;
    }
    if ((config->feed_max == 0U)
        || (config->feed_max > WDT_MAX_TICKS * WDT_MAX_DIVIDER / WDT_LSI_KHZ)) {
        return -MR_EINVAL;
    }

    /* Round up using the finest divider that fits the reload register */
    cycles = (mr_uint32_t)config->feed_max * WDT_LSI_KHZ;
    for (prescaler = 0U, divider = 4U;; prescaler++, divider *= 2U) {
        ticks = (cycles + divider - 1U) / divider;
        if (ticks <= WDT_MAX_TICKS) {
            break;
        }
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check WDT state */
    if (priv->running) {
        mr_port_irq_enable(mask);
        return -MR_EBUSY;
    }

    /* Save WDT configuration */
    priv->ticks = (mr_uint16_t)ticks;
    priv->prescaler = prescaler;

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t wdt_configuration(const mr_driver_t *drv,
                                  mr_wdt_config_t *config) {
    wdt_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t cycles;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Get WDT configuration */
    cycles = (mr_uint32_t)priv->ticks * (4U << priv->prescaler);
    mr_port_irq_enable(mask);
    if (cycles == 0U) {
        return -MR_EINVAL;
    }
    config->feed_min = 0U;
    config->feed_max = cycles / WDT_LSI_KHZ;
    config->pretimeout = 0U;
    return 0;
}

#if !defined(MR_USE_CH32V30X)
static mr_err_t wdt_wait_sync(void) {
    mr_uint32_t attempt;

    /* Wait synchronization */
    for (attempt = 0U; attempt < WDT_SYNC_ATTEMPTS; attempt++) {
        if (((RCC->RSTSCKR & RCC_LSIRDY) != 0U)
            && ((IWDG->STATR & (IWDG_PVU | IWDG_RVU)) == 0U)) {
            return 0;
        }
        mr_port_clock_busy_wait(10U);
    }
    return -MR_ETIMEDOUT;
}
#endif /* !defined(MR_USE_CH32V30X) */

static mr_err_t wdt_start(const mr_driver_t *drv) {
    wdt_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check WDT state */
    if (priv->running || (priv->ticks == 0U)) {
        ret = priv->running ? -MR_EBUSY : -MR_EINVAL;
        mr_port_irq_enable(mask);
        return ret;
    }

#if defined(MR_USE_CH32V30X)
    /* Use the V30x EVT sequence. Starting the watchdog also starts LSI.
     * Polling LSIRDY and the update flags before the start key can time out.
     */
    IWDG->CTLR = WDT_KEY_UNLOCK;
    IWDG->PSCR = priv->prescaler;
    IWDG->RLDR = priv->ticks - 1U;
    IWDG->CTLR = WDT_KEY_RELOAD;
    IWDG->CTLR = WDT_KEY_START;
    priv->running = MR_TRUE;
    ret = 0;
#else
    /* Start LSI */
    RCC->RSTSCKR |= RCC_LSION;
    ret = wdt_wait_sync();
    if (ret == 0) {
        /* Init IWDG */
        IWDG->CTLR = WDT_KEY_UNLOCK;
        IWDG->PSCR = priv->prescaler;
        IWDG->RLDR = priv->ticks - 1U;
        /* Synchronize the reload before starting the irreversible countdown */
        ret = wdt_wait_sync();
        if (ret == 0) {
            /* Start IWDG */
            IWDG->CTLR = WDT_KEY_START;
            IWDG->CTLR = WDT_KEY_RELOAD;
            priv->running = MR_TRUE;
        } else {
            IWDG->CTLR = 0U;
        }
    }
#endif /* defined(MR_USE_CH32V30X) */

    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

static mr_err_t wdt_feed(const mr_driver_t *drv) {
    MR_IGNORE(drv);

    /* Feed WDT */
    IWDG->CTLR = WDT_KEY_RELOAD;
    return 0;
}

static mr_err_t wdt_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    wdt_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Get WDT state */
    mask = mr_port_irq_disable();
    *busy = priv->running || ((FLASH->OBR & WDT_OPTION_SOFTWARE) == 0U);
    mr_port_irq_enable(mask);
    return 0;
}

/* Instance install */
#define CH32_HT_WDT_INSTALL(_id, _ops)                                         \
    static wdt_priv_t MR_CAT(__wdt_priv_, _id);                                \
    MR_HT_WDT_INSTALL(_id, &MR_CAT(__wdt_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(CH32_HT_WDT_INSTALL, &__wdt_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_WDT) */

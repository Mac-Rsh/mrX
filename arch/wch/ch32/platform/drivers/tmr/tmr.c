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
#include <mrX/port/mr_port_irq.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    wch_ch32_tmr
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

/* TMR IRQ definition */
#define TMR_NO_IRQ                      (-1)

#if defined(MR_USE_CH32VM00X) && defined(TIM3)
/* TIM3 clock mode definition */
#define TMR_TIM3_CLOCK_TIM1             (7U << 8)
#endif /* defined(MR_USE_CH32VM00X) && defined(TIM3) */

/* TMR private data type */
typedef struct tmr_priv {
    TIM_TypeDef *tim;
    void (*clk_enable)(void);
    mr_uint32_t tim3_input_hz, tim3_dma_compare;
    mr_uint32_t tim3_cc1_ticks, tim3_cc2_ticks;
    int irqn;
    mr_uint8_t irq_priority;
    mr_tmr_config_t config;
    mr_bool_t configured, signal, events;
} tmr_priv_t;

static mr_err_t tmr_make_period(const tmr_priv_t *priv, mr_uint64_t period,
                                mr_ch32_timer_base_t *base) {
    mr_uint64_t max_counts;
    mr_uint32_t hz;

    /* Include the terminal count at ARR = 0xffff or 0xffffffff */
    max_counts = (mr_uint64_t)MR_UINT16_MAX + 1U;
#if defined(MR_CH32_HAS_TIM5_32BIT)
    if (priv->tim == TIM5) {
        max_counts = (mr_uint64_t)MR_UINT32_MAX + 1U;
    }
#elif defined(MR_USE_CH32V205)
    if (priv->tim == TIM4) {
        max_counts = (mr_uint64_t)MR_UINT32_MAX + 1U;
    }
#endif /* CH32 timer with 32-bit reload */
    hz = (priv->tim3_input_hz != 0U) ? priv->tim3_input_hz
                                    : mr_ch32_timer_clock(priv->tim);
    return mr_ch32_timer_make_period(hz, period, max_counts, 1U, base);
}

static mr_bool_t tmr_is_streamlined(const tmr_priv_t *priv) {
#if defined(MR_USE_CH32VM00X) && defined(TIM3)
    return priv->tim == TIM3;
#else
    MR_IGNORE(priv);
    return MR_FALSE;
#endif /* defined(MR_USE_CH32VM00X) && defined(TIM3) */
}

static mr_bool_t tmr_running(const tmr_priv_t *priv) {
    /* The SDK overlays TIM3_CTLR on CTLR1 and both use bit 0 for CEN */
    return (priv->tim->CTLR1 & TIM_CEN) != 0U;
}

#if defined(MR_USE_CH32VM00X) && defined(TIM3)
static void tmr_tim3_dma(const tmr_priv_t *priv, mr_bool_t enable) {
    mr_uint16_t requests;

    /* Only the selected compare output drives the DMA request */
    requests = 0U;
    if (enable) {
        requests = (priv->tim3_dma_compare == 3U) ? SLTM_CC3DE : SLTM_CC4DE;
    }
    priv->tim->TIM3_DMAINTENR = requests;
}
#endif /* defined(MR_USE_CH32VM00X) && defined(TIM3) */

static void tmr_program(const tmr_priv_t *priv, const mr_tmr_config_t *config,
                        const mr_ch32_timer_base_t *base) {
#if defined(MR_USE_CH32VM00X) && defined(TIM3)
    /* Program the streamlined timer */
    if (tmr_is_streamlined(priv)) {
        priv->tim->TIM3_CTLR = 0U;
        priv->tim->TIM3_DMAINTENR = 0U;
        priv->tim->TIM3_ATRLR = (mr_uint16_t)(base->counts - 1U);
        priv->tim->TIM3_CNT = 0U;
        /* Set the optional ADC injection trigger positions */
        if (priv->tim3_cc1_ticks != 0U) {
            priv->tim->TIM3_CH1CVR = (mr_uint16_t)priv->tim3_cc1_ticks;
        }
        if (priv->tim3_cc2_ticks != 0U) {
            priv->tim->TIM3_CH2CVR = (mr_uint16_t)priv->tim3_cc2_ticks;
        }
        /* Match at the terminal count, once per up-counting period */
        if (priv->tim3_dma_compare == 3U) {
            priv->tim->TIM3_CH3CVR = (mr_uint16_t)(base->counts - 1U);
        } else if (priv->tim3_dma_compare == 4U) {
            priv->tim->TIM3_CH4CVR = (mr_uint16_t)(base->counts - 1U);
        }
        if (priv->tim3_input_hz != 0U) {
            /* SMS = 111 routes TIM1 TRGO to the unprescaled TIM3 counter */
            priv->tim->TIM3_CTLR = TMR_TIM3_CLOCK_TIM1;
        }
        return;
    }
#endif /* defined(MR_USE_CH32VM00X) && defined(TIM3) */

    /* Program the timer base */
    priv->tim->DMAINTENR = 0U;
    /* Select counter enable as TRGO while issuing the software update */
    priv->tim->CTLR2 = TIM_MMS_0;
    if (!mr_ch32_timer_is_basic(priv->tim)) {
        priv->tim->SMCFGR = 0U;
        priv->tim->CCER = 0U;
    }
    if (mr_ch32_timer_is_advanced(priv->tim)) {
        priv->tim->RPTCR = 0U;
    }
    priv->tim->CTLR1 = (config->mode == MR_TMR_MODE_ONESHOT) ? TIM_OPM : 0U;
    priv->tim->PSC = base->divider - 1U;
    mr_ch32_timer_set_reload(priv->tim, (mr_uint32_t)(base->counts - 1U));
    priv->tim->SWEVGR = TIM_UG;
    priv->tim->INTFR = 0U;
}

static mr_err_t tmr_configure(const mr_driver_t *drv,
                              const mr_tmr_config_t *config) {
    tmr_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_ch32_timer_base_t base;
    mr_err_t ret;
    int mask;

    /* Check TMR resources */
    if (priv->tim == MR_NULL) {
        return -MR_EINVAL;
    }

    /* Check streamlined timer */
    if (!tmr_is_streamlined(priv)
        && ((priv->tim3_input_hz != 0U) || (priv->tim3_dma_compare != 0U)
            || (priv->tim3_cc1_ticks != 0U) || (priv->tim3_cc2_ticks != 0U))) {
        return -MR_ENOSYS;
    }
    if ((priv->tim3_dma_compare != 0U) && (priv->tim3_dma_compare != 3U)
        && (priv->tim3_dma_compare != 4U)) {
        return -MR_EINVAL;
    }
    if (tmr_is_streamlined(priv)
        && ((config->mode != MR_TMR_MODE_PERIODIC)
            || mr_ch32_irq_valid(priv->irqn))) {
        return -MR_ENOSYS;
    }

    /* Calculate TMR period */
    ret = tmr_make_period(priv, config->period, &base);
    if (ret != 0) {
        return ret;
    }
    /* TIM3 has no prescaler and stops when its auto-reload value is zero */
    if (tmr_is_streamlined(priv)
        && ((base.divider != 1U) || (base.counts < 2U)
            || (priv->tim3_cc1_ticks >= base.counts)
            || (priv->tim3_cc2_ticks >= base.counts))) {
        return -MR_ERANGE;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check TMR state */
    if (priv->configured && (priv->signal || tmr_running(priv))) {
        mr_port_irq_enable(mask);
        return -MR_EBUSY;
    }

    /* Claim the timer */
    ret = mr_ch32_timer_claim(priv->tim, drv);
    if (ret != 0) {
        mr_port_irq_enable(mask);
        return ret;
    }

    /* Init TMR */
    priv->clk_enable();
    tmr_program(priv, config, &base);

    /* Save TMR configuration */
    priv->config = *config;
    priv->config.period = base.period;
    priv->configured = MR_TRUE;

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t tmr_configuration(const mr_driver_t *drv,
                                  mr_tmr_config_t *config) {
    tmr_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check TMR state */
    if (!priv->configured) {
        mr_port_irq_enable(mask);
        return -MR_ENODEV;
    }

    /* Get TMR configuration */
    *config = priv->config;

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t tmr_start(const mr_driver_t *drv) {
    tmr_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check TMR state */
    if (!priv->configured) {
        mr_port_irq_enable(mask);
        return -MR_ENODEV;
    }
    if (tmr_running(priv)) {
        mr_port_irq_enable(mask);
        return -MR_EBUSY;
    }

#if defined(MR_USE_CH32VM00X) && defined(TIM3)
    /* Start streamlined timer */
    if (tmr_is_streamlined(priv)) {
        priv->tim->TIM3_CNT = 0U;
        tmr_tim3_dma(priv, priv->signal);
        priv->tim->TIM3_CTLR |= SLTM_CEN;
        mr_port_irq_enable(mask);
        return 0;
    }
#endif /* defined(MR_USE_CH32VM00X) && defined(TIM3) */

    /* Reload CNT and its prescaler without propagating a software trigger */
    priv->tim->CTLR2 = TIM_MMS_0;
    priv->tim->SWEVGR = TIM_UG;
    priv->tim->INTFR = (mr_uint16_t)~TIM_UIF;
    /* With the signal off, RESET leaves the output low until a software UG */
    priv->tim->CTLR2 = priv->signal ? TIM_MMS_1 : 0U;
    if (priv->events) {
        NVIC_ClearPendingIRQ((IRQn_Type)priv->irqn);
        priv->tim->DMAINTENR |= TIM_UIE;
    }

    /* Start TMR */
    priv->tim->CTLR1 |= TIM_CEN;

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t tmr_stop(const mr_driver_t *drv) {
    tmr_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

#if defined(MR_USE_CH32VM00X) && defined(TIM3)
    /* Stop streamlined timer */
    if (tmr_is_streamlined(priv)) {
        if (priv->configured) {
            tmr_tim3_dma(priv, MR_FALSE);
            priv->tim->TIM3_CTLR &= ~SLTM_CEN;
        }
        mr_port_irq_enable(mask);
        return 0;
    }
#endif /* defined(MR_USE_CH32VM00X) && defined(TIM3) */

    /* Stop TMR */
    if (priv->configured) {
        priv->tim->DMAINTENR &= ~TIM_UIE;
        priv->tim->CTLR1 &= ~TIM_CEN;
        priv->tim->INTFR = (mr_uint16_t)~TIM_UIF;
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t tmr_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    tmr_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Get TMR state */
    mask = mr_port_irq_disable();
    *busy = priv->configured && tmr_running(priv);
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t tmr_signal_control(const mr_driver_t *drv,
                                  mr_tmr_signal_t signal, mr_bool_t enable) {
    tmr_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Check parameter */
    if (signal != MR_TMR_SIGNAL_PERIOD) {
        return -MR_EINVAL;
    }

    /* Check TMR capability */
    if (tmr_is_streamlined(priv) && (priv->tim3_dma_compare == 0U)) {
        return -MR_ENOSYS;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check TMR state */
    if (!priv->configured) {
        mr_port_irq_enable(mask);
        return -MR_ENODEV;
    }

    /* Configure TMR signal */
#if defined(MR_USE_CH32VM00X) && defined(TIM3)
    if (tmr_is_streamlined(priv)) {
        /* Arm the request only while the timer is counting */
        tmr_tim3_dma(priv, enable && tmr_running(priv));
        priv->signal = enable;
        mr_port_irq_enable(mask);
        return 0;
    }
#endif /* defined(MR_USE_CH32VM00X) && defined(TIM3) */
    priv->tim->CTLR2 = enable ? TIM_MMS_1 : 0U;
    priv->signal = enable;

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t tmr_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                 mr_bool_t enable) {
    tmr_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Check parameter */
    if (events != MR_TMR_WAIT_TIMEOUT) {
        return -MR_EINVAL;
    }

    /* Check TMR capability */
    if ((priv->tim == MR_NULL) || tmr_is_streamlined(priv)
        || !mr_ch32_irq_valid(priv->irqn)) {
        return -MR_ENOSYS;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Update event state */
    if (priv->events == enable) {
        mr_port_irq_enable(mask);
        return 0;
    }
    priv->events = enable;
    if (enable) {
        NVIC_SetPriority((IRQn_Type)priv->irqn, priv->irq_priority);
        NVIC_ClearPendingIRQ((IRQn_Type)priv->irqn);
        if (priv->configured && tmr_running(priv)) {
            priv->tim->INTFR = (mr_uint16_t)~TIM_UIF;
            priv->tim->DMAINTENR |= TIM_UIE;
        }
        NVIC_EnableIRQ((IRQn_Type)priv->irqn);
    } else {
        if (priv->configured) {
            priv->tim->DMAINTENR &= ~TIM_UIE;
        }
        NVIC_DisableIRQ((IRQn_Type)priv->irqn);
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

MR_UNUSED static void tmr_irq(mr_driver_t *drv) {
    tmr_priv_t *priv = MR_DRIVER_PRIV(drv);
    const mr_tmr_data_t *data;
    int mask;

    /* Streamlined timers have no update IRQ */
    if (tmr_is_streamlined(priv)) {
        return;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Handle update event */
    if (priv->configured && ((priv->tim->INTFR & TIM_UIF) != 0U)
        && ((priv->tim->DMAINTENR & TIM_UIE) != 0U)) {
        priv->tim->INTFR = (mr_uint16_t)~TIM_UIF;
        data = MR_DRIVER_DATA(drv);
        if (!mr_driver_is_ready(drv) || !MR_OBJECT_IS_INITED(&data->event)) {
            priv->tim->DMAINTENR &= ~TIM_UIE;
            priv->events = MR_FALSE;
        } else {
            /* Raise TMR event */
            mr_tmr_event_raise(drv, MR_TMR_WAIT_TIMEOUT);
        }
    }

    /* Unlock */
    mr_port_irq_enable(mask);
}

/* Instance install */
#define CH32_HT_TMR_INSTALL(_id, _ops)                                         \
    extern void MR_HT_DRIVER_PROP(_id, wch_clken)(void);                       \
    static tmr_priv_t MR_CAT(__tmr_priv_, _id) = {                             \
        .tim = MR_HT_DRIVER_PROP(_id, wch_timx),                               \
        .clk_enable = MR_HT_DRIVER_PROP(_id, wch_clken),                       \
        .tim3_input_hz = MR_HT_DRIVER_PROP_OR(_id, wch_tim3_input_hz, 0U),     \
        .tim3_dma_compare =                                                    \
            MR_HT_DRIVER_PROP_OR(_id, wch_tim3_dma_compare, 0U),               \
        .tim3_cc1_ticks = MR_HT_DRIVER_PROP_OR(_id, wch_tim3_cc1_ticks, 0U),   \
        .tim3_cc2_ticks = MR_HT_DRIVER_PROP_OR(_id, wch_tim3_cc2_ticks, 0U),   \
        .irqn = __CH32_HT_TMR_IRQ_NUMBER(_id),                                 \
        .irq_priority = __CH32_HT_TMR_IRQ_PRIORITY(_id),                       \
    };                                                                         \
    __CH32_HT_TMR_IRQ(_id)                                                     \
    MR_HT_TMR_INSTALL(_id, &MR_CAT(__tmr_priv_, _id), _ops)
#define __CH32_HT_TMR_IRQ(_id)                                                 \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, wch_irq_handler),                   \
                (MR_CH32_INTERRUPT void                                        \
                     MR_HT_DRIVER_PROP(_id, wch_irq_handler)(void) {           \
                     tmr_irq(MR_HT_DRIVER_FIND(MR_HT_DRIVER(_id)));            \
                 }),                                                           \
                ())
#define __CH32_HT_TMR_IRQ_NUMBER(_id)                                          \
    MR_HT_DRIVER_PROP_OR(_id, wch_irqn, TMR_NO_IRQ)
#define __CH32_HT_TMR_IRQ_PRIORITY(_id)                                        \
    MR_HT_DRIVER_PROP_OR(_id, wch_irq_priority, 0U)
MR_HT_DRIVER_FOREACH(CH32_HT_TMR_INSTALL, &__tmr_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_TMR) */

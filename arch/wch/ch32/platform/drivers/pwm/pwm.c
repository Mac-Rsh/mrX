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
#define MR_HT_COMPAT                    wch_ch32_pwm
#if MR_HT_HAS_DRIVER()
#if !defined(MR_USE_DRIVER_GPIO)
#error "CH32 PWM requires the GPIO driver"
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
static mr_err_t pwm_fault_configure(const mr_driver_t *drv, mr_uint32_t fault,
                                    const mr_pwm_fault_config_t *config);
static mr_err_t pwm_fault_configuration(const mr_driver_t *drv,
                                        mr_uint32_t fault,
                                        mr_pwm_fault_config_t *config);
static mr_err_t pwm_fault_clear(const mr_driver_t *drv, mr_uint32_t faults);
static mr_err_t pwm_signal_configure(const mr_driver_t *drv,
                                     mr_pwm_signal_t signal,
                                     const mr_pwm_signal_config_t *config);
static mr_err_t pwm_signal_configuration(const mr_driver_t *drv,
                                         mr_pwm_signal_t signal,
                                         mr_pwm_signal_config_t *config);
static mr_err_t pwm_signal_control(const mr_driver_t *drv,
                                   mr_pwm_signal_t signal, mr_bool_t enable);
static mr_err_t pwm_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable);
#define PWM_OPS(_fault_configure, _fault_configuration, _fault_clear)          \
    {                                                                          \
        .configure = pwm_configure,                                            \
        .configuration = pwm_configuration,                                    \
        .channel_configure = pwm_channel_configure,                            \
        .channel_configuration = pwm_channel_configuration,                    \
        .update = pwm_update,                                                  \
        .start = pwm_start,                                                    \
        .stop = pwm_stop,                                                      \
        .state = pwm_state,                                                    \
        .fault_configure = _fault_configure,                                   \
        .fault_configuration = _fault_configuration,                           \
        .fault_clear = _fault_clear,                                           \
        .signal_configure = pwm_signal_configure,                              \
        .signal_configuration = pwm_signal_configuration,                      \
        .signal_control = pwm_signal_control,                                  \
        .event_control = pwm_event_control,                                    \
    }
MR_UNUSED static const mr_pwm_ops_t __pwm_ops =
    PWM_OPS(MR_NULL, MR_NULL, MR_NULL);
MR_UNUSED static const mr_pwm_ops_t __pwm_break_ops =
    PWM_OPS(pwm_fault_configure, pwm_fault_configuration, pwm_fault_clear);

/* PWM channel definition */
#define PWM_CHANNELS                    (4U)
#define PWM_CHANNEL_MASK                (0x0fU)
#define PWM_COMP_CHANNELS               (3U)
#define PWM_COMP_MASK                   (0x07U)

/* Advanced timer dead-time definition */
#define PWM_DTG_MASK                    (0xffU)
#define PWM_DTG_MAX_TICKS               (1008U)
#define PWM_NS_PER_SECOND               (1000000000ULL)

/* PWM channel mode definition */
#define PWM_MODE_INACTIVE               (0x40U)
#define PWM_MODE_ACTIVE                 (0x50U)
#define PWM_MODE_PRELOAD                (0x68U)

/* PWM IRQ definition */
#define PWM_NO_IRQ                      (-1)

/* PWM fault input definition */
#define PWM_BREAK_FAULT                 (1U)

/* PWM private data type */
typedef struct pwm_priv {
    TIM_TypeDef *tim;
    void (*clk_enable)(void);
    mr_ht_gpio_spec_t pins[PWM_CHANNELS];
    mr_ht_gpio_spec_t comp_pins[PWM_COMP_CHANNELS];
    mr_ht_gpio_spec_t bkin;
    mr_ch32_timer_base_t base;
    mr_pwm_config_t config;
    mr_pwm_fault_config_t fault;
    mr_pwm_channel_config_t channels[PWM_CHANNELS];
    mr_uint32_t compare[PWM_CHANNELS];
    mr_uint32_t configured, running, complementary;
    int irqn, brk_irqn;
    mr_uint8_t irq_priority;
    mr_bool_t grouped, period_signal, period_event, fault_event;
} pwm_priv_t;

static mr_uint32_t pwm_fault_mask(const pwm_priv_t *priv) {
    return (priv->fault.enable && ((priv->tim->INTFR & TIM_BIF) != 0U))
        ? PWM_BREAK_FAULT : 0U;
}

static mr_err_t pwm_break_inactive(const pwm_priv_t *priv) {
    const mr_gpio_data_t *data;
    mr_uint8_t level;
    mr_ssize_t size;

    if (!mr_driver_is_ready(priv->bkin.drv)) {
        return -MR_ENODEV;
    }
    size = mr_gpio_read(priv->bkin.drv, priv->bkin.pin, &level);
    if (size < 0) {
        return (mr_err_t)size;
    }
    /* Timer polarity applies to the physical level before GPIO inversion */
    data = MR_DRIVER_DATA(priv->bkin.drv);
    level ^= (data->invert_mask & (1U << priv->bkin.pin)) != 0U;
    return (level == (priv->fault.polarity == MR_PWM_POLARITY_ACTIVE_HIGH))
        ? -MR_EBUSY : 0;
}

static mr_err_t pwm_make_period(const TIM_TypeDef *tim, mr_uint64_t period,
                                mr_bool_t center, mr_ch32_timer_base_t *base) {
    mr_uint64_t max_counts;

    /* Leave room in CCR for exact 100 percent duty */
    max_counts = MR_UINT16_MAX;
#if defined(MR_CH32_HAS_TIM5_32BIT)
    if (tim == TIM5) {
        max_counts = MR_UINT32_MAX;
    }
#elif defined(MR_USE_CH32V205)
    if (tim == TIM4) {
        max_counts = MR_UINT32_MAX;
    }
#endif /* CH32 timer with 32-bit compare */
    /* In center-aligned mode full duty requires a compare greater than ARR */
    return mr_ch32_timer_make_period(mr_ch32_timer_clock(tim), period,
                                     max_counts - center, center ? 2U : 1U,
                                     base);
}

static mr_err_t pwm_make_deadtime(mr_uint32_t hz, mr_uint64_t ns,
                                 mr_uint32_t *dtg, mr_uint64_t *actual) {
    mr_uint32_t ticks, step, code;

    /* CKD is zero, so dead time uses the timer clock before PSC */
    if (ns > ((PWM_DTG_MAX_TICKS * PWM_NS_PER_SECOND) / hz)) {
        return -MR_ERANGE;
    }
    ticks = (mr_uint32_t)((ns * hz + PWM_NS_PER_SECOND - 1U)
                          / PWM_NS_PER_SECOND);

    /* Round to the next representable delay in the four DTG ranges */
    if (ticks <= 127U) {
        code = ticks;
    } else if (ticks <= 254U) {
        step = (ticks + 1U) / 2U;
        code = 0x80U + step - 64U;
        ticks = step * 2U;
    } else if (ticks <= 504U) {
        step = (ticks + 7U) / 8U;
        code = 0xc0U + step - 32U;
        ticks = step * 8U;
    } else {
        step = (ticks + 15U) / 16U;
        code = 0xe0U + step - 32U;
        ticks = step * 16U;
    }
    *dtg = code;
    *actual = (ticks * PWM_NS_PER_SECOND) / hz;
    return 0;
}

static void pwm_set_trgo(TIM_TypeDef *tim, mr_uint16_t mms) {
    /* Preserve the idle levels programmed in OISx/OISxN */
    tim->CTLR2 = (tim->CTLR2 & ~TIM_MMS) | mms;
}

static void pwm_set_idle_levels(TIM_TypeDef *tim, mr_uint32_t channel,
                                const mr_pwm_channel_config_t *config) {
    mr_uint16_t ctlr, main, comp;

    /* OIS fields are physical levels, independent of CCER polarity */
    main = TIM_OIS1 << (channel * 2U);
    ctlr = tim->CTLR2 & ~main;
    if ((config->idle == MR_PWM_IDLE_ACTIVE)
        != (config->polarity == MR_PWM_POLARITY_ACTIVE_LOW)) {
        ctlr |= main;
    }
    if (channel < PWM_COMP_CHANNELS) {
        comp = TIM_OIS1N << (channel * 2U);
        ctlr &= ~comp;
        if (config->comp && ((config->comp_idle == MR_PWM_IDLE_ACTIVE)
                             != (config->comp_polarity
                                 == MR_PWM_POLARITY_ACTIVE_LOW))) {
            ctlr |= comp;
        }
    }
    tim->CTLR2 = ctlr;
}

static void pwm_mode(TIM_TypeDef *tim, mr_uint32_t ch, mr_uint32_t mode) {
    volatile uint16_t *reg;
    mr_uint32_t shift;

    /* Write the channel mode field */
    reg = (ch < 2U) ? &tim->CHCTLR1 : &tim->CHCTLR2;
    shift = (ch % 2U) * 8U;
    *reg = (*reg & ~(0xffU << shift)) | (mode << shift);
}

static void pwm_compare(const pwm_priv_t *priv, mr_uint32_t ch,
                        mr_uint32_t compare) {
    TIM_TypeDef *tim;

    /* Keep the output asserted across both halves of a full-duty period */
    tim = priv->tim;
    if ((priv->config.alignment == MR_PWM_ALIGN_CENTER)
        && (compare == priv->base.counts)) {
        compare++;
    }
#if defined(MR_CH32_HAS_TIM5_32BIT)
    if (tim == TIM5) {
        /* TIM5 uses full-width compare writes on all four channels */
        switch (ch) {
            case 0U:
                tim->CH1CVR_R32 = compare;
                break;
            case 1U:
                tim->CH2CVR_R32 = compare;
                break;
            case 2U:
                tim->CH3CVR_R32 = compare;
                break;
            default:
                tim->CH4CVR_R32 = compare;
                break;
        }
        return;
    }
#elif defined(MR_USE_CH32V205)
    if (tim == TIM4) {
        typedef uint32_t pwm_cvr32_t __attribute__((may_alias));
        volatile pwm_cvr32_t *cvr = (volatile pwm_cvr32_t *)&tim->CH1CVR;

        /* TIM4 compare registers are 32-bit at 4-byte stride from CH1CVR */
        cvr[ch] = compare;
        return;
    }
#endif /* CH32 timer with 32-bit compare */

    /* The period limit bounds compare to the instance's register width */
    switch (ch) {
        case 0U:
            tim->CH1CVR = compare;
            break;
        case 1U:
            tim->CH2CVR = compare;
            break;
        case 2U:
            tim->CH3CVR = compare;
            break;
        default:
            tim->CH4CVR = compare;
            break;
    }
}

static mr_err_t pwm_configure(const mr_driver_t *drv,
                              const mr_pwm_config_t *config) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    const mr_pwm_data_t *data;
    mr_ch32_timer_base_t base;
    mr_bool_t center;
    mr_err_t ret;
    int mask;

    /* Check PWM resources */
    if (priv->tim == MR_NULL) {
        return -MR_EINVAL;
    }

    /* Check configuration */
    data = MR_DRIVER_DATA(drv);
    center = config->alignment == MR_PWM_ALIGN_CENTER;
    if (mr_ch32_timer_is_basic(priv->tim)
        || ((center || ((data->comp_mask | data->fault_mask) != 0U))
            && !mr_ch32_timer_is_advanced(priv->tim))
        || (config->cycles != 0U)
        || (!center && (config->alignment != MR_PWM_ALIGN_EDGE_UP))
        || (config->update_mode != MR_PWM_UPDATE_PERIOD)) {
        return -MR_ENOSYS;
    }
    if (((data->channel_mask & ~PWM_CHANNEL_MASK) != 0U)
        || ((data->comp_mask & ~PWM_COMP_MASK) != 0U)
        || ((data->fault_mask != 0U) && (priv->bkin.drv == MR_NULL))) {
        return -MR_EINVAL;
    }
#if defined(MR_USE_CH32VM00X) && defined(TIM3)
    if (priv->tim == TIM3) {
        return -MR_ENOSYS;
    }
#endif /* defined(MR_USE_CH32VM00X) && defined(TIM3) */

    /* Calculate PWM period, leaving room in CCR for exact 100 percent duty */
    ret = pwm_make_period(priv->tim, config->period, center, &base);
    if (ret != 0) {
        return ret;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check PWM state */
    if ((priv->running != 0U) || priv->fault.enable) {
        mr_port_irq_enable(mask);
        return -MR_EBUSY;
    }

    /* Claim the timer */
    ret = mr_ch32_timer_claim(priv->tim, drv);
    if (ret != 0) {
        mr_port_irq_enable(mask);
        return ret;
    }

    /* Init PWM */
    priv->clk_enable();
    priv->grouped = (data->comp_mask | data->fault_mask) != 0U;
    priv->tim->CTLR1 = TIM_ARPE | (center ? TIM_CMS_0 : 0U);
    priv->tim->CTLR2 = TIM_MMS_0;
    priv->tim->SMCFGR = 0U;
    priv->tim->DMAINTENR = 0U;
    priv->tim->CCER = 0U;
    if (mr_ch32_timer_is_advanced(priv->tim)) {
        priv->tim->BDTR = priv->grouped ? TIM_OSSI : 0U;
        /* Update once per full period rather than on both counter turns */
        priv->tim->RPTCR = center ? 1U : 0U;
    }
    priv->tim->PSC = base.divider - 1U;
    mr_ch32_timer_set_reload(priv->tim,
                             (mr_uint32_t)(base.counts - (center ? 0U : 1U)));
    priv->tim->SWEVGR = TIM_UG;
    priv->tim->INTFR = 0U;
    priv->tim->CTLR2 = priv->period_signal ? TIM_MMS_1 : 0U;

    /* Save PWM configuration */
    priv->base = base;
    priv->config = *config;
    priv->config.period = base.period;
    priv->configured = 0U;
    priv->complementary = 0U;

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t pwm_configuration(const mr_driver_t *drv,
                                  mr_pwm_config_t *config) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check PWM state */
    if (priv->base.hz == 0U) {
        mr_port_irq_enable(mask);
        return -MR_ENODEV;
    }

    /* Get PWM configuration */
    *config = priv->config;

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t pwm_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_pwm_channel_config_t *config) {
    mr_uint32_t bit, shift, ccer, compare, dtg;
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint64_t actual, deadtime;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    if ((channel >= PWM_CHANNELS) || (priv->pins[channel].drv == MR_NULL)) {
        return -MR_EINVAL;
    }

    /* Check configuration */
    if ((config->phase != 0U) || (config->idle == MR_PWM_IDLE_HIGH_Z)
        || (config->dead_rise != config->dead_fall)
        || (!config->comp && (config->dead_rise != 0U))) {
        return -MR_ENOSYS;
    }
    if ((priv->bkin.drv != MR_NULL)
        && ((config->idle != MR_PWM_IDLE_INACTIVE)
            || (config->comp && (config->comp_idle != MR_PWM_IDLE_INACTIVE)))) {
        return -MR_ENOSYS;
    }
    if (config->comp) {
        if ((channel >= PWM_COMP_CHANNELS)
            || (config->comp_idle == MR_PWM_IDLE_HIGH_Z)
            || ((config->idle == MR_PWM_IDLE_ACTIVE)
                && (config->comp_idle == MR_PWM_IDLE_ACTIVE))) {
            return -MR_ENOSYS;
        }
        if (priv->comp_pins[channel].drv == MR_NULL) {
            return -MR_EINVAL;
        }
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check PWM state */
    bit = 1U << channel;
    dtg = 0U;
    deadtime = 0U;
    if (priv->base.hz == 0U) {
        ret = -MR_ENODEV;
    } else if ((priv->running & bit) != 0U
               || (priv->grouped && (priv->running != 0U))) {
        ret = -MR_EBUSY;
    } else {
        ret = mr_ch32_timer_make_pulse(&priv->base, config->pulse, &compare,
                                      &actual);
    }
    if ((ret == 0) && config->comp) {
        ret = pwm_make_deadtime(priv->base.hz, config->dead_rise, &dtg,
                                &deadtime);
        if ((ret == 0) && ((priv->complementary & ~bit) != 0U)
            && ((priv->tim->BDTR & PWM_DTG_MASK) != dtg)) {
            ret = -MR_ENOSYS;
        }
    }
    if (ret == 0) {
        /* Configure GPIO */
        ret = mr_ch32_gpio_pin_alternate(&priv->pins[channel], MR_TRUE,
                                        MR_GPIO_PULL_NONE, MR_GPIO_OTYPE_PP);
        if ((ret == 0) && config->comp) {
            ret = mr_ch32_gpio_pin_alternate(&priv->comp_pins[channel], MR_TRUE,
                                            MR_GPIO_PULL_NONE,
                                            MR_GPIO_OTYPE_PP);
        }
    }
    if (ret == 0) {
        /* Configure PWM channel */
        shift = channel * 4U;
        ccer = priv->tim->CCER & ~(0xfU << shift);
        if (config->polarity == MR_PWM_POLARITY_ACTIVE_LOW) {
            ccer |= TIM_CC1P << shift;
        }
        if (config->comp) {
            ccer |= TIM_CC1NE << shift;
            if (config->comp_polarity == MR_PWM_POLARITY_ACTIVE_LOW) {
                ccer |= TIM_CC1NP << shift;
            }
            priv->tim->BDTR = (priv->tim->BDTR & ~PWM_DTG_MASK) | dtg;
            priv->complementary |= bit;
        } else {
            priv->complementary &= ~bit;
        }
        if (priv->grouped) {
            pwm_set_idle_levels(priv->tim, channel, config);
        }
        pwm_mode(priv->tim, channel,
                 (config->idle == MR_PWM_IDLE_ACTIVE) ? PWM_MODE_ACTIVE
                                                      : PWM_MODE_INACTIVE);
        priv->tim->CCER = ccer | (TIM_CC1E << shift);
        if (!priv->grouped && mr_ch32_timer_is_advanced(priv->tim)) {
            priv->tim->BDTR |= TIM_MOE;
        }
        priv->channels[channel] = *config;
        priv->channels[channel].pulse = actual;
        priv->channels[channel].dead_rise = deadtime;
        priv->channels[channel].dead_fall = deadtime;
        priv->compare[channel] = compare;
        priv->configured |= bit;
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

static mr_err_t pwm_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_pwm_channel_config_t *config) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check channel */
    if ((channel >= PWM_CHANNELS)
        || ((priv->configured & (1U << channel)) == 0U)) {
        mr_port_irq_enable(mask);
        return -MR_ENODEV;
    }

    /* Get channel configuration */
    *config = priv->channels[channel];

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_bool_t pwm_comp_state_supported(const mr_pwm_update_t *update) {
    if (update->state == MR_PWM_OUTPUT_PWM) {
        return update->comp_state == MR_PWM_OUTPUT_PWM;
    }
    if (update->state == MR_PWM_OUTPUT_ACTIVE) {
        return update->comp_state == MR_PWM_OUTPUT_INACTIVE;
    }
    return (update->state == MR_PWM_OUTPUT_INACTIVE)
        && (update->comp_state == MR_PWM_OUTPUT_ACTIVE);
}

static mr_err_t pwm_update(const mr_driver_t *drv,
                           const mr_pwm_update_t *updates, mr_size_t count) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t compare[PWM_CHANNELS];
    mr_uint64_t pulse[PWM_CHANNELS];
    mr_uint32_t channels, ch;
    mr_err_t ret;
    mr_size_t i;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check PWM state */
    if (priv->base.hz == 0U) {
        mr_port_irq_enable(mask);
        return -MR_ENODEV;
    }

    /* Stage channel updates */
    channels = 0U;
    ret = 0;
    for (i = 0U; i < count; i++) {
        ch = updates[i].channel;
        if ((ch >= PWM_CHANNELS) || ((priv->configured & (1U << ch)) == 0U)) {
            ret = -MR_ENODEV;
        } else if ((updates[i].phase != 0U)
                   || (updates[i].state == MR_PWM_OUTPUT_HIGH_Z)) {
            ret = -MR_ENOSYS;
        } else if (((priv->complementary & (1U << ch)) != 0U)
                   && !pwm_comp_state_supported(&updates[i])) {
            /* A complementary pair shares one compare reference */
            ret = -MR_ENOSYS;
        } else if (updates[i].state == MR_PWM_OUTPUT_INACTIVE) {
            compare[ch] = 0U;
            pulse[ch] = 0U;
        } else if (updates[i].state == MR_PWM_OUTPUT_ACTIVE) {
            compare[ch] = (mr_uint32_t)priv->base.counts;
            pulse[ch] = priv->base.period;
        } else {
            ret = mr_ch32_timer_make_pulse(&priv->base, updates[i].pulse,
                                          &compare[ch], &pulse[ch]);
        }
        if (ret != 0) {
            mr_port_irq_enable(mask);
            return ret;
        }
        channels |= 1U << ch;
    }

    /* Block shadow transfers until all active channel values are staged */
    priv->tim->CTLR1 |= TIM_UDIS;
    for (ch = 0U; ch < PWM_CHANNELS; ch++) {
        if ((channels & (1U << ch)) != 0U) {
            priv->compare[ch] = compare[ch];
            priv->channels[ch].pulse = pulse[ch];
            if ((priv->running & (1U << ch)) != 0U) {
                pwm_compare(priv, ch, compare[ch]);
            }
        }
    }
    priv->tim->CTLR1 &= ~TIM_UDIS;

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t pwm_start(const mr_driver_t *drv, mr_uint32_t channels) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t ch;
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check channels */
    if (channels == 0U) {
        mr_port_irq_enable(mask);
        return 0;
    }
    if ((channels & ~priv->configured) != 0U) {
        mr_port_irq_enable(mask);
        return -MR_ENODEV;
    }
    if ((channels & priv->running) != 0U) {
        mr_port_irq_enable(mask);
        return -MR_EBUSY;
    }
    if (priv->grouped && (channels != priv->configured)) {
        mr_port_irq_enable(mask);
        return -MR_ENOSYS;
    }
    if (priv->fault.enable) {
        ret = (pwm_fault_mask(priv) != 0U) ? -MR_EBUSY
                                          : pwm_break_inactive(priv);
        if (ret != 0) {
            mr_port_irq_enable(mask);
            return ret;
        }
    }

    /* Load compare values */
    for (ch = 0U; ch < PWM_CHANNELS; ch++) {
        if ((channels & (1U << ch)) != 0U) {
            /* Load the active compare before enabling its preload */
            pwm_compare(priv, ch, priv->compare[ch]);
            pwm_mode(priv->tim, ch, PWM_MODE_PRELOAD);
        }
    }
    if (priv->running == 0U) {
        /* Reload the time base only for the first group of running channels */
        pwm_set_trgo(priv->tim, TIM_MMS_0);
        priv->tim->SWEVGR = TIM_UG;
        priv->tim->INTFR = (mr_uint16_t)~TIM_UIF;
        pwm_set_trgo(priv->tim, priv->period_signal ? TIM_MMS_1 : 0U);
    }

    /* Start PWM */
    if (priv->grouped) {
        /* Keep BKE armed through MOE and reject a fault racing with start */
        if (pwm_fault_mask(priv) != 0U) {
            mr_port_irq_enable(mask);
            return -MR_EBUSY;
        }
        priv->tim->BDTR |= TIM_MOE;
        if (priv->fault.enable && ((pwm_fault_mask(priv) != 0U)
                                  || ((priv->tim->BDTR & TIM_MOE) == 0U))) {
            priv->tim->BDTR &= ~TIM_MOE;
            mr_port_irq_enable(mask);
            return -MR_EBUSY;
        }
    }
    if ((priv->running == 0U) && priv->period_event) {
        priv->tim->DMAINTENR |= TIM_UIE;
    }
    priv->running |= channels;
    priv->tim->CTLR1 |= TIM_CEN;

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t pwm_stop(const mr_driver_t *drv, mr_uint32_t channels) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t ch;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check PWM state */
    if (priv->running == 0U) {
        mr_port_irq_enable(mask);
        return 0;
    }
    channels &= priv->running;
    if (priv->grouped && (channels != 0U)) {
        if (channels != priv->running) {
            mr_port_irq_enable(mask);
            return -MR_ENOSYS;
        }
        /* Both sides enter their hardware idle states through the MOE gate */
        priv->tim->BDTR &= ~TIM_MOE;
    }

    /* Apply idle state */
    for (ch = 0U; ch < PWM_CHANNELS; ch++) {
        if ((channels & (1U << ch)) != 0U) {
            pwm_mode(priv->tim, ch,
                     (priv->channels[ch].idle == MR_PWM_IDLE_ACTIVE)
                         ? PWM_MODE_ACTIVE : PWM_MODE_INACTIVE);
        }
    }

    /* Stop PWM */
    priv->running &= ~channels;
    if (priv->running == 0U) {
        priv->tim->CTLR1 &= ~TIM_CEN;
        priv->tim->DMAINTENR &= ~TIM_UIE;
        priv->tim->INTFR = (mr_uint16_t)~TIM_UIF;
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t pwm_state(const mr_driver_t *drv, mr_pwm_state_t *state) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Get PWM state */
    mask = mr_port_irq_disable();
    state->running_mask = priv->running;
    state->fault_mask = pwm_fault_mask(priv);
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t pwm_fault_configure(const mr_driver_t *drv, mr_uint32_t fault,
                                    const mr_pwm_fault_config_t *config) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint16_t bdtr;
    mr_err_t ret;
    int mask;

    if ((fault != 0U) || (priv->bkin.drv == MR_NULL)) {
        return -MR_ENOSYS;
    }
    if ((config->filter != 0U) || (config->mode != MR_PWM_FAULT_LATCHED)) {
        return -MR_ENOSYS;
    }

    mask = mr_port_irq_disable();
    if (priv->base.hz == 0U) {
        ret = -MR_ENODEV;
    } else if ((priv->running != 0U) || (pwm_fault_mask(priv) != 0U)) {
        ret = -MR_EBUSY;
    } else {
        ret = 0;
        if (config->enable && !priv->fault.enable) {
            ret = mr_ch32_gpio_pin_alternate(&priv->bkin, MR_FALSE,
                                            MR_GPIO_PULL_NONE,
                                            MR_GPIO_OTYPE_PP);
        }
        if (ret == 0) {
            /* Keep software restart explicit by leaving AOE clear */
            bdtr = priv->tim->BDTR & ~(TIM_BKE | TIM_BKP | TIM_AOE | TIM_MOE);
            if (config->polarity == MR_PWM_POLARITY_ACTIVE_HIGH) {
                bdtr |= TIM_BKP;
            }
            priv->tim->DMAINTENR &= ~TIM_BIE;
            if (!priv->fault.enable) {
                priv->tim->INTFR = (mr_uint16_t)~TIM_BIF;
            }
            if (config->enable) {
                bdtr |= TIM_BKE;
            }
            priv->tim->BDTR = bdtr;
            priv->fault = *config;
            if (config->enable && priv->fault_event) {
                priv->tim->DMAINTENR |= TIM_BIE;
            }
        }
    }
    mr_port_irq_enable(mask);
    return ret;
}

static mr_err_t pwm_fault_configuration(const mr_driver_t *drv,
                                        mr_uint32_t fault,
                                        mr_pwm_fault_config_t *config) {
    const pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    if ((fault != 0U) || (priv->bkin.drv == MR_NULL)) {
        return -MR_ENOSYS;
    }
    mask = mr_port_irq_disable();
    if (priv->base.hz == 0U) {
        mr_port_irq_enable(mask);
        return -MR_ENODEV;
    }
    *config = priv->fault;
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t pwm_fault_clear(const mr_driver_t *drv, mr_uint32_t faults) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;
    int mask;

    if ((faults != PWM_BREAK_FAULT) || (priv->bkin.drv == MR_NULL)) {
        return -MR_ENOSYS;
    }
    mask = mr_port_irq_disable();
    if (priv->base.hz == 0U) {
        ret = -MR_ENODEV;
    } else if (priv->running != 0U) {
        ret = -MR_EBUSY;
    } else {
        ret = 0;
        if (priv->fault.enable) {
            ret = pwm_break_inactive(priv);
            if (ret == 0) {
                /* Clear only BIF while MOE stays disabled */
                priv->tim->INTFR = (mr_uint16_t)~TIM_BIF;
                if (priv->fault_event) {
                    priv->tim->DMAINTENR |= TIM_BIE;
                }
                if (pwm_fault_mask(priv) != 0U) {
                    ret = -MR_EBUSY;
                }
            }
        }
    }
    mr_port_irq_enable(mask);
    return ret;
}

static mr_err_t pwm_signal_status(const pwm_priv_t *priv,
                                  mr_pwm_signal_t signal) {
    /* Check signal */
    if (signal != MR_PWM_SIGNAL_PERIOD) {
        return -MR_ENOSYS;
    }
    return (priv->base.hz != 0U) ? 0 : -MR_ENODEV;
}

static mr_err_t pwm_signal_configure(const mr_driver_t *drv,
                                     mr_pwm_signal_t signal,
                                     const mr_pwm_signal_config_t *config) {
    const pwm_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check configuration */
    if ((signal == MR_PWM_SIGNAL_PERIOD) && (config->offset != 0U)) {
        return -MR_EINVAL;
    }
    return pwm_signal_status(priv, signal);
}

static mr_err_t pwm_signal_configuration(const mr_driver_t *drv,
                                         mr_pwm_signal_t signal,
                                         mr_pwm_signal_config_t *config) {
    const pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;

    /* Get signal configuration */
    ret = pwm_signal_status(priv, signal);
    if (ret == 0) {
        config->offset = 0U;
    }
    return ret;
}

static mr_err_t pwm_signal_control(const mr_driver_t *drv,
                                   mr_pwm_signal_t signal, mr_bool_t enable) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check signal */
    ret = pwm_signal_status(priv, signal);
    if (ret == 0) {
        /* Configure PWM signal */
        priv->period_signal = enable;
        pwm_set_trgo(priv->tim, enable ? TIM_MMS_1 : 0U);
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

static mr_err_t pwm_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable) {
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Check PWM capability */
    if ((priv->tim == MR_NULL)
        || ((events & ~(MR_PWM_WAIT_PERIOD | MR_PWM_WAIT_FAULT)) != 0U)
        || (((events & MR_PWM_WAIT_PERIOD) != 0U)
            && !mr_ch32_irq_valid(priv->irqn))
        || (((events & MR_PWM_WAIT_FAULT) != 0U)
            && !mr_ch32_irq_valid(priv->brk_irqn))) {
        return -MR_ENOSYS;
    }
    if (events == 0U) {
        return (mr_ch32_irq_valid(priv->irqn)
                || mr_ch32_irq_valid(priv->brk_irqn)) ? 0 : -MR_ENOSYS;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Update event state */
    if (((events & MR_PWM_WAIT_PERIOD) != 0U)
        && (priv->period_event != enable)) {
        priv->period_event = enable;
        if (enable) {
            NVIC_SetPriority((IRQn_Type)priv->irqn, priv->irq_priority);
            NVIC_ClearPendingIRQ((IRQn_Type)priv->irqn);
            if (priv->running != 0U) {
                priv->tim->INTFR = (mr_uint16_t)~TIM_UIF;
                priv->tim->DMAINTENR |= TIM_UIE;
            }
            NVIC_EnableIRQ((IRQn_Type)priv->irqn);
        } else {
            if (priv->base.hz != 0U) {
                priv->tim->DMAINTENR &= ~TIM_UIE;
            }
            NVIC_DisableIRQ((IRQn_Type)priv->irqn);
        }
    }
    if (((events & MR_PWM_WAIT_FAULT) != 0U) && (priv->fault_event != enable)) {
        priv->fault_event = enable;
        if (enable) {
            NVIC_SetPriority((IRQn_Type)priv->brk_irqn, priv->irq_priority);
            NVIC_ClearPendingIRQ((IRQn_Type)priv->brk_irqn);
            if (priv->fault.enable) {
                priv->tim->DMAINTENR |= TIM_BIE;
            }
            NVIC_EnableIRQ((IRQn_Type)priv->brk_irqn);
        } else {
            if (priv->base.hz != 0U) {
                priv->tim->DMAINTENR &= ~TIM_BIE;
            }
            NVIC_DisableIRQ((IRQn_Type)priv->brk_irqn);
        }
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

MR_UNUSED static void pwm_irq(mr_driver_t *drv) {
    const mr_pwm_data_t *data = MR_DRIVER_DATA(drv);
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_pwm_event_data_t event;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Handle update event */
    if ((priv->running != 0U) && ((priv->tim->INTFR & TIM_UIF) != 0U)
        && ((priv->tim->DMAINTENR & TIM_UIE) != 0U)) {
        priv->tim->INTFR = (mr_uint16_t)~TIM_UIF;
        if (mr_driver_is_ready(drv) && MR_OBJECT_IS_INITED(&data->event)) {
            /* Raise PWM event */
            event.channel_mask = priv->running;
            event.fault_mask = pwm_fault_mask(priv);
            mr_pwm_event_raise(drv, MR_PWM_WAIT_PERIOD, &event);
        } else {
            priv->tim->DMAINTENR &= ~TIM_UIE;
            NVIC_DisableIRQ((IRQn_Type)priv->irqn);
            priv->period_event = MR_FALSE;
        }
    }

    /* Unlock */
    mr_port_irq_enable(mask);
}

MR_UNUSED static void pwm_break_irq(mr_driver_t *drv) {
    const mr_pwm_data_t *data = MR_DRIVER_DATA(drv);
    pwm_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_pwm_event_data_t event;
    int mask;

    mask = mr_port_irq_disable();
    if ((pwm_fault_mask(priv) != 0U)
        && ((priv->tim->DMAINTENR & TIM_BIE) != 0U)) {
        /* Mask notifications while BIF remains latched; BKE stays armed */
        priv->tim->DMAINTENR &= ~TIM_BIE;
        if (mr_driver_is_ready(drv) && MR_OBJECT_IS_INITED(&data->event)) {
            event.channel_mask = priv->running;
            event.fault_mask = PWM_BREAK_FAULT;
            mr_pwm_event_raise(drv, MR_PWM_WAIT_FAULT, &event);
        } else {
            NVIC_DisableIRQ((IRQn_Type)priv->brk_irqn);
            priv->fault_event = MR_FALSE;
        }
    }
    mr_port_irq_enable(mask);
}

/* Instance install */
#define CH32_HT_PWM_INSTALL(_id, _ops)                                         \
    extern void MR_HT_DRIVER_PROP(_id, wch_clken)(void);                       \
    static pwm_priv_t MR_CAT(__pwm_priv_, _id) = {                             \
        .tim = MR_HT_DRIVER_PROP(_id, wch_timx),                               \
        .clk_enable = MR_HT_DRIVER_PROP(_id, wch_clken),                       \
        .pins = {                                                              \
            MR_HT_GPIO_SPEC_OR(_id, wch_ch1, {0}),                             \
            MR_HT_GPIO_SPEC_OR(_id, wch_ch2, {0}),                             \
            MR_HT_GPIO_SPEC_OR(_id, wch_ch3, {0}),                             \
            MR_HT_GPIO_SPEC_OR(_id, wch_ch4, {0}),                             \
        },                                                                     \
        .comp_pins = {                                                         \
            MR_HT_GPIO_SPEC_OR(_id, wch_ch1n, {0}),                            \
            MR_HT_GPIO_SPEC_OR(_id, wch_ch2n, {0}),                            \
            MR_HT_GPIO_SPEC_OR(_id, wch_ch3n, {0}),                            \
        },                                                                     \
        .bkin = MR_HT_GPIO_SPEC_OR(_id, wch_bkin, {0}),                        \
        .fault = {                                                             \
            .mode = MR_PWM_FAULT_LATCHED,                                      \
        },                                                                     \
        .irqn = __CH32_HT_PWM_IRQ_NUMBER(_id),                                 \
        .brk_irqn = __CH32_HT_PWM_BRK_IRQ_NUMBER(_id),                         \
        .irq_priority = __CH32_HT_PWM_IRQ_PRIORITY(_id),                       \
    };                                                                         \
    __CH32_HT_PWM_IRQ(_id)                                                     \
    __CH32_HT_PWM_BRK_IRQ(_id)                                                 \
    MR_HT_PWM_INSTALL(_id, &MR_CAT(__pwm_priv_, _id),                          \
                      MR_HT_DRIVER_HAS_PROP(_id, wch_bkin)                     \
                          ? &__pwm_break_ops : (_ops))
#define __CH32_HT_PWM_IRQ(_id)                                                 \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, wch_irq_handler),                   \
                (MR_CH32_INTERRUPT void                                        \
                     MR_HT_DRIVER_PROP(_id, wch_irq_handler)(void) {           \
                     pwm_irq(MR_HT_DRIVER_FIND(MR_HT_DRIVER(_id)));            \
                 }),                                                           \
                ())
#define __CH32_HT_PWM_IRQ_NUMBER(_id)                                          \
    MR_HT_DRIVER_PROP_OR(_id, wch_irqn, PWM_NO_IRQ)
#define __CH32_HT_PWM_IRQ_PRIORITY(_id)                                        \
    MR_HT_DRIVER_PROP_OR(_id, wch_irq_priority, 0U)
#define __CH32_HT_PWM_BRK_IRQ(_id)                                             \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, wch_brk_irq_handler),               \
                (MR_CH32_INTERRUPT void                                        \
                     MR_HT_DRIVER_PROP(_id, wch_brk_irq_handler)(void) {       \
                     pwm_break_irq(MR_HT_DRIVER_FIND(MR_HT_DRIVER(_id)));      \
                 }),                                                           \
                ())
#define __CH32_HT_PWM_BRK_IRQ_NUMBER(_id)                                      \
    MR_HT_DRIVER_PROP_OR(_id, wch_brk_irqn, PWM_NO_IRQ)
MR_HT_DRIVER_FOREACH(CH32_HT_PWM_INSTALL, &__pwm_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_PWM) */

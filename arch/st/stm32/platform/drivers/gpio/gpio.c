/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_GPIO)
#include <mrX/drivers/gpio/mr_gpio.h>
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/libc/mr_string.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    st_stm32_gpio
#if MR_HT_HAS_DRIVER()

/* Driver GPIO operations */
static mr_err_t gpio_pin_configure(const mr_driver_t *drv, mr_uint32_t pin,
                                   const mr_gpio_config_t *config);
static mr_err_t gpio_pin_configuration(const mr_driver_t *drv, mr_uint32_t pin,
                                       mr_gpio_config_t *config);
static mr_err_t gpio_pin_write(const mr_driver_t *drv, mr_uint32_t pin,
                               mr_uint8_t value);
static mr_err_t gpio_pin_read(const mr_driver_t *drv, mr_uint32_t pin,
                              mr_uint8_t *value);
static mr_err_t gpio_port_write(const mr_driver_t *drv, mr_uint32_t mask);
static mr_err_t gpio_port_read(const mr_driver_t *drv, mr_uint32_t *mask);
static mr_err_t gpio_event_attach(const mr_driver_t *drv, mr_uint32_t pin,
                                  mr_waiter_t *entry);
static mr_err_t gpio_event_control(const mr_driver_t *drv, mr_uint32_t pin,
                                   mr_uint32_t events, mr_bool_t enable);
MR_UNUSED static const mr_gpio_ops_t __gpio_ops = {
    .pin_configure = gpio_pin_configure,
    .pin_configuration = gpio_pin_configuration,
    .pin_write = gpio_pin_write,
    .pin_read = gpio_pin_read,
    .port_write = gpio_port_write,
    .port_read = gpio_port_read,
    .event_attach = gpio_event_attach,
    .event_control = gpio_event_control,
};

/* GPIO private type */
typedef struct gpio_priv {
    GPIO_TypeDef *port;
    void (*clk_enable)(void);
    const IRQn_Type *irqns;
    mr_uint32_t irq_priority;
    mr_uint32_t irq_owned_mask;
    mr_uint32_t exti_mask;
} gpio_priv_t;

/* GPIO IRQ map type */
typedef struct gpio_irq_map {
    const mr_driver_t *drv;
    mr_uint8_t event_enabled;
    mr_waitable_t events;
    mr_uint8_t mode;
} gpio_irq_map_t;

/* GPIO lock */
static mr_spinlock_t __gpio_lock = MR_SPINLOCK_INIT();
/* GPIO IRQ map */
static gpio_irq_map_t __gpio_irq_map[16];

/* GPIO capability definitions */
#if defined(GPIO_MODER_MODE0) || defined(GPIO_MODER_MODER0)
#define STM32_GPIO_HAS_MODER
#endif /* defined(GPIO_MODER_MODE0) || defined(GPIO_MODER_MODER0) */
#if defined(GPIO_AFRL_AFSEL0) || defined(GPIO_AFRL_AFRL0)
#define STM32_GPIO_HAS_AFR
#endif /* defined(GPIO_AFRL_AFSEL0) || defined(GPIO_AFRL_AFRL0) */

#if defined(STM32_GPIO_HAS_MODER)
/* GPIO pull map */
static const mr_uint8_t __gpio_pull_from_pupdr[] = {
    MR_GPIO_PULL_NONE,
    MR_GPIO_PULL_UP,
    MR_GPIO_PULL_DOWN,
    MR_GPIO_PULL_NONE,
};
#endif /* defined(STM32_GPIO_HAS_MODER) */

/* GPIO HAL pull map */
static const uint32_t __gpio_hal_pull[] = {
    GPIO_NOPULL,
    GPIO_PULLUP,
    GPIO_PULLDOWN,
};

static volatile uint32_t *gpio_exti_imr(void) {
#if defined(EXTI_D1)
#if defined(DUAL_CORE) && defined(CORE_CM4)
    return &EXTI_D2->IMR1;
#else
    return &EXTI_D1->IMR1;
#endif /* defined(DUAL_CORE) && defined(CORE_CM4) */
#elif defined(EXTI_IMR1_IM0) || defined(EXTI_IMR1_IM0_Msk)
    return &EXTI->IMR1;
#elif defined(EXTI_IMR_MR0) || defined(EXTI_IMR_IM0)                           \
    || defined(EXTI_IMR_IM0_Msk)
    return &EXTI->IMR;
#else
#error "Unsupported STM32 EXTI interrupt-mask layout"
#endif /* STM32 EXTI interrupt-mask layout */
}

static void gpio_exti_it(mr_uint32_t pin, mr_bool_t enable) {
    volatile uint32_t *imr;

    /* Get EXTI mask register */
    imr = gpio_exti_imr();

    /* Control EXTI interrupt */
    if (enable) {
        SET_BIT(*imr, (1UL << pin));
    } else {
        CLEAR_BIT(*imr, (1UL << pin));
    }
}

static void gpio_exti_clear_pending(uint16_t pin_mask) {
    /* Clear EXTI pending */
#if defined(__HAL_GPIO_EXTI_CLEAR_RISING_IT)                                   \
    && defined(__HAL_GPIO_EXTI_CLEAR_FALLING_IT)
    __HAL_GPIO_EXTI_CLEAR_RISING_IT(pin_mask);
    __HAL_GPIO_EXTI_CLEAR_FALLING_IT(pin_mask);
#elif defined(DUAL_CORE) && defined(CORE_CM4)                                  \
    && defined(__HAL_GPIO_EXTID2_CLEAR_IT)
    __HAL_GPIO_EXTID2_CLEAR_IT(pin_mask);
#elif defined(__HAL_GPIO_EXTI_CLEAR_IT)
    __HAL_GPIO_EXTI_CLEAR_IT(pin_mask);
#else
#error "Unsupported STM32 EXTI pending-register API"
#endif /* STM32 EXTI pending-register API */
}

static mr_uint32_t gpio_pin_from_mask(uint16_t pin_mask) {
    int bit;

    /* Find first set bit */
    bit = mr_ffs32(pin_mask);

    /* Check pin mask */
    if ((bit == 0) || (pin_mask != (uint16_t)(1UL << (bit - 1)))) {
        return 16U;
    }
    return (mr_uint32_t)(bit - 1);
}

static mr_err_t gpio_pin_configure(const mr_driver_t *drv, mr_uint32_t pin,
                                   const mr_gpio_config_t *config) {
    mr_bool_t irq_claimed, irq_mode, irq_owned;
    gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    GPIO_InitTypeDef gpio_init = {0};
    gpio_irq_map_t *line;
    uint16_t pin_mask;
    IRQn_Type irqn;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    if (pin >= 16U) {
        return -MR_EINVAL;
    }
    if (config->alt != MR_GPIO_ALT_DISABLE) {
        /* AF number is not available in mr_gpio_config_t */
        return -MR_ENOSYS;
    }
    if (config->pull > MR_GPIO_PULL_DOWN) {
        return -MR_EINVAL;
    }

    /* Prepare IRQ state */
    pin_mask = (uint16_t)(1UL << pin);
    irq_owned = ((priv->irq_owned_mask & pin_mask) != 0U);
    irqn = irq_owned ? priv->irqns[pin] : (IRQn_Type)0;
    irq_mode = MR_FALSE;

    /* Translate pin mode */
    switch (config->mode) {
        case MR_GPIO_MODE_ANALOG: {
            gpio_init.Mode = GPIO_MODE_ANALOG;
            break;
        }
        case MR_GPIO_MODE_OUTPUT: {
            gpio_init.Mode = (config->otype == MR_GPIO_OTYPE_PP)
                               ? GPIO_MODE_OUTPUT_PP
                               : GPIO_MODE_OUTPUT_OD;
            break;
        }
        case MR_GPIO_MODE_INPUT: {
            gpio_init.Mode = GPIO_MODE_INPUT;
            break;
        }
        case MR_GPIO_MODE_IRQ_RISING: {
            gpio_init.Mode = GPIO_MODE_IT_RISING;
            irq_mode = MR_TRUE;
            break;
        }
        case MR_GPIO_MODE_IRQ_FALLING: {
            gpio_init.Mode = GPIO_MODE_IT_FALLING;
            irq_mode = MR_TRUE;
            break;
        }
        case MR_GPIO_MODE_IRQ_EDGE: {
            gpio_init.Mode = GPIO_MODE_IT_RISING_FALLING;
            irq_mode = MR_TRUE;
            break;
        }
        case MR_GPIO_MODE_IRQ_HIGH:
        case MR_GPIO_MODE_IRQ_LOW: {
            /* EXTI does not support level triggering */
            return -MR_ENOSYS;
        }
        default: {
            return -MR_EINVAL;
        }
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__gpio_lock);

    /* Check EXTI ownership */
    line = &__gpio_irq_map[pin];
    irq_claimed = (line->drv == drv);
    if (irq_mode) {
        if ((priv->exti_mask & pin_mask) == 0U) {
            ret = -MR_EPERM;
            goto _exit;
        }
        if ((line->drv != MR_NULL) && !irq_claimed) {
            ret = -MR_EBUSY;
            goto _exit;
        }
    }
    if (priv->clk_enable == MR_NULL) {
        ret = -MR_ENODEV;
        goto _exit;
    }

    /* Init EXTI event */
    if (irq_mode && !MR_OBJECT_IS_INITED((mr_object_t *)&line->events)) {
        ret = mr_waitable_init(&line->events);
        if (ret < 0) {
            goto _exit;
        }
    }

    /* Enable GPIO clock */
    priv->clk_enable();

    /* Release old EXTI state */
    if (irq_claimed) {
        gpio_exti_it(pin, MR_FALSE);
        if (!irq_mode) {
            if (irq_owned) {
                HAL_NVIC_DisableIRQ(irqn);
            }
            HAL_GPIO_DeInit(priv->port, pin_mask);
            gpio_exti_clear_pending(pin_mask);
            if (irq_owned) {
                HAL_NVIC_ClearPendingIRQ(irqn);
            }
            /* Release EXTI ownership */
            mr_waitable_cancel_all(&line->events);
            line->event_enabled = 0U;
            line->mode = 0U;
            line->drv = MR_NULL;
        }
    }

    if (config->mode == MR_GPIO_MODE_OUTPUT) {
        /* Preload output to avoid a mode-switch glitch */
        HAL_GPIO_WritePin(priv->port, pin_mask,
                          (config->level != 0U) ? GPIO_PIN_SET
                                                : GPIO_PIN_RESET);
    }

    /* Init GPIO */
    gpio_init.Pin = pin_mask;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Pull = __gpio_hal_pull[config->pull];
    HAL_GPIO_Init(priv->port, &gpio_init);
    if (irq_mode) {
        /* Restore EXTI mask after HAL init */
        gpio_exti_it(pin, MR_FALSE);
        gpio_exti_clear_pending(pin_mask);

        /* Update EXTI ownership */
        line->mode = config->mode;
        line->drv = drv;

        /* Init EXTI IRQ */
        if (!irq_claimed && irq_owned) {
            HAL_NVIC_SetPriority(irqn, priv->irq_priority, 0U);
            HAL_NVIC_ClearPendingIRQ(irqn);
            HAL_NVIC_EnableIRQ(irqn);
        }

        /* Restore EXTI event */
        if (line->event_enabled != 0U) {
            gpio_exti_it(pin, MR_TRUE);
        }
    }
    ret = 0;
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__gpio_lock, mask);
    return ret;
}

static mr_err_t gpio_pin_configuration(const mr_driver_t *drv, mr_uint32_t pin,
                                       mr_gpio_config_t *config) {
#if defined(STM32_GPIO_HAS_MODER)
    gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    uint32_t shift, moder, pupdr;
    int mask;
#endif /* defined(STM32_GPIO_HAS_MODER) */

    /* Check parameter */
    if (pin >= 16U) {
        return -MR_EINVAL;
    }

#if !defined(STM32_GPIO_HAS_MODER)
    MR_IGNORE(drv);
    MR_IGNORE(config);
    return -MR_ENOSYS;
#else
    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__gpio_lock);

    /* HAL has no query API, read GPIO registers */
    shift = pin << 1U;
    moder = (priv->port->MODER >> shift) & 0x03UL;

    /* Check alternate mode */
    if (moder == 0x2UL) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__gpio_lock, mask);
        return -MR_ENOSYS;
    }

    /* Init pin configuration */
    config->alt = MR_GPIO_ALT_DISABLE;
    config->level = 0U;
    config->otype = MR_GPIO_OTYPE_PP;
    config->pull = MR_GPIO_PULL_NONE;

    /* Decode pin mode */
    if (moder == 0x3UL) {
        config->mode = MR_GPIO_MODE_ANALOG;
    } else {
        pupdr = (priv->port->PUPDR >> shift) & 0x03UL;
        config->pull = __gpio_pull_from_pupdr[pupdr];
        if (moder == 0x1UL) {
            config->mode = MR_GPIO_MODE_OUTPUT;
            config->otype = ((priv->port->OTYPER & (1UL << pin)) != 0U)
                              ? MR_GPIO_OTYPE_OD
                              : MR_GPIO_OTYPE_PP;
            config->level = ((priv->port->ODR & (1UL << pin)) != 0U) ? 1U : 0U;
        } else {
            config->mode = (drv == __gpio_irq_map[pin].drv)
                             ? __gpio_irq_map[pin].mode
                             : MR_GPIO_MODE_INPUT;
        }
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__gpio_lock, mask);
    return 0;
#endif /* defined(STM32_GPIO_HAS_MODER) */
}

static mr_err_t gpio_pin_write(const mr_driver_t *drv, mr_uint32_t pin,
                               mr_uint8_t value) {
    gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    GPIO_PinState state;

    /* Check parameter */
    if (pin >= 16U) {
        return -MR_EINVAL;
    }

    /* Write pin value */
    state = (value != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    HAL_GPIO_WritePin(priv->port, (uint16_t)(1UL << pin), state);
    return 0;
}

static mr_err_t gpio_pin_read(const mr_driver_t *drv, mr_uint32_t pin,
                              mr_uint8_t *value) {
    gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    GPIO_PinState state;

    /* Check parameter */
    if (pin >= 16U) {
        return -MR_EINVAL;
    }

    /* Read pin value */
    state = HAL_GPIO_ReadPin(priv->port, (uint16_t)(1UL << pin));
    *value = (state == GPIO_PIN_SET) ? 1U : 0U;
    return 0;
}

static mr_err_t gpio_port_write(const mr_driver_t *drv, mr_uint32_t mask) {
    gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    uint16_t pin_reset, pin_set;

    /* Write port atomically */
    pin_reset = (uint16_t)(~mask & 0xFFFFUL);
    pin_set = (uint16_t)(mask & 0xFFFFUL);
    priv->port->BSRR = ((uint32_t)pin_reset << 16U) | pin_set;
    return 0;
}

static mr_err_t gpio_port_read(const mr_driver_t *drv, mr_uint32_t *mask) {
    gpio_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* HAL has no port-read API, read IDR */
    *mask = priv->port->IDR & 0xFFFFUL;
    return 0;
}

static mr_err_t gpio_event_attach(const mr_driver_t *drv, mr_uint32_t pin,
                                  mr_waiter_t *entry) {
    gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;

    /* Check parameter */
    if (pin >= 16U) {
        return -MR_EINVAL;
    }
    if ((priv->exti_mask & (1U << pin)) == 0U) {
        return -MR_EPERM;
    }

    /* Init EXTI event */
    if (!MR_OBJECT_IS_INITED((mr_object_t *)&__gpio_irq_map[pin].events)) {
        ret = mr_waitable_init(&__gpio_irq_map[pin].events);
        if (ret < 0) {
            return ret;
        }
    }

    /* Attach EXTI event */
    return mr_waiter_wait(entry, &__gpio_irq_map[pin].events);
}

static mr_err_t gpio_event_control(const mr_driver_t *drv, mr_uint32_t pin,
                                   mr_uint32_t events, mr_bool_t enable) {
    gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;
    int mask;

    /* Check parameter */
    if (pin >= 16U) {
        return -MR_EINVAL;
    }
    if ((events == 0U) || ((events & ~MR_GPIO_WAIT_EXTI) != 0U)) {
        return -MR_EINVAL;
    }
    if ((priv->exti_mask & (1U << pin)) == 0U) {
        return -MR_EPERM;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__gpio_lock);

    /* Control EXTI interrupt */
    if (__gpio_irq_map[pin].drv == drv) {
        __gpio_irq_map[pin].event_enabled = enable ? 1U : 0U;
        gpio_exti_it(pin, enable);
        ret = 0;
    } else {
        ret = -MR_EPERM;
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__gpio_lock, mask);
    return ret;
}

mr_err_t mr_stm32_gpio_pin_alternate(const mr_ht_gpio_spec_t *spec,
                                     mr_uint32_t af, mr_gpio_pull_t pull,
                                     mr_gpio_otype_t otype) {
    const mr_driver_t *drv;
#if defined(STM32_GPIO_HAS_AFR)
    GPIO_InitTypeDef gpio_init = {0};
    gpio_priv_t *priv;
    mr_err_t ret;
    int mask;
#endif /* defined(STM32_GPIO_HAS_AFR) */

    /* Check parameter */
    if ((spec == MR_NULL) || (spec->drv == MR_NULL)) {
        return -MR_EINVAL;
    }

    /* Get GPIO driver */
    drv = spec->drv;

    /* Check GPIO parameter */
    if ((MR_DRIVER_OPS(drv) != &__gpio_ops) || (spec->pin >= 16U) || (af > 15U)
        || ((mr_uint32_t)pull > MR_GPIO_PULL_DOWN)
        || ((mr_uint32_t)otype > MR_GPIO_OTYPE_OD)) {
        return -MR_EINVAL;
    }

#if !defined(STM32_GPIO_HAS_AFR)
    /* Legacy GPIO needs direction and remap data */
    return -MR_ENOSYS;
#else

    /* Get GPIO private data */
    priv = MR_DRIVER_PRIV(drv);

    /* Check GPIO clock */
    if (!priv->clk_enable) {
        return -MR_ENODEV;
    }

    /* Build GPIO configuration */
    gpio_init.Pin = (uint16_t)(1UL << spec->pin);
    gpio_init.Mode
        = (otype == MR_GPIO_OTYPE_PP) ? GPIO_MODE_AF_PP : GPIO_MODE_AF_OD;
    gpio_init.Pull = __gpio_hal_pull[pull];
#if defined(GPIO_SPEED_FREQ_VERY_HIGH)
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
#else
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
#endif /* defined(GPIO_SPEED_FREQ_VERY_HIGH) */
    gpio_init.Alternate = af;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__gpio_lock);

    /* Check EXTI ownership */
    if (__gpio_irq_map[spec->pin].drv == drv) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Enable GPIO clock */
    priv->clk_enable();

    /* Init GPIO */
    HAL_GPIO_Init(priv->port, &gpio_init);
    ret = 0;
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__gpio_lock, mask);
    return ret;
#endif /* !defined(STM32_GPIO_HAS_AFR) */
}

mr_err_t mr_stm32_gpio_pin_release(const mr_ht_gpio_spec_t *spec) {
    const mr_driver_t *drv;
    gpio_priv_t *priv;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    if ((spec == MR_NULL) || (spec->drv == MR_NULL)) {
        return -MR_EINVAL;
    }

    /* Get GPIO driver */
    drv = spec->drv;

    /* Check GPIO parameter */
    if ((MR_DRIVER_OPS(drv) != &__gpio_ops) || (spec->pin >= 16U)) {
        return -MR_EINVAL;
    }

    /* Get GPIO private data */
    priv = MR_DRIVER_PRIV(drv);

    /* Check GPIO clock */
    if (priv->clk_enable == MR_NULL) {
        return -MR_ENODEV;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__gpio_lock);

    /* Check EXTI ownership */
    if (__gpio_irq_map[spec->pin].drv == drv) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Enable GPIO clock */
    priv->clk_enable();

    /* Deinit GPIO */
    HAL_GPIO_DeInit(priv->port, (uint16_t)(1UL << spec->pin));
    ret = 0;
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__gpio_lock, mask);
    return ret;
}

static void gpio_irq_handle(mr_uint32_t pin) {
    mr_gpio_event_data_t data;
    mr_driver_t *drv;

    /* Check parameter */
    if (pin >= 16U) {
        return;
    }

    /* Get EXTI owner */
    drv = (mr_driver_t *)__gpio_irq_map[pin].drv;

    /* Check event state */
    if ((drv == MR_NULL) || (!__gpio_irq_map[pin].event_enabled)) {
        return;
    }

    /* Raise GPIO EXTI event */
    data.pin = pin;
    mr_waitable_raise(&__gpio_irq_map[pin].events, MR_GPIO_WAIT_EXTI, &data);
}

#if defined(__HAL_GPIO_EXTI_CLEAR_RISING_IT)                                   \
    && defined(__HAL_GPIO_EXTI_CLEAR_FALLING_IT)
void HAL_GPIO_EXTI_Rising_Callback(uint16_t gpio_pin) {
    gpio_irq_handle(gpio_pin_from_mask(gpio_pin));
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t gpio_pin) {
    gpio_irq_handle(gpio_pin_from_mask(gpio_pin));
}
#else
void HAL_GPIO_EXTI_Callback(uint16_t gpio_pin) {
    gpio_irq_handle(gpio_pin_from_mask(gpio_pin));
}
#endif /* defined(__HAL_GPIO_EXTI_CLEAR_RISING_IT)                             \
    && defined(__HAL_GPIO_EXTI_CLEAR_FALLING_IT) */

/* Instance install */
#define ST_HT_GPIO_INSTALL(_id, _ops)                                          \
    static void MR_CAT(__gpio_clk_enable_, _id)(void) {                        \
        MR_HT_DRIVER_PROP(_id, st_clken)();                                    \
    }                                                                          \
    static const IRQn_Type MR_CAT(__gpio_irqns_, _id)[16] = {                  \
        __ST_HT_GPIO_IRQN_INIT(_id, 0) __ST_HT_GPIO_IRQN_INIT(_id, 1)          \
            __ST_HT_GPIO_IRQN_INIT(_id, 2) __ST_HT_GPIO_IRQN_INIT(_id, 3)      \
                __ST_HT_GPIO_IRQN_INIT(_id, 4) __ST_HT_GPIO_IRQN_INIT(_id, 5)  \
                    __ST_HT_GPIO_IRQN_INIT(_id, 6) __ST_HT_GPIO_IRQN_INIT(     \
                        _id, 7) __ST_HT_GPIO_IRQN_INIT(_id, 8)                 \
                        __ST_HT_GPIO_IRQN_INIT(_id, 9) __ST_HT_GPIO_IRQN_INIT( \
                            _id, 10) __ST_HT_GPIO_IRQN_INIT(_id, 11)           \
                            __ST_HT_GPIO_IRQN_INIT(_id, 12)                    \
                                __ST_HT_GPIO_IRQN_INIT(_id, 13)                \
                                    __ST_HT_GPIO_IRQN_INIT(_id, 14)            \
                                        __ST_HT_GPIO_IRQN_INIT(_id, 15)};      \
    static gpio_priv_t MR_CAT(__gpio_priv_, _id) = {                           \
        .port = MR_HT_DRIVER_PROP(_id, st_gpiox),                              \
        .clk_enable = MR_CAT(__gpio_clk_enable_, _id),                         \
        .irqns = MR_CAT(__gpio_irqns_, _id),                                   \
        .irq_priority = MR_HT_DRIVER_PROP_OR(_id, st_irq_priority, 0U),        \
        .irq_owned_mask = __ST_HT_GPIO_IRQ_OWNED_MASK(_id),                    \
        .exti_mask = __ST_HT_GPIO_EXTI_MASK(_id),                              \
    };                                                                         \
    __ST_HT_GPIO_EXTI_IRQ(_id, 0)                                              \
    __ST_HT_GPIO_EXTI_IRQ(_id, 1)                                              \
    __ST_HT_GPIO_EXTI_IRQ(_id, 2)                                              \
    __ST_HT_GPIO_EXTI_IRQ(_id, 3)                                              \
    __ST_HT_GPIO_EXTI_IRQ(_id, 4)                                              \
    __ST_HT_GPIO_EXTI_IRQ(_id, 5)                                              \
    __ST_HT_GPIO_EXTI_IRQ(_id, 6)                                              \
    __ST_HT_GPIO_EXTI_IRQ(_id, 7)                                              \
    __ST_HT_GPIO_EXTI_IRQ(_id, 8)                                              \
    __ST_HT_GPIO_EXTI_IRQ(_id, 9)                                              \
    __ST_HT_GPIO_EXTI_IRQ(_id, 10)                                             \
    __ST_HT_GPIO_EXTI_IRQ(_id, 11)                                             \
    __ST_HT_GPIO_EXTI_IRQ(_id, 12)                                             \
    __ST_HT_GPIO_EXTI_IRQ(_id, 13)                                             \
    __ST_HT_GPIO_EXTI_IRQ(_id, 14)                                             \
    __ST_HT_GPIO_EXTI_IRQ(_id, 15)                                             \
    MR_HT_GPIO_INSTALL(_id, &MR_CAT(__gpio_priv_, _id), _ops)
#define __ST_HT_GPIO_IRQ_OWNED_BIT(_id, _n)                                    \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, MR_CAT(st_irq_handler_, _n)),       \
                (| (1U << (_n))), ())
#define __ST_HT_GPIO_IRQ_OWNED_MASK(_id)                                       \
    (0U __ST_HT_GPIO_IRQ_OWNED_BIT(_id, 0) __ST_HT_GPIO_IRQ_OWNED_BIT(         \
        _id, 1) __ST_HT_GPIO_IRQ_OWNED_BIT(_id, 2)                             \
         __ST_HT_GPIO_IRQ_OWNED_BIT(_id, 3) __ST_HT_GPIO_IRQ_OWNED_BIT(        \
             _id, 4) __ST_HT_GPIO_IRQ_OWNED_BIT(_id, 5)                        \
             __ST_HT_GPIO_IRQ_OWNED_BIT(_id, 6) __ST_HT_GPIO_IRQ_OWNED_BIT(    \
                 _id, 7) __ST_HT_GPIO_IRQ_OWNED_BIT(_id, 8)                    \
                 __ST_HT_GPIO_IRQ_OWNED_BIT(_id, 9)                            \
                     __ST_HT_GPIO_IRQ_OWNED_BIT(_id, 10)                       \
                         __ST_HT_GPIO_IRQ_OWNED_BIT(_id, 11)                   \
                             __ST_HT_GPIO_IRQ_OWNED_BIT(_id, 12)               \
                                 __ST_HT_GPIO_IRQ_OWNED_BIT(_id, 13)           \
                                     __ST_HT_GPIO_IRQ_OWNED_BIT(_id, 14)       \
                                         __ST_HT_GPIO_IRQ_OWNED_BIT(_id, 15))
#define __ST_HT_GPIO_EXTI_MASK(_id)                                            \
    MR_HT_DRIVER_PROP_OR(_id, st_exti_mask, __ST_HT_GPIO_IRQ_OWNED_MASK(_id))
#define __ST_HT_GPIO_IRQN(_id, _n)                                             \
    MR_HT_DRIVER_PROP_OR(_id, MR_CAT(st_irqn_, _n), EXTI##_n##_IRQn)
#define __ST_HT_GPIO_IRQN_INIT(_id, _n)                                        \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, MR_CAT(st_irq_handler_, _n)),       \
                ([_n] = __ST_HT_GPIO_IRQN(_id, _n), ), ())
#define __ST_HT_GPIO_EXTI_IRQ(_id, _n)                                         \
    MR_MACRO_IF(                                                               \
        MR_HT_DRIVER_HAS_PROP(_id, MR_CAT(st_irq_handler_, _n)),               \
        (void MR_HT_DRIVER_PROP(_id, MR_CAT(st_irq_handler_, _n))(void) {      \
            HAL_GPIO_EXTI_IRQHandler(MR_CAT(GPIO_PIN_, _n));                   \
        }),                                                                    \
        ())
MR_HT_DRIVER_FOREACH(ST_HT_GPIO_INSTALL, &__gpio_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_GPIO) */

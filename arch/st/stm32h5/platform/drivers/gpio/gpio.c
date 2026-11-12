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

/* HardTree compatible */
#define MR_HT_COMPAT                    st_stm32h5_gpio
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

/* GPIO private data type */
typedef struct gpio_priv {
    GPIO_TypeDef *port;
    void (*clk_enable)(void);
    mr_uint32_t irq_priority;
    mr_uint32_t exti_mask;
} gpio_priv_t;

/* GPIO IRQ map type */
typedef struct gpio_irq_map {
    const mr_driver_t *drv;
    mr_uint8_t event_enabled;
    mr_waitable_t events;
    mr_uint8_t mode;
} gpio_irq_map_t;

/* Protect EXTI ownership and shared GPIO register updates */
static mr_spinlock_t __gpio_lock = MR_SPINLOCK_INIT();
/* EXTI lines are chip-global and share one IRQ map */
static gpio_irq_map_t __gpio_irq_map[16];

/* STM32H5 has one dedicated NVIC IRQ per EXTI line */
static const IRQn_Type __gpio_irqn[] = {
    EXTI0_IRQn,  EXTI1_IRQn,  EXTI2_IRQn,  EXTI3_IRQn,
    EXTI4_IRQn,  EXTI5_IRQn,  EXTI6_IRQn,  EXTI7_IRQn,
    EXTI8_IRQn,  EXTI9_IRQn,  EXTI10_IRQn, EXTI11_IRQn,
    EXTI12_IRQn, EXTI13_IRQn, EXTI14_IRQn, EXTI15_IRQn,
};

/* PUPDR to mr_gpio_pull_t map (encoding 3 is reserved) */
static const mr_uint8_t __gpio_pull_from_pupdr[] = {
    MR_GPIO_PULL_NONE,
    MR_GPIO_PULL_UP,
    MR_GPIO_PULL_DOWN,
    MR_GPIO_PULL_NONE,
};
/* mr_gpio_pull_t to HAL pull map */
static const uint32_t __gpio_hal_pull[] = {
    GPIO_NOPULL,
    GPIO_PULLUP,
    GPIO_PULLDOWN,
};

static void gpio_exti_it(mr_uint32_t pin, mr_bool_t enable) {
    if (enable) {
        SET_BIT(EXTI->IMR1, (1UL << pin));
    } else {
        CLEAR_BIT(EXTI->IMR1, (1UL << pin));
    }
}

static void gpio_exti_clear_pending(mr_uint32_t pin, uint16_t pin_mask) {
    /* EXTI pending registers clear only the bits written as one */
    EXTI->RPR1 = pin_mask;
    EXTI->FPR1 = pin_mask;
    HAL_NVIC_ClearPendingIRQ(__gpio_irqn[pin]);
}

static mr_uint32_t gpio_pin_from_mask(uint16_t pin_mask) {
    if (pin_mask == 0U) {
        return 16U;
    }
    /* HAL callbacks supply a single-bit GPIO_PIN_x mask */
    return 31U - (mr_uint32_t)__CLZ((uint32_t)pin_mask);
}

static mr_err_t gpio_pin_configure(const mr_driver_t *drv, mr_uint32_t pin,
                                   const mr_gpio_config_t *config) {
    gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    GPIO_InitTypeDef gpio_init = {0};
    mr_bool_t irq_claimed, irq_mode;
    gpio_irq_map_t *line;
    uint16_t pin_mask;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    if (pin >= 16U) {
        return -MR_EINVAL;
    }
    if (config->alt != MR_GPIO_ALT_DISABLE) {
        /* STM32H5 alternate-function mode also needs an AF number, which the
         * shared mr_gpio_config_t does not carry */
        return -MR_ENOSYS;
    }
    if (config->pull > MR_GPIO_PULL_DOWN) {
        return -MR_EINVAL;
    }
    pin_mask = (uint16_t)(1UL << pin);
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
            /* STM32 EXTI only supports edge triggering */
            return -MR_ENOSYS;
        }
        default: {
            return -MR_EINVAL;
        }
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__gpio_lock);

    /* Check ownership before another port can claim the line */
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

    /* Prepare the waitable before routing the hardware interrupt */
    if (irq_mode && !MR_OBJECT_IS_INITED((mr_object_t *)&line->events)) {
        ret = mr_waitable_init(&line->events);
        if (ret < 0) {
            goto _exit;
        }
    }

    /* Mask the old line before changing its hardware configuration */
    priv->clk_enable();
    if (irq_claimed) {
        gpio_exti_it(pin, MR_FALSE);
        if (!irq_mode) {
            HAL_NVIC_DisableIRQ(__gpio_irqn[pin]);
            HAL_GPIO_DeInit(priv->port, pin_mask);
            gpio_exti_clear_pending(pin, pin_mask);
            /* Remove old subscribers before another port claims the line */
            mr_waitable_cancel_all(&line->events);
            line->event_enabled = 0U;
            line->mode = 0U;
            line->drv = MR_NULL;
        }
    }

    if (config->mode == MR_GPIO_MODE_OUTPUT) {
        /* Preload the output level to avoid a glitch on mode switch */
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
        /* Mask the HAL-enabled line and clear stale edges before rearming */
        gpio_exti_it(pin, MR_FALSE);
        gpio_exti_clear_pending(pin, pin_mask);
        line->mode = config->mode;
        line->drv = drv;
        if (!irq_claimed) {
            HAL_NVIC_SetPriority(__gpio_irqn[pin], priv->irq_priority, 0U);
            HAL_NVIC_EnableIRQ(__gpio_irqn[pin]);
        }
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
    gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    uint32_t shift, moder, pupdr;
    int mask;

    /* Check parameter */
    if (pin >= 16U) {
        return -MR_EINVAL;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__gpio_lock);

    /* HAL has no configuration query; read the relevant registers */
    shift = pin << 1U;
    moder = (priv->port->MODER >> shift) & 0x03UL;
    if (moder == 0x2UL) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__gpio_lock, mask);
        return -MR_ENOSYS;
    }

    /* Decode pin mode and check the EXTI owner */
    config->alt = MR_GPIO_ALT_DISABLE;
    config->level = 0U;
    config->otype = MR_GPIO_OTYPE_PP;
    config->pull = MR_GPIO_PULL_NONE;
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
}

static mr_err_t gpio_pin_write(const mr_driver_t *drv, mr_uint32_t pin,
                               mr_uint8_t value) {
    gpio_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check parameter */
    if (pin >= 16U) {
        return -MR_EINVAL;
    }

    /* Write pin value */
    HAL_GPIO_WritePin(priv->port, (uint16_t)(1UL << pin),
                      (value != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
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

    /* HAL sets and resets both pin groups in one BSRR transaction */
    pin_reset = (uint16_t)(~mask & 0xFFFFUL);
    pin_set = (uint16_t)(mask & 0xFFFFUL);
    HAL_GPIO_WriteMultipleStatePin(priv->port, pin_reset, pin_set);
    return 0;
}

static mr_err_t gpio_port_read(const mr_driver_t *drv, mr_uint32_t *mask) {
    gpio_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* HAL has no port-read API, so read IDR directly */
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

    /* Init the event before the pin is configured */
    if (!MR_OBJECT_IS_INITED((mr_object_t *)&__gpio_irq_map[pin].events)) {
        ret = mr_waitable_init(&__gpio_irq_map[pin].events);
        if (ret < 0) {
            return ret;
        }
    }

    /* Attach GPIO EXTI event */
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

    /* Switch the EXTI interrupt mask without re-init */
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

mr_err_t mr_stm32h5_gpio_pin_alternate(const mr_ht_gpio_spec_t *spec,
                                       mr_uint32_t af, mr_gpio_pull_t pull,
                                       mr_gpio_otype_t otype) {
    GPIO_InitTypeDef gpio_init = {0};
    const mr_driver_t *drv;
    gpio_priv_t *priv;
    int mask;

    /* Check parameter */
    if ((spec == MR_NULL) || (spec->drv == MR_NULL)) {
        return -MR_EINVAL;
    }
    drv = spec->drv;
    if ((MR_DRIVER_OPS(drv) != &__gpio_ops) || (spec->pin >= 16U) || (af > 15U)
        || ((mr_uint32_t)pull > MR_GPIO_PULL_DOWN)
        || ((mr_uint32_t)otype > MR_GPIO_OTYPE_OD)) {
        return -MR_EINVAL;
    }
    priv = MR_DRIVER_PRIV(drv);
    if (priv->clk_enable == MR_NULL) {
        return -MR_ENODEV;
    }

    /* Prepare the alternate-function configuration */
    gpio_init.Pin = (uint16_t)(1UL << spec->pin);
    gpio_init.Mode
        = (otype == MR_GPIO_OTYPE_PP) ? GPIO_MODE_AF_PP : GPIO_MODE_AF_OD;
    gpio_init.Pull = __gpio_hal_pull[pull];
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = af;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__gpio_lock);

    /* AF programming does not change the EXTI routing of another port */
    if (__gpio_irq_map[spec->pin].drv == drv) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__gpio_lock, mask);
        return -MR_EBUSY;
    }
    priv->clk_enable();
    HAL_GPIO_Init(priv->port, &gpio_init);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__gpio_lock, mask);
    return 0;
}

mr_err_t mr_stm32h5_gpio_pin_release(const mr_ht_gpio_spec_t *spec) {
    const mr_driver_t *drv;
    gpio_priv_t *priv;
    int mask;

    /* Check parameter */
    if ((spec == MR_NULL) || (spec->drv == MR_NULL)) {
        return -MR_EINVAL;
    }
    drv = spec->drv;
    if ((MR_DRIVER_OPS(drv) != &__gpio_ops) || (spec->pin >= 16U)) {
        return -MR_EINVAL;
    }
    priv = MR_DRIVER_PRIV(drv);
    if (priv->clk_enable == MR_NULL) {
        return -MR_ENODEV;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__gpio_lock);

    /* HAL only clears EXTI configuration if the line is routed to this port */
    if (__gpio_irq_map[spec->pin].drv == drv) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__gpio_lock, mask);
        return -MR_EBUSY;
    }
    priv->clk_enable();
    HAL_GPIO_DeInit(priv->port, (uint16_t)(1UL << spec->pin));

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__gpio_lock, mask);
    return 0;
}

static void gpio_irq_handle(mr_uint32_t pin) {
    mr_gpio_event_data_t data;
    mr_driver_t *drv;

    /* Check the EXTI line and event state */
    if (pin >= 16U) {
        return;
    }
    drv = (mr_driver_t *)__gpio_irq_map[pin].drv;
    if ((drv == MR_NULL) || (!__gpio_irq_map[pin].event_enabled)) {
        return;
    }

    /* Raise GPIO EXTI event */
    data.pin = pin;
    mr_waitable_raise(&__gpio_irq_map[pin].events, MR_GPIO_WAIT_EXTI, &data);
}

void HAL_GPIO_EXTI_Rising_Callback(uint16_t gpio_pin) {
    gpio_irq_handle(gpio_pin_from_mask(gpio_pin));
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t gpio_pin) {
    gpio_irq_handle(gpio_pin_from_mask(gpio_pin));
}

/* Instance install */
#define ST_HT_GPIO_INSTALL(_id, _ops)                                          \
    static void MR_CAT(__gpio_clk_enable_, _id)(void) {                        \
        MR_HT_DRIVER_PROP(_id, st_clken)();                                    \
    }                                                                          \
    static gpio_priv_t MR_CAT(__gpio_priv_, _id) = {                           \
        .port = MR_HT_DRIVER_PROP(_id, st_gpiox),                              \
        .clk_enable = MR_CAT(__gpio_clk_enable_, _id),                         \
        .irq_priority = MR_HT_DRIVER_PROP(_id, st_irq_priority),               \
        .exti_mask                                                             \
        = (0U __ST_HT_GPIO_EXTI_BIT(_id, 0) __ST_HT_GPIO_EXTI_BIT(             \
            _id, 1) __ST_HT_GPIO_EXTI_BIT(_id, 2) __ST_HT_GPIO_EXTI_BIT(_id,   \
                                                                        3)     \
               __ST_HT_GPIO_EXTI_BIT(_id, 4) __ST_HT_GPIO_EXTI_BIT(_id, 5)     \
                   __ST_HT_GPIO_EXTI_BIT(_id, 6) __ST_HT_GPIO_EXTI_BIT(_id, 7) \
                       __ST_HT_GPIO_EXTI_BIT(_id, 8) __ST_HT_GPIO_EXTI_BIT(    \
                           _id, 9) __ST_HT_GPIO_EXTI_BIT(_id, 10)              \
                           __ST_HT_GPIO_EXTI_BIT(_id, 11)                      \
                               __ST_HT_GPIO_EXTI_BIT(_id, 12)                  \
                                   __ST_HT_GPIO_EXTI_BIT(_id, 13)              \
                                       __ST_HT_GPIO_EXTI_BIT(_id, 14)          \
                                           __ST_HT_GPIO_EXTI_BIT(_id, 15)),    \
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
#define __ST_HT_GPIO_EXTI_BIT(_id, _n)                                         \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, MR_CAT(st_irq_handler_, _n)),       \
                (| (1U << (_n))), ())
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

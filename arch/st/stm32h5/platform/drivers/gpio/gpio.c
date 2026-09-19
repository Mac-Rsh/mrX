/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-12        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_GPIO)
#include <mrX/drivers/gpio/mr_gpio.h>

/* HardTree compatible */
#define MR_HT_COMPAT st_stm32h5_gpio

/* GPIO operations */
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

/* GPIO private data */
typedef struct gpio_priv {
    GPIO_TypeDef *port;
    void (*clk_enable)(void); /* Board-provided HAL clock-enable macro */
    mr_uint32_t irq_priority;
} gpio_priv_t;

/* GPIO IRQ map */
typedef struct gpio_irq_map {
    const mr_driver_t *drv;
    mr_uint8_t mode;
    mr_uint8_t event_enabled;
} gpio_irq_map_t;

static gpio_irq_map_t __gpio_irq_map[16];
/* GPIO EXTI waitables */
static mr_waitable_t __gpio_exti[16];

/* STM32H5 has one dedicated NVIC IRQ per EXTI line */
static const IRQn_Type __gpio_irqn[] = {
    EXTI0_IRQn,  EXTI1_IRQn,  EXTI2_IRQn,  EXTI3_IRQn, EXTI4_IRQn,  EXTI5_IRQn,
    EXTI6_IRQn,  EXTI7_IRQn,  EXTI8_IRQn,  EXTI9_IRQn, EXTI10_IRQn, EXTI11_IRQn,
    EXTI12_IRQn, EXTI13_IRQn, EXTI14_IRQn, EXTI15_IRQn};

/* PUPDR[1:0] -> mr_gpio_pull_t; hardware value 3 is reserved */
static const mr_uint8_t __gpio_pull_from_pupdr[] = {
    MR_GPIO_PULL_NONE, MR_GPIO_PULL_UP, MR_GPIO_PULL_DOWN, MR_GPIO_PULL_NONE};

/* mr_gpio_pull_t -> HAL pull (caller has already rejected out-of-range) */
static const uint32_t __gpio_hal_pull[]
    = {GPIO_NOPULL, GPIO_PULLUP, GPIO_PULLDOWN};

/* Switch EXTI IMR1 without re-running HAL_GPIO_Init */
static void gpio_exti_it(mr_uint32_t pin, mr_bool_t enable) {
    if (enable) {
        SET_BIT(EXTI->IMR1, (1UL << pin));
    } else {
        CLEAR_BIT(EXTI->IMR1, (1UL << pin));
    }
}

/* Extract the EXTI line number from a HAL GPIO_PIN_x mask */
static mr_uint32_t gpio_pin_from_mask(uint16_t pin_mask) {
    if (pin_mask == 0U) {
        return 16U;
    }
    return 31U - (mr_uint32_t)__CLZ((uint32_t)pin_mask);
}

static mr_err_t gpio_pin_configure(const mr_driver_t *drv, mr_uint32_t pin,
                                   const mr_gpio_config_t *config) {
    gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    GPIO_InitTypeDef gpio_init = {0};
    mr_bool_t irq_release = MR_FALSE;
    mr_bool_t irq_claimed = MR_FALSE;
    mr_bool_t irq_mode = MR_FALSE;
    uint16_t pin_mask;

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

    /* Check EXTI line ownership before touching clocks or the irq map */
    if (irq_mode) {
        if ((__gpio_irq_map[pin].drv != MR_NULL)
            && (drv != __gpio_irq_map[pin].drv)) {
            return -MR_EBUSY;
        }
        irq_claimed = (__gpio_irq_map[pin].drv == drv);
    } else if (drv == __gpio_irq_map[pin].drv) {
        irq_release = MR_TRUE;
    }

    /* Enable GPIO clock through the board-provided HAL clock-enable */
    if (priv->clk_enable == MR_NULL) {
        return -MR_ENODEV;
    }
    priv->clk_enable();

    if (irq_mode) {
        /* Initialize the waitable */
        if (!MR_OBJECT_IS_INITED((mr_object_t *)&__gpio_exti[pin])) {
            mr_waitable_init(&__gpio_exti[pin]);
        }

        /* Mark the irq as used */
        __gpio_irq_map[pin].mode = config->mode;
        __gpio_irq_map[pin].drv = drv;
        if (!irq_claimed) {
            __gpio_irq_map[pin].event_enabled = 0U;
        }

        /* HAL_GPIO_Init below programs EXTICR, the edge registers and IMR1;
         * only the NVIC line is managed here, as with generated CubeMX code */
        if (!irq_claimed) {
            HAL_NVIC_SetPriority(__gpio_irqn[pin], priv->irq_priority, 0U);
            HAL_NVIC_EnableIRQ(__gpio_irqn[pin]);
        }
    } else if (irq_release) {
        /* Mark the irq as unused */
        __gpio_irq_map[pin].drv = MR_NULL;
        __gpio_irq_map[pin].mode = 0;
        __gpio_irq_map[pin].event_enabled = 0U;

        /* Reconfiguring away from IRQ mode: HAL_GPIO_DeInit resets the pin
         * and tears down the EXTI line this port owned (EXTICR, edges, IMR1) */
        HAL_GPIO_DeInit(priv->port, pin_mask);
        HAL_NVIC_DisableIRQ(__gpio_irqn[pin]);
    }

    if (config->mode == MR_GPIO_MODE_OUTPUT) {
        /* Write the initial level before switching the mode, so the pin
         * comes up glitch-free at the requested level */
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
        /* HAL_GPIO_Init enables IMR1; keep it off until event_enable */
        gpio_exti_it(pin, __gpio_irq_map[pin].event_enabled != 0U);
    }
    return 0;
}

static mr_err_t gpio_pin_configuration(const mr_driver_t *drv, mr_uint32_t pin,
                                       mr_gpio_config_t *config) {
    gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    uint32_t pin_mask;
    uint32_t moder, pupdr, otyper;

    /* Check parameter */
    if (pin >= 16U) {
        return -MR_EINVAL;
    }
    pin_mask = (1UL << pin);

    /* The HAL has no configuration-query API, read the registers back */
    moder = (priv->port->MODER >> (pin << 1U)) & 0x03UL;
    pupdr = (priv->port->PUPDR >> (pin << 1U)) & 0x03UL;
    otyper = (priv->port->OTYPER >> pin) & 0x01UL;

    config->alt = MR_GPIO_ALT_DISABLE;
    config->level = 0U;
    switch (moder) {
        case 0x3UL: {
            config->mode = MR_GPIO_MODE_ANALOG;
            config->pull = MR_GPIO_PULL_NONE;
            config->otype = MR_GPIO_OTYPE_PP;
            break;
        }
        case 0x1UL: {
            config->mode = MR_GPIO_MODE_OUTPUT;
            config->otype
                = (otyper != 0U) ? MR_GPIO_OTYPE_OD : MR_GPIO_OTYPE_PP;
            config->pull = __gpio_pull_from_pupdr[pupdr];
            config->level = (priv->port->ODR & pin_mask) ? 1U : 0U;
            break;
        }
        case 0x2UL: {
            /* Alternate function mode is not configurable by this driver */
            return -MR_ENOSYS;
        }
        default: {
            config->mode = (drv == __gpio_irq_map[pin].drv)
                             ? __gpio_irq_map[pin].mode
                             : MR_GPIO_MODE_INPUT;
            config->otype = MR_GPIO_OTYPE_PP;
            config->pull = __gpio_pull_from_pupdr[pupdr];
            break;
        }
    }
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
    uint16_t pin_set = (uint16_t)(mask & 0xFFFFUL);
    uint16_t pin_reset = (uint16_t)(~mask & 0xFFFFUL);

    /* HAL sets and resets both pin groups in one BSRR transaction */
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
    /* Check whether this pin's EXTI is owned by this driver */
    if (pin >= 16U) {
        return -MR_EINVAL;
    }
    if (__gpio_irq_map[pin].drv != drv) {
        return -MR_EINVAL;
    }

    /* Attach GPIO EXTI event */
    return mr_waiter_wait(entry, &__gpio_exti[pin]);
}

static mr_err_t gpio_event_control(const mr_driver_t *drv, mr_uint32_t pin,
                                   mr_uint32_t events, mr_bool_t enable) {
    /* Check parameter */
    if (pin >= 16U) {
        return -MR_EINVAL;
    }
    if ((events == 0U) || ((events & ~MR_GPIO_WAIT_EXTI) != 0U)) {
        return -MR_EINVAL;
    }
    if (__gpio_irq_map[pin].drv != drv) {
        return -MR_EPERM;
    }

    /* Switch the EXTI interrupt mask without re-init */
    __gpio_irq_map[pin].event_enabled = enable ? 1U : 0U;
    gpio_exti_it(pin, enable);
    return 0;
}

static void gpio_irq_handle(mr_uint32_t pin) {
    mr_uint32_t data;
    mr_driver_t *drv;

    if (pin >= 16U) {
        return;
    }
    drv = (mr_driver_t *)__gpio_irq_map[pin].drv;
    if ((drv == MR_NULL) || !__gpio_irq_map[pin].event_enabled) {
        return;
    }

    /* Raise GPIO EXTI event */
    data = pin;
    mr_waitable_raise(&__gpio_exti[pin], MR_GPIO_WAIT_EXTI, &data);
}

/* Strong overrides ensure HAL's weak no-op callbacks reach the mrX waitable */
void HAL_GPIO_EXTI_Rising_Callback(uint16_t gpio_pin) {
    gpio_irq_handle(gpio_pin_from_mask(gpio_pin));
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t gpio_pin) {
    gpio_irq_handle(gpio_pin_from_mask(gpio_pin));
}

/* One NVIC line per EXTI line on STM32H5: dispatch through the HAL handler,
 * which clears the pending flag and invokes the callbacks above */
#define DRV_GPIO_IRQ_HANDLER(_n)                                               \
    void EXTI##_n##_IRQHandler(void) {                                         \
        HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_##_n);                               \
    }
DRV_GPIO_IRQ_HANDLER(0)
DRV_GPIO_IRQ_HANDLER(1)
DRV_GPIO_IRQ_HANDLER(2)
DRV_GPIO_IRQ_HANDLER(3)
DRV_GPIO_IRQ_HANDLER(4)
DRV_GPIO_IRQ_HANDLER(5)
DRV_GPIO_IRQ_HANDLER(6)
DRV_GPIO_IRQ_HANDLER(7)
DRV_GPIO_IRQ_HANDLER(8)
DRV_GPIO_IRQ_HANDLER(9)
DRV_GPIO_IRQ_HANDLER(10)
DRV_GPIO_IRQ_HANDLER(11)
DRV_GPIO_IRQ_HANDLER(12)
DRV_GPIO_IRQ_HANDLER(13)
DRV_GPIO_IRQ_HANDLER(14)
DRV_GPIO_IRQ_HANDLER(15)

/* Instance install */
#define ST_HT_GPIO_INSTALL(_id, _ops)                                          \
    static void MR_CAT(__gpio_clk_enable_, _id)(void) {                        \
        MR_HT_DRIVER_PROP(_id, st_clken)();                                    \
    }                                                                          \
    static gpio_priv_t MR_CAT(__gpio_priv_, _id)                               \
        = {.port = MR_HT_DRIVER_PROP(_id, st_gpiox),                           \
           .clk_enable = MR_CAT(__gpio_clk_enable_, _id),                      \
           .irq_priority = MR_HT_DRIVER_PROP(_id, st_irq_priority)};           \
    MR_HT_GPIO_INSTALL(_id, &MR_CAT(__gpio_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(ST_HT_GPIO_INSTALL, &__gpio_ops);
#endif /* defined(MR_USE_DRIVER_GPIO) */

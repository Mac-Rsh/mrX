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
#define MR_HT_COMPAT                    wch_ch32_gpio
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
static mr_err_t gpio_port_write(const mr_driver_t *drv, mr_uint32_t values);
static mr_err_t gpio_port_read(const mr_driver_t *drv, mr_uint32_t *values);
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

/* GPIO width definition */
#if defined(MR_USE_CH32V00X) || defined(MR_USE_CH32VM00X)
#define CH32_GPIO_PIN_COUNT             8U
#else
#define CH32_GPIO_PIN_COUNT             16U
#endif /* defined(MR_USE_CH32V00X) || defined(MR_USE_CH32VM00X) */
#define CH32_GPIO_PIN_MASK              ((1U << CH32_GPIO_PIN_COUNT) - 1U)

/* GPIO EXTI routing definition */
#if CH32_GPIO_PIN_COUNT == 8U
#define CH32_GPIO_ROUTE_MASK            0x3U
#else
#define CH32_GPIO_ROUTE_MASK            0xfU
#endif /* CH32_GPIO_PIN_COUNT == 8U */

/* GPIO input configuration definition */
#define CH32_GPIO_CFG_ANALOG            0x0U
#define CH32_GPIO_CFG_FLOAT             0x4U
#define CH32_GPIO_CFG_PULL              0x8U
#define CH32_GPIO_CFG_RESERVED          0xcU

/* GPIO output configuration definition */
#define CH32_GPIO_CFG_MODE_MASK         0x3U
#define CH32_GPIO_CFG_OPEN_DRAIN        0x4U
#define CH32_GPIO_CFG_ALTERNATE         0x8U
/* Output mode 1 (10 MHz) and mode 3 (30/50 MHz) are valid on all families */
#define CH32_GPIO_CFG_OUTPUT            0x1U
#define CH32_GPIO_CFG_OUTPUT_FAST       0x3U

/* GPIO EXTI group mask definition */
#define CH32_GPIO_EXTI_LOW              0x00ffU
#define CH32_GPIO_EXTI_MID              0x03e0U
#define CH32_GPIO_EXTI_HIGH             0xfc00U

/* GPIO private type */
typedef struct gpio_priv {
    GPIO_TypeDef *port;
    mr_uint32_t clock_mask;
    mr_uint32_t port_source;
    mr_uint32_t exti_mask;
} gpio_priv_t;

/* GPIO EXTI line type */
typedef struct gpio_exti_line {
    const mr_driver_t *owner;
    mr_waitable_t waitable;
    mr_uint8_t mode;
} gpio_exti_line_t;

/* GPIO EXTI vector type */
typedef struct gpio_exti_vector {
    IRQn_Type irqn;
    mr_uint32_t mask;
} gpio_exti_vector_t;

/* GPIO halfword register access type */
typedef uint16_t gpio_halfword_t __attribute__((may_alias));

/* GPIO lock */
static mr_spinlock_t __gpio_lock = MR_SPINLOCK_INIT();
/* GPIO EXTI line map */
static gpio_exti_line_t __gpio_exti_lines[CH32_GPIO_PIN_COUNT];

/* GPIO EXTI vector map */
static const gpio_exti_vector_t __gpio_exti_vectors[] = {
#if CH32_GPIO_PIN_COUNT == 8U
    {.irqn = EXTI7_0_IRQn, .mask = CH32_GPIO_EXTI_LOW},
#else
    {.irqn = EXTI0_IRQn, .mask = 1U << 0},
    {.irqn = EXTI1_IRQn, .mask = 1U << 1},
    {.irqn = EXTI2_IRQn, .mask = 1U << 2},
    {.irqn = EXTI3_IRQn, .mask = 1U << 3},
    {.irqn = EXTI4_IRQn, .mask = 1U << 4},
    {.irqn = EXTI9_5_IRQn, .mask = CH32_GPIO_EXTI_MID},
    {.irqn = EXTI15_10_IRQn, .mask = CH32_GPIO_EXTI_HIGH},
#endif /* CH32_GPIO_PIN_COUNT == 8U */
};

static void gpio_clock_enable(mr_uint32_t clock_mask) {
#if defined(MR_USE_CH32VM00X) || defined(MR_USE_CH32V205)                      \
    || defined(MR_USE_CH32V4X7)
    if ((RCC->PB2PCENR & clock_mask) != clock_mask) {
        RCC->PB2PCENR |= clock_mask;
    }
#else
    if ((RCC->APB2PCENR & clock_mask) != clock_mask) {
        RCC->APB2PCENR |= clock_mask;
    }
#endif /* defined(MR_USE_CH32VM00X) || defined(MR_USE_CH32V4X7) */
}

static volatile uint32_t *gpio_config_register(GPIO_TypeDef *port,
                                               mr_uint32_t pin) {
#if CH32_GPIO_PIN_COUNT == 16U
    if (pin >= 8U) {
        return &port->CFGHR;
    }
#else
    MR_IGNORE(pin);
#endif /* CH32_GPIO_PIN_COUNT == 16U */
    return &port->CFGLR;
}

static void gpio_config_write(GPIO_TypeDef *port, mr_uint32_t pin,
                              mr_uint32_t mode) {
    mr_uint32_t shift, config;
    volatile uint32_t *reg;

    /* Write the pin configuration field */
    reg = gpio_config_register(port, pin);
    shift = (pin & 7U) * 4U;
    config = *reg & ~(0xfU << shift);
    *reg = config | (mode << shift);
}

static mr_bool_t gpio_pin_locked(const GPIO_TypeDef *port, mr_uint32_t bit) {
    mr_uint32_t lock_bits;

    /* Read the pin lock sequence */
    lock_bits = GPIO_LCKK | bit;
    return (port->LCKR & lock_bits) == lock_bits;
}

static mr_uint16_t gpio_data_read(const volatile uint32_t *reg) {
    /* WCH specifies halfword accesses for the port data registers */
    return *(const volatile gpio_halfword_t *)reg;
}

static void gpio_data_write(volatile uint32_t *reg, mr_uint16_t value) {
    *(volatile gpio_halfword_t *)reg = value;
}

static void gpio_latch_write(GPIO_TypeDef *port, mr_uint32_t bit,
                             mr_bool_t high) {
    /* Write the output latch */
    gpio_data_write(high ? &port->BSHR : &port->BCR, (mr_uint16_t)bit);
}

static void gpio_input_configure(GPIO_TypeDef *port, mr_uint32_t pin,
                                 mr_gpio_pull_t pull) {
    /* Configure the floating input first */
    gpio_config_write(port, pin, CH32_GPIO_CFG_FLOAT);
    if (pull != MR_GPIO_PULL_NONE) {
        /* The latch selects the pull direction once the pin is an input */
        gpio_latch_write(port, 1U << pin, pull == MR_GPIO_PULL_UP);
        gpio_config_write(port, pin, CH32_GPIO_CFG_PULL);
    }
}

static const gpio_exti_vector_t *gpio_exti_vector(mr_uint32_t pin) {
#if CH32_GPIO_PIN_COUNT == 8U
    MR_IGNORE(pin);
    return &__gpio_exti_vectors[0];
#else
    if (pin < 5U) {
        return &__gpio_exti_vectors[pin];
    }
    return &__gpio_exti_vectors[(pin < 10U) ? 5 : 6];
#endif /* CH32_GPIO_PIN_COUNT == 8U */
}

static void gpio_exti_route(mr_uint32_t pin, mr_uint32_t port_source) {
    volatile uint32_t *route;
    mr_uint32_t shift;

#if CH32_GPIO_PIN_COUNT == 8U
    route = &AFIO->EXTICR;
    shift = pin * 2U;
#else
    route = &AFIO->EXTICR[pin / 4U];
    shift = (pin % 4U) * 4U;
#endif /* CH32_GPIO_PIN_COUNT == 8U */

    /* Route the EXTI line to the port */
    *route = (*route & ~(CH32_GPIO_ROUTE_MASK << shift))
           | ((port_source & CH32_GPIO_ROUTE_MASK) << shift);
}

static void gpio_exti_edges(mr_uint32_t bit, mr_gpio_mode_t mode) {
    /* Configure rising edge */
    if ((mode == MR_GPIO_MODE_IRQ_EDGE) || (mode == MR_GPIO_MODE_IRQ_RISING)) {
        EXTI->RTENR |= bit;
    } else {
        EXTI->RTENR &= ~bit;
    }

    /* Configure falling edge */
    if ((mode == MR_GPIO_MODE_IRQ_EDGE) || (mode == MR_GPIO_MODE_IRQ_FALLING)) {
        EXTI->FTENR |= bit;
    } else {
        EXTI->FTENR &= ~bit;
    }
}

static void gpio_exti_enable(mr_uint32_t pin, mr_bool_t enable) {
    const gpio_exti_vector_t *vector;
    mr_uint32_t bit;

    /* Control EXTI interrupt */
    vector = gpio_exti_vector(pin);
    bit = 1U << pin;
    /* A disabled line can still latch edges. Clear them before rearming */
    EXTI->INTENR &= ~bit;
    EXTI->INTFR = bit;
    if (enable) {
        EXTI->INTENR |= bit;
        NVIC_EnableIRQ(vector->irqn);
    } else if ((EXTI->INTENR & vector->mask) == 0U) {
        NVIC_DisableIRQ(vector->irqn);
        NVIC_ClearPendingIRQ(vector->irqn);
    }
}

static mr_err_t gpio_pin_configure(const mr_driver_t *drv, mr_uint32_t pin,
                                   const mr_gpio_config_t *config) {
    const gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_bool_t irq_mode, irq_enabled;
    gpio_exti_line_t *line;
    mr_uint32_t bit, mode;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    if ((pin >= CH32_GPIO_PIN_COUNT) || (config->pull > MR_GPIO_PULL_DOWN)) {
        return -MR_EINVAL;
    }
    if (config->alt != MR_GPIO_ALT_DISABLE) {
        return -MR_ENOSYS;
    }
    if ((config->mode == MR_GPIO_MODE_IRQ_HIGH)
        || (config->mode == MR_GPIO_MODE_IRQ_LOW)) {
        /* EXTI does not support level triggering */
        return -MR_ENOSYS;
    }
    if (((config->mode == MR_GPIO_MODE_OUTPUT)
         || (config->mode == MR_GPIO_MODE_ANALOG))
        && (config->pull != MR_GPIO_PULL_NONE)) {
        return -MR_ENOSYS;
    }

    /* Prepare IRQ state */
    bit = 1U << pin;
    irq_mode = (config->mode >= MR_GPIO_MODE_IRQ_RISING)
               && (config->mode <= MR_GPIO_MODE_IRQ_EDGE);
    if (irq_mode && ((priv->exti_mask & bit) == 0U)) {
        return -MR_EPERM;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__gpio_lock);

    /* Check EXTI ownership */
    line = &__gpio_exti_lines[pin];
    if (irq_mode && (line->owner != MR_NULL) && (line->owner != drv)) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Enable GPIO clock */
    gpio_clock_enable(priv->clock_mask);

    /* Check pin lock */
    if (gpio_pin_locked(priv->port, bit)) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Init EXTI event */
    if (irq_mode && !MR_OBJECT_IS_INITED(&line->waitable)) {
        ret = mr_waitable_init(&line->waitable);
        if (ret < 0) {
            goto _exit;
        }
    }

    /* Release old EXTI state */
    irq_enabled = MR_FALSE;
    if (line->owner == drv) {
        irq_enabled = (EXTI->INTENR & bit) != 0U;
        /* Mask this line while changing its pin or edge configuration */
        gpio_exti_enable(pin, MR_FALSE);
        if (!irq_mode) {
            gpio_exti_edges(bit, (mr_gpio_mode_t)config->mode);
            mr_waitable_cancel_all(&line->waitable);
            line->owner = MR_NULL;
            line->mode = 0U;
        }
    }

    /* Init GPIO */
    if (config->mode == MR_GPIO_MODE_OUTPUT) {
        /* Preload the latch before enabling the output driver */
        gpio_latch_write(priv->port, bit, config->level != 0U);
        mode = CH32_GPIO_CFG_OUTPUT;
        if (config->otype == MR_GPIO_OTYPE_OD) {
            mode |= CH32_GPIO_CFG_OPEN_DRAIN;
        }
        gpio_config_write(priv->port, pin, mode);
    } else if (config->mode == MR_GPIO_MODE_ANALOG) {
        gpio_config_write(priv->port, pin, CH32_GPIO_CFG_ANALOG);
    } else {
        gpio_input_configure(priv->port, pin, (mr_gpio_pull_t)config->pull);
    }
    if (irq_mode) {
        /* Init EXTI line */
        gpio_clock_enable(RCC_AFIOEN);
        gpio_exti_route(pin, priv->port_source);
        gpio_exti_edges(bit, (mr_gpio_mode_t)config->mode);
        line->mode = config->mode;
        line->owner = drv;
        gpio_exti_enable(pin, irq_enabled);
    }
    ret = 0;
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__gpio_lock, mask);
    return ret;
}

static mr_err_t gpio_pin_configuration(const mr_driver_t *drv, mr_uint32_t pin,
                                       mr_gpio_config_t *config) {
    const gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t bit, shift, mode;
    int mask;

    /* Check parameter */
    if (pin >= CH32_GPIO_PIN_COUNT) {
        return -MR_EINVAL;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__gpio_lock);
    gpio_clock_enable(priv->clock_mask);

    /* The SDK has no configuration query for the mode and pull */
    bit = 1U << pin;
    shift = (pin & 7U) * 4U;
    mode = (*gpio_config_register(priv->port, pin) >> shift) & 0xfU;
    if (mode == CH32_GPIO_CFG_RESERVED) {
        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__gpio_lock, mask);
        return -MR_ENOSYS;
    }

    /* Init pin configuration */
    config->alt = MR_GPIO_ALT_DISABLE;
    config->pull = MR_GPIO_PULL_NONE;
    config->otype = MR_GPIO_OTYPE_PP;
    config->level = MR_GPIO_LEVEL_LOW;

    /* Decode pin mode */
    if ((mode & CH32_GPIO_CFG_MODE_MASK) != 0U) {
        config->mode = MR_GPIO_MODE_OUTPUT;
        config->alt = ((mode & CH32_GPIO_CFG_ALTERNATE) != 0U)
                        ? MR_GPIO_ALT_ENABLE
                        : MR_GPIO_ALT_DISABLE;
        config->otype = ((mode & CH32_GPIO_CFG_OPEN_DRAIN) != 0U)
                          ? MR_GPIO_OTYPE_OD
                          : MR_GPIO_OTYPE_PP;
        if ((mode & CH32_GPIO_CFG_ALTERNATE) == 0U) {
            config->level = (gpio_data_read(&priv->port->OUTDR) & bit) != 0U;
        } else {
            config->level = (gpio_data_read(&priv->port->INDR) & bit) != 0U;
        }
    } else if (mode == CH32_GPIO_CFG_ANALOG) {
        config->mode = MR_GPIO_MODE_ANALOG;
    } else {
        config->mode = (__gpio_exti_lines[pin].owner == drv)
                         ? __gpio_exti_lines[pin].mode
                         : MR_GPIO_MODE_INPUT;
        if (mode == CH32_GPIO_CFG_PULL) {
            config->pull = ((gpio_data_read(&priv->port->OUTDR) & bit) != 0U)
                             ? MR_GPIO_PULL_UP
                             : MR_GPIO_PULL_DOWN;
        }
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__gpio_lock, mask);
    return 0;
}

static mr_err_t gpio_pin_write(const mr_driver_t *drv, mr_uint32_t pin,
                               mr_uint8_t value) {
    const gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Check parameter */
    if (pin >= CH32_GPIO_PIN_COUNT) {
        return -MR_EINVAL;
    }

    /* Write pin value */
    mask = mr_spinlock_lock_irqsave(&__gpio_lock);
    gpio_clock_enable(priv->clock_mask);
    gpio_latch_write(priv->port, 1U << pin, value != 0U);
    mr_spinlock_unlock_irqrestore(&__gpio_lock, mask);
    return 0;
}

static mr_err_t gpio_pin_read(const mr_driver_t *drv, mr_uint32_t pin,
                              mr_uint8_t *value) {
    const gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Check parameter */
    if (pin >= CH32_GPIO_PIN_COUNT) {
        return -MR_EINVAL;
    }

    /* Read pin value */
    mask = mr_spinlock_lock_irqsave(&__gpio_lock);
    gpio_clock_enable(priv->clock_mask);
    *value = (gpio_data_read(&priv->port->INDR) & (1U << pin)) != 0U;
    mr_spinlock_unlock_irqrestore(&__gpio_lock, mask);
    return 0;
}

static mr_err_t gpio_port_write(const mr_driver_t *drv, mr_uint32_t values) {
    const gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    const mr_gpio_data_t *data;
    mr_uint32_t bits, latch;
    int mask;

    /* Get exist mask */
    data = MR_DRIVER_DATA(drv);
    bits = data->exist_mask & CH32_GPIO_PIN_MASK;

    /* Preserve latch bits for pins outside the declared port mask */
    mask = mr_spinlock_lock_irqsave(&__gpio_lock);
    gpio_clock_enable(priv->clock_mask);
    latch = gpio_data_read(&priv->port->OUTDR);
    latch = (latch & ~bits) | (values & bits);
    gpio_data_write(&priv->port->OUTDR, (mr_uint16_t)latch);
    mr_spinlock_unlock_irqrestore(&__gpio_lock, mask);
    return 0;
}

static mr_err_t gpio_port_read(const mr_driver_t *drv, mr_uint32_t *values) {
    const gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Read port value */
    mask = mr_spinlock_lock_irqsave(&__gpio_lock);
    gpio_clock_enable(priv->clock_mask);
    *values = gpio_data_read(&priv->port->INDR) & CH32_GPIO_PIN_MASK;
    mr_spinlock_unlock_irqrestore(&__gpio_lock, mask);
    return 0;
}

static mr_err_t gpio_event_attach(const mr_driver_t *drv, mr_uint32_t pin,
                                  mr_waiter_t *entry) {
    gpio_exti_line_t *line;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    if (pin >= CH32_GPIO_PIN_COUNT) {
        return -MR_EINVAL;
    }

    /* Attach EXTI event */
    mask = mr_spinlock_lock_irqsave(&__gpio_lock);
    line = &__gpio_exti_lines[pin];
    ret = (line->owner == drv) ? mr_waiter_wait(entry, &line->waitable)
                              : -MR_EPERM;
    mr_spinlock_unlock_irqrestore(&__gpio_lock, mask);
    return ret;
}

static mr_err_t gpio_event_control(const mr_driver_t *drv, mr_uint32_t pin,
                                   mr_uint32_t events, mr_bool_t enable) {
    mr_uint32_t bit;
    int mask;

    /* Check parameter */
    if ((pin >= CH32_GPIO_PIN_COUNT) || (events != MR_GPIO_WAIT_EXTI)) {
        return -MR_EINVAL;
    }

    /* Control EXTI interrupt */
    mask = mr_spinlock_lock_irqsave(&__gpio_lock);
    if (__gpio_exti_lines[pin].owner != drv) {
        mr_spinlock_unlock_irqrestore(&__gpio_lock, mask);
        return -MR_EPERM;
    }
    bit = 1U << pin;
    if (((EXTI->INTENR & bit) != 0U) != (enable != MR_FALSE)) {
        gpio_exti_enable(pin, enable);
    }
    mr_spinlock_unlock_irqrestore(&__gpio_lock, mask);
    return 0;
}

mr_err_t mr_ch32_gpio_pin_alternate(const mr_ht_gpio_spec_t *spec,
                                    mr_bool_t output, mr_gpio_pull_t pull,
                                    mr_gpio_otype_t otype) {
    const mr_gpio_data_t *data;
    const gpio_priv_t *priv;
    const mr_driver_t *drv;
    mr_uint32_t bit, mode;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    if ((spec == MR_NULL) || (spec->drv == MR_NULL)
        || (spec->pin >= CH32_GPIO_PIN_COUNT)
        || ((mr_uint32_t)pull > MR_GPIO_PULL_DOWN)
        || ((mr_uint32_t)otype > MR_GPIO_OTYPE_OD)) {
        return -MR_EINVAL;
    }

    /* Get GPIO driver */
    drv = spec->drv;
    if (MR_DRIVER_OPS(drv) != &__gpio_ops) {
        return -MR_EINVAL;
    }
    data = MR_DRIVER_DATA(drv);
    bit = 1U << spec->pin;
    if ((data->exist_mask & bit) == 0U) {
        return -MR_EINVAL;
    }
    if (output && (pull != MR_GPIO_PULL_NONE)) {
        return -MR_ENOSYS;
    }

    /* Get GPIO private data */
    priv = MR_DRIVER_PRIV(drv);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__gpio_lock);

    /* Check GPIO state */
    if (!MR_DRIVER_IS_SETUP(drv) || MR_DRIVER_IS_DYING(drv)) {
        ret = -MR_ENODEV;
        goto _exit;
    }

    /* Check EXTI ownership */
    if (__gpio_exti_lines[spec->pin].owner == drv) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Enable GPIO clock */
    gpio_clock_enable(priv->clock_mask);

    /* Check pin lock */
    if (gpio_pin_locked(priv->port, bit)) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Init GPIO */
    if (output) {
        mode = CH32_GPIO_CFG_ALTERNATE | CH32_GPIO_CFG_OUTPUT_FAST;
        if (otype == MR_GPIO_OTYPE_OD) {
            mode |= CH32_GPIO_CFG_OPEN_DRAIN;
        }
        gpio_config_write(priv->port, spec->pin, mode);
    } else {
        gpio_input_configure(priv->port, spec->pin, pull);
    }
    ret = 0;
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__gpio_lock, mask);
    return ret;
}

/* GPIO EXTI capability definition */
#define __CH32_HT_GPIO_EXTI_MASK(_id)                                          \
    MR_HT_DRIVER_PROP_OR(_id, wch_exti_mask, 0U)
#define __CH32_HT_GPIO_EXTI_LINES                                              \
    (MR_LISTIFY(MR_HT_DRIVER_NUM(), (|), __CH32_HT_GPIO_EXTI_MASK))

#if (__CH32_HT_GPIO_EXTI_LINES != 0U)
static void gpio_exti_dispatch(mr_uint32_t group) {
    mr_gpio_event_data_t data;
    gpio_exti_line_t *line;
    mr_uint32_t pin, bit;
    int mask;

    /* Dispatch pending EXTI events */
    for (pin = 0U; pin < CH32_GPIO_PIN_COUNT; pin++) {
        bit = 1U << pin;
        if ((group & bit) == 0U) {
            continue;
        }
        /* Keep each event bound to its owner through synchronous dispatch */
        mask = mr_port_irq_disable();
        if ((EXTI->INTENR & EXTI->INTFR & bit) != 0U) {
            /* Acknowledge EXTI pending */
            EXTI->INTFR = bit;
            line = &__gpio_exti_lines[pin];
            if ((line->owner != MR_NULL) && MR_DRIVER_IS_SETUP(line->owner)
                && !MR_DRIVER_IS_DYING(line->owner)) {
                /* Raise GPIO EXTI event */
                data.pin = pin;
                mr_waitable_raise(&line->waitable, MR_GPIO_WAIT_EXTI, &data);
            }
        }
        mr_port_irq_enable(mask);
    }
}

/* EXTI IRQ handler definition */
#define __CH32_HT_GPIO_EXTI_HANDLER(_name, _mask)                              \
    void _name(void) MR_CH32_INTERRUPT;                                        \
    void _name(void) {                                                         \
        gpio_exti_dispatch(_mask);                                             \
    }

#if CH32_GPIO_PIN_COUNT == 8U
__CH32_HT_GPIO_EXTI_HANDLER(EXTI7_0_IRQHandler, CH32_GPIO_EXTI_LOW)
#else
#if (__CH32_HT_GPIO_EXTI_LINES & (1U << 0))
__CH32_HT_GPIO_EXTI_HANDLER(EXTI0_IRQHandler, 1U << 0)
#endif /* (__CH32_HT_GPIO_EXTI_LINES & (1U << 0)) */
#if (__CH32_HT_GPIO_EXTI_LINES & (1U << 1))
__CH32_HT_GPIO_EXTI_HANDLER(EXTI1_IRQHandler, 1U << 1)
#endif /* (__CH32_HT_GPIO_EXTI_LINES & (1U << 1)) */
#if (__CH32_HT_GPIO_EXTI_LINES & (1U << 2))
__CH32_HT_GPIO_EXTI_HANDLER(EXTI2_IRQHandler, 1U << 2)
#endif /* (__CH32_HT_GPIO_EXTI_LINES & (1U << 2)) */
#if (__CH32_HT_GPIO_EXTI_LINES & (1U << 3))
__CH32_HT_GPIO_EXTI_HANDLER(EXTI3_IRQHandler, 1U << 3)
#endif /* (__CH32_HT_GPIO_EXTI_LINES & (1U << 3)) */
#if (__CH32_HT_GPIO_EXTI_LINES & (1U << 4))
__CH32_HT_GPIO_EXTI_HANDLER(EXTI4_IRQHandler, 1U << 4)
#endif /* (__CH32_HT_GPIO_EXTI_LINES & (1U << 4)) */
#if (__CH32_HT_GPIO_EXTI_LINES & CH32_GPIO_EXTI_MID)
__CH32_HT_GPIO_EXTI_HANDLER(EXTI9_5_IRQHandler, CH32_GPIO_EXTI_MID)
#endif /* (__CH32_HT_GPIO_EXTI_LINES & CH32_GPIO_EXTI_MID) */
#if (__CH32_HT_GPIO_EXTI_LINES & CH32_GPIO_EXTI_HIGH)
__CH32_HT_GPIO_EXTI_HANDLER(EXTI15_10_IRQHandler, CH32_GPIO_EXTI_HIGH)
#endif /* (__CH32_HT_GPIO_EXTI_LINES & CH32_GPIO_EXTI_HIGH) */
#endif /* CH32_GPIO_PIN_COUNT == 8U */
#endif /* (__CH32_HT_GPIO_EXTI_LINES != 0U) */

/* Instance install */
#define CH32_HT_GPIO_INSTALL(_id, _ops)                                        \
    static const gpio_priv_t MR_CAT(__gpio_priv_, _id) = {                     \
        .port = MR_HT_DRIVER_PROP(_id, wch_gpiox),                             \
        .clock_mask = MR_HT_DRIVER_PROP(_id, wch_clock_mask),                  \
        .port_source = MR_HT_DRIVER_PROP(_id, wch_port_source),                \
        .exti_mask = __CH32_HT_GPIO_EXTI_MASK(_id),                            \
    };                                                                         \
    MR_HT_GPIO_INSTALL(_id, &MR_CAT(__gpio_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(CH32_HT_GPIO_INSTALL, &__gpio_ops);

#else

mr_err_t mr_ch32_gpio_pin_alternate(const mr_ht_gpio_spec_t *spec,
                                    mr_bool_t output, mr_gpio_pull_t pull,
                                    mr_gpio_otype_t otype) {
    /* GPIO instances are required for peripheral pin muxing */
    MR_IGNORE(spec);
    MR_IGNORE(output);
    MR_IGNORE(pull);
    MR_IGNORE(otype);
    return -MR_ENODEV;
}

#endif /* MR_HT_HAS_DRIVER() */

mr_err_t mr_ch32_gpio_pin_release(const mr_ht_gpio_spec_t *spec) {
    return mr_ch32_gpio_pin_alternate(spec, MR_FALSE, MR_GPIO_PULL_NONE,
                                      MR_GPIO_OTYPE_PP);
}

#endif /* defined(MR_USE_DRIVER_GPIO) */

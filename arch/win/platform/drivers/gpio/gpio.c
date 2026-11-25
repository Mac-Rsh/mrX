/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-11-25        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_GPIO)
#include <mrX/drivers/gpio/mr_gpio.h>
#include <mrX/libc/mr_string.h>

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

static const mr_gpio_ops_t __win_gpio_ops = {
    .pin_configure = gpio_pin_configure,
    .pin_configuration = gpio_pin_configuration,
    .pin_write = gpio_pin_write,
    .pin_read = gpio_pin_read,
    .port_write = gpio_port_write,
    .port_read = gpio_port_read,
    .event_attach = gpio_event_attach,
    .event_control = gpio_event_control,
};

static mr_bool_t gpio_mode_is_irq(mr_uint8_t mode) {
    return (mode >= MR_GPIO_MODE_IRQ_RISING)
           && (mode <= MR_GPIO_MODE_IRQ_LOW);
}

static void gpio_events_ensure_locked(mr_sim_gpio_t *sim) {
    mr_uint32_t i;

    if (sim->events_inited) {
        return;
    }
    for (i = 0U; i < 32U; i++) {
        (void)mr_waitable_init(&sim->events[i]);
    }
    sim->events_inited = MR_TRUE;
}

static mr_err_t gpio_pin_configure(const mr_driver_t *drv, mr_uint32_t pin,
                                   const mr_gpio_config_t *config) {
    mr_sim_gpio_t *sim = (mr_sim_gpio_t *)MR_DRIVER_PRIV(drv);
    int state;

    if ((!sim) || (!config) || (pin >= 32U)) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    sim->configs[pin] = *config;
    if (config->mode == MR_GPIO_MODE_OUTPUT) {
        if (config->level) {
            sim->out_mask |= (1U << pin);
        } else {
            sim->out_mask &= ~(1U << pin);
        }
    }
    if (!gpio_mode_is_irq(config->mode)) {
        sim->event_enabled_mask &= ~(1U << pin);
    }
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_err_t gpio_pin_configuration(const mr_driver_t *drv, mr_uint32_t pin,
                                       mr_gpio_config_t *config) {
    mr_sim_gpio_t *sim = (mr_sim_gpio_t *)MR_DRIVER_PRIV(drv);
    int state;

    if ((!sim) || (!config) || (pin >= 32U)) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    *config = sim->configs[pin];
    if (config->mode == MR_GPIO_MODE_OUTPUT) {
        config->level = (sim->out_mask >> pin) & 1U;
    }
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_err_t gpio_pin_write(const mr_driver_t *drv, mr_uint32_t pin,
                               mr_uint8_t value) {
    mr_sim_gpio_t *sim = (mr_sim_gpio_t *)MR_DRIVER_PRIV(drv);
    int state;

    if ((!sim) || (pin >= 32U)) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    if (value) {
        sim->out_mask |= (1U << pin);
    } else {
        sim->out_mask &= ~(1U << pin);
    }
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_err_t gpio_pin_read(const mr_driver_t *drv, mr_uint32_t pin,
                              mr_uint8_t *value) {
    mr_sim_gpio_t *sim = (mr_sim_gpio_t *)MR_DRIVER_PRIV(drv);
    int state;

    if ((!sim) || (!value) || (pin >= 32U)) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    if (sim->configs[pin].mode == MR_GPIO_MODE_OUTPUT) {
        *value = (mr_uint8_t)((sim->out_mask >> pin) & 1U);
    } else {
        *value = (mr_uint8_t)((sim->in_mask >> pin) & 1U);
    }
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_err_t gpio_port_write(const mr_driver_t *drv, mr_uint32_t mask) {
    mr_sim_gpio_t *sim = (mr_sim_gpio_t *)MR_DRIVER_PRIV(drv);
    int state;

    if (!sim) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    sim->out_mask = mask;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_err_t gpio_port_read(const mr_driver_t *drv, mr_uint32_t *mask) {
    mr_sim_gpio_t *sim = (mr_sim_gpio_t *)MR_DRIVER_PRIV(drv);
    int state;
    mr_uint32_t result = 0U;
    mr_uint32_t i;

    if ((!sim) || (!mask)) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    for (i = 0U; i < 32U; i++) {
        mr_uint32_t bit = (sim->configs[i].mode == MR_GPIO_MODE_OUTPUT)
                              ? ((sim->out_mask >> i) & 1U)
                              : ((sim->in_mask >> i) & 1U);
        result |= (bit << i);
    }
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    *mask = result;
    return 0;
}

static mr_err_t gpio_event_attach(const mr_driver_t *drv, mr_uint32_t pin,
                                  mr_waiter_t *entry) {
    mr_sim_gpio_t *sim = (mr_sim_gpio_t *)MR_DRIVER_PRIV(drv);
    int state;

    if ((!sim) || (!entry) || (pin >= 32U)) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    gpio_events_ensure_locked(sim);
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return mr_waiter_wait(entry, &sim->events[pin]);
}

static mr_err_t gpio_event_control(const mr_driver_t *drv, mr_uint32_t pin,
                                   mr_uint32_t events, mr_bool_t enable) {
    mr_sim_gpio_t *sim = (mr_sim_gpio_t *)MR_DRIVER_PRIV(drv);
    int state;

    if ((!sim) || (pin >= 32U)) {
        return -MR_EINVAL;
    }
    if ((events & ~MR_GPIO_WAIT_EXTI) != 0U) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    if (enable && !gpio_mode_is_irq(sim->configs[pin].mode)) {
        mr_spinlock_unlock_irqrestore(&sim->lock, state);
        return -MR_EINVAL;
    }
    if (events & MR_GPIO_WAIT_EXTI) {
        if (enable) {
            gpio_events_ensure_locked(sim);
            sim->event_enabled_mask |= (1U << pin);
        } else {
            sim->event_enabled_mask &= ~(1U << pin);
        }
    }
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_bool_t gpio_should_trigger(mr_uint8_t mode, mr_uint8_t old_val,
                                     mr_uint8_t new_val) {
    switch (mode) {
    case MR_GPIO_MODE_IRQ_RISING:
        return (!old_val && new_val) ? MR_TRUE : MR_FALSE;
    case MR_GPIO_MODE_IRQ_FALLING:
        return (old_val && !new_val) ? MR_TRUE : MR_FALSE;
    case MR_GPIO_MODE_IRQ_EDGE:
        return (old_val != new_val) ? MR_TRUE : MR_FALSE;
    case MR_GPIO_MODE_IRQ_HIGH:
        return (new_val != 0U) ? MR_TRUE : MR_FALSE;
    case MR_GPIO_MODE_IRQ_LOW:
        return (new_val == 0U) ? MR_TRUE : MR_FALSE;
    default:
        return MR_FALSE;
    }
}

/* =========================== Simulation Helpers =========================== */

mr_err_t mr_sim_gpio_init(mr_driver_t *drv, mr_sim_gpio_t *sim,
                          mr_uint32_t exist_mask, mr_uint32_t invert_mask) {
    mr_err_t ret;

    if ((!drv) || (!sim)) {
        return -MR_EINVAL;
    }

    mr_memset(sim, 0, sizeof(*sim));
    mr_spinlock_init(&sim->lock);
    sim->data.exist_mask = exist_mask;
    sim->data.invert_mask = invert_mask;

    ret = mr_gpio_init(drv, &sim->data, sim, (mr_gpio_ops_t *)&__win_gpio_ops);
    if (ret != 0) {
        return ret;
    }
    ret = mr_driver_setup(drv);
    if (ret != 0) {
        mr_gpio_del(drv);
        return ret;
    }
    return 0;
}

mr_err_t mr_sim_gpio_set(mr_driver_t *drv, mr_uint32_t pin, mr_uint8_t value) {
    mr_sim_gpio_t *sim;
    mr_gpio_data_t *gp_data;
    mr_uint8_t old_val, new_val;
    mr_bool_t fire = MR_FALSE;
    int state;

    if ((!drv) || (pin >= 32U)) {
        return -MR_EINVAL;
    }

    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);
    sim = (mr_sim_gpio_t *)MR_DRIVER_PRIV(drv);
    if ((!gp_data) || (!sim) || !(gp_data->exist_mask & (1U << pin))) {
        return -MR_EINVAL;
    }

    new_val = value ? 1U : 0U;
    state = mr_spinlock_lock_irqsave(&sim->lock);
    old_val = (mr_uint8_t)((sim->in_mask >> pin) & 1U);
    if (new_val) {
        sim->in_mask |= (1U << pin);
    } else {
        sim->in_mask &= ~(1U << pin);
    }
    if ((sim->event_enabled_mask & (1U << pin))
        && gpio_should_trigger(sim->configs[pin].mode, old_val, new_val)) {
        fire = MR_TRUE;
    }
    mr_spinlock_unlock_irqrestore(&sim->lock, state);

    if (fire) {
        mr_gpio_event_data_t ev_data = {.pin = pin};
        mr_waitable_raise(&sim->events[pin], MR_GPIO_WAIT_EXTI, &ev_data);
    }
    return 0;
}

mr_err_t mr_sim_gpio_get(mr_driver_t *drv, mr_uint32_t pin, mr_uint8_t *value) {
    mr_gpio_data_t *gp_data;

    if ((!drv) || (!value) || (pin >= 32U)) {
        return -MR_EINVAL;
    }
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);
    if ((!gp_data) || !(gp_data->exist_mask & (1U << pin))) {
        return -MR_EINVAL;
    }
    return gpio_pin_read(drv, pin, value);
}

mr_err_t mr_sim_gpio_port_set(mr_driver_t *drv, mr_uint32_t mask) {
    mr_gpio_data_t *gp_data;
    mr_uint32_t i;

    if (!drv) {
        return -MR_EINVAL;
    }
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);
    if (!gp_data) {
        return -MR_EINVAL;
    }

    for (i = 0U; i < 32U; i++) {
        if (gp_data->exist_mask & (1U << i)) {
            (void)mr_sim_gpio_set(drv, i, (mr_uint8_t)((mask >> i) & 1U));
        }
    }
    return 0;
}

mr_err_t mr_sim_gpio_port_get(mr_driver_t *drv, mr_uint32_t *mask) {
    mr_gpio_data_t *gp_data;
    mr_err_t ret;

    if ((!drv) || (!mask)) {
        return -MR_EINVAL;
    }
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);
    if (!gp_data) {
        return -MR_EINVAL;
    }
    ret = gpio_port_read(drv, mask);
    if (ret == 0) {
        *mask &= gp_data->exist_mask;
    }
    return ret;
}

/* ============================ HardTree Install ============================ */
#define MR_HT_COMPAT                    win_gpio
#if MR_HT_HAS_DRIVER()
#define WIN_HT_GPIO_INSTALL(_id, _ops)                                         \
    static mr_sim_gpio_t MR_CAT(__win_gpio_priv_, _id) = {                     \
        .lock = MR_SPINLOCK_INIT(),                                            \
    };                                                                         \
    MR_HT_GPIO_INSTALL(_id, &MR_CAT(__win_gpio_priv_, _id), _ops)

MR_HT_DRIVER_FOREACH(WIN_HT_GPIO_INSTALL, &__win_gpio_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_GPIO) */

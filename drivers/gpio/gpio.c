/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mr-X/drivers/gpio/mr_gpio.h>
#if defined(MR_USE_DRIVER_GPIO)
#include <mr-X/libc/mr_malloc.h>

/* GPIO driver operations(static & dynamic) */
static mr_err_t gpio_init(const mr_driver_t *drv);
static mr_err_t gpio_statik_deinit(const mr_driver_t *drv);
static mr_err_t gpio_dynamic_deinit(const mr_driver_t *drv);
MR_DRIVER_DOPSES_DEFINE(gpio, gpio_init, gpio_statik_deinit,
                        gpio_dynamic_deinit);

mr_err_t mr_gpio_init(mr_driver_t *drv, mr_gpio_data_t *data, void *priv,
                      mr_gpio_ops_t *ops) {
    const mr_driver_dopses_t *dopses = MR_DRIVER_DOPSES_FIND(gpio);
    const mr_driver_dops_t *dops = MR_DRIVER_DOPSES_STATIK(dopses);

    /* Check parameter */
    MR_ASSERT(drv != MR_NULL);
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Init GPIO driver */
    return mr_driver_init(drv, MR_DRIVER_TYPE_GPIO, dops, data, priv, ops);
}

mr_driver_t *mr_gpio_create(mr_gpio_data_t *data, void *priv,
                            mr_gpio_ops_t *ops) {
    const mr_driver_dopses_t *dopses = MR_DRIVER_DOPSES_FIND(gpio);
    const mr_driver_dops_t *dops = MR_DRIVER_DOPSES_DYNAMIC(dopses);

    /* Check parameter */
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Create GPIO driver */
    return mr_driver_create(MR_DRIVER_TYPE_GPIO, dops, data, priv, ops);
}

mr_err_t mr_gpio_pin_mode_set(const mr_driver_t *drv, mr_uint32_t pin,
                              mr_uint32_t mode) {
    mr_gpio_data_t *gp_data;
    mr_gpio_ops_t *gp_ops;
    mr_uint8_t value;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT((mode & MR_GPIO_OTYPE_MASK) != MR_GPIO_OTYPE_MASK);
    MR_ASSERT((mode & MR_GPIO_PULL_MASK) != MR_GPIO_PULL_MASK);
    MR_ASSERT((mode & MR_GPIO_DIR_MASK) != MR_GPIO_DIR_MASK);
    MR_ASSERT(pin < 32);

    /* Get GPIO data and operations */
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);
    gp_ops = (mr_gpio_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if pin is existed */
    if (!(gp_data->exist_mask & (1U << pin))) {
        return -MR_EINVAL;
    }

    /* Set pin mode */
    ret = gp_ops->pin_mode_set(drv, pin, mode);
    if (ret != 0) {
        return ret;
    }

    /* If pin is output, set pin default value */
    if (mode & MR_GPIO_DIR_OUTPUT) {
        value = (!!(mode & MR_GPIO_LEVEL_MASK))
              ^ (!!(gp_data->invert & (1U << pin)));
        return gp_ops->pin_set(drv, pin, value);
    }
    return 0;
}

mr_err_t mr_gpio_pin_mode_get(const mr_driver_t *drv, mr_uint32_t pin,
                              mr_uint32_t *mode) {
    mr_gpio_data_t *gp_data;
    mr_gpio_ops_t *gp_ops;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(mode != MR_NULL);
    MR_ASSERT(pin < 32);

    /* Get GPIO data and operations */
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);
    gp_ops = (mr_gpio_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if pin is existed */
    if (!(gp_data->exist_mask & (1U << pin))) {
        return -MR_EINVAL;
    }

    /* Get pin mode */
    ret = gp_ops->pin_mode_get(drv, pin, mode);
    if (ret != 0) {
        return ret;
    }

    /* Get pin active level */
    if (!(gp_data->invert & (1U << pin))) {
        *mode &= ~MR_GPIO_ACTIVE_LOW;
    } else {
        *mode |= MR_GPIO_ACTIVE_LOW;
    }
    return 0;
}

mr_err_t mr_gpio_pin_set(const mr_driver_t *drv, mr_uint32_t pin,
                         mr_uint8_t value) {
    mr_gpio_data_t *gp_data;
    mr_gpio_ops_t *gp_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(pin < 32);

    /* Get GPIO data and operations */
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);
    gp_ops = (mr_gpio_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if pin is existed */
    if (!(gp_data->exist_mask & (1U << pin))) {
        return -MR_EINVAL;
    }

    /* Set pin value */
    value = (!!value) ^ (!!(gp_data->invert & (1U << pin)));
    return gp_ops->pin_set(drv, pin, value);
}

int mr_gpio_pin_get(const mr_driver_t *drv, mr_uint32_t pin) {
    mr_gpio_data_t *gp_data;
    mr_gpio_ops_t *gp_ops;
    mr_uint8_t value;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(pin < 32);

    /* Get GPIO data and operations */
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);
    gp_ops = (mr_gpio_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if pin is existed */
    if (!(gp_data->exist_mask & (1U << pin))) {
        return -MR_EINVAL;
    }

    /* Get pin value */
    ret = gp_ops->pin_get(drv, pin, &value);
    if (ret != 0) {
        return (int)ret;
    }
    return (int)((!!value) ^ (!!(gp_data->invert & (1U << pin))));
}

mr_err_t mr_gpio_pin_toggle(const mr_driver_t *drv, mr_uint32_t pin) {
    mr_gpio_data_t *gp_data;
    mr_gpio_ops_t *gp_ops;
    mr_uint8_t value;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(pin < 32);

    /* Get GPIO data and operations */
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);
    gp_ops = (mr_gpio_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if pin is existed */
    if (!(gp_data->exist_mask & (1U << pin))) {
        return -MR_EINVAL;
    }

    /* Get pin value */
    ret = gp_ops->pin_get(drv, pin, &value);
    if (ret != 0) {
        return (int)ret;
    }

    /* Toggle pin value */
    value = (!!value) ^ MR_UINT8_MAX;
    return gp_ops->pin_set(drv, pin, value);
}

mr_err_t mr_gpio_port_set(const mr_driver_t *drv, mr_uint32_t mask) {
    mr_gpio_data_t *gp_data;
    mr_gpio_ops_t *gp_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get GPIO data and operations */
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);
    gp_ops = (mr_gpio_ops_t *)MR_DRIVER_OPS(drv);

    /* Set port value */
    mask = mask ^ gp_data->invert;
    return gp_ops->port_set(drv, mask);
}

mr_err_t mr_gpio_port_get(const mr_driver_t *drv, mr_uint32_t *mask) {
    mr_gpio_data_t *gp_data;
    mr_gpio_ops_t *gp_ops;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(mask != MR_NULL);

    /* Get GPIO data and operations */
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);
    gp_ops = (mr_gpio_ops_t *)MR_DRIVER_OPS(drv);

    /* Get port value */
    ret = gp_ops->port_get(drv, mask);
    if (ret != 0) {
        return ret;
    }
    *mask = *mask ^ gp_data->invert;
    return 0;
}

mr_err_t mr_gpio_port_toggle(const mr_driver_t *drv) {
    mr_gpio_ops_t *gp_ops;
    mr_uint32_t mask;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get GPIO operations */
    gp_ops = (mr_gpio_ops_t *)MR_DRIVER_OPS(drv);

    /* Get port value */
    ret = gp_ops->port_get(drv, &mask);
    if (ret != 0) {
        return ret;
    }

    /* Toggle port value */
    mask = mask ^ MR_UINT32_MAX;
    return gp_ops->port_set(drv, mask);
}

#if defined(MR_USE_DRIVER_GPIO_IRQ)
mr_err_t mr_gpio_irq_attach(mr_driver_t *drv, mr_waiter_t *entry) {
    mr_gpio_data_t *gp_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(entry != MR_NULL);

    /* Get GPIO data */
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);

    /* Auto set GPIO irq event */
    mr_waiter_event_set(entry, mr_waiter_event(entry) | MR_WAIT_IN);

    /* Attach GPIO irq */
    return mr_waiter_wait(entry, &gp_data->irq);
}

mr_err_t mr_gpio_irq_handle(mr_driver_t *drv, mr_uint32_t pin) {
    mr_gpio_data_t *gp_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(pin < 32);

    /* Get GPIO data */
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);

    /* Raise GPIO irq */
    return mr_waitable_raise(&gp_data->irq, MR_WAIT_IN | pin);
}
#endif /* defined(MR_USE_DRIVER_GPIO_IRQ) */

static mr_err_t gpio_init(const mr_driver_t *drv) {
    mr_gpio_data_t *gp_data;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get GPIO data */
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);

#if defined(MR_USE_DRIVER_GPIO_IRQ)
    /* Init GPIO irq */
    ret = mr_waitable_init(&gp_data->irq, MR_NULL, MR_NULL);
    if (ret != 0) {
        return ret;
    }
#else
    MR_UNUSED(gp_data);
    MR_UNUSED(ret);
#endif /* defined(MR_USE_DRIVER_GPIO_IRQ) */
    return 0;
}

static mr_err_t gpio_statik_deinit(const mr_driver_t *drv) {
    mr_gpio_data_t *gp_data;

    /* Check parameter */
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get GPIO data */
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);

#if defined(MR_USE_DRIVER_GPIO_IRQ)
    /* Deinit GPIO irq */
    mr_waitable_del(&gp_data->irq);
#else
    MR_UNUSED(gp_data);
#endif /* defined(MR_USE_DRIVER_GPIO_IRQ) */
    return 0;
}

static mr_err_t gpio_dynamic_deinit(const mr_driver_t *drv) {
    mr_err_t ret;

    /* Deinit GPIO */
    ret = gpio_statik_deinit(drv);
    if (ret != 0) {
        return ret;
    }

    /* Free GPIO data */
    mr_free(MR_DRIVER_DATA(drv));
    return 0;
}
#endif /* defined(MR_USE_DRIVER_GPIO) */

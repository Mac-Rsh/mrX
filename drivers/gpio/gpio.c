/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/gpio/mr_gpio.h>
#if defined(MR_USE_DRIVER_GPIO)
#include <mrX/libc/mr_malloc.h>

/* GPIO driver clazzes(static & dynamic) */
static mr_err_t gpio_init(const mr_driver_t *drv);
static mr_err_t gpio_statik_deinit(const mr_driver_t *drv);
static mr_err_t gpio_dynamic_deinit(const mr_driver_t *drv);
MR_DRIVER_CLAZZES_DEFINE(gpio, gpio_init, gpio_statik_deinit,
                         gpio_dynamic_deinit);

mr_err_t mr_gpio_pin_configure(mr_driver_t *drv, mr_uint32_t pin,
                               mr_gpio_config_t *config) {
    mr_gpio_data_t *gp_data;
    mr_gpio_ops_t *gp_ops;
    mr_uint8_t value;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);
    MR_ASSERT(pin < 32);

    /* Get GPIO data and operations */
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);
    gp_ops = (mr_gpio_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if pin is existed */
    if (!(gp_data->exist_mask & (1U << pin))) {
        return -MR_EINVAL;
    }

    /* Configure pin */
    ret = gp_ops->pin_configure(drv, pin, config);
    if (ret != 0) {
        return ret;
    }

    /* If pin is output, write pin default value */
    if (config->mode == MR_GPIO_MODE_OUTPUT) {
        value = (!!config->level) ^ (!!(gp_data->invert & (1U << pin)));
        return gp_ops->pin_write(drv, pin, value);
    }
    return 0;
}

mr_err_t mr_gpio_pin_configuration(mr_driver_t *drv, mr_uint32_t pin,
                                   mr_gpio_config_t *config) {
    mr_gpio_data_t *gp_data;
    mr_gpio_ops_t *gp_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);
    MR_ASSERT(pin < 32);

    /* Get GPIO data and operations */
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);
    gp_ops = (mr_gpio_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if pin is existed */
    if (!(gp_data->exist_mask & (1U << pin))) {
        return -MR_EINVAL;
    }

    /* Get pin configuration */
    return gp_ops->pin_configuration(drv, pin, config);
}

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

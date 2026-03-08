/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/gpio/mr_gpio.h>
#if defined(MR_USE_DRIVER_GPIO)
#include <mrX/libc/mr_malloc.h>

/* GPIO driver clazzes(static & dynamic) */
static mr_err_t gpio_dynamic_deinit(const mr_driver_t *drv);
MR_DRIVER_CLAZZES_DEFINE(gpio, MR_NULL, MR_NULL, gpio_dynamic_deinit);

mr_err_t mr_gpio_configure(mr_driver_t *drv, mr_uint32_t pin,
                           mr_gpio_config_t *config) {
    const mr_gpio_ops_t *gp_ops;
    mr_gpio_data_t *gp_data;
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
        value = (!!config->level) ^ (!!(gp_data->invert_mask & (1U << pin)));
        return gp_ops->pin_write(drv, pin, value);
    }
    return 0;
}

mr_err_t mr_gpio_configuration(mr_driver_t *drv, mr_uint32_t pin,
                               mr_gpio_config_t *config) {
    const mr_gpio_ops_t *gp_ops;
    mr_gpio_data_t *gp_data;

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

static mr_err_t gpio_dynamic_deinit(const mr_driver_t *drv) {
    /* Free GPIO data */
    mr_free(MR_DRIVER_DATA(drv));
    return 0;
}
#endif /* defined(MR_USE_DRIVER_GPIO) */

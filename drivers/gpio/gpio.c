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
static void gpio_release(const mr_driver_t *drv);
MR_DRIVER_CLAZZES_DEFINE(gpio, MR_NULL, MR_NULL, gpio_release);

mr_err_t mr_gpio_configure(mr_driver_t *drv, mr_uint32_t pin,
                           mr_gpio_config_t *config) {
    const mr_gpio_ops_t *gp_ops;
    mr_gpio_data_t *gp_data;
    mr_gpio_config_t cfg;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get GPIO data and operations */
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);
    gp_ops = (mr_gpio_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(gp_ops->pin_configure != MR_NULL);
    cfg = *config;

    /* Check if pin is existed */
    if ((pin >= 32U) || (!(gp_data->exist_mask & (1U << pin)))) {
        return -MR_EINVAL;
    }

    /* Invert output level */
    if (cfg.mode == MR_GPIO_MODE_OUTPUT) {
        cfg.level ^= !!(gp_data->invert_mask & (1U << pin));
    }

    /* Configure pin */
    return gp_ops->pin_configure(drv, pin, &cfg);
}

mr_err_t mr_gpio_configuration(mr_driver_t *drv, mr_uint32_t pin,
                               mr_gpio_config_t *config) {
    const mr_gpio_ops_t *gp_ops;
    mr_gpio_data_t *gp_data;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(config != MR_NULL);

    /* Get GPIO data and operations */
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);
    gp_ops = (mr_gpio_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(gp_ops->pin_configuration != MR_NULL);

    /* Check if pin is existed */
    if ((pin >= 32U) || (!(gp_data->exist_mask & (1U << pin)))) {
        return -MR_EINVAL;
    }

    /* Get pin configuration */
    ret = gp_ops->pin_configuration(drv, pin, config);
    if (ret != 0) {
        return ret;
    }

    /* Restore logical output level for the caller */
    if (config->mode == MR_GPIO_MODE_OUTPUT) {
        config->level ^= !!(gp_data->invert_mask & (1U << pin));
    }
    return 0;
}

static void gpio_release(const mr_driver_t *drv) {
    /* Free GPIO data */
    mr_free(MR_DRIVER_DATA(drv));
}
#endif /* defined(MR_USE_DRIVER_GPIO) */

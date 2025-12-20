/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/drivers/gpio/mr_gpio.h>
#if defined(MR_USE_DRIVER_GPIO)

/* Hard tree compatible */
#define MR_HT_COMPAT test_gpio

/* Driver GPIO operations */
static mr_err_t drv_pin_configure(const mr_driver_t *drv, mr_uint32_t pin,
                                  const mr_gpio_config_t *config);
static mr_err_t drv_pin_configuration(const mr_driver_t *drv, mr_uint32_t pin,
                                      mr_gpio_config_t *config);
static mr_err_t drv_pin_write(const mr_driver_t *drv, mr_uint32_t pin,
                              mr_uint8_t value);
static mr_err_t drv_pin_read(const mr_driver_t *drv, mr_uint32_t pin,
                             mr_uint8_t *value);
static mr_err_t drv_port_write(const mr_driver_t *drv, mr_uint32_t mask);
static mr_err_t drv_port_read(const mr_driver_t *drv, mr_uint32_t *mask);
static const mr_gpio_ops_t __drv_gpio_ops
    = {.pin_configure = drv_pin_configure,
       .pin_configuration = drv_pin_configuration,
       .pin_write = drv_pin_write,
       .pin_read = drv_pin_read,
       .port_write = drv_port_write,
       .port_read = drv_port_read};

static mr_err_t drv_pin_configure(const mr_driver_t *drv, mr_uint32_t pin,
                                  const struct mr_gpio_config *config) {
    MR_UNUSED(drv);
    MR_UNUSED(pin);
    MR_UNUSED(config);
    return 0;
}

static mr_err_t drv_pin_configuration(const mr_driver_t *drv, mr_uint32_t pin,
                                      struct mr_gpio_config *config) {
    MR_UNUSED(drv);
    MR_UNUSED(pin);
    MR_UNUSED(config);
    return 0;
}

static mr_err_t drv_pin_write(const mr_driver_t *drv, mr_uint32_t pin,
                              mr_uint8_t value) {
    MR_UNUSED(drv);
    MR_UNUSED(pin);
    MR_UNUSED(value);
    return 0;
}

static mr_err_t drv_pin_read(const mr_driver_t *drv, mr_uint32_t pin,
                             mr_uint8_t *value) {
    MR_UNUSED(drv);
    MR_UNUSED(pin);
    *value = 0;
    return 0;
}

static mr_err_t drv_port_write(const mr_driver_t *drv, mr_uint32_t mask) {
    MR_UNUSED(drv);
    MR_UNUSED(mask);
    return 0;
}

static mr_err_t drv_port_read(const mr_driver_t *drv, mr_uint32_t *mask) {
    MR_UNUSED(drv);
    *mask = 0;
    return 0;
}

/* Instance install */
#define TEST_GPIO_HT_INST_GPIO_INSTALL(_id, _ops)                              \
    MR_HT_INST_GPIO_INSTALL(_id, MR_NULL, _ops)
MR_HT_INST_DRIVER_FOREACH(TEST_GPIO_HT_INST_GPIO_INSTALL, &__drv_gpio_ops);
#endif /* defined(MR_USE_DRIVER_GPIO) */
#endif /* defined(MR_USE_TEST) */

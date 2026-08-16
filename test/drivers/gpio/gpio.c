/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_DRIVER_GPIO)
#include <mrX/drivers/gpio/mr_gpio.h>
#include <mrX/libc/mr_malloc.h>

static int gpio_configure_count;
static int gpio_configuration_count;
static int gpio_write_count;
static mr_uint8_t gpio_last_value;
static mr_uint32_t gpio_last_pin;
static mr_gpio_config_t gpio_last_config;
static mr_err_t gpio_configure_ret;
static mr_err_t gpio_configuration_ret;
static mr_err_t gpio_write_ret;

static void gpio_test_reset(void) {
    gpio_configure_count = 0;
    gpio_configuration_count = 0;
    gpio_write_count = 0;
    gpio_last_value = 0;
    gpio_last_pin = 0;
    gpio_last_config = (mr_gpio_config_t){0};
    gpio_configure_ret = 0;
    gpio_configuration_ret = 0;
    gpio_write_ret = 0;
}

static mr_err_t gpio_pin_configure(const mr_driver_t *drv, mr_uint32_t pin,
                                   const mr_gpio_config_t *config) {
    MR_IGNORE(drv);
    gpio_configure_count++;
    gpio_last_pin = pin;
    gpio_last_config = *config;
    return gpio_configure_ret;
}

static mr_err_t gpio_pin_configuration(const mr_driver_t *drv, mr_uint32_t pin,
                                       mr_gpio_config_t *config) {
    MR_IGNORE(drv);
    gpio_configuration_count++;
    gpio_last_pin = pin;
    if (gpio_configuration_ret == 0) {
        *config = gpio_last_config;
    }
    return gpio_configuration_ret;
}

static mr_err_t gpio_pin_write(const mr_driver_t *drv, mr_uint32_t pin,
                               mr_uint8_t value) {
    MR_IGNORE(drv);
    gpio_write_count++;
    gpio_last_pin = pin;
    gpio_last_value = value;
    return gpio_write_ret;
}

static mr_err_t gpio_pin_read(const mr_driver_t *drv, mr_uint32_t pin,
                              mr_uint8_t *value) {
    MR_IGNORE(drv);
    MR_IGNORE(pin);
    *value = gpio_last_value;
    return 0;
}

static mr_err_t gpio_port_write(const mr_driver_t *drv, mr_uint32_t mask) {
    MR_IGNORE(drv);
    gpio_last_value = (mr_uint8_t)mask;
    return 0;
}

static mr_err_t gpio_port_read(const mr_driver_t *drv, mr_uint32_t *mask) {
    MR_IGNORE(drv);
    *mask = 0xa5a5a5a5U;
    return 0;
}

static const mr_gpio_ops_t gpio_ops = {
    .pin_configure = gpio_pin_configure,
    .pin_configuration = gpio_pin_configuration,
    .pin_write = gpio_pin_write,
    .pin_read = gpio_pin_read,
    .port_write = gpio_port_write,
    .port_read = gpio_port_read,
};

static void gpio_waiter_entry(mr_waiter_t *waiter, void *param,
                              mr_wait_event_t event, const void *data) {
    MR_IGNORE(waiter);
    MR_IGNORE(param);
    MR_IGNORE(event);
    MR_IGNORE(data);
}

static mr_driver_t *gpio_create(mr_uint32_t exist, mr_uint32_t invert) {
    mr_gpio_data_t *data = mr_malloc(sizeof(*data));

    if (!data) {
        return MR_NULL;
    }
    data->exist_mask = exist;
    data->invert_mask = invert;
    return mr_gpio_create(data, MR_NULL, (mr_gpio_ops_t *)&gpio_ops);
}

static void test_gpio_configure_paths(void) {
    mr_driver_t *drv;
    mr_gpio_config_t config = {.mode = MR_GPIO_MODE_INPUT};
    mr_err_t ret;

    gpio_test_reset();
    drv = gpio_create((1U << 0) | (1U << 31), 1U << 0);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    ret = mr_gpio_configure(drv, 2, &config);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EINVAL);
    ret = mr_gpio_configure(drv, 32, &config);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(gpio_configure_count, 0);

    gpio_configure_ret = -MR_EIO;
    ret = mr_gpio_configure(drv, 0, &config);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EIO);
    MR_TEST_ASSERT_EQ_INT(gpio_configure_count, 1);

    gpio_configure_ret = 0;
    config.mode = MR_GPIO_MODE_OUTPUT;
    config.level = MR_GPIO_LEVEL_HIGH;
    ret = mr_gpio_configure(drv, 0, &config);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(gpio_last_value, 0);
    MR_TEST_ASSERT_EQ_INT(gpio_write_count, 1);

    ret = mr_gpio_configure(drv, 31, &config);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(gpio_last_value, 1);
    mr_gpio_del(drv);
}
MR_TEST_EXPORT(gpio, configure_paths, test_gpio_configure_paths);

static void test_gpio_configuration_and_io(void) {
    mr_driver_t *drv;
    mr_gpio_config_t saved;
    mr_uint8_t value;
    mr_uint32_t mask;

    gpio_test_reset();
    drv = gpio_create(0xffffffffU, 0x0000000fU);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);

    MR_TEST_ASSERT_EQ_INT(mr_gpio_configuration(drv, 3, &saved), 0);
    MR_TEST_ASSERT_EQ_INT(gpio_configuration_count, 1);
    gpio_configuration_ret = -MR_EIO;
    MR_TEST_ASSERT_EQ_INT(mr_gpio_configuration(drv, 3, &saved), -MR_EIO);
    MR_TEST_ASSERT_EQ_INT(mr_gpio_configuration(drv, 32, &saved), -MR_EINVAL);

    MR_TEST_ASSERT_EQ_INT(mr_gpio_input(drv, 3, MR_GPIO_PULL_UP), 0);
    MR_TEST_ASSERT_EQ_INT(gpio_last_config.pull, MR_GPIO_PULL_UP);
    MR_TEST_ASSERT_EQ_INT(mr_gpio_output(drv, 3, MR_GPIO_OTYPE_OD, 1), 0);
    MR_TEST_ASSERT_EQ_INT(mr_gpio_write(drv, 0, 1), sizeof(value));
    MR_TEST_ASSERT_EQ_INT(gpio_last_value, 0);
    value = 0;
    MR_TEST_ASSERT_EQ_INT(mr_gpio_read(drv, 0, &value), sizeof(value));
    MR_TEST_ASSERT_EQ_INT(value, 1);
    MR_TEST_ASSERT_EQ_INT(mr_gpio_port_write(drv, 0xffffffffU), sizeof(mask));
    MR_TEST_ASSERT_EQ_INT(mr_gpio_port_read(drv, &mask), sizeof(mask));
    MR_TEST_ASSERT_EQ_UINT(mask, 0xa5a5a5aaU);
    MR_TEST_ASSERT_EQ_INT(mr_gpio_toggle(drv, 3), 0);
    MR_TEST_ASSERT_EQ_INT(mr_gpio_port_toggle(drv), 0);
    mr_gpio_del(drv);
}
MR_TEST_EXPORT(gpio, configuration_and_io, test_gpio_configuration_and_io);

static void test_gpio_event_attach_and_reference(void) {
    mr_driver_t *drv;
    mr_waiter_t waiter;

    gpio_test_reset();
    drv = gpio_create(1U, 0);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_ASSERT_EQ_INT(
        mr_waiter_init(&waiter, gpio_waiter_entry, MR_NULL, MR_WAIT_IN), 0);
    MR_TEST_ASSERT_EQ_INT(mr_gpio_event_attach(drv, 1, &waiter), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_gpio_event_attach(drv, 0, &waiter), -MR_ENOSYS);
    mr_waiter_del(&waiter);
    mr_gpio_del(drv);
}
MR_TEST_EXPORT(gpio, event_attach_and_reference,
               test_gpio_event_attach_and_reference);
#endif

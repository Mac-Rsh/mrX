/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/gpio/mr_gpio.h>
#if defined(MR_USE_DRIVER_GPIO) && defined(MR_USE_DRIVER_I2C)
#include <mrX/drivers/i2c/mr_i2c.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    gpio_pcf8574
#if MR_HT_HAS_DRIVER()

/* Device definition */
#define PCF8574_ADDR_MIN                0x20U
#define PCF8574_ADDR_MAX                0x27U
#define PCF8574A_ADDR_MIN               0x38U
#define PCF8574A_ADDR_MAX               0x3fU

/* Driver GPIO private data type */
typedef struct drv_gpio_pcf8574_priv {
    mr_driver_t *i2c;
    mr_uint32_t addr;
    mr_uint8_t inputs;
    mr_uint8_t shadow;
} drv_gpio_pcf8574_priv_t;

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
MR_UNUSED static const mr_gpio_ops_t __drv_gpio_pcf8574_ops = {
    .pin_configure = drv_pin_configure,
    .pin_configuration = drv_pin_configuration,
    .pin_write = drv_pin_write,
    .pin_read = drv_pin_read,
    .port_write = drv_port_write,
    .port_read = drv_port_read,
    .event_attach = MR_NULL,
    .event_control = MR_NULL,
};

static mr_err_t pcf8574_write(drv_gpio_pcf8574_priv_t *priv, mr_uint8_t inputs,
                              mr_uint8_t shadow) {
    mr_i2c_msg_t msg;
    mr_uint8_t value;
    mr_ssize_t ret;

    /* Release input pins through the quasi-bidirectional latch */
    value = shadow | inputs;

    /* Write port latch */
    msg.addr = (mr_uint16_t)priv->addr;
    msg.flags = MR_I2C_MSG_STOP;
    msg.buf = &value;
    msg.len = sizeof(value);
    ret = mr_i2c_transfer(priv->i2c, &msg, 1U);
    if (ret < 0) {
        return (mr_err_t)ret;
    }
    if (ret != 1) {
        return -MR_EIO;
    }

    /* Commit cached state */
    priv->inputs = inputs;
    priv->shadow = value;
    return 0;
}

static mr_err_t drv_pin_configure(const mr_driver_t *drv, mr_uint32_t pin,
                                  const mr_gpio_config_t *config) {
    drv_gpio_pcf8574_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint8_t inputs, shadow, bit;

    /* Check I2C bus */
    if ((priv->i2c == MR_NULL) || !MR_DRIVER_IS_SETUP(priv->i2c)
        || MR_DRIVER_IS_DYING(priv->i2c)) {
        return -MR_ENODEV;
    }
    if (!MR_DRIVER_IS_TYPE(priv->i2c, MR_DRIVER_TYPE_I2C)) {
        return -MR_EINVAL;
    }

    /* Check chip address and configuration */
    if (!(((priv->addr >= PCF8574_ADDR_MIN) && (priv->addr <= PCF8574_ADDR_MAX))
          || ((priv->addr >= PCF8574A_ADDR_MIN)
              && (priv->addr <= PCF8574A_ADDR_MAX)))) {
        return -MR_EINVAL;
    }
    if (config->alt || (config->pull != MR_GPIO_PULL_NONE)
        || ((config->mode != MR_GPIO_MODE_INPUT)
            && (config->mode != MR_GPIO_MODE_OUTPUT))
        || ((config->mode == MR_GPIO_MODE_OUTPUT)
            && (config->otype != MR_GPIO_OTYPE_OD))) {
        return -MR_ENOSYS;
    }

    /* Stage direction and output */
    bit = (mr_uint8_t)(1U << pin);
    inputs = priv->inputs;
    shadow = priv->shadow;
    if (config->mode == MR_GPIO_MODE_INPUT) {
        inputs |= bit;
    } else {
        inputs &= (mr_uint8_t)~bit;
        shadow = config->level ? (shadow | bit) : (shadow & ~bit);
    }
    return pcf8574_write(priv, inputs, shadow);
}

static mr_err_t drv_pin_configuration(const mr_driver_t *drv, mr_uint32_t pin,
                                      mr_gpio_config_t *config) {
    drv_gpio_pcf8574_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Read cached configuration */
    config->mode = (priv->inputs & (1U << pin)) ? MR_GPIO_MODE_INPUT
                                                : MR_GPIO_MODE_OUTPUT;
    config->alt = MR_GPIO_ALT_DISABLE;
    config->pull = MR_GPIO_PULL_NONE;
    config->otype = MR_GPIO_OTYPE_OD;
    config->level = (priv->shadow >> pin) & 1U;
    return 0;
}

static mr_err_t drv_pin_write(const mr_driver_t *drv, mr_uint32_t pin,
                              mr_uint8_t value) {
    drv_gpio_pcf8574_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint8_t shadow, bit;

    /* Update output shadow */
    bit = (mr_uint8_t)(1U << pin);
    shadow = value ? (priv->shadow | bit) : (priv->shadow & ~bit);
    return pcf8574_write(priv, priv->inputs, shadow);
}

static mr_err_t drv_port_write(const mr_driver_t *drv, mr_uint32_t mask) {
    drv_gpio_pcf8574_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Update output pins */
    return pcf8574_write(priv, priv->inputs, (mr_uint8_t)mask);
}

static mr_err_t drv_port_read(const mr_driver_t *drv, mr_uint32_t *mask) {
    drv_gpio_pcf8574_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_i2c_msg_t msg;
    mr_uint8_t value;
    mr_ssize_t ret;

    /* Read port value */
    msg.addr = (mr_uint16_t)priv->addr;
    msg.flags = MR_I2C_MSG_READ | MR_I2C_MSG_STOP;
    msg.buf = &value;
    msg.len = sizeof(value);
    ret = mr_i2c_transfer(priv->i2c, &msg, 1U);
    if (ret < 0) {
        return (mr_err_t)ret;
    }
    if (ret != 1) {
        return -MR_EIO;
    }

    *mask = value;
    return 0;
}

static mr_err_t drv_pin_read(const mr_driver_t *drv, mr_uint32_t pin,
                             mr_uint8_t *value) {
    mr_uint32_t mask;
    mr_err_t ret;

    /* Read port */
    ret = drv_port_read(drv, &mask);
    if (ret != 0) {
        return ret;
    }

    /* Select pin */
    *value = (mr_uint8_t)((mask >> pin) & 1U);
    return 0;
}

/* Instance install */
#define GPIO_PCF8574_HT_GPIO_INSTALL(_id, _ops)                                \
    static drv_gpio_pcf8574_priv_t MR_CAT(__drv_gpio_pcf8574_priv_, _id) = {   \
        .i2c = MR_HT_DRIVER_FIND(MR_HT_DRIVER_PROP(_id, i2c)),                 \
        .addr = MR_HT_DRIVER_PROP(_id, addr),                                  \
        .inputs = 0xffU,                                                       \
        .shadow = 0xffU,                                                       \
    };                                                                         \
    static mr_gpio_data_t MR_CAT(__drv_gpio_pcf8574_data_, _id)                \
        = MR_GPIO_DATA_INIT(MR_HT_DRIVER_PROP_OR(_id, exist_mask, 0xffU)       \
                                & 0xffU,                                       \
                            MR_HT_DRIVER_PROP_OR(_id, invert_mask, 0U));       \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(gpio),                  \
                         MR_DRIVER_TYPE_GPIO,                                  \
                         &MR_CAT(__drv_gpio_pcf8574_data_, _id),               \
                         &MR_CAT(__drv_gpio_pcf8574_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(GPIO_PCF8574_HT_GPIO_INSTALL, &__drv_gpio_pcf8574_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_GPIO) && defined(MR_USE_DRIVER_I2C) */

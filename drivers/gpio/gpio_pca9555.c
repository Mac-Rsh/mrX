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
#define MR_HT_COMPAT                    gpio_pca9555
#if MR_HT_HAS_DRIVER()

/* Device definition */
#define PCA9555_ADDR_MIN                0x20U
#define PCA9555_ADDR_MAX                0x27U
#define PCA9555_BANK_COUNT              2U

/* Register map: one byte per port */
#define PCA9555_REG_INPUT               0x00U
#define PCA9555_REG_OUTPUT              0x02U
#define PCA9555_REG_DIRECTION           0x06U

/* Driver GPIO private data type */
typedef struct drv_gpio_pca9555_priv {
    mr_driver_t *i2c;
    mr_uint32_t addr;
} drv_gpio_pca9555_priv_t;

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
MR_UNUSED static const mr_gpio_ops_t __drv_gpio_pca9555_ops = {
    .pin_configure = drv_pin_configure,
    .pin_configuration = drv_pin_configuration,
    .pin_write = drv_pin_write,
    .pin_read = drv_pin_read,
    .port_write = drv_port_write,
    .port_read = drv_port_read,
    .event_attach = MR_NULL,
    .event_control = MR_NULL,
};

static mr_err_t pca9555_reg_write(drv_gpio_pca9555_priv_t *priv, mr_uint8_t reg,
                                  mr_uint8_t value) {
    mr_i2c_msg_t msg;
    mr_uint8_t tx[2];
    mr_ssize_t ret;

    /* Build register write frame */
    tx[0] = reg;
    tx[1] = value;

    /* Transfer frame */
    msg.addr = (mr_uint16_t)priv->addr;
    msg.flags = MR_I2C_MSG_STOP;
    msg.buf = tx;
    msg.len = sizeof(tx);
    ret = mr_i2c_transfer(priv->i2c, &msg, 1U);
    if (ret < 0) {
        return (mr_err_t)ret;
    }
    if (ret != 1) {
        return -MR_EIO;
    }
    return 0;
}

static mr_err_t pca9555_reg_read(drv_gpio_pca9555_priv_t *priv, mr_uint8_t reg,
                                 mr_uint8_t *value) {
    mr_i2c_msg_t msgs[2];
    mr_ssize_t ret;

    /* Select register and keep the bus active */
    msgs[0].addr = (mr_uint16_t)priv->addr;
    msgs[0].flags = MR_I2C_MSG_NOSTOP;
    msgs[0].buf = &reg;
    msgs[0].len = sizeof(reg);

    /* Read register and release the bus */
    msgs[1].addr = (mr_uint16_t)priv->addr;
    msgs[1].flags = MR_I2C_MSG_READ | MR_I2C_MSG_STOP;
    msgs[1].buf = value;
    msgs[1].len = sizeof(*value);

    /* Transfer messages */
    ret = mr_i2c_transfer(priv->i2c, msgs, 2U);
    if (ret < 0) {
        return (mr_err_t)ret;
    }
    if (ret != 2) {
        return -MR_EIO;
    }
    return 0;
}

static mr_err_t drv_pin_configure(const mr_driver_t *drv, mr_uint32_t pin,
                                  const mr_gpio_config_t *config) {
    drv_gpio_pca9555_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint8_t direction, output, bank, bit;
    mr_err_t ret;

    /* Check I2C bus */
    if ((priv->i2c == MR_NULL) || !MR_DRIVER_IS_SETUP(priv->i2c)
        || MR_DRIVER_IS_DYING(priv->i2c)) {
        return -MR_ENODEV;
    }
    if (!MR_DRIVER_IS_TYPE(priv->i2c, MR_DRIVER_TYPE_I2C)) {
        return -MR_EINVAL;
    }

    /* Check chip address and configuration */
    if ((priv->addr < PCA9555_ADDR_MIN) || (priv->addr > PCA9555_ADDR_MAX)) {
        return -MR_EINVAL;
    }
    if (config->alt || (config->pull != MR_GPIO_PULL_NONE)
        || ((config->mode != MR_GPIO_MODE_INPUT)
            && (config->mode != MR_GPIO_MODE_OUTPUT))
        || ((config->mode == MR_GPIO_MODE_OUTPUT)
            && (config->otype != MR_GPIO_OTYPE_PP))) {
        return -MR_ENOSYS;
    }

    /* Read current direction */
    bank = (mr_uint8_t)(pin / 8U);
    bit = (mr_uint8_t)(1U << (pin % 8U));
    ret = pca9555_reg_read(priv, PCA9555_REG_DIRECTION + bank, &direction);
    if (ret != 0) {
        return ret;
    }

    /* Preload output before enabling it */
    if (config->mode == MR_GPIO_MODE_OUTPUT) {
        ret = pca9555_reg_read(priv, PCA9555_REG_OUTPUT + bank, &output);
        if (ret != 0) {
            return ret;
        }
        output = config->level ? (output | bit) : (output & ~bit);
        ret = pca9555_reg_write(priv, PCA9555_REG_OUTPUT + bank, output);
        if (ret != 0) {
            return ret;
        }
        direction &= (mr_uint8_t)~bit;
    } else {
        direction |= bit;
    }

    /* Commit direction */
    return pca9555_reg_write(priv, PCA9555_REG_DIRECTION + bank, direction);
}

static mr_err_t drv_pin_configuration(const mr_driver_t *drv, mr_uint32_t pin,
                                      mr_gpio_config_t *config) {
    drv_gpio_pca9555_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint8_t direction, output, bank, bit;
    mr_err_t ret;

    /* Read direction and output latch */
    bank = (mr_uint8_t)(pin / 8U);
    bit = (mr_uint8_t)(1U << (pin % 8U));
    ret = pca9555_reg_read(priv, PCA9555_REG_DIRECTION + bank, &direction);
    if (ret != 0) {
        return ret;
    }
    ret = pca9555_reg_read(priv, PCA9555_REG_OUTPUT + bank, &output);
    if (ret != 0) {
        return ret;
    }

    /* Decode configuration */
    config->mode = (direction & bit) ? MR_GPIO_MODE_INPUT : MR_GPIO_MODE_OUTPUT;
    config->alt = MR_GPIO_ALT_DISABLE;
    config->pull = MR_GPIO_PULL_NONE;
    config->otype = MR_GPIO_OTYPE_PP;
    config->level = !!(output & bit);
    return 0;
}

static mr_err_t drv_pin_write(const mr_driver_t *drv, mr_uint32_t pin,
                              mr_uint8_t value) {
    drv_gpio_pca9555_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint8_t output, bank, bit;
    mr_err_t ret;

    /* Read output latch */
    bank = (mr_uint8_t)(pin / 8U);
    bit = (mr_uint8_t)(1U << (pin % 8U));
    ret = pca9555_reg_read(priv, PCA9555_REG_OUTPUT + bank, &output);
    if (ret != 0) {
        return ret;
    }

    /* Update output latch */
    output = value ? (output | bit) : (output & ~bit);
    return pca9555_reg_write(priv, PCA9555_REG_OUTPUT + bank, output);
}

static mr_err_t drv_port_write(const mr_driver_t *drv, mr_uint32_t mask) {
    drv_gpio_pca9555_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint8_t direction, output, selected, bank;
    mr_err_t ret;

    /* Update output pins in each bank */
    for (bank = 0U; bank < PCA9555_BANK_COUNT; bank++) {
        ret = pca9555_reg_read(priv, PCA9555_REG_DIRECTION + bank, &direction);
        if (ret != 0) {
            return ret;
        }
        ret = pca9555_reg_read(priv, PCA9555_REG_OUTPUT + bank, &output);
        if (ret != 0) {
            return ret;
        }

        selected = (mr_uint8_t)~direction;
        output = (output & ~selected) | ((mask >> (bank * 8U)) & selected);
        ret = pca9555_reg_write(priv, PCA9555_REG_OUTPUT + bank, output);
        if (ret != 0) {
            return ret;
        }
    }
    return 0;
}

static mr_err_t drv_port_read(const mr_driver_t *drv, mr_uint32_t *mask) {
    drv_gpio_pca9555_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint8_t low, high;
    mr_err_t ret;

    /* Read both input banks */
    ret = pca9555_reg_read(priv, PCA9555_REG_INPUT, &low);
    if (ret != 0) {
        return ret;
    }
    ret = pca9555_reg_read(priv, PCA9555_REG_INPUT + 1U, &high);
    if (ret != 0) {
        return ret;
    }

    *mask = low | ((mr_uint32_t)high << 8);
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
#define GPIO_PCA9555_HT_GPIO_INSTALL(_id, _ops)                                \
    static drv_gpio_pca9555_priv_t MR_CAT(__drv_gpio_pca9555_priv_, _id) = {   \
        .i2c = MR_HT_DRIVER_FIND(MR_HT_DRIVER_PROP(_id, i2c)),                 \
        .addr = MR_HT_DRIVER_PROP(_id, addr),                                  \
    };                                                                         \
    static mr_gpio_data_t MR_CAT(__drv_gpio_pca9555_data_, _id)                \
        = MR_GPIO_DATA_INIT(MR_HT_DRIVER_PROP_OR(_id, exist_mask, 0xffffU)     \
                                & 0xffffU,                                     \
                            MR_HT_DRIVER_PROP_OR(_id, invert_mask, 0U));       \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(gpio),                  \
                         MR_DRIVER_TYPE_GPIO,                                  \
                         &MR_CAT(__drv_gpio_pca9555_data_, _id),               \
                         &MR_CAT(__drv_gpio_pca9555_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(GPIO_PCA9555_HT_GPIO_INSTALL, &__drv_gpio_pca9555_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_GPIO) && defined(MR_USE_DRIVER_I2C) */

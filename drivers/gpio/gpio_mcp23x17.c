/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/gpio/mr_gpio.h>
#if defined(MR_USE_DRIVER_GPIO)                                                \
    && (defined(MR_USE_DRIVER_I2C) || defined(MR_USE_DRIVER_SPI))
#if defined(MR_USE_DRIVER_I2C)
#include <mrX/drivers/i2c/mr_i2c.h>
#endif /* defined(MR_USE_DRIVER_I2C) */
#if defined(MR_USE_DRIVER_SPI)
#include <mrX/drivers/spi/mr_spi.h>
#endif /* defined(MR_USE_DRIVER_SPI) */

/* HardTree compatible */
#define MR_HT_COMPAT                    gpio_mcp23x17
#if MR_HT_HAS_DRIVER()

/* Device definition: BANK=0, one byte per register */
#define MCP23X17_REG_DIRECTION          0x00U
#define MCP23X17_REG_POLARITY           0x02U
#define MCP23X17_REG_GPINTEN            0x04U
#define MCP23X17_REG_IOCON              0x0aU
#define MCP23X17_REG_PULL_UP            0x0cU
#define MCP23X17_REG_INPUT              0x12U
#define MCP23X17_REG_OUTPUT             0x14U
#define MCP23X17_IOCON_HAEN             0x08U
#define MCP23X17_I2C_ADDR_MIN           0x20U
#define MCP23X17_I2C_ADDR_MAX           0x27U
#define MCP23X17_SPI_ADDR_MAX           7U
#define MCP23X17_SPI_OPCODE             0x40U
#define MCP23X17_SPI_CLOCK_MAX          1000000U
#define MCP23X17_BANK_COUNT             2U

/* Driver GPIO private data type */
typedef struct drv_gpio_mcp23x17_priv {
#if defined(MR_USE_DRIVER_I2C)
    mr_driver_t *i2c;
#endif /* defined(MR_USE_DRIVER_I2C) */
#if defined(MR_USE_DRIVER_SPI)
    mr_ht_spi_spec_t spi;
#endif /* defined(MR_USE_DRIVER_SPI) */
    mr_uint32_t addr;
    mr_bool_t initialized;
} drv_gpio_mcp23x17_priv_t;

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
MR_UNUSED static const mr_gpio_ops_t __drv_gpio_mcp23x17_ops = {
    .pin_configure = drv_pin_configure,
    .pin_configuration = drv_pin_configuration,
    .pin_write = drv_pin_write,
    .pin_read = drv_pin_read,
    .port_write = drv_port_write,
    .port_read = drv_port_read,
    .event_attach = MR_NULL,
    .event_control = MR_NULL,
};

#if defined(MR_USE_DRIVER_I2C)
static mr_err_t mcp23x17_i2c_write(drv_gpio_mcp23x17_priv_t *priv,
                                   mr_uint8_t reg, mr_uint8_t value) {
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

static mr_err_t mcp23x17_i2c_read(drv_gpio_mcp23x17_priv_t *priv,
                                  mr_uint8_t reg, mr_uint8_t *value) {
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
#endif /* defined(MR_USE_DRIVER_I2C) */

#if defined(MR_USE_DRIVER_SPI)
static mr_err_t mcp23x17_spi_configure(drv_gpio_mcp23x17_priv_t *priv) {
    mr_spi_config_t actual, config;
    mr_err_t ret;

    /* Configure SPI for MCP23S17 */
    config.clock_rate = MCP23X17_SPI_CLOCK_MAX;
    config.data_bits = 8U;
    config.mode = MR_SPI_MODE_0;
    config.bit_order = MR_SPI_BIT_ORDER_MSB;
    config.role = MR_SPI_ROLE_MASTER;
    ret = mr_ht_spi_configure(&priv->spi, &config);
    if (ret != 0) {
        return ret;
    }

    /* Verify the applied configuration */
    ret = mr_ht_spi_configuration(&priv->spi, &actual);
    if (ret != 0) {
        return ret;
    }
    if ((actual.clock_rate == 0U)
        || (actual.clock_rate > MCP23X17_SPI_CLOCK_MAX)
        || (actual.data_bits != 8U) || (actual.mode != MR_SPI_MODE_0)
        || (actual.bit_order != MR_SPI_BIT_ORDER_MSB)
        || (actual.role != MR_SPI_ROLE_MASTER)) {
        return -MR_ENOSYS;
    }
    return 0;
}

static mr_err_t mcp23x17_spi_write(drv_gpio_mcp23x17_priv_t *priv,
                                   mr_uint8_t addr, mr_uint8_t reg,
                                   mr_uint8_t value) {
    mr_uint8_t tx[3];
    mr_spi_msg_t msg;
    mr_ssize_t ret;

    /* Build register write frame */
    tx[0] = (mr_uint8_t)(MCP23X17_SPI_OPCODE | (addr << 1));
    tx[1] = reg;
    tx[2] = value;

    /* Transfer frame */
    msg.tx_buf = tx;
    msg.tx_len = sizeof(tx);
    msg.rx_buf = MR_NULL;
    msg.rx_len = 0U;
    ret = mr_ht_spi_transfer(&priv->spi, &msg, 1U);
    if (ret < 0) {
        return (mr_err_t)ret;
    }
    if (ret != 1) {
        return -MR_EIO;
    }
    return 0;
}

static mr_err_t mcp23x17_spi_read(drv_gpio_mcp23x17_priv_t *priv,
                                  mr_uint8_t reg, mr_uint8_t *value) {
    mr_uint8_t tx[3], rx[3];
    mr_spi_msg_t msg;
    mr_ssize_t ret;

    /* Build register read frame */
    tx[0] = (mr_uint8_t)(MCP23X17_SPI_OPCODE | (priv->addr << 1) | 1U);
    tx[1] = reg;
    tx[2] = 0U;

    /* Transfer frame */
    msg.tx_buf = tx;
    msg.tx_len = sizeof(tx);
    msg.rx_buf = rx;
    msg.rx_len = sizeof(rx);
    ret = mr_ht_spi_transfer(&priv->spi, &msg, 1U);
    if (ret < 0) {
        return (mr_err_t)ret;
    }
    if (ret != 1) {
        return -MR_EIO;
    }

    *value = rx[2];
    return 0;
}
#endif /* defined(MR_USE_DRIVER_SPI) */

static mr_err_t mcp23x17_bus_configure(drv_gpio_mcp23x17_priv_t *priv) {
#if defined(MR_USE_DRIVER_I2C) && defined(MR_USE_DRIVER_SPI)
    /* Require exactly one configured bus */
    if ((priv->i2c != MR_NULL) && (priv->spi.drv != MR_NULL)) {
        return -MR_EINVAL;
    }
    if ((priv->i2c == MR_NULL) && (priv->spi.drv == MR_NULL)) {
        return -MR_EINVAL;
    }

    if (priv->i2c != MR_NULL) {
        /* Check I2C bus and address */
        if (!MR_DRIVER_IS_SETUP(priv->i2c) || MR_DRIVER_IS_DYING(priv->i2c)) {
            return -MR_ENODEV;
        }
        if (!MR_DRIVER_IS_TYPE(priv->i2c, MR_DRIVER_TYPE_I2C)
            || (priv->addr < MCP23X17_I2C_ADDR_MIN)
            || (priv->addr > MCP23X17_I2C_ADDR_MAX)) {
            return -MR_EINVAL;
        }
        return 0;
    }

    /* Check SPI bus and address */
    if (!MR_DRIVER_IS_SETUP(priv->spi.drv)
        || MR_DRIVER_IS_DYING(priv->spi.drv)) {
        return -MR_ENODEV;
    }
    if (!MR_DRIVER_IS_TYPE(priv->spi.drv, MR_DRIVER_TYPE_SPI)
        || (priv->addr > MCP23X17_SPI_ADDR_MAX)) {
        return -MR_EINVAL;
    }
    return mcp23x17_spi_configure(priv);
#elif defined(MR_USE_DRIVER_I2C)
    /* Check I2C bus and address */
    if (priv->i2c == MR_NULL) {
        return -MR_EINVAL;
    }
    if (!MR_DRIVER_IS_SETUP(priv->i2c) || MR_DRIVER_IS_DYING(priv->i2c)) {
        return -MR_ENODEV;
    }
    if (!MR_DRIVER_IS_TYPE(priv->i2c, MR_DRIVER_TYPE_I2C)
        || (priv->addr < MCP23X17_I2C_ADDR_MIN)
        || (priv->addr > MCP23X17_I2C_ADDR_MAX)) {
        return -MR_EINVAL;
    }
    return 0;
#elif defined(MR_USE_DRIVER_SPI)
    /* Check SPI bus and address */
    if (priv->spi.drv == MR_NULL) {
        return -MR_EINVAL;
    }
    if (!MR_DRIVER_IS_SETUP(priv->spi.drv)
        || MR_DRIVER_IS_DYING(priv->spi.drv)) {
        return -MR_ENODEV;
    }
    if (!MR_DRIVER_IS_TYPE(priv->spi.drv, MR_DRIVER_TYPE_SPI)
        || (priv->addr > MCP23X17_SPI_ADDR_MAX)) {
        return -MR_EINVAL;
    }
    return mcp23x17_spi_configure(priv);
#endif
}

static mr_err_t mcp23x17_reg_write(drv_gpio_mcp23x17_priv_t *priv,
                                   mr_uint8_t reg, mr_uint8_t value) {
#if defined(MR_USE_DRIVER_I2C) && defined(MR_USE_DRIVER_SPI)
    /* Use the configured bus */
    if (priv->i2c != MR_NULL) {
        return mcp23x17_i2c_write(priv, reg, value);
    }
    return mcp23x17_spi_write(priv, (mr_uint8_t)priv->addr, reg, value);
#elif defined(MR_USE_DRIVER_I2C)
    return mcp23x17_i2c_write(priv, reg, value);
#elif defined(MR_USE_DRIVER_SPI)
    return mcp23x17_spi_write(priv, (mr_uint8_t)priv->addr, reg, value);
#endif
}

static mr_err_t mcp23x17_reg_read(drv_gpio_mcp23x17_priv_t *priv,
                                  mr_uint8_t reg, mr_uint8_t *value) {
#if defined(MR_USE_DRIVER_I2C) && defined(MR_USE_DRIVER_SPI)
    /* Use the configured bus */
    if (priv->i2c != MR_NULL) {
        return mcp23x17_i2c_read(priv, reg, value);
    }
    return mcp23x17_spi_read(priv, reg, value);
#elif defined(MR_USE_DRIVER_I2C)
    return mcp23x17_i2c_read(priv, reg, value);
#elif defined(MR_USE_DRIVER_SPI)
    return mcp23x17_spi_read(priv, reg, value);
#endif
}

static mr_err_t mcp23x17_enable_haen(drv_gpio_mcp23x17_priv_t *priv) {
#if defined(MR_USE_DRIVER_I2C) && defined(MR_USE_DRIVER_SPI)
    if (priv->i2c != MR_NULL) {
        return mcp23x17_reg_write(priv, MCP23X17_REG_IOCON,
                                  MCP23X17_IOCON_HAEN);
    }

    /* HAEN resets disabled, so the first SPI IOCON write must use address 0 */
    return mcp23x17_spi_write(priv, 0U, MCP23X17_REG_IOCON,
                              MCP23X17_IOCON_HAEN);
#elif defined(MR_USE_DRIVER_I2C)
    return mcp23x17_reg_write(priv, MCP23X17_REG_IOCON, MCP23X17_IOCON_HAEN);
#elif defined(MR_USE_DRIVER_SPI)
    /* HAEN resets disabled, so the first SPI IOCON write must use address 0 */
    return mcp23x17_spi_write(priv, 0U, MCP23X17_REG_IOCON,
                              MCP23X17_IOCON_HAEN);
#endif
}

static mr_err_t mcp23x17_init(drv_gpio_mcp23x17_priv_t *priv) {
    mr_err_t ret;

    /* Skip repeated initialization */
    if (priv->initialized) {
        return 0;
    }

    /* Require reset BANK=0 and enable SPI hardware addressing */
    ret = mcp23x17_enable_haen(priv);
    if (ret != 0) {
        return ret;
    }

    /* Disable interrupt-on-change */
    ret = mcp23x17_reg_write(priv, MCP23X17_REG_GPINTEN, 0U);
    if (ret != 0) {
        return ret;
    }
    ret = mcp23x17_reg_write(priv, MCP23X17_REG_GPINTEN + 1U, 0U);
    if (ret != 0) {
        return ret;
    }

    /* Keep logical inversion exclusively in the GPIO public layer */
    ret = mcp23x17_reg_write(priv, MCP23X17_REG_POLARITY, 0U);
    if (ret != 0) {
        return ret;
    }
    ret = mcp23x17_reg_write(priv, MCP23X17_REG_POLARITY + 1U, 0U);
    if (ret != 0) {
        return ret;
    }

    priv->initialized = MR_TRUE;
    return 0;
}

static mr_err_t drv_pin_configure(const mr_driver_t *drv, mr_uint32_t pin,
                                  const mr_gpio_config_t *config) {
    drv_gpio_mcp23x17_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint8_t direction, output, pull, bank, bit;
    mr_err_t ret;

    /* Check and configure bus */
    ret = mcp23x17_bus_configure(priv);
    if (ret != 0) {
        return ret;
    }
    /* Check pin configuration */
    if (config->alt || (config->pull > MR_GPIO_PULL_UP)
        || ((config->mode != MR_GPIO_MODE_INPUT)
            && (config->mode != MR_GPIO_MODE_OUTPUT))
        || ((config->mode == MR_GPIO_MODE_OUTPUT)
            && (config->otype != MR_GPIO_OTYPE_PP))) {
        return -MR_ENOSYS;
    }

    /* Initialize chip */
    ret = mcp23x17_init(priv);
    if (ret != 0) {
        return ret;
    }

    /* Read current direction */
    bank = (mr_uint8_t)(pin / 8U);
    bit = (mr_uint8_t)(1U << (pin % 8U));
    ret = mcp23x17_reg_read(priv, MCP23X17_REG_DIRECTION + bank, &direction);
    if (ret != 0) {
        return ret;
    }

    /* Preload output before enabling it */
    if (config->mode == MR_GPIO_MODE_OUTPUT) {
        ret = mcp23x17_reg_read(priv, MCP23X17_REG_OUTPUT + bank, &output);
        if (ret != 0) {
            return ret;
        }
        output = config->level ? (output | bit) : (output & ~bit);
        ret = mcp23x17_reg_write(priv, MCP23X17_REG_OUTPUT + bank, output);
        if (ret != 0) {
            return ret;
        }
        direction &= (mr_uint8_t)~bit;
    } else {
        direction |= bit;
    }

    /* Commit direction */
    ret = mcp23x17_reg_write(priv, MCP23X17_REG_DIRECTION + bank, direction);
    if (ret != 0) {
        return ret;
    }

    /* Update pull-up */
    ret = mcp23x17_reg_read(priv, MCP23X17_REG_PULL_UP + bank, &pull);
    if (ret != 0) {
        return ret;
    }
    pull = (config->pull == MR_GPIO_PULL_UP) ? (pull | bit) : (pull & ~bit);
    return mcp23x17_reg_write(priv, MCP23X17_REG_PULL_UP + bank, pull);
}

static mr_err_t drv_pin_configuration(const mr_driver_t *drv, mr_uint32_t pin,
                                      mr_gpio_config_t *config) {
    drv_gpio_mcp23x17_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint8_t direction, output, pull, bank, bit;
    mr_err_t ret;

    /* Read direction, output latch and pull-up */
    bank = (mr_uint8_t)(pin / 8U);
    bit = (mr_uint8_t)(1U << (pin % 8U));
    ret = mcp23x17_reg_read(priv, MCP23X17_REG_DIRECTION + bank, &direction);
    if (ret != 0) {
        return ret;
    }
    ret = mcp23x17_reg_read(priv, MCP23X17_REG_OUTPUT + bank, &output);
    if (ret != 0) {
        return ret;
    }
    ret = mcp23x17_reg_read(priv, MCP23X17_REG_PULL_UP + bank, &pull);
    if (ret != 0) {
        return ret;
    }

    /* Decode configuration */
    config->mode = (direction & bit) ? MR_GPIO_MODE_INPUT : MR_GPIO_MODE_OUTPUT;
    config->alt = MR_GPIO_ALT_DISABLE;
    config->pull = (pull & bit) ? MR_GPIO_PULL_UP : MR_GPIO_PULL_NONE;
    config->otype = MR_GPIO_OTYPE_PP;
    config->level = !!(output & bit);
    return 0;
}

static mr_err_t drv_pin_write(const mr_driver_t *drv, mr_uint32_t pin,
                              mr_uint8_t value) {
    drv_gpio_mcp23x17_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint8_t output, bank, bit;
    mr_err_t ret;

    /* Read output latch */
    bank = (mr_uint8_t)(pin / 8U);
    bit = (mr_uint8_t)(1U << (pin % 8U));
    ret = mcp23x17_reg_read(priv, MCP23X17_REG_OUTPUT + bank, &output);
    if (ret != 0) {
        return ret;
    }

    /* Update output latch */
    output = value ? (output | bit) : (output & ~bit);
    return mcp23x17_reg_write(priv, MCP23X17_REG_OUTPUT + bank, output);
}

static mr_err_t drv_port_write(const mr_driver_t *drv, mr_uint32_t mask) {
    drv_gpio_mcp23x17_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint8_t direction, output, selected, bank;
    mr_err_t ret;

    /* Update output pins in each bank */
    for (bank = 0U; bank < MCP23X17_BANK_COUNT; bank++) {
        ret = mcp23x17_reg_read(priv, MCP23X17_REG_DIRECTION + bank,
                                &direction);
        if (ret != 0) {
            return ret;
        }
        ret = mcp23x17_reg_read(priv, MCP23X17_REG_OUTPUT + bank, &output);
        if (ret != 0) {
            return ret;
        }

        selected = (mr_uint8_t)~direction;
        output = (output & ~selected) | ((mask >> (bank * 8U)) & selected);
        ret = mcp23x17_reg_write(priv, MCP23X17_REG_OUTPUT + bank, output);
        if (ret != 0) {
            return ret;
        }
    }
    return 0;
}

static mr_err_t drv_port_read(const mr_driver_t *drv, mr_uint32_t *mask) {
    drv_gpio_mcp23x17_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint8_t low, high;
    mr_err_t ret;

    /* Read both input banks */
    ret = mcp23x17_reg_read(priv, MCP23X17_REG_INPUT, &low);
    if (ret != 0) {
        return ret;
    }
    ret = mcp23x17_reg_read(priv, MCP23X17_REG_INPUT + 1U, &high);
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

#if defined(MR_USE_DRIVER_I2C)
#define MCP23X17_I2C_INIT(_id)                                                 \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, i2c),                               \
                (.i2c = MR_HT_DRIVER_FIND(MR_HT_DRIVER_PROP(_id, i2c)), ), ())
#else
#define MCP23X17_I2C_INIT(_id)
#endif /* defined(MR_USE_DRIVER_I2C) */

#if defined(MR_USE_DRIVER_SPI)
#define MCP23X17_SPI_INIT(_id)                                                 \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, spi),                               \
                (.spi = MR_HT_SPI_SPEC(_id, spi), ), ())
#else
#define MCP23X17_SPI_INIT(_id)
#endif /* defined(MR_USE_DRIVER_SPI) */

/* Instance install */
#define GPIO_MCP23X17_HT_GPIO_INSTALL(_id, _ops)                               \
    static drv_gpio_mcp23x17_priv_t MR_CAT(__drv_gpio_mcp23x17_priv_, _id) = { \
        MCP23X17_I2C_INIT(_id) MCP23X17_SPI_INIT(_id).addr                     \
        = MR_HT_DRIVER_PROP(_id, addr),                                        \
    };                                                                         \
    static mr_gpio_data_t MR_CAT(__drv_gpio_mcp23x17_data_, _id)               \
        = MR_GPIO_DATA_INIT(MR_HT_DRIVER_PROP_OR(_id, exist_mask, 0xffffU)     \
                                & 0xffffU,                                     \
                            MR_HT_DRIVER_PROP_OR(_id, invert_mask, 0U));       \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(gpio),                  \
                         MR_DRIVER_TYPE_GPIO,                                  \
                         &MR_CAT(__drv_gpio_mcp23x17_data_, _id),              \
                         &MR_CAT(__drv_gpio_mcp23x17_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(GPIO_MCP23X17_HT_GPIO_INSTALL, &__drv_gpio_mcp23x17_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_GPIO) && (defined(MR_USE_DRIVER_I2C) ||        \
          defined(MR_USE_DRIVER_SPI)) */

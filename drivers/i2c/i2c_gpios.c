/**
 * @copyright (c) 2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/i2c/mr_i2c.h>
#if defined(MR_USE_DRIVER_I2C_GPIOS)
#include <mrX/drivers/gpio/mr_gpio.h>
#include <mrX/kernel/mr_clock.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    i2c_gpios

/* Driver I2C operations */
static mr_err_t drv_i2c_configure(const mr_driver_t *drv,
                                  const mr_i2c_config_t *config);
static int drv_i2c_xfer(const mr_driver_t *drv, mr_i2c_msg_t *msg);
static mr_err_t drv_i2c_recover(const mr_driver_t *drv);
MR_UNUSED static const mr_i2c_ops_t __drv_i2c_ops
    = {.configure = drv_i2c_configure,
       .xfer = drv_i2c_xfer,
       .recover = drv_i2c_recover};

/* Driver I2C private data type */
typedef struct drv_i2c_priv {
    mr_ht_gpio_spec_t scl;
    mr_ht_gpio_spec_t sda;
    mr_uint32_t delay_us;
} drv_i2c_priv_t;

/* I2C control macro functions definition */
#define I2C_SCL_LOW(_priv)                                                     \
    mr_gpio_pin_write((_priv)->scl.drv, (_priv)->scl.pin, MR_GPIO_LEVEL_LOW)
#define I2C_SCL_HIGH(_priv)                                                    \
    mr_gpio_pin_write((_priv)->scl.drv, (_priv)->scl.pin, MR_GPIO_LEVEL_HIGH)
#define I2C_SDA_LOW(_priv)                                                     \
    mr_gpio_pin_write((_priv)->sda.drv, (_priv)->sda.pin, MR_GPIO_LEVEL_LOW)
#define I2C_SDA_HIGH(_priv)                                                    \
    mr_gpio_pin_write((_priv)->sda.drv, (_priv)->sda.pin, MR_GPIO_LEVEL_HIGH)
#define I2C_SDA_READ(_priv, _level)                                            \
    mr_gpio_pin_read((_priv)->sda.drv, (_priv)->sda.pin, &(_level))
#define I2C_DELAY(_priv)                mr_clock_busy_wait((_priv)->delay_us)

static mr_err_t drv_i2c_configure(const mr_driver_t *drv,
                                  const mr_i2c_config_t *config) {
    drv_i2c_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;

    /* Configure SCL&SDA GPIO */
    ret = mr_gpio_pin_output(priv->scl.drv, priv->scl.pin, MR_GPIO_OTYPE_OD,
                             MR_GPIO_LEVEL_HIGH);
    if (ret != 0) {
        return ret;
    }
    ret = mr_gpio_pin_output(priv->sda.drv, priv->sda.pin, MR_GPIO_OTYPE_OD,
                             MR_GPIO_LEVEL_HIGH);
    if (ret != 0) {
        return ret;
    }

    /* Calculate delay time */
    if (config->baud_rate > 0) {
        /* Baud rate out of range */
        if (config->baud_rate > 1000000U) {
            return -MR_EINVAL;
        }
        priv->delay_us = 1000000U / config->baud_rate / 2;
    } else {
        priv->delay_us = 0;
    }
    return 0;
}

static void i2c_start(drv_i2c_priv_t *priv) {
    /* IDLE */
    I2C_SDA_HIGH(priv);
    I2C_SCL_HIGH(priv);
    I2C_DELAY(priv);

    /* START */
    I2C_SDA_LOW(priv);
    I2C_DELAY(priv);
    I2C_SCL_LOW(priv);
    I2C_DELAY(priv);
}

static void i2c_stop(drv_i2c_priv_t *priv) {
    /* IDLE */
    I2C_SDA_LOW(priv);
    I2C_SCL_LOW(priv);
    I2C_DELAY(priv);

    /* STOP */
    I2C_SCL_HIGH(priv);
    I2C_DELAY(priv);
    I2C_SDA_HIGH(priv);
    I2C_DELAY(priv);
}

static mr_err_t i2c_check_ack(drv_i2c_priv_t *priv) {
    mr_uint8_t level;

    /* WAIT */
    I2C_SDA_HIGH(priv);
    I2C_DELAY(priv);
    I2C_SCL_HIGH(priv);
    I2C_DELAY(priv);

    /* CHECK */
    I2C_SDA_READ(priv, level);
    I2C_SCL_LOW(priv);
    I2C_DELAY(priv);

    /* ACK = LOW, NACK = HIGH */
    return (level == MR_GPIO_LEVEL_LOW) ? 0 : -MR_EIO;
}

static mr_err_t i2c_write_byte(drv_i2c_priv_t *priv, mr_uint8_t data) {
    mr_uint8_t i;

    /* SEND(MSB) */
    for (i = 0; i < 8; i++) {
        /* DATA */
        if (data & 0x80U) {
            I2C_SDA_HIGH(priv);
        } else {
            I2C_SDA_LOW(priv);
        }
        I2C_DELAY(priv);

        /* CLK */
        I2C_SCL_HIGH(priv);
        I2C_DELAY(priv);
        I2C_SCL_LOW(priv);
        I2C_DELAY(priv);
        data <<= 1;
    }

    /* ACK */
    return i2c_check_ack(priv);
}

static void i2c_answer_ack(drv_i2c_priv_t *priv, mr_bool_t ack) {
    /* ACK = LOW, NACK = HIGH */
    if (ack) {
        I2C_SDA_LOW(priv);
    } else {
        I2C_SDA_HIGH(priv);
    }
    I2C_DELAY(priv);

    /* CLK */
    I2C_SCL_HIGH(priv);
    I2C_DELAY(priv);
    I2C_SCL_LOW(priv);
    I2C_DELAY(priv);
}

static mr_uint8_t i2c_read_byte(drv_i2c_priv_t *priv, mr_bool_t ack) {
    mr_uint8_t i, data, level;

    /* RELEASE */
    I2C_SDA_HIGH(priv);

    /* RECV */
    for (data = 0, i = 0; i < 8; i++) {
        /* CLK */
        I2C_SCL_HIGH(priv);
        I2C_DELAY(priv);

        /* DATA */
        I2C_SDA_READ(priv, level);
        data = (mr_uint8_t)(data << 1U);
        if (level == MR_GPIO_LEVEL_HIGH) {
            data |= 0x1U;
        }

        /* CLK */
        I2C_SCL_LOW(priv);
        I2C_DELAY(priv);
    }

    /* ACK */
    i2c_answer_ack(priv, ack);

    /* RELEASE */
    I2C_SDA_HIGH(priv);
    return data;
}

static mr_err_t i2c_write_addr(drv_i2c_priv_t *priv, mr_uint16_t addr,
                               mr_bool_t is_addr_bits10, mr_bool_t is_read) {
    mr_uint8_t addr_byte;
    mr_bool_t ret;

    /* ADDRESS */
    if (!is_addr_bits10) {
        /* 7-BITS ADDRESS */
        addr_byte = (mr_uint8_t)(addr << 0x1U);
        if (is_read) {
            addr_byte |= 0x1U;
        }
        ret = i2c_write_byte(priv, addr_byte);
        if (ret != 0) {
            return ret;
        }
    } else {
        /* 10-BITS ADDRESS */
        addr_byte = 0xf0U | ((addr >> 7U) & 0x06U);
        if (is_read) {
            addr_byte |= 0x1U;
        }
        ret = i2c_write_byte(priv, addr_byte);
        if (ret != 0) {
            return ret;
        }
        ret = i2c_write_byte(priv, (mr_uint8_t)addr);
        if (ret != 0) {
            return ret;
        }

        if (is_read) {
            /* 10-BITS-RESTART */
            i2c_start(priv);
            addr_byte = 0xf1U | ((addr >> 7U) & 0x06U);
            ret = i2c_write_byte(priv, addr_byte);
            if (ret != 0) {
                return ret;
            }
        }
    }
    return 0;
}

static int drv_i2c_xfer(const mr_driver_t *drv, mr_i2c_msg_t *msg) {
    mr_bool_t is_addr_bits10, is_start, is_read, is_stop;
    drv_i2c_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint8_t *buf;
    mr_uint32_t i;
    int ret;

    /* Get xfer flags and buffer */
    is_addr_bits10 = (mr_bool_t)(msg->flags & MR_I2C_MSG_ADDR_BITS_10);
    is_start = (mr_bool_t)(msg->flags & MR_I2C_MSG_START);
    is_read = (mr_bool_t)(msg->flags & MR_I2C_MSG_READ);
    is_stop = (mr_bool_t)(msg->flags & MR_I2C_MSG_STOP);
    buf = (mr_uint8_t *)msg->buf;

    /* START */
    if (is_start) {
        i2c_start(priv);
    }

    /* ADDRESS */
    ret = i2c_write_addr(priv, msg->addr, is_addr_bits10, is_read);
    if (ret != 0) {
        goto _exit;
    }

    /* DATA */
    if (!is_read) {
        /* WRITE */
        for (i = 0; i < msg->size; i++) {
            ret = i2c_write_byte(priv, buf[i]);
            if (ret != 0) {
                goto _exit;
            }
        }
    } else {
        /* READ */
        for (i = 0; i < msg->size; i++) {
            buf[i] = i2c_read_byte(priv, i < (msg->size - 1));
        }
    }

    /* STOP */
    if (is_stop) {
        i2c_stop(priv);
    }
    return (int)msg->size;
_exit:
    /* STOP */
    i2c_stop(priv);
    return ret;
}

static mr_err_t drv_i2c_recover(const mr_driver_t *drv) {
    drv_i2c_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint8_t level, i;
    mr_err_t ret;

    /* RELEASE */
    I2C_SDA_HIGH(priv);

    /* SEND(9PULSE) */
    for (i = 0; i < 9; i++) {
        /* RELEASE = HIGH, STUCK = LOW */
        I2C_SDA_READ(priv, level);
        if (level == MR_GPIO_LEVEL_HIGH) {
            break;
        }

        /* CLK */
        I2C_SCL_HIGH(priv);
        I2C_DELAY(priv);
        I2C_SCL_LOW(priv);
        I2C_DELAY(priv);
    }

    /* RELEASE = HIGH, STUCK = LOW */
    I2C_SDA_READ(priv, level);
    if (level != MR_GPIO_LEVEL_HIGH) {
        /* STUCK */
        ret = -MR_ETIMEDOUT;
    } else {
        ret = 0;
    }

    /* STOP */
    i2c_stop(priv);
    return ret;
}

/* Instance install */
#define I2C_GPIO_HT_INST_I2C_INSTALL(_id, _ops)                                \
    static drv_i2c_priv_t __drv_i2c_priv_##_id                                 \
        = {.scl = MR_HT_GPIO_SPEC(MR_HT_INST_DRIVER(_id), scl_gpios),          \
           .sda = MR_HT_GPIO_SPEC(MR_HT_INST_DRIVER(_id), sda_gpios),          \
           .delay_us = 1};                                                     \
    MR_HT_INST_I2C_INSTALL(_id, &__drv_i2c_priv_##_id, _ops)
MR_HT_INST_DRIVER_FOREACH(I2C_GPIO_HT_INST_I2C_INSTALL, &__drv_i2c_ops);
#endif /* defined(MR_USE_DRIVER_I2C_GPIOS) */

/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/i2c/mr_i2c.h>
#if defined(MR_USE_DRIVER_I2C) && defined(MR_USE_DRIVER_GPIO)
#include <mrX/drivers/gpio/mr_gpio.h>
#include <mrX/kernel/mr_clock.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    i2c_gpio
#if MR_HT_HAS_DRIVER()

/* GPIO I2C definition */
#define I2C_GPIO_MAX_CLOCK_RATE         (100000U)
#define I2C_GPIO_STRETCH_TIMEOUT_US     (25000U)
#define I2C_GPIO_BUS_CLEAR_CLOCKS       (9U)

/* Driver I2C operations */
static mr_err_t drv_configure(const mr_driver_t *drv,
                              const mr_i2c_config_t *config);
static mr_err_t drv_configuration(const mr_driver_t *drv,
                                  mr_i2c_config_t *config);
static mr_ssize_t drv_transfer(const mr_driver_t *drv, const mr_i2c_msg_t *msgs,
                               mr_size_t num);
MR_UNUSED static const mr_i2c_ops_t __drv_i2c_gpio_ops = {
    .configure = drv_configure,
    .configuration = drv_configuration,
    .transfer = drv_transfer,
};

/* Driver I2C private data type */
typedef struct drv_i2c_gpio_priv {
    mr_ht_gpio_spec_t scl;
    mr_ht_gpio_spec_t sda;
    mr_i2c_config_t config;
    mr_uint32_t half_period_us;
    mr_uint32_t stretch_timeout_us;
} drv_i2c_gpio_priv_t;

static void i2c_gpio_delay(mr_uint32_t us) {
    /* Delay software bus timing */
    if (us != 0U) {
        mr_clock_busy_wait(us);
    }
}

static mr_err_t i2c_gpio_write(mr_ht_gpio_spec_t *gpio, mr_uint8_t value) {
    mr_ssize_t ret;

    /* Write GPIO level */
    ret = mr_ht_gpio_write(gpio, value);
    return (ret < 0) ? (mr_err_t)ret : 0;
}

static mr_err_t i2c_gpio_read(mr_ht_gpio_spec_t *gpio, mr_uint8_t *value) {
    mr_ssize_t ret;

    /* Read GPIO level */
    ret = mr_ht_gpio_read(gpio, value);
    if (ret < 0) {
        return (mr_err_t)ret;
    }
    /* Normalize GPIO level */
    *value = !!(*value);
    return 0;
}

static void i2c_gpio_release_bus(drv_i2c_gpio_priv_t *priv) {
    /* Open-drain HIGH releases a line instead of driving it high */
    i2c_gpio_write(&priv->sda, MR_GPIO_LEVEL_HIGH);
    i2c_gpio_write(&priv->scl, MR_GPIO_LEVEL_HIGH);
}

static mr_err_t i2c_gpio_scl_high(drv_i2c_gpio_priv_t *priv) {
    mr_uint32_t elapsed, delay;
    mr_uint8_t value;
    mr_err_t ret;

    /* Release SCL and wait for a slave which is stretching the clock */
    ret = i2c_gpio_write(&priv->scl, MR_GPIO_LEVEL_HIGH);
    if (ret != 0) {
        return ret;
    }
    elapsed = 0;
    while (MR_TRUE) {
        ret = i2c_gpio_read(&priv->scl, &value);
        if (ret != 0) {
            return ret;
        }
        if (value != 0U) {
            i2c_gpio_delay(priv->half_period_us);
            return 0;
        }
        if (elapsed >= priv->stretch_timeout_us) {
            return -MR_ETIMEDOUT;
        }
        delay = priv->stretch_timeout_us - elapsed;
        delay = MR_MIN(priv->half_period_us, delay);
        i2c_gpio_delay(delay);
        elapsed += delay;
    }
}

static mr_err_t i2c_gpio_start(drv_i2c_gpio_priv_t *priv, mr_bool_t restart) {
    mr_uint8_t value;
    mr_err_t ret;

    /* Prepare SDA while SCL is low, then release SCL */
    ret = i2c_gpio_write(&priv->sda, MR_GPIO_LEVEL_HIGH);
    if (ret != 0) {
        return ret;
    }
    i2c_gpio_delay(priv->half_period_us);
    ret = i2c_gpio_scl_high(priv);
    if (ret != 0) {
        return ret;
    }

    /* A normal START may only begin on an idle bus */
    if (!restart) {
        ret = i2c_gpio_read(&priv->sda, &value);
        if (ret != 0) {
            return ret;
        }
        if (value == 0U) {
            return -MR_EBUSY;
        }
    }

    /* SDA falling while SCL is high creates START or repeated START */
    ret = i2c_gpio_write(&priv->sda, MR_GPIO_LEVEL_LOW);
    if (ret != 0) {
        return ret;
    }
    i2c_gpio_delay(priv->half_period_us);
    ret = i2c_gpio_write(&priv->scl, MR_GPIO_LEVEL_LOW);
    if (ret != 0) {
        i2c_gpio_release_bus(priv);
        return ret;
    }
    return 0;
}

static mr_err_t i2c_gpio_stop(drv_i2c_gpio_priv_t *priv) {
    mr_uint8_t value;
    mr_err_t ret;

    /* SDA rising while SCL is high creates STOP */
    ret = i2c_gpio_write(&priv->sda, MR_GPIO_LEVEL_LOW);
    if (ret != 0) {
        return ret;
    }
    i2c_gpio_delay(priv->half_period_us);
    ret = i2c_gpio_scl_high(priv);
    if (ret != 0) {
        return ret;
    }
    ret = i2c_gpio_write(&priv->sda, MR_GPIO_LEVEL_HIGH);
    if (ret != 0) {
        return ret;
    }
    i2c_gpio_delay(priv->half_period_us);

    /* Detect a slave which still holds SDA after STOP */
    ret = i2c_gpio_read(&priv->sda, &value);
    if (ret != 0) {
        return ret;
    }
    return (value != 0U) ? 0 : -MR_EBUSY;
}

static mr_err_t i2c_gpio_bus_clear(drv_i2c_gpio_priv_t *priv) {
    mr_uint32_t clock;
    mr_uint8_t value;
    mr_err_t ret;

    /* Release SDA and clock up to nine bits out of a stuck slave */
    ret = i2c_gpio_write(&priv->sda, MR_GPIO_LEVEL_HIGH);
    if (ret != 0) {
        return ret;
    }
    for (clock = 0U; clock < I2C_GPIO_BUS_CLEAR_CLOCKS; clock++) {
        ret = i2c_gpio_write(&priv->scl, MR_GPIO_LEVEL_LOW);
        if (ret != 0) {
            return ret;
        }
        i2c_gpio_delay(priv->half_period_us);
        ret = i2c_gpio_scl_high(priv);
        if (ret != 0) {
            return ret;
        }
        ret = i2c_gpio_read(&priv->sda, &value);
        if (ret != 0) {
            return ret;
        }
        if (value != 0U) {
            break;
        }
    }

    /* Finish recovery with a well-formed STOP */
    ret = i2c_gpio_write(&priv->scl, MR_GPIO_LEVEL_LOW);
    if (ret != 0) {
        return ret;
    }
    i2c_gpio_delay(priv->half_period_us);
    return i2c_gpio_stop(priv);
}

static mr_err_t i2c_gpio_write_bit(drv_i2c_gpio_priv_t *priv, mr_uint8_t bit) {
    mr_uint8_t value;
    mr_err_t ret;

    /* Set data during the SCL low period */
    bit = !!bit;
    ret = i2c_gpio_write(&priv->sda, bit);
    if (ret != 0) {
        return ret;
    }

    /* Keep the data stable before raising SCL */
    i2c_gpio_delay(priv->half_period_us);

    /* Raise SCL and handle clock stretching */
    ret = i2c_gpio_scl_high(priv);
    if (ret != 0) {
        return ret;
    }

    /* Losing a released HIGH bit means another master won arbitration */
    if (bit != 0U) {
        ret = i2c_gpio_read(&priv->sda, &value);
        if (ret != 0) {
            return ret;
        }
        if (value == 0U) {
            i2c_gpio_release_bus(priv);
            return -MR_EAGAIN;
        }
    }
    return i2c_gpio_write(&priv->scl, MR_GPIO_LEVEL_LOW);
}

static mr_err_t i2c_gpio_read_bit(drv_i2c_gpio_priv_t *priv, mr_uint8_t *bit) {
    mr_err_t ret, low_ret;

    /* Release SDA before the slave drives the data or ACK bit */
    ret = i2c_gpio_write(&priv->sda, MR_GPIO_LEVEL_HIGH);
    if (ret != 0) {
        return ret;
    }
    i2c_gpio_delay(priv->half_period_us);
    ret = i2c_gpio_scl_high(priv);
    if (ret != 0) {
        return ret;
    }
    ret = i2c_gpio_read(&priv->sda, bit);
    low_ret = i2c_gpio_write(&priv->scl, MR_GPIO_LEVEL_LOW);
    return (ret != 0) ? ret : low_ret;
}

static mr_err_t i2c_gpio_write_byte(drv_i2c_gpio_priv_t *priv,
                                    mr_uint8_t byte) {
    mr_uint32_t bit;
    mr_uint8_t ack;
    mr_err_t ret;

    /* Write byte from MSB to LSB */
    for (bit = 0U; bit < 8U; bit++) {
        ret = i2c_gpio_write_bit(priv, byte & 0x80U);
        if (ret != 0) {
            return ret;
        }
        byte <<= 1;
    }

    /* ACK is an active LOW from the receiver */
    ret = i2c_gpio_read_bit(priv, &ack);
    if (ret != 0) {
        return ret;
    }
    return (ack == 0U) ? 0 : -MR_EIO;
}

static mr_err_t i2c_gpio_read_byte(drv_i2c_gpio_priv_t *priv, mr_uint8_t *byte,
                                   mr_bool_t send_ack) {
    mr_uint8_t value;
    mr_uint32_t bit;
    mr_err_t ret;

    /* Read byte from MSB to LSB */
    *byte = 0U;
    for (bit = 0U; bit < 8U; bit++) {
        ret = i2c_gpio_read_bit(priv, &value);
        if (ret != 0) {
            return ret;
        }
        *byte = (mr_uint8_t)((*byte << 1) | value);
    }

    /* ACK every byte except the final byte in a read message */
    return i2c_gpio_write_bit(priv, send_ack ? 0U : 1U);
}

static mr_err_t i2c_gpio_address(drv_i2c_gpio_priv_t *priv,
                                 const mr_i2c_msg_t *msg) {
    mr_uint8_t write_header, read_header;
    mr_bool_t read;
    mr_err_t ret;

    /* Get transfer direction from message flags */
    read = !!(msg->flags & MR_I2C_MSG_READ);

    /* Send 7-bit address and transfer direction */
    if (!(msg->flags & MR_I2C_MSG_ADDR10)) {
        return i2c_gpio_write_byte(priv, (mr_uint8_t)((msg->addr << 1) | read));
    }

    /* A 10-bit transfer always starts with the write form of the header */
    write_header = (mr_uint8_t)(0xf0U | ((msg->addr >> 7) & 0x06U));
    ret = i2c_gpio_write_byte(priv, write_header);
    if (ret != 0) {
        return ret;
    }
    ret = i2c_gpio_write_byte(priv, (mr_uint8_t)msg->addr);
    if ((ret != 0) || !read) {
        return ret;
    }

    /* A 10-bit read repeats START and retransmits the header with R/W set */
    ret = i2c_gpio_start(priv, MR_TRUE);
    if (ret != 0) {
        return ret;
    }
    read_header = (mr_uint8_t)(0xf1U | ((msg->addr >> 7) & 0x06U));
    return i2c_gpio_write_byte(priv, read_header);
}

static mr_err_t i2c_gpio_validate_messages(const mr_i2c_msg_t *msgs,
                                           mr_size_t num) {
    mr_size_t i;

    /* An empty message list is valid */
    if (num == 0U) {
        return 0;
    }

    /* Validate flags and address range for each message */
    for (i = 0U; i < num; i++) {
        /* STOP and NOSTOP cannot be requested together */
        if ((msgs[i].flags & (MR_I2C_MSG_STOP | MR_I2C_MSG_NOSTOP))
            == (MR_I2C_MSG_STOP | MR_I2C_MSG_NOSTOP)) {
            return -MR_EINVAL;
        }

        /* Check the address against the selected address width */
        if ((msgs[i].flags & MR_I2C_MSG_ADDR10) != 0U) {
            if (msgs[i].addr > 0x03ffU) {
                return -MR_EINVAL;
            }
        } else if (msgs[i].addr > 0x007fU) {
            return -MR_EINVAL;
        }
    }

    /* Holding the bus across transfer() calls has no public ownership API */
    if (msgs[num - 1U].flags & MR_I2C_MSG_NOSTOP) {
        return -MR_ENOSYS;
    }
    return 0;
}

static mr_err_t drv_configure(const mr_driver_t *drv,
                              const mr_i2c_config_t *config) {
    drv_i2c_gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_i2c_config_t new_config = *config;
    mr_uint8_t value;
    mr_err_t ret;

    /* GPIO I2C supports only Standard-mode Master operation */
    if (new_config.role != MR_I2C_ROLE_MASTER) {
        return -MR_ENOSYS;
    }
    if (new_config.clock_rate > I2C_GPIO_MAX_CLOCK_RATE) {
        return -MR_ENOSYS;
    }

    /* A failed hardware reconfiguration must leave transfers disabled */
    priv->config = new_config;
    priv->config.clock_rate = 0U;
    priv->half_period_us = 0U;

    /* A zero clock releases both pins and disables transfers */
    if (new_config.clock_rate == 0U) {
        ret = mr_ht_gpio_input(&priv->sda, MR_GPIO_PULL_NONE);
        if (ret != 0) {
            return ret;
        }
        ret = mr_ht_gpio_input(&priv->scl, MR_GPIO_PULL_NONE);
        if (ret != 0) {
            return ret;
        }
        return 0;
    }

    /* Generic GPIO calls cannot guarantee timing above Standard-mode */
    priv->half_period_us = 500000U + new_config.clock_rate - 1U;
    priv->half_period_us /= new_config.clock_rate;

    /* External pull-ups are required; HIGH means release in open-drain mode */
    ret = mr_ht_gpio_output(&priv->sda, MR_GPIO_OTYPE_OD, MR_GPIO_LEVEL_HIGH);
    if (ret != 0) {
        return ret;
    }
    ret = mr_ht_gpio_output(&priv->scl, MR_GPIO_OTYPE_OD, MR_GPIO_LEVEL_HIGH);
    if (ret != 0) {
        return ret;
    }

    /* Wait for SCL and recover a slave which was reset mid-byte */
    ret = i2c_gpio_scl_high(priv);
    if (ret != 0) {
        i2c_gpio_release_bus(priv);
        return ret;
    }
    ret = i2c_gpio_read(&priv->sda, &value);
    if (ret != 0) {
        i2c_gpio_release_bus(priv);
        return ret;
    }
    if (value == 0U) {
        ret = i2c_gpio_bus_clear(priv);
        if (ret != 0) {
            i2c_gpio_release_bus(priv);
            return ret;
        }
    }
    priv->config = new_config;
    return 0;
}

static mr_err_t drv_configuration(const mr_driver_t *drv,
                                  mr_i2c_config_t *config) {
    drv_i2c_gpio_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Get the current GPIO I2C configuration */
    *config = priv->config;
    return 0;
}

static mr_ssize_t drv_transfer(const mr_driver_t *drv, const mr_i2c_msg_t *msgs,
                               mr_size_t num) {
    drv_i2c_gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    const mr_i2c_msg_t *msg;
    mr_bool_t active;
    mr_size_t i, j;
    mr_err_t ret;

    /* An empty transfer completes without accessing the bus */
    if (num == 0U) {
        return 0;
    }

    /* Reject transfers while the controller is disabled */
    if (priv->config.clock_rate == 0U) {
        return -MR_ENODEV;
    }

    /* Validate all messages before taking control of the bus */
    ret = i2c_gpio_validate_messages(msgs, num);
    if (ret != 0) {
        return ret;
    }

    /* Transfer each message as one addressed bus transaction */
    for (active = MR_FALSE, i = 0U; i < num; i++) {
        msg = &msgs[i];

        /* Every message begins with START or repeated START and an address */
        ret = i2c_gpio_start(priv, active);
        if (ret != 0) {
            goto _error;
        }
        active = MR_TRUE;
        ret = i2c_gpio_address(priv, msg);
        if (ret != 0) {
            goto _error;
        }

        /* Transfer message payload in the requested direction */
        for (j = 0U; j < msg->len; j++) {
            if (msg->flags & MR_I2C_MSG_READ) {
                ret = i2c_gpio_read_byte(priv, &msg->buf[j],
                                         (j + 1U) < msg->len);
            } else {
                ret = i2c_gpio_write_byte(priv, msg->buf[j]);
            }
            if (ret != 0) {
                goto _error;
            }
        }

        /* STOP splits a group; final message stops unless NOSTOP rejected */
        if ((msg->flags & MR_I2C_MSG_STOP) || ((i + 1U) == num)) {
            ret = i2c_gpio_stop(priv);
            if (ret != 0) {
                goto _error;
            }
            active = MR_FALSE;
        }
    }

    /* Return the number of messages transferred successfully */
    return (mr_ssize_t)num;
_error:
    /* Release or terminate the bus according to the failure type */
    if (ret == -MR_EAGAIN) {
        /* Arbitration loss forbids generating STOP on another master's bus */
        i2c_gpio_release_bus(priv);
    } else if (active) {
        /* Preserve the original error while making a best effort to stop */
        i2c_gpio_stop(priv);
        i2c_gpio_release_bus(priv);
    }
    return ret;
}

/* Instance install */
#define I2C_GPIO_HT_I2C_INSTALL(_id, _ops)                                     \
    static drv_i2c_gpio_priv_t MR_CAT(__drv_i2c_gpio_priv_, _id) = {           \
        .scl = MR_HT_GPIO_SPEC(_id, scl),                                      \
        .sda = MR_HT_GPIO_SPEC(_id, sda),                                      \
        .stretch_timeout_us = MR_HT_DRIVER_PROP_OR(                            \
            _id, stretch_timeout_us, I2C_GPIO_STRETCH_TIMEOUT_US),             \
    };                                                                         \
    MR_HT_I2C_INSTALL(_id, &MR_CAT(__drv_i2c_gpio_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(I2C_GPIO_HT_I2C_INSTALL, &__drv_i2c_gpio_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_I2C) && defined(MR_USE_DRIVER_GPIO) */

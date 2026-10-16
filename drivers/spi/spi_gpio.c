/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/spi/mr_spi.h>
#if defined(MR_USE_DRIVER_SPI) && defined(MR_USE_DRIVER_GPIO)
#include <mrX/drivers/gpio/mr_gpio.h>
#include <mrX/kernel/mr_service.h>
#include <mrX/kernel/mr_clock.h>
#include <mrX/libc/mr_string.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    spi_gpio
#if MR_HT_HAS_DRIVER()

/* GPIO SPI definition */
#define SPI_GPIO_MAX_CLOCK_RATE         (500000U)

/* Driver SPI private data type */
typedef struct drv_spi_gpio_priv {
    mr_ht_gpio_spec_t sck;
    mr_ht_gpio_spec_t mosi;
    mr_ht_gpio_spec_t miso;
    mr_ht_gpio_spec_t *cs;
    mr_size_t cs_num;
    mr_bool_t cs_ready;
    mr_spi_config_t *configs;
} drv_spi_gpio_priv_t;

/* Driver SPI operations */
static mr_err_t drv_configure(const mr_driver_t *drv, mr_uint32_t cs,
                              const mr_spi_config_t *config);
static mr_err_t drv_configuration(const mr_driver_t *drv, mr_uint32_t cs,
                                  mr_spi_config_t *config);
static mr_ssize_t drv_transfer(const mr_driver_t *drv, mr_uint32_t cs,
                               const mr_spi_msg_t *msgs, mr_size_t num);
MR_UNUSED static const mr_spi_ops_t __drv_spi_gpio_ops = {
    .configure = drv_configure,
    .configuration = drv_configuration,
    .transfer = drv_transfer,
};

static void spi_gpio_delay(mr_uint32_t us) {
    /* Delay software bus timing */
    if (us != 0U) {
        mr_clock_busy_wait(us);
    }
}

static mr_err_t spi_gpio_write(mr_ht_gpio_spec_t *gpio, mr_uint8_t value) {
    mr_ssize_t ret;

    /* Write GPIO level */
    ret = mr_ht_gpio_write(gpio, value);
    return (ret < 0) ? (mr_err_t)ret : 0;
}

static mr_err_t spi_gpio_read(mr_ht_gpio_spec_t *gpio, mr_uint8_t *value) {
    mr_ssize_t ret;

    /* Read GPIO level */
    ret = mr_ht_gpio_read(gpio, value);
    return (ret < 0) ? (mr_err_t)ret : 0;
}

static mr_bool_t spi_gpio_has_pin(const mr_ht_gpio_spec_t *gpio) {
    /* Check whether an optional GPIO pin is present */
    return gpio->drv != MR_NULL;
}

static mr_err_t spi_gpio_cs_setup(drv_spi_gpio_priv_t *priv) {
    mr_err_t ret;
    mr_size_t i;

    /* Configure every chip-select output in its inactive state */
    for (i = 0U; i < priv->cs_num; i++) {
        ret = mr_ht_gpio_output(&priv->cs[i], MR_GPIO_OTYPE_PP,
                                MR_GPIO_LEVEL_HIGH);
        if (ret != 0) {
            return ret;
        }
    }

    /* Mark chip-select outputs ready after all pins are configured */
    priv->cs_ready = MR_TRUE;
    return 0;
}

static mr_err_t spi_gpio_cs_control(drv_spi_gpio_priv_t *priv, mr_uint32_t cs,
                                    mr_bool_t active) {
    mr_uint8_t value;

    /* Drive the selected chip-select line */
    value = active ? MR_GPIO_LEVEL_LOW : MR_GPIO_LEVEL_HIGH;
    return spi_gpio_write(&priv->cs[cs], value);
}

static mr_err_t spi_gpio_validate_messages(const drv_spi_gpio_priv_t *priv,
                                           const mr_spi_msg_t *msgs,
                                           mr_size_t num,
                                           mr_size_t frame_size) {
    mr_size_t i;

    /* Validate all messages before asserting chip select */
    for (i = 0U; i < num; i++) {
        /* Buffer lengths must contain complete frames */
        if (((msgs[i].tx_len % frame_size) != 0U)
            || ((msgs[i].rx_len % frame_size) != 0U)) {
            return -MR_EINVAL;
        }

        /* Receiving data requires a configured MISO pin */
        if ((msgs[i].rx_buf != MR_NULL) && (msgs[i].rx_len != 0U)
            && !spi_gpio_has_pin(&priv->miso)) {
            return -MR_ENOSYS;
        }
    }
    return 0;
}

static mr_err_t spi_gpio_prepare(drv_spi_gpio_priv_t *priv,
                                 const mr_spi_config_t *config) {
    mr_uint8_t idle;
    mr_err_t ret;

    /* Configure clock idle level and master output pin */
    idle = (config->mode & 0x02U) ? MR_GPIO_LEVEL_HIGH : MR_GPIO_LEVEL_LOW;
    ret = mr_ht_gpio_output(&priv->sck, MR_GPIO_OTYPE_PP, idle);
    if (ret != 0) {
        return ret;
    }
    ret = mr_ht_gpio_output(&priv->mosi, MR_GPIO_OTYPE_PP, MR_GPIO_LEVEL_LOW);
    if (ret != 0) {
        return ret;
    }

    /* Configure the optional master input pin */
    if (spi_gpio_has_pin(&priv->miso)) {
        ret = mr_ht_gpio_input(&priv->miso, MR_GPIO_PULL_NONE);
        if (ret != 0) {
            return ret;
        }
    }
    return 0;
}

static mr_uint32_t spi_gpio_frame_load(const void *buf, mr_size_t frame,
                                       mr_size_t frame_size) {
    const mr_uint8_t *src;
    mr_uint32_t value;

    /* Load one frame without assuming buffer alignment */
    src = (const mr_uint8_t *)buf + frame * frame_size;
    value = 0;
    mr_memcpy(&value, src, frame_size);
    return value;
}

static mr_err_t spi_gpio_transfer_frame(drv_spi_gpio_priv_t *priv,
                                        const mr_spi_config_t *config,
                                        mr_uint32_t half_period_us,
                                        mr_uint32_t tx, mr_uint32_t *rx) {
    mr_uint8_t idle, active, value;
    mr_uint32_t bit, index;
    mr_err_t ret;

    /* Derive clock levels from CPOL and clear the receive frame */
    idle = (config->mode & 0x02U) ? MR_GPIO_LEVEL_HIGH : MR_GPIO_LEVEL_LOW;
    active = !idle;
    *rx = 0U;

    /* Shift every configured data bit in the selected bit order */
    for (bit = 0U; bit < config->data_bits; bit++) {
        index = (config->bit_order == MR_SPI_BIT_ORDER_LSB)
                  ? bit
                  : (mr_uint32_t)config->data_bits - bit - 1U;

        if ((config->mode & 0x01U) == 0U) {
            /* CPHA=0 changes data before and samples on the leading edge */
            ret = spi_gpio_write(&priv->mosi, (tx >> index) & 0x01U);
            if (ret != 0) {
                return ret;
            }
            spi_gpio_delay(half_period_us);
            ret = spi_gpio_write(&priv->sck, active);
            if (ret != 0) {
                return ret;
            }
            if (spi_gpio_has_pin(&priv->miso)) {
                ret = spi_gpio_read(&priv->miso, &value);
                if (ret != 0) {
                    return ret;
                }
                *rx |= (mr_uint32_t)value << index;
            }
            spi_gpio_delay(half_period_us);
            ret = spi_gpio_write(&priv->sck, idle);
            if (ret != 0) {
                return ret;
            }
        } else {
            /* CPHA=1 changes data on leading and samples on trailing edge */
            ret = spi_gpio_write(&priv->sck, active);
            if (ret != 0) {
                return ret;
            }
            ret = spi_gpio_write(&priv->mosi, (tx >> index) & 0x01U);
            if (ret != 0) {
                return ret;
            }
            spi_gpio_delay(half_period_us);
            ret = spi_gpio_write(&priv->sck, idle);
            if (ret != 0) {
                return ret;
            }
            if (spi_gpio_has_pin(&priv->miso)) {
                ret = spi_gpio_read(&priv->miso, &value);
                if (ret != 0) {
                    return ret;
                }
                *rx |= (mr_uint32_t)value << index;
            }
            spi_gpio_delay(half_period_us);
        }
    }
    return 0;
}

static void spi_gpio_frame_store(void *buf, mr_size_t frame,
                                 mr_size_t frame_size, mr_uint32_t value) {
    mr_uint8_t *dst;

    /* Store one frame without assuming buffer alignment */
    dst = (mr_uint8_t *)buf + frame * frame_size;
    mr_memcpy(dst, &value, frame_size);
}

static mr_err_t drv_configure(const mr_driver_t *drv, mr_uint32_t cs,
                              const mr_spi_config_t *config) {
    drv_spi_gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_spi_config_t new_config;
    mr_err_t ret;

    /* Check the selected chip-select index */
    if (cs >= priv->cs_num) {
        return -MR_EINVAL;
    }

    /* Validate the requested GPIO SPI configuration */
    new_config = *config;
    if ((new_config.clock_rate != 0U)
        && ((new_config.data_bits == 0U) || (new_config.data_bits > 32U))) {
        return -MR_EINVAL;
    }
    if ((new_config.clock_rate != 0U)
        && (new_config.role != MR_SPI_ROLE_MASTER)) {
        return -MR_ENOSYS;
    }
    if (new_config.clock_rate > SPI_GPIO_MAX_CLOCK_RATE) {
        return -MR_ENOSYS;
    }

    /* A failed reconfiguration leaves this chip select disabled */
    priv->configs[cs] = new_config;
    priv->configs[cs].clock_rate = 0U;

    /* Initialize all chip selects safely before the first configuration */
    if (!priv->cs_ready) {
        ret = spi_gpio_cs_setup(priv);
        if (ret != 0) {
            return ret;
        }
    }

    /* Keep the selected device inactive while saving its configuration */
    ret = spi_gpio_cs_control(priv, cs, MR_FALSE);
    if (ret != 0) {
        return ret;
    }
    priv->configs[cs] = new_config;
    return 0;
}

static mr_err_t drv_configuration(const mr_driver_t *drv, mr_uint32_t cs,
                                  mr_spi_config_t *config) {
    drv_spi_gpio_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check the selected chip-select index */
    if (cs >= priv->cs_num) {
        return -MR_EINVAL;
    }

    /* Get the selected GPIO SPI configuration */
    *config = priv->configs[cs];
    return 0;
}

static mr_ssize_t drv_transfer(const mr_driver_t *drv, mr_uint32_t cs,
                               const mr_spi_msg_t *msgs, mr_size_t num) {
    mr_size_t frame_size, tx_frames, rx_frames, frame_num, i, j;
    drv_spi_gpio_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t half_period_us, frame_mask, tx, rx;
    const mr_spi_config_t *config;
    const mr_spi_msg_t *msg;
    mr_err_t cleanup, ret;

    /* Check the selected chip-select index */
    if (cs >= priv->cs_num) {
        return -MR_EINVAL;
    }

    /* Reject transfers while this chip select is disabled */
    config = &priv->configs[cs];
    if (config->clock_rate == 0U) {
        return -MR_ENODEV;
    }

    /* Validate message buffers against the configured frame width */
    if (config->data_bits <= 8U) {
        frame_size = sizeof(mr_uint8_t);
    } else if (config->data_bits <= 16U) {
        frame_size = sizeof(mr_uint16_t);
    } else {
        frame_size = sizeof(mr_uint32_t);
    }
    ret = spi_gpio_validate_messages(priv, msgs, num, frame_size);
    if (ret != 0) {
        return ret;
    }

    /* Calculate bit timing and valid frame bits */
    half_period_us = 500000U + config->clock_rate - 1U;
    half_period_us /= config->clock_rate;
    if (config->data_bits != 32U) {
        frame_mask = (1UL << config->data_bits) - 1U;
    } else {
        frame_mask = 0xffffffffU;
    }

    /* Apply this chip-select configuration to the shared bus pins */
    ret = spi_gpio_prepare(priv, config);
    if (ret != 0) {
        return ret;
    }

    /* Assert chip select before starting clock activity */
    ret = spi_gpio_cs_control(priv, cs, MR_TRUE);
    if (ret != 0) {
        goto _exit;
    }
    spi_gpio_delay(half_period_us);

    /* Transfer every message while keeping the same chip select active */
    for (i = 0U; i < num; i++) {
        msg = &msgs[i];

        /* Clock enough frames to satisfy both transmit and receive lengths */
        tx_frames = msg->tx_len / frame_size;
        rx_frames = msg->rx_len / frame_size;
        frame_num = MR_MAX(tx_frames, rx_frames);
        for (j = 0U; j < frame_num; j++) {
            /* Transmit all ones when no transmit frame is provided */
            if ((msg->tx_buf != MR_NULL) && (j < tx_frames)) {
                tx = spi_gpio_frame_load(msg->tx_buf, j, frame_size);
            } else {
                tx = frame_mask;
            }
            ret = spi_gpio_transfer_frame(priv, config, half_period_us,
                                          tx & frame_mask, &rx);
            if (ret != 0) {
                goto _exit;
            }
            /* Store received data only while the receive buffer has frames */
            if ((msg->rx_buf != MR_NULL) && (j < rx_frames)) {
                spi_gpio_frame_store(msg->rx_buf, j, frame_size,
                                     rx & frame_mask);
            }
        }
    }
    ret = 0;
_exit:
    /* Return clock and chip select to their inactive levels on every path */
    cleanup = spi_gpio_write(&priv->sck, (config->mode & 0x02U)
                                             ? MR_GPIO_LEVEL_HIGH
                                             : MR_GPIO_LEVEL_LOW);
    if ((ret == 0) && (cleanup != 0)) {
        ret = cleanup;
    }
    spi_gpio_delay(half_period_us);
    cleanup = spi_gpio_cs_control(priv, cs, MR_FALSE);
    if ((ret == 0) && (cleanup != 0)) {
        ret = cleanup;
    }

    /* Return the transferred message count unless an operation failed */
    return (ret == 0) ? (mr_ssize_t)num : (mr_ssize_t)ret;
}

/* Instance install */
#define SPI_GPIO_HT_SPI_INSTALL(_id, _ops)                                     \
    static mr_ht_gpio_spec_t MR_CAT(__drv_spi_gpio_cs_, _id)[]                 \
        = {MR_HT_GPIO_SPEC_FOREACH(_id, cs)};                                  \
    static mr_spi_config_t MR_CAT(                                             \
        __drv_spi_gpio_configs_,                                               \
        _id)[MR_ARRAY_SIZE(MR_CAT(__drv_spi_gpio_cs_, _id))];                  \
    static drv_spi_gpio_priv_t MR_CAT(__drv_spi_gpio_priv_, _id) = {           \
        .sck = MR_HT_GPIO_SPEC(_id, sck),                                      \
        .mosi = MR_HT_GPIO_SPEC(_id, mosi),                                    \
        MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, miso),                          \
                    (.miso = MR_HT_GPIO_SPEC(_id, miso)), (.miso = {0})),      \
        .cs = MR_CAT(__drv_spi_gpio_cs_, _id),                                 \
        .cs_num = MR_ARRAY_SIZE(MR_CAT(__drv_spi_gpio_cs_, _id)),              \
        .configs = MR_CAT(__drv_spi_gpio_configs_, _id),                       \
    };                                                                         \
    MR_HT_SPI_INSTALL(_id, &MR_CAT(__drv_spi_gpio_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(SPI_GPIO_HT_SPI_INSTALL, &__drv_spi_gpio_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_SPI) && defined(MR_USE_DRIVER_GPIO) */

/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/gpio/mr_gpio.h>
#if defined(MR_USE_DRIVER_GPIO_HC595)

/* HardTree compatible */
#define MR_HT_COMPAT                    gpio_hc595

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
MR_UNUSED static const mr_gpio_ops_t __drv_gpio_hc595_ops = {
    .pin_configure = drv_pin_configure,
    .pin_configuration = drv_pin_configuration,
    .pin_write = drv_pin_write,
    .pin_read = drv_pin_read,
    .port_write = drv_port_write,
    .port_read = drv_port_read,
    .attach = MR_NULL,
};

/* Driver GPIO private data type */
typedef struct drv_gpio_hc595_priv {
    mr_ht_gpio_spec_t srclr;
    mr_ht_gpio_spec_t oe;
    mr_ht_gpio_spec_t ser;
    mr_ht_gpio_spec_t srclk;
    mr_ht_gpio_spec_t rclk;
    mr_uint8_t cascade_num;
    mr_uint32_t shadow;
    mr_bool_t configured;
} drv_gpio_hc595_priv_t;

static mr_err_t hc595_cascade_write(drv_gpio_hc595_priv_t *priv,
                                    mr_uint32_t shadow) {
    mr_uint32_t chip, bit, index;
    mr_uint8_t value;
    mr_ssize_t ret;

    /* Write chip */
    for (chip = 0; chip < priv->cascade_num; chip++) {
        for (bit = 8U; bit > 0U; bit--) {
            /* Write bit */
            index = (chip * 8U) + (bit - 1U);
            value = (mr_uint8_t)(shadow >> index);
            ret = mr_ht_gpio_write(&priv->ser, value & 0x01U);
            if (ret < 0) {
                return (mr_err_t)ret;
            }

            /* Clock pulse */
            ret = mr_ht_gpio_write(&priv->srclk, MR_GPIO_LEVEL_HIGH);
            if (ret < 0) {
                return (mr_err_t)ret;
            }
            ret = mr_ht_gpio_write(&priv->srclk, MR_GPIO_LEVEL_LOW);
            if (ret < 0) {
                return (mr_err_t)ret;
            }
        }
    }

    /* Latch pulse */
    ret = mr_ht_gpio_write(&priv->rclk, MR_GPIO_LEVEL_HIGH);
    if (ret < 0) {
        return (mr_err_t)ret;
    }
    ret = mr_ht_gpio_write(&priv->rclk, MR_GPIO_LEVEL_LOW);
    if (ret < 0) {
        return (mr_err_t)ret;
    }
    return 0;
}

static mr_err_t drv_pin_configure(const mr_driver_t *drv, mr_uint32_t pin,
                                  const mr_gpio_config_t *config) {
    drv_gpio_hc595_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((priv->cascade_num > 0U) && (priv->cascade_num <= 4U));

    /* Only support OUTPUT mode */
    if ((pin >= ((mr_uint32_t)priv->cascade_num * 8U))
        || (config->mode != MR_GPIO_MODE_OUTPUT)) {
        return -MR_EINVAL;
    }

    /* Check if the HC595 is already configured */
    if (priv->configured) {
        return 0;
    }

    /* Optional GPIOs(OE, SRCLR) */
    if (priv->oe.drv) {
        ret = mr_ht_gpio_output(&priv->oe, MR_GPIO_OTYPE_PP,
                                MR_GPIO_LEVEL_HIGH);
        if (ret != 0) {
            return ret;
        }
    }
    if (priv->srclr.drv) {
        ret = mr_ht_gpio_output(&priv->srclr, MR_GPIO_OTYPE_PP,
                                MR_GPIO_LEVEL_HIGH);
        if (ret != 0) {
            return ret;
        }
    }

    /* Required GPIOs(SER, SRCLK, RCLK) */
    ret = mr_ht_gpio_output(&priv->ser, MR_GPIO_OTYPE_PP, MR_GPIO_LEVEL_LOW);
    if (ret != 0) {
        return ret;
    }
    ret = mr_ht_gpio_output(&priv->srclk, MR_GPIO_OTYPE_PP, MR_GPIO_LEVEL_LOW);
    if (ret != 0) {
        return ret;
    }
    ret = mr_ht_gpio_output(&priv->rclk, MR_GPIO_OTYPE_PP, MR_GPIO_LEVEL_LOW);
    if (ret != 0) {
        return ret;
    }

    /* Write all chip data */
    ret = hc595_cascade_write(priv, 0);
    if (ret != 0) {
        return ret;
    }

    /* Enable output */
    if (priv->oe.drv) {
        ret = (mr_err_t)mr_ht_gpio_write(&priv->oe, MR_GPIO_LEVEL_LOW);
        if (ret < 0) {
            return ret;
        }
    }

    /* Mark the HC595 as configured */
    priv->configured = MR_TRUE;
    return 0;
}

static mr_err_t drv_pin_configuration(const mr_driver_t *drv, mr_uint32_t pin,
                                      mr_gpio_config_t *config) {
    MR_IGNORE(drv);
    MR_IGNORE(pin);

    /* Only support OUTPUT mode */
    config->mode = MR_GPIO_MODE_OUTPUT;
    config->pull = MR_GPIO_PULL_NONE;
    config->otype = MR_GPIO_OTYPE_PP;
    config->alt = MR_FALSE;
    return 0;
}

static mr_err_t drv_pin_write(const mr_driver_t *drv, mr_uint32_t pin,
                              mr_uint8_t value) {
    drv_gpio_hc595_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t shadow;
    mr_err_t ret;

    /* Set the pin value */
    shadow = priv->shadow;
    if (value) {
        shadow |= (1U << pin);
    } else {
        shadow &= ~(1U << pin);
    }
    if (shadow == priv->shadow) {
        return 0;
    }
    ret = hc595_cascade_write(priv, shadow);
    if (ret != 0) {
        return ret;
    }
    priv->shadow = shadow;
    return 0;
}

static mr_err_t drv_pin_read(const mr_driver_t *drv, mr_uint32_t pin,
                             mr_uint8_t *value) {
    drv_gpio_hc595_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Get the pin value */
    *value = (mr_uint8_t)((priv->shadow >> pin) & 0x01U);
    return 0;
}

static mr_err_t drv_port_write(const mr_driver_t *drv, mr_uint32_t mask) {
    drv_gpio_hc595_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;

    /* Set all chip data */
    ret = hc595_cascade_write(priv, mask);
    if (ret != 0) {
        return ret;
    }
    priv->shadow = mask;
    return 0;
}

static mr_err_t drv_port_read(const mr_driver_t *drv, mr_uint32_t *mask) {
    drv_gpio_hc595_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Get all chip data */
    *mask = priv->shadow;
    return 0;
}

/* Instance install */
#define GPIO_HC595_HT_INST_GPIO_INSTALL(_id, _ops)                             \
    static drv_gpio_hc595_priv_t MR_CAT(__drv_gpio_hc595_priv_, _id) = {       \
        MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, oe),                            \
                    (.oe = MR_HT_GPIO_SPEC(_id, oe)), (.oe = {0})),            \
        MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, srclr),                         \
                    (.srclr = MR_HT_GPIO_SPEC(_id, srclr)), (.srclr = {0})),   \
        .ser = MR_HT_GPIO_SPEC(_id, ser),                                      \
        .srclk = MR_HT_GPIO_SPEC(_id, srclk),                                  \
        .rclk = MR_HT_GPIO_SPEC(_id, rclk),                                    \
        .cascade_num = MR_HT_DRIVER_PROP_OR(_id, cascade_num, 1),              \
    };                                                                         \
    MR_HT_GPIO_INSTALL(_id, &MR_CAT(__drv_gpio_hc595_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(GPIO_HC595_HT_INST_GPIO_INSTALL, &__drv_gpio_hc595_ops);
#endif /* defined(MR_USE_DRIVER_GPIO_HC595) */

/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/gpio/mr_gpio.h>
#if defined(MR_USE_DRIVER_GPIO_HC165)

/* HardTree compatible */
#define MR_HT_COMPAT                    gpio_hc165

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
MR_UNUSED static const mr_gpio_ops_t __drv_gpio_hc165_ops = {
    .pin_configure = drv_pin_configure,
    .pin_configuration = drv_pin_configuration,
    .pin_write = drv_pin_write,
    .pin_read = drv_pin_read,
    .port_write = drv_port_write,
    .port_read = drv_port_read,
    .event_attach = MR_NULL,
};

/* Driver GPIO private data type */
typedef struct drv_gpio_hc165_priv {
    mr_ht_gpio_spec_t clk_inh;
    mr_ht_gpio_spec_t ser;
    mr_ht_gpio_spec_t clk;
    mr_ht_gpio_spec_t sh;
    mr_ht_gpio_spec_t qh;
    mr_uint8_t cascade_num;
    mr_bool_t configured;
} drv_gpio_hc165_priv_t;

static mr_err_t hc165_cascade_read(drv_gpio_hc165_priv_t *priv,
                                   mr_uint32_t *mask) {
    mr_uint32_t chip, bit, index;
    mr_uint8_t value;
    mr_ssize_t ret;

    /* Shift load */
    ret = mr_ht_gpio_write(&priv->sh, MR_GPIO_LEVEL_LOW);
    if (ret < 0) {
        return (mr_err_t)ret;
    }
    ret = mr_ht_gpio_write(&priv->sh, MR_GPIO_LEVEL_HIGH);
    if (ret < 0) {
        return (mr_err_t)ret;
    }

    /* Read chip */
    for (*mask = 0, chip = 0; chip < priv->cascade_num; chip++) {
        for (bit = 8U; bit > 0U; bit--) {
            /* Read bit */
            ret = mr_ht_gpio_read(&priv->qh, &value);
            if (ret < 0) {
                return (mr_err_t)ret;
            }
            index = ((priv->cascade_num - chip - 1U) * 8U) + (bit - 1U);
            *mask |= ((mr_uint32_t)(value & 0x01U) << index);

            /* Clock pulse */
            ret = mr_ht_gpio_write(&priv->clk, MR_GPIO_LEVEL_HIGH);
            if (ret < 0) {
                return (mr_err_t)ret;
            }
            ret = mr_ht_gpio_write(&priv->clk, MR_GPIO_LEVEL_LOW);
            if (ret < 0) {
                return (mr_err_t)ret;
            }
        }
    }
    return 0;
}

static mr_err_t drv_pin_configure(const mr_driver_t *drv, mr_uint32_t pin,
                                  const mr_gpio_config_t *config) {
    drv_gpio_hc165_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((priv->cascade_num > 0U) && (priv->cascade_num <= 4U));

    /* Only support INPUT mode */
    if ((pin >= ((mr_uint32_t)priv->cascade_num * 8U))
        || (config->mode != MR_GPIO_MODE_INPUT)) {
        return -MR_EINVAL;
    }

    /* Check if the HC165 is already configured */
    if (priv->configured) {
        return 0;
    }

    /* Optional GPIOs(CLK-INH, SER) */
    if (priv->clk_inh.drv) {
        ret = mr_ht_gpio_output(&priv->clk_inh, MR_GPIO_OTYPE_PP,
                                MR_GPIO_LEVEL_LOW);
        if (ret != 0) {
            return ret;
        }
    }
    if (priv->ser.drv) {
        ret = mr_ht_gpio_output(&priv->ser, MR_GPIO_OTYPE_PP,
                                MR_GPIO_LEVEL_LOW);
        if (ret != 0) {
            return ret;
        }
    }

    /* Required GPIOs(CLK, SH, QH) */
    ret = mr_ht_gpio_output(&priv->clk, MR_GPIO_OTYPE_PP, MR_GPIO_LEVEL_LOW);
    if (ret != 0) {
        return ret;
    }
    ret = mr_ht_gpio_output(&priv->sh, MR_GPIO_OTYPE_PP, MR_GPIO_LEVEL_HIGH);
    if (ret != 0) {
        return ret;
    }
    ret = mr_ht_gpio_input(&priv->qh, MR_GPIO_PULL_NONE);
    if (ret != 0) {
        return ret;
    }

    /* Mark the HC165 as configured */
    priv->configured = MR_TRUE;
    return 0;
}

static mr_err_t drv_pin_configuration(const mr_driver_t *drv, mr_uint32_t pin,
                                      mr_gpio_config_t *config) {
    MR_IGNORE(drv);
    MR_IGNORE(pin);

    /* Only support INPUT mode */
    config->mode = MR_GPIO_MODE_INPUT;
    config->pull = MR_GPIO_PULL_NONE;
    config->alt = MR_FALSE;
    return 0;
}

static mr_err_t drv_pin_write(const mr_driver_t *drv, mr_uint32_t pin,
                              mr_uint8_t value) {
    MR_IGNORE(drv);
    MR_IGNORE(pin);
    MR_IGNORE(value);

    /* Not support write */
    return -MR_EPERM;
}

static mr_err_t drv_pin_read(const mr_driver_t *drv, mr_uint32_t pin,
                             mr_uint8_t *value) {
    drv_gpio_hc165_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t mask;
    mr_err_t ret;

    /* Read all chip data */
    ret = hc165_cascade_read(priv, &mask);
    if (ret != 0) {
        return ret;
    }

    /* Get the pin value */
    *value = (mr_uint8_t)((mask >> pin) & 0x01U);
    return 0;
}

static mr_err_t drv_port_write(const mr_driver_t *drv, mr_uint32_t mask) {
    MR_IGNORE(drv);
    MR_IGNORE(mask);

    /* Not support write */
    return -MR_EPERM;
}

static mr_err_t drv_port_read(const mr_driver_t *drv, mr_uint32_t *mask) {
    drv_gpio_hc165_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Read all chip data */
    return hc165_cascade_read(priv, mask);
}

/* Instance install */
#define GPIO_HC165_HT_GPIO_INSTALL(_id, _ops)                                  \
    static drv_gpio_hc165_priv_t MR_CAT(__drv_gpio_hc165_priv_, _id) = {       \
        MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, clk_inh),                       \
                    (.clk_inh = MR_HT_GPIO_SPEC(_id, clk_inh)),                \
                    (.clk_inh = {0})),                                         \
        MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, ser),                           \
                    (.ser = MR_HT_GPIO_SPEC(_id, ser)), (.ser = {0})),         \
        .clk = MR_HT_GPIO_SPEC(_id, clk),                                      \
        .sh = MR_HT_GPIO_SPEC(_id, sh),                                        \
        .qh = MR_HT_GPIO_SPEC(_id, qh),                                        \
        .cascade_num = MR_HT_DRIVER_PROP_OR(_id, cascade_num, 1),              \
    };                                                                         \
    MR_HT_GPIO_INSTALL(_id, &MR_CAT(__drv_gpio_hc165_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(GPIO_HC165_HT_GPIO_INSTALL, &__drv_gpio_hc165_ops);
#endif /* defined(MR_USE_DRIVER_GPIO_HC165) */

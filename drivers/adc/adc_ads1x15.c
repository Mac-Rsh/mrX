/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/adc/mr_adc.h>
#if defined(MR_USE_DRIVER_ADC) && defined(MR_USE_DRIVER_I2C)
#include <mrX/drivers/i2c/mr_i2c.h>
#include <mrX/kernel/mr_clock.h>
#include <mrX/libc/mr_string.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    adc_ads1x15
#if MR_HT_HAS_DRIVER()

/* Register definition */
#define ADS1X15_REG_CONVERSION          0x00U
#define ADS1X15_REG_CONFIG              0x01U

/* Configuration register fields */
#define ADS1X15_CONFIG_OS               0x8000U
#define ADS1X15_CONFIG_MODE_SINGLE      0x0100U
#define ADS1X15_CONFIG_COMP_DISABLED    0x0003U
#define ADS1X15_CONFIG_MUX_SHIFT        12U
#define ADS1X15_CONFIG_PGA_SHIFT        9U
#define ADS1X15_CONFIG_DR_SHIFT         5U

/* Driver ADC private data type */
typedef struct drv_adc_ads1x15_priv {
    mr_driver_t *bus;
    mr_uint32_t addr;
    mr_uint32_t resolution;
    mr_uint32_t data_rate;
    mr_adc_config_t channels[4];
} drv_adc_ads1x15_priv_t;

/* Driver ADC operations */
static mr_err_t drv_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_adc_config_t *config);
static mr_err_t drv_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_adc_config_t *config);
static mr_ssize_t drv_read(const mr_driver_t *drv, mr_adc_seq_t *seq);
MR_UNUSED static const mr_adc_ops_t __drv_adc_ads1x15_ops = {
    .channel_configure = drv_channel_configure,
    .channel_configuration = drv_channel_configuration,
    .read = drv_read,
#if defined(MR_USE_DRIVER_ADC_STREAM)
    .stream_start = MR_NULL,
    .stream_stop = MR_NULL,
    .stream_is_busy = MR_NULL,
    .stream_offset = MR_NULL,
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */
    .event_control = MR_NULL,
};

static mr_err_t ads1x15_reg_write(drv_adc_ads1x15_priv_t *priv, mr_uint8_t reg,
                                  mr_uint16_t value) {
    mr_i2c_msg_t msg;
    mr_uint8_t tx[3];
    mr_ssize_t ret;

    /* Build register write frame */
    tx[0] = reg;
    tx[1] = (mr_uint8_t)(value >> 8);
    tx[2] = (mr_uint8_t)value;

    /* Send register and 16-bit value */
    msg.addr = (mr_uint16_t)priv->addr;
    msg.flags = MR_I2C_MSG_STOP;
    msg.buf = tx;
    msg.len = sizeof(tx);

    /* Transfer message */
    ret = mr_i2c_transfer(priv->bus, &msg, 1U);
    if (ret < 0) {
        return (mr_err_t)ret;
    }
    if (ret != 1) {
        return -MR_EIO;
    }
    return 0;
}

static mr_err_t ads1x15_reg_read(drv_adc_ads1x15_priv_t *priv, mr_uint8_t reg,
                                 mr_uint16_t *value) {
    mr_i2c_msg_t msgs[2];
    mr_uint8_t rx[2];
    mr_ssize_t ret;

    /* Select register and keep the bus active */
    msgs[0].addr = (mr_uint16_t)priv->addr;
    msgs[0].flags = MR_I2C_MSG_NOSTOP;
    msgs[0].buf = &reg;
    msgs[0].len = 1U;

    /* Read 16-bit register value and release the bus */
    msgs[1].addr = (mr_uint16_t)priv->addr;
    msgs[1].flags = MR_I2C_MSG_READ | MR_I2C_MSG_STOP;
    msgs[1].buf = rx;
    msgs[1].len = sizeof(rx);

    /* Transfer messages */
    ret = mr_i2c_transfer(priv->bus, msgs, 2U);
    if (ret < 0) {
        return (mr_err_t)ret;
    }
    if (ret != 2) {
        return -MR_EIO;
    }

    /* Decode big-endian register value */
    *value = (mr_uint16_t)(((mr_uint16_t)rx[0] << 8) | rx[1]);
    return 0;
}

static mr_err_t ads1x15_pga(mr_uint8_t gain, mr_uint16_t *pga) {
    switch (gain) {
        case MR_ADC_GAIN_1_3: {
            *pga = 0U;
            break;
        }
        case MR_ADC_GAIN_1_2: {
            *pga = 1U;
            break;
        }
        case MR_ADC_GAIN_1: {
            *pga = 2U;
            break;
        }
        case MR_ADC_GAIN_2: {
            *pga = 3U;
            break;
        }
        case MR_ADC_GAIN_4: {
            *pga = 4U;
            break;
        }
        case MR_ADC_GAIN_8: {
            *pga = 5U;
            break;
        }
        default: {
            return -MR_ENOSYS;
        }
    }
    return 0;
}

static mr_err_t ads1x15_wait_ready(drv_adc_ads1x15_priv_t *priv) {
    static const mr_uint16_t rates_16[] = {
        8U, 16U, 32U, 64U, 128U, 250U, 475U, 860U,
    };
    static const mr_uint16_t rates_12[] = {
        128U, 250U, 490U, 920U, 1600U, 2400U, 3300U, 3300U,
    };
    mr_uint32_t poll, polls, rate;
    mr_uint16_t config;
    mr_err_t ret;

    /* Bound polling to about two conversion periods */
    rate = (priv->resolution == 12U) ? rates_12[priv->data_rate]
                                     : rates_16[priv->data_rate];
    polls = (2000000U / rate + 999U) / 1000U + 2U;

    /* Wait for the conversion-ready flag */
    for (poll = 0U; poll < polls; poll++) {
        mr_clock_busy_wait(1000U);
        ret = ads1x15_reg_read(priv, ADS1X15_REG_CONFIG, &config);
        if (ret != 0) {
            return ret;
        }
        if ((config & ADS1X15_CONFIG_OS) != 0U) {
            return 0;
        }
    }
    return -MR_ETIMEDOUT;
}

static mr_err_t drv_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_adc_config_t *config) {
    drv_adc_ads1x15_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint16_t pga;
    mr_err_t ret;

    /* Check bus */
    if ((priv->bus == MR_NULL) || !MR_DRIVER_IS_SETUP(priv->bus)
        || MR_DRIVER_IS_DYING(priv->bus)) {
        return -MR_ENODEV;
    }
    if (!MR_DRIVER_IS_TYPE(priv->bus, MR_DRIVER_TYPE_I2C)) {
        return -MR_EINVAL;
    }

    /* Check configuration */
    if (channel >= 4U) {
        return -MR_EINVAL;
    }
    if (config->ref != MR_ADC_REF_INTERNAL) {
        return -MR_ENOSYS;
    }
    ret = ads1x15_pga(config->gain, &pga);

    /* Cache configuration */
    if (ret == 0) {
        priv->channels[channel] = *config;
    }
    return ret;
}

static mr_err_t drv_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_adc_config_t *config) {
    drv_adc_ads1x15_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Read cached configuration */
    if (channel >= 4U) {
        return -MR_EINVAL;
    }
    *config = priv->channels[channel];
    return 0;
}

static mr_err_t ads1x15_sample(drv_adc_ads1x15_priv_t *priv,
                               mr_uint32_t channel, mr_uint16_t *sample) {
    mr_uint16_t config, raw, mux, pga;
    mr_err_t ret;

    /* Check chip configuration */
    if ((priv->addr < 0x48U) || (priv->addr > 0x4bU)
        || (priv->data_rate > 7U)) {
        return -MR_EINVAL;
    }
    ret = ads1x15_pga(priv->channels[channel].gain, &pga);
    if (ret != 0) {
        return ret;
    }

    /* Build single-shot conversion configuration */
    mux = (mr_uint16_t)(channel + (priv->channels[channel].diff ? 0U : 4U));
    config = (mr_uint16_t)(ADS1X15_CONFIG_OS | ADS1X15_CONFIG_MODE_SINGLE
                           | ADS1X15_CONFIG_COMP_DISABLED
                           | (mux << ADS1X15_CONFIG_MUX_SHIFT)
                           | (pga << ADS1X15_CONFIG_PGA_SHIFT)
                           | (priv->data_rate << ADS1X15_CONFIG_DR_SHIFT));

    /* Start conversion */
    ret = ads1x15_reg_write(priv, ADS1X15_REG_CONFIG, config);
    if (ret != 0) {
        return ret;
    }

    /* Wait for conversion completion */
    ret = ads1x15_wait_ready(priv);
    if (ret != 0) {
        return ret;
    }

    /* Read conversion result */
    ret = ads1x15_reg_read(priv, ADS1X15_REG_CONVERSION, &raw);
    if (ret != 0) {
        return ret;
    }

    /* Sign-extend ADS1015 12-bit sample */
    if (priv->resolution == 12U) {
        raw >>= 4;
        if ((raw & 0x0800U) != 0U) {
            raw |= 0xf000U;
        }
    }

    /* Clamp negative single-ended readings to zero */
    if (!priv->channels[channel].diff && ((raw & 0x8000U) != 0U)) {
        raw = 0U;
    }
    *sample = raw;
    return 0;
}

static mr_ssize_t drv_read(const mr_driver_t *drv, mr_adc_seq_t *seq) {
    drv_adc_ads1x15_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t channels, channel;
    const mr_adc_data_t *data;
    mr_uint16_t sample;
    mr_uint8_t *buf;
    mr_size_t count;
    mr_err_t ret;

    /* Check sequence format and channel mask */
    data = MR_DRIVER_DATA(drv);
    if (((priv->resolution != 12U) && (priv->resolution != 16U))
        || (seq->resolution != priv->resolution)
        || ((seq->channels & ~(data->exist_mask & 0x0fU)) != 0U)
        || ((seq->size % sizeof(sample)) != 0U)) {
        return -MR_EINVAL;
    }
    if (seq->oversample != 0U) {
        return -MR_ENOSYS;
    }
    if ((seq->channels == 0U) || (seq->size == 0U)) {
        return 0;
    }
    if (seq->buf == MR_NULL) {
        return -MR_EINVAL;
    }

    /* Cache sequence state */
    channels = seq->channels;
    buf = seq->buf;
    count = 0U;

    /* Fill the sequence buffer in channel order */
    while (count < seq->size) {
        for (channel = 0U; channel < 4U; channel++) {
            if ((channels & (1U << channel)) == 0U) {
                continue;
            }

            /* Read one channel sample */
            ret = ads1x15_sample(priv, channel, &sample);
            if (ret != 0) {
                if (count != 0U) {
                    return (mr_ssize_t)count;
                }
                return ret;
            }

            /* Store sample without assuming buffer alignment */
            mr_memcpy(buf + count, &sample, sizeof(sample));
            count += sizeof(sample);
            if (count == seq->size) {
                break;
            }
        }
    }
    return (mr_ssize_t)count;
}

/* Instance install */
#define ADC_ADS1X15_HT_ADC_INSTALL(_id, _ops)                                  \
    static drv_adc_ads1x15_priv_t MR_CAT(__drv_adc_ads1x15_priv_, _id) = {     \
        .bus = MR_HT_DRIVER_FIND(MR_HT_DRIVER_PROP(_id, i2c)),                 \
        .addr = MR_HT_DRIVER_PROP(_id, addr),                                  \
        .resolution = MR_HT_DRIVER_PROP(_id, resolution),                      \
        .data_rate = MR_HT_DRIVER_PROP_OR(_id, data_rate, 4U),                 \
    };                                                                         \
    static mr_adc_data_t MR_CAT(__drv_adc_ads1x15_data_, _id)                  \
        = MR_ADC_DATA_INIT(4096U, MR_HT_DRIVER_PROP_OR(_id, exist_mask, 0x0fU) \
                                      & 0x0fU);                                \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(adc),                   \
                         MR_DRIVER_TYPE_ADC,                                   \
                         &MR_CAT(__drv_adc_ads1x15_data_, _id),                \
                         &MR_CAT(__drv_adc_ads1x15_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(ADC_ADS1X15_HT_ADC_INSTALL, &__drv_adc_ads1x15_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_ADC) && defined(MR_USE_DRIVER_I2C) */

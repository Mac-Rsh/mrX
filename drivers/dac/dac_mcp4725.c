/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/dac/mr_dac.h>
#if defined(MR_USE_DRIVER_DAC) && defined(MR_USE_DRIVER_I2C)
#include <mrX/drivers/i2c/mr_i2c.h>
#include <mrX/libc/mr_string.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    dac_mcp4725
#if MR_HT_HAS_DRIVER()

/* Device definition */
#define MCP4725_ADDR_MIN                0x60U
#define MCP4725_ADDR_MAX                0x67U
#define MCP4725_RESOLUTION              12U
#define MCP4725_SAMPLE_MAX              0x0fffU

/* Driver DAC private data type */
typedef struct drv_dac_mcp4725_priv {
    mr_driver_t *bus;
    mr_uint32_t addr;
} drv_dac_mcp4725_priv_t;

/* Driver DAC operations */
static mr_err_t drv_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_dac_config_t *config);
static mr_err_t drv_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_dac_config_t *config);
static mr_ssize_t drv_write(const mr_driver_t *drv, mr_dac_seq_t *seq);
MR_UNUSED static const mr_dac_ops_t __drv_dac_mcp4725_ops = {
    .channel_configure = drv_channel_configure,
    .channel_configuration = drv_channel_configuration,
    .write = drv_write,
#if defined(MR_USE_DRIVER_DAC_STREAM)
    .stream_start = MR_NULL,
    .stream_stop = MR_NULL,
    .stream_is_busy = MR_NULL,
    .stream_offset = MR_NULL,
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */
    .event_control = MR_NULL,
};

static mr_err_t mcp4725_write_sample(drv_dac_mcp4725_priv_t *priv,
                                     mr_uint16_t sample) {
    mr_i2c_msg_t msg;
    mr_uint8_t tx[2];
    mr_ssize_t ret;

    /* Build fast-mode volatile write frame */
    tx[0] = (mr_uint8_t)(sample >> 8);
    tx[1] = (mr_uint8_t)sample;

    /* Transfer frame */
    msg.addr = (mr_uint16_t)priv->addr;
    msg.flags = MR_I2C_MSG_STOP;
    msg.buf = tx;
    msg.len = sizeof(tx);
    ret = mr_i2c_transfer(priv->bus, &msg, 1U);
    if (ret < 0) {
        return (mr_err_t)ret;
    }
    if (ret != 1) {
        return -MR_EIO;
    }
    return 0;
}

static mr_uint16_t mcp4725_load_sample(const mr_uint8_t *buf) {
    mr_uint16_t sample;

    /* Read sample without assuming buffer alignment */
    mr_memcpy(&sample, buf, sizeof(sample));
    return sample;
}

static mr_err_t drv_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_dac_config_t *config) {
    drv_dac_mcp4725_priv_t *priv = MR_DRIVER_PRIV(drv);

    MR_IGNORE(channel);

    /* Check bus */
    if ((priv->bus == MR_NULL) || !MR_DRIVER_IS_SETUP(priv->bus)
        || MR_DRIVER_IS_DYING(priv->bus)) {
        return -MR_ENODEV;
    }
    if (!MR_DRIVER_IS_TYPE(priv->bus, MR_DRIVER_TYPE_I2C)) {
        return -MR_EINVAL;
    }

    /* Check chip configuration */
    if ((priv->addr < MCP4725_ADDR_MIN) || (priv->addr > MCP4725_ADDR_MAX)) {
        return -MR_EINVAL;
    }
    if (config->ref != MR_DAC_REF_VDD_1) {
        return -MR_ENOSYS;
    }
    return 0;
}

static mr_err_t drv_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_dac_config_t *config) {
    MR_IGNORE(drv);
    MR_IGNORE(channel);

    /* Return fixed configuration */
    config->ref = MR_DAC_REF_VDD_1;
    return 0;
}

static mr_ssize_t drv_write(const mr_driver_t *drv, mr_dac_seq_t *seq) {
    drv_dac_mcp4725_priv_t *priv = MR_DRIVER_PRIV(drv);
    const mr_uint8_t *buf;
    mr_uint16_t sample;
    mr_size_t count;
    mr_err_t ret;

    /* Check sequence format */
    if ((seq->resolution != MCP4725_RESOLUTION)
        || ((seq->size % sizeof(sample)) != 0U)) {
        return -MR_EINVAL;
    }
    if ((seq->channels == 0U) || (seq->size == 0U)) {
        return 0;
    }
    buf = seq->buf;

    /* Validate every sample before changing the output */
    for (count = 0U; count < seq->size; count += sizeof(sample)) {
        sample = mcp4725_load_sample(buf + count);
        if (sample > MCP4725_SAMPLE_MAX) {
            return -MR_EINVAL;
        }
    }

    /* Write samples in sequence order */
    for (count = 0U; count < seq->size; count += sizeof(sample)) {
        sample = mcp4725_load_sample(buf + count);
        ret = mcp4725_write_sample(priv, sample);
        if (ret != 0) {
            /* Preserve completed samples on partial failure */
            if (count != 0U) {
                return (mr_ssize_t)count;
            }
            return ret;
        }
    }
    return (mr_ssize_t)count;
}

/* Instance install */
#define DAC_MCP4725_HT_DAC_INSTALL(_id, _ops)                                  \
    static drv_dac_mcp4725_priv_t MR_CAT(__drv_dac_mcp4725_priv_, _id) = {     \
        .bus = MR_HT_DRIVER_FIND(MR_HT_DRIVER_PROP(_id, i2c)),                 \
        .addr = MR_HT_DRIVER_PROP(_id, addr),                                  \
    };                                                                         \
    static mr_dac_data_t MR_CAT(__drv_dac_mcp4725_data_, _id)                  \
        = MR_DAC_DATA_INIT(0U,                                                 \
                           MR_HT_DRIVER_PROP_OR(_id, exist_mask, 1U) & 1U);    \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(dac),                   \
                         MR_DRIVER_TYPE_DAC,                                   \
                         &MR_CAT(__drv_dac_mcp4725_data_, _id),                \
                         &MR_CAT(__drv_dac_mcp4725_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(DAC_MCP4725_HT_DAC_INSTALL, &__drv_dac_mcp4725_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_DAC) && defined(MR_USE_DRIVER_I2C) */

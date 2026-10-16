/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/dac/mr_dac.h>
#if defined(MR_USE_DRIVER_DAC) && defined(MR_USE_DRIVER_SPI)
#include <mrX/drivers/spi/mr_spi.h>
#include <mrX/libc/mr_string.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    dac_mcp4922
#if MR_HT_HAS_DRIVER()

/* Device definition */
#define MCP4922_CHANNEL_COUNT           2U
#define MCP4922_CHANNEL_SHIFT           15U
#define MCP4922_FRAME_SIZE              2U
#define MCP4922_RESOLUTION              12U
#define MCP4922_SAMPLE_MAX              0x0fffU
#define MCP4922_CONTROL_DEFAULT         0x3000U
#define MCP4922_SPI_CLOCK_MAX           1000000U

/* Driver DAC private data type */
typedef struct drv_dac_mcp4922_priv {
    mr_ht_spi_spec_t spi;
} drv_dac_mcp4922_priv_t;

/* Driver DAC operations */
static mr_err_t drv_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_dac_config_t *config);
static mr_err_t drv_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_dac_config_t *config);
static mr_ssize_t drv_write(const mr_driver_t *drv, mr_dac_seq_t *seq);
MR_UNUSED static const mr_dac_ops_t __drv_dac_mcp4922_ops = {
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

static mr_err_t mcp4922_configure(drv_dac_mcp4922_priv_t *priv) {
    mr_spi_config_t actual, config;
    mr_err_t ret;

    /* Configure SPI for MCP4922 */
    config.clock_rate = MCP4922_SPI_CLOCK_MAX;
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
    if ((actual.clock_rate == 0U) || (actual.clock_rate > MCP4922_SPI_CLOCK_MAX)
        || (actual.data_bits != 8U) || (actual.mode != MR_SPI_MODE_0)
        || (actual.bit_order != MR_SPI_BIT_ORDER_MSB)
        || (actual.role != MR_SPI_ROLE_MASTER)) {
        return -MR_ENOSYS;
    }
    return 0;
}

static mr_err_t mcp4922_write_sample(drv_dac_mcp4922_priv_t *priv,
                                     mr_uint32_t channel, mr_uint16_t sample) {
    mr_uint8_t tx[MCP4922_FRAME_SIZE];
    mr_uint16_t frame;
    mr_spi_msg_t msg;
    mr_ssize_t ret;

    /* Build write frame */
    frame = (mr_uint16_t)(MCP4922_CONTROL_DEFAULT
                          | ((mr_uint16_t)channel << MCP4922_CHANNEL_SHIFT)
                          | sample);
    tx[0] = (mr_uint8_t)(frame >> 8);
    tx[1] = (mr_uint8_t)frame;

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

static mr_uint16_t mcp4922_load_sample(const mr_uint8_t *buf) {
    mr_uint16_t sample;

    /* Read sample without assuming buffer alignment */
    mr_memcpy(&sample, buf, sizeof(sample));
    return sample;
}

static mr_err_t drv_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_dac_config_t *config) {
    drv_dac_mcp4922_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;

    MR_IGNORE(channel);

    /* Check bus */
    if ((priv->spi.drv == MR_NULL) || !MR_DRIVER_IS_SETUP(priv->spi.drv)
        || MR_DRIVER_IS_DYING(priv->spi.drv)) {
        return -MR_ENODEV;
    }
    if (!MR_DRIVER_IS_TYPE(priv->spi.drv, MR_DRIVER_TYPE_SPI)) {
        return -MR_EINVAL;
    }

    /* Check channel configuration */
    if (config->ref != MR_DAC_REF_EXTERNAL_0) {
        return -MR_ENOSYS;
    }

    /* Configure SPI */
    ret = mcp4922_configure(priv);
    if (ret != 0) {
        return ret;
    }
    return 0;
}

static mr_err_t drv_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_dac_config_t *config) {
    MR_IGNORE(drv);
    MR_IGNORE(channel);

    /* Return fixed configuration */
    config->ref = MR_DAC_REF_EXTERNAL_0;
    return 0;
}

static mr_ssize_t drv_write(const mr_driver_t *drv, mr_dac_seq_t *seq) {
    drv_dac_mcp4922_priv_t *priv = MR_DRIVER_PRIV(drv);
    const mr_uint8_t *buf;
    mr_uint32_t channels, channel;
    mr_uint16_t sample;
    mr_size_t count;
    mr_err_t ret;

    /* Check sequence format */
    if ((seq->resolution != MCP4922_RESOLUTION)
        || ((seq->size % sizeof(sample)) != 0U)) {
        return -MR_EINVAL;
    }
    channels = seq->channels;
    if ((channels == 0U) || (seq->size == 0U)) {
        return 0;
    }
    buf = seq->buf;

    /* Validate every sample before changing an output */
    for (count = 0U; count < seq->size; count += sizeof(sample)) {
        sample = mcp4922_load_sample(buf + count);
        if (sample > MCP4922_SAMPLE_MAX) {
            return -MR_EINVAL;
        }
    }

    /* Write channel samples */
    count = 0U;
    while (count < seq->size) {
        /* Scan enabled channels in ascending order */
        for (channel = 0U; channel < MCP4922_CHANNEL_COUNT; channel++) {
            if ((channels & (1U << channel)) == 0U) {
                continue;
            }

            /* Write one channel sample */
            sample = mcp4922_load_sample(buf + count);
            ret = mcp4922_write_sample(priv, channel, sample);
            if (ret != 0) {
                /* Preserve completed samples on partial failure */
                if (count != 0U) {
                    return (mr_ssize_t)count;
                }
                return ret;
            }

            count += sizeof(sample);
            /* Finish when the sequence buffer is full */
            if (count == seq->size) {
                return (mr_ssize_t)count;
            }
        }
    }
    return (mr_ssize_t)count;
}

/* Instance install */
#define DAC_MCP4922_HT_DAC_INSTALL(_id, _ops)                                  \
    static drv_dac_mcp4922_priv_t MR_CAT(__drv_dac_mcp4922_priv_, _id) = {     \
        .spi = MR_HT_SPI_SPEC(_id, spi),                                       \
    };                                                                         \
    static mr_dac_data_t MR_CAT(__drv_dac_mcp4922_data_, _id)                  \
        = MR_DAC_DATA_INIT(0U,                                                 \
                           MR_HT_DRIVER_PROP_OR(_id, exist_mask, 3U) & 3U);    \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(dac),                   \
                         MR_DRIVER_TYPE_DAC,                                   \
                         &MR_CAT(__drv_dac_mcp4922_data_, _id),                \
                         &MR_CAT(__drv_dac_mcp4922_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(DAC_MCP4922_HT_DAC_INSTALL, &__drv_dac_mcp4922_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_DAC) && defined(MR_USE_DRIVER_SPI) */

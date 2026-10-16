/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/adc/mr_adc.h>
#if defined(MR_USE_DRIVER_ADC) && defined(MR_USE_DRIVER_SPI)
#include <mrX/drivers/spi/mr_spi.h>
#include <mrX/libc/mr_string.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    adc_mcp3008
#if MR_HT_HAS_DRIVER()

/* Command definition */
#define MCP3008_CMD_START               0x01U
#define MCP3008_CMD_SINGLE_ENDED        0x80U
#define MCP3008_CMD_CHANNEL_SHIFT       4U
#define MCP3008_FRAME_SIZE              3U
#define MCP3008_RESULT_MASK             0x03ffU
#define MCP3008_SPI_CLOCK_MAX           1000000U

/* Driver ADC private data type */
typedef struct drv_adc_mcp3008_priv {
    mr_ht_spi_spec_t spi;
    mr_bool_t differential[8];
} drv_adc_mcp3008_priv_t;

/* Driver ADC operations */
static mr_err_t drv_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_adc_config_t *config);
static mr_err_t drv_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_adc_config_t *config);
static mr_ssize_t drv_read(const mr_driver_t *drv, mr_adc_seq_t *seq);
MR_UNUSED static const mr_adc_ops_t __drv_adc_mcp3008_ops = {
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

static mr_err_t mcp3008_configure(drv_adc_mcp3008_priv_t *priv) {
    mr_spi_config_t actual, config;
    mr_err_t ret;

    /* Configure SPI for MCP3008 */
    config.clock_rate = MCP3008_SPI_CLOCK_MAX;
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
    if ((actual.clock_rate == 0U) || (actual.clock_rate > MCP3008_SPI_CLOCK_MAX)
        || (actual.data_bits != 8U) || (actual.mode != MR_SPI_MODE_0)
        || (actual.bit_order != MR_SPI_BIT_ORDER_MSB)
        || (actual.role != MR_SPI_ROLE_MASTER)) {
        return -MR_ENOSYS;
    }
    return 0;
}

static mr_err_t mcp3008_transfer(drv_adc_mcp3008_priv_t *priv,
                                 const mr_uint8_t tx[MCP3008_FRAME_SIZE],
                                 mr_uint8_t rx[MCP3008_FRAME_SIZE]) {
    mr_spi_msg_t msg;
    mr_ssize_t ret;

    /* Transfer conversion frame */
    msg.tx_buf = tx;
    msg.tx_len = MCP3008_FRAME_SIZE;
    msg.rx_buf = rx;
    msg.rx_len = MCP3008_FRAME_SIZE;
    ret = mr_ht_spi_transfer(&priv->spi, &msg, 1U);
    if (ret < 0) {
        return (mr_err_t)ret;
    }
    if (ret != 1) {
        return -MR_EIO;
    }
    return 0;
}

static mr_err_t mcp3008_sample(drv_adc_mcp3008_priv_t *priv,
                               mr_uint32_t channel, mr_uint16_t *sample) {
    mr_uint8_t tx[MCP3008_FRAME_SIZE], rx[MCP3008_FRAME_SIZE];
    mr_err_t ret;

    /* Build conversion command */
    tx[0] = MCP3008_CMD_START;
    tx[1] = (mr_uint8_t)(channel << MCP3008_CMD_CHANNEL_SHIFT);
    tx[2] = 0U;
    if (!priv->differential[channel]) {
        tx[1] |= MCP3008_CMD_SINGLE_ENDED;
    }

    /* Transfer conversion frame */
    ret = mcp3008_transfer(priv, tx, rx);
    if (ret != 0) {
        return ret;
    }

    /* Decode 10-bit conversion result */
    *sample = (mr_uint16_t)((((mr_uint16_t)rx[1] << 8) | rx[2])
                            & MCP3008_RESULT_MASK);
    return 0;
}

static mr_err_t drv_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_adc_config_t *config) {
    drv_adc_mcp3008_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;

    /* Check bus */
    if ((priv->spi.drv == MR_NULL) || !MR_DRIVER_IS_SETUP(priv->spi.drv)
        || MR_DRIVER_IS_DYING(priv->spi.drv)) {
        return -MR_ENODEV;
    }
    if (!MR_DRIVER_IS_TYPE(priv->spi.drv, MR_DRIVER_TYPE_SPI)) {
        return -MR_EINVAL;
    }

    /* Check channel configuration */
    if ((config->ref != MR_ADC_REF_EXTERNAL_0)
        || (config->gain != MR_ADC_GAIN_1)) {
        return -MR_ENOSYS;
    }

    /* Configure SPI */
    ret = mcp3008_configure(priv);
    if (ret != 0) {
        return ret;
    }

    /* Cache channel configuration */
    priv->differential[channel] = !!config->diff;
    return 0;
}

static mr_err_t drv_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_adc_config_t *config) {
    drv_adc_mcp3008_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Read cached configuration */
    config->diff = priv->differential[channel];
    config->ref = MR_ADC_REF_EXTERNAL_0;
    config->gain = MR_ADC_GAIN_1;
    return 0;
}

static mr_ssize_t drv_read(const mr_driver_t *drv, mr_adc_seq_t *seq) {
    drv_adc_mcp3008_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t channel;
    mr_uint16_t sample;
    mr_size_t count;
    mr_err_t ret;

    /* Check sequence */
    if ((seq->resolution != 10U) || ((seq->size % sizeof(sample)) != 0U)) {
        return -MR_EINVAL;
    }
    if (seq->oversample != 0U) {
        return -MR_ENOSYS;
    }
    if ((seq->channels == 0U) || (seq->size == 0U)) {
        return 0;
    }

    /* Read channel samples */
    count = 0U;
    while (count < seq->size) {
        /* Scan enabled channels in ascending order */
        for (channel = 0U; channel < 8U; channel++) {
            if ((seq->channels & (1U << channel)) == 0U) {
                continue;
            }

            /* Read one channel sample */
            ret = mcp3008_sample(priv, channel, &sample);
            if (ret != 0) {
                /* Preserve completed samples on partial failure */
                if (count != 0U) {
                    return (mr_ssize_t)count;
                }
                return ret;
            }

            /* Store sample without assuming buffer alignment */
            mr_memcpy((mr_uint8_t *)seq->buf + count, &sample, sizeof(sample));
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
#define ADC_MCP3008_HT_ADC_INSTALL(_id, _ops)                                  \
    static drv_adc_mcp3008_priv_t MR_CAT(__drv_adc_mcp3008_priv_, _id) = {     \
        .spi = MR_HT_SPI_SPEC(_id, spi),                                       \
    };                                                                         \
    static mr_adc_data_t MR_CAT(__drv_adc_mcp3008_data_, _id)                  \
        = MR_ADC_DATA_INIT(0U, MR_HT_DRIVER_PROP_OR(_id, exist_mask, 0xffU)    \
                                   & 0xffU);                                   \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(adc),                   \
                         MR_DRIVER_TYPE_ADC,                                   \
                         &MR_CAT(__drv_adc_mcp3008_data_, _id),                \
                         &MR_CAT(__drv_adc_mcp3008_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(ADC_MCP3008_HT_ADC_INSTALL, &__drv_adc_mcp3008_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_ADC) && defined(MR_USE_DRIVER_SPI) */

/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_ADC)
#include <mrX/drivers/adc/mr_adc.h>
#if defined(MR_USE_DRIVER_ADC_STREAM) && defined(MR_USE_DRIVER_DMA)
#include <mrX/drivers/dma/mr_dma.h>
#define ADC_USE_STREAM
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) && defined(MR_USE_DRIVER_DMA) */
#if defined(MR_USE_DRIVER_ADC_STREAM) && defined(MR_USE_CH32VM00X)
#define ADC_USE_INJECTED_STREAM
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) && defined(MR_USE_CH32VM00X) */
#include <mrX/port/mr_port_clock.h>
#include <mrX/port/mr_port_irq.h>
#include <mrX/libc/mr_string.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    wch_ch32_adc
#if MR_HT_HAS_DRIVER()

/* Driver ADC operations */
static mr_err_t adc_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_adc_config_t *config);
static mr_err_t adc_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_adc_config_t *config);
static mr_ssize_t adc_read(const mr_driver_t *drv, mr_adc_seq_t *seq);
#if defined(ADC_USE_STREAM)
static mr_err_t adc_stream_start(const mr_driver_t *drv, mr_adc_seq_t *seq,
                                 const mr_adc_stream_config_t *config);
static mr_err_t adc_stream_stop(const mr_driver_t *drv);
static mr_err_t adc_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy);
static mr_err_t adc_stream_offset(const mr_driver_t *drv, mr_size_t *offset);
static mr_err_t adc_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable);
#endif /* defined(ADC_USE_STREAM) */
MR_UNUSED static const mr_adc_ops_t __adc_ops = {
    .channel_configure = adc_channel_configure,
    .channel_configuration = adc_channel_configuration,
    .read = adc_read,
#if defined(ADC_USE_STREAM)
    .stream_start = adc_stream_start,
    .stream_stop = adc_stream_stop,
    .stream_is_busy = adc_stream_is_busy,
    .stream_offset = adc_stream_offset,
    .event_control = adc_event_control,
#else
    .event_control = MR_NULL,
#endif /* defined(ADC_USE_STREAM) */
};

#if defined(ADC_USE_STREAM)
/* ADC DMA stream type */
typedef struct adc_dma_stream {
    mr_ht_dma_spec_t dma;
    mr_dma_transfer_t transfer;
    mr_waiter_t waiter;
    mr_driver_t *drv;
    mr_uint32_t events;
    mr_bool_t complete;
} adc_dma_stream_t;
#endif /* defined(ADC_USE_STREAM) */

#if defined(ADC_USE_INJECTED_STREAM)
/* ADC injected stream type */
typedef struct adc_injected_stream {
    mr_driver_t *drv;
    mr_uint8_t *buf;
    mr_size_t size, offset, frame_size;
    mr_uint32_t trigger, events;
    mr_uint8_t irq_priority;
    mr_bool_t circular;
} adc_injected_stream_t;
#endif /* defined(ADC_USE_INJECTED_STREAM) */

/* ADC private data type */
typedef struct adc_priv {
    ADC_TypeDef *adc;
    void (*clk_enable)(void);
    mr_uint8_t ref;
    mr_bool_t initialized, busy;
#if defined(ADC_PGA) && defined(ADC_BUFEN)
    mr_uint8_t gain[18];
#endif /* defined(ADC_PGA) && defined(ADC_BUFEN) */
#if defined(ADC_USE_STREAM)
    adc_dma_stream_t stream;
    mr_uint32_t trigger;
#endif /* defined(ADC_USE_STREAM) */
#if defined(ADC_USE_INJECTED_STREAM)
    adc_injected_stream_t injected;
#endif /* defined(ADC_USE_INJECTED_STREAM) */
} adc_priv_t;

/* ADC hardware limits definition */
#if defined(MR_USE_CH32V00X)
#define ADC_CHANNEL_MASK                (0x3ffU)
#define ADC_RESOLUTION                  (10U)
#elif defined(MR_USE_CH32VM00X)
#define ADC_CHANNEL_MASK                (0x3ffU)
#define ADC_RESOLUTION                  (12U)
#else
#define ADC_CHANNEL_MASK                (0x3ffffU)
#define ADC_RESOLUTION                  (12U)
#endif /* CH32 EVT SDK family */
#define ADC_POLL_ATTEMPTS               (1000U)
#define ADC_POLL_INTERVAL_US            (1U)
#define ADC_SEQUENCE_RANKS              (16U)

#if defined(ADC_USE_INJECTED_STREAM)
/* ADC injected group definition */
#define ADC_INJECTED_RANKS              (4U)
#define ADC_INJECTED_PIN_TRIGGER        (6U)
#define ADC_INJECTED_EVENTS                                                    \
    (MR_ADC_WAIT_STREAM_HALF | MR_ADC_WAIT_STREAM_FULL)
#endif /* defined(ADC_USE_INJECTED_STREAM) */

#if defined(ADC_TSVREFE)
/* ADC1 internal channel definition */
#define ADC_INTERNAL_CHANNELS           (0x30000U)
#endif /* defined(ADC_TSVREFE) */

#if defined(ADC_USE_STREAM)
/* ADC DMA event definition */
#define ADC_DMA_EVENTS                                                         \
    (MR_DMA_WAIT_HALF | MR_DMA_WAIT_DONE | MR_DMA_WAIT_ERROR)

/* ADC stream event mask definition */
#define ADC_STREAM_EVENTS                                                      \
    (MR_ADC_WAIT_STREAM_HALF | MR_ADC_WAIT_STREAM_FULL                         \
     | MR_ADC_WAIT_STREAM_ERROR)

/* ADC software trigger selector definition */
#define ADC_SOFTWARE_TRIGGER            (7U)
#endif /* defined(ADC_USE_STREAM) */

static mr_uint32_t adc_channels(const adc_priv_t *priv) {
#if defined(ADC_TSVREFE)
    if (priv->adc != ADC1) {
        return ADC_CHANNEL_MASK & ~ADC_INTERNAL_CHANNELS;
    }
#else
    MR_IGNORE(priv);
#endif /* defined(ADC_TSVREFE) */
    return ADC_CHANNEL_MASK;
}

#if defined(ADC_RSTCAL) && defined(ADC_CAL)
static mr_err_t adc_calibrate(ADC_TypeDef *adc, mr_uint32_t flag) {
    mr_uint32_t attempt;

    /* Calibrate ADC */
    adc->CTLR2 |= flag;
    for (attempt = 0U; attempt < ADC_POLL_ATTEMPTS; attempt++) {
        if ((adc->CTLR2 & flag) == 0U) {
            return 0;
        }
        mr_port_clock_busy_wait(ADC_POLL_INTERVAL_US);
    }
    return -MR_ETIMEDOUT;
}
#endif /* defined(ADC_RSTCAL) && defined(ADC_CAL) */

static mr_err_t adc_initialize(adc_priv_t *priv) {
    ADC_TypeDef *adc;

    /* Check ADC resources */
    if (priv->adc == MR_NULL) {
        return -MR_EINVAL;
    }

    /* Check ADC state */
    if (priv->initialized) {
        return 0;
    }

    /* Enable ADC clock */
    priv->clk_enable();

    /* Init ADC */
    adc = priv->adc;
    adc->CTLR1 = 0U;
    adc->CTLR2 = ADC_EXTSEL | ADC_ADON;
#if defined(MR_USE_CH32VM00X)
    adc->CTLR3 = 0U;
#endif /* defined(MR_USE_CH32VM00X) */
    mr_port_clock_busy_wait(10U);
#if defined(ADC_RSTCAL) && defined(ADC_CAL)
    /* Calibrate ADC */
    if ((adc_calibrate(adc, ADC_RSTCAL) != 0)
        || (adc_calibrate(adc, ADC_CAL) != 0)) {
        adc->CTLR2 = 0U;
        return -MR_ETIMEDOUT;
    }
#endif /* defined(ADC_RSTCAL) && defined(ADC_CAL) */
    adc->RSQR1 = 0U;
    priv->initialized = MR_TRUE;
    return 0;
}

static void adc_sample_time(ADC_TypeDef *adc, mr_uint32_t channel,
                            mr_uint32_t sample_time) {
    mr_uint32_t shift;

    /* Set sample time */
    if (channel < 10U) {
        shift = channel * 3U;
        adc->SAMPTR2 = (adc->SAMPTR2 & ~(7U << shift)) | (sample_time << shift);
#if !defined(MR_USE_CH32VM00X)
    } else {
        shift = (channel - 10U) * 3U;
        adc->SAMPTR1 = (adc->SAMPTR1 & ~(7U << shift)) | (sample_time << shift);
#endif /* !defined(MR_USE_CH32VM00X) */
    }
}

static void adc_set_gain(const adc_priv_t *priv, mr_uint32_t channel) {
#if defined(ADC_PGA) && defined(ADC_BUFEN)
    mr_uint32_t gain, bits;

    /* Set input buffer gain */
    gain = priv->gain[channel];
    bits = 0U;
    if (gain == MR_ADC_GAIN_4) {
        bits = ADC_PGA_0 | ADC_BUFEN;
    } else if (gain == MR_ADC_GAIN_16) {
        bits = ADC_PGA_1 | ADC_BUFEN;
    } else if (gain == MR_ADC_GAIN_64) {
        bits = ADC_PGA | ADC_BUFEN;
    }
    priv->adc->CTLR1 = (priv->adc->CTLR1 & ~(ADC_PGA | ADC_BUFEN)) | bits;
#else
    MR_IGNORE(priv);
    MR_IGNORE(channel);
#endif /* defined(ADC_PGA) && defined(ADC_BUFEN) */
}

static mr_err_t adc_convert(const adc_priv_t *priv, mr_uint32_t channel,
                             mr_uint32_t sample_time, mr_uint16_t *value) {
    ADC_TypeDef *adc;
    mr_uint32_t attempt;

    /* Configure channel */
    adc = priv->adc;
    adc_set_gain(priv, channel);
    adc_sample_time(adc, channel, sample_time);
    adc->CTLR1 &= ~ADC_SCAN;
    adc->RSQR1 = 0U;
    adc->RSQR3 = channel;
    adc->STATR = 0U;

    /* Start ADC */
    adc->CTLR2 = (adc->CTLR2 & ~(ADC_EXTSEL | ADC_CONT))
        | ADC_EXTSEL | ADC_EXTTRIG | ADC_SWSTART;

    /* Wait ADC */
    for (attempt = 0U; attempt < ADC_POLL_ATTEMPTS; attempt++) {
        if ((adc->STATR & ADC_EOC) != 0U) {
            /* Read ADC */
            *value = (mr_uint16_t)adc->RDATAR & ((1U << ADC_RESOLUTION) - 1U);
            return 0;
        }
        mr_port_clock_busy_wait(ADC_POLL_INTERVAL_US);
    }
    return -MR_ETIMEDOUT;
}

static void adc_internal_enable(ADC_TypeDef *adc, mr_uint32_t channels) {
#if defined(ADC_TSVREFE)
    /* Enable the internal input buffer */
    if (((channels & ADC_INTERNAL_CHANNELS) != 0U)
        && ((adc->CTLR2 & ADC_TSVREFE) == 0U)) {
        /* Calibration runs before the internal input buffer is enabled */
        adc->CTLR2 |= ADC_TSVREFE;
        mr_port_clock_busy_wait(20U);
    }
#else
    MR_IGNORE(adc);
    MR_IGNORE(channels);
#endif /* defined(ADC_TSVREFE) */
}

static mr_err_t adc_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_adc_config_t *config) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
#if defined(ADC_PGA) && defined(ADC_BUFEN)
    int mask;
#endif /* defined(ADC_PGA) && defined(ADC_BUFEN) */

    /* Check channel */
    if ((channel >= 32U) || ((adc_channels(priv) & (1U << channel)) == 0U)) {
        return -MR_EINVAL;
    }

    /* Check configuration */
    if ((config->diff != MR_ADC_DIFF_SINGLE) || (config->ref != priv->ref)) {
        return -MR_ENOSYS;
    }
#if defined(ADC_PGA) && defined(ADC_BUFEN)
    if ((config->gain != MR_ADC_GAIN_1)
        && ((channel >= 16U) || ((config->gain != MR_ADC_GAIN_4)
                                 && (config->gain != MR_ADC_GAIN_16)
                                 && (config->gain != MR_ADC_GAIN_64)))) {
        return -MR_ENOSYS;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check stream state */
#if defined(ADC_USE_STREAM)
    if ((priv->stream.drv != MR_NULL) && !priv->stream.complete) {
        mr_port_irq_enable(mask);
        return -MR_EBUSY;
    }
#endif /* defined(ADC_USE_STREAM) */

    /* Check ADC state */
    if (priv->busy) {
        mr_port_irq_enable(mask);
        return -MR_EBUSY;
    }

    /* Save channel gain */
    priv->gain[channel] = config->gain;

    /* Unlock */
    mr_port_irq_enable(mask);
#else
    if (config->gain != MR_ADC_GAIN_1) {
        return -MR_ENOSYS;
    }
#endif /* defined(ADC_PGA) && defined(ADC_BUFEN) */
    return 0;
}

static mr_err_t adc_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_adc_config_t *config) {
    const adc_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check channel */
    if ((channel >= 32U) || ((adc_channels(priv) & (1U << channel)) == 0U)) {
        return -MR_EINVAL;
    }

    /* Get channel configuration */
    config->diff = MR_ADC_DIFF_SINGLE;
#if defined(ADC_PGA) && defined(ADC_BUFEN)
    config->gain = priv->gain[channel];
#else
    config->gain = MR_ADC_GAIN_1;
#endif /* defined(ADC_PGA) && defined(ADC_BUFEN) */
    config->ref = priv->ref;
    return 0;
}

static mr_ssize_t adc_read(const mr_driver_t *drv, mr_adc_seq_t *seq) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    const mr_adc_data_t *data;
    mr_uint32_t channel;
    mr_uint16_t sample;
    mr_uint8_t *buf;
    mr_size_t bytes;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    data = MR_DRIVER_DATA(drv);
    if ((seq->channels & ~(data->exist_mask & adc_channels(priv))) != 0U
        || (seq->size > (mr_size_t)MR_SSIZE_MAX)) {
        return -MR_EINVAL;
    }
    if ((seq->resolution != ADC_RESOLUTION) || (seq->oversample > 7U)) {
        return -MR_ENOSYS;
    }
    if ((seq->channels == 0U) || (seq->size < sizeof(sample))) {
        return 0;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check stream state */
#if defined(ADC_USE_STREAM)
    if ((priv->stream.drv != MR_NULL) && !priv->stream.complete) {
        mr_port_irq_enable(mask);
        return -MR_EBUSY;
    }
#endif /* defined(ADC_USE_STREAM) */

    /* Check ADC state */
    if (priv->busy) {
        mr_port_irq_enable(mask);
        return -MR_EBUSY;
    }

    /* Mark ADC busy */
    priv->busy = MR_TRUE;

    /* Unlock */
    mr_port_irq_enable(mask);

#if defined(ADC_PGA) && defined(ADC_BUFEN)
    /* The input buffer requires at least the second sample-time setting */
    if (seq->oversample == 0U) {
        for (channel = 0U; channel < 16U; channel++) {
            if (((seq->channels & (1U << channel)) != 0U)
                && (priv->gain[channel] != MR_ADC_GAIN_1)) {
                mask = mr_port_irq_disable();
                priv->busy = MR_FALSE;
                mr_port_irq_enable(mask);
                return -MR_ENOSYS;
            }
        }
    }
#endif /* defined(ADC_PGA) && defined(ADC_BUFEN) */

    /* Read channel sequence */
    bytes = 0U;
    buf = seq->buf;
    ret = adc_initialize(priv);
    if (ret == 0) {
        adc_internal_enable(priv->adc, seq->channels);
    }
    while ((ret == 0) && ((seq->size - bytes) >= sizeof(sample))) {
        for (channel = 0U; channel < 32U; channel++) {
            if ((seq->channels & (1U << channel)) == 0U) {
                continue;
            }
            ret = adc_convert(priv, channel, seq->oversample, &sample);
            if (ret != 0) {
                /* Abort the timed out conversion before another request */
                priv->adc->CTLR2 = 0U;
                priv->initialized = MR_FALSE;
                break;
            }
            mr_memcpy(&buf[bytes], &sample, sizeof(sample));
            bytes += sizeof(sample);
            if ((seq->size - bytes) < sizeof(sample)) {
                break;
            }
        }
    }

    /* Mark ADC idle */
    mask = mr_port_irq_disable();
    priv->busy = MR_FALSE;
    mr_port_irq_enable(mask);
    return (bytes != 0U) ? (mr_ssize_t)bytes : ret;
}

#if (defined(ADC_USE_STREAM) || defined(ADC_USE_INJECTED_STREAM))              \
    && defined(ADC_PGA) && defined(ADC_BUFEN)
static mr_bool_t adc_stream_gain_supported(const adc_priv_t *priv,
                                           const mr_adc_seq_t *seq) {
    mr_uint32_t remaining, channel;
    mr_uint8_t gain;

    /* The gain is shared by all ranks in a scan */
    remaining = seq->channels;
    gain = priv->gain[__builtin_ctz(remaining)];
    if ((seq->oversample == 0U) && (gain != MR_ADC_GAIN_1)) {
        return MR_FALSE;
    }
    while (remaining != 0U) {
        channel = (mr_uint32_t)__builtin_ctz(remaining);
        if (priv->gain[channel] != gain) {
            return MR_FALSE;
        }
        remaining &= remaining - 1U;
    }
    return MR_TRUE;
}
#endif /* ADC stream with shared gain */

#if defined(ADC_USE_STREAM)
static void adc_stream_sequence(ADC_TypeDef *adc, mr_uint32_t channels,
                                mr_uint32_t sample_time, mr_uint32_t count) {
    mr_uint32_t ranks[3] = {0};
    mr_uint32_t channel, rank;

    /* Build ADC ranks */
    rank = 0U;
    while (channels != 0U) {
        channel = (mr_uint32_t)__builtin_ctz(channels);
        channels &= channels - 1U;
        adc_sample_time(adc, channel, sample_time);
        ranks[rank / 6U] |= channel << ((rank % 6U) * 5U);
        rank++;
    }
    adc->RSQR3 = ranks[0];
    adc->RSQR2 = ranks[1];
    adc->RSQR1 = ranks[2] | ((count - 1U) << 20);
    adc->CTLR1 = (count > 1U) ? ADC_SCAN : 0U;
}

static void adc_stream_quiesce(adc_priv_t *priv) {
    /* Disable ADC stream requests */
    priv->adc->CTLR2 &= ~(ADC_DMA | ADC_EXTTRIG | ADC_CONT | ADC_ADON);
    priv->initialized = MR_FALSE;
}

static mr_err_t adc_dma_stop(adc_dma_stream_t *stream) {
    mr_err_t ret;

    /* Check stream state */
    if (stream->drv == MR_NULL) {
        return 0;
    }

    /* Check DMA */
    if (!mr_driver_is_ready(stream->dma.drv)) {
        return -MR_ENODEV;
    }

    /* Stop DMA */
    ret = mr_ht_dma_stop(&stream->dma);
    if (ret != 0) {
        return ret;
    }

    /* Disable DMA event */
    mr_ht_dma_event_disable(&stream->dma, ADC_DMA_EVENTS);

    /* Release waiter */
    mr_waiter_del(&stream->waiter);

    /* Reset stream state */
    stream->drv = MR_NULL;
    stream->complete = MR_FALSE;
    return 0;
}

static mr_err_t adc_dma_start(const mr_driver_t *drv, adc_dma_stream_t *stream,
                              mr_waiter_entry_t *entry, void *param) {
    static const mr_dma_config_t config = {
        .src_width = MR_DMA_WIDTH_16,
        .dst_width = MR_DMA_WIDTH_16,
        .src_adj = MR_DMA_ADJ_CONST,
        .dst_adj = MR_DMA_ADJ_INC,
        .src_burst = MR_DMA_BURST_1,
        .dst_burst = MR_DMA_BURST_1,
        .direction = MR_DMA_DIR_P2M,
    };
    mr_err_t ret;

    /* Check DMA */
    if (stream->dma.drv == MR_NULL) {
        return -MR_ENOSYS;
    }
    if (!mr_driver_is_ready(stream->dma.drv)) {
        return -MR_ENODEV;
    }

    /* Configure DMA */
    ret = mr_ht_dma_configure(&stream->dma, &config);
    if (ret != 0) {
        return ret;
    }

    /* Init waiter */
    ret = mr_waiter_init(&stream->waiter, entry, param, ADC_DMA_EVENTS);
    if (ret != 0) {
        return ret;
    }

    /* Attach DMA event */
    ret = mr_ht_dma_event_attach(&stream->dma, &stream->waiter);
    if (ret == 0) {
        ret = mr_ht_dma_event_enable(&stream->dma, ADC_DMA_EVENTS);
        if (ret == 0) {
            ret = mr_ht_dma_start(&stream->dma, &stream->transfer);
            if (ret != 0) {
                mr_ht_dma_event_disable(&stream->dma, ADC_DMA_EVENTS);
            }
        }
    }
    if (ret != 0) {
        /* Release waiter */
        mr_waiter_del(&stream->waiter);
        return ret;
    }

    /* Activate stream */
    stream->drv = (mr_driver_t *)drv;
    return 0;
}

static void adc_dma_event(mr_waiter_t *waiter, void *param,
                          mr_wait_event_t event, const void *data) {
    const mr_adc_data_t *event_data;
    mr_uint32_t events;
    adc_priv_t *priv;
    int mask;

    priv = (adc_priv_t *)param;
    MR_IGNORE(waiter);
    MR_IGNORE(data);

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check stream state */
    if ((priv->stream.drv == MR_NULL) || priv->stream.complete) {
        mr_port_irq_enable(mask);
        return;
    }

    /* Translate DMA event */
    events = 0U;
    if ((event & MR_DMA_WAIT_ERROR) != 0U) {
        events = MR_ADC_WAIT_STREAM_ERROR;
        priv->stream.complete = MR_TRUE;
    } else {
        if ((event & MR_DMA_WAIT_HALF) != 0U) {
            events |= MR_ADC_WAIT_STREAM_HALF;
        }
        if ((event & MR_DMA_WAIT_DONE) != 0U) {
            events |= MR_ADC_WAIT_STREAM_FULL;
            priv->stream.complete = priv->stream.transfer.next == MR_NULL;
        }
    }
    if (priv->stream.complete) {
        adc_stream_quiesce(priv);
    }

    /* Raise ADC event */
    events &= priv->stream.events;
    event_data = MR_DRIVER_DATA(priv->stream.drv);
    if ((events != 0U) && mr_driver_is_ready(priv->stream.drv)
        && MR_OBJECT_IS_INITED(&event_data->event)) {
        mr_adc_event_raise(priv->stream.drv, events);
    }

    /* Unlock */
    mr_port_irq_enable(mask);
}

static mr_err_t adc_stream_start(const mr_driver_t *drv, mr_adc_seq_t *seq,
                                  const mr_adc_stream_config_t *config) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    const mr_adc_data_t *data;
    mr_uint32_t count, ctlr;
    mr_bool_t external;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    data = MR_DRIVER_DATA(drv);
    external = config->source == MR_ADC_STREAM_SOURCE_EXTERNAL;
    count = (mr_uint32_t)__builtin_popcount(seq->channels);
    if ((seq->resolution != ADC_RESOLUTION) || (seq->oversample > 7U)
        || (external && (priv->trigger >= ADC_SOFTWARE_TRIGGER))) {
        return -MR_ENOSYS;
    }

    /* Check sequence */
    if ((count == 0U) || (count > ADC_SEQUENCE_RANKS)
        || ((seq->channels & ~(data->exist_mask & adc_channels(priv))) != 0U)
        || (seq->size == 0U) || ((seq->size % (count * 2U)) != 0U)
        || ((seq->size / 2U) > MR_UINT16_MAX)
        || (((mr_uintptr_t)seq->buf % 2U) != 0U)) {
        return -MR_EINVAL;
    }

    /* Check DMA */
    if (priv->stream.dma.drv == MR_NULL) {
        return -MR_ENOSYS;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check stream state */
    if (priv->busy || ((priv->stream.drv != MR_NULL)
                      && !priv->stream.complete)) {
        mr_port_irq_enable(mask);
        return -MR_EBUSY;
    }
#if defined(ADC_PGA) && defined(ADC_BUFEN)
    /* Check gain configuration */
    if (!adc_stream_gain_supported(priv, seq)) {
        mr_port_irq_enable(mask);
        return -MR_ENOSYS;
    }
#endif /* defined(ADC_PGA) && defined(ADC_BUFEN) */

    /* Stop old stream */
    ret = adc_dma_stop(&priv->stream);
    if (ret == 0) {
        priv->busy = MR_TRUE;
    }
    mr_port_irq_enable(mask);
    if (ret != 0) {
        return ret;
    }

    /* Init ADC */
    ret = adc_initialize(priv);
    if (ret == 0) {
        adc_internal_enable(priv->adc, seq->channels);
    }

    /* Lock */
    mask = mr_port_irq_disable();
    if (ret == 0) {
        /* Start stream */
        adc_stream_sequence(priv->adc, seq->channels, seq->oversample, count);
        adc_set_gain(priv, (mr_uint32_t)__builtin_ctz(seq->channels));
        priv->adc->STATR = 0U;
        priv->stream.transfer.src = (const void *)&priv->adc->RDATAR;
        priv->stream.transfer.dst = seq->buf;
        priv->stream.transfer.count = seq->size / sizeof(mr_uint16_t);
        priv->stream.transfer.next = config->circular
            ? &priv->stream.transfer : MR_NULL;
        ret = adc_dma_start(drv, &priv->stream, adc_dma_event, priv);
        if (ret == 0) {
            /* Start ADC */
            ctlr = priv->adc->CTLR2 & ~(ADC_EXTSEL | ADC_CONT);
            ctlr |= ADC_DMA | ADC_EXTTRIG;
            ctlr |= external ? ((mr_uint32_t)priv->trigger << 17U)
                             : (ADC_EXTSEL | ADC_CONT | ADC_SWSTART);
            priv->adc->CTLR2 = ctlr;
        }
    }

    /* Mark ADC idle */
    priv->busy = MR_FALSE;

    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

static mr_err_t adc_stream_stop(const mr_driver_t *drv) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check ADC state */
    if (priv->busy) {
        mr_port_irq_enable(mask);
        return -MR_EBUSY;
    }

    /* Stop stream */
    if ((priv->stream.drv != MR_NULL) && !priv->stream.complete) {
        adc_stream_quiesce(priv);
    }
    ret = adc_dma_stop(&priv->stream);

    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

static mr_err_t adc_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Get stream state */
    mask = mr_port_irq_disable();
    *busy = (priv->stream.drv != MR_NULL) && !priv->stream.complete;
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t adc_stream_offset(const mr_driver_t *drv, mr_size_t *offset) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Get stream offset */
    if (priv->stream.dma.drv == MR_NULL) {
        return -MR_ENOSYS;
    }
    if (!mr_driver_is_ready(priv->stream.dma.drv)) {
        return -MR_ENODEV;
    }
    return mr_ht_dma_offset(&priv->stream.dma, offset);
}

static mr_err_t adc_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Check parameter */
    if ((events == 0U) || ((events & ~ADC_STREAM_EVENTS) != 0U)) {
        return -MR_EINVAL;
    }

    /* Check stream support */
    if (priv->stream.dma.drv == MR_NULL) {
        return -MR_ENOSYS;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Update event mask */
    if (enable) {
        priv->stream.events |= events;
    } else {
        priv->stream.events &= ~events;
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}
#endif /* defined(ADC_USE_STREAM) */

#if defined(ADC_USE_INJECTED_STREAM)
static void adc_injected_sequence(ADC_TypeDef *adc, const mr_adc_seq_t *seq,
                                  mr_uint32_t count) {
    mr_uint32_t channels, channel, rank, ranks;

    /* Short sequences start at JSQ[5 - count] and end at JSQ4 */
    channels = seq->channels;
    ranks = (count - 1U) << 20U;
    for (rank = ADC_INJECTED_RANKS - count; rank < ADC_INJECTED_RANKS; rank++) {
        channel = (mr_uint32_t)__builtin_ctz(channels);
        channels &= channels - 1U;
        adc_sample_time(adc, channel, seq->oversample);
        ranks |= channel << (rank * 5U);
    }
    adc->ISQR = ranks;
    /* Zero hardware offsets so IDATAR contains unsigned raw samples */
    adc->IOFR1 = 0U;
    adc->IOFR2 = 0U;
    adc->IOFR3 = 0U;
    adc->IOFR4 = 0U;
    adc->CTLR1 = (count > 1U) ? ADC_SCAN : 0U;
}

static void adc_injected_quiesce(adc_priv_t *priv) {
    /* Abort conversion before clearing pending interrupts and releasing buf */
    priv->adc->CTLR2 = 0U;
    priv->adc->CTLR1 &= ~ADC_JEOCIE;
    priv->adc->STATR = 0U;
    NVIC_DisableIRQ(ADC_IRQn);
    NVIC_ClearPendingIRQ(ADC_IRQn);
    priv->initialized = MR_FALSE;
    priv->injected.drv = MR_NULL;
    priv->injected.buf = MR_NULL;
    priv->busy = MR_FALSE;
}

static mr_err_t adc_injected_start(const mr_driver_t *drv, mr_adc_seq_t *seq,
                                  const mr_adc_stream_config_t *config) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    adc_injected_stream_t *stream;
    const mr_adc_data_t *data;
    mr_size_t frame_size;
    mr_uint32_t count;
    mr_err_t ret;
    int mask;

    /* Check trigger and ADC resources */
    stream = &priv->injected;
    if ((config->source != MR_ADC_STREAM_SOURCE_EXTERNAL)
        || (seq->resolution != ADC_RESOLUTION) || (seq->oversample > 7U)) {
        return -MR_ENOSYS;
    }
    if ((priv->adc != ADC1) || (stream->trigger > ADC_INJECTED_PIN_TRIGGER)) {
        return -MR_EINVAL;
    }

    /* Check that the buffer holds complete scans */
    data = MR_DRIVER_DATA(drv);
    count = (mr_uint32_t)__builtin_popcount(seq->channels);
    frame_size = count * sizeof(mr_uint16_t);
    if ((count == 0U) || (count > ADC_INJECTED_RANKS)
        || ((seq->channels & ~(data->exist_mask & adc_channels(priv))) != 0U)
        || (seq->buf == MR_NULL) || (seq->size == 0U)
        || ((seq->size % frame_size) != 0U)) {
        return -MR_EINVAL;
    }

    /* Hold ADC ownership during initialization and through the last frame */
    mask = mr_port_irq_disable();
    if (priv->busy) {
        mr_port_irq_enable(mask);
        return -MR_EBUSY;
    }
#if defined(ADC_PGA) && defined(ADC_BUFEN)
    if (!adc_stream_gain_supported(priv, seq)) {
        mr_port_irq_enable(mask);
        return -MR_ENOSYS;
    }
#endif /* defined(ADC_PGA) && defined(ADC_BUFEN) */
    priv->busy = MR_TRUE;
    mr_port_irq_enable(mask);

    ret = adc_initialize(priv);
    if (ret == 0) {
        adc_injected_sequence(priv->adc, seq, count);
        adc_set_gain(priv, (mr_uint32_t)__builtin_ctz(seq->channels));
    }

    /* Publish the buffer and enable the IRQ before arming external triggers */
    mask = mr_port_irq_disable();
    if (ret == 0) {
        stream->drv = (mr_driver_t *)drv;
        stream->buf = seq->buf;
        stream->size = seq->size;
        stream->offset = 0U;
        stream->frame_size = frame_size;
        stream->circular = config->circular;
        priv->adc->STATR = 0U;
        NVIC_SetPriority(ADC_IRQn, stream->irq_priority);
        NVIC_ClearPendingIRQ(ADC_IRQn);
        NVIC_EnableIRQ(ADC_IRQn);
        priv->adc->CTLR1 |= ADC_JEOCIE;
        priv->adc->CTLR2 = ADC_ADON | ADC_JEXTTRIG | (stream->trigger << 12U);
    } else {
        priv->busy = MR_FALSE;
    }
    mr_port_irq_enable(mask);
    return ret;
}

static mr_err_t adc_injected_stop(const mr_driver_t *drv) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    mask = mr_port_irq_disable();
    if (priv->injected.buf != MR_NULL) {
        adc_injected_quiesce(priv);
    } else if (priv->busy) {
        mr_port_irq_enable(mask);
        return -MR_EBUSY;
    }
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t adc_injected_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    const adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    mask = mr_port_irq_disable();
    *busy = priv->injected.buf != MR_NULL;
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t adc_injected_offset(const mr_driver_t *drv, mr_size_t *offset) {
    const adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    mask = mr_port_irq_disable();
    *offset = priv->injected.offset;
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t adc_injected_event_control(const mr_driver_t *drv,
                                          mr_uint32_t events,
                                          mr_bool_t enable) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    if ((events == 0U) || ((events & ~ADC_INJECTED_EVENTS) != 0U)) {
        return -MR_EINVAL;
    }

    /* JEOC must remain enabled to move samples even with no subscriptions */
    mask = mr_port_irq_disable();
    if (enable) {
        priv->injected.events |= events;
    } else {
        priv->injected.events &= ~events;
    }
    mr_port_irq_enable(mask);
    return 0;
}

MR_UNUSED static void adc_injected_irq(adc_priv_t *priv) {
    mr_uint16_t frame[ADC_INJECTED_RANKS];
    adc_injected_stream_t *stream;
    const mr_adc_data_t *data;
    mr_size_t old_offset;
    mr_uint32_t events;
    ADC_TypeDef *adc;
    mr_driver_t *drv;
    int mask;

    stream = &priv->injected;
    adc = priv->adc;
    mask = mr_port_irq_disable();
    if ((stream->buf == MR_NULL) || ((adc->STATR & ADC_JEOC) == 0U)) {
        mr_port_irq_enable(mask);
        return;
    }
    drv = stream->drv;
    if (!mr_driver_is_ready(drv)) {
        adc_injected_quiesce(priv);
        mr_port_irq_enable(mask);
        return;
    }

    /* Read the completed frame and acknowledge the write-zero status bits */
    adc->STATR = ADC_AWD | ADC_STRT;
    frame[0] = (mr_uint16_t)adc->IDATAR1;
    frame[1] = (mr_uint16_t)adc->IDATAR2;
    frame[2] = (mr_uint16_t)adc->IDATAR3;
    frame[3] = (mr_uint16_t)adc->IDATAR4;
    old_offset = stream->offset;
    mr_memcpy(&stream->buf[old_offset], frame, stream->frame_size);
    stream->offset += stream->frame_size;

    /* Commit wrap or shutdown before notifying synchronous waiters */
    events = 0U;
    if (stream->offset == stream->size) {
        events = MR_ADC_WAIT_STREAM_FULL;
        if (stream->circular) {
            stream->offset = 0U;
        } else {
            adc_injected_quiesce(priv);
        }
    } else if ((old_offset < stream->size / 2U)
               && (stream->offset >= stream->size / 2U)) {
        events = MR_ADC_WAIT_STREAM_HALF;
    }
    events &= stream->events;
    data = MR_DRIVER_DATA(drv);
    if ((events != 0U) && MR_OBJECT_IS_INITED(&data->event)) {
        mr_adc_event_raise(drv, events);
    }
    mr_port_irq_enable(mask);
}

/* Driver ADC injected operations */
MR_UNUSED static const mr_adc_ops_t __adc_injected_ops = {
    .channel_configure = adc_channel_configure,
    .channel_configuration = adc_channel_configuration,
    .read = adc_read,
    .stream_start = adc_injected_start,
    .stream_stop = adc_injected_stop,
    .stream_is_busy = adc_injected_is_busy,
    .stream_offset = adc_injected_offset,
    .event_control = adc_injected_event_control,
};
#endif /* defined(ADC_USE_INJECTED_STREAM) */

/* Instance install */
#define CH32_HT_ADC_INSTALL(_id, _ops)                                         \
    extern void MR_HT_DRIVER_PROP(_id, wch_clken)(void);                       \
    static adc_priv_t MR_CAT(__adc_priv_, _id) = {                             \
        .adc = MR_HT_DRIVER_PROP(_id, wch_adcx),                               \
        .clk_enable = MR_HT_DRIVER_PROP(_id, wch_clken),                       \
        .ref = MR_HT_DRIVER_PROP_OR(_id, wch_ref, MR_ADC_REF_VDD_1),           \
        __CH32_HT_ADC_STREAM(_id)                                              \
        __CH32_HT_ADC_INJECTED_STREAM(_id)                                     \
    };                                                                         \
    __CH32_HT_ADC_INJECTED_IRQ(_id)                                            \
    MR_HT_ADC_INSTALL(_id, &MR_CAT(__adc_priv_, _id),                          \
                      __CH32_HT_ADC_OPS(_id, _ops))
#if defined(ADC_USE_STREAM)
#define __CH32_HT_ADC_STREAM(_id)                                              \
    .stream = {                                                                \
        .dma = MR_HT_DMA_SPEC_OR(_id, rx_dma, {0}),                            \
    },                                                                         \
    .trigger = MR_HT_DRIVER_PROP_OR(_id, wch_trigger, ADC_SOFTWARE_TRIGGER),
#else
#define __CH32_HT_ADC_STREAM(_id)
#endif /* defined(ADC_USE_STREAM) */
#if defined(ADC_USE_INJECTED_STREAM)
#define __CH32_HT_ADC_INJECTED_STREAM(_id)                                     \
    .injected = {                                                              \
        .trigger = MR_HT_DRIVER_PROP_OR(_id, wch_injected_trigger, 0U),        \
        .irq_priority = MR_HT_DRIVER_PROP_OR(_id, wch_irq_priority, 0U),       \
    },
#define __CH32_HT_ADC_INJECTED_IRQ(_id)                                        \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, wch_injected_trigger),              \
                (__CH32_HT_ADC_INSTALL_IRQ(_id)), ())
#define __CH32_HT_ADC_INSTALL_IRQ(_id)                                         \
    MR_CH32_INTERRUPT void ADC1_IRQHandler(void) {                             \
        adc_injected_irq(&MR_CAT(__adc_priv_, _id));                           \
    }
#define __CH32_HT_ADC_OPS(_id, _ops)                                           \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, wch_injected_trigger),              \
                (&__adc_injected_ops), (_ops))
#else
#define __CH32_HT_ADC_INJECTED_STREAM(_id)
#define __CH32_HT_ADC_INJECTED_IRQ(_id)
#define __CH32_HT_ADC_OPS(_id, _ops)     (_ops)
#endif /* defined(ADC_USE_INJECTED_STREAM) */
MR_HT_DRIVER_FOREACH(CH32_HT_ADC_INSTALL, &__adc_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_ADC) */

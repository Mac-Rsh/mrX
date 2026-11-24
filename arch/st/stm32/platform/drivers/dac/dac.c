/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_DAC)
#include <mrX/drivers/dac/mr_dac.h>
#include <mrX/drivers/gpio/mr_gpio.h>
#if defined(MR_USE_DRIVER_DAC_STREAM) && defined(MR_USE_DRIVER_DMA)
#include <mrX/drivers/dma/mr_dma.h>
#define DAC_USE_STREAM
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) && defined(MR_USE_DRIVER_DMA) */

/* HardTree compatible */
#define MR_HT_COMPAT                    st_stm32_dac
#if MR_HT_HAS_DRIVER()
#if !defined(MR_USE_DRIVER_GPIO)
#error "STM32 DAC requires the GPIO driver"
#endif /* !defined(MR_USE_DRIVER_GPIO) */

/* Driver DAC operations */
static mr_err_t dac_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_dac_config_t *config);
static mr_err_t dac_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_dac_config_t *config);
static mr_ssize_t dac_write(const mr_driver_t *drv, mr_dac_seq_t *seq);
#if defined(DAC_USE_STREAM)
static mr_err_t dac_stream_start(const mr_driver_t *drv, mr_dac_seq_t *seq,
                                 const mr_dac_stream_config_t *config);
static mr_err_t dac_stream_stop(const mr_driver_t *drv);
static mr_err_t dac_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy);
static mr_err_t dac_stream_offset(const mr_driver_t *drv, mr_size_t *offset);
static mr_err_t dac_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable);
#endif /* defined(DAC_USE_STREAM) */
MR_UNUSED static const mr_dac_ops_t __dac_ops = {
    .channel_configure = dac_channel_configure,
    .channel_configuration = dac_channel_configuration,
    .write = dac_write,
#if defined(DAC_USE_STREAM)
    .stream_start = dac_stream_start,
    .stream_stop = dac_stream_stop,
    .stream_is_busy = dac_stream_is_busy,
    .stream_offset = dac_stream_offset,
    .event_control = dac_event_control,
#else
    .event_control = MR_NULL,
#endif /* defined(DAC_USE_STREAM) */
};

/* DAC capability definition */
#if defined(DAC_CR_DMAEN1) && defined(DAC_CR_TEN1) && defined(DAC_IT_DMAUDR1)  \
    && defined(DAC_FLAG_DMAUDR1)
#define STM32_DAC_HAS_STREAM
#endif /* DAC stream capability */
#if defined(DAC_USE_STREAM) && !defined(STM32_DAC_HAS_STREAM)
#error "STM32 DAC stream is not supported by this DAC IP"
#endif /* defined(DAC_USE_STREAM) && !defined(STM32_DAC_HAS_STREAM) */

/* DAC channel type */
typedef struct dac_channel {
    uint32_t channel;
    mr_ht_gpio_spec_t gpio;
} dac_channel_t;

/* DAC sample format type */
typedef struct dac_sample_format {
    uint32_t alignment;
    mr_uint16_t max_value;
    mr_uint8_t sample_size;
} dac_sample_format_t;

#if defined(DAC_USE_STREAM)
/* DAC DMA type */
typedef struct dac_dma {
    mr_ht_dma_spec_t dma;
    mr_dma_transfer_t transfer;
    mr_waiter_t waiter;
} dac_dma_t;

/* DAC stream channel type */
typedef struct dac_stream_channel {
    uint32_t dma_enable;
    uint32_t trigger_enable;
    uint32_t underrun_interrupt;
    uint32_t underrun_flag;
} dac_stream_channel_t;

/* DAC stream type */
typedef struct dac_stream {
    uint32_t trigger;
    IRQn_Type irqn;
    mr_uint32_t irq_priority;
    dac_dma_t *dma;
    mr_driver_t *driver;
    mr_size_t size;
    volatile mr_size_t offset;
    mr_size_t prefill;
    mr_uint32_t event_mask;
    volatile mr_bool_t complete;
    volatile mr_bool_t draining;
    mr_uint8_t irq_valid: 1;
    mr_uint8_t trigger_valid: 1;
    mr_uint8_t use_dma: 1;
    mr_uint8_t dma_started: 1;
    mr_uint8_t half_enabled: 1;
    mr_uint8_t circular: 1;
    mr_uint8_t channel: 1;
} dac_stream_t;
#endif /* defined(DAC_USE_STREAM) */

/* DAC private data type */
typedef struct dac_priv {
    DAC_HandleTypeDef handle;
    void (*clk_enable)(void);
    const dac_channel_t *channels;
    mr_size_t channels_num;
    mr_uint8_t ref;
    mr_uint8_t started_mask;
#if defined(DAC_USE_STREAM)
    dac_stream_t stream;
#endif /* defined(DAC_USE_STREAM) */
} dac_priv_t;

/* DAC channel count definition */
#if defined(DAC_CHANNEL_2)
#define DAC_CHANNEL_MAX                 (2U)
#else
#define DAC_CHANNEL_MAX                 (1U)
#endif /* defined(DAC_CHANNEL_2) */

#if defined(DAC_USE_STREAM)
/* DAC event mask definition */
#define DAC_EVENT_MASK                                                         \
    (MR_DAC_WAIT_STREAM_HALF | MR_DAC_WAIT_STREAM_DONE                         \
     | MR_DAC_WAIT_STREAM_ERROR)
#endif /* defined(DAC_USE_STREAM) */

static mr_bool_t dac_hal_channel_valid(uint32_t channel) {
    /* Check channel */
    if (channel == DAC_CHANNEL_1) {
        return MR_TRUE;
    }
#if defined(DAC_CHANNEL_2)
    if (channel == DAC_CHANNEL_2) {
        return MR_TRUE;
    }
#endif /* defined(DAC_CHANNEL_2) */
    return MR_FALSE;
}

static mr_uint32_t dac_channel_mask(const dac_priv_t *priv) {
    /* Get channel mask */
    if (priv->channels_num > DAC_CHANNEL_MAX) {
        return 0U;
    }
    return (1UL << priv->channels_num) - 1U;
}

#if defined(DAC_USE_STREAM)
static dac_stream_channel_t dac_stream_channel(const dac_priv_t *priv,
                                               mr_uint32_t channel) {
    dac_stream_channel_t stream_channel;
    uint32_t hw_channel;

    /* Get hardware channel */
    stream_channel = (dac_stream_channel_t){0};
    hw_channel = priv->channels[channel].channel;

    /* Translate stream channel */
    if (hw_channel == DAC_CHANNEL_1) {
        stream_channel.dma_enable = DAC_CR_DMAEN1;
        stream_channel.trigger_enable = DAC_CR_TEN1;
        stream_channel.underrun_interrupt = DAC_IT_DMAUDR1;
        stream_channel.underrun_flag = DAC_FLAG_DMAUDR1;
        return stream_channel;
    }
#if defined(DAC_CHANNEL_2) && defined(DAC_CR_DMAEN2) && defined(DAC_CR_TEN2)   \
    && defined(DAC_IT_DMAUDR2) && defined(DAC_FLAG_DMAUDR2)
    if (hw_channel == DAC_CHANNEL_2) {
        stream_channel.dma_enable = DAC_CR_DMAEN2;
        stream_channel.trigger_enable = DAC_CR_TEN2;
        stream_channel.underrun_interrupt = DAC_IT_DMAUDR2;
        stream_channel.underrun_flag = DAC_FLAG_DMAUDR2;
    }
#endif /* DAC channel 2 stream capability */
    return stream_channel;
}
#endif /* defined(DAC_USE_STREAM) */

static mr_err_t dac_init(dac_priv_t *priv) {
    mr_size_t i, j;

    /* Check DAC resources */
    if ((priv->handle.Instance == MR_NULL) || (priv->clk_enable == MR_NULL)
        || (priv->channels == MR_NULL) || (priv->channels_num == 0U)
        || (priv->channels_num > DAC_CHANNEL_MAX)) {
        return -MR_ENODEV;
    }

    /* Check DAC channels */
    for (i = 0U; i < priv->channels_num; i++) {
        /* Check channel */
        if (!dac_hal_channel_valid(priv->channels[i].channel)) {
            return -MR_EINVAL;
        }

        /* Check duplicate channel */
        for (j = 0U; j < i; j++) {
            if (priv->channels[i].channel == priv->channels[j].channel) {
                return -MR_EINVAL;
            }
        }
    }

    /* Enable DAC clock */
    priv->clk_enable();

    /* Init DAC */
    return mr_stm32_hal_status(HAL_DAC_Init(&priv->handle));
}

static mr_err_t dac_gpio_configure(dac_priv_t *priv, mr_uint32_t channel) {
    const dac_channel_t *channel_data;
    mr_gpio_config_t gpio_config;

    channel_data = &priv->channels[channel];

    /* Check GPIO */
    if (channel_data->gpio.drv == MR_NULL) {
        return -MR_ENODEV;
    }

    /* Configure GPIO */
    gpio_config = (mr_gpio_config_t){0};
    gpio_config.mode = MR_GPIO_MODE_ANALOG;
    gpio_config.pull = MR_GPIO_PULL_NONE;
    return mr_gpio_configure(channel_data->gpio.drv, channel_data->gpio.pin,
                             &gpio_config);
}
static mr_err_t dac_configure_hw_channel(dac_priv_t *priv, mr_uint32_t channel,
                                         uint32_t trigger) {
    DAC_ChannelConfTypeDef dac_config;
    mr_uint8_t bit;
    mr_err_t ret;

    /* Configure GPIO */
    ret = dac_gpio_configure(priv, channel);
    if (ret != 0) {
        return ret;
    }

    /* Stop DAC channel */
    bit = (mr_uint8_t)(1U << channel);
    if ((priv->started_mask & bit) != 0U) {
        ret = mr_stm32_hal_status(
            HAL_DAC_Stop(&priv->handle, priv->channels[channel].channel));
        if (ret != 0) {
            return ret;
        }
        priv->started_mask &= (mr_uint8_t)~bit;
    }

    /* Configure DAC channel */
    dac_config = (DAC_ChannelConfTypeDef){0};
#if defined(DAC_HIGH_FREQUENCY_INTERFACE_MODE_AUTOMATIC)
    dac_config.DAC_HighFrequency = DAC_HIGH_FREQUENCY_INTERFACE_MODE_AUTOMATIC;
#endif /* defined(DAC_HIGH_FREQUENCY_INTERFACE_MODE_AUTOMATIC) */
#if defined(DAC_SAMPLEANDHOLD_DISABLE)
    dac_config.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
#endif /* defined(DAC_SAMPLEANDHOLD_DISABLE) */
    dac_config.DAC_Trigger = trigger;
    dac_config.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
#if defined(DAC_CHIPCONNECT_EXTERNAL)
    dac_config.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_EXTERNAL;
#endif /* defined(DAC_CHIPCONNECT_EXTERNAL) */
#if defined(DAC_TRIMMING_FACTORY)
    dac_config.DAC_UserTrimming = DAC_TRIMMING_FACTORY;
#endif /* defined(DAC_TRIMMING_FACTORY) */
    return mr_stm32_hal_status(HAL_DAC_ConfigChannel(
        &priv->handle, &dac_config, priv->channels[channel].channel));
}

static mr_err_t dac_seq_channels(const dac_priv_t *priv, mr_uint32_t channels,
                                 mr_uint8_t *index, mr_uint32_t *count) {
    mr_uint32_t channel, rank;

    /* Check channels */
    if ((channels == 0U) || ((channels & ~dac_channel_mask(priv)) != 0U)) {
        return -MR_EINVAL;
    }

    /* Build DAC ranks */
    rank = 0U;
    for (channel = 0U; channel < priv->channels_num; channel++) {
        if ((channels & (1U << channel)) == 0U) {
            continue;
        }

        /* Check rank */
        if (rank >= DAC_CHANNEL_MAX) {
            return -MR_EINVAL;
        }

        /* Set rank channel */
        index[rank] = (mr_uint8_t)channel;
        rank++;
    }

    /* Set rank count */
    *count = rank;
    return 0;
}

static mr_err_t dac_sample_format(mr_uint8_t resolution,
                                  dac_sample_format_t *format) {
    switch (resolution) {
        case 8U: {
#if defined(DAC_ALIGN_8B_R)
            format->alignment = DAC_ALIGN_8B_R;
            format->max_value = 0x00ffU;
            format->sample_size = sizeof(mr_uint8_t);
            return 0;
#else
            return -MR_ENOSYS;
#endif /* defined(DAC_ALIGN_8B_R) */
        }
        case 12U: {
            format->alignment = DAC_ALIGN_12B_R;
            format->max_value = 0x0fffU;
            format->sample_size = sizeof(mr_uint16_t);
            return 0;
        }
        default: {
            return -MR_EINVAL;
        }
    }
}

static mr_uint16_t dac_sample_get(const void *buf, mr_size_t index,
                                  mr_uint8_t sample_size) {
    if (sample_size == sizeof(mr_uint8_t)) {
        return ((const mr_uint8_t *)buf)[index];
    }
    return ((const mr_uint16_t *)buf)[index];
}

static mr_err_t dac_channel_start(const mr_driver_t *drv, mr_uint32_t channel,
                                  uint32_t alignment, mr_uint16_t value) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint8_t bit;
    mr_err_t ret;

    /* Set output value */
    ret = mr_stm32_hal_status(HAL_DAC_SetValue(&priv->handle,
                                               priv->channels[channel].channel,
                                               alignment, value));
    if (ret != 0) {
        return ret;
    }

    /* Start DAC channel */
    bit = (mr_uint8_t)(1U << channel);
    if ((priv->started_mask & bit) != 0U) {
        return 0;
    }
    ret = mr_stm32_hal_status(
        HAL_DAC_Start(&priv->handle, priv->channels[channel].channel));
    if (ret != 0) {
        return ret;
    }
    priv->started_mask |= bit;
    return 0;
}

static mr_err_t dac_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_dac_config_t *config) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
#if defined(DAC_USE_STREAM)
    mr_bool_t busy;
#endif /* defined(DAC_USE_STREAM) */
    mr_err_t ret;

    /* Check channel */
    if (channel >= priv->channels_num) {
        return -MR_EINVAL;
    }

#if defined(DAC_USE_STREAM)
    /* Check stream state */
    ret = dac_stream_is_busy(drv, &busy);
    if (ret != 0) {
        return ret;
    }
    if (busy) {
        return -MR_EBUSY;
    }
#endif /* defined(DAC_USE_STREAM) */

    /* Check configuration */
    if (config->ref != priv->ref) {
        return -MR_EINVAL;
    }

    /* Init DAC */
    ret = dac_init(priv);
    if (ret != 0) {
        return ret;
    }

    /* Configure DAC channel */
    return dac_configure_hw_channel(priv, channel, DAC_TRIGGER_NONE);
}

static mr_err_t dac_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_dac_config_t *config) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check channel */
    if (channel >= priv->channels_num) {
        return -MR_EINVAL;
    }

    /* Get channel configuration */
    config->ref = priv->ref;
    return 0;
}

static mr_ssize_t dac_write(const mr_driver_t *drv, mr_dac_seq_t *seq) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint8_t ranks[DAC_CHANNEL_MAX];
    mr_uint32_t channel_count, rank;
    dac_sample_format_t format;
    mr_size_t samples, i;
    mr_uint16_t value;
    mr_err_t ret;
#if defined(DAC_USE_STREAM)
    mr_bool_t busy;
#endif /* defined(DAC_USE_STREAM) */

#if defined(DAC_USE_STREAM)
    /* Check stream state */
    ret = dac_stream_is_busy(drv, &busy);
    if (ret != 0) {
        return ret;
    }
    if (busy) {
        return -MR_EBUSY;
    }
#endif /* defined(DAC_USE_STREAM) */

    /* Check sequence */
    ret = dac_sample_format(seq->resolution, &format);
    if (ret != 0) {
        return ret;
    }
    if ((seq->channels == 0U)
        || ((seq->channels & ~dac_channel_mask(priv)) != 0U)) {
        return -MR_EINVAL;
    }
    if (seq->size == 0U) {
        return 0;
    }

    /* Check sequence buffer */
    if ((seq->buf == MR_NULL) || (seq->size > (mr_size_t)MR_SSIZE_MAX)
        || ((seq->size % format.sample_size) != 0U)
        || (((mr_uintptr_t)seq->buf % format.sample_size) != 0U)) {
        return -MR_EINVAL;
    }

    /* Build DAC ranks */
    ret = dac_seq_channels(priv, seq->channels, ranks, &channel_count);
    if (ret != 0) {
        return ret;
    }

    /* Check sample count */
    samples = seq->size / format.sample_size;
    if ((samples % channel_count) != 0U) {
        return -MR_EINVAL;
    }

    /* Check samples */
    for (i = 0U; i < samples; i++) {
        value = dac_sample_get(seq->buf, i, format.sample_size);
        if (value > format.max_value) {
            return -MR_EINVAL;
        }
    }

    /* Init DAC */
    ret = dac_init(priv);
    if (ret != 0) {
        return ret;
    }

    /* Configure DAC channels */
    for (rank = 0U; rank < channel_count; rank++) {
        ret = dac_configure_hw_channel(priv, ranks[rank], DAC_TRIGGER_NONE);
        if (ret != 0) {
            return ret;
        }
    }

    /* Write samples */
    for (i = 0U; i < samples; i++) {
        value = dac_sample_get(seq->buf, i, format.sample_size);
        ret = dac_channel_start(drv, ranks[i % channel_count], format.alignment,
                                value);
        if (ret != 0) {
            return (i != 0U) ? (mr_ssize_t)(i * format.sample_size) : ret;
        }
    }
    return (mr_ssize_t)seq->size;
}

#if defined(DAC_USE_STREAM)
static volatile uint32_t *dac_dhr(DAC_TypeDef *instance, uint32_t channel,
                                  uint32_t alignment) {
    /* Get data register */
#if defined(DAC_ALIGN_8B_R)
    if (alignment == DAC_ALIGN_8B_R) {
        if (channel == DAC_CHANNEL_1) {
            return &instance->DHR8R1;
        }
#if defined(DAC_CHANNEL_2)
        if (channel == DAC_CHANNEL_2) {
            return &instance->DHR8R2;
        }
#endif /* defined(DAC_CHANNEL_2) */
        return MR_NULL;
    }
#endif /* defined(DAC_ALIGN_8B_R) */
    if (alignment == DAC_ALIGN_12B_R) {
        if (channel == DAC_CHANNEL_1) {
            return &instance->DHR12R1;
        }
#if defined(DAC_CHANNEL_2)
        if (channel == DAC_CHANNEL_2) {
            return &instance->DHR12R2;
        }
#endif /* defined(DAC_CHANNEL_2) */
    }
    return MR_NULL;
}

static void dac_stream_notify(dac_stream_t *stream, mr_wait_event_t events) {
    if (events == 0U) {
        return;
    }

    /* Filter DAC events */
    events &= (mr_wait_event_t)stream->event_mask;
    if ((events != 0U) && (stream->driver != MR_NULL)) {
        mr_dac_event_raise(stream->driver, events);
    }
}

static void dac_stream_disable(dac_priv_t *priv) {
    dac_stream_t *stream = &priv->stream;
    dac_stream_channel_t channel;

    /* Get stream channel */
    channel = dac_stream_channel(priv, stream->channel);

    /* Disable stream requests */
    __HAL_DAC_DISABLE_IT(&priv->handle, channel.underrun_interrupt);
    CLEAR_BIT(priv->handle.Instance->CR,
              channel.dma_enable | channel.trigger_enable);
}

static void dac_stream_halt(dac_priv_t *priv) {
    /* Disable stream */
    dac_stream_disable(priv);

    /* Complete stream */
    priv->stream.complete = MR_TRUE;
}

static void dac_dma_event(mr_waiter_t *waiter, void *param,
                          mr_wait_event_t event, const void *data) {
    dac_priv_t *priv = (dac_priv_t *)param;
    dac_stream_t *stream = &priv->stream;
    mr_wait_event_t dac_event;

    MR_IGNORE(waiter);
    MR_IGNORE(data);

    /* Check stream state */
    if ((stream->driver == MR_NULL) || stream->complete) {
        return;
    }

    /* Translate DMA event */
    dac_event = 0U;
    if ((event & MR_DMA_WAIT_ERROR) != 0U) {
        dac_stream_halt(priv);
        dac_event = MR_DAC_WAIT_STREAM_ERROR;
    } else {
        if (((event & MR_DMA_WAIT_HALF) != 0U) && stream->half_enabled) {
            dac_event |= MR_DAC_WAIT_STREAM_HALF;
        }
        if ((event & MR_DMA_WAIT_DONE) != 0U) {
            if (stream->circular) {
                dac_event |= MR_DAC_WAIT_STREAM_DONE;
            } else {
                /* DMA completion leaves the final sample in DHR */
                stream->draining = MR_TRUE;
            }
        }
    }

    /* Raise DAC event */
    dac_stream_notify(stream, dac_event);
}

static mr_wait_event_t dac_stream_dma_events(const dac_stream_t *stream) {
    return MR_DMA_WAIT_DONE | MR_DMA_WAIT_ERROR
         | (stream->half_enabled ? MR_DMA_WAIT_HALF : 0U);
}

static mr_err_t dac_stream_start_dma(dac_priv_t *priv,
                                     const mr_dac_seq_t *seq,
                                     const dac_sample_format_t *format) {
    dac_stream_t *stream = &priv->stream;
    dac_dma_t *dma = stream->dma;
    mr_dma_config_t config;
    mr_wait_event_t events;
    mr_uint32_t width;
    mr_err_t ret;

    /* Configure sample access width */
    width = (format->sample_size == sizeof(mr_uint8_t)) ? MR_DMA_WIDTH_8
                                                        : MR_DMA_WIDTH_16;
    config.src_width = width;
#if defined(STM32H5)
    /* Use word writes to the H5 DAC data holding register */
    config.dst_width = MR_DMA_WIDTH_32;
#else
    config.dst_width = width;
#endif /* defined(STM32H5) */
    config.src_adj = MR_DMA_ADJ_INC;
    config.dst_adj = MR_DMA_ADJ_CONST;
    config.src_burst = MR_DMA_BURST_1;
    config.dst_burst = MR_DMA_BURST_1;
    config.direction = MR_DMA_DIR_M2P;

    /* Build DMA transfer */
    dma->transfer.next = stream->circular ? &dma->transfer : MR_NULL;
    dma->transfer.src = (const mr_uint8_t *)seq->buf + stream->prefill;
    dma->transfer.dst = (void *)dac_dhr(
        priv->handle.Instance, priv->channels[stream->channel].channel,
        format->alignment);
    dma->transfer.count
        = (seq->size - stream->prefill) / format->sample_size;
    if (dma->transfer.dst == MR_NULL) {
        return -MR_EINVAL;
    }
    if (dma->transfer.count == 0U) {
        stream->draining = MR_TRUE;
        return 0;
    }

    /* Get DMA events */
    events = dac_stream_dma_events(stream);

    /* Configure DMA */
    ret = mr_ht_dma_configure(&dma->dma, &config);
    if (ret != 0) {
        return ret;
    }

    /* Init waiter */
    ret = mr_waiter_init(&dma->waiter, dac_dma_event, priv, events);
    if (ret != 0) {
        return ret;
    }

    /* Attach DMA event */
    ret = mr_ht_dma_event_attach(&dma->dma, &dma->waiter);
    if (ret != 0) {
        goto _exit;
    }
    ret = mr_ht_dma_event_enable(&dma->dma, events);
    if (ret != 0) {
        goto _exit;
    }

    /* Start DMA */
    ret = mr_ht_dma_start(&dma->dma, &dma->transfer);
    if (ret != 0) {
        mr_ht_dma_event_disable(&dma->dma, events);
        goto _exit;
    }
    stream->dma_started = MR_TRUE;
    return 0;
_exit:
    /* Release waiter */
    mr_waiter_del(&dma->waiter);
    return ret;
}

static mr_err_t dac_stream_start(const mr_driver_t *drv, mr_dac_seq_t *seq,
                                 const mr_dac_stream_config_t *config) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    dac_stream_t *stream = &priv->stream;
    dac_stream_channel_t stream_channel;
    mr_size_t sample_count, i;
    dac_sample_format_t format;
    mr_bool_t busy, use_dma;
    mr_uint32_t channel;
    mr_uint16_t value;
    mr_err_t ret;

    /* Check stream source */
    if (config->source != MR_DAC_STREAM_SOURCE_EXTERNAL) {
        return -MR_ENOSYS;
    }

    /* Check stream resources */
    use_dma = MR_FALSE;
    if ((stream->dma != MR_NULL) && (stream->dma->dma.drv != MR_NULL)) {
        use_dma = MR_TRUE;
    }
    if (!stream->trigger_valid) {
        return -MR_ENODEV;
    }
    if (!config->circular && !stream->irq_valid) {
        return -MR_ENODEV;
    }

    /* Check sequence configuration */
    ret = dac_sample_format(seq->resolution, &format);
    if (ret != 0) {
        return ret;
    }
    if ((seq->channels == 0U)
        || ((seq->channels & ~dac_channel_mask(priv)) != 0U)) {
        return -MR_EINVAL;
    }
    if ((seq->channels & (seq->channels - 1U)) != 0U) {
        return -MR_EINVAL;
    }

    /* Check buffer */
    if ((seq->buf == MR_NULL) || (seq->size == 0U)
        || ((seq->size % format.sample_size) != 0U)
        || (((mr_uintptr_t)seq->buf % format.sample_size) != 0U)) {
        return -MR_EINVAL;
    }

    /* Check sample count */
    sample_count = seq->size / format.sample_size;
    if (!use_dma && ((sample_count != 1U) || config->circular)) {
        return -MR_ENOSYS;
    }

    /* Check samples */
    for (i = 0U; i < sample_count; i++) {
        value = dac_sample_get(seq->buf, i, format.sample_size);
        if (value > format.max_value) {
            return -MR_EINVAL;
        }
    }

    /* Check stream state */
    ret = dac_stream_is_busy(drv, &busy);
    if (ret != 0) {
        return ret;
    }
    if (busy) {
        return -MR_EBUSY;
    }

    /* Init DAC */
    ret = dac_init(priv);
    if (ret != 0) {
        return ret;
    }

    /* Find stream channel */
    for (channel = 0U; channel < priv->channels_num; channel++) {
        if ((seq->channels & (1U << channel)) != 0U) {
            break;
        }
    }

    /* Get stream channel */
    stream_channel = dac_stream_channel(priv, channel);
    if ((stream_channel.dma_enable == 0U)
        || (stream_channel.trigger_enable == 0U)
        || (stream_channel.underrun_interrupt == 0U)
        || (stream_channel.underrun_flag == 0U)) {
        return -MR_ENOSYS;
    }

    /* Configure stream channel */
    ret = dac_configure_hw_channel(priv, channel, stream->trigger);
    if (ret != 0) {
        return ret;
    }

    /* Update stream state */
    stream->channel = (mr_uint8_t)channel;
    stream->circular = (config->circular != MR_FALSE);
    stream->size = seq->size;
    stream->prefill = stream->circular ? 0U : format.sample_size;
    stream->offset = stream->prefill;
    stream->use_dma = use_dma;
    stream->dma_started = MR_FALSE;
    stream->half_enabled = stream->circular && ((sample_count % 2U) == 0U);
    stream->complete = MR_FALSE;
    stream->draining = !use_dma;
    stream->driver = (mr_driver_t *)drv;

    /* Preload first sample */
    if (stream->prefill != 0U) {
        value = dac_sample_get(seq->buf, 0U, format.sample_size);
        ret = mr_stm32_hal_status(
            HAL_DAC_SetValue(&priv->handle, priv->channels[channel].channel,
                             format.alignment, value));
        if (ret != 0) {
            goto _stop;
        }
    }

    /* Start DMA */
    if (use_dma) {
        ret = dac_stream_start_dma(priv, seq, &format);
        if (ret != 0) {
            goto _stop;
        }
    }

    /* Enable stream requests */
    __HAL_DAC_CLEAR_FLAG(&priv->handle, stream_channel.underrun_flag);
    if (!stream->circular) {
        HAL_NVIC_SetPriority(stream->irqn, stream->irq_priority, 0U);
        HAL_NVIC_EnableIRQ(stream->irqn);
        __HAL_DAC_ENABLE_IT(&priv->handle, stream_channel.underrun_interrupt);
    }
    SET_BIT(priv->handle.Instance->CR, stream_channel.dma_enable);

    /* Start DAC */
    priv->started_mask |= (mr_uint8_t)(1U << channel);
    ret = mr_stm32_hal_status(
        HAL_DAC_Start(&priv->handle, priv->channels[channel].channel));
    if (ret == 0) {
        return 0;
    }
_stop:
    /* Stop stream */
    dac_stream_stop(drv);
    return ret;
}

static mr_err_t dac_stream_stop(const mr_driver_t *drv) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    dac_stream_t *stream = &priv->stream;
    dac_stream_channel_t channel;
    mr_wait_event_t events;
    dac_dma_t *dma;
    mr_uint8_t bit;
    mr_err_t ret;

    /* Check stream state */
    if (stream->driver == MR_NULL) {
        return 0;
    }

    /* Get stream channel */
    channel = dac_stream_channel(priv, stream->channel);

    /* Disable stream */
    dac_stream_disable(priv);

    /* Clear underrun flag */
    __HAL_DAC_CLEAR_FLAG(&priv->handle, channel.underrun_flag);

    /* Stop DAC */
    bit = (mr_uint8_t)(1U << stream->channel);
    if ((priv->started_mask & bit) != 0U) {
        ret = mr_stm32_hal_status(HAL_DAC_Stop(
            &priv->handle, priv->channels[stream->channel].channel));
        if (ret != 0) {
            return ret;
        }
        priv->started_mask &= (mr_uint8_t)~bit;
    }

    /* Check DMA */
    dma = stream->dma;
    if (stream->use_dma && stream->dma_started && (dma != MR_NULL)) {
        /* Get DMA events */
        events = dac_stream_dma_events(stream);

        /* Stop DMA */
        ret = mr_ht_dma_stop(&dma->dma);
        if (ret != 0) {
            return ret;
        }

        /* Disable DMA event */
        ret = mr_ht_dma_event_disable(&dma->dma, events);
        if (ret != 0) {
            return ret;
        }

        /* Release waiter */
        mr_waiter_del(&dma->waiter);
        stream->dma_started = MR_FALSE;
    }

    /* Release stream */
    stream->driver = MR_NULL;
    return 0;
}

static mr_err_t dac_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    dac_stream_t *stream = &priv->stream;
    mr_bool_t dma_busy;
    mr_err_t ret;

    /* Check stream state */
    if (stream->driver == MR_NULL) {
        *busy = MR_FALSE;
        return 0;
    }
    if (stream->complete) {
        ret = dac_stream_stop(drv);
        if (ret != 0) {
            return ret;
        }
        *busy = MR_FALSE;
        return 0;
    }

    /* Check stream mode */
    if (!stream->circular) {
        *busy = MR_TRUE;
        return 0;
    }
    if (!stream->use_dma || (stream->dma == MR_NULL)) {
        return -MR_ENODEV;
    }

    /* Check DMA state */
    ret = mr_ht_dma_is_busy(&stream->dma->dma, &dma_busy);
    if (ret != 0) {
        return ret;
    }
    if (dma_busy) {
        *busy = MR_TRUE;
        return 0;
    }

    /* Complete stream */
    stream->complete = MR_TRUE;
    ret = dac_stream_stop(drv);
    if (ret != 0) {
        return ret;
    }
    *busy = MR_FALSE;
    return 0;
}

static mr_err_t dac_stream_offset(const mr_driver_t *drv, mr_size_t *offset) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    dac_stream_t *stream = &priv->stream;
    mr_err_t ret;

    /* Get DMA stream offset */
    if (stream->use_dma) {
        if (stream->dma->transfer.count == 0U) {
            *offset = stream->prefill;
            return 0;
        }
        ret = mr_ht_dma_offset(&stream->dma->dma, offset);
        if (ret != 0) {
            return ret;
        }
        *offset += stream->prefill;
        return 0;
    }

    /* Get IRQ stream offset */
    *offset = stream->offset;
    return 0;
}

static mr_err_t dac_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    dac_stream_t *stream = &priv->stream;

    /* Check parameter */
    if ((events == 0U) || ((events & ~DAC_EVENT_MASK) != 0U)) {
        return -MR_EINVAL;
    }

    /* Check event state */
    if (!enable && (stream->driver != MR_NULL)) {
        if (((events & MR_DAC_WAIT_STREAM_ERROR) != 0U)
            || (((events & MR_DAC_WAIT_STREAM_DONE) != 0U)
                && !stream->circular)) {
            return -MR_EBUSY;
        }
    }

    /* Update event mask */
    if (enable) {
        stream->event_mask |= events;
    } else {
        stream->event_mask &= ~events;
    }
    return 0;
}

MR_UNUSED static void dac_irq(dac_priv_t *priv) {
    dac_stream_t *stream = &priv->stream;
    dac_stream_channel_t channel;
    mr_wait_event_t event;

    /* Check stream state */
    if ((stream->driver == MR_NULL) || stream->complete || stream->circular) {
        return;
    }

    /* Get stream channel */
    channel = dac_stream_channel(priv, stream->channel);
    if ((channel.underrun_flag == 0U) || (channel.underrun_interrupt == 0U)) {
        return;
    }

    /* Check underrun */
    if ((__HAL_DAC_GET_FLAG(&priv->handle, channel.underrun_flag) == 0U)
        || (__HAL_DAC_GET_IT_SOURCE(&priv->handle, channel.underrun_interrupt)
            == 0U)) {
        return;
    }

    /* Complete stream */
    __HAL_DAC_CLEAR_FLAG(&priv->handle, channel.underrun_flag);
    dac_stream_halt(priv);
    stream->offset = stream->size;

    /* Get stream event */
    if (stream->draining) {
        event = MR_DAC_WAIT_STREAM_DONE;
    } else {
        event = MR_DAC_WAIT_STREAM_ERROR;
    }

    /* Raise DAC event */
    dac_stream_notify(stream, event);
}
#endif /* defined(DAC_USE_STREAM) */

/* Instance install */
#define ST_HT_DAC_INSTALL(_id, _ops)                                           \
    static void MR_CAT(__dac_clk_enable_, _id)(void) {                         \
        MR_HT_DRIVER_PROP(_id, st_clken)();                                    \
    }                                                                          \
    static const dac_channel_t MR_CAT(__dac_channel_, _id)[]                   \
        = {MR_HT_DRIVER_PROP_ELEM_FOREACH(_id, st_channels,                    \
                                          __ST_HT_DAC_CHANNEL_IMPL)};          \
    static mr_dac_data_t MR_CAT(__dac_data_, _id) = MR_DAC_DATA_INIT(          \
        MR_HT_DRIVER_PROP_OR(_id, ref_internal, 3300),                         \
        (1UL << MR_HT_DRIVER_PROP_ELEM_NUM(_id, st_channels)) - 1U);           \
    __ST_HT_DAC_DMA_INSTALL(_id)                                               \
    static dac_priv_t MR_CAT(__dac_priv_, _id) = {                             \
        .handle = {                                                            \
            .Instance = MR_HT_DRIVER_PROP(_id, st_dacx),                       \
        },                                                                     \
        .clk_enable = MR_CAT(__dac_clk_enable_, _id),                          \
        .channels = MR_CAT(__dac_channel_, _id),                               \
        .channels_num = MR_HT_DRIVER_PROP_ELEM_NUM(_id, st_channels),          \
        .ref = MR_HT_DRIVER_PROP_OR(_id, st_ref, MR_DAC_REF_INTERNAL),         \
        __ST_HT_DAC_STREAM(_id)                                                \
    };                                                                         \
    __ST_HT_DAC_IRQ(_id)                                                       \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(dac),                   \
                         MR_DRIVER_TYPE_DAC, &MR_CAT(__dac_data_, _id),        \
                         &MR_CAT(__dac_priv_, _id), _ops)
#define __ST_HT_DAC_CHANNEL_IMPL(_elem, _node, _prop)                          \
    {                                                                          \
        .channel = MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 0),               \
        .gpio = {                                                              \
            .drv =                                                             \
                MR_HT_DRIVER_FIND(                                             \
                    MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 1)),             \
            .pin = (mr_uint8_t)MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 2),   \
        },                                                                     \
    }
#if defined(DAC_USE_STREAM)
#define __ST_HT_DAC_HAS_DMA(_id) MR_HT_DRIVER_HAS_PROP(_id, st_dma)
#define __ST_HT_DAC_DMA_INSTALL(_id)                                           \
    MR_MACRO_IF(__ST_HT_DAC_HAS_DMA(_id),                                      \
                (static dac_dma_t MR_CAT(__dac_dma_, _id) = {                  \
                    .dma = MR_HT_DMA_SPEC(_id, st_dma),                        \
                };),                                                           \
                ())
#define __ST_HT_DAC_DMA(_id)                                                   \
    MR_MACRO_IF(__ST_HT_DAC_HAS_DMA(_id),                                      \
                (.dma = &MR_CAT(__dac_dma_, _id),), ())
#define __ST_HT_DAC_HAS_IRQ(_id)                                               \
    MR_MACRO_AND(MR_HT_DRIVER_HAS_PROP(_id, st_irqn),                          \
                 MR_HT_DRIVER_HAS_PROP(_id, st_irq_handler))
#define __ST_HT_DAC_IRQ(_id)                                                   \
    MR_MACRO_IF(__ST_HT_DAC_HAS_IRQ(_id),                                      \
                (void MR_HT_DRIVER_PROP(_id, st_irq_handler)(void) {           \
                    dac_irq(&MR_CAT(__dac_priv_, _id));                        \
                }),                                                            \
                ())
#define __ST_HT_DAC_STREAM(_id)                                                \
    .stream = {                                                                \
        __ST_HT_DAC_DMA(_id)                                                   \
        .trigger = MR_HT_DRIVER_PROP_OR(_id, st_trigger, 0U),                  \
        .irqn = MR_HT_DRIVER_PROP_OR(_id, st_irqn, (IRQn_Type)0),              \
        .irq_priority = MR_HT_DRIVER_PROP_OR(_id, st_irq_priority, 0U),        \
        .irq_valid = __ST_HT_DAC_HAS_IRQ(_id),                                 \
        .trigger_valid = MR_HT_DRIVER_HAS_PROP(_id, st_trigger),               \
    },
#else
#define __ST_HT_DAC_DMA_INSTALL(_id)
#define __ST_HT_DAC_IRQ(_id)
#define __ST_HT_DAC_STREAM(_id)
#endif /* defined(DAC_USE_STREAM) */
MR_HT_DRIVER_FOREACH(ST_HT_DAC_INSTALL, &__dac_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_DAC) */

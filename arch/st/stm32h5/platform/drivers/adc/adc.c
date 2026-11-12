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
#include <mrX/drivers/gpio/mr_gpio.h>
#if defined(MR_USE_DRIVER_ADC_STREAM) && defined(MR_USE_DRIVER_DMA)
#include <mrX/drivers/dma/mr_dma.h>
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) && defined(MR_USE_DRIVER_DMA) */
#include "stm32h5xx_hal_adc.h"
#include "stm32h5xx_hal_adc_ex.h"

/* HardTree compatible */
#define MR_HT_COMPAT                    st_stm32h5_adc
#if MR_HT_HAS_DRIVER()

/* Driver ADC operations */
static mr_err_t adc_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_adc_config_t *config);
static mr_err_t adc_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_adc_config_t *config);
static mr_ssize_t adc_read(const mr_driver_t *drv, mr_adc_seq_t *seq);
#if defined(MR_USE_DRIVER_ADC_STREAM)
static mr_err_t adc_stream_start(const mr_driver_t *drv, mr_adc_seq_t *seq,
                                 const mr_adc_stream_config_t *config);
static mr_err_t adc_stream_stop(const mr_driver_t *drv);
static mr_err_t adc_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy);
static mr_err_t adc_stream_offset(const mr_driver_t *drv, mr_size_t *offset);
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */
static mr_err_t adc_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable);
MR_UNUSED static const mr_adc_ops_t __adc_ops = {
    .channel_configure = adc_channel_configure,
    .channel_configuration = adc_channel_configuration,
    .read = adc_read,
#if defined(MR_USE_DRIVER_ADC_STREAM)
    .stream_start = adc_stream_start,
    .stream_stop = adc_stream_stop,
    .stream_is_busy = adc_stream_is_busy,
    .stream_offset = adc_stream_offset,
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */
    .event_control = adc_event_control,
};

/* ADC channel type */
typedef struct adc_channel {
    uint32_t channel;
    mr_ht_gpio_spec_t gpio;
} adc_channel_t;

#if defined(MR_USE_DRIVER_ADC_STREAM)
#if defined(MR_USE_DRIVER_DMA)
/* ADC DMA phase definition */
typedef enum adc_dma_phase {
    ADC_DMA_IDLE,
    ADC_DMA_WAITER_READY,
    ADC_DMA_EVENTS_ENABLED,
    ADC_DMA_STARTED,
} adc_dma_phase_t;

/* ADC DMA stream type */
typedef struct adc_stream_dma {
    mr_ht_dma_spec_t spec;
    mr_dma_transfer_t xfer;
    mr_waiter_t waiter;
    adc_dma_phase_t phase;
} adc_stream_dma_t;
#endif /* defined(MR_USE_DRIVER_DMA) */

/* ADC stream type */
typedef struct adc_stream {
    uint32_t trigger;
    uint32_t trigger_edge;
    IRQn_Type irqn;
    mr_uint32_t irq_priority;
#if defined(MR_USE_DRIVER_DMA)
    adc_stream_dma_t *dma;
#endif /* defined(MR_USE_DRIVER_DMA) */
    mr_driver_t *drv;
    mr_uint16_t *buf;
    mr_size_t size;
    volatile mr_uint32_t offset;
    mr_atomic_t event_state;
    mr_uint8_t irq_valid: 1;
    mr_uint8_t trigger_valid: 1;
    mr_uint8_t use_dma: 1;
    mr_uint8_t adc_started: 1;
    mr_uint8_t half_enabled: 1;
    mr_uint8_t circular: 1;
} adc_stream_t;
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */

/* ADC private data type */
typedef struct adc_priv {
    ADC_HandleTypeDef *handle;
    void (*clk_enable)(void);
    const adc_channel_t *channels;
    mr_uint32_t channels_num;
    mr_uint8_t ref;
#if defined(MR_USE_DRIVER_ADC_STREAM)
    adc_stream_t *stream;
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */
} adc_priv_t;

/* Device definition */
#define ADC_POLL_TIMEOUT_MS             (100U)
#define ADC_CHANNEL_BITS                (32U)

/* ADC event mask definition */
#define ADC_EVENT_MASK                                                         \
    (MR_ADC_WAIT_STREAM_HALF | MR_ADC_WAIT_STREAM_FULL                         \
     | MR_ADC_WAIT_STREAM_ERROR)

/* The sequence oversample field selects a HAL sampling time */
static const uint32_t __adc_sample_time[] = {
    ADC_SAMPLETIME_2CYCLES_5,   ADC_SAMPLETIME_6CYCLES_5,
    ADC_SAMPLETIME_12CYCLES_5,  ADC_SAMPLETIME_24CYCLES_5,
    ADC_SAMPLETIME_47CYCLES_5,  ADC_SAMPLETIME_92CYCLES_5,
    ADC_SAMPLETIME_247CYCLES_5, ADC_SAMPLETIME_640CYCLES_5,
};

#if defined(MR_USE_DRIVER_ADC_STREAM)
/* ADC stream state definition */
#define ADC_STREAM_COMPLETE             (1U << 3)

/* Stream rank translation */
static const uint32_t __adc_regular_rank[] = {
    ADC_REGULAR_RANK_1, ADC_REGULAR_RANK_2, ADC_REGULAR_RANK_3,
    ADC_REGULAR_RANK_4, ADC_REGULAR_RANK_5, ADC_REGULAR_RANK_6,
    ADC_REGULAR_RANK_7, ADC_REGULAR_RANK_8,
};
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */

static mr_err_t adc_resolution(mr_uint8_t resolution,
                               uint32_t *hal_resolution) {
    /* Translate resolution */
    switch (resolution) {
        case 12U: {
            *hal_resolution = ADC_RESOLUTION_12B;
            break;
        }
        case 10U: {
            *hal_resolution = ADC_RESOLUTION_10B;
            break;
        }
        case 8U: {
            *hal_resolution = ADC_RESOLUTION_8B;
            break;
        }
        case 6U: {
            *hal_resolution = ADC_RESOLUTION_6B;
            break;
        }
        default: {
            return -MR_EINVAL;
        }
    }
    return 0;
}

static mr_err_t adc_init_mode(adc_priv_t *priv, uint32_t resolution,
                              FunctionalState continuous_conv,
                              FunctionalState dma_requests,
                              uint32_t conversions, uint32_t trigger,
                              uint32_t trigger_edge) {
    ADC_HandleTypeDef *handle;
    mr_err_t ret;

    /* Check parameter */
    handle = priv->handle;
    if ((handle == MR_NULL) || (priv->clk_enable == MR_NULL)) {
        return -MR_EINVAL;
    }

    /* Configure conversion mode */
    priv->clk_enable();
    handle->Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    handle->Init.Resolution = resolution;
    handle->Init.DataAlign = ADC_DATAALIGN_RIGHT;
    handle->Init.ScanConvMode
        = (conversions > 1U) ? ADC_SCAN_ENABLE : ADC_SCAN_DISABLE;
    handle->Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    handle->Init.LowPowerAutoWait = DISABLE;
    handle->Init.ContinuousConvMode = continuous_conv;
    handle->Init.NbrOfConversion = conversions;
    handle->Init.DiscontinuousConvMode = DISABLE;
    handle->Init.NbrOfDiscConversion = 1U;
    handle->Init.ExternalTrigConv = trigger;
    handle->Init.ExternalTrigConvEdge = trigger_edge;
    handle->Init.SamplingMode = ADC_SAMPLING_MODE_NORMAL;
    handle->Init.DMAContinuousRequests = dma_requests;
    handle->Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    handle->Init.OversamplingMode = DISABLE;
    ret = mr_stm32h5_hal_status(HAL_ADC_Init(handle));
    if (ret != 0) {
        return ret;
    }

    /* Calibrate ADC */
    return mr_stm32h5_hal_status(
        HAL_ADCEx_Calibration_Start(handle, ADC_SINGLE_ENDED));
}

static mr_err_t adc_init(adc_priv_t *priv, uint32_t resolution) {
    return adc_init_mode(priv, resolution, DISABLE, DISABLE, 1U,
                         ADC_SOFTWARE_START, ADC_EXTERNALTRIGCONVEDGE_NONE);
}

#if defined(MR_USE_DRIVER_ADC_STREAM)
static mr_err_t adc_init_stream(adc_priv_t *priv, uint32_t resolution,
                                mr_uint32_t conversions,
                                const mr_adc_stream_config_t *config,
                                mr_bool_t use_dma) {
    mr_bool_t software;

    /* Select continuous or externally triggered conversions */
    software = (config->source == MR_ADC_STREAM_SOURCE_SOFTWARE);
    return adc_init_mode(priv, resolution, software ? ENABLE : DISABLE,
                         use_dma ? ENABLE : DISABLE, conversions,
                         software ? ADC_SOFTWARE_START : priv->stream->trigger,
                         software ? ADC_EXTERNALTRIGCONVEDGE_NONE
                                  : priv->stream->trigger_edge);
}
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */

static mr_err_t adc_configure_hw_channel(adc_priv_t *priv,
                                         const adc_channel_t *channel,
                                         mr_uint32_t sample_time,
                                         uint32_t rank) {
    ADC_ChannelConfTypeDef config = {0};
    mr_gpio_config_t gpio_config = {0};
    mr_err_t ret;

    /* Check parameter */
    if ((channel->gpio.drv == MR_NULL)
        || (sample_time >= MR_ARRAY_SIZE(__adc_sample_time))) {
        return -MR_EINVAL;
    }

    /* Route the input through the GPIO owner's analog configuration */
    gpio_config.mode = MR_GPIO_MODE_ANALOG;
    gpio_config.pull = MR_GPIO_PULL_NONE;
    ret = mr_gpio_configure(channel->gpio.drv, channel->gpio.pin, &gpio_config);
    if (ret != 0) {
        return ret;
    }

    /* Configure the regular channel */
    config.Channel = channel->channel;
    config.Rank = rank;
    config.SamplingTime = __adc_sample_time[sample_time];
    config.SingleDiff = ADC_SINGLE_ENDED;
    config.OffsetNumber = ADC_OFFSET_NONE;
    config.Offset = 0U;
    config.OffsetSign = ADC_OFFSET_SIGN_NEGATIVE;
    config.OffsetSaturation = DISABLE;
    return mr_stm32h5_hal_status(HAL_ADC_ConfigChannel(priv->handle, &config));
}

static mr_err_t adc_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_adc_config_t *config) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;
#if defined(MR_USE_DRIVER_ADC_STREAM)
    mr_bool_t busy;
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */

    /* Check parameter */
    if (channel >= priv->channels_num) {
        return -MR_EINVAL;
    }

#if defined(MR_USE_DRIVER_ADC_STREAM)
    /* Check stream state */
    ret = adc_stream_is_busy(drv, &busy);
    if (ret != 0) {
        return ret;
    }
    if (busy) {
        return -MR_EBUSY;
    }
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */

    /* Check configuration */
    if ((config->diff != MR_ADC_DIFF_SINGLE) || (config->ref != priv->ref)
        || (config->gain != MR_ADC_GAIN_1)) {
        return -MR_EINVAL;
    }

    /* Initialize ADC */
    ret = adc_init(priv, ADC_RESOLUTION_12B);
    if (ret != 0) {
        return ret;
    }
    return adc_configure_hw_channel(priv, &priv->channels[channel],
                                    MR_ARRAY_SIZE(__adc_sample_time) - 1U,
                                    ADC_REGULAR_RANK_1);
}

static mr_err_t adc_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_adc_config_t *config) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check parameter */
    if (channel >= priv->channels_num) {
        return -MR_EINVAL;
    }

    /* All channels use the same fixed input configuration */
    config->diff = MR_ADC_DIFF_SINGLE;
    config->ref = priv->ref;
    config->gain = MR_ADC_GAIN_1;
    return 0;
}

static mr_err_t adc_convert(adc_priv_t *priv, const adc_channel_t *channel,
                            mr_uint32_t sample_time, mr_uint16_t *value) {
    mr_uint16_t sample;
    mr_err_t ret;

    /* Configure channel */
    ret = adc_configure_hw_channel(priv, channel, sample_time,
                                   ADC_REGULAR_RANK_1);
    if (ret != 0) {
        return ret;
    }

    /* Start conversion */
    ret = mr_stm32h5_hal_status(HAL_ADC_Start(priv->handle));
    if (ret != 0) {
        return ret;
    }
    ret = mr_stm32h5_hal_status(
        HAL_ADC_PollForConversion(priv->handle, ADC_POLL_TIMEOUT_MS));
    if (ret != 0) {
        HAL_ADC_Stop(priv->handle);
        return ret;
    }

    /* Commit the sample only after the conversion has been stopped */
    sample = (mr_uint16_t)HAL_ADC_GetValue(priv->handle);
    ret = mr_stm32h5_hal_status(HAL_ADC_Stop(priv->handle));
    if (ret != 0) {
        return ret;
    }
    *value = sample;
    return 0;
}

static mr_ssize_t adc_read(const mr_driver_t *drv, mr_adc_seq_t *seq) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    uint32_t resolution;
    mr_uint32_t channel;
    mr_uint16_t *buf;
    mr_size_t count;
    mr_err_t ret;
#if defined(MR_USE_DRIVER_ADC_STREAM)
    mr_bool_t busy;
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */

#if defined(MR_USE_DRIVER_ADC_STREAM)
    /* Check stream state */
    ret = adc_stream_is_busy(drv, &busy);
    if (ret != 0) {
        return ret;
    }
    if (busy) {
        return -MR_EBUSY;
    }
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */

    /* Check sequence */
    ret = adc_resolution(seq->resolution, &resolution);
    if (ret != 0) {
        return ret;
    }
    if (seq->oversample >= MR_ARRAY_SIZE(__adc_sample_time)) {
        return -MR_EINVAL;
    }

    /* Check sequence buffer */
    if ((seq->channels == 0U) || (seq->size < sizeof(mr_uint16_t))) {
        return 0;
    }
    if (((mr_uintptr_t)seq->buf % sizeof(mr_uint16_t)) != 0U) {
        return -MR_EINVAL;
    }

    /* Init ADC for the requested resolution */
    ret = adc_init(priv, resolution);
    if (ret != 0) {
        return ret;
    }

    /* Read channels in bit order, including a partial final frame.
     * Return completed bytes if a later conversion fails */
    buf = seq->buf;
    count = 0U;
    while ((seq->size - count) >= sizeof(mr_uint16_t)) {
        for (channel = 0U; channel < ADC_CHANNEL_BITS; channel++) {
            if ((seq->channels & (1UL << channel)) == 0U) {
                continue;
            }
            if (channel >= priv->channels_num) {
                return (count != 0U) ? (mr_ssize_t)count : -MR_EINVAL;
            }
            ret = adc_convert(priv, &priv->channels[channel], seq->oversample,
                              buf);
            if (ret != 0) {
                return (count != 0U) ? (mr_ssize_t)count : ret;
            }
            buf++;
            count += sizeof(mr_uint16_t);
            if ((seq->size - count) < sizeof(mr_uint16_t)) {
                break;
            }
        }
    }
    return (mr_ssize_t)count;
}

#if defined(MR_USE_DRIVER_ADC_STREAM)
static void adc_stream_notify(adc_stream_t *stream, mr_wait_event_t events) {
    if (events == 0U) {
        return;
    }

    /* Forward only the events enabled by the caller */
    events &= (mr_wait_event_t)mr_atomic_load(&stream->event_state);
    if (events != 0U) {
        mr_adc_event_raise(stream->drv, events);
    }
}

static void adc_stream_halt(adc_priv_t *priv) {
    /* Disarm external triggers before requesting a nonblocking stop in IRQ */
    CLEAR_BIT(priv->handle->Instance->CFGR, ADC_CFGR_EXTEN);
    if (LL_ADC_REG_IsConversionOngoing(priv->handle->Instance)) {
        LL_ADC_REG_StopConversion(priv->handle->Instance);
    }
    mr_atomic_fetch_or(&priv->stream->event_state, ADC_STREAM_COMPLETE);
}

#if defined(MR_USE_DRIVER_DMA)
static void adc_dma_event(mr_waiter_t *waiter, void *param,
                          mr_wait_event_t event, const void *data) {
    mr_wait_event_t adc_event;
    adc_stream_t *stream;
    adc_priv_t *priv;

    priv = (adc_priv_t *)param;
    stream = priv->stream;

    MR_IGNORE(waiter);
    MR_IGNORE(data);

    /* Check stream state */
    if ((stream->drv == MR_NULL)
        || ((mr_atomic_load(&stream->event_state) & ADC_STREAM_COMPLETE)
            != 0U)) {
        return;
    }

    /* Translate DMA events and stop requests on error or one-shot completion */
    adc_event = 0U;
    if ((event & MR_DMA_WAIT_ERROR) != 0U) {
        adc_stream_halt(priv);
        adc_event = MR_ADC_WAIT_STREAM_ERROR;
    } else {
        if (((event & MR_DMA_WAIT_HALF) != 0U) && stream->half_enabled) {
            adc_event |= MR_ADC_WAIT_STREAM_HALF;
        }
        if ((event & MR_DMA_WAIT_DONE) != 0U) {
            if (!stream->circular) {
                adc_stream_halt(priv);
            }
            adc_event |= MR_ADC_WAIT_STREAM_FULL;
        }
    }
    adc_stream_notify(stream, adc_event);
}

static mr_wait_event_t adc_stream_dma_events(const adc_stream_t *stream) {
    return MR_DMA_WAIT_DONE | MR_DMA_WAIT_ERROR
         | (stream->half_enabled ? MR_DMA_WAIT_HALF : 0U);
}

static mr_err_t adc_stream_start_dma(adc_priv_t *priv) {
    static const mr_dma_config_t config = {
        .src_width = MR_DMA_WIDTH_16,
        .dst_width = MR_DMA_WIDTH_16,
        .src_adj = MR_DMA_ADJ_CONST,
        .dst_adj = MR_DMA_ADJ_INC,
        .src_burst = MR_DMA_BURST_1,
        .dst_burst = MR_DMA_BURST_1,
        .direction = MR_DMA_DIR_P2M,
    };
    mr_wait_event_t events;
    adc_stream_dma_t *dma;
    adc_stream_t *stream;
    mr_err_t ret;

    /* Configure DMA while ADC requests are masked */
    stream = priv->stream;
    dma = stream->dma;
    ret = mr_dma_configure(dma->spec.drv, dma->spec.channel, &config);
    if (ret != 0) {
        return ret;
    }

    /* Subscribe before starting DMA so completion and errors can halt ADC */
    events = adc_stream_dma_events(stream);
    ret = mr_waiter_init(&dma->waiter, adc_dma_event, priv, events);
    if (ret != 0) {
        return ret;
    }
    dma->phase = ADC_DMA_WAITER_READY;
    ret = mr_dma_event_attach(dma->spec.drv, dma->spec.channel, &dma->waiter);
    if (ret != 0) {
        return ret;
    }
    ret = mr_dma_event_enable(dma->spec.drv, dma->spec.channel, events);
    if (ret != 0) {
        return ret;
    }
    dma->phase = ADC_DMA_EVENTS_ENABLED;

    /* Start the transfer before unmasking ADC requests */
    dma->xfer.next = stream->circular ? &dma->xfer : MR_NULL;
    dma->xfer.src = (void *)&priv->handle->Instance->DR;
    dma->xfer.dst = stream->buf;
    dma->xfer.count = stream->size;
    ret = mr_dma_start(dma->spec.drv, dma->spec.channel, &dma->xfer);
    if (ret != 0) {
        return ret;
    }
    dma->phase = ADC_DMA_STARTED;
    SET_BIT(priv->handle->Instance->CFGR, ADC_CFGR_DMAEN);
    return 0;
}
#endif /* defined(MR_USE_DRIVER_DMA) */

static mr_err_t adc_seq_ranks(const adc_priv_t *priv, mr_uint32_t channels,
                              mr_uint8_t *index, mr_uint32_t *count) {
    mr_uint32_t channel, selected;

    /* Map selected channels to consecutive regular ranks */
    selected = 0U;
    for (channel = 0U; channel < ADC_CHANNEL_BITS; channel++) {
        if ((channels & (1UL << channel)) == 0U) {
            continue;
        }
        if ((channel >= priv->channels_num)
            || (selected >= MR_ARRAY_SIZE(__adc_regular_rank))) {
            return -MR_EINVAL;
        }
        index[selected] = (mr_uint8_t)channel;
        selected++;
    }
    if (selected == 0U) {
        return -MR_EINVAL;
    }
    *count = selected;
    return 0;
}

static mr_err_t adc_stream_stop(const mr_driver_t *drv) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    adc_stream_t *stream;
#if defined(MR_USE_DRIVER_DMA)
    adc_stream_dma_t *dma;
#endif /* defined(MR_USE_DRIVER_DMA) */
    mr_err_t ret;

    /* Check stream state */
    stream = priv->stream;
    if ((stream == MR_NULL) || (stream->drv == MR_NULL)) {
        return 0;
    }

    /* Stop conversions before releasing the destination buffer */
    if (stream->adc_started) {
        ret = mr_stm32h5_hal_status(stream->use_dma
                                        ? HAL_ADC_Stop(priv->handle)
                                        : HAL_ADC_Stop_IT(priv->handle));
        if (ret != 0) {
            return ret;
        }
        stream->adc_started = 0U;
    }

#if defined(MR_USE_DRIVER_DMA)
    /* Keep the waiter until DMA stops and its event sources are masked */
    dma = stream->dma;
    if (dma != MR_NULL) {
        if (dma->phase == ADC_DMA_STARTED) {
            ret = mr_dma_stop(dma->spec.drv, dma->spec.channel);
            if (ret != 0) {
                return ret;
            }
            dma->phase = ADC_DMA_EVENTS_ENABLED;
        }
        if (dma->phase == ADC_DMA_EVENTS_ENABLED) {
            ret = mr_dma_event_disable(dma->spec.drv, dma->spec.channel,
                                       adc_stream_dma_events(stream));
            if (ret != 0) {
                return ret;
            }
            dma->phase = ADC_DMA_WAITER_READY;
        }
        if (dma->phase == ADC_DMA_WAITER_READY) {
            mr_waiter_del(&dma->waiter);
            dma->phase = ADC_DMA_IDLE;
        }
    }
#endif /* defined(MR_USE_DRIVER_DMA) */

    /* Release stream ownership only after cleanup succeeds */
    CLEAR_BIT(priv->handle->Instance->CFGR, ADC_CFGR_DMAEN);
    stream->drv = MR_NULL;
    return 0;
}

static mr_err_t adc_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    adc_stream_t *stream;
#if defined(MR_USE_DRIVER_DMA)
    mr_bool_t dma_busy;
#endif /* defined(MR_USE_DRIVER_DMA) */
    mr_err_t ret;

    /* Check stream state */
    stream = priv->stream;
    if ((stream == MR_NULL) || (stream->drv == MR_NULL)) {
        *busy = MR_FALSE;
        return 0;
    }
    if ((mr_atomic_load(&stream->event_state) & ADC_STREAM_COMPLETE) != 0U) {
        ret = adc_stream_stop(drv);
        if (ret == 0) {
            *busy = MR_FALSE;
        }
        return ret;
    }

#if defined(MR_USE_DRIVER_DMA)
    /* Check DMA completion if the ADC has not reported it yet */
    if (stream->use_dma) {
        ret = mr_dma_is_busy(stream->dma->spec.drv, stream->dma->spec.channel,
                             &dma_busy);
        if (ret != 0) {
            return ret;
        }
        if (!dma_busy) {
            mr_atomic_fetch_or(&stream->event_state, ADC_STREAM_COMPLETE);
            ret = adc_stream_stop(drv);
            if (ret != 0) {
                return ret;
            }
        }
        *busy = dma_busy;
        return 0;
    }
#endif /* defined(MR_USE_DRIVER_DMA) */

    *busy = MR_TRUE;
    return 0;
}

static mr_err_t adc_stream_offset(const mr_driver_t *drv, mr_size_t *offset) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    adc_stream_t *stream;

    /* DMA owns the write position when it fills the buffer */
    stream = priv->stream;
    if (stream == MR_NULL) {
        *offset = 0U;
        return 0;
    }
#if defined(MR_USE_DRIVER_DMA)
    if (stream->use_dma) {
        return mr_dma_offset(stream->dma->spec.drv, stream->dma->spec.channel,
                             offset);
    }
#endif /* defined(MR_USE_DRIVER_DMA) */

    *offset = stream->offset;
    return 0;
}

static mr_err_t adc_stream_start(const mr_driver_t *drv, mr_adc_seq_t *seq,
                                 const mr_adc_stream_config_t *config) {
    mr_uint8_t ranks[MR_ARRAY_SIZE(__adc_regular_rank)];
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t channels_num, rank;
    mr_bool_t busy, use_dma;
    mr_size_t sample_count;
    adc_stream_t *stream;
    uint32_t resolution;
    mr_err_t ret;

    /* Check stream resources */
    stream = priv->stream;
    if (stream == MR_NULL) {
        return -MR_ENODEV;
    }
#if defined(MR_USE_DRIVER_DMA)
    use_dma = (stream->dma != MR_NULL) && (stream->dma->spec.drv != MR_NULL);
#else
    use_dma = MR_FALSE;
#endif /* defined(MR_USE_DRIVER_DMA) */
    if (!use_dma && !stream->irq_valid) {
        return -MR_ENODEV;
    }
    if ((config->source == MR_ADC_STREAM_SOURCE_EXTERNAL)
        && !stream->trigger_valid) {
        return -MR_ENODEV;
    }

    /* Check sequence and require a buffer of whole frames */
    if ((seq->buf == MR_NULL) || (seq->size == 0U)
        || ((seq->size % sizeof(mr_uint16_t)) != 0U)
        || (((mr_uintptr_t)seq->buf % sizeof(mr_uint16_t)) != 0U)) {
        return -MR_EINVAL;
    }
    ret = adc_resolution(seq->resolution, &resolution);
    if (ret != 0) {
        return ret;
    }
    if (seq->oversample >= MR_ARRAY_SIZE(__adc_sample_time)) {
        return -MR_EINVAL;
    }
    ret = adc_seq_ranks(priv, seq->channels, ranks, &channels_num);
    if (ret != 0) {
        return ret;
    }
    sample_count = seq->size / sizeof(mr_uint16_t);
    if ((sample_count < channels_num)
        || ((sample_count % channels_num) != 0U)) {
        return -MR_EINVAL;
    }

    /* Check stream state */
    ret = adc_stream_is_busy(drv, &busy);
    if (ret != 0) {
        return ret;
    }
    if (busy) {
        return -MR_EBUSY;
    }

    /* Configure the ADC and its channel ranks */
    ret = adc_init_stream(priv, resolution, channels_num, config, use_dma);
    if (ret != 0) {
        return ret;
    }
    for (rank = 0U; rank < channels_num; rank++) {
        ret = adc_configure_hw_channel(priv, &priv->channels[ranks[rank]],
                                       seq->oversample,
                                       __adc_regular_rank[rank]);
        if (ret != 0) {
            return ret;
        }
    }

    /* Publish the buffer and frame boundaries before enabling interrupts */
    stream->buf = seq->buf;
    stream->size = seq->size;
    stream->use_dma = use_dma;
    stream->circular = (config->circular != MR_FALSE);
    /* Report half completion only at a whole-frame boundary */
    stream->half_enabled = ((sample_count % (channels_num * 2U)) == 0U);
    stream->offset = 0U;
    mr_atomic_fetch_and(&stream->event_state,
                        ~(mr_atomic_t)ADC_STREAM_COMPLETE);
    stream->drv = (mr_driver_t *)drv;

    /* Prepare the data path before arming the ADC */
    CLEAR_BIT(priv->handle->Instance->CFGR, ADC_CFGR_DMAEN);
#if defined(MR_USE_DRIVER_DMA)
    if (use_dma) {
        ret = adc_stream_start_dma(priv);
        if (ret != 0) {
            goto _stop;
        }
    }
#endif /* defined(MR_USE_DRIVER_DMA) */
    if (!use_dma) {
        HAL_NVIC_SetPriority(stream->irqn, stream->irq_priority, 0U);
        HAL_NVIC_EnableIRQ(stream->irqn);
    }

    /* Stop must also handle an ADC start that only partly succeeded */
    stream->adc_started = 1U;
    ret = mr_stm32h5_hal_status(use_dma ? HAL_ADC_Start(priv->handle)
                                        : HAL_ADC_Start_IT(priv->handle));
    if (ret == 0) {
        return 0;
    }

_stop:
    /* Preserve the start error and leave failed cleanup available for retry */
    adc_stream_stop(drv);
    return ret;
}
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */

static mr_err_t adc_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable) {
#if defined(MR_USE_DRIVER_ADC_STREAM)
    const adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    adc_stream_t *stream;
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */

    /* Check parameter */
    if ((events == 0U) || ((events & ~ADC_EVENT_MASK) != 0U)) {
        return -MR_EINVAL;
    }
#if defined(MR_USE_DRIVER_ADC_STREAM)
    /* Keep required notifications enabled during an active stream */
    stream = priv->stream;
    if (stream == MR_NULL) {
        return 0;
    }
    if (!enable && (stream->drv != MR_NULL)) {
        if (((events & MR_ADC_WAIT_STREAM_ERROR) != 0U)
            || (((events & MR_ADC_WAIT_STREAM_FULL) != 0U)
                && !stream->circular)) {
            return -MR_EBUSY;
        }
    }
    /* Change notifications independently of the IRQs that fill the buffer */
    if (enable) {
        mr_atomic_fetch_or(&stream->event_state, (mr_atomic_t)events);
    } else {
        mr_atomic_fetch_and(&stream->event_state, ~(mr_atomic_t)events);
    }
#else
    MR_IGNORE(drv);
    MR_IGNORE(enable);
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */
    return 0;
}

#if defined(MR_USE_DRIVER_ADC_STREAM)
MR_UNUSED static void adc_irq(adc_priv_t *priv) {
    ADC_HandleTypeDef *handle;
    mr_wait_event_t event;
    adc_stream_t *stream;
    mr_uint32_t offset;

    /* Check stream state */
    stream = priv->stream;
    if ((stream == MR_NULL) || (stream->drv == MR_NULL) || stream->use_dma) {
        return;
    }

    /* An overrun invalidates the current frame */
    handle = priv->handle;
    if ((__HAL_ADC_GET_FLAG(handle, ADC_FLAG_OVR) != 0U)
        && (__HAL_ADC_GET_IT_SOURCE(handle, ADC_IT_OVR) != 0U)) {
        __HAL_ADC_CLEAR_FLAG(handle, ADC_FLAG_OVR);
        __HAL_ADC_DISABLE_IT(handle, ADC_IT_EOC | ADC_IT_EOS | ADC_IT_OVR);
        adc_stream_halt(priv);
        event = MR_ADC_WAIT_STREAM_ERROR;
    } else {
        if ((__HAL_ADC_GET_FLAG(handle, ADC_FLAG_EOC) == 0U)
            || (__HAL_ADC_GET_IT_SOURCE(handle, ADC_IT_EOC) == 0U)) {
            return;
        }

        /* Reading the data register acknowledges EOC */
        offset = stream->offset;
        stream->buf[offset / sizeof(mr_uint16_t)]
            = (mr_uint16_t)HAL_ADC_GetValue(handle);
        offset += sizeof(mr_uint16_t);
        stream->offset = offset;
        if (__HAL_ADC_GET_FLAG(handle, ADC_FLAG_EOS) != 0U) {
            __HAL_ADC_CLEAR_FLAG(handle, ADC_FLAG_EOS);
        }

        /* Stop a one-shot stream before notifying its subscribers */
        event = 0U;
        if (stream->half_enabled && (offset == (stream->size / 2U))) {
            event = MR_ADC_WAIT_STREAM_HALF;
        }
        if (offset == stream->size) {
            event = MR_ADC_WAIT_STREAM_FULL;
            if (!stream->circular) {
                __HAL_ADC_DISABLE_IT(handle,
                                     ADC_IT_EOC | ADC_IT_EOS | ADC_IT_OVR);
                adc_stream_halt(priv);
            }
        }
    }

    /* Notify subscribers while the completed byte count is still visible */
    adc_stream_notify(stream, event);
    if ((event == MR_ADC_WAIT_STREAM_FULL) && stream->circular) {
        stream->offset = 0U;
    }
}
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */

/* Instance install */
#define ST_HT_ADC_INSTALL(_id, _ops)                                           \
    static void MR_CAT(__adc_clk_enable_, _id)(void) {                         \
        MR_HT_DRIVER_PROP(_id, st_clken)();                                    \
    }                                                                          \
    static ADC_HandleTypeDef MR_CAT(__adc_handle_, _id) = {                    \
        .Instance = MR_HT_DRIVER_PROP(_id, st_adcx),                           \
    };                                                                         \
    static const adc_channel_t MR_CAT(__adc_channel_, _id)[]                   \
        = {MR_HT_DRIVER_PROP_ELEM_FOREACH(_id, st_channels,                    \
                                          __ST_HT_ADC_CHANNEL_IMPL)};          \
    __ST_HT_ADC_STREAM_INSTALL(_id)                                            \
    static adc_priv_t MR_CAT(__adc_priv_, _id)                                 \
        = {.handle = &MR_CAT(__adc_handle_, _id),                              \
           .clk_enable = MR_CAT(__adc_clk_enable_, _id),                       \
           .channels = MR_CAT(__adc_channel_, _id),                            \
           .channels_num = MR_ARRAY_SIZE(MR_CAT(__adc_channel_, _id)),         \
           .ref = MR_HT_DRIVER_PROP(_id, st_ref),                              \
           __ST_HT_ADC_STREAM(_id)};                                           \
    __ST_HT_ADC_IRQ(_id)                                                       \
    MR_HT_ADC_INSTALL(_id, &MR_CAT(__adc_priv_, _id), _ops)
#define __ST_HT_ADC_CHANNEL_IMPL(_elem, _node, _prop)                          \
    {                                                                          \
        .channel = MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 0),               \
        .gpio = {                                                              \
            .drv =                                                             \
                MR_HT_DRIVER_FIND(                                             \
                    MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 1)),             \
            .pin = (mr_uint8_t)MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 2),   \
        },                                                                     \
    }
#if defined(MR_USE_DRIVER_ADC_STREAM)
#if defined(MR_USE_DRIVER_DMA)
#define __ST_HT_ADC_HAS_DMA(_id) MR_HT_DRIVER_HAS_PROP(_id, st_dma)
#define __ST_HT_ADC_DMA_INSTALL(_id)                                           \
    MR_MACRO_IF(__ST_HT_ADC_HAS_DMA(_id),                                      \
                (static adc_stream_dma_t MR_CAT(__adc_dma_, _id) = {           \
                    .spec = MR_HT_DMA_SPEC(_id, st_dma),                       \
                };),                                                           \
                ())
#define __ST_HT_ADC_DMA(_id)                                                   \
    MR_MACRO_IF(__ST_HT_ADC_HAS_DMA(_id), (.dma = &MR_CAT(__adc_dma_, _id), ), \
                ())
#else
#define __ST_HT_ADC_HAS_DMA(_id)        0
#define __ST_HT_ADC_DMA_INSTALL(_id)
#define __ST_HT_ADC_DMA(_id)
#endif /* defined(MR_USE_DRIVER_DMA) */
#define __ST_HT_ADC_HAS_IRQ(_id)                                               \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, st_irqn),                           \
                (MR_HT_DRIVER_HAS_PROP(_id, st_irq_handler)), (0))
#define __ST_HT_ADC_HAS_STREAM(_id)                                            \
    MR_MACRO_IF(__ST_HT_ADC_HAS_DMA(_id), (1), (__ST_HT_ADC_HAS_IRQ(_id)))
#define __ST_HT_ADC_IRQ(_id)                                                   \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, st_irq_handler),                    \
                (void MR_HT_DRIVER_PROP(_id, st_irq_handler)(void) {           \
                    adc_irq(&MR_CAT(__adc_priv_, _id));                        \
                }),                                                            \
                ())
#define __ST_HT_ADC_STREAM_INSTALL(_id)                                        \
    __ST_HT_ADC_DMA_INSTALL(_id)                                               \
    MR_MACRO_IF(__ST_HT_ADC_HAS_STREAM(_id),                                   \
                (static adc_stream_t MR_CAT(__adc_stream_, _id) = {            \
                    .trigger = MR_HT_DRIVER_PROP_OR(_id, st_trigger, 0U),      \
                    .trigger_edge =                                            \
                        MR_HT_DRIVER_PROP_OR(_id, st_trigger_edge,             \
                                             ADC_EXTERNALTRIGCONVEDGE_RISING), \
                    .irqn = MR_HT_DRIVER_PROP_OR(_id, st_irqn, (IRQn_Type)0),  \
                    .irq_priority =                                            \
                        MR_HT_DRIVER_PROP_OR(_id, st_irq_priority, 0U),        \
                    __ST_HT_ADC_DMA(_id)                                       \
                    .event_state = MR_ATOMIC_INIT(0),                          \
                    .irq_valid = __ST_HT_ADC_HAS_IRQ(_id),                     \
                    .trigger_valid = MR_HT_DRIVER_HAS_PROP(_id, st_trigger),   \
                };),                                                           \
                ())
#define __ST_HT_ADC_STREAM(_id)                                                \
    MR_MACRO_IF(__ST_HT_ADC_HAS_STREAM(_id),                                   \
                (.stream = &MR_CAT(__adc_stream_, _id), ), ())
#else
#define __ST_HT_ADC_IRQ(_id)
#define __ST_HT_ADC_STREAM_INSTALL(_id)
#define __ST_HT_ADC_STREAM(_id)
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */
MR_HT_DRIVER_FOREACH(ST_HT_ADC_INSTALL, &__adc_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_ADC) */

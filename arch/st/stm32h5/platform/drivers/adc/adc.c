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
#if defined(MR_USE_DRIVER_ADC_STREAM)
/* Enable ADC stream operations when configured */
#define ADC_USE_STREAM
#if defined(MR_USE_DRIVER_DMA)
/* Enable DMA as an ADC stream data path when available */
#define ADC_USE_DMA
#include <mrX/drivers/dma/mr_dma.h>
#endif /* defined(MR_USE_DRIVER_DMA) */
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */
#include "stm32h5xx_hal_adc.h"
#include "stm32h5xx_hal_adc_ex.h"

/* HardTree compatible */
#define MR_HT_COMPAT st_stm32h5_adc
#if MR_HT_HAS_DRIVER()

/* ADC operations */
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
#endif /* defined(ADC_USE_STREAM) */
static mr_err_t adc_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable);
MR_UNUSED static const mr_adc_ops_t __adc_ops = {
    .channel_configure = adc_channel_configure,
    .channel_configuration = adc_channel_configuration,
    .read = adc_read,
#if defined(ADC_USE_STREAM)
    .stream_start = adc_stream_start,
    .stream_stop = adc_stream_stop,
    .stream_is_busy = adc_stream_is_busy,
    .stream_offset = adc_stream_offset,
#endif /* defined(ADC_USE_STREAM) */
    .event_control = adc_event_control,
};

/* ADC channel */
typedef struct adc_channel {
    uint32_t channel;
    mr_ht_gpio_spec_t gpio;
    mr_adc_config_t config;
} adc_channel_t;

#if defined(ADC_USE_STREAM)
/* ADC stream */
typedef struct adc_stream {
#if defined(ADC_USE_DMA)
    mr_ht_dma_spec_t dma;
    mr_dma_transfer_t xfer;
    mr_waiter_t waiter;
#endif /* defined(ADC_USE_DMA) */
    mr_driver_t *drv;
    mr_uint16_t *buf;
    mr_size_t size;
    mr_size_t offset;
    uint32_t trigger;
    uint32_t trigger_edge;
    IRQn_Type irqn;
    mr_uint32_t irq_priority;
    mr_atomic_t active;
    mr_atomic_t complete;
    mr_bool_t irq_valid;
    mr_bool_t trigger_valid;
    mr_bool_t use_dma;
    mr_bool_t dma_started;
    mr_bool_t waiter_inited;
    mr_bool_t half_enabled;
    mr_bool_t circular;
} adc_stream_t;
#endif /* defined(ADC_USE_STREAM) */

/* ADC private data */
typedef struct adc_priv {
    ADC_HandleTypeDef *handle;
    void (*clk_enable)(void);
    adc_channel_t *channels;
    mr_uint32_t channels_num;
    mr_uint32_t event_mask;
    mr_uint8_t ref;
#if defined(ADC_USE_STREAM)
    adc_stream_t stream;
#endif /* defined(ADC_USE_STREAM) */
} adc_priv_t;

/* Conversion timeout */
#define ADC_POLL_TIMEOUT_MS (100U)
/* Supported events */
#define ADC_EVENT_MASK                                                         \
    (MR_ADC_WAIT_STREAM_HALF | MR_ADC_WAIT_STREAM_FULL                         \
     | MR_ADC_WAIT_STREAM_ERROR)

/* Sample-time translation */
static const uint32_t __adc_sample_time[] = {
    ADC_SAMPLETIME_2CYCLES_5,   ADC_SAMPLETIME_6CYCLES_5,
    ADC_SAMPLETIME_12CYCLES_5,  ADC_SAMPLETIME_24CYCLES_5,
    ADC_SAMPLETIME_47CYCLES_5,  ADC_SAMPLETIME_92CYCLES_5,
    ADC_SAMPLETIME_247CYCLES_5, ADC_SAMPLETIME_640CYCLES_5,
};

#if defined(ADC_USE_STREAM)
/* Stream rank translation */
static const uint32_t __adc_regular_rank[] = {
    ADC_REGULAR_RANK_1, ADC_REGULAR_RANK_2, ADC_REGULAR_RANK_3,
    ADC_REGULAR_RANK_4, ADC_REGULAR_RANK_5, ADC_REGULAR_RANK_6,
    ADC_REGULAR_RANK_7, ADC_REGULAR_RANK_8,
};
#endif /* defined(ADC_USE_STREAM) */

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
                              uint32_t scan_conv_mode,
                              FunctionalState continuous_conv,
                              FunctionalState dma_requests,
                              uint32_t conversions, uint32_t trigger,
                              uint32_t trigger_edge) {
    ADC_HandleTypeDef *handle;
    mr_err_t ret;

    handle = priv->handle;
    /* Check parameter */
    if ((handle == MR_NULL) || (priv->clk_enable == MR_NULL)) {
        return -MR_EINVAL;
    }
    priv->clk_enable();

    /* Configure conversion mode */
    handle->Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    handle->Init.Resolution = resolution;
    handle->Init.DataAlign = ADC_DATAALIGN_RIGHT;
    handle->Init.ScanConvMode = scan_conv_mode;
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
    return adc_init_mode(priv, resolution, ADC_SCAN_DISABLE, DISABLE, DISABLE,
                         1U, ADC_SOFTWARE_START, ADC_EXTERNALTRIGCONVEDGE_NONE);
}

#if defined(ADC_USE_STREAM)
static mr_err_t adc_init_stream(adc_priv_t *priv, uint32_t resolution,
                                mr_uint32_t conversions,
                                const mr_adc_stream_config_t *config,
                                mr_bool_t use_dma) {
    mr_bool_t software;

    software = (config->source == MR_ADC_STREAM_SOURCE_SOFTWARE);

    return adc_init_mode(
        priv, resolution,
        (conversions > 1U) ? ADC_SCAN_ENABLE : ADC_SCAN_DISABLE,
        software ? ENABLE : DISABLE, use_dma ? ENABLE : DISABLE, conversions,
        software ? ADC_SOFTWARE_START : priv->stream.trigger,
        software ? ADC_EXTERNALTRIGCONVEDGE_NONE : priv->stream.trigger_edge);
}
#endif /* defined(ADC_USE_STREAM) */

static mr_err_t adc_gpio_configure(adc_channel_t *channel) {
    mr_gpio_config_t cfg;

    cfg = (mr_gpio_config_t){0};

    /* Check GPIO */
    if (channel->gpio.drv == MR_NULL) {
        return -MR_EINVAL;
    }

    /* Configure GPIO */
    cfg.mode = MR_GPIO_MODE_ANALOG;
    cfg.pull = MR_GPIO_PULL_NONE;
    return mr_ht_gpio_configure(&channel->gpio, &cfg);
}

static mr_err_t adc_configure_hw_channel(adc_priv_t *priv,
                                         const adc_channel_t *channel,
                                         mr_uint32_t sample_time,
                                         uint32_t rank) {
    ADC_ChannelConfTypeDef config;

    config = (ADC_ChannelConfTypeDef){0};

    /* Check sample time */
    if (sample_time >= MR_ARRAY_SIZE(__adc_sample_time)) {
        return -MR_EINVAL;
    }

    /* Configure channel */
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
    adc_channel_t *adc_channel;
    mr_err_t ret;
#if defined(ADC_USE_STREAM)
    mr_bool_t busy;
#endif /* defined(ADC_USE_STREAM) */

    /* Check parameter */
    if (channel >= priv->channels_num) {
        return -MR_EINVAL;
    }
    adc_channel = &priv->channels[channel];

#if defined(ADC_USE_STREAM)
    /* Check stream state */
    ret = adc_stream_is_busy(drv, &busy);
    if (ret != 0) {
        return ret;
    }
    if (busy) {
        return -MR_EBUSY;
    }
#endif /* defined(ADC_USE_STREAM) */

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
    ret = adc_gpio_configure(adc_channel);
    if (ret != 0) {
        return ret;
    }

    /* Configure channel */
    ret = adc_configure_hw_channel(priv, adc_channel,
                                   MR_ARRAY_SIZE(__adc_sample_time) - 1U,
                                   ADC_REGULAR_RANK_1);
    if (ret != 0) {
        return ret;
    }
    adc_channel->config = *config;
    return 0;
}

static mr_err_t adc_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_adc_config_t *config) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check parameter */
    if (channel >= priv->channels_num) {
        return -MR_EINVAL;
    }

    /* Get configuration */
    *config = priv->channels[channel].config;
    return 0;
}

static mr_err_t adc_convert(adc_priv_t *priv, adc_channel_t *channel,
                            mr_uint32_t sample_time, mr_uint16_t *value) {
    mr_err_t ret;

    /* Configure channel */
    ret = adc_gpio_configure(channel);
    if (ret != 0) {
        return ret;
    }
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
        (void)HAL_ADC_Stop(priv->handle);
        return ret;
    }

    /* Read conversion result */
    *value = (mr_uint16_t)HAL_ADC_GetValue(priv->handle);
    return mr_stm32h5_hal_status(HAL_ADC_Stop(priv->handle));
}

static mr_ssize_t adc_read(const mr_driver_t *drv, mr_adc_seq_t *seq) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t channel, resolution;
    mr_uint16_t *buf;
    mr_size_t count;
    mr_err_t ret;
#if defined(ADC_USE_STREAM)
    mr_bool_t busy;
#endif /* defined(ADC_USE_STREAM) */

    count = 0U;

#if defined(ADC_USE_STREAM)
    /* Check stream state */
    ret = adc_stream_is_busy(drv, &busy);
    if (ret != 0) {
        return ret;
    }
    if (busy) {
        return -MR_EBUSY;
    }
#endif /* defined(ADC_USE_STREAM) */

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

    ret = adc_init(priv, resolution);
    if (ret != 0) {
        return ret;
    }

    /* Read channels */
    buf = (mr_uint16_t *)seq->buf;
    while ((seq->size - count) >= sizeof(mr_uint16_t)) {
        for (channel = 0U; channel < 32U; channel++) {
            adc_channel_t *adc_channel;

            if ((seq->channels & (1UL << channel)) == 0U) {
                continue;
            }
            if (channel >= priv->channels_num) {
                return (count != 0U) ? (mr_ssize_t)count : -MR_EINVAL;
            }
            adc_channel = &priv->channels[channel];

            ret = adc_convert(priv, adc_channel, seq->oversample, buf);
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

#if defined(ADC_USE_STREAM)
static void adc_stream_halt(adc_priv_t *priv) {
    /* Halt conversion */
    CLEAR_BIT(priv->handle->Instance->CFGR, ADC_CFGR_EXTEN);
    if (LL_ADC_REG_IsConversionOngoing(priv->handle->Instance)) {
        LL_ADC_REG_StopConversion(priv->handle->Instance);
    }
}

#if defined(ADC_USE_DMA)
static void adc_dma_event(mr_waiter_t *waiter, void *param,
                          mr_wait_event_t event, const void *data) {
    mr_wait_event_t adc_event;
    adc_priv_t *priv;

    priv = (adc_priv_t *)param;

    adc_event = 0U;

    MR_IGNORE(waiter);
    MR_IGNORE(data);

    /* Handle DMA events */
    if ((mr_atomic_load(&priv->stream.active) == 0)
        || (priv->stream.drv == MR_NULL)) {
        return;
    }
    if (event & MR_DMA_WAIT_ERROR) {
        /* Stop ADC */
        adc_stream_halt(priv);
        mr_atomic_store(&priv->stream.complete, 1);
        if (priv->event_mask & MR_ADC_WAIT_STREAM_ERROR) {
            (void)mr_adc_event_raise(priv->stream.drv,
                                     MR_ADC_WAIT_STREAM_ERROR);
        }
        return;
    }
    if ((event & MR_DMA_WAIT_HALF) && priv->stream.half_enabled
        && (priv->event_mask & MR_ADC_WAIT_STREAM_HALF)) {
        adc_event |= MR_ADC_WAIT_STREAM_HALF;
    }
    if (event & MR_DMA_WAIT_DONE) {
        if (priv->event_mask & MR_ADC_WAIT_STREAM_FULL) {
            adc_event |= MR_ADC_WAIT_STREAM_FULL;
        }
        if (!priv->stream.circular) {
            /* Mark stream complete */
            adc_stream_halt(priv);
            mr_atomic_store(&priv->stream.complete, 1);
        }
    }
    if (adc_event != 0U) {
        (void)mr_adc_event_raise(priv->stream.drv, adc_event);
    }
}
#endif /* defined(ADC_USE_DMA) */

static void adc_stream_release(adc_priv_t *priv) {
#if defined(ADC_USE_DMA)
    if (priv->stream.waiter_inited) {
        (void)mr_waiter_del(&priv->stream.waiter);
        priv->stream.waiter_inited = MR_FALSE;
    }
#endif /* defined(ADC_USE_DMA) */
    /* Release stream */
    priv->stream.drv = MR_NULL;
}

static mr_err_t adc_seq_ranks(const adc_priv_t *priv, mr_uint32_t channels,
                              mr_uint8_t *index, mr_uint32_t *count) {
    mr_uint32_t channel, selected;

    selected = 0U;

    for (channel = 0U; channel < 32U; channel++) {
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
    /* Get channel ranks */
    *count = selected;
    return 0;
}

static mr_err_t adc_stream_stop_impl(const mr_driver_t *drv) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t first_error, ret;

    first_error = 0;
    if (mr_atomic_load(&priv->stream.active) != 0) {
#if defined(ADC_USE_DMA)
        if (priv->stream.use_dma && priv->stream.dma_started) {
            /* Stop DMA */
            if (priv->stream.dma.drv == MR_NULL) {
                first_error = -MR_ENODEV;
            } else {
                ret = mr_dma_stop(priv->stream.dma.drv,
                                  priv->stream.dma.channel);
                if ((ret != 0) && (first_error == 0)) {
                    first_error = ret;
                }
            }
        }
#endif /* defined(ADC_USE_DMA) */

        /* Stop ADC */
        ret = mr_stm32h5_hal_status(priv->stream.use_dma
                                        ? HAL_ADC_Stop(priv->handle)
                                        : HAL_ADC_Stop_IT(priv->handle));
        if ((ret != 0) && (first_error == 0)) {
            first_error = ret;
        }

        /* Check cleanup result */
        if (first_error != 0) {
            return first_error;
        }
        mr_atomic_store(&priv->stream.active, 0);
        priv->stream.dma_started = MR_FALSE;
        CLEAR_BIT(priv->handle->Instance->CFGR, ADC_CFGR_DMAEN);
    }
    adc_stream_release(priv);
    return first_error;
}

static mr_err_t adc_stream_is_busy_impl(const mr_driver_t *drv,
                                        mr_bool_t *busy) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check stream state */
    if (mr_atomic_load(&priv->stream.active) == 0) {
        *busy = MR_FALSE;
        return 0;
    }
    if (mr_atomic_load(&priv->stream.complete) != 0) {
        *busy = MR_FALSE;
        return adc_stream_stop(drv);
    }

#if defined(ADC_USE_DMA)
    if (priv->stream.use_dma) {
        mr_err_t ret;

        if (priv->stream.dma.drv == MR_NULL) {
            return -MR_ENODEV;
        }
        ret = mr_dma_is_busy(priv->stream.dma.drv, priv->stream.dma.channel,
                             busy);
        if (ret != 0) {
            return ret;
        }
        if (!*busy) {
            mr_atomic_store(&priv->stream.complete, 1);
            return adc_stream_stop(drv);
        }
        return 0;
    }
#endif /* defined(ADC_USE_DMA) */

    *busy = MR_TRUE;
    return 0;
}

static mr_err_t adc_stream_offset_impl(const mr_driver_t *drv,
                                       mr_size_t *offset) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Get stream offset */
#if defined(ADC_USE_DMA)
    if (priv->stream.use_dma) {
        if (priv->stream.dma.drv == MR_NULL) {
            return -MR_ENODEV;
        }
        return mr_dma_offset(priv->stream.dma.drv, priv->stream.dma.channel,
                             offset);
    }
#endif /* defined(ADC_USE_DMA) */

    *offset = priv->stream.offset;
    return 0;
}

static void adc_irq(adc_priv_t *priv) {
    mr_wait_event_t event;

    event = 0U;

    /* Check stream state */
    if ((priv == MR_NULL) || (mr_atomic_load(&priv->stream.active) == 0)
        || priv->stream.use_dma) {
        return;
    }

    /* Check overrun */
    if ((__HAL_ADC_GET_FLAG(priv->handle, ADC_FLAG_OVR) != 0U)
        && (__HAL_ADC_GET_IT_SOURCE(priv->handle, ADC_IT_OVR) != 0U)) {
        __HAL_ADC_CLEAR_FLAG(priv->handle, ADC_FLAG_OVR);
        __HAL_ADC_DISABLE_IT(priv->handle,
                             ADC_IT_EOC | ADC_IT_EOS | ADC_IT_OVR);
        adc_stream_halt(priv);
        mr_atomic_store(&priv->stream.complete, 1);
        if (priv->event_mask & MR_ADC_WAIT_STREAM_ERROR) {
            (void)mr_adc_event_raise(priv->stream.drv,
                                     MR_ADC_WAIT_STREAM_ERROR);
        }
        return;
    }

    /* Read conversion result */
    if ((__HAL_ADC_GET_FLAG(priv->handle, ADC_FLAG_EOC) == 0U)
        || (__HAL_ADC_GET_IT_SOURCE(priv->handle, ADC_IT_EOC) == 0U)) {
        return;
    }

    priv->stream.buf[priv->stream.offset / sizeof(mr_uint16_t)]
        = (mr_uint16_t)HAL_ADC_GetValue(priv->handle);
    priv->stream.offset += sizeof(mr_uint16_t);
    if (__HAL_ADC_GET_FLAG(priv->handle, ADC_FLAG_EOS) != 0U) {
        __HAL_ADC_CLEAR_FLAG(priv->handle, ADC_FLAG_EOS);
    }

    if (priv->stream.half_enabled
        && (priv->stream.offset == (priv->stream.size / 2U))
        && (priv->event_mask & MR_ADC_WAIT_STREAM_HALF)) {
        event |= MR_ADC_WAIT_STREAM_HALF;
    }
    if (priv->stream.offset == priv->stream.size) {
        /* Raise full event */
        if (priv->event_mask & MR_ADC_WAIT_STREAM_FULL) {
            event |= MR_ADC_WAIT_STREAM_FULL;
        }
        if (priv->stream.circular) {
            /* Raise event before resetting offset */
            if (event != 0U) {
                (void)mr_adc_event_raise(priv->stream.drv, event);
            }
            priv->stream.offset = 0U;
            return;
        }

        __HAL_ADC_DISABLE_IT(priv->handle,
                             ADC_IT_EOC | ADC_IT_EOS | ADC_IT_OVR);
        adc_stream_halt(priv);
        mr_atomic_store(&priv->stream.complete, 1);
    }
    if (event != 0U) {
        (void)mr_adc_event_raise(priv->stream.drv, event);
    }
}

static mr_err_t adc_stream_start_impl(const mr_driver_t *drv, mr_adc_seq_t *seq,
                                      const mr_adc_stream_config_t *config) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
#if defined(ADC_USE_DMA)
    mr_dma_config_t dma_config;
    mr_wait_event_t dma_events;
#endif /* defined(ADC_USE_DMA) */
    mr_uint8_t ranks[MR_ARRAY_SIZE(__adc_regular_rank)];
    mr_uint32_t channels_num, resolution, rank;
    mr_bool_t busy, use_dma;
    mr_size_t sample_count;
    mr_err_t ret;

    use_dma = MR_FALSE;
#if defined(ADC_USE_DMA)
    dma_config = (mr_dma_config_t){
        .src_width = MR_DMA_WIDTH_16,
        .dst_width = MR_DMA_WIDTH_16,
        .src_adj = MR_DMA_ADJ_CONST,
        .dst_adj = MR_DMA_ADJ_INC,
        .src_burst = MR_DMA_BURST_1,
        .dst_burst = MR_DMA_BURST_1,
        .direction = MR_DMA_DIR_P2M,
    };
#endif /* defined(ADC_USE_DMA) */
#if defined(ADC_USE_DMA)
    use_dma = (priv->stream.dma.drv != MR_NULL);
#endif /* defined(ADC_USE_DMA) */

    /* Check stream parameters */
    if (!use_dma && !priv->stream.irq_valid) {
        return -MR_ENODEV;
    }
    if ((config->source == MR_ADC_STREAM_SOURCE_EXTERNAL)
        && !priv->stream.trigger_valid) {
        return -MR_ENODEV;
    }
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
    if (mr_atomic_load(&priv->stream.active) != 0) {
        ret = adc_stream_stop(drv);
        if (ret != 0) {
            return ret;
        }
    }

    /* Configure channels */
    ret = adc_init_stream(priv, resolution, channels_num, config, use_dma);
    if (ret != 0) {
        return ret;
    }
    for (rank = 0U; rank < channels_num; rank++) {
        adc_channel_t *adc_channel;

        adc_channel = &priv->channels[ranks[rank]];

        ret = adc_gpio_configure(adc_channel);
        if (ret != 0) {
            return ret;
        }
        ret = adc_configure_hw_channel(priv, adc_channel, seq->oversample,
                                       __adc_regular_rank[rank]);
        if (ret != 0) {
            return ret;
        }
    }

    /* Save stream state */
    priv->stream.drv = (mr_driver_t *)drv;
    priv->stream.buf = seq->buf;
    priv->stream.size = seq->size;
    priv->stream.offset = 0U;
    priv->stream.use_dma = use_dma;
    priv->stream.dma_started = MR_FALSE;
    priv->stream.waiter_inited = MR_FALSE;
    priv->stream.circular = (config->circular != MR_FALSE);
    priv->stream.half_enabled
        = ((sample_count >= (channels_num * 2U))
           && ((sample_count % (channels_num * 2U)) == 0U));
    mr_atomic_store(&priv->stream.complete, 0);

#if defined(ADC_USE_DMA)
    if (use_dma) {
        /* Configure DMA */
        CLEAR_BIT(priv->handle->Instance->CFGR, ADC_CFGR_DMAEN);
        ret = mr_dma_configure(priv->stream.dma.drv, priv->stream.dma.channel,
                               &dma_config);
        if (ret != 0) {
            adc_stream_release(priv);
            return ret;
        }

        dma_events = MR_DMA_WAIT_DONE | MR_DMA_WAIT_ERROR;
        if (priv->stream.half_enabled) {
            dma_events |= MR_DMA_WAIT_HALF;
        }
        ret = mr_waiter_init(&priv->stream.waiter, adc_dma_event, priv,
                             dma_events);
        if (ret != 0) {
            adc_stream_release(priv);
            return ret;
        }
        priv->stream.waiter_inited = MR_TRUE;
        ret = mr_dma_event_attach(priv->stream.dma.drv,
                                  priv->stream.dma.channel,
                                  &priv->stream.waiter);
        if (ret != 0) {
            adc_stream_release(priv);
            return ret;
        }
        ret = mr_dma_event_enable(priv->stream.dma.drv,
                                  priv->stream.dma.channel, dma_events);
        if (ret != 0) {
            (void)adc_stream_stop(drv);
            return ret;
        }

        priv->stream.xfer.next
            = config->circular ? &priv->stream.xfer : MR_NULL;
        priv->stream.xfer.src = (void *)&priv->handle->Instance->DR;
        priv->stream.xfer.dst = seq->buf;
        priv->stream.xfer.count = seq->size;
        ret = mr_dma_start(priv->stream.dma.drv, priv->stream.dma.channel,
                           &priv->stream.xfer);
        if (ret != 0) {
            (void)adc_stream_stop(drv);
            return ret;
        }
        priv->stream.dma_started = MR_TRUE;
        SET_BIT(priv->handle->Instance->CFGR, ADC_CFGR_DMAEN);
    } else
#endif /* defined(ADC_USE_DMA) */
    {
        CLEAR_BIT(priv->handle->Instance->CFGR, ADC_CFGR_DMAEN);
        HAL_NVIC_SetPriority(priv->stream.irqn, priv->stream.irq_priority, 0U);
        HAL_NVIC_EnableIRQ(priv->stream.irqn);
    }

    /* Start stream */
    mr_atomic_store(&priv->stream.active, 1);

    /* Start ADC */
    ret = mr_stm32h5_hal_status(use_dma ? HAL_ADC_Start(priv->handle)
                                        : HAL_ADC_Start_IT(priv->handle));
    if (ret != 0) {
        (void)adc_stream_stop(drv);
        return ret;
    }
    return 0;
}
#endif /* defined(ADC_USE_STREAM) */

#if defined(ADC_USE_STREAM)
static mr_err_t adc_stream_start(const mr_driver_t *drv, mr_adc_seq_t *seq,
                                 const mr_adc_stream_config_t *config) {
    return adc_stream_start_impl(drv, seq, config);
}

static mr_err_t adc_stream_stop(const mr_driver_t *drv) {
    return adc_stream_stop_impl(drv);
}

static mr_err_t adc_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    return adc_stream_is_busy_impl(drv, busy);
}

static mr_err_t adc_stream_offset(const mr_driver_t *drv, mr_size_t *offset) {
    return adc_stream_offset_impl(drv, offset);
}
#endif /* defined(ADC_USE_STREAM) */

static mr_err_t adc_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check parameter */
    if ((events == 0U) || ((events & ~ADC_EVENT_MASK) != 0U)) {
        return -MR_EINVAL;
    }
#if defined(ADC_USE_STREAM)
    if (!enable && (mr_atomic_load(&priv->stream.active) != 0)) {
        if ((events & MR_ADC_WAIT_STREAM_ERROR) != 0U) {
            return -MR_EBUSY;
        }
        if (((events & MR_ADC_WAIT_STREAM_FULL) != 0U)
            && !priv->stream.circular) {
            return -MR_EBUSY;
        }
    }
#endif /* defined(ADC_USE_STREAM) */
    /* Update event mask */
    if (enable) {
        priv->event_mask |= events;
    } else {
        priv->event_mask &= ~events;
    }
    return 0;
}

/* ADC channel mapping */
#define __ST_HT_ADC_CHANNEL_IMPL(_elem, _node, _prop)                          \
    {                                                                          \
        .channel = MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 0),               \
        .gpio                                                                  \
        = {.drv                                                                \
           = MR_HT_DRIVER_FIND(MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 1)),  \
           .pin = (mr_uint8_t)MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 2)},   \
        .config = {.diff = MR_ADC_DIFF_SINGLE,                                 \
           .ref = MR_HT_NODE_PROP(_node, st_ref),                              \
           .gain = MR_ADC_GAIN_1},                                             \
}
#if defined(ADC_USE_STREAM)
#if defined(ADC_USE_DMA)
/* ADC DMA mapping */
#define __ST_HT_ADC_DMA(_id)                                                   \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, st_dma),                            \
                (.dma = MR_HT_DMA_SPEC(_id, st_dma)), (.dma = {0})),
#else
#define __ST_HT_ADC_DMA(_id)
#endif /* defined(ADC_USE_DMA) */
/* ADC IRQ handler */
#define __ST_HT_ADC_IRQ(_id)                                                   \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, st_irq_handler),                    \
                (void MR_HT_DRIVER_PROP(_id, st_irq_handler)(void) {           \
                    adc_irq(&MR_CAT(__adc_priv_, _id));                        \
                }),                                                            \
                ())
/* ADC stream mapping */
#define __ST_HT_ADC_STREAM(_id)                                                \
    .stream                                                                    \
        = {__ST_HT_ADC_DMA(_id)                                                \
               MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, st_trigger),             \
                           (.trigger = MR_HT_DRIVER_PROP(_id, st_trigger)),    \
                           (.trigger = 0)),                                    \
           .trigger_edge = MR_HT_DRIVER_PROP_OR(                               \
               _id, st_trigger_edge, ADC_EXTERNALTRIGCONVEDGE_RISING),         \
           .irqn = MR_HT_DRIVER_PROP_OR(_id, st_irqn, (IRQn_Type)0),           \
           .irq_priority = MR_HT_DRIVER_PROP_OR(_id, st_irq_priority, 0U),     \
           .active = MR_ATOMIC_INIT(0),                                        \
           .complete = MR_ATOMIC_INIT(0),                                      \
           .irq_valid = (MR_HT_DRIVER_HAS_PROP(_id, st_irqn)                   \
                         && MR_HT_DRIVER_HAS_PROP(_id, st_irq_handler)),       \
           .trigger_valid = MR_HT_DRIVER_HAS_PROP(_id, st_trigger)},
#else
#define __ST_HT_ADC_IRQ(_id)
#define __ST_HT_ADC_STREAM(_id)
#endif /* defined(ADC_USE_STREAM) */
/* ADC instance install */
#define ST_HT_ADC_INSTALL(_id, _ops)                                           \
    static void MR_CAT(__adc_clk_enable_, _id)(void) {                         \
        MR_HT_DRIVER_PROP(_id, st_clken)();                                    \
    }                                                                          \
    static ADC_HandleTypeDef MR_CAT(__adc_handle_, _id)                        \
        = {.Instance = MR_HT_DRIVER_PROP(_id, st_adcx)};                       \
    static adc_channel_t MR_CAT(__adc_channel_, _id)[]                         \
        = {MR_HT_DRIVER_PROP_ELEM_FOREACH(_id, st_channels,                    \
                                          __ST_HT_ADC_CHANNEL_IMPL)};          \
    static adc_priv_t MR_CAT(__adc_priv_, _id)                                 \
        = {.handle = &MR_CAT(__adc_handle_, _id),                              \
           .clk_enable = MR_CAT(__adc_clk_enable_, _id),                       \
           .ref = MR_HT_DRIVER_PROP(_id, st_ref),                              \
           .channels = MR_CAT(__adc_channel_, _id),                            \
           .channels_num = MR_ARRAY_SIZE(MR_CAT(__adc_channel_, _id)),         \
           __ST_HT_ADC_STREAM(_id)};                                           \
    __ST_HT_ADC_IRQ(_id)                                                       \
    MR_HT_ADC_INSTALL(_id, &MR_CAT(__adc_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(ST_HT_ADC_INSTALL, &__adc_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_ADC) */

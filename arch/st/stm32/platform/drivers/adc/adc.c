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
#define ADC_USE_STREAM
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) && defined(MR_USE_DRIVER_DMA) */

/* HardTree compatible */
#define MR_HT_COMPAT                    st_stm32_adc
#if MR_HT_HAS_DRIVER()
#if !defined(MR_USE_DRIVER_GPIO)
#error "STM32 ADC requires the GPIO driver"
#endif /* !defined(MR_USE_DRIVER_GPIO) */

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

/* ADC capability definition */
#if defined(ADC_CR_ADSTART) && defined(ADC_CR_ADSTP) && defined(ADC_FLAG_EOC)  \
    && defined(ADC_IT_EOC) && defined(ADC_FLAG_EOS) && defined(ADC_IT_EOS)     \
    && defined(ADC_FLAG_OVR) && defined(ADC_IT_OVR)
#define STM32_ADC_HAS_STREAM
#endif /* ADC stream capability */
#if defined(ADC_USE_STREAM) && !defined(STM32_ADC_HAS_STREAM)
#error "STM32 ADC stream is not supported by this ADC IP"
#endif /* defined(ADC_USE_STREAM) && !defined(STM32_ADC_HAS_STREAM) */

/* ADC channel type */
typedef struct adc_channel {
    uint32_t channel;
    mr_ht_gpio_spec_t gpio;
} adc_channel_t;

#if defined(ADC_USE_STREAM)
/* ADC DMA type */
typedef struct adc_dma {
    mr_ht_dma_spec_t dma;
    mr_dma_transfer_t transfer;
    mr_waiter_t waiter;
} adc_dma_t;

/* ADC stream type */
typedef struct adc_stream {
    uint32_t trigger;
    uint32_t trigger_edge;
    IRQn_Type irqn;
    mr_uint32_t irq_priority;
    adc_dma_t *dma;
    mr_driver_t *driver;
    mr_uint8_t *buf;
    mr_size_t size;
    mr_uint32_t event_mask;
    mr_uint8_t sample_size;
    volatile mr_uint32_t offset;
    volatile mr_bool_t complete;
    mr_uint8_t irq_valid: 1;
    mr_uint8_t trigger_valid: 1;
    mr_uint8_t use_dma: 1;
    mr_uint8_t adc_started: 1;
    mr_uint8_t half_enabled: 1;
    mr_uint8_t circular: 1;
} adc_stream_t;
#endif /* defined(ADC_USE_STREAM) */

/* ADC private data type */
typedef struct adc_priv {
    ADC_HandleTypeDef handle;
    void (*clk_enable)(void);
    const adc_channel_t *channels;
    mr_uint32_t channels_num;
    mr_uint8_t ref;
#if defined(ADC_USE_STREAM)
    adc_stream_t stream;
#endif /* defined(ADC_USE_STREAM) */
} adc_priv_t;

/* ADC definition */
#define ADC_POLL_TIMEOUT_MS             (100U)
#define ADC_CHANNEL_BITS                (32U)

/* ADC trigger edge definition */
#if defined(ADC_EXTERNALTRIGCONVEDGE_NONE)
#define ADC_TRIGGER_EDGE_NONE           ADC_EXTERNALTRIGCONVEDGE_NONE
#else
#define ADC_TRIGGER_EDGE_NONE           (0U)
#endif /* defined(ADC_EXTERNALTRIGCONVEDGE_NONE) */

#if defined(ADC_USE_STREAM)
/* ADC event mask definition */
#define ADC_EVENT_MASK                                                         \
    (MR_ADC_WAIT_STREAM_HALF | MR_ADC_WAIT_STREAM_FULL                         \
     | MR_ADC_WAIT_STREAM_ERROR)

#if defined(ADC_EXTERNALTRIGCONVEDGE_RISING)
#define ADC_TRIGGER_EDGE_RISING         ADC_EXTERNALTRIGCONVEDGE_RISING
#else
#define ADC_TRIGGER_EDGE_RISING         ADC_TRIGGER_EDGE_NONE
#endif /* defined(ADC_EXTERNALTRIGCONVEDGE_RISING) */
#endif /* defined(ADC_USE_STREAM) */

/* ADC sample time map */
#if defined(ADC_SAMPLETIME_2CYCLES_5) && defined(ADC_SAMPLETIME_640CYCLES_5)
static const uint32_t __adc_sample_time[] = {
    ADC_SAMPLETIME_2CYCLES_5,   ADC_SAMPLETIME_6CYCLES_5,
    ADC_SAMPLETIME_12CYCLES_5,  ADC_SAMPLETIME_24CYCLES_5,
    ADC_SAMPLETIME_47CYCLES_5,  ADC_SAMPLETIME_92CYCLES_5,
    ADC_SAMPLETIME_247CYCLES_5, ADC_SAMPLETIME_640CYCLES_5,
};
#elif defined(ADC_SAMPLETIME_1CYCLE_5) && defined(ADC_SAMPLETIME_160CYCLES_5)
static const uint32_t __adc_sample_time[] = {
    ADC_SAMPLETIME_1CYCLE_5,   ADC_SAMPLETIME_3CYCLES_5,
    ADC_SAMPLETIME_7CYCLES_5,  ADC_SAMPLETIME_12CYCLES_5,
    ADC_SAMPLETIME_19CYCLES_5, ADC_SAMPLETIME_39CYCLES_5,
    ADC_SAMPLETIME_79CYCLES_5, ADC_SAMPLETIME_160CYCLES_5,
};
#elif defined(ADC_SAMPLETIME_3CYCLES) && defined(ADC_SAMPLETIME_480CYCLES)
static const uint32_t __adc_sample_time[] = {
    ADC_SAMPLETIME_3CYCLES,   ADC_SAMPLETIME_15CYCLES,
    ADC_SAMPLETIME_28CYCLES,  ADC_SAMPLETIME_56CYCLES,
    ADC_SAMPLETIME_84CYCLES,  ADC_SAMPLETIME_112CYCLES,
    ADC_SAMPLETIME_144CYCLES, ADC_SAMPLETIME_480CYCLES,
};
#elif defined(ADC_SAMPLETIME_1CYCLE_5) && defined(ADC_SAMPLETIME_239CYCLES_5)
static const uint32_t __adc_sample_time[] = {
    ADC_SAMPLETIME_1CYCLE_5,   ADC_SAMPLETIME_7CYCLES_5,
    ADC_SAMPLETIME_13CYCLES_5, ADC_SAMPLETIME_28CYCLES_5,
    ADC_SAMPLETIME_41CYCLES_5, ADC_SAMPLETIME_55CYCLES_5,
    ADC_SAMPLETIME_71CYCLES_5, ADC_SAMPLETIME_239CYCLES_5,
};
#else
#error "Unsupported STM32 ADC sampling time layout"
#endif /* ADC sample time layout */

#if defined(ADC_USE_STREAM)
/* ADC rank map */
static const uint32_t __adc_regular_rank[] = {
    ADC_REGULAR_RANK_1, ADC_REGULAR_RANK_2, ADC_REGULAR_RANK_3,
    ADC_REGULAR_RANK_4, ADC_REGULAR_RANK_5, ADC_REGULAR_RANK_6,
    ADC_REGULAR_RANK_7, ADC_REGULAR_RANK_8,
};
#endif /* defined(ADC_USE_STREAM) */

static mr_err_t adc_resolution(mr_uint8_t resolution,
                               uint32_t *hal_resolution) {
    /* Translate resolution */
#if defined(ADC_RESOLUTION_12B)
    switch (resolution) {
        case 12U: {
            *hal_resolution = ADC_RESOLUTION_12B;
            break;
        }
#if defined(ADC_RESOLUTION_10B)
        case 10U: {
            *hal_resolution = ADC_RESOLUTION_10B;
            break;
        }
#endif /* defined(ADC_RESOLUTION_10B) */
#if defined(ADC_RESOLUTION_8B)
        case 8U: {
            *hal_resolution = ADC_RESOLUTION_8B;
            break;
        }
#endif /* defined(ADC_RESOLUTION_8B) */
#if defined(ADC_RESOLUTION_6B)
        case 6U: {
            *hal_resolution = ADC_RESOLUTION_6B;
            break;
        }
#endif /* defined(ADC_RESOLUTION_6B) */
        default: {
            return -MR_EINVAL;
        }
    }
#else
    if (resolution != 12U) {
        return -MR_EINVAL;
    }
    *hal_resolution = 0U;
#endif /* defined(ADC_RESOLUTION_12B) */
    return 0;
}

static mr_size_t adc_sample_size(mr_uint8_t resolution) {
    return (resolution <= 8U) ? sizeof(mr_uint8_t) : sizeof(mr_uint16_t);
}

static void adc_sample_store(void *buf, mr_size_t offset, mr_size_t sample_size,
                             mr_uint16_t value) {
    mr_uint8_t *data = buf;

    if (sample_size == sizeof(mr_uint8_t)) {
        data[offset] = (mr_uint8_t)value;
        return;
    }
    *(mr_uint16_t *)(data + offset) = value;
}

static mr_err_t adc_calibrate(ADC_HandleTypeDef *handle) {
    /* Calibrate ADC */
#if defined(ADC_CR_ADCAL) || defined(ADC_CR2_CAL)
#if defined(ADC_CALIB_OFFSET) && defined(ADC_SINGLE_ENDED)
    return mr_stm32_hal_status(HAL_ADCEx_Calibration_Start(
        handle, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED));
#elif defined(ADC_SINGLE_ENDED)
    return mr_stm32_hal_status(
        HAL_ADCEx_Calibration_Start(handle, ADC_SINGLE_ENDED));
#else
    return mr_stm32_hal_status(HAL_ADCEx_Calibration_Start(handle));
#endif /* defined(ADC_CALIB_OFFSET) && defined(ADC_SINGLE_ENDED) */
#else
    MR_IGNORE(handle);
    return 0;
#endif /* defined(ADC_CR_ADCAL) || defined(ADC_CR2_CAL) */
}

static void adc_make_init(uint32_t resolution, FunctionalState continuous_conv,
                          FunctionalState dma_requests, uint32_t conversions,
                          uint32_t trigger, uint32_t trigger_edge,
                          ADC_InitTypeDef *init) {
    /* Build ADC init */
    *init = (ADC_InitTypeDef){0};
#if defined(ADC_CLOCK_SYNC_PCLK_DIV4)
    init->ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
#endif /* defined(ADC_CLOCK_SYNC_PCLK_DIV4) */
#if defined(ADC_RESOLUTION_12B)
    init->Resolution = resolution;
#else
    MR_IGNORE(resolution);
#endif /* defined(ADC_RESOLUTION_12B) */
    init->DataAlign = ADC_DATAALIGN_RIGHT;
    init->ScanConvMode = (conversions > 1U) ? ENABLE : DISABLE;
#if defined(ADC_EOC_SINGLE_CONV)
    init->EOCSelection = ADC_EOC_SINGLE_CONV;
#endif /* defined(ADC_EOC_SINGLE_CONV) */
#if defined(ADC_CFGR_AUTDLY) || defined(ADC_CFGR1_WAIT)
    init->LowPowerAutoWait = DISABLE;
#endif /* defined(ADC_CFGR_AUTDLY) || defined(ADC_CFGR1_WAIT) */
    init->ContinuousConvMode = continuous_conv;
    init->NbrOfConversion = conversions;
    init->DiscontinuousConvMode = DISABLE;
#if defined(ADC_CR1_DISCNUM) || defined(ADC_CR1_DISCNUM_Msk)                   \
    || defined(ADC_CFGR_DISCNUM) || defined(ADC_CFGR_DISCNUM_Msk)
    init->NbrOfDiscConversion = 1U;
#endif /* ADC discontinuous conversion count */
    init->ExternalTrigConv = trigger;
#if defined(ADC_EOC_SINGLE_CONV)
    init->ExternalTrigConvEdge = trigger_edge;
    init->DMAContinuousRequests = dma_requests;
#else
    MR_IGNORE(trigger_edge);
    MR_IGNORE(dma_requests);
#endif /* defined(ADC_EOC_SINGLE_CONV) */
#if defined(ADC_SAMPLING_MODE_NORMAL)
    init->SamplingMode = ADC_SAMPLING_MODE_NORMAL;
#endif /* defined(ADC_SAMPLING_MODE_NORMAL) */
#if defined(ADC_OVR_DATA_OVERWRITTEN)
    init->Overrun = ADC_OVR_DATA_OVERWRITTEN;
#endif /* defined(ADC_OVR_DATA_OVERWRITTEN) */
#if defined(ADC_OVERSAMPLING_RATIO_2)
    init->OversamplingMode = DISABLE;
#endif /* defined(ADC_OVERSAMPLING_RATIO_2) */
}

static mr_err_t adc_init_mode(adc_priv_t *priv, uint32_t resolution,
                              FunctionalState continuous_conv,
                              FunctionalState dma_requests,
                              uint32_t conversions, uint32_t trigger,
                              uint32_t trigger_edge) {
    ADC_InitTypeDef init;
    mr_err_t ret;

    /* Check ADC resources */
    if ((priv->handle.Instance == MR_NULL) || (priv->clk_enable == MR_NULL)) {
        return -MR_EINVAL;
    }

    /* Build ADC init */
    adc_make_init(resolution, continuous_conv, dma_requests, conversions,
                  trigger, trigger_edge, &init);

    /* Enable ADC clock */
    priv->clk_enable();

    /* Init ADC */
    priv->handle.Init = init;
    ret = mr_stm32_hal_status(HAL_ADC_Init(&priv->handle));
    if (ret != 0) {
        return ret;
    }

    /* Calibrate ADC */
    return adc_calibrate(&priv->handle);
}

static mr_err_t adc_init(adc_priv_t *priv, uint32_t resolution) {
    return adc_init_mode(priv, resolution, DISABLE, DISABLE, 1U,
                         ADC_SOFTWARE_START, ADC_TRIGGER_EDGE_NONE);
}

static mr_err_t adc_configure_hw_channel(adc_priv_t *priv,
                                         const adc_channel_t *channel,
                                         mr_uint32_t sample_time,
                                         uint32_t rank) {
    ADC_ChannelConfTypeDef adc_config;
    mr_gpio_config_t gpio_config;
    mr_err_t ret;

    /* Check channel */
    if ((channel->gpio.drv == MR_NULL)
        || (sample_time >= MR_ARRAY_SIZE(__adc_sample_time))) {
        return -MR_EINVAL;
    }

    /* Configure GPIO */
    gpio_config = (mr_gpio_config_t){0};
    gpio_config.mode = MR_GPIO_MODE_ANALOG;
    gpio_config.pull = MR_GPIO_PULL_NONE;
    ret = mr_gpio_configure(channel->gpio.drv, channel->gpio.pin, &gpio_config);
    if (ret != 0) {
        return ret;
    }

    /* Configure ADC channel */
    adc_config = (ADC_ChannelConfTypeDef){0};
    adc_config.Channel = channel->channel;
    adc_config.Rank = rank;
    adc_config.SamplingTime = __adc_sample_time[sample_time];
#if defined(ADC_SINGLE_ENDED)
    adc_config.SingleDiff = ADC_SINGLE_ENDED;
#endif /* defined(ADC_SINGLE_ENDED) */
#if defined(ADC_OFFSET_NONE)
    adc_config.OffsetNumber = ADC_OFFSET_NONE;
#endif /* defined(ADC_OFFSET_NONE) */
#if defined(ADC_OFFSET_SIGN_NEGATIVE)
    adc_config.OffsetSign = ADC_OFFSET_SIGN_NEGATIVE;
    adc_config.OffsetSaturation = DISABLE;
#endif /* defined(ADC_OFFSET_SIGN_NEGATIVE) */
    return mr_stm32_hal_status(
        HAL_ADC_ConfigChannel(&priv->handle, &adc_config));
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

    /* Start ADC */
    ret = mr_stm32_hal_status(HAL_ADC_Start(&priv->handle));
    if (ret != 0) {
        return ret;
    }

    /* Wait ADC */
    ret = mr_stm32_hal_status(
        HAL_ADC_PollForConversion(&priv->handle, ADC_POLL_TIMEOUT_MS));
    if (ret != 0) {
        HAL_ADC_Stop(&priv->handle);
        return ret;
    }

    /* Read ADC */
    sample = (mr_uint16_t)HAL_ADC_GetValue(&priv->handle);

    /* Stop ADC */
    ret = mr_stm32_hal_status(HAL_ADC_Stop(&priv->handle));
    if (ret != 0) {
        return ret;
    }
    *value = sample;
    return 0;
}

static mr_err_t adc_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_adc_config_t *config) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    uint32_t resolution;
#if defined(ADC_USE_STREAM)
    mr_bool_t busy;
#endif /* defined(ADC_USE_STREAM) */
    mr_err_t ret;

    /* Check channel */
    if (channel >= priv->channels_num) {
        return -MR_EINVAL;
    }

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

    /* Init ADC */
    ret = adc_resolution(12U, &resolution);
    if (ret != 0) {
        return ret;
    }
    ret = adc_init(priv, resolution);
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

    /* Check channel */
    if (channel >= priv->channels_num) {
        return -MR_EINVAL;
    }

    /* Get channel configuration */
    config->diff = MR_ADC_DIFF_SINGLE;
    config->ref = priv->ref;
    config->gain = MR_ADC_GAIN_1;
    return 0;
}

static mr_ssize_t adc_read(const mr_driver_t *drv, mr_adc_seq_t *seq) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_size_t sample_size, count;
    mr_uint32_t channel;
    uint32_t resolution;
    mr_uint16_t sample;
    mr_err_t ret;
#if defined(ADC_USE_STREAM)
    mr_bool_t busy;
#endif /* defined(ADC_USE_STREAM) */

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
    sample_size = adc_sample_size(seq->resolution);

    /* Check sequence buffer */
    if ((seq->channels == 0U) || (seq->size < sample_size)) {
        return 0;
    }
    if ((seq->buf == MR_NULL) || (seq->size > (mr_size_t)MR_SSIZE_MAX)
        || (((mr_uintptr_t)seq->buf % sample_size) != 0U)) {
        return -MR_EINVAL;
    }

    /* Init ADC */
    ret = adc_init(priv, resolution);
    if (ret != 0) {
        return ret;
    }

    /* Read channel sequence */
    count = 0U;
    while ((seq->size - count) >= sample_size) {
        for (channel = 0U; channel < ADC_CHANNEL_BITS; channel++) {
            if ((seq->channels & (1UL << channel)) == 0U) {
                continue;
            }
            if (channel >= priv->channels_num) {
                return (count != 0U) ? (mr_ssize_t)count : -MR_EINVAL;
            }
            ret = adc_convert(priv, &priv->channels[channel], seq->oversample,
                              &sample);
            if (ret != 0) {
                return (count != 0U) ? (mr_ssize_t)count : ret;
            }
            adc_sample_store(seq->buf, count, sample_size, sample);
            count += sample_size;
            if ((seq->size - count) < sample_size) {
                break;
            }
        }
    }
    return (mr_ssize_t)count;
}

#if defined(ADC_USE_STREAM)
static mr_err_t adc_init_stream(adc_priv_t *priv, uint32_t resolution,
                                mr_uint32_t conversions,
                                const mr_adc_stream_config_t *config,
                                mr_bool_t use_dma) {
    FunctionalState continuous_conv, dma_requests;
    uint32_t trigger, trigger_edge;

    /* Configure stream source */
    if (config->source == MR_ADC_STREAM_SOURCE_SOFTWARE) {
        continuous_conv = ENABLE;
        trigger = ADC_SOFTWARE_START;
        trigger_edge = ADC_TRIGGER_EDGE_NONE;
    } else {
        continuous_conv = DISABLE;
        trigger = priv->stream.trigger;
        trigger_edge = priv->stream.trigger_edge;
    }
    /* Configure DMA request */
    dma_requests = use_dma ? ENABLE : DISABLE;

    /* Init ADC stream */
    return adc_init_mode(priv, resolution, continuous_conv, dma_requests,
                         conversions, trigger, trigger_edge);
}

static void adc_trigger_disable(adc_priv_t *priv) {
    /* Disable external trigger */
#if defined(ADC_CFGR_EXTEN)
    CLEAR_BIT(priv->handle.Instance->CFGR, ADC_CFGR_EXTEN);
#elif defined(ADC_CFGR1_EXTEN)
    CLEAR_BIT(priv->handle.Instance->CFGR1, ADC_CFGR1_EXTEN);
#else
    MR_IGNORE(priv);
#endif /* defined(ADC_CFGR_EXTEN) || defined(ADC_CFGR1_EXTEN) */
}

static mr_err_t adc_dma_control(adc_priv_t *priv, mr_bool_t enable) {
    /* Control DMA request */
#if defined(ADC_CFGR_DMAEN)
    if (enable) {
        SET_BIT(priv->handle.Instance->CFGR, ADC_CFGR_DMAEN);
    } else {
        CLEAR_BIT(priv->handle.Instance->CFGR, ADC_CFGR_DMAEN);
    }
    return 0;
#elif defined(ADC_CFGR1_DMAEN)
    if (enable) {
        SET_BIT(priv->handle.Instance->CFGR1, ADC_CFGR1_DMAEN);
    } else {
        CLEAR_BIT(priv->handle.Instance->CFGR1, ADC_CFGR1_DMAEN);
    }
    return 0;
#else
    MR_IGNORE(priv);
    return enable ? -MR_ENOSYS : 0;
#endif /* defined(ADC_CFGR_DMAEN) || defined(ADC_CFGR1_DMAEN) */
}

static void adc_conversion_stop(adc_priv_t *priv) {
    /* Stop conversion */
    if ((READ_BIT(priv->handle.Instance->CR, ADC_CR_ADSTART)) != 0U) {
        SET_BIT(priv->handle.Instance->CR, ADC_CR_ADSTP);
    }
}

static void adc_stream_notify(adc_stream_t *stream, mr_wait_event_t events) {
    if (events == 0U) {
        return;
    }

    /* Filter ADC events */
    events &= (mr_wait_event_t)stream->event_mask;
    if ((events != 0U) && (stream->driver != MR_NULL)) {
        mr_adc_event_raise(stream->driver, events);
    }
}

static void adc_stream_disable(adc_priv_t *priv) {
    /* Disable stream requests */
    adc_trigger_disable(priv);
    if (priv->stream.use_dma) {
        adc_dma_control(priv, MR_FALSE);
    }
}

static void adc_stream_halt(adc_priv_t *priv) {
    /* Disable stream */
    adc_stream_disable(priv);
    adc_conversion_stop(priv);

    /* Complete stream */
    priv->stream.complete = MR_TRUE;
}

static void adc_dma_event(mr_waiter_t *waiter, void *param,
                          mr_wait_event_t event, const void *data) {
    adc_priv_t *priv = (adc_priv_t *)param;
    adc_stream_t *stream = &priv->stream;
    mr_wait_event_t adc_event;

    MR_IGNORE(waiter);
    MR_IGNORE(data);

    /* Check stream state */
    if ((stream->driver == MR_NULL) || stream->complete) {
        return;
    }

    /* Translate DMA event */
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
    adc_stream_t *stream = &priv->stream;
    adc_dma_t *dma = stream->dma;
    mr_dma_config_t config;
    mr_wait_event_t events;
    mr_uint32_t width;
    mr_err_t ret;

    /* Configure sample access width */
    width = (stream->sample_size == sizeof(mr_uint8_t)) ? MR_DMA_WIDTH_8
                                                        : MR_DMA_WIDTH_16;
    config = (mr_dma_config_t){
        .src_width = width,
        .dst_width = width,
        .src_adj = MR_DMA_ADJ_CONST,
        .dst_adj = MR_DMA_ADJ_INC,
        .src_burst = MR_DMA_BURST_1,
        .dst_burst = MR_DMA_BURST_1,
        .direction = MR_DMA_DIR_P2M,
    };

    /* Get DMA events */
    events = adc_stream_dma_events(stream);

    /* Configure DMA */
    ret = mr_ht_dma_configure(&dma->dma, &config);
    if (ret != 0) {
        return ret;
    }

    /* Init waiter */
    ret = mr_waiter_init(&dma->waiter, adc_dma_event, priv, events);
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
    dma->transfer.next = stream->circular ? &dma->transfer : MR_NULL;
    dma->transfer.src = (void *)&priv->handle.Instance->DR;
    dma->transfer.dst = stream->buf;
    dma->transfer.count = stream->size / stream->sample_size;
    ret = mr_ht_dma_start(&dma->dma, &dma->transfer);
    if (ret != 0) {
        mr_ht_dma_event_disable(&dma->dma, events);
        goto _exit;
    }

    /* Enable ADC DMA request */
    ret = adc_dma_control(priv, MR_TRUE);
    if (ret == 0) {
        return 0;
    }
    mr_ht_dma_stop(&dma->dma);
    mr_ht_dma_event_disable(&dma->dma, events);
_exit:
    /* Release waiter */
    mr_waiter_del(&dma->waiter);
    return ret;
}

static mr_err_t adc_seq_ranks(const adc_priv_t *priv, mr_uint32_t channels,
                              mr_uint8_t *index, mr_uint32_t *count) {
    mr_uint32_t channel, rank;

    /* Check channels */
    if (channels == 0U) {
        return -MR_EINVAL;
    }

    /* Build ADC ranks */
    rank = 0U;
    for (channel = 0U; channel < ADC_CHANNEL_BITS; channel++) {
        if ((channels & (1UL << channel)) == 0U) {
            continue;
        }

        /* Check channel */
        if (channel >= priv->channels_num) {
            return -MR_EINVAL;
        }

        /* Check rank */
        if (rank >= MR_ARRAY_SIZE(__adc_regular_rank)) {
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

static mr_err_t adc_stream_start(const mr_driver_t *drv, mr_adc_seq_t *seq,
                                 const mr_adc_stream_config_t *config) {
    mr_uint8_t ranks[MR_ARRAY_SIZE(__adc_regular_rank)];
    mr_size_t sample_count, half_count, sample_size;
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    adc_stream_t *stream = &priv->stream;
    mr_uint32_t channels_num, rank;
    mr_bool_t busy, use_dma;
    uint32_t resolution;
    mr_err_t ret;

    /* Check stream resources */
    use_dma = MR_FALSE;
    if ((stream->dma != MR_NULL) && (stream->dma->dma.drv != MR_NULL)) {
        use_dma = MR_TRUE;
    }
    if (!use_dma && !stream->irq_valid) {
        return -MR_ENODEV;
    }
    if ((config->source == MR_ADC_STREAM_SOURCE_EXTERNAL)
        && !stream->trigger_valid) {
        return -MR_ENODEV;
    }

    /* Check sequence configuration */
    ret = adc_resolution(seq->resolution, &resolution);
    if (ret != 0) {
        return ret;
    }
    if (seq->oversample >= MR_ARRAY_SIZE(__adc_sample_time)) {
        return -MR_EINVAL;
    }
    sample_size = adc_sample_size(seq->resolution);

    /* Check buffer */
    if ((seq->buf == MR_NULL) || (seq->size == 0U)
        || ((seq->size % sample_size) != 0U)
        || (((mr_uintptr_t)seq->buf % sample_size) != 0U)) {
        return -MR_EINVAL;
    }

    /* Build ADC ranks */
    ret = adc_seq_ranks(priv, seq->channels, ranks, &channels_num);
    if (ret != 0) {
        return ret;
    }

    /* Check sample count */
    sample_count = seq->size / sample_size;
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

    /* Init ADC stream */
    ret = adc_init_stream(priv, resolution, channels_num, config, use_dma);
    if (ret != 0) {
        return ret;
    }

    /* Configure ADC channels */
    for (rank = 0U; rank < channels_num; rank++) {
        ret = adc_configure_hw_channel(priv, &priv->channels[ranks[rank]],
                                       seq->oversample,
                                       __adc_regular_rank[rank]);
        if (ret != 0) {
            return ret;
        }
    }

    /* Update stream state */
    stream->buf = seq->buf;
    stream->size = seq->size;
    stream->sample_size = (mr_uint8_t)sample_size;
    stream->use_dma = use_dma;
    stream->circular = (config->circular != MR_FALSE);
    stream->offset = 0U;
    stream->complete = MR_FALSE;
    stream->driver = (mr_driver_t *)drv;

    /* Check half event boundary */
    half_count = sample_count / 2U;
    stream->half_enabled
        = ((sample_count % 2U) == 0U) && ((half_count % channels_num) == 0U);

    /* Start data path */
    if (use_dma) {
        ret = adc_stream_start_dma(priv);
        if (ret != 0) {
            stream->driver = MR_NULL;
            return ret;
        }
    } else {
        HAL_NVIC_SetPriority(stream->irqn, stream->irq_priority, 0U);
        HAL_NVIC_EnableIRQ(stream->irqn);
    }

    /* Start ADC */
    stream->adc_started = 1U;
    if (use_dma) {
        ret = mr_stm32_hal_status(HAL_ADC_Start(&priv->handle));
    } else {
        ret = mr_stm32_hal_status(HAL_ADC_Start_IT(&priv->handle));
    }
    if (ret != 0) {
        adc_stream_stop(drv);
    }
    return ret;
}

static mr_err_t adc_stream_stop(const mr_driver_t *drv) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    adc_stream_t *stream = &priv->stream;
    adc_dma_t *dma = stream->dma;
    mr_wait_event_t events;
    mr_err_t ret;

    /* Check stream state */
    if (stream->driver == MR_NULL) {
        return 0;
    }

    /* Disable stream */
    adc_stream_disable(priv);

    /* Stop ADC */
    if (stream->adc_started) {
        if (stream->use_dma) {
            ret = mr_stm32_hal_status(HAL_ADC_Stop(&priv->handle));
        } else {
            ret = mr_stm32_hal_status(HAL_ADC_Stop_IT(&priv->handle));
        }
        if (ret != 0) {
            return ret;
        }
        stream->adc_started = 0U;
    }

    /* Check DMA */
    if (stream->use_dma && (dma != MR_NULL)) {
        /* Get DMA events */
        events = adc_stream_dma_events(stream);

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
    }

    /* Release stream */
    stream->driver = MR_NULL;
    return 0;
}

static mr_err_t adc_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    adc_stream_t *stream = &priv->stream;
    mr_bool_t dma_busy;
    mr_err_t ret;

    /* Check stream state */
    if (stream->driver == MR_NULL) {
        *busy = MR_FALSE;
        return 0;
    }
    if (stream->complete) {
        ret = adc_stream_stop(drv);
        if (ret != 0) {
            return ret;
        }
        *busy = MR_FALSE;
        return 0;
    }

    /* Check stream mode */
    if (!stream->use_dma) {
        *busy = MR_TRUE;
        return 0;
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
    ret = adc_stream_stop(drv);
    if (ret != 0) {
        return ret;
    }
    *busy = MR_FALSE;
    return 0;
}

static mr_err_t adc_stream_offset(const mr_driver_t *drv, mr_size_t *offset) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    adc_stream_t *stream = &priv->stream;

    /* Get DMA stream offset */
    if (stream->use_dma) {
        return mr_ht_dma_offset(&stream->dma->dma, offset);
    }

    /* Get IRQ stream offset */
    *offset = stream->offset;
    return 0;
}

static mr_err_t adc_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable) {
    const adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    adc_stream_t *stream = &priv->stream;

    /* Check parameter */
    if ((events == 0U) || ((events & ~ADC_EVENT_MASK) != 0U)) {
        return -MR_EINVAL;
    }

    /* Check event state */
    if (!enable && (stream->driver != MR_NULL)) {
        if (((events & MR_ADC_WAIT_STREAM_ERROR) != 0U)
            || (((events & MR_ADC_WAIT_STREAM_FULL) != 0U)
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

MR_UNUSED static void adc_irq(adc_priv_t *priv) {
    ADC_HandleTypeDef *handle = &priv->handle;
    adc_stream_t *stream = &priv->stream;
    mr_wait_event_t event;
    mr_uint32_t offset;

    /* Check stream state */
    if ((stream->driver == MR_NULL) || stream->use_dma) {
        return;
    }

    /* Check overrun */
    if ((__HAL_ADC_GET_FLAG(handle, ADC_FLAG_OVR) != 0U)
        && (__HAL_ADC_GET_IT_SOURCE(handle, ADC_IT_OVR) != 0U)) {
        __HAL_ADC_CLEAR_FLAG(handle, ADC_FLAG_OVR);
        __HAL_ADC_DISABLE_IT(handle, ADC_IT_EOC | ADC_IT_EOS | ADC_IT_OVR);
        adc_stream_halt(priv);
        adc_stream_notify(stream, MR_ADC_WAIT_STREAM_ERROR);
        return;
    }

    /* Check conversion */
    if ((__HAL_ADC_GET_FLAG(handle, ADC_FLAG_EOC) == 0U)
        || (__HAL_ADC_GET_IT_SOURCE(handle, ADC_IT_EOC) == 0U)) {
        return;
    }

    /* Read ADC value */
    offset = stream->offset;
    adc_sample_store(stream->buf, offset, stream->sample_size,
                     (mr_uint16_t)HAL_ADC_GetValue(handle));

    /* Update stream offset */
    offset += stream->sample_size;
    stream->offset = offset;

    /* Clear sequence flag */
    if (__HAL_ADC_GET_FLAG(handle, ADC_FLAG_EOS) != 0U) {
        __HAL_ADC_CLEAR_FLAG(handle, ADC_FLAG_EOS);
    }

    /* Check stream event */
    event = 0U;
    if (stream->half_enabled && (offset == (stream->size / 2U))) {
        event = MR_ADC_WAIT_STREAM_HALF;
    }
    if (offset == stream->size) {
        event = MR_ADC_WAIT_STREAM_FULL;
        if (!stream->circular) {
            __HAL_ADC_DISABLE_IT(handle, ADC_IT_EOC | ADC_IT_EOS | ADC_IT_OVR);
            adc_stream_halt(priv);
        }
    }

    /* Raise ADC event */
    adc_stream_notify(stream, event);

    /* Reset circular stream */
    if ((event == MR_ADC_WAIT_STREAM_FULL) && stream->circular) {
        stream->offset = 0U;
    }
}
#endif /* defined(ADC_USE_STREAM) */

/* Instance install */
#define ST_HT_ADC_INSTALL(_id, _ops)                                           \
    static void MR_CAT(__adc_clk_enable_, _id)(void) {                         \
        MR_HT_DRIVER_PROP(_id, st_clken)();                                    \
    }                                                                          \
    static const adc_channel_t MR_CAT(__adc_channel_, _id)[]                   \
        = {MR_HT_DRIVER_PROP_ELEM_FOREACH(_id, st_channels,                    \
                                          __ST_HT_ADC_CHANNEL_IMPL)};          \
    __ST_HT_ADC_DMA_INSTALL(_id)                                               \
    static adc_priv_t MR_CAT(__adc_priv_, _id) = {                             \
        .handle = {                                                            \
            .Instance = MR_HT_DRIVER_PROP(_id, st_adcx),                       \
        },                                                                     \
        .clk_enable = MR_CAT(__adc_clk_enable_, _id),                          \
        .channels = MR_CAT(__adc_channel_, _id),                               \
        .channels_num = MR_HT_DRIVER_PROP_ELEM_NUM(_id, st_channels),          \
        .ref = MR_HT_DRIVER_PROP(_id, st_ref),                                 \
        __ST_HT_ADC_STREAM(_id)                                                \
    };                                                                         \
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
#if defined(ADC_USE_STREAM)
#define __ST_HT_ADC_HAS_DMA(_id) MR_HT_DRIVER_HAS_PROP(_id, st_dma)
#define __ST_HT_ADC_DMA_INSTALL(_id)                                           \
    MR_MACRO_IF(__ST_HT_ADC_HAS_DMA(_id),                                      \
                (static adc_dma_t MR_CAT(__adc_dma_, _id) = {                  \
                    .dma = MR_HT_DMA_SPEC(_id, st_dma),                        \
                };),                                                           \
                ())
#define __ST_HT_ADC_DMA(_id)                                                   \
    MR_MACRO_IF(__ST_HT_ADC_HAS_DMA(_id),                                      \
                (.dma = &MR_CAT(__adc_dma_, _id),), ())
#define __ST_HT_ADC_HAS_IRQ(_id)                                               \
    MR_MACRO_AND(MR_HT_DRIVER_HAS_PROP(_id, st_irqn),                          \
                 MR_HT_DRIVER_HAS_PROP(_id, st_irq_handler))
#define __ST_HT_ADC_IRQ(_id)                                                   \
    MR_MACRO_IF(__ST_HT_ADC_HAS_IRQ(_id),                                      \
                (void MR_HT_DRIVER_PROP(_id, st_irq_handler)(void) {           \
                    adc_irq(&MR_CAT(__adc_priv_, _id));                        \
                }),                                                            \
                ())
#define __ST_HT_ADC_STREAM(_id)                                                \
    .stream = {                                                                \
        __ST_HT_ADC_DMA(_id)                                                   \
        .trigger = MR_HT_DRIVER_PROP_OR(_id, st_trigger, 0U),                  \
        .trigger_edge =                                                        \
            MR_HT_DRIVER_PROP_OR(_id, st_trigger_edge,                         \
                                 ADC_TRIGGER_EDGE_RISING),                     \
        .irqn = MR_HT_DRIVER_PROP_OR(_id, st_irqn, (IRQn_Type)0),              \
        .irq_priority = MR_HT_DRIVER_PROP_OR(_id, st_irq_priority, 0U),        \
        .irq_valid = __ST_HT_ADC_HAS_IRQ(_id),                                 \
        .trigger_valid = MR_HT_DRIVER_HAS_PROP(_id, st_trigger),               \
    },
#else
#define __ST_HT_ADC_DMA_INSTALL(_id)
#define __ST_HT_ADC_IRQ(_id)
#define __ST_HT_ADC_STREAM(_id)
#endif /* defined(ADC_USE_STREAM) */
MR_HT_DRIVER_FOREACH(ST_HT_ADC_INSTALL, &__adc_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_ADC) */

/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-12        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_ADC)
#include <mrX/drivers/adc/mr_adc.h>
#if defined(MR_USE_DRIVER_ADC_STREAM) && defined(MR_USE_DRIVER_DMA)            \
    && defined(MR_USE_DRIVER_TMR)
#include <mrX/drivers/dma/mr_dma.h>
#include <mrX/drivers/tmr/mr_tmr.h>
#include <mrX/libc/mr_atomic.h>
#endif /* ADC stream + DMA + TMR */
#include "stm32h5xx_hal_adc.h"
#include "stm32h5xx_hal_adc_ex.h"

/* HardTree compatible */
#define MR_HT_COMPAT st_stm32h5_adc

#define ADC_POLL_TIMEOUT_MS (100U)
#define ADC_NANOSECONDS     (1000000000ULL)
#define ADC_EVENT_MASK                                                         \
    (MR_ADC_WAIT_STREAM_HALF | MR_ADC_WAIT_STREAM_FULL                         \
     | MR_ADC_WAIT_STREAM_ERROR)

/* ADC operations */
static mr_err_t adc_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_adc_config_t *config);
static mr_err_t adc_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_adc_config_t *config);
static mr_ssize_t adc_read(const mr_driver_t *drv, mr_adc_seq_t *seq);
#if defined(MR_USE_DRIVER_ADC_STREAM) && defined(MR_USE_DRIVER_DMA)            \
    && defined(MR_USE_DRIVER_TMR)
static mr_err_t adc_stream_start(const mr_driver_t *drv, mr_adc_seq_t *seq,
                                 const mr_adc_stream_config_t *config);
static mr_err_t adc_stream_stop(const mr_driver_t *drv);
static mr_err_t adc_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy);
static mr_err_t adc_stream_offset(const mr_driver_t *drv, mr_size_t *offset);
#endif /* ADC stream + DMA + TMR */
static mr_err_t adc_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable);
MR_UNUSED static const mr_adc_ops_t __adc_ops = {
    .channel_configure = adc_channel_configure,
    .channel_configuration = adc_channel_configuration,
    .read = adc_read,
#if defined(MR_USE_DRIVER_ADC_STREAM) && defined(MR_USE_DRIVER_DMA)            \
    && defined(MR_USE_DRIVER_TMR)
    .stream_start = adc_stream_start,
    .stream_stop = adc_stream_stop,
    .stream_is_busy = adc_stream_is_busy,
    .stream_offset = adc_stream_offset,
#endif /* ADC stream + DMA + TMR */
    .event_control = adc_event_control,
};

/* ADC channel */
typedef struct adc_channel {
    uint32_t channel;
    GPIO_TypeDef *port;
    uint32_t pin;
    mr_adc_config_t config;
    mr_bool_t gpio_ready;
} adc_channel_t;

/* ADC private data */
typedef struct adc_priv {
    ADC_HandleTypeDef *handle;
    void (*clk_enable)(void); /* Board-provided HAL clock-enable macro */
    mr_uint8_t ref;
    adc_channel_t *channels;
    mr_uint32_t channels_num;
    mr_uint32_t event_mask;
    mr_bool_t initialized;
    mr_bool_t calibrated;
#if defined(MR_USE_DRIVER_ADC_STREAM) && defined(MR_USE_DRIVER_DMA)            \
    && defined(MR_USE_DRIVER_TMR)
    mr_ht_dma_spec_t dma;
    mr_driver_t *timer;
    uint32_t trigger;
    mr_driver_t *stream_driver;
    mr_dma_transfer_t transfer;
    mr_waiter_t dma_waiter;
    volatile mr_atomic_t stream_active;
    mr_bool_t stream_circular;
    mr_bool_t dma_waiter_initialized;
#endif /* ADC stream + DMA + TMR */
} adc_priv_t;

/* HAL sampling time selected by mr_adc_seq_t.oversample */
static const uint32_t __adc_sample_time[] = {
    ADC_SAMPLETIME_2CYCLES_5,   ADC_SAMPLETIME_6CYCLES_5,
    ADC_SAMPLETIME_12CYCLES_5,  ADC_SAMPLETIME_24CYCLES_5,
    ADC_SAMPLETIME_47CYCLES_5,  ADC_SAMPLETIME_92CYCLES_5,
    ADC_SAMPLETIME_247CYCLES_5, ADC_SAMPLETIME_640CYCLES_5,
};

#if defined(MR_USE_DRIVER_ADC_STREAM) && defined(MR_USE_DRIVER_DMA)            \
    && defined(MR_USE_DRIVER_TMR)
static const uint32_t __adc_regular_rank[] = {
    ADC_REGULAR_RANK_1, ADC_REGULAR_RANK_2, ADC_REGULAR_RANK_3,
    ADC_REGULAR_RANK_4, ADC_REGULAR_RANK_5, ADC_REGULAR_RANK_6,
    ADC_REGULAR_RANK_7, ADC_REGULAR_RANK_8,
};
#endif /* ADC stream + DMA + TMR */

static mr_err_t adc_resolution(mr_uint8_t resolution,
                               uint32_t *hal_resolution) {
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
    ADC_HandleTypeDef *handle = priv->handle;
    mr_bool_t same;
    mr_err_t ret;

    /* Check parameter */
    if ((handle == MR_NULL) || (priv->clk_enable == MR_NULL)) {
        return -MR_EINVAL;
    }
    priv->clk_enable();

    same = priv->initialized
        && (handle->Init.ClockPrescaler == ADC_CLOCK_SYNC_PCLK_DIV4)
        && (handle->Init.Resolution == resolution)
        && (handle->Init.DataAlign == ADC_DATAALIGN_RIGHT)
        && (handle->Init.ScanConvMode == scan_conv_mode)
        && (handle->Init.EOCSelection == ADC_EOC_SINGLE_CONV)
        && (handle->Init.LowPowerAutoWait == DISABLE)
        && (handle->Init.ContinuousConvMode == continuous_conv)
        && (handle->Init.NbrOfConversion == conversions)
        && (handle->Init.DiscontinuousConvMode == DISABLE)
        && (handle->Init.NbrOfDiscConversion == 1U)
        && (handle->Init.ExternalTrigConv == trigger)
        && (handle->Init.ExternalTrigConvEdge == trigger_edge)
        && (handle->Init.SamplingMode == ADC_SAMPLING_MODE_NORMAL)
        && (handle->Init.DMAContinuousRequests == dma_requests)
        && (handle->Init.Overrun == ADC_OVR_DATA_OVERWRITTEN)
        && (handle->Init.OversamplingMode == DISABLE);

    if (!same) {
        /* Configure the ADC conversion mode */
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
            priv->initialized = MR_FALSE;
            priv->calibrated = MR_FALSE;
            return ret;
        }
        priv->initialized = MR_TRUE;
    }

    if (!priv->calibrated) {
        /* Calibrate the supported single-ended input mode */
        ret = mr_stm32h5_hal_status(
            HAL_ADCEx_Calibration_Start(handle, ADC_SINGLE_ENDED));
        if (ret != 0) {
            return ret;
        }
        priv->calibrated = MR_TRUE;
    }
    return 0;
}

static mr_err_t adc_init(adc_priv_t *priv, uint32_t resolution) {
    return adc_init_mode(priv, resolution, ADC_SCAN_DISABLE, DISABLE, DISABLE,
                         1U, ADC_SOFTWARE_START, ADC_EXTERNALTRIGCONVEDGE_NONE);
}

#if defined(MR_USE_DRIVER_ADC_STREAM) && defined(MR_USE_DRIVER_DMA)            \
    && defined(MR_USE_DRIVER_TMR)
static mr_err_t adc_init_stream(adc_priv_t *priv, uint32_t resolution,
                                mr_uint32_t conversions, mr_bool_t circular) {
    return adc_init_mode(priv, resolution, ADC_SCAN_ENABLE, DISABLE,
                         circular ? ENABLE : DISABLE, conversions,
                         priv->trigger, ADC_EXTERNALTRIGCONVEDGE_RISING);
}
#endif /* ADC stream + DMA + TMR */

static mr_err_t adc_gpio_configure(adc_channel_t *channel) {
    GPIO_InitTypeDef gpio_init = {0};
    mr_err_t ret;

    if (!channel->gpio_ready) {
        if ((channel->port == MR_NULL) || (channel->pin == 0U)
            || ((channel->pin & (channel->pin - 1U)) != 0U)) {
            return -MR_EINVAL;
        }
    }

    ret = mr_stm32h5_gpio_clock_enable(channel->port);
    if (ret != 0) {
        return ret;
    }
    if (channel->gpio_ready) {
        return 0;
    }

    /* Set the analog input pin to analog mode with no pull */
    gpio_init.Pin = channel->pin;
    gpio_init.Mode = GPIO_MODE_ANALOG;
    gpio_init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(channel->port, &gpio_init);
    channel->gpio_ready = MR_TRUE;
    return 0;
}

static mr_err_t adc_configure_hw_channel(adc_priv_t *priv,
                                         const adc_channel_t *channel,
                                         mr_uint32_t sample_time,
                                         uint32_t rank) {
    ADC_ChannelConfTypeDef config = {0};

    if (sample_time >= MR_ARRAY_SIZE(__adc_sample_time)) {
        return -MR_EINVAL;
    }

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
#if defined(MR_USE_DRIVER_ADC_STREAM) && defined(MR_USE_DRIVER_DMA)            \
    && defined(MR_USE_DRIVER_TMR)
    mr_bool_t busy;
#endif /* ADC stream + DMA + TMR */

    /* Check parameter */
    if (channel >= priv->channels_num) {
        return -MR_EINVAL;
    }
    adc_channel = &priv->channels[channel];

#if defined(MR_USE_DRIVER_ADC_STREAM) && defined(MR_USE_DRIVER_DMA)            \
    && defined(MR_USE_DRIVER_TMR)
    /* Do not alter ADC ranks while a stream is active */
    ret = adc_stream_is_busy(drv, &busy);
    if (ret != 0) {
        return ret;
    }
    if (busy) {
        return -MR_EBUSY;
    }
#endif /* ADC stream + DMA + TMR */

    /* Check supported channel configuration */
    if ((config->diff != MR_ADC_DIFF_SINGLE) || (config->ref != priv->ref)
        || (config->gain != MR_ADC_GAIN_1)) {
        return -MR_EINVAL;
    }

    /* Initialize the ADC and configure the GPIO */
    ret = adc_init(priv, ADC_RESOLUTION_12B);
    if (ret != 0) {
        return ret;
    }
    ret = adc_gpio_configure(adc_channel);
    if (ret != 0) {
        return ret;
    }

    /* Apply the single-ended channel configuration */
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

    /* Get channel configuration */
    *config = priv->channels[channel].config;
    return 0;
}

static mr_err_t adc_convert(adc_priv_t *priv, adc_channel_t *channel,
                            mr_uint32_t sample_time, mr_uint16_t *value) {
    mr_err_t ret;

    ret = adc_gpio_configure(channel);
    if (ret != 0) {
        return ret;
    }
    ret = adc_configure_hw_channel(priv, channel, sample_time,
                                   ADC_REGULAR_RANK_1);
    if (ret != 0) {
        return ret;
    }

    /* Start one conversion and wait for its result */
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

    /* Read the value and disable the ADC before changing channels */
    *value = (mr_uint16_t)HAL_ADC_GetValue(priv->handle);
    return mr_stm32h5_hal_status(HAL_ADC_Stop(priv->handle));
}

#if defined(MR_USE_DRIVER_ADC_STREAM) && defined(MR_USE_DRIVER_DMA)            \
    && defined(MR_USE_DRIVER_TMR)
static void adc_stream_halt_trigger(adc_priv_t *priv) {
    if (priv->timer != MR_NULL) {
        (void)mr_tmr_stop(priv->timer);
    }
    if (LL_ADC_REG_IsConversionOngoing(priv->handle->Instance)) {
        LL_ADC_REG_StopConversion(priv->handle->Instance);
    }
}

static void adc_dma_event(mr_waiter_t *waiter, void *param,
                          mr_wait_event_t event, const void *data) {
    adc_priv_t *priv = (adc_priv_t *)param;
    mr_wait_event_t adc_event = 0U;

    MR_IGNORE(waiter);
    MR_IGNORE(data);

    /* Translate DMA events while the ADC stream is active */
    if ((mr_atomic_load(&priv->stream_active) == 0)
        || (priv->stream_driver == MR_NULL)) {
        return;
    }
    if (event & MR_DMA_WAIT_ERROR) {
        /* Stop the trigger; defer DMA/waiter cleanup out of IRQ */
        adc_stream_halt_trigger(priv);
        if (priv->event_mask & MR_ADC_WAIT_STREAM_ERROR) {
            (void)mr_adc_event_raise(priv->stream_driver,
                                     MR_ADC_WAIT_STREAM_ERROR);
        }
        return;
    }
    if ((event & MR_DMA_WAIT_HALF)
        && (priv->event_mask & MR_ADC_WAIT_STREAM_HALF)) {
        adc_event |= MR_ADC_WAIT_STREAM_HALF;
    }
    if (event & MR_DMA_WAIT_DONE) {
        if (priv->event_mask & MR_ADC_WAIT_STREAM_FULL) {
            adc_event |= MR_ADC_WAIT_STREAM_FULL;
        }
        if (!priv->stream_circular) {
            /* Keep stream_active until the consumer runs stream_stop */
            adc_stream_halt_trigger(priv);
        }
    }
    if (adc_event != 0U) {
        (void)mr_adc_event_raise(priv->stream_driver, adc_event);
    }
}

static void adc_dma_waiter_release(adc_priv_t *priv) {
    if (priv->dma_waiter_initialized) {
        (void)mr_waiter_del(&priv->dma_waiter);
        priv->dma_waiter_initialized = MR_FALSE;
    }
}

static mr_err_t adc_seq_ranks(const adc_priv_t *priv, mr_uint32_t channels,
                              mr_uint8_t *index, mr_uint32_t *count) {
    mr_uint32_t channel;
    mr_uint32_t selected = 0U;

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
    *count = selected;
    return 0;
}

static mr_err_t adc_stream_stop(const mr_driver_t *drv) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t first_error = 0;
    mr_err_t ret;

    /* Stop the trigger source before stopping the data path */
    if (priv->timer != MR_NULL) {
        ret = mr_tmr_stop(priv->timer);
        if ((ret != 0) && (first_error == 0)) {
            first_error = ret;
        }
    }

    if (mr_atomic_load(&priv->stream_active) != 0) {
        /* Stop/recover DMA even when HAL has put the channel in ERROR */
        if (priv->dma.drv == MR_NULL) {
            if (first_error == 0) {
                first_error = -MR_ENODEV;
            }
        } else {
            ret = mr_dma_stop(priv->dma.drv, priv->dma.channel);
            if ((ret != 0) && (first_error == 0)) {
                first_error = ret;
            }
        }

        /* Stop ADC conversion before releasing its DMA waiter */
        ret = mr_stm32h5_hal_status(HAL_ADC_Stop(priv->handle));
        if ((ret != 0) && (first_error == 0)) {
            first_error = ret;
        }

        /* Keep ownership and the waiter until cleanup succeeds */
        if (first_error != 0) {
            return first_error;
        }
        mr_atomic_store(&priv->stream_active, 0);
    }
    if (priv->initialized) {
        CLEAR_BIT(priv->handle->Instance->CFGR, ADC_CFGR_DMAEN);
    }
    adc_dma_waiter_release(priv);
    priv->stream_driver = MR_NULL;
    return first_error;
}

static mr_err_t adc_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;

    /* An idle ADC stream does not require an initialized DMA channel */
    if (mr_atomic_load(&priv->stream_active) == 0) {
        *busy = MR_FALSE;
        return 0;
    }
    if (priv->dma.drv == MR_NULL) {
        return -MR_ENODEV;
    }

    /* Get DMA transfer state */
    ret = mr_dma_is_busy(priv->dma.drv, priv->dma.channel, busy);
    if (ret != 0) {
        return ret;
    }

    /* Stop the ADC after a finite transfer has completed */
    if ((!*busy) && (mr_atomic_load(&priv->stream_active) != 0)) {
        ret = adc_stream_stop(drv);
        if (ret != 0) {
            return ret;
        }
    }
    return 0;
}

static mr_err_t adc_stream_offset(const mr_driver_t *drv, mr_size_t *offset) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Get the DMA transfer offset */
    if (priv->dma.drv == MR_NULL) {
        return -MR_ENODEV;
    }
    return mr_dma_offset(priv->dma.drv, priv->dma.channel, offset);
}

static mr_err_t adc_stream_start(const mr_driver_t *drv, mr_adc_seq_t *seq,
                                 const mr_adc_stream_config_t *config) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_tmr_config_t timer_config;
    mr_dma_config_t dma_config = {
        .src_width = MR_DMA_WIDTH_16,
        .dst_width = MR_DMA_WIDTH_16,
        .src_adj = MR_DMA_ADJ_CONST,
        .dst_adj = MR_DMA_ADJ_INC,
        .src_burst = MR_DMA_BURST_1,
        .dst_burst = MR_DMA_BURST_1,
        .direction = MR_DMA_DIR_P2M,
    };
    mr_uint8_t ranks[MR_ARRAY_SIZE(__adc_regular_rank)];
    mr_uint64_t period;
    mr_uint32_t channels_num;
    mr_uint32_t resolution;
    mr_uint32_t rank;
    mr_size_t sample_count;
    mr_bool_t busy;
    mr_err_t ret;

    /* Check stream parameters */
    if ((priv->dma.drv == MR_NULL) || (priv->timer == MR_NULL)) {
        return -MR_ENODEV;
    }
    if ((config->sample_rate == 0U) || (seq->buf == MR_NULL)
        || (seq->size == 0U) || ((seq->size % sizeof(mr_uint16_t)) != 0U)
        || (((mr_uintptr_t)seq->buf % sizeof(mr_uint16_t)) != 0U)) {
        return -MR_EINVAL;
    }
    period = ADC_NANOSECONDS / config->sample_rate;
    if (period == 0U) {
        return -MR_ERANGE;
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
    if ((sample_count < (channels_num * 2U))
        || ((sample_count % (channels_num * 2U)) != 0U)) {
        return -MR_EINVAL;
    }

    /* Reject a running transfer and clean up an already completed stream */
    ret = adc_stream_is_busy(drv, &busy);
    if (ret != 0) {
        return ret;
    }
    if (busy) {
        return -MR_EBUSY;
    }

    /* Configure the timer before arming ADC; UG can generate a trigger */
    timer_config.period = period;
    timer_config.signal = MR_TMR_SIGNAL_UPDATE;
    timer_config.mode = MR_TMR_MODE_PERIODIC;
    ret = mr_tmr_configure(priv->timer, &timer_config);
    if (ret != 0) {
        return ret;
    }

    /* Configure ADC scan ranks in logical channel order */
    ret = adc_init_stream(priv, resolution, channels_num, config->circular);
    if (ret != 0) {
        return ret;
    }
    for (rank = 0U; rank < channels_num; rank++) {
        adc_channel_t *adc_channel = &priv->channels[ranks[rank]];

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

    /* Configure DMA for ADC data register to memory transfers */
    CLEAR_BIT(priv->handle->Instance->CFGR, ADC_CFGR_DMAEN);
    ret = mr_dma_configure(priv->dma.drv, priv->dma.channel, &dma_config);
    if (ret != 0) {
        return ret;
    }

    /* Attach a fresh waiter so it can be safely released on stream stop */
    priv->stream_driver = (mr_driver_t *)drv;
    ret = mr_waiter_init(&priv->dma_waiter, adc_dma_event, priv,
                         MR_DMA_WAIT_HALF | MR_DMA_WAIT_DONE
                             | MR_DMA_WAIT_ERROR);
    if (ret != 0) {
        priv->stream_driver = MR_NULL;
        return ret;
    }
    priv->dma_waiter_initialized = MR_TRUE;
    ret = mr_dma_event_attach(priv->dma.drv, priv->dma.channel,
                              &priv->dma_waiter);
    if (ret != 0) {
        (void)adc_stream_stop(drv);
        return ret;
    }
    ret = mr_dma_event_enable(priv->dma.drv, priv->dma.channel,
                              MR_DMA_WAIT_HALF | MR_DMA_WAIT_DONE
                                  | MR_DMA_WAIT_ERROR);
    if (ret != 0) {
        (void)adc_stream_stop(drv);
        return ret;
    }

    /* Start DMA before enabling ADC conversion requests */
    priv->transfer.next = config->circular ? &priv->transfer : MR_NULL;
    priv->transfer.src = (void *)&priv->handle->Instance->DR;
    priv->transfer.dst = seq->buf;
    priv->transfer.count = seq->size;
    priv->stream_circular = (config->circular != MR_FALSE);
    ret = mr_dma_start(priv->dma.drv, priv->dma.channel, &priv->transfer);
    if (ret != 0) {
        (void)adc_stream_stop(drv);
        return ret;
    }
    SET_BIT(priv->handle->Instance->CFGR, ADC_CFGR_DMAEN);
    mr_atomic_store(&priv->stream_active, 1);

    /* Arm the ADC before enabling the hardware trigger */
    ret = mr_stm32h5_hal_status(HAL_ADC_Start(priv->handle));
    if (ret != 0) {
        (void)adc_stream_stop(drv);
        return ret;
    }
    ret = mr_tmr_start(priv->timer);
    if (ret != 0) {
        (void)adc_stream_stop(drv);
        return ret;
    }
    return 0;
}
#endif /* ADC stream + DMA + TMR */

static mr_ssize_t adc_read(const mr_driver_t *drv, mr_adc_seq_t *seq) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint16_t *buf;
    mr_uint32_t channel;
    mr_uint32_t resolution;
    mr_size_t count = 0U;
    mr_err_t ret;
#if defined(MR_USE_DRIVER_ADC_STREAM) && defined(MR_USE_DRIVER_DMA)            \
    && defined(MR_USE_DRIVER_TMR)
    mr_bool_t busy;
#endif /* ADC stream + DMA + TMR */

#if defined(MR_USE_DRIVER_ADC_STREAM) && defined(MR_USE_DRIVER_DMA)            \
    && defined(MR_USE_DRIVER_TMR)
    /* Do not convert while a stream is active */
    ret = adc_stream_is_busy(drv, &busy);
    if (ret != 0) {
        return ret;
    }
    if (busy) {
        return -MR_EBUSY;
    }
#endif /* ADC stream + DMA + TMR */

    /* Check supported resolution and oversample */
    ret = adc_resolution(seq->resolution, &resolution);
    if (ret != 0) {
        return ret;
    }
    if (seq->oversample >= MR_ARRAY_SIZE(__adc_sample_time)) {
        return -MR_EINVAL;
    }

    /* No channels or no space for one sample */
    if ((seq->channels == 0U) || (seq->size < sizeof(mr_uint16_t))) {
        return 0;
    }

    ret = adc_init(priv, resolution);
    if (ret != 0) {
        return ret;
    }

    /* Read enabled channels in logical channel order */
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

static mr_err_t adc_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable) {
    adc_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check parameter */
    if ((events == 0U) || ((events & ~ADC_EVENT_MASK) != 0U)) {
        return -MR_EINVAL;
    }
#if defined(MR_USE_DRIVER_ADC_STREAM) && defined(MR_USE_DRIVER_DMA)            \
    && defined(MR_USE_DRIVER_TMR)
    if (!enable && (mr_atomic_load(&priv->stream_active) != 0)) {
        if ((events & MR_ADC_WAIT_STREAM_ERROR) != 0U) {
            return -MR_EBUSY;
        }
        if (((events & MR_ADC_WAIT_STREAM_FULL) != 0U)
            && !priv->stream_circular) {
            return -MR_EBUSY;
        }
    }
#endif /* ADC stream + DMA + TMR */
    if (enable) {
        priv->event_mask |= events;
    } else {
        priv->event_mask &= ~events;
    }
    return 0;
}

/* Instance install */
#define __ST_HT_ADC_CHANNEL_IMPL(_elem, _node, _prop)                          \
    {                                                                          \
        .channel = MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 0),               \
        .port = MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 1),                  \
        .pin = MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 2),                   \
        .config = {.diff = MR_ADC_DIFF_SINGLE,                                 \
                   .ref = MR_HT_NODE_PROP(_node, st_ref),                      \
                   .gain = MR_ADC_GAIN_1},                                     \
}
#if defined(MR_USE_DRIVER_ADC_STREAM) && defined(MR_USE_DRIVER_DMA)            \
    && defined(MR_USE_DRIVER_TMR)
#define __ST_HT_ADC_DMA_SPEC(_id)                                              \
    .dma = MR_HT_DMA_SPEC(_id, st_dma),                                        \
    .timer = MR_HT_DRIVER_FIND(MR_HT_DRIVER_PROP(_id, st_timer)),              \
    .trigger = MR_HT_DRIVER_PROP(_id, st_trigger),                             \
    .stream_active = MR_ATOMIC_INIT(0),
#else
#define __ST_HT_ADC_DMA_SPEC(_id)
#endif /* ADC stream + DMA + TMR */
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
           __ST_HT_ADC_DMA_SPEC(_id)};                                         \
    MR_HT_ADC_INSTALL(_id, &MR_CAT(__adc_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(ST_HT_ADC_INSTALL, &__adc_ops);
#endif /* defined(MR_USE_DRIVER_ADC) */

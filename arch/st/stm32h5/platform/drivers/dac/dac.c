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
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) && defined(MR_USE_DRIVER_DMA) */
#include "stm32h5xx_hal_dac.h"

/* HardTree compatible */
#define MR_HT_COMPAT                    st_stm32h5_dac
#if MR_HT_HAS_DRIVER()

/* Driver DAC operations */
static mr_err_t dac_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_dac_config_t *config);
static mr_err_t dac_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_dac_config_t *config);
static mr_ssize_t dac_write(const mr_driver_t *drv, mr_dac_seq_t *seq);
#if defined(MR_USE_DRIVER_DAC_STREAM)
static mr_err_t dac_stream_start(const mr_driver_t *drv, mr_dac_seq_t *seq,
                                 const mr_dac_stream_config_t *config);
static mr_err_t dac_stream_stop(const mr_driver_t *drv);
static mr_err_t dac_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy);
static mr_err_t dac_stream_offset(const mr_driver_t *drv, mr_size_t *offset);
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */
static mr_err_t dac_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable);
MR_UNUSED static const mr_dac_ops_t __dac_ops = {
    .channel_configure = dac_channel_configure,
    .channel_configuration = dac_channel_configuration,
    .write = dac_write,
#if defined(MR_USE_DRIVER_DAC_STREAM)
    .stream_start = dac_stream_start,
    .stream_stop = dac_stream_stop,
    .stream_is_busy = dac_stream_is_busy,
    .stream_offset = dac_stream_offset,
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */
    .event_control = dac_event_control,
};

/* DAC channel type */
typedef struct dac_channel {
    uint32_t channel;
    mr_ht_gpio_spec_t gpio;
} dac_channel_t;

#if defined(MR_USE_DRIVER_DAC_STREAM)
#if defined(MR_USE_DRIVER_DMA)
/* DAC DMA phase definition */
typedef enum dac_dma_phase {
    DAC_DMA_IDLE,
    DAC_DMA_WAITER_READY,
    DAC_DMA_EVENTS_ENABLED,
    DAC_DMA_STARTED,
} dac_dma_phase_t;

/* DAC stream DMA type */
typedef struct dac_stream_dma {
    mr_ht_dma_spec_t spec;
    mr_dma_transfer_t xfer;
    mr_waiter_t waiter;
    dac_dma_phase_t phase;
} dac_stream_dma_t;
#endif /* defined(MR_USE_DRIVER_DMA) */

/* DAC stream type */
typedef struct dac_stream {
#if defined(MR_USE_DRIVER_DMA)
    dac_stream_dma_t *dma;
#endif /* defined(MR_USE_DRIVER_DMA) */
    mr_driver_t *drv;
    uint32_t trigger;
    IRQn_Type irqn;
    mr_uint32_t irq_priority;
    mr_atomic_t event_state;
    mr_size_t size;
    mr_size_t offset;
    mr_size_t prefill;
    mr_uint8_t irq_valid: 1;
    mr_uint8_t trigger_valid: 1;
    mr_uint8_t use_dma: 1;
    mr_uint8_t half_enabled: 1;
    mr_uint8_t circular: 1;
    mr_uint8_t channel: 1;
} dac_stream_t;
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */

/* DAC private data type */
typedef struct dac_priv {
    DAC_HandleTypeDef hdac;
    void (*clk_enable)(void);
    const dac_channel_t *channels;
    mr_size_t channels_num;
    mr_uint8_t ref;
    mr_uint8_t started_mask;
#if defined(MR_USE_DRIVER_DAC_STREAM)
    dac_stream_t stream;
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */
} dac_priv_t;

/* DAC channel limit definition */
#define DAC_CHANNEL_MAX                 (2U)

/* DAC event mask definition */
#define DAC_EVENT_MASK                                                         \
    (MR_DAC_WAIT_STREAM_HALF | MR_DAC_WAIT_STREAM_DONE                         \
     | MR_DAC_WAIT_STREAM_ERROR)

#if defined(MR_USE_DRIVER_DAC_STREAM)
/* DAC stream state definition */
#define DAC_STREAM_COMPLETE             (1U << 3)
#define DAC_STREAM_DRAINING             (1U << 4)
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */

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
    if ((priv->channels_num == 0U) || (priv->channels_num > DAC_CHANNEL_MAX)) {
        return 0U;
    }
    return (1UL << priv->channels_num) - 1U;
}

#if defined(MR_USE_DRIVER_DAC_STREAM)
static uint32_t dac_dmaen(const dac_priv_t *priv, mr_uint32_t channel) {
    if (priv->channels[channel].channel == DAC_CHANNEL_1) {
        return DAC_CR_DMAEN1;
    }
#if defined(DAC_CHANNEL_2)
    if (priv->channels[channel].channel == DAC_CHANNEL_2) {
        return DAC_CR_DMAEN2;
    }
#endif /* defined(DAC_CHANNEL_2) */
    return 0U;
}

static uint32_t dac_ten(const dac_priv_t *priv, mr_uint32_t channel) {
    if (priv->channels[channel].channel == DAC_CHANNEL_1) {
        return DAC_CR_TEN1;
    }
#if defined(DAC_CHANNEL_2)
    if (priv->channels[channel].channel == DAC_CHANNEL_2) {
        return DAC_CR_TEN2;
    }
#endif /* defined(DAC_CHANNEL_2) */
    return 0U;
}

static uint32_t dac_dmaudrie(const dac_priv_t *priv, mr_uint32_t channel) {
    if (priv->channels[channel].channel == DAC_CHANNEL_1) {
        return DAC_IT_DMAUDR1;
    }
#if defined(DAC_CHANNEL_2)
    if (priv->channels[channel].channel == DAC_CHANNEL_2) {
        return DAC_IT_DMAUDR2;
    }
#endif /* defined(DAC_CHANNEL_2) */
    return 0U;
}

static uint32_t dac_dmaudr(const dac_priv_t *priv, mr_uint32_t channel) {
    if (priv->channels[channel].channel == DAC_CHANNEL_1) {
        return DAC_FLAG_DMAUDR1;
    }
#if defined(DAC_CHANNEL_2)
    if (priv->channels[channel].channel == DAC_CHANNEL_2) {
        return DAC_FLAG_DMAUDR2;
    }
#endif /* defined(DAC_CHANNEL_2) */
    return 0U;
}
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */

static mr_err_t dac_hw_init(dac_priv_t *priv) {
    mr_size_t i, j;

    /* Check instance */
    if ((priv->clk_enable == MR_NULL) || (priv->channels == MR_NULL)
        || (priv->channels_num == 0U)
        || (priv->channels_num > DAC_CHANNEL_MAX)) {
        return -MR_ENODEV;
    }
    for (i = 0U; i < priv->channels_num; i++) {
        if (!dac_hal_channel_valid(priv->channels[i].channel)) {
            return -MR_EINVAL;
        }
        for (j = 0U; j < i; j++) {
            if (priv->channels[i].channel == priv->channels[j].channel) {
                return -MR_EINVAL;
            }
        }
    }
    priv->clk_enable();

    /* Initialize DAC */
    return mr_stm32h5_hal_status(HAL_DAC_Init(&priv->hdac));
}

static mr_err_t dac_gpio_configure(dac_priv_t *priv, mr_uint32_t channel) {
    const dac_channel_t *hw;
    mr_gpio_config_t cfg;

    hw = &priv->channels[channel];
    cfg = (mr_gpio_config_t){0};

    /* Check GPIO */
    if (hw->gpio.drv == MR_NULL) {
        return -MR_ENODEV;
    }

    /* Configure GPIO */
    cfg.mode = MR_GPIO_MODE_ANALOG;
    cfg.pull = MR_GPIO_PULL_NONE;
    return mr_gpio_configure(hw->gpio.drv, hw->gpio.pin, &cfg);
}

static mr_err_t dac_config_trigger(dac_priv_t *priv, mr_uint32_t channel,
                                   uint32_t trigger) {
    DAC_ChannelConfTypeDef dac_config;
    mr_uint8_t bit;
    mr_err_t ret;

    dac_config = (DAC_ChannelConfTypeDef){0};
    bit = (mr_uint8_t)(1U << channel);
    ret = dac_gpio_configure(priv, channel);
    if (ret != 0) {
        return ret;
    }

    /* Stop channel */
    if (priv->started_mask & bit) {
        ret = mr_stm32h5_hal_status(
            HAL_DAC_Stop(&priv->hdac, priv->channels[channel].channel));
        if (ret != 0) {
            return ret;
        }
        priv->started_mask &= (mr_uint8_t)~bit;
    }

    /* Apply the selected trigger and analog output configuration */
    dac_config.DAC_HighFrequency = DAC_HIGH_FREQUENCY_INTERFACE_MODE_AUTOMATIC;
    dac_config.DAC_DMADoubleDataMode = DISABLE;
    dac_config.DAC_SignedFormat = DISABLE;
    dac_config.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
    dac_config.DAC_Trigger = trigger;
    dac_config.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
    dac_config.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_EXTERNAL;
    dac_config.DAC_UserTrimming = DAC_TRIMMING_FACTORY;
    return mr_stm32h5_hal_status(HAL_DAC_ConfigChannel(
        &priv->hdac, &dac_config, priv->channels[channel].channel));
}

static mr_err_t dac_channel_init(const mr_driver_t *drv, mr_uint32_t channel) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;

    /* Initialize channel */
    ret = dac_hw_init(priv);
    if (ret != 0) {
        return ret;
    }
    if (channel >= priv->channels_num) {
        return -MR_EINVAL;
    }
    return dac_config_trigger(priv, channel, DAC_TRIGGER_NONE);
}

static mr_err_t dac_seq_channels(const dac_priv_t *priv, mr_uint32_t channels,
                                 mr_uint8_t *index, mr_uint32_t *count) {
    mr_uint32_t channel, selected;

    /* Validate the selected channels before constructing their order */
    selected = 0U;
    if ((channels == 0U) || ((channels & ~dac_channel_mask(priv)) != 0U)) {
        return -MR_EINVAL;
    }

    /* Get channel ranks */
    for (channel = 0U; channel < priv->channels_num; channel++) {
        if ((channels & (1U << channel)) != 0U) {
            index[selected] = (mr_uint8_t)channel;
            selected++;
        }
    }
    *count = selected;
    return 0;
}

#if defined(MR_USE_DRIVER_DAC_STREAM)
#if defined(MR_USE_DRIVER_DMA)
static volatile uint32_t *dac_dhr12r(DAC_TypeDef *instance, uint32_t channel) {
    /* Get data register */
    if (channel == DAC_CHANNEL_1) {
        return &instance->DHR12R1;
    }
#if defined(DAC_CHANNEL_2)
    if (channel == DAC_CHANNEL_2) {
        return &instance->DHR12R2;
    }
#endif /* defined(DAC_CHANNEL_2) */
    return MR_NULL;
}
#endif /* defined(MR_USE_DRIVER_DMA) */

static void dac_stream_notify(dac_priv_t *priv, mr_wait_event_t events) {
    /* Forward only events enabled by the caller */
    events &= (mr_wait_event_t)mr_atomic_load(&priv->stream.event_state);
    if (events != 0U) {
        mr_dac_event_raise(priv->stream.drv, events);
    }
}

static void dac_stream_halt(dac_priv_t *priv) {
    /* Mask the underrun interrupt and stop new DMA and trigger requests */
    __HAL_DAC_DISABLE_IT(&priv->hdac, dac_dmaudrie(priv, priv->stream.channel));
    CLEAR_BIT(priv->hdac.Instance->CR,
              dac_dmaen(priv, priv->stream.channel)
                  | dac_ten(priv, priv->stream.channel));
}

#if defined(MR_USE_DRIVER_DMA)
static void dac_dma_event(mr_waiter_t *waiter, void *param,
                          mr_wait_event_t event, const void *data) {
    mr_wait_event_t dac_event;
    dac_priv_t *priv;

    MR_IGNORE(waiter);
    MR_IGNORE(data);

    /* Ignore events after stream ownership has been released */
    priv = (dac_priv_t *)param;
    if ((priv->stream.drv == MR_NULL)
        || ((mr_atomic_load(&priv->stream.event_state) & DAC_STREAM_COMPLETE)
            != 0U)) {
        return;
    }

    /* Stop requests on DMA error before notifying subscribers */
    if (event & MR_DMA_WAIT_ERROR) {
        dac_stream_halt(priv);
        mr_atomic_fetch_or(&priv->stream.event_state, DAC_STREAM_COMPLETE);
        dac_stream_notify(priv, MR_DAC_WAIT_STREAM_ERROR);
        return;
    }

    dac_event = 0U;
    if ((event & MR_DMA_WAIT_HALF) && priv->stream.half_enabled) {
        dac_event |= MR_DAC_WAIT_STREAM_HALF;
    }
    if (event & MR_DMA_WAIT_DONE) {
        if (priv->stream.circular) {
            dac_event |= MR_DAC_WAIT_STREAM_DONE;
        } else {
            /* DMA completion leaves the final sample in DHR */
            mr_atomic_fetch_or(&priv->stream.event_state, DAC_STREAM_DRAINING);
        }
    }
    dac_stream_notify(priv, dac_event);
}

static mr_wait_event_t dac_stream_dma_events(const dac_stream_t *stream) {
    return MR_DMA_WAIT_DONE | MR_DMA_WAIT_ERROR
         | (stream->half_enabled ? MR_DMA_WAIT_HALF : 0U);
}

static mr_err_t dac_stream_start_dma(dac_priv_t *priv, const mr_dac_seq_t *seq) {
    static const mr_dma_config_t config = {
        .src_width = MR_DMA_WIDTH_16,
        .dst_width = MR_DMA_WIDTH_32,
        .src_adj = MR_DMA_ADJ_INC,
        .dst_adj = MR_DMA_ADJ_CONST,
        .src_burst = MR_DMA_BURST_1,
        .dst_burst = MR_DMA_BURST_1,
        .direction = MR_DMA_DIR_M2P,
    };
    mr_wait_event_t events;
    dac_stream_dma_t *dma;
    dac_stream_t *stream;
    mr_err_t ret;

    stream = &priv->stream;
    dma = stream->dma;
    dma->xfer.count = seq->size - stream->prefill;
    if (dma->xfer.count == 0U) {
        /* A preloaded single sample needs only the DAC underrun IRQ */
        mr_atomic_fetch_or(&stream->event_state, DAC_STREAM_DRAINING);
        return 0;
    }

    /* Prepare DMA while the DAC channel cannot issue requests */
    dma->xfer.next = stream->circular ? &dma->xfer : MR_NULL;
    dma->xfer.src = (const mr_uint8_t *)seq->buf + stream->prefill;
    dma->xfer.dst = (void *)dac_dhr12r(
        priv->hdac.Instance, priv->channels[stream->channel].channel);
    if (dma->xfer.dst == MR_NULL) {
        return -MR_EINVAL;
    }
    ret = mr_dma_configure(dma->spec.drv, dma->spec.channel, &config);
    if (ret != 0) {
        return ret;
    }

    /* Subscribe before starting the transfer */
    events = dac_stream_dma_events(stream);
    ret = mr_waiter_init(&dma->waiter, dac_dma_event, priv, events);
    if (ret != 0) {
        return ret;
    }
    dma->phase = DAC_DMA_WAITER_READY;
    ret = mr_dma_event_attach(dma->spec.drv, dma->spec.channel,
                              &dma->waiter);
    if (ret != 0) {
        return ret;
    }
    ret = mr_dma_event_enable(dma->spec.drv, dma->spec.channel, events);
    if (ret != 0) {
        return ret;
    }
    dma->phase = DAC_DMA_EVENTS_ENABLED;

    ret = mr_dma_start(dma->spec.drv, dma->spec.channel, &dma->xfer);
    if (ret != 0) {
        return ret;
    }
    dma->phase = DAC_DMA_STARTED;
    return 0;
}
#endif /* defined(MR_USE_DRIVER_DMA) */

MR_UNUSED static void dac_irq(dac_priv_t *priv) {
    uint32_t flag, interrupt;
    mr_wait_event_t event;

    /* Check the active finite stream and its underrun interrupt */
    if ((priv->stream.drv == MR_NULL)
        || ((mr_atomic_load(&priv->stream.event_state) & DAC_STREAM_COMPLETE)
            != 0U)
        || priv->stream.circular) {
        return;
    }
    flag = dac_dmaudr(priv, priv->stream.channel);
    interrupt = dac_dmaudrie(priv, priv->stream.channel);
    if ((flag == 0U) || (interrupt == 0U)
        || (__HAL_DAC_GET_FLAG(&priv->hdac, flag) == 0U)
        || (__HAL_DAC_GET_IT_SOURCE(&priv->hdac, interrupt) == 0U)) {
        return;
    }

    /* Disarm the channel before publishing its final offset */
    __HAL_DAC_CLEAR_FLAG(&priv->hdac, flag);
    dac_stream_halt(priv);
    priv->stream.offset = priv->stream.size;
    mr_atomic_fetch_or(&priv->stream.event_state, DAC_STREAM_COMPLETE);

    /* Underrun confirms the final trigger only after DMA has drained */
    event = ((mr_atomic_load(&priv->stream.event_state) & DAC_STREAM_DRAINING)
             != 0U) ? MR_DAC_WAIT_STREAM_DONE : MR_DAC_WAIT_STREAM_ERROR;
    dac_stream_notify(priv, event);
}

static mr_err_t dac_stream_stop(const mr_driver_t *drv) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    dac_stream_t *stream;
#if defined(MR_USE_DRIVER_DMA)
    dac_stream_dma_t *dma;
#endif /* defined(MR_USE_DRIVER_DMA) */
    mr_uint8_t bit;
    mr_err_t ret;

    /* Check stream ownership */
    stream = &priv->stream;
    if (stream->drv == MR_NULL) {
        return 0;
    }

    /* Mask requests before stopping the DAC and releasing the source buffer */
    dac_stream_halt(priv);
    mr_atomic_fetch_or(&stream->event_state, DAC_STREAM_COMPLETE);
    __HAL_DAC_CLEAR_FLAG(&priv->hdac, dac_dmaudr(priv, stream->channel));
    bit = (mr_uint8_t)(1U << stream->channel);
    if (priv->started_mask & bit) {
        ret = mr_stm32h5_hal_status(HAL_DAC_Stop(
            &priv->hdac, priv->channels[stream->channel].channel));
        if (ret != 0) {
            return ret;
        }
        priv->started_mask &= (mr_uint8_t)~bit;
    }

#if defined(MR_USE_DRIVER_DMA)
    dma = stream->dma;
    /* Keep the waiter until DMA is stopped and its events are masked */
    if ((dma != MR_NULL) && (dma->phase == DAC_DMA_STARTED)) {
        ret = mr_dma_stop(dma->spec.drv, dma->spec.channel);
        if (ret != 0) {
            return ret;
        }
        dma->phase = DAC_DMA_EVENTS_ENABLED;
    }
    if ((dma != MR_NULL) && (dma->phase == DAC_DMA_EVENTS_ENABLED)) {
        ret = mr_dma_event_disable(dma->spec.drv, dma->spec.channel,
                                   dac_stream_dma_events(stream));
        if (ret != 0) {
            return ret;
        }
        dma->phase = DAC_DMA_WAITER_READY;
    }
    if ((dma != MR_NULL) && (dma->phase == DAC_DMA_WAITER_READY)) {
        mr_waiter_del(&dma->waiter);
        dma->phase = DAC_DMA_IDLE;
    }
#endif /* defined(MR_USE_DRIVER_DMA) */

    /* Release stream ownership only after cleanup succeeds */
    stream->drv = MR_NULL;
    return 0;
}

static mr_err_t dac_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check stream state */
    if (priv->stream.drv == MR_NULL) {
        *busy = MR_FALSE;
        return 0;
    }

    /* Finish cleanup before reporting an inactive stream */
    if ((mr_atomic_load(&priv->stream.event_state) & DAC_STREAM_COMPLETE)
        != 0U) {
        mr_err_t ret;

        ret = dac_stream_stop(drv);
        if (ret == 0) {
            *busy = MR_FALSE;
        }
        return ret;
    }
    if (!priv->stream.circular) {
        *busy = MR_TRUE;
        return 0;
    }

#if defined(MR_USE_DRIVER_DMA)
    {
        mr_err_t ret;

        if (!priv->stream.use_dma || (priv->stream.dma == MR_NULL)) {
            return -MR_ENODEV;
        }

        /* Check DMA state */
        ret = mr_dma_is_busy(priv->stream.dma->spec.drv,
                             priv->stream.dma->spec.channel,
                             busy);
        if (ret != 0) {
            return ret;
        }

        if (!*busy) {
            return dac_stream_stop(drv);
        }
        return 0;
    }
#else
    return -MR_ENODEV;
#endif /* defined(MR_USE_DRIVER_DMA) */
}

static mr_err_t dac_stream_offset(const mr_driver_t *drv, mr_size_t *offset) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Get stream offset */
#if defined(MR_USE_DRIVER_DMA)
    if (priv->stream.use_dma) {
        mr_err_t ret;

        if (priv->stream.dma == MR_NULL) {
            return -MR_ENODEV;
        }
        if (priv->stream.dma->xfer.count == 0U) {
            *offset = priv->stream.prefill;
            return 0;
        }
        ret = mr_dma_offset(priv->stream.dma->spec.drv,
                            priv->stream.dma->spec.channel,
                            offset);
        if (ret == 0) {
            *offset += priv->stream.prefill;
        }
        return ret;
    }
#endif /* defined(MR_USE_DRIVER_DMA) */

    *offset = priv->stream.offset;
    return 0;
}

static mr_err_t dac_stream_start(const mr_driver_t *drv, mr_dac_seq_t *seq,
                                 const mr_dac_stream_config_t *config) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    const mr_uint16_t *samples;
    mr_size_t sample_count, i;
    mr_bool_t busy, use_dma;
    dac_stream_t *stream;
    mr_uint32_t channel;
    mr_err_t ret;

    stream = &priv->stream;
    samples = seq->buf;

    /* Check trigger source */
    if (config->source != MR_DAC_STREAM_SOURCE_EXTERNAL) {
        return -MR_ENOSYS;
    }
#if defined(MR_USE_DRIVER_DMA)
    use_dma = ((stream->dma != MR_NULL) && (stream->dma->spec.drv != MR_NULL));
#else
    use_dma = MR_FALSE;
#endif /* defined(MR_USE_DRIVER_DMA) */

    /* Check stream resources and sample layout */
    if (!stream->trigger_valid || (!config->circular && !stream->irq_valid)) {
        return -MR_ENODEV;
    }
    if ((seq->buf == MR_NULL) || (seq->size == 0U) || (seq->resolution != 12U)
        || ((seq->size % sizeof(mr_uint16_t)) != 0U)
        || (((mr_uintptr_t)seq->buf % sizeof(mr_uint16_t)) != 0U)
        || (seq->channels == 0U)
        || ((seq->channels & ~dac_channel_mask(priv)) != 0U)
        || ((seq->channels & (seq->channels - 1U)) != 0U)) {
        return -MR_EINVAL;
    }
    sample_count = seq->size / sizeof(mr_uint16_t);
    if (!use_dma && ((sample_count != 1U) || config->circular)) {
        return -MR_ENOSYS;
    }
    for (i = 0U; i < sample_count; i++) {
        if (samples[i] > 0x0FFFU) {
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

    /* Find the single channel selected by the sequence */
    for (channel = 0U; channel < priv->channels_num; channel++) {
        if ((seq->channels & (1U << channel)) != 0U) {
            break;
        }
    }

    /* Configure the selected channel for the board's external trigger */
    ret = dac_hw_init(priv);
    if (ret != 0) {
        return ret;
    }
    ret = dac_config_trigger(priv, channel, stream->trigger);
    if (ret != 0) {
        return ret;
    }

    /* Publish stream state before enabling DMA or DAC interrupts */
    stream->channel = (mr_uint8_t)channel;
    stream->circular = (config->circular != MR_FALSE);

    stream->size = seq->size;
    stream->prefill = stream->circular ? 0U : sizeof(mr_uint16_t);
    stream->offset = stream->prefill;
    stream->use_dma = use_dma;
    stream->half_enabled = (stream->circular && ((sample_count % 2U) == 0U));
    mr_atomic_fetch_and(&stream->event_state,
                        ~(mr_atomic_t)(DAC_STREAM_COMPLETE
                                       | DAC_STREAM_DRAINING));
    if (!use_dma) {
        mr_atomic_fetch_or(&stream->event_state, DAC_STREAM_DRAINING);
    }
    stream->drv = (mr_driver_t *)drv;

    /* Preload the first sample so the first trigger has data to output */
    if (stream->prefill != 0U) {
        ret = mr_stm32h5_hal_status(
            HAL_DAC_SetValue(&priv->hdac, priv->channels[channel].channel,
                             DAC_ALIGN_12B_R, samples[0]));
        if (ret != 0) {
            goto _stop;
        }
    }

#if defined(MR_USE_DRIVER_DMA)
    if (use_dma) {
        ret = dac_stream_start_dma(priv, seq);
        if (ret != 0) {
            goto _stop;
        }
    }
#endif /* defined(MR_USE_DRIVER_DMA) */

    /* Finite streams use underrun to observe the final DHR-to-DOR transfer */
    __HAL_DAC_CLEAR_FLAG(&priv->hdac, dac_dmaudr(priv, channel));
    if (!stream->circular) {
        HAL_NVIC_SetPriority(stream->irqn, stream->irq_priority, 0U);
        HAL_NVIC_EnableIRQ(stream->irqn);
        __HAL_DAC_ENABLE_IT(&priv->hdac, dac_dmaudrie(priv, channel));
    }
    SET_BIT(priv->hdac.Instance->CR, dac_dmaen(priv, channel));

    /* Include a partially started DAC in rollback */
    priv->started_mask |= (mr_uint8_t)(1U << channel);
    ret = mr_stm32h5_hal_status(
        HAL_DAC_Start(&priv->hdac, priv->channels[channel].channel));
    if (ret == 0) {
        return 0;
    }

_stop:
    /* Preserve the start error and leave failed cleanup available for retry */
    dac_stream_stop(drv);
    return ret;
}
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */

static mr_err_t dac_channel_start(const mr_driver_t *drv, mr_uint32_t channel,
                                  mr_uint16_t value) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint8_t bit;
    mr_err_t ret;

    bit = (mr_uint8_t)(1U << channel);
    /* Initialize channel */
    ret = dac_channel_init(drv, channel);
    if (ret != 0) {
        return ret;
    }

    /* Set output value */
    ret = mr_stm32h5_hal_status(HAL_DAC_SetValue(
        &priv->hdac, priv->channels[channel].channel, DAC_ALIGN_12B_R, value));
    if (ret != 0) {
        return ret;
    }
    if (!(priv->started_mask & bit)) {
        ret = mr_stm32h5_hal_status(
            HAL_DAC_Start(&priv->hdac, priv->channels[channel].channel));
        if (ret != 0) {
            return ret;
        }
        priv->started_mask |= bit;
    }
    return 0;
}

static mr_err_t dac_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_dac_config_t *config) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
#if defined(MR_USE_DRIVER_DAC_STREAM)
    mr_bool_t busy;
    mr_err_t ret;
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */

    /* Check parameter */
    if ((channel >= priv->channels_num) || (config->ref != priv->ref)) {
        return -MR_EINVAL;
    }

#if defined(MR_USE_DRIVER_DAC_STREAM)
    /* Check stream state */
    ret = dac_stream_is_busy(drv, &busy);
    if (ret != 0) {
        return ret;
    }
    if (busy) {
        return -MR_EBUSY;
    }
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */

    /* Initialize channel */
    return dac_channel_init(drv, channel);
}

static mr_err_t dac_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_dac_config_t *config) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check channel */
    if (channel >= priv->channels_num) {
        return -MR_EINVAL;
    }

    /* Get reference */
    config->ref = priv->ref;
    return 0;
}

static mr_ssize_t dac_write(const mr_driver_t *drv, mr_dac_seq_t *seq) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint8_t ranks[DAC_CHANNEL_MAX];
    mr_uint32_t channel_count;
    const mr_uint16_t *buf;
    mr_size_t samples, i;
    mr_err_t ret;
#if defined(MR_USE_DRIVER_DAC_STREAM)
    mr_bool_t busy;
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */

#if defined(MR_USE_DRIVER_DAC_STREAM)
    /* Check stream state */
    ret = dac_stream_is_busy(drv, &busy);
    if (ret != 0) {
        return ret;
    }
    if (busy) {
        return -MR_EBUSY;
    }
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */

    /* Check sequence */
    if ((seq->resolution != 12U) || (seq->size > (mr_size_t)MR_SSIZE_MAX)
        || ((seq->size % sizeof(mr_uint16_t)) != 0U) || (seq->channels == 0U)
        || ((seq->channels & ~dac_channel_mask(priv)) != 0U)) {
        return -MR_EINVAL;
    }
    if (seq->size == 0U) {
        return 0;
    }
    ret = dac_seq_channels(priv, seq->channels, ranks, &channel_count);
    if (ret != 0) {
        return ret;
    }
    samples = seq->size / sizeof(mr_uint16_t);
    if ((samples % channel_count) != 0U) {
        return -MR_EINVAL;
    }

    /* Check sample values */
    buf = seq->buf;
    for (i = 0U; i < samples; i++) {
        if (buf[i] > 0x0FFFU) {
            return -MR_EINVAL;
        }
    }

    /* Write samples */
    for (i = 0U; i < samples; i++) {
        ret = dac_channel_start(drv, ranks[i % channel_count], buf[i]);
        if (ret != 0) {
            return (i != 0U) ? (mr_ssize_t)(i * sizeof(mr_uint16_t)) : ret;
        }
    }
    return (mr_ssize_t)seq->size;
}

static mr_err_t dac_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable) {
#if defined(MR_USE_DRIVER_DAC_STREAM)
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */

    /* Check parameter */
    if ((events == 0U) || ((events & ~DAC_EVENT_MASK) != 0U)) {
        return -MR_EINVAL;
    }
#if defined(MR_USE_DRIVER_DAC_STREAM)
    if (!enable && (priv->stream.drv != MR_NULL)) {
        if ((events & MR_DAC_WAIT_STREAM_ERROR) != 0U) {
            return -MR_EBUSY;
        }
        if (((events & MR_DAC_WAIT_STREAM_DONE) != 0U)
            && !priv->stream.circular) {
            return -MR_EBUSY;
        }
    }
    /* Change notifications independently of hardware interrupt sources */
    if (enable) {
        mr_atomic_fetch_or(&priv->stream.event_state, (mr_atomic_t)events);
    } else {
        mr_atomic_fetch_and(&priv->stream.event_state, ~(mr_atomic_t)events);
    }
#else
    MR_IGNORE(drv);
    MR_IGNORE(enable);
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */
    return 0;
}

/* Instance install */
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
#if defined(MR_USE_DRIVER_DAC_STREAM)
#if defined(MR_USE_DRIVER_DMA)
#define __ST_HT_DAC_DMA_INSTALL(_id)                                           \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, st_dma),                            \
                (static dac_stream_dma_t MR_CAT(__dac_dma_, _id) = {           \
                    .spec = MR_HT_DMA_SPEC(_id, st_dma),                       \
                };),                                                           \
                ())
#define __ST_HT_DAC_DMA(_id)                                                   \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, st_dma),                            \
                (.dma = &MR_CAT(__dac_dma_, _id),),                            \
                ())
#else
#define __ST_HT_DAC_DMA_INSTALL(_id)
#define __ST_HT_DAC_DMA(_id)
#endif /* defined(MR_USE_DRIVER_DMA) */
#define __ST_HT_DAC_IRQ(_id)                                                   \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, st_irq_handler),                    \
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
        .event_state = MR_ATOMIC_INIT(0),                                      \
        .irq_valid =                                                           \
            (MR_HT_DRIVER_HAS_PROP(_id, st_irqn)                               \
             && MR_HT_DRIVER_HAS_PROP(_id, st_irq_handler)),                   \
        .trigger_valid = MR_HT_DRIVER_HAS_PROP(_id, st_trigger),               \
    },
#else
#define __ST_HT_DAC_DMA_INSTALL(_id)
#define __ST_HT_DAC_IRQ(_id)
#define __ST_HT_DAC_STREAM(_id)
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */
#define ST_HT_DAC_INSTALL(_id, _ops)                                           \
    static void MR_CAT(__dac_clk_enable_, _id)(void) {                         \
        MR_HT_DRIVER_PROP(_id, st_clken)();                                    \
    }                                                                          \
    static const dac_channel_t MR_CAT(__dac_channels_, _id)[] = {              \
        MR_HT_DRIVER_PROP_ELEM_FOREACH(_id, st_channels,                       \
                                       __ST_HT_DAC_CHANNEL_IMPL)               \
    };                                                                         \
    static mr_dac_data_t MR_CAT(__dac_data_, _id) = MR_DAC_DATA_INIT(          \
        MR_HT_DRIVER_PROP_OR(_id, ref_internal, 3300),                         \
        (1UL << MR_ARRAY_SIZE(MR_CAT(__dac_channels_, _id))) - 1U);            \
    __ST_HT_DAC_DMA_INSTALL(_id)                                               \
    static dac_priv_t MR_CAT(__dac_priv_, _id) = {                             \
        .hdac = {                                                              \
            .Instance = MR_HT_DRIVER_PROP(_id, st_dacx),                       \
        },                                                                     \
        .clk_enable = MR_CAT(__dac_clk_enable_, _id),                          \
        .channels = MR_CAT(__dac_channels_, _id),                              \
        .channels_num = MR_ARRAY_SIZE(MR_CAT(__dac_channels_, _id)),           \
        .ref = MR_HT_DRIVER_PROP_OR(_id, st_ref, MR_DAC_REF_INTERNAL),         \
        __ST_HT_DAC_STREAM(_id)                                                \
    };                                                                         \
    __ST_HT_DAC_IRQ(_id)                                                       \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(dac),                   \
                         MR_DRIVER_TYPE_DAC, &MR_CAT(__dac_data_, _id),        \
                         &MR_CAT(__dac_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(ST_HT_DAC_INSTALL, &__dac_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_DAC) */

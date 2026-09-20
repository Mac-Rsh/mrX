/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-12        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_DAC)
#include <mrX/drivers/dac/mr_dac.h>
#include <mrX/drivers/gpio/mr_gpio.h>
#if defined(MR_USE_DRIVER_DAC_STREAM) && defined(MR_USE_DRIVER_DMA)            \
    && defined(MR_USE_DRIVER_TMR)
#define DAC_USE_STREAM
#include <mrX/drivers/dma/mr_dma.h>
#include <mrX/drivers/tmr/mr_tmr.h>
#endif /* DAC stream + DMA + TMR */
#include "stm32h5xx_hal_dac.h"

/* HardTree compatible */
#define MR_HT_COMPAT                    st_stm32h5_dac
#if MR_HT_HAS_DRIVER()

/* DAC operations */
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
#endif /* defined(DAC_USE_STREAM) */
static mr_err_t dac_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable);
MR_UNUSED static const mr_dac_ops_t __dac_ops = {
    .channel_configure = dac_channel_configure,
    .channel_configuration = dac_channel_configuration,
    .write = dac_write,
#if defined(DAC_USE_STREAM)
    .stream_start = dac_stream_start,
    .stream_stop = dac_stream_stop,
    .stream_is_busy = dac_stream_is_busy,
    .stream_offset = dac_stream_offset,
#endif /* defined(DAC_USE_STREAM) */
    .event_control = dac_event_control,
};

typedef struct dac_channel {
    uint32_t channel;
    mr_ht_gpio_spec_t gpio;
} dac_channel_t;

#if defined(DAC_USE_STREAM)
typedef struct dac_stream {
    mr_ht_dma_spec_t dma;
    mr_driver_t *timer;
    mr_driver_t *drv;
    mr_dma_transfer_t xfer;
    mr_waiter_t waiter;
    uint32_t trigger;
    mr_atomic_t active;
    mr_atomic_t complete;
    mr_size_t prefill;
    mr_bool_t dma_started;
    mr_uint8_t channel;
    mr_bool_t circular;
} dac_stream_t;
#endif /* defined(DAC_USE_STREAM) */

typedef struct dac_priv {
    DAC_HandleTypeDef hdac;
    void (*clk_enable)(void);
    dac_channel_t *channels;
    mr_size_t channels_num;
    mr_uint8_t ref;
    mr_uint8_t started_mask;
    mr_uint32_t event_mask;
#if defined(DAC_USE_STREAM)
    dac_stream_t stream;
#endif /* defined(DAC_USE_STREAM) */
} dac_priv_t;

#define DAC_CHANNEL_MAX                 (2U)
#define DAC_EVENT_MASK                                                         \
    (MR_DAC_WAIT_STREAM_HALF | MR_DAC_WAIT_STREAM_DONE                         \
     | MR_DAC_WAIT_STREAM_ERROR)
#if defined(DAC_USE_STREAM)
#define DAC_NANOSECONDS                 (1000000000ULL)
#endif /* defined(DAC_USE_STREAM) */

static mr_bool_t dac_hal_channel_valid(uint32_t channel) {
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
    if ((priv->channels_num == 0U) || (priv->channels_num > DAC_CHANNEL_MAX)) {
        return 0U;
    }
    return (1UL << priv->channels_num) - 1U;
}

#if defined(DAC_USE_STREAM)
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
#endif /* defined(DAC_USE_STREAM) */

static mr_err_t dac_hw_init(dac_priv_t *priv) {
    mr_size_t i;
    mr_size_t j;

    /* Check the HardTree description before touching the peripheral */
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

    /* Initialize the peripheral */
    return mr_stm32h5_hal_status(HAL_DAC_Init(&priv->hdac));
}

static mr_err_t dac_gpio_configure(dac_priv_t *priv, mr_uint32_t channel) {
    dac_channel_t *hw = &priv->channels[channel];
    mr_gpio_config_t cfg = {0};

    if (hw->gpio.drv == MR_NULL) {
        return -MR_ENODEV;
    }

    /* Set the analog output pin to analog mode with no pull */
    cfg.mode = MR_GPIO_MODE_ANALOG;
    cfg.pull = MR_GPIO_PULL_NONE;
    return mr_ht_gpio_configure(&hw->gpio, &cfg);
}

static mr_err_t dac_config_trigger(dac_priv_t *priv, mr_uint32_t channel,
                                   uint32_t trigger) {
    DAC_ChannelConfTypeDef dac_config = {0};
    mr_uint8_t bit = (mr_uint8_t)(1U << channel);
    mr_err_t ret;

    ret = dac_gpio_configure(priv, channel);
    if (ret != 0) {
        return ret;
    }

    /* Stop before changing the trigger source of an enabled channel */
    if (priv->started_mask & bit) {
        ret = mr_stm32h5_hal_status(
            HAL_DAC_Stop(&priv->hdac, priv->channels[channel].channel));
        if (ret != 0) {
            return ret;
        }
        priv->started_mask &= (mr_uint8_t)~bit;
    }

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
    mr_uint32_t channel;
    mr_uint32_t selected = 0U;

    if ((channels == 0U) || ((channels & ~dac_channel_mask(priv)) != 0U)) {
        return -MR_EINVAL;
    }
    for (channel = 0U; channel < priv->channels_num; channel++) {
        if ((channels & (1U << channel)) != 0U) {
            index[selected] = (mr_uint8_t)channel;
            selected++;
        }
    }
    *count = selected;
    return 0;
}

#if defined(DAC_USE_STREAM)
static mr_err_t dac_channel_trigger_configure(const mr_driver_t *drv,
                                              mr_uint32_t channel) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;

    ret = dac_hw_init(priv);
    if (ret != 0) {
        return ret;
    }

    /* Program the HardTree-bound timer trigger; Start waits for DMA */
    return dac_config_trigger(priv, channel, priv->stream.trigger);
}

static volatile uint32_t *dac_dhr12r(DAC_TypeDef *instance, uint32_t channel) {
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

static void dac_stream_waiter_release(dac_priv_t *priv) {
    if (priv->stream.drv != MR_NULL) {
        (void)mr_waiter_del(&priv->stream.waiter);
        priv->stream.drv = MR_NULL;
    }
}

static void dac_stream_halt_trigger(dac_priv_t *priv) {
    if (priv->stream.timer != MR_NULL) {
        (void)mr_tmr_stop(priv->stream.timer);
    }
}

static void dac_stream_complete(void *param) {
    dac_priv_t *priv = param;

    if (mr_atomic_load(&priv->stream.active) == 0) {
        return;
    }
    mr_atomic_store(&priv->stream.complete, 1);
    if (priv->event_mask & MR_DAC_WAIT_STREAM_DONE) {
        (void)mr_dac_event_raise(priv->stream.drv, MR_DAC_WAIT_STREAM_DONE);
    }
}

static void dac_dma_event(mr_waiter_t *waiter, void *param,
                          mr_wait_event_t event, const void *data) {
    dac_priv_t *priv = (dac_priv_t *)param;
    mr_wait_event_t dac_event = 0U;
    mr_err_t ret;

    MR_IGNORE(waiter);
    MR_IGNORE(data);

    /* Translate DMA events while the DAC stream is active */
    if ((mr_atomic_load(&priv->stream.active) == 0)
        || (priv->stream.drv == MR_NULL)) {
        return;
    }
    if (event & MR_DMA_WAIT_ERROR) {
        /* Stop the trigger; defer DMA/channel/waiter cleanup out of IRQ */
        dac_stream_halt_trigger(priv);
        mr_atomic_store(&priv->stream.complete, 1);
        if (priv->event_mask & MR_DAC_WAIT_STREAM_ERROR) {
            (void)mr_dac_event_raise(priv->stream.drv,
                                     MR_DAC_WAIT_STREAM_ERROR);
        }
        return;
    }
    if ((event & MR_DMA_WAIT_HALF)
        && (priv->event_mask & MR_DAC_WAIT_STREAM_HALF)) {
        dac_event |= MR_DAC_WAIT_STREAM_HALF;
    }
    if (event & MR_DMA_WAIT_DONE) {
        if (!priv->stream.circular) {
            /* The last word still needs one trigger to reach DOR */
            CLEAR_BIT(priv->hdac.Instance->CR,
                      dac_dmaen(priv, priv->stream.channel));
            ret = mr_stm32h5_tmr_drain(priv->stream.timer, dac_stream_complete,
                                       priv);
            if (ret != 0) {
                dac_stream_halt_trigger(priv);
                mr_atomic_store(&priv->stream.complete, 1);
                if (priv->event_mask & MR_DAC_WAIT_STREAM_ERROR) {
                    dac_event |= MR_DAC_WAIT_STREAM_ERROR;
                }
            }
        } else if (priv->event_mask & MR_DAC_WAIT_STREAM_DONE) {
            dac_event |= MR_DAC_WAIT_STREAM_DONE;
        }
    }
    if (dac_event != 0U) {
        (void)mr_dac_event_raise(priv->stream.drv, dac_event);
    }
}

static mr_err_t dac_stream_stop(const mr_driver_t *drv) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint8_t bit;
    mr_err_t first_error = 0;
    mr_err_t ret;

    /* Stop the trigger source before stopping the data path */
    if (priv->stream.timer != MR_NULL) {
        ret = mr_tmr_stop(priv->stream.timer);
        /* A failed start can leave a callback armed on a stopped timer */
        (void)mr_stm32h5_tmr_drain(priv->stream.timer, MR_NULL, MR_NULL);
        if ((ret != 0) && (first_error == 0)) {
            first_error = ret;
        }
    }

    if (mr_atomic_load(&priv->stream.active) != 0) {
        /* Stop/recover DMA even when HAL has put the channel in ERROR */
        if (priv->stream.dma.drv == MR_NULL) {
            if (first_error == 0) {
                first_error = -MR_ENODEV;
            }
        } else if (priv->stream.dma_started) {
            ret = mr_dma_stop(priv->stream.dma.drv, priv->stream.dma.channel);
            if ((ret != 0) && (first_error == 0)) {
                first_error = ret;
            }
        }

        bit = (mr_uint8_t)(1U << priv->stream.channel);
        if (priv->started_mask & bit) {
            ret = mr_stm32h5_hal_status(HAL_DAC_Stop(
                &priv->hdac, priv->channels[priv->stream.channel].channel));
            if ((ret != 0) && (first_error == 0)) {
                first_error = ret;
            }
            if (ret == 0) {
                priv->started_mask &= (mr_uint8_t)~bit;
            }
        }

        /* Keep ownership and the waiter until cleanup succeeds */
        if (first_error != 0) {
            return first_error;
        }
        mr_atomic_store(&priv->stream.active, 0);
        priv->stream.dma_started = MR_FALSE;
    }
    if (HAL_DAC_GetState(&priv->hdac) != HAL_DAC_STATE_RESET) {
        CLEAR_BIT(priv->hdac.Instance->CR,
                  dac_dmaen(priv, priv->stream.channel));
    }
    dac_stream_waiter_release(priv);
    return first_error;
}

static mr_err_t dac_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;

    /* An idle DAC stream does not require an initialized DMA channel */
    if (mr_atomic_load(&priv->stream.active) == 0) {
        *busy = MR_FALSE;
        return 0;
    }
    if (priv->stream.dma.drv == MR_NULL) {
        return -MR_ENODEV;
    }

    /* Finite output includes the final DHR-to-DOR conversion */
    if (!priv->stream.circular) {
        *busy = (mr_atomic_load(&priv->stream.complete) == 0);
        return *busy ? 0 : dac_stream_stop(drv);
    }

    /* Get DMA transfer state */
    ret = mr_dma_is_busy(priv->stream.dma.drv, priv->stream.dma.channel, busy);
    if (ret != 0) {
        return ret;
    }

    /* Recover a circular stream after a DMA error */
    if ((!*busy) && (mr_atomic_load(&priv->stream.active) != 0)) {
        ret = dac_stream_stop(drv);
        if (ret != 0) {
            return ret;
        }
    }
    return 0;
}

static mr_err_t dac_stream_offset(const mr_driver_t *drv, mr_size_t *offset) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;

    /* Get the DMA transfer offset */
    if (priv->stream.dma.drv == MR_NULL) {
        return -MR_ENODEV;
    }
    if (priv->stream.xfer.count == 0U) {
        *offset = priv->stream.prefill;
        return 0;
    }
    ret = mr_dma_offset(priv->stream.dma.drv, priv->stream.dma.channel,
                        offset);
    if (ret == 0) {
        *offset += priv->stream.prefill;
    }
    return ret;
}

static mr_err_t dac_stream_start(const mr_driver_t *drv, mr_dac_seq_t *seq,
                                 const mr_dac_stream_config_t *config) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_tmr_config_t timer_config;
    mr_dma_config_t dma_config = {
        .src_width = MR_DMA_WIDTH_32,
        .dst_width = MR_DMA_WIDTH_32,
        .src_adj = MR_DMA_ADJ_INC,
        .dst_adj = MR_DMA_ADJ_CONST,
        .src_burst = MR_DMA_BURST_1,
        .dst_burst = MR_DMA_BURST_1,
        .direction = MR_DMA_DIR_M2P,
    };
    mr_uint64_t period;
    mr_uint32_t channel;
    mr_bool_t busy;
    mr_err_t ret;

    /* Check stream parameters */
    if ((priv->stream.dma.drv == MR_NULL)
        || (priv->stream.timer == MR_NULL)) {
        return -MR_ENODEV;
    }
    if ((config->update_rate == 0U) || (seq->buf == MR_NULL)
        || (seq->size == 0U) || (seq->resolution != 12U)
        || ((seq->size % sizeof(mr_uint32_t)) != 0U)
        || (((mr_uintptr_t)seq->buf % sizeof(mr_uint32_t)) != 0U)
        || (seq->channels == 0U)
        || ((seq->channels & ~dac_channel_mask(priv)) != 0U)
        || ((seq->channels & (seq->channels - 1U)) != 0U)) {
        return -MR_EINVAL;
    }
    period = DAC_NANOSECONDS / config->update_rate;
    if (period == 0U) {
        return -MR_ERANGE;
    }

    /* Reject a running transfer and clean up an already completed stream */
    ret = dac_stream_is_busy(drv, &busy);
    if (ret != 0) {
        return ret;
    }
    if (busy) {
        return -MR_EBUSY;
    }

    /* Configure before arming DAC so the timer's UG cannot consume data */
    timer_config.period = period;
    timer_config.signal = MR_TMR_SIGNAL_UPDATE;
    timer_config.mode = MR_TMR_MODE_PERIODIC;
    ret = mr_tmr_configure(priv->stream.timer, &timer_config);
    if (ret != 0) {
        return ret;
    }

    for (channel = 0U; channel < priv->channels_num; channel++) {
        if ((seq->channels & (1U << channel)) != 0U) {
            break;
        }
    }
    ret = dac_channel_trigger_configure(drv, channel);
    if (ret != 0) {
        return ret;
    }
    priv->stream.channel = (mr_uint8_t)channel;
    priv->stream.circular = (config->circular != MR_FALSE);

    /* Configure DMA for memory to DHR12Rx transfers */
    ret = mr_dma_configure(priv->stream.dma.drv, priv->stream.dma.channel,
                           &dma_config);
    if (ret != 0) {
        return ret;
    }

    /* Attach a fresh waiter so it can be safely released on stream stop */
    ret = mr_waiter_init(&priv->stream.waiter, dac_dma_event, priv,
                         MR_DMA_WAIT_HALF | MR_DMA_WAIT_DONE
                             | MR_DMA_WAIT_ERROR);
    if (ret != 0) {
        return ret;
    }
    priv->stream.drv = (mr_driver_t *)drv;
    ret = mr_dma_event_attach(priv->stream.dma.drv, priv->stream.dma.channel,
                              &priv->stream.waiter);
    if (ret != 0) {
        (void)dac_stream_stop(drv);
        return ret;
    }
    ret = mr_dma_event_enable(priv->stream.dma.drv, priv->stream.dma.channel,
                              MR_DMA_WAIT_HALF | MR_DMA_WAIT_DONE
                                  | MR_DMA_WAIT_ERROR);
    if (ret != 0) {
        (void)dac_stream_stop(drv);
        return ret;
    }

    /* Preload finite output; each trigger converts then requests the next */
    priv->stream.prefill = config->circular ? 0U : sizeof(mr_uint32_t);
    priv->stream.dma_started = MR_FALSE;
    mr_atomic_store(&priv->stream.complete, 0);
    if (priv->stream.prefill != 0U) {
        ret = mr_stm32h5_hal_status(HAL_DAC_SetValue(
            &priv->hdac, priv->channels[channel].channel, DAC_ALIGN_12B_R,
            *(const mr_uint32_t *)seq->buf & 0x0FFFU));
        if (ret != 0) {
            (void)dac_stream_stop(drv);
            return ret;
        }
    }

    /* Start DMA before enabling DAC conversion requests */
    priv->stream.xfer.next = config->circular ? &priv->stream.xfer : MR_NULL;
    priv->stream.xfer.count = seq->size - priv->stream.prefill;
    priv->stream.xfer.src = (const mr_uint8_t *)seq->buf
                            + priv->stream.prefill;
    priv->stream.xfer.dst = (void *)dac_dhr12r(priv->hdac.Instance,
                                               priv->channels[channel].channel);
    if (priv->stream.xfer.dst == MR_NULL) {
        (void)dac_stream_stop(drv);
        return -MR_EINVAL;
    }
    mr_atomic_store(&priv->stream.active, 1);
    if (priv->stream.xfer.count != 0U) {
        ret = mr_dma_start(priv->stream.dma.drv, priv->stream.dma.channel,
                           &priv->stream.xfer);
        if (ret != 0) {
            (void)dac_stream_stop(drv);
            return ret;
        }
        priv->stream.dma_started = MR_TRUE;
        SET_BIT(priv->hdac.Instance->CR, dac_dmaen(priv, channel));
    } else {
        /* One sample requires exactly one trigger and no DMA request */
        CLEAR_BIT(priv->hdac.Instance->CR, dac_dmaen(priv, channel));
        ret = mr_stm32h5_tmr_drain(priv->stream.timer, dac_stream_complete,
                                   priv);
        if (ret != 0) {
            (void)dac_stream_stop(drv);
            return ret;
        }
    }

    /* Enable the DAC before enabling the hardware trigger */
    ret = mr_stm32h5_hal_status(
        HAL_DAC_Start(&priv->hdac, priv->channels[channel].channel));
    if (ret != 0) {
        (void)dac_stream_stop(drv);
        return ret;
    }
    priv->started_mask |= (mr_uint8_t)(1U << channel);

    ret = mr_tmr_start(priv->stream.timer);
    if (ret != 0) {
        (void)dac_stream_stop(drv);
        return ret;
    }
    return 0;
}
#endif /* defined(DAC_USE_STREAM) */

static mr_err_t dac_channel_start(const mr_driver_t *drv, mr_uint32_t channel,
                                  mr_uint16_t value) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint8_t bit = (mr_uint8_t)(1U << channel);
    mr_err_t ret;

    /* Initialize the selected channel */
    ret = dac_channel_init(drv, channel);
    if (ret != 0) {
        return ret;
    }

    /* Set the output before enabling the channel */
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
#if defined(DAC_USE_STREAM)
    mr_bool_t busy;
    mr_err_t ret;
#endif /* defined(DAC_USE_STREAM) */

    /* Check channel and supported reference */
    if ((channel >= priv->channels_num) || (config->ref != priv->ref)) {
        return -MR_EINVAL;
    }

#if defined(DAC_USE_STREAM)
    /* Do not alter the trigger while a stream is active */
    ret = dac_stream_is_busy(drv, &busy);
    if (ret != 0) {
        return ret;
    }
    if (busy) {
        return -MR_EBUSY;
    }
#endif /* defined(DAC_USE_STREAM) */

    /* Configure channel */
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

    /* H5 DAC channels share one board-level reference input */
    config->ref = priv->ref;
    return 0;
}

static mr_ssize_t dac_write(const mr_driver_t *drv, mr_dac_seq_t *seq) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    const mr_uint16_t *buf;
    mr_uint8_t ranks[DAC_CHANNEL_MAX];
    mr_uint32_t channel_count;
    mr_size_t samples;
    mr_size_t i;
    mr_err_t ret;
#if defined(DAC_USE_STREAM)
    mr_bool_t busy;
#endif /* defined(DAC_USE_STREAM) */

#if defined(DAC_USE_STREAM)
    /* Do not convert while a stream is active */
    ret = dac_stream_is_busy(drv, &busy);
    if (ret != 0) {
        return ret;
    }
    if (busy) {
        return -MR_EBUSY;
    }
#endif /* defined(DAC_USE_STREAM) */

    /* Check supported resolution, channel mask and sample layout */
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

    /* Validate sample values before writing */
    buf = seq->buf;
    for (i = 0U; i < samples; i++) {
        if (buf[i] > 0x0FFFU) {
            return -MR_EINVAL;
        }
    }

    /* Write each sample to the next selected channel in channel order */
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
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check parameter */
    if ((events == 0U) || ((events & ~DAC_EVENT_MASK) != 0U)) {
        return -MR_EINVAL;
    }
#if defined(DAC_USE_STREAM)
    if (!enable && (mr_atomic_load(&priv->stream.active) != 0)) {
        if ((events & MR_DAC_WAIT_STREAM_ERROR) != 0U) {
            return -MR_EBUSY;
        }
        if (((events & MR_DAC_WAIT_STREAM_DONE) != 0U)
            && !priv->stream.circular) {
            return -MR_EBUSY;
        }
    }
#endif /* defined(DAC_USE_STREAM) */
    if (enable) {
        priv->event_mask |= events;
    } else {
        priv->event_mask &= ~events;
    }
    return 0;
}

#define __ST_HT_DAC_CHANNEL_IMPL(_elem, _node, _prop)                          \
    {                                                                          \
        .channel = MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 0),               \
        .gpio                                                                  \
        = {.drv = MR_HT_DRIVER_FIND(                                           \
               MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 1)),                  \
           .pin = (mr_uint8_t)MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 2)},   \
    }
#if defined(DAC_USE_STREAM)
#define __ST_HT_DAC_STREAM(_id)                                                \
    .stream                                                                    \
        = {MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, st_dma),                     \
                       (.dma = MR_HT_DMA_SPEC(_id, st_dma)), (.dma = {0})),    \
           MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, st_timer),                   \
                       (.timer = MR_HT_DRIVER_FIND(                            \
                            MR_HT_DRIVER_PROP(_id, st_timer))),                \
                       (.timer = MR_NULL)),                                    \
           MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, st_trigger),                 \
                       (.trigger = MR_HT_DRIVER_PROP(_id, st_trigger)),        \
                       (.trigger = 0)),                                        \
           .active = MR_ATOMIC_INIT(0)},
#else
#define __ST_HT_DAC_STREAM(_id)
#endif /* defined(DAC_USE_STREAM) */
#define ST_HT_DAC_INSTALL(_id, _ops)                                           \
    static void MR_CAT(__dac_clk_enable_, _id)(void) {                         \
        MR_HT_DRIVER_PROP(_id, st_clken)();                                    \
    }                                                                          \
    static dac_channel_t MR_CAT(__dac_channels_, _id)[]                        \
        = {MR_HT_DRIVER_PROP_ELEM_FOREACH(_id, st_channels,                    \
                                          __ST_HT_DAC_CHANNEL_IMPL)};          \
    static mr_dac_data_t MR_CAT(__dac_data_, _id) = MR_DAC_DATA_INIT(          \
        MR_HT_DRIVER_PROP_OR(_id, ref_internal, 3300),                         \
        (1UL << MR_ARRAY_SIZE(MR_CAT(__dac_channels_, _id))) - 1U);            \
    static dac_priv_t MR_CAT(__dac_priv_, _id)                                 \
        = {.hdac = {.Instance = MR_HT_DRIVER_PROP(_id, st_dacx)},              \
           .clk_enable = MR_CAT(__dac_clk_enable_, _id),                       \
           .channels = MR_CAT(__dac_channels_, _id),                           \
           .channels_num = MR_ARRAY_SIZE(MR_CAT(__dac_channels_, _id)),        \
           .ref = MR_HT_DRIVER_PROP_OR(_id, st_ref, MR_DAC_REF_INTERNAL),      \
           __ST_HT_DAC_STREAM(_id)};                                           \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(dac),                   \
                         MR_DRIVER_TYPE_DAC, &MR_CAT(__dac_data_, _id),        \
                         &MR_CAT(__dac_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(ST_HT_DAC_INSTALL, &__dac_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_DAC) */

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
#if defined(MR_USE_DRIVER_DAC_STREAM) && defined(MR_USE_DRIVER_DMA)
#include <mrX/drivers/dma/mr_dma.h>
#include <mrX/port/mr_port_clock.h>
#define DAC_USE_STREAM
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) && defined(MR_USE_DRIVER_DMA) */
#include <mrX/port/mr_port_irq.h>
#include <mrX/libc/mr_string.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    wch_ch32_dac
#if MR_HT_HAS_DRIVER()
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

#if defined(DAC_USE_STREAM)
/* DAC DMA stream type */
typedef struct dac_dma_stream {
    mr_ht_dma_spec_t dma;
    mr_dma_transfer_t transfer;
    mr_waiter_t waiter;
    mr_driver_t *drv;
    mr_uint32_t events;
    mr_bool_t complete;
} dac_dma_stream_t;
#endif /* defined(DAC_USE_STREAM) */

/* DAC private data type */
typedef struct dac_priv {
    void (*clk_enable)(void);
    mr_uint8_t ref;
    mr_uint32_t configured;
#if defined(DAC_USE_STREAM)
    dac_dma_stream_t stream;
    void (*dma_request)(mr_bool_t enable);
    void (*software_request)(mr_bool_t enable);
    void (*active_request)(mr_bool_t enable);
    mr_uint32_t trigger, shift;
#endif /* defined(DAC_USE_STREAM) */
} dac_priv_t;

/* DAC channel definition */
#define DAC_CHANNEL_MASK                (0x03U)
#define DAC_CHANNEL_BITS                (16U)

/* DAC sample definition */
#define DAC_MAX_SAMPLE                  (0x0fffU)

#if defined(DAC_USE_STREAM)
/* DAC stream event mask definition */
#define DAC_STREAM_EVENTS                                                      \
    (MR_DAC_WAIT_STREAM_HALF | MR_DAC_WAIT_STREAM_DONE                         \
     | MR_DAC_WAIT_STREAM_ERROR)

/* DAC DMA event definition */
#define DAC_DMA_EVENTS                                                         \
    (MR_DMA_WAIT_HALF | MR_DMA_WAIT_DONE | MR_DMA_WAIT_ERROR)
#endif /* defined(DAC_USE_STREAM) */

static mr_err_t dac_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_dac_config_t *config) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t shift;
    int mask;

    /* Check channel */
    if (channel >= 2U) {
        return -MR_EINVAL;
    }

    /* Check configuration */
    if (config->ref != priv->ref) {
        return -MR_ENOSYS;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check stream state */
#if defined(DAC_USE_STREAM)
    if (priv->stream.drv && !priv->stream.complete) {
        mr_port_irq_enable(mask);
        return -MR_EBUSY;
    }
#endif /* defined(DAC_USE_STREAM) */

    /* Enable DAC clock */
    if (priv->configured == 0U) {
        priv->clk_enable();
    }

    /* Change trigger and waveform settings with the channel disabled */
    shift = channel * DAC_CHANNEL_BITS;
    if ((priv->configured & (1U << channel)) == 0U) {
        DAC->CTLR &= ~(DAC_EN1 << shift);
        DAC->CTLR &= ~(0xffffU << shift);
        DAC->CTLR |= DAC_EN1 << shift;
    }

    /* Update DAC state */
    priv->configured |= 1U << channel;

    /* Unlock */
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t dac_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_dac_config_t *config) {
    const dac_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check channel */
    if (channel >= 2U) {
        return -MR_EINVAL;
    }

    /* Get channel configuration */
    config->ref = priv->ref;
    return 0;
}

static mr_ssize_t dac_write(const mr_driver_t *drv, mr_dac_seq_t *seq) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    const mr_dac_data_t *data;
    const mr_uint8_t *buf;
    mr_uint16_t sample;
    mr_size_t bytes;
    int mask;

    /* Check parameter */
    data = MR_DRIVER_DATA(drv);
    if ((seq->channels == 0U)
        || ((seq->channels & ~(data->exist_mask & DAC_CHANNEL_MASK)) != 0U)
        || (seq->size > (mr_size_t)MR_SSIZE_MAX)
        || ((seq->size % sizeof(sample)) != 0U)) {
        return -MR_EINVAL;
    }
    if ((seq->resolution != 12U) && (seq->resolution != 8U)) {
        return -MR_ENOSYS;
    }
    if (seq->size == 0U) {
        return 0;
    }
    /* Each selected channel accepts one sample per immediate write */
    if (seq->size != ((seq->channels == DAC_CHANNEL_MASK) ? 4U : 2U)) {
        return -MR_EINVAL;
    }

    /* Check samples */
    buf = seq->buf;
    for (bytes = 0U; bytes < seq->size; bytes += sizeof(sample)) {
        mr_memcpy(&sample, &buf[bytes], sizeof(sample));
        if (sample >= (1U << seq->resolution)) {
            return -MR_EINVAL;
        }
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check stream state */
#if defined(DAC_USE_STREAM)
    if (priv->stream.drv && !priv->stream.complete) {
        mr_port_irq_enable(mask);
        return -MR_EBUSY;
    }
#endif /* defined(DAC_USE_STREAM) */

    /* Check channel state */
    if ((seq->channels & ~priv->configured) != 0U) {
        mr_port_irq_enable(mask);
        return -MR_ENODEV;
    }

    /* Write samples */
    mr_memcpy(&sample, buf, sizeof(sample));
    sample <<= 12U - seq->resolution;
    if (seq->channels == DAC_CHANNEL_MASK) {
        mr_uint16_t second;

        mr_memcpy(&second, &buf[sizeof(sample)], sizeof(second));
        second <<= 12U - seq->resolution;
        DAC->RD12BDHR = ((mr_uint32_t)second << 16) | sample;
    } else if (seq->channels == 1U) {
        DAC->R12BDHR1 = sample;
    } else {
        DAC->R12BDHR2 = sample;
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return (mr_ssize_t)seq->size;
}

#if defined(DAC_USE_STREAM)
static void dac_stream_quiesce(dac_priv_t *priv) {
    /* Disable DAC stream requests */
    if (priv->active_request != MR_NULL) {
        priv->active_request(MR_FALSE);
    } else {
        DAC->CTLR &= ~((DAC_DMAEN1 | DAC_TEN1) << priv->shift);
    }
}

static mr_err_t dac_dma_stop(dac_dma_stream_t *stream) {
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
    mr_ht_dma_event_disable(&stream->dma, DAC_DMA_EVENTS);

    /* Release waiter */
    mr_waiter_del(&stream->waiter);

    /* Reset stream state */
    stream->drv = MR_NULL;
    stream->complete = MR_FALSE;
    return 0;
}

static mr_err_t dac_dma_start(const mr_driver_t *drv, dac_dma_stream_t *stream,
                              mr_waiter_entry_t *entry, void *param,
                              mr_uint32_t width) {
    mr_dma_config_t config = {
        .src_width = width,
        .dst_width = MR_DMA_WIDTH_32,
        .src_adj = MR_DMA_ADJ_INC,
        .dst_adj = MR_DMA_ADJ_CONST,
        .src_burst = MR_DMA_BURST_1,
        .dst_burst = MR_DMA_BURST_1,
        .direction = MR_DMA_DIR_M2P,
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
    ret = mr_waiter_init(&stream->waiter, entry, param, DAC_DMA_EVENTS);
    if (ret != 0) {
        return ret;
    }

    /* Attach DMA event */
    ret = mr_ht_dma_event_attach(&stream->dma, &stream->waiter);
    if (ret == 0) {
        ret = mr_ht_dma_event_enable(&stream->dma, DAC_DMA_EVENTS);
        if (ret == 0) {
            ret = mr_ht_dma_start(&stream->dma, &stream->transfer);
            if (ret != 0) {
                mr_ht_dma_event_disable(&stream->dma, DAC_DMA_EVENTS);
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

static void dac_dma_event(mr_waiter_t *waiter, void *param,
                          mr_wait_event_t event, const void *data) {
    const mr_dac_data_t *event_data;
    mr_uint32_t events;
    dac_priv_t *priv;
    int mask;

    priv = (dac_priv_t *)param;
    MR_IGNORE(waiter);
    MR_IGNORE(data);

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check stream state */
    if (!priv->stream.drv || priv->stream.complete) {
        mr_port_irq_enable(mask);
        return;
    }

    /* Translate DMA event */
    events = 0U;
    if (event & MR_DMA_WAIT_ERROR) {
        dac_stream_quiesce(priv);
        priv->stream.complete = MR_TRUE;
        events = MR_DAC_WAIT_STREAM_ERROR;
    } else {
        if (event & MR_DMA_WAIT_HALF) {
            events |= MR_DAC_WAIT_STREAM_HALF;
        }
        if (event & MR_DMA_WAIT_DONE) {
            if (priv->stream.transfer.next == MR_NULL) {
                dac_stream_quiesce(priv);
                /* Wait for the untriggered DHR-to-DOR transfer */
                mr_port_clock_busy_wait(2U);
                priv->stream.complete = MR_TRUE;
            }
            events |= MR_DAC_WAIT_STREAM_DONE;
        }
    }

    /* Raise DAC event */
    events &= priv->stream.events;
    event_data = MR_DRIVER_DATA(priv->stream.drv);
    if ((events != 0U) && mr_driver_is_ready(priv->stream.drv)
        && MR_OBJECT_IS_INITED(&event_data->event)) {
        mr_dac_event_raise(priv->stream.drv, events);
    }

    /* Unlock */
    mr_port_irq_enable(mask);
}

static mr_err_t dac_stream_start(const mr_driver_t *drv, mr_dac_seq_t *seq,
                                 const mr_dac_stream_config_t *config) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    void (*request)(mr_bool_t enable);
    const mr_dac_data_t *data;
    mr_uint32_t shift, width, ctlr;
    const mr_uint8_t *samples;
    mr_uint16_t sample;
    mr_size_t i;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    if (seq->resolution != 12U) {
        return -MR_ENOSYS;
    }

    /* Check stream source */
    request = (config->source == MR_DAC_STREAM_SOURCE_EXTERNAL)
        ? priv->dma_request : priv->software_request;
    /* DAC-generated requests load the sample for the following trigger */
    if ((request == MR_NULL)
        && ((config->source != MR_DAC_STREAM_SOURCE_EXTERNAL)
            || !config->circular || (priv->trigger >= 7U)
            || (seq->channels == DAC_CHANNEL_MASK))) {
        return -MR_ENOSYS;
    }

    /* Check sequence */
    data = MR_DRIVER_DATA(drv);
    width = (seq->channels == DAC_CHANNEL_MASK) ? 4U : 2U;
    if ((seq->channels == 0U)
        || ((seq->channels & ~(data->exist_mask & DAC_CHANNEL_MASK)) != 0U)
        || (seq->size == 0U) || ((seq->size % width) != 0U)
        || ((seq->size / width) > MR_UINT16_MAX)
        || (((mr_uintptr_t)seq->buf % width) != 0U)) {
        return -MR_EINVAL;
    }

    /* Check samples */
    samples = seq->buf;
    for (i = 0U; i < seq->size; i += sizeof(sample)) {
        mr_memcpy(&sample, &samples[i], sizeof(sample));
        if (sample > DAC_MAX_SAMPLE) {
            return -MR_EINVAL;
        }
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check channel state */
    if ((seq->channels & ~priv->configured) != 0U) {
        ret = -MR_ENODEV;
        goto _exit;
    }

    /* Check stream state */
    if (priv->stream.drv && !priv->stream.complete) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Stop old stream */
    ret = dac_dma_stop(&priv->stream);
    if (ret != 0) {
        goto _exit;
    }

    /* Start stream */
    shift = (seq->channels == 1U) ? 0U : DAC_CHANNEL_BITS;
    priv->stream.transfer.src = seq->buf;
    if (seq->channels == DAC_CHANNEL_MASK) {
        priv->stream.transfer.dst = (void *)&DAC->RD12BDHR;
    } else {
        priv->stream.transfer.dst = (seq->channels == 1U)
            ? (void *)&DAC->R12BDHR1 : (void *)&DAC->R12BDHR2;
    }
    priv->stream.transfer.count = seq->size / width;
    priv->stream.transfer.next = config->circular
        ? &priv->stream.transfer : MR_NULL;
    ret = dac_dma_start(drv, &priv->stream, dac_dma_event, priv,
                        (width == 4U) ? MR_DMA_WIDTH_32 : MR_DMA_WIDTH_16);
    if (ret == 0) {
        priv->shift = shift;
        priv->active_request = request;
        if (request != MR_NULL) {
            request(MR_TRUE);
        } else {
            /* The trigger selector is writable only while EN is clear */
            DAC->CTLR &= ~(DAC_EN1 << shift);
            ctlr = DAC->CTLR & ~(DAC_TSEL1 << shift);
            ctlr |= ((priv->trigger << 3) | DAC_DMAEN1 | DAC_TEN1) << shift;
            DAC->CTLR = ctlr;
            DAC->CTLR = ctlr | (DAC_EN1 << shift);
        }
    }

_exit:
    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

static mr_err_t dac_stream_stop(const mr_driver_t *drv) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Stop stream */
    if (priv->stream.drv && !priv->stream.complete) {
        dac_stream_quiesce(priv);
    }
    ret = dac_dma_stop(&priv->stream);
    if (ret == 0) {
        priv->active_request = MR_NULL;
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

static mr_err_t dac_stream_is_busy(const mr_driver_t *drv, mr_bool_t *busy) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Get stream state */
    mask = mr_port_irq_disable();
    *busy = priv->stream.drv && !priv->stream.complete;
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t dac_stream_offset(const mr_driver_t *drv, mr_size_t *offset) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Get stream offset */
    if (!mr_driver_is_ready(priv->stream.dma.drv)) {
        return priv->stream.dma.drv ? -MR_ENODEV : -MR_ENOSYS;
    }
    return mr_ht_dma_offset(&priv->stream.dma, offset);
}

static mr_err_t dac_event_control(const mr_driver_t *drv, mr_uint32_t events,
                                  mr_bool_t enable) {
    dac_priv_t *priv = MR_DRIVER_PRIV(drv);
    int mask;

    /* Check parameter */
    if ((events == 0U) || ((events & ~DAC_STREAM_EVENTS) != 0U)) {
        return -MR_EINVAL;
    }

    /* Check stream support */
    if (!priv->stream.dma.drv) {
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
#endif /* defined(DAC_USE_STREAM) */

/* Instance install */
#define CH32_HT_DAC_INSTALL(_id, _ops)                                         \
    extern void MR_HT_DRIVER_PROP(_id, wch_clken)(void);                       \
    __CH32_HT_DAC_STREAM_DECL(_id)                                             \
    static dac_priv_t MR_CAT(__dac_priv_, _id) = {                             \
        .clk_enable = MR_HT_DRIVER_PROP(_id, wch_clken),                       \
        .ref = MR_HT_DRIVER_PROP_OR(_id, wch_ref, MR_DAC_REF_VDD_1),           \
        __CH32_HT_DAC_STREAM(_id)                                              \
    };                                                                         \
    MR_HT_DAC_INSTALL(_id, &MR_CAT(__dac_priv_, _id), _ops)
#if defined(DAC_USE_STREAM)
#define __CH32_HT_DAC_REQUEST_DECL(_id, _prop)                                 \
    extern void MR_HT_DRIVER_PROP(_id, _prop)(mr_bool_t enable);
#define __CH32_HT_DAC_STREAM(_id)                                              \
    .stream = {                                                                \
        .dma = MR_HT_DMA_SPEC_OR(_id, tx_dma, {0}),                            \
    },                                                                         \
    .dma_request = MR_HT_DRIVER_PROP_OR(_id, wch_dma_request, MR_NULL),        \
    .software_request = MR_HT_DRIVER_PROP_OR(                                  \
        _id, wch_software_dma_request, MR_NULL),                               \
    .trigger = MR_HT_DRIVER_PROP_OR(_id, wch_trigger, 7U),
#define __CH32_HT_DAC_STREAM_DECL(_id)                                         \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, wch_dma_request),                   \
                (__CH32_HT_DAC_REQUEST_DECL(_id, wch_dma_request)), ())        \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, wch_software_dma_request),          \
                (__CH32_HT_DAC_REQUEST_DECL(_id, wch_software_dma_request)), ())
#else
#define __CH32_HT_DAC_STREAM(_id)
#define __CH32_HT_DAC_STREAM_DECL(_id)
#endif /* defined(DAC_USE_STREAM) */
MR_HT_DRIVER_FOREACH(CH32_HT_DAC_INSTALL, &__dac_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_DAC) */

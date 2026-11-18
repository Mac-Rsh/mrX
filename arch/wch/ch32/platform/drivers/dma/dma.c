/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_DMA)
#include <mrX/drivers/dma/mr_dma.h>
#include <mrX/port/mr_port_irq.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    wch_ch32_dma
#if MR_HT_HAS_DRIVER()

/* Driver DMA operations */
static mr_err_t dma_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_dma_config_t *config);
static mr_err_t dma_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_dma_config_t *config);
static mr_err_t dma_start(const mr_driver_t *drv, mr_uint32_t channel,
                          const mr_dma_transfer_t *transfer);
static mr_err_t dma_stop(const mr_driver_t *drv, mr_uint32_t channel);
static mr_err_t dma_is_busy(const mr_driver_t *drv, mr_uint32_t channel,
                            mr_bool_t *busy);
static mr_err_t dma_offset(const mr_driver_t *drv, mr_uint32_t channel,
                           mr_size_t *offset);
static mr_err_t dma_event_attach(const mr_driver_t *drv, mr_uint32_t channel,
                                 mr_waiter_t *entry);
static mr_err_t dma_event_control(const mr_driver_t *drv, mr_uint32_t channel,
                                  mr_uint32_t events, mr_bool_t enable);
MR_UNUSED static const mr_dma_ops_t __dma_ops = {
    .channel_configure = dma_channel_configure,
    .channel_configuration = dma_channel_configuration,
    .start = dma_start,
    .stop = dma_stop,
    .is_busy = dma_is_busy,
    .offset = dma_offset,
    .event_attach = dma_event_attach,
    .event_control = dma_event_control,
};

/* DMA channel count definition */
#if defined(MR_USE_CH32V4X7)
#define CH32_DMA_CHANNELS               13
#define CH32_DMA2_CHANNELS              (11U)
#define CH32_DMA_CONTROLLERS            (2U)
#elif defined(MR_USE_CH32V30X)
#define CH32_DMA_CHANNELS               11
#define CH32_DMA2_CHANNELS              CH32_DMA_CHANNELS
#define CH32_DMA_CONTROLLERS            (2U)
#elif defined(MR_USE_CH32V205)
#define CH32_DMA_CHANNELS               8
#define CH32_DMA2_CHANNELS              CH32_DMA_CHANNELS
#define CH32_DMA_CONTROLLERS            (2U)
#elif defined(MR_USE_CH32V20X)
#define CH32_DMA_CHANNELS               8
#define CH32_DMA_CONTROLLERS            (1U)
#else
#define CH32_DMA_CHANNELS               7
#define CH32_DMA_CONTROLLERS            (1U)
#endif /* CH32 EVT SDK family */
/* DMA per-channel status definition */
#define CH32_DMA_FLAG_DONE              (1U << 1)
#define CH32_DMA_FLAG_HALF              (1U << 2)
#define CH32_DMA_FLAG_ERROR             (1U << 3)
#define CH32_DMA_FLAG_MASK              (0x0fU)

/* DMA control field definition */
#define CH32_DMA_PWIDTH_SHIFT           (8U)
#define CH32_DMA_MWIDTH_SHIFT           (10U)

/* DMA IRQ mask definition */
#define CH32_DMA_IRQ_MASK                                                      \
    (DMA_CFGR1_HTIE | DMA_CFGR1_TCIE | DMA_CFGR1_TEIE)

/* DMA event mask definition */
#define CH32_DMA_EVENTS                                                        \
    (MR_DMA_WAIT_HALF | MR_DMA_WAIT_DONE | MR_DMA_WAIT_ERROR)

/* DMA IRQ definition */
#define CH32_DMA_NO_IRQ                 (-1)

/* DMA hardware type */
typedef struct dma_hw {
    DMA_Channel_TypeDef *channel;
    DMA_TypeDef *status;
    mr_uint32_t shift;
    mr_uint32_t controller;
} dma_hw_t;

/* DMA channel type */
typedef struct dma_channel {
    mr_waitable_t waitable;
    mr_dma_config_t config;
    mr_uint16_t count;
    mr_uint8_t events;
    mr_bool_t configured;
    mr_bool_t failed;
} dma_channel_t;

/* DMA private data type */
typedef struct dma_priv {
    DMA_TypeDef *instance;
    const int *irqs;
    mr_uint8_t irq_priority;
    dma_channel_t channels[CH32_DMA_CHANNELS];
} dma_priv_t;

/* DMA controller owners */
static const mr_driver_t *__dma_owners[CH32_DMA_CONTROLLERS];

static mr_err_t dma_get_hw(const dma_priv_t *priv, mr_uint32_t channel,
                           dma_hw_t *hw) {
    const mr_uint32_t stride = DMA1_Channel2_BASE - DMA1_Channel1_BASE;
    mr_uintptr_t base;

    /* Check channel mapping */
    if (channel >= CH32_DMA_CHANNELS) {
        return -MR_EINVAL;
    }

    /* Resolve channel register address */
    hw->status = priv->instance;
    hw->shift = 4U * channel;
    hw->controller = 0U;
    if (priv->instance == DMA1) {
#if defined(MR_USE_CH32V4X7)
        if (channel < 7U) {
            base = DMA1_Channel1_BASE + stride * channel;
        } else {
            /* Channels 8 and above use a shorter stride and separate flags */
            base = DMA1_Channel8_BASE
                   + (DMA1_Channel9_BASE - DMA1_Channel8_BASE) * (channel - 7U);
            hw->status = DMA1_EXTEN;
            hw->shift = 4U * (channel - 7U);
        }
#else
#if defined(MR_USE_CH32V30X)
        if (channel >= 7U) {
            return -MR_EINVAL;
        }
#endif /* defined(MR_USE_CH32V30X) */
        base = DMA1_Channel1_BASE + stride * channel;
#endif /* defined(MR_USE_CH32V4X7) */
#if (CH32_DMA_CONTROLLERS == 2U)
    } else if (priv->instance == DMA2) {
        if (channel >= CH32_DMA2_CHANNELS) {
            return -MR_EINVAL;
        }
        hw->controller = 1U;
#if defined(MR_USE_CH32V205)
        base = DMA2_Channel1_BASE + stride * channel;
#else
        if (channel < 7U) {
            base = DMA2_Channel1_BASE + stride * channel;
        } else {
            base = DMA2_Channel8_BASE
                   + (DMA2_Channel9_BASE - DMA2_Channel8_BASE) * (channel - 7U);
            hw->status = DMA2_EXTEN;
            hw->shift = 4U * (channel - 7U);
        }
#endif /* defined(MR_USE_CH32V205) */
#endif /* (CH32_DMA_CONTROLLERS == 2U) */
    } else {
        return -MR_EINVAL;
    }
    hw->channel = (DMA_Channel_TypeDef *)base;
    return 0;
}

static void dma_clock_enable(mr_uint32_t controller) {
    /* Enable controller clock */
#if defined(MR_USE_CH32V205) || defined(MR_USE_CH32V4X7)
    RCC->HBPCENR |= (controller == 0U) ? RCC_HBPeriph_DMA1 : RCC_HBPeriph_DMA2;
#elif defined(MR_USE_CH32VM00X)
    MR_IGNORE(controller);
    RCC->HBPCENR |= RCC_DMA1EN;
#elif defined(MR_USE_CH32V30X)
    RCC->AHBPCENR |= (controller == 0U) ? RCC_DMA1EN : RCC_DMA2EN;
#else
    MR_IGNORE(controller);
    RCC->AHBPCENR |= RCC_DMA1EN;
#endif /* CH32 EVT SDK family */
}

static mr_uint32_t dma_pending(const dma_hw_t *hw) {
    /* Read controller pending flags */
    return (hw->status->INTFR >> hw->shift) & CH32_DMA_FLAG_MASK;
}

static mr_bool_t dma_active(const dma_hw_t *hw) {
    /* Check channel activity */
    return ((hw->channel->CFGR & DMA_CFGR1_EN) != 0U)
           && ((hw->channel->CFGR & DMA_CFGR1_CIRC) != 0U
               || (hw->channel->CNTR != 0U))
           && ((dma_pending(hw) & CH32_DMA_FLAG_ERROR) == 0U);
}

static mr_uint32_t dma_irq_bits(mr_uint32_t events) {
    mr_uint32_t bits;

    /* Translate subscribed events */
    bits = 0U;
    if ((events & MR_DMA_WAIT_DONE) != 0U) {
        bits |= DMA_CFGR1_TCIE;
    }
    if ((events & MR_DMA_WAIT_HALF) != 0U) {
        bits |= DMA_CFGR1_HTIE;
    }
    if ((events & MR_DMA_WAIT_ERROR) != 0U) {
        bits |= DMA_CFGR1_TEIE;
    }
    return bits;
}

static mr_err_t dma_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_dma_config_t *config) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
    dma_channel_t *state;
    mr_err_t ret;
    dma_hw_t hw;
    int mask;

    /* Resolve channel */
    ret = dma_get_hw(priv, channel, &hw);
    if (ret != 0) {
        return ret;
    }

    /* Check parameter */
    if ((config->direction > MR_DMA_DIR_M2M)
        || (config->src_adj > MR_DMA_ADJ_CONST)
        || (config->dst_adj > MR_DMA_ADJ_CONST)
        || (config->src_burst > MR_DMA_BURST_256)
        || (config->dst_burst > MR_DMA_BURST_256)) {
        return -MR_EINVAL;
    }
    if ((config->src_width == MR_DMA_WIDTH_64)
        || (config->dst_width == MR_DMA_WIDTH_64)
        || (config->src_adj == MR_DMA_ADJ_DEC)
        || (config->dst_adj == MR_DMA_ADJ_DEC)
        || ((config->direction == MR_DMA_DIR_M2M)
            && (config->src_width != config->dst_width))
        || (config->src_burst != MR_DMA_BURST_1)
        || (config->dst_burst != MR_DMA_BURST_1)) {
        return -MR_ENOSYS;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check controller ownership */
    if ((__dma_owners[hw.controller] != MR_NULL)
        && (__dma_owners[hw.controller] != drv)) {
        ret = -MR_EBUSY;
    } else {
        /* Enable controller clock */
        dma_clock_enable(hw.controller);
        /* Check channel state */
        if (dma_active(&hw)) {
            ret = -MR_EBUSY;
        } else {
            /* Init channel */
            __dma_owners[hw.controller] = drv;
            state = &priv->channels[channel];
            hw.channel->CFGR = 0U;
            hw.channel->CNTR = 0U;
            hw.status->INTFCR = CH32_DMA_FLAG_MASK << hw.shift;
            state->config = *config;
            state->count = 0U;
            state->configured = MR_TRUE;
            state->failed = MR_FALSE;
        }
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

static mr_err_t dma_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_dma_config_t *config) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;
    dma_hw_t hw;
    int mask;

    /* Resolve channel */
    ret = dma_get_hw(priv, channel, &hw);
    if (ret != 0) {
        return ret;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check channel state */
    if (priv->channels[channel].configured) {
        /* Get channel configuration */
        *config = priv->channels[channel].config;
    } else {
        ret = -MR_ENODEV;
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

static mr_bool_t dma_valid_address(mr_uintptr_t address, mr_uint32_t width,
                                    mr_uint32_t adjust, mr_size_t count) {
    mr_size_t span;

    /* Get endpoint byte span */
    span = (adjust == MR_DMA_ADJ_INC) ? count * width : width;

    /* Check address range and alignment */
    return (address != 0U) && ((address & (width - 1U)) == 0U)
           && (span - 1U <= MR_UINT32_MAX - address);
}

static mr_err_t dma_start(const mr_driver_t *drv, mr_uint32_t channel,
                           const mr_dma_transfer_t *transfer) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uintptr_t source, destination;
    mr_size_t count;
    mr_dma_config_t config;
    dma_channel_t *state;
    mr_uint32_t control;
    mr_bool_t circular;
    int mask, irq;
    mr_err_t ret;
    dma_hw_t hw;

    /* Resolve channel */
    ret = dma_get_hw(priv, channel, &hw);
    if (ret != 0) {
        return ret;
    }

    /* Check transfer */
    if (transfer->count == 0U) {
        return -MR_EINVAL;
    }

    /* Resolve transfer topology */
    circular = (transfer->next == transfer);
    if (!circular && (transfer->next != MR_NULL)) {
        return -MR_ENOSYS;
    }

    /* Lock */
    mask = mr_port_irq_disable();
    state = &priv->channels[channel];

    /* Check channel state */
    if (!state->configured) {
        ret = -MR_ENODEV;
        goto _exit;
    }
    if (dma_active(&hw)) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Check transfer count */
    config = state->config;
    count = transfer->count;
    if (count > MR_UINT16_MAX) {
        ret = -MR_EINVAL;
        goto _exit;
    }
    if (circular && (config.direction == MR_DMA_DIR_M2M)) {
        ret = -MR_ENOSYS;
        goto _exit;
    }

    /* Validate transfer endpoints */
    source = (mr_uintptr_t)transfer->src;
    destination = (mr_uintptr_t)transfer->dst;
    if (!dma_valid_address(source, 1U << config.src_width, config.src_adj,
                           count)
        || !dma_valid_address(destination, 1U << config.dst_width,
                              config.dst_adj, count)) {
        ret = -MR_EINVAL;
        goto _exit;
    }

    /* Program addresses and count while EN is clear */
    hw.channel->CFGR = 0U;
    hw.status->INTFCR = CH32_DMA_FLAG_MASK << hw.shift;
    hw.channel->CNTR = count;
    control = circular ? DMA_CFGR1_CIRC : 0U;
    if (config.direction == MR_DMA_DIR_M2P) {
        hw.channel->PADDR = destination;
        hw.channel->MADDR = source;
        control |= DMA_CFGR1_DIR | (config.src_width << CH32_DMA_MWIDTH_SHIFT)
                   | (config.dst_width << CH32_DMA_PWIDTH_SHIFT);
        control |= (config.src_adj == MR_DMA_ADJ_INC) ? DMA_CFGR1_MINC : 0U;
        control |= (config.dst_adj == MR_DMA_ADJ_INC) ? DMA_CFGR1_PINC : 0U;
    } else {
        hw.channel->PADDR = source;
        hw.channel->MADDR = destination;
        control |= (config.src_width << CH32_DMA_PWIDTH_SHIFT)
                   | (config.dst_width << CH32_DMA_MWIDTH_SHIFT);
        control |= (config.src_adj == MR_DMA_ADJ_INC) ? DMA_CFGR1_PINC : 0U;
        control |= (config.dst_adj == MR_DMA_ADJ_INC) ? DMA_CFGR1_MINC : 0U;
        control |= (config.direction == MR_DMA_DIR_M2M) ? DMA_CFGR1_MEM2MEM
                                                       : 0U;
    }
    state->count = (mr_uint16_t)count;
    state->failed = MR_FALSE;

    /* Enable channel IRQ */
    irq = priv->irqs[channel];
    if (mr_ch32_irq_valid(irq) && (state->events != 0U)) {
        NVIC_ClearPendingIRQ((IRQn_Type)irq);
        NVIC_SetPriority((IRQn_Type)irq, priv->irq_priority);
        NVIC_EnableIRQ((IRQn_Type)irq);
        control |= dma_irq_bits(state->events) | DMA_CFGR1_TEIE;
    }

    /* Publish CPU writes to the source before enabling the DMA master */
    __asm__ volatile ("fence iorw, iorw" ::: "memory");
    hw.channel->CFGR = control | DMA_CFGR1_EN;
_exit:
    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

static mr_err_t dma_stop(const mr_driver_t *drv, mr_uint32_t channel) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
    dma_channel_t *state;
    int mask, irq;
    mr_err_t ret;
    dma_hw_t hw;

    /* Resolve channel */
    ret = dma_get_hw(priv, channel, &hw);
    if (ret != 0) {
        return ret;
    }

    /* Lock */
    mask = mr_port_irq_disable();
    state = &priv->channels[channel];

    /* Check channel state */
    if (!state->configured) {
        ret = -MR_ENODEV;
        goto _exit;
    }

    /* Stop DMA */
    hw.channel->CFGR &= ~(DMA_CFGR1_EN | CH32_DMA_IRQ_MASK);
    if ((hw.channel->CFGR & DMA_CFGR1_EN) != 0U) {
        ret = -MR_EBUSY;
        goto _exit;
    }
    state->failed = state->failed
                    || ((dma_pending(&hw) & CH32_DMA_FLAG_ERROR) != 0U);
    hw.status->INTFCR = CH32_DMA_FLAG_MASK << hw.shift;

    /* Disable channel IRQ */
    irq = priv->irqs[channel];
    if (mr_ch32_irq_valid(irq)) {
        NVIC_DisableIRQ((IRQn_Type)irq);
        NVIC_ClearPendingIRQ((IRQn_Type)irq);
    }
    __asm__ volatile ("fence iorw, iorw" ::: "memory");
_exit:
    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

static mr_err_t dma_is_busy(const mr_driver_t *drv, mr_uint32_t channel,
                             mr_bool_t *busy) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;
    dma_hw_t hw;
    int mask;

    /* Resolve channel */
    ret = dma_get_hw(priv, channel, &hw);
    if (ret != 0) {
        return ret;
    }

    /* Get channel state */
    mask = mr_port_irq_disable();
    *busy = priv->channels[channel].configured && dma_active(&hw);
    __asm__ volatile ("fence iorw, iorw" ::: "memory");
    mr_port_irq_enable(mask);
    return 0;
}

static mr_err_t dma_offset(const mr_driver_t *drv, mr_uint32_t channel,
                           mr_size_t *offset) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t remaining, width;
    dma_channel_t *state;
    mr_err_t ret;
    dma_hw_t hw;
    int mask;

    /* Resolve channel */
    ret = dma_get_hw(priv, channel, &hw);
    if (ret != 0) {
        return ret;
    }

    /* Lock */
    mask = mr_port_irq_disable();
    state = &priv->channels[channel];

    /* Get channel progress */
    if (!state->configured) {
        ret = -MR_ENODEV;
    } else if (state->failed
               || ((dma_pending(&hw) & CH32_DMA_FLAG_ERROR) != 0U)) {
        ret = -MR_EIO;
    } else {
        /* Calculate completed items */
        remaining = hw.channel->CNTR & MR_UINT16_MAX;
        width = state->config.src_width;
        if ((remaining > state->count)
            || ((remaining == 0U)
                && ((hw.channel->CFGR & DMA_CFGR1_CIRC) != 0U))) {
            remaining = state->count;
        }
        __asm__ volatile ("fence iorw, iorw" ::: "memory");
        *offset = (mr_size_t)(state->count - remaining) << width;
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

static mr_err_t dma_event_attach(const mr_driver_t *drv, mr_uint32_t channel,
                                 mr_waiter_t *entry) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
    dma_channel_t *state;
    mr_err_t ret;
    dma_hw_t hw;
    int mask;

    /* Resolve channel */
    ret = dma_get_hw(priv, channel, &hw);
    if (ret != 0) {
        return ret;
    }

    /* Check IRQ support */
    if (!mr_ch32_irq_valid(priv->irqs[channel])) {
        return -MR_ENOSYS;
    }

    /* Attach DMA event */
    mask = mr_port_irq_disable();
    state = &priv->channels[channel];
    ret = 0;
    if (!MR_OBJECT_IS_INITED(&state->waitable)) {
        ret = mr_waitable_init(&state->waitable);
    }
    if (ret == 0) {
        ret = mr_waiter_wait(entry, &state->waitable);
    }
    mr_port_irq_enable(mask);
    return ret;
}

static mr_err_t dma_event_control(const mr_driver_t *drv, mr_uint32_t channel,
                                  mr_uint32_t events, mr_bool_t enable) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
    dma_channel_t *state;
    mr_uint32_t bits;
    int mask, irq;
    mr_err_t ret;
    dma_hw_t hw;

    /* Resolve channel */
    ret = dma_get_hw(priv, channel, &hw);
    if (ret != 0) {
        return ret;
    }

    /* Check parameter */
    if ((events == 0U) || ((events & ~CH32_DMA_EVENTS) != 0U)) {
        return -MR_EINVAL;
    }

    /* Check IRQ support */
    irq = priv->irqs[channel];
    if (!mr_ch32_irq_valid(irq)) {
        return -MR_ENOSYS;
    }

    /* Lock */
    mask = mr_port_irq_disable();
    state = &priv->channels[channel];

    /* Check channel state */
    if (!state->configured) {
        ret = -MR_ENODEV;
        goto _exit;
    }

    /* Update event mask */
    if (enable) {
        state->events |= events;
    } else {
        state->events &= ~events;
    }

    /* Update channel IRQ */
    if (dma_active(&hw)) {
        bits = dma_irq_bits(state->events);
        if (bits != 0U) {
            NVIC_SetPriority((IRQn_Type)irq, priv->irq_priority);
            NVIC_EnableIRQ((IRQn_Type)irq);
            bits |= DMA_CFGR1_TEIE;
        }
        hw.channel->CFGR = (hw.channel->CFGR & ~CH32_DMA_IRQ_MASK) | bits;
    }
_exit:
    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

MR_UNUSED static void dma_irq(const mr_driver_t *drv, mr_uint32_t channel) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_uint32_t status, events, control;
    dma_channel_t *state;
    dma_hw_t hw;
    int mask;

    /* Resolve channel */
    if (dma_get_hw(priv, channel, &hw) != 0) {
        return;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check controller ownership */
    if (__dma_owners[hw.controller] != drv) {
        mr_port_irq_enable(mask);
        return;
    }
    state = &priv->channels[channel];

    /* Handle channel IRQ */
    control = hw.channel->CFGR;
    status = dma_pending(&hw) & (control & CH32_DMA_IRQ_MASK);
    hw.status->INTFCR = status << hw.shift;
    events = 0U;
    if ((status & CH32_DMA_FLAG_ERROR) != 0U) {
        state->failed = MR_TRUE;
        hw.channel->CFGR = control & ~(DMA_CFGR1_EN | CH32_DMA_IRQ_MASK);
        events = MR_DMA_WAIT_ERROR;
    } else {
        if ((status & CH32_DMA_FLAG_HALF) != 0U) {
            events |= MR_DMA_WAIT_HALF;
        }
        if ((status & CH32_DMA_FLAG_DONE) != 0U) {
            if ((control & DMA_CFGR1_CIRC) == 0U) {
                hw.channel->CFGR
                    = control & ~(DMA_CFGR1_EN | CH32_DMA_IRQ_MASK);
            }
            events |= MR_DMA_WAIT_DONE;
        }
    }

    /* Waiters consume the buffer after the hardware status is committed */
    __asm__ volatile ("fence iorw, iorw" ::: "memory");

    /* Raise DMA event */
    events &= state->events;
    if ((events != 0U) && MR_DRIVER_IS_SETUP(drv) && !MR_DRIVER_IS_DYING(drv)
        && MR_OBJECT_IS_INITED(&state->waitable)) {
        mr_waitable_raise(&state->waitable, events, MR_NULL);
    }

    /* Unlock */
    mr_port_irq_enable(mask);
}

/* Instance install */
#define CH32_HT_DMA_INSTALL(_id, _ops)                                         \
    static const int MR_CAT(__dma_irqs_, _id)[] = {                            \
        MR_LISTIFY_NEST(CH32_DMA_CHANNELS, (,), __CH32_HT_DMA_IRQ_ENTRY, _id), \
    };                                                                         \
    static dma_priv_t MR_CAT(__dma_priv_, _id) = {                             \
        .instance = MR_HT_DRIVER_PROP(_id, wch_dmax),                          \
        .irqs = MR_CAT(__dma_irqs_, _id),                                      \
        .irq_priority = MR_HT_DRIVER_PROP_OR(_id, wch_irq_priority, 0U),       \
    };                                                                         \
    MR_LISTIFY_NEST(CH32_DMA_CHANNELS, (), __CH32_HT_DMA_IRQ_HANDLER, _id)     \
    MR_HT_DMA_INSTALL(_id, &MR_CAT(__dma_priv_, _id), _ops)
#define __CH32_HT_DMA_IRQN(_id, _n)                                            \
    MR_HT_DRIVER_PROP_OR(_id, MR_CAT(wch_irqn_, _n), CH32_DMA_NO_IRQ)
#define __CH32_HT_DMA_IRQ_ENTRY(_n, _id)                                       \
    __CH32_HT_DMA_IRQN(_id, _n)
#define __CH32_HT_DMA_IRQ_HANDLER(_n, _id)                                     \
    MR_MACRO_IF(                                                               \
        MR_HT_DRIVER_HAS_PROP(_id, MR_CAT(wch_irq_handler_, _n)),              \
        (MR_CH32_INTERRUPT void                                                \
             MR_HT_DRIVER_PROP(_id, MR_CAT(wch_irq_handler_, _n))(void) {      \
             dma_irq(MR_HT_DRIVER_FIND(MR_HT_DRIVER(_id)), (_n));              \
         }),                                                                   \
        ())
MR_HT_DRIVER_FOREACH(CH32_HT_DMA_INSTALL, &__dma_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_DMA) */

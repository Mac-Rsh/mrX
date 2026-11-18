/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-11-17        MacRsh        Add classic STM32 DMA variants
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_DMA)
#include <mrX/drivers/dma/mr_dma.h>
#if defined(MR_USE_DCACHE)
#include <mrX/port/mr_port_cache.h>
#define DMA_USE_DCACHE
#endif /* defined(MR_USE_DCACHE) */

/* HardTree compatible */
#define MR_HT_COMPAT                    st_stm32_dma
#if MR_HT_HAS_DRIVER()
#if defined(GPDMA1) && defined(DMA_GPDMA_LINEAR_NODE)
#define STM32_DMA_GPDMA
#elif defined(DMA_SxCR_EN) && defined(DMA1_Stream0)
#define STM32_DMA_STREAM
#elif defined(DMA_CCR_EN) && defined(DMA1_Channel1)
#define STM32_DMA_CHANNEL
#else
#error "Unsupported STM32 DMA IP"
#endif /* STM32 DMA IP variant */

#if (defined(DMAMUX1) || defined(DMAMUX1_Channel0) || defined(DMA_REQUEST_0))  \
    && !defined(STM32_DMA_GPDMA)
#define STM32_DMA_HAS_REQUEST
#endif /* (defined(DMAMUX1) || defined(DMAMUX1_Channel0) ||                    \
    defined(DMA_REQUEST_0)) && !defined(STM32_DMA_GPDMA) */

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
#if defined(MR_USE_WAIT)
static mr_err_t dma_event_attach(const mr_driver_t *drv, mr_uint32_t channel,
                                 mr_waiter_t *entry);
static mr_err_t dma_event_control(const mr_driver_t *drv, mr_uint32_t channel,
                                  mr_uint32_t events, mr_bool_t enable);
#endif /* defined(MR_USE_WAIT) */
MR_UNUSED static const mr_dma_ops_t __dma_ops = {
    .channel_configure = dma_channel_configure,
    .channel_configuration = dma_channel_configuration,
    .start = dma_start,
    .stop = dma_stop,
    .is_busy = dma_is_busy,
    .offset = dma_offset,
#if defined(MR_USE_WAIT)
    .event_attach = dma_event_attach,
    .event_control = dma_event_control,
#endif /* defined(MR_USE_WAIT) */
};

/* DMA channel type */
typedef struct dma_channel {
    DMA_HandleTypeDef handle;
#if defined(STM32_DMA_GPDMA)
    DMA_NodeTypeDef node;
    DMA_QListTypeDef queue;
#endif /* defined(STM32_DMA_GPDMA) */
    mr_dma_config_t config;
    mr_size_t half_count;
    mr_size_t count;
    mr_atomic_t last_offset;
#if defined(DMA_USE_DCACHE)
    const void *src;
    void *dst;
    mr_size_t cache_count;
#endif /* defined(DMA_USE_DCACHE) */
    mr_uint8_t circular: 1;
#if defined(MR_USE_WAIT)
    mr_uint32_t event_mask;
    mr_waitable_t events;
#endif /* defined(MR_USE_WAIT) */
} dma_channel_t;

/* DMA route type */
typedef struct dma_route {
#if defined(STM32_DMA_GPDMA)
    DMA_TriggerConfTypeDef trigger;
#endif /* defined(STM32_DMA_GPDMA) */
    uint32_t request;
    IRQn_Type irqn;
    mr_uint8_t irq_valid: 1;
} dma_route_t;

/* DMA private data type */
typedef struct dma_priv {
    DMA_TypeDef *instance;
    void (*clk_enable)(void);
    const dma_route_t *routes;
    mr_size_t routes_num;
    mr_uint32_t irq_priority;
    dma_channel_t *channels;
} dma_priv_t;

/* DMA channel register layout definition */
#if defined(STM32_DMA_GPDMA)
#define DMA_CHANNELS                    (8U)
#define DMA_CHANNEL_OFFSET              (0x50U)
#define DMA_CHANNEL_STRIDE              (0x80U)
#elif defined(STM32_DMA_STREAM)
#define DMA_CHANNELS                    (8U)
#define DMA_CHANNEL_OFFSET                                                     \
    ((mr_uintptr_t)DMA1_Stream0 - (mr_uintptr_t)DMA1)
#define DMA_CHANNEL_STRIDE                                                     \
    ((mr_uintptr_t)DMA1_Stream1 - (mr_uintptr_t)DMA1_Stream0)
#else
#if defined(DMA1_Channel8)
#define DMA_CHANNELS                    (8U)
#elif defined(DMA1_Channel7)
#define DMA_CHANNELS                    (7U)
#elif defined(DMA1_Channel6)
#define DMA_CHANNELS                    (6U)
#elif defined(DMA1_Channel5)
#define DMA_CHANNELS                    (5U)
#else
#error "Unsupported STM32 classic DMA channel count"
#endif /* classic DMA channel count */
#define DMA_CHANNEL_OFFSET                                                     \
    ((mr_uintptr_t)DMA1_Channel1 - (mr_uintptr_t)DMA1)
#define DMA_CHANNEL_STRIDE                                                     \
    ((mr_uintptr_t)DMA1_Channel2 - (mr_uintptr_t)DMA1_Channel1)
#endif /* STM32 DMA IP variant */

/* DMA event and interrupt mask definition */
#define DMA_EVENT_MASK                                                         \
    (MR_DMA_WAIT_HALF | MR_DMA_WAIT_DONE | MR_DMA_WAIT_ERROR)
#if defined(STM32_DMA_GPDMA)
#define DMA_ERROR_IT                                                           \
    (DMA_IT_DTE | DMA_IT_ULE | DMA_IT_USE | DMA_IT_TO)
#elif defined(STM32_DMA_STREAM) && defined(DMA_IT_DME)
#define DMA_ERROR_IT                    (DMA_IT_TE | DMA_IT_DME)
#else
#define DMA_ERROR_IT                    DMA_IT_TE
#endif /* STM32 DMA IP variant */

#if defined(STM32_DMA_GPDMA)
/* Source transfer width mapping */
static const uint32_t __dma_src_width[] = {
    DMA_SRC_DATAWIDTH_BYTE,
    DMA_SRC_DATAWIDTH_HALFWORD,
    DMA_SRC_DATAWIDTH_WORD,
};

/* Destination transfer width mapping */
static const uint32_t __dma_dst_width[] = {
    DMA_DEST_DATAWIDTH_BYTE,
    DMA_DEST_DATAWIDTH_HALFWORD,
    DMA_DEST_DATAWIDTH_WORD,
};

/* Transfer burst mapping */
static const uint32_t __dma_burst[] = {
    1U, 2U, 4U, 8U, 16U, 32U, 64U,
};
#else
/* Classic DMA memory transfer width mapping */
static const uint32_t __dma_mem_width[] = {
    DMA_MDATAALIGN_BYTE,
    DMA_MDATAALIGN_HALFWORD,
    DMA_MDATAALIGN_WORD,
};

/* Classic DMA peripheral transfer width mapping */
static const uint32_t __dma_periph_width[] = {
    DMA_PDATAALIGN_BYTE,
    DMA_PDATAALIGN_HALFWORD,
    DMA_PDATAALIGN_WORD,
};
#endif /* defined(STM32_DMA_GPDMA) */

/* Transfer direction mapping */
static const uint32_t __dma_direction[] = {
    DMA_PERIPH_TO_MEMORY,
    DMA_MEMORY_TO_PERIPH,
    DMA_MEMORY_TO_MEMORY,
};

static dma_channel_t *dma_get_channel(dma_priv_t *priv, mr_uint32_t channel) {
    /* Check channel mapping */
    if ((channel >= priv->routes_num) || (channel >= DMA_CHANNELS)) {
        return MR_NULL;
    }
    return &priv->channels[channel];
}

#if defined(STM32_DMA_GPDMA)
static mr_err_t dma_default_irqn(const dma_priv_t *priv, mr_uint32_t channel,
                                 IRQn_Type *irqn) {
    static const IRQn_Type gpdma1_irqs[] = {
        GPDMA1_Channel0_IRQn, GPDMA1_Channel1_IRQn, GPDMA1_Channel2_IRQn,
        GPDMA1_Channel3_IRQn, GPDMA1_Channel4_IRQn, GPDMA1_Channel5_IRQn,
        GPDMA1_Channel6_IRQn, GPDMA1_Channel7_IRQn,
    };
#if defined(GPDMA2)
    static const IRQn_Type gpdma2_irqs[] = {
        GPDMA2_Channel0_IRQn, GPDMA2_Channel1_IRQn, GPDMA2_Channel2_IRQn,
        GPDMA2_Channel3_IRQn, GPDMA2_Channel4_IRQn, GPDMA2_Channel5_IRQn,
        GPDMA2_Channel6_IRQn, GPDMA2_Channel7_IRQn,
    };
#endif /* defined(GPDMA2) */

    /* Resolve fixed GPDMA channel IRQ */
    if (channel >= DMA_CHANNELS) {
        return -MR_EINVAL;
    }
    if (priv->instance == GPDMA1) {
        *irqn = gpdma1_irqs[channel];
        return 0;
    }
#if defined(GPDMA2)
    if (priv->instance == GPDMA2) {
        *irqn = gpdma2_irqs[channel];
        return 0;
    }
#endif /* defined(GPDMA2) */
    return -MR_EINVAL;
}
#endif /* defined(STM32_DMA_GPDMA) */

static mr_err_t dma_irqn(const dma_priv_t *priv, mr_uint32_t channel,
                         IRQn_Type *irqn) {
    /* Prefer explicit board IRQ routing */
    if ((priv->routes != MR_NULL) && (channel < priv->routes_num)
        && priv->routes[channel].irq_valid) {
        *irqn = priv->routes[channel].irqn;
        return 0;
    }

    /* Fall back to fixed GPDMA channel IRQ */
#if defined(STM32_DMA_GPDMA)
    return dma_default_irqn(priv, channel, irqn);
#else
    return -MR_EINVAL;
#endif /* defined(STM32_DMA_GPDMA) */
}

static void dma_apply_events(dma_channel_t *channel) {
    mr_bool_t half_required, completion_required;

    /* Set mandatory interrupt requirements */
    half_required = MR_FALSE;
    completion_required = !channel->circular;

#if defined(MR_USE_WAIT)
    /* Merge event subscriptions */
    half_required = (channel->event_mask & MR_DMA_WAIT_HALF) != 0U;
    completion_required |= (channel->event_mask & MR_DMA_WAIT_DONE) != 0U;

#if defined(DMA_USE_DCACHE)
    /* Keep TC for circular cache ownership rollover */
    completion_required |= channel->circular && half_required
                        && (channel->config.direction != MR_DMA_DIR_M2P);
#endif /* defined(DMA_USE_DCACHE) */
#endif /* defined(MR_USE_WAIT) */

    /* Configure transfer complete interrupt */
    if (completion_required) {
        __HAL_DMA_ENABLE_IT(&channel->handle, DMA_IT_TC);
    } else {
        __HAL_DMA_DISABLE_IT(&channel->handle, DMA_IT_TC);
    }

    /* Keep fault IRQs independent of event subscriptions */
    __HAL_DMA_ENABLE_IT(&channel->handle, DMA_ERROR_IT);
#if defined(STM32_DMA_STREAM) && defined(DMA_IT_FE)
    __HAL_DMA_ENABLE_IT(&channel->handle, DMA_IT_FE);
#endif /* defined(STM32_DMA_STREAM) && defined(DMA_IT_FE) */

    /* Configure half-transfer interrupt */
    if (half_required) {
        __HAL_DMA_ENABLE_IT(&channel->handle, DMA_IT_HT);
    } else {
        __HAL_DMA_DISABLE_IT(&channel->handle, DMA_IT_HT);
    }
}

static mr_bool_t dma_hw_busy(const dma_channel_t *channel) {
    HAL_DMA_StateTypeDef state;

    /* Read HAL state */
    state = HAL_DMA_GetState(&channel->handle);

#if defined(STM32_DMA_GPDMA)
    /* Treat suspended GPDMA as active */
    return ((state == HAL_DMA_STATE_BUSY) || (state == HAL_DMA_STATE_SUSPEND));
#else
    return (state == HAL_DMA_STATE_BUSY);
#endif /* defined(STM32_DMA_GPDMA) */
}

static mr_err_t dma_width(mr_uint32_t width, const uint32_t *table,
                          uint32_t *hal_width) {
    /* Check transfer width */
    if (width == MR_DMA_WIDTH_64) {
        return -MR_ENOSYS;
    }
    if (width > MR_DMA_WIDTH_32) {
        return -MR_EINVAL;
    }

    /* Convert to HAL width */
    *hal_width = table[width];
    return 0;
}

static mr_err_t dma_adjust(mr_uint32_t adjust, uint32_t increment,
                           uint32_t *hal_adjust) {
    /* Check address adjustment */
    if (adjust == MR_DMA_ADJ_DEC) {
        return -MR_ENOSYS;
    }
    if (adjust > MR_DMA_ADJ_CONST) {
        return -MR_EINVAL;
    }

    /* Convert to HAL increment mode */
    *hal_adjust = (adjust == MR_DMA_ADJ_INC) ? increment : 0U;
    return 0;
}

#if defined(STM32_DMA_GPDMA)
static mr_err_t dma_burst(mr_uint32_t burst, uint32_t *hal_burst) {
    /* Check GPDMA burst length */
    if (burst >= MR_DMA_BURST_128) {
        return (burst <= MR_DMA_BURST_256) ? -MR_ENOSYS : -MR_EINVAL;
    }

    /* Convert to HAL burst length */
    *hal_burst = __dma_burst[burst];
    return 0;
}
#endif /* defined(STM32_DMA_GPDMA) */

static mr_err_t dma_request(const dma_priv_t *priv, mr_uint32_t channel,
                            mr_uint32_t direction, uint32_t *request) {
    /* Resolve memory-to-memory request */
#if defined(STM32_DMA_GPDMA)
    if (direction == MR_DMA_DIR_M2M) {
        *request = DMA_REQUEST_SW;
        return 0;
    }
#elif defined(STM32_DMA_HAS_REQUEST) && defined(DMA_REQUEST_MEM2MEM)
    if (direction == MR_DMA_DIR_M2M) {
        *request = DMA_REQUEST_MEM2MEM;
        return 0;
    }
#endif /* defined(STM32_DMA_GPDMA) */

    /* Resolve peripheral request from board routing */
    if ((priv->routes == MR_NULL) || (channel >= priv->routes_num)) {
        return -MR_EINVAL;
    }
    *request = priv->routes[channel].request;
    return 0;
}

#if defined(STM32_DMA_GPDMA)
static const DMA_TriggerConfTypeDef *dma_trigger(const dma_priv_t *priv,
                                                 mr_uint32_t channel) {
    /* Resolve channel trigger from board routing */
    if ((priv->routes == MR_NULL) || (channel >= priv->routes_num)) {
        return MR_NULL;
    }
    return &priv->routes[channel].trigger;
}
#endif /* defined(STM32_DMA_GPDMA) */

static mr_err_t dma_make_config(const mr_dma_config_t *config, uint32_t request,
                                DMA_InitTypeDef *init) {
#if !defined(STM32_DMA_GPDMA)
    uint32_t mem_width, periph_width;
    uint32_t mem_adj, periph_adj;
#endif /* !defined(STM32_DMA_GPDMA) */
    mr_err_t ret;

    /* Check transfer direction */
    if (config->direction > MR_DMA_DIR_M2M) {
        return -MR_EINVAL;
    }

#if defined(STM32_DMA_STREAM)
    /* Reject stream width conversion without FIFO packing */
    if (config->src_width != config->dst_width) {
        return -MR_ENOSYS;
    }
#endif /* defined(STM32_DMA_STREAM) */

#if defined(STM32_DMA_GPDMA)
    /* Configure endpoint widths */
    ret = dma_width(config->src_width, __dma_src_width, &init->SrcDataWidth);
    if (ret != 0) {
        return ret;
    }
    ret = dma_width(config->dst_width, __dma_dst_width, &init->DestDataWidth);
    if (ret != 0) {
        return ret;
    }

    /* Configure endpoint addressing */
    ret = dma_adjust(config->src_adj, DMA_SINC_INCREMENTED, &init->SrcInc);
    if (ret != 0) {
        return ret;
    }
    ret = dma_adjust(config->dst_adj, DMA_DINC_INCREMENTED, &init->DestInc);
    if (ret != 0) {
        return ret;
    }

    /* Configure endpoint bursts */
    ret = dma_burst(config->src_burst, &init->SrcBurstLength);
    if (ret != 0) {
        return ret;
    }
    ret = dma_burst(config->dst_burst, &init->DestBurstLength);
    if (ret != 0) {
        return ret;
    }

    /* HAL uses the direction to set SWREQ in linear mode */
    init->Direction = (request == DMA_REQUEST_SW)
                        ? DMA_MEMORY_TO_MEMORY
                        : __dma_direction[config->direction];
    init->Request = request;
    init->BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    init->Priority = DMA_LOW_PRIORITY_HIGH_WEIGHT;
    if (config->direction == MR_DMA_DIR_M2P) {
        init->TransferAllocatedPort
            = DMA_SRC_ALLOCATED_PORT1 | DMA_DEST_ALLOCATED_PORT0;
    } else {
        init->TransferAllocatedPort
            = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT1;
    }
    init->TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    init->Mode = DMA_NORMAL;
#else
    /* Check classic DMA burst mode */
    if ((config->src_burst != MR_DMA_BURST_1)
        || (config->dst_burst != MR_DMA_BURST_1)) {
        return -MR_ENOSYS;
    }

    /* Map source and destination endpoints */
    if (config->direction == MR_DMA_DIR_M2P) {
        ret = dma_width(config->src_width, __dma_mem_width, &mem_width);
        if (ret != 0) {
            return ret;
        }
        ret = dma_width(config->dst_width, __dma_periph_width, &periph_width);
        if (ret != 0) {
            return ret;
        }
        ret = dma_adjust(config->src_adj, DMA_MINC_ENABLE, &mem_adj);
        if (ret != 0) {
            return ret;
        }
        ret = dma_adjust(config->dst_adj, DMA_PINC_ENABLE, &periph_adj);
        if (ret != 0) {
            return ret;
        }
    } else {
        ret = dma_width(config->src_width, __dma_periph_width, &periph_width);
        if (ret != 0) {
            return ret;
        }
        ret = dma_width(config->dst_width, __dma_mem_width, &mem_width);
        if (ret != 0) {
            return ret;
        }
        ret = dma_adjust(config->src_adj, DMA_PINC_ENABLE, &periph_adj);
        if (ret != 0) {
            return ret;
        }
        ret = dma_adjust(config->dst_adj, DMA_MINC_ENABLE, &mem_adj);
        if (ret != 0) {
            return ret;
        }
    }

    /* Configure classic DMA transfer */
    init->Direction = __dma_direction[config->direction];
    init->PeriphInc = periph_adj;
    init->MemInc = mem_adj;
    init->PeriphDataAlignment = periph_width;
    init->MemDataAlignment = mem_width;
    init->Mode = DMA_NORMAL;
    init->Priority = DMA_PRIORITY_LOW;

    /* Configure request routing */
#if defined(STM32_DMA_HAS_REQUEST)
    init->Request = request;
#elif defined(STM32_DMA_STREAM) && defined(DMA_SxCR_CHSEL)
    init->Channel = request;
#else
    MR_IGNORE(request);
#endif /* STM32 classic DMA request routing */

    /* Configure stream burst and FIFO mode */
#if defined(STM32_DMA_STREAM) && defined(DMA_MBURST_SINGLE)                    \
    && defined(DMA_PBURST_SINGLE)
    init->MemBurst = DMA_MBURST_SINGLE;
    init->PeriphBurst = DMA_PBURST_SINGLE;
#if defined(DMA_FIFOMODE_DISABLE)
    init->FIFOMode = DMA_FIFOMODE_DISABLE;
    init->FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
#endif /* defined(DMA_FIFOMODE_DISABLE) */
#endif /* defined(STM32_DMA_STREAM)                                            \
        && defined(DMA_MBURST_SINGLE) && defined(DMA_PBURST_SINGLE) */
#endif /* defined(STM32_DMA_GPDMA) */
    return 0;
}

#if defined(STM32_DMA_STREAM)
static DMA_Stream_TypeDef *dma_instance(const dma_priv_t *priv,
                                        mr_uint32_t channel) {
#else
static DMA_Channel_TypeDef *dma_instance(const dma_priv_t *priv,
                                         mr_uint32_t channel) {
#endif /* defined(STM32_DMA_STREAM) */
    mr_uintptr_t address;

    /* Derive channel register address from controller layout */
    address = (mr_uintptr_t)priv->instance + DMA_CHANNEL_OFFSET
            + ((mr_uintptr_t)channel * DMA_CHANNEL_STRIDE);
#if defined(STM32_DMA_STREAM)
    return (DMA_Stream_TypeDef *)address;
#else
    return (DMA_Channel_TypeDef *)address;
#endif /* defined(STM32_DMA_STREAM) */
}

static mr_size_t dma_width_size(mr_uint32_t width) {
    return (mr_size_t)1U << width;
}

static mr_bool_t dma_address_valid(mr_uintptr_t address, mr_uint32_t adjust,
                                   mr_size_t width, mr_size_t count) {
    mr_size_t span;

    /* Get endpoint byte span */
    span = (adjust == MR_DMA_ADJ_INC) ? count * width : width;

    /* Check address range and alignment */
    if ((address > MR_UINT32_MAX) || ((address % width) != 0U)) {
        return MR_FALSE;
    }

    /* Check endpoint span */
    if (span == 0U) {
        return MR_FALSE;
    }

    /* Check endpoint address range */
    return (span - 1U) <= ((mr_size_t)MR_UINT32_MAX - address);
}

static mr_err_t dma_transfer_count(const dma_channel_t *channel,
                                   mr_size_t count, uint32_t *hw_count) {
#if defined(STM32_DMA_GPDMA)
    mr_size_t src_size;

    /* GPDMA BNDT counts source-side bytes */
    src_size = dma_width_size(channel->config.src_width);
    if (count > (DMA_CBR1_BNDT / src_size)) {
        return -MR_ERANGE;
    }
    *hw_count = (uint32_t)(count * src_size);
#else
    /* Classic NDTR/CNDTR count transfer items */
#if defined(IS_DMA_BUFFER_SIZE)
    if (!IS_DMA_BUFFER_SIZE(count)) {
        return -MR_ERANGE;
    }
#else
    if (count > 0xffffU) {
        return -MR_ERANGE;
    }
#endif /* defined(IS_DMA_BUFFER_SIZE) */
    *hw_count = (uint32_t)count;
#endif /* defined(STM32_DMA_GPDMA) */
    return 0;
}

static mr_err_t dma_transfer_validate(const dma_channel_t *channel,
                                      const mr_dma_transfer_t *transfer) {
    mr_size_t src_size, dst_size;
    mr_uintptr_t src, dst;

    /* Resolve transfer endpoints */
    src_size = dma_width_size(channel->config.src_width);
    dst_size = dma_width_size(channel->config.dst_width);
    src = (mr_uintptr_t)transfer->src;
    dst = (mr_uintptr_t)transfer->dst;

    /* Validate source endpoint */
    if (!dma_address_valid(src, channel->config.src_adj, src_size,
                           transfer->count)) {
        return -MR_EINVAL;
    }

    /* Validate destination endpoint */
    if (!dma_address_valid(dst, channel->config.dst_adj, dst_size,
                           transfer->count)) {
        return -MR_EINVAL;
    }

    return 0;
}

static mr_size_t dma_current_count(const dma_channel_t *channel) {
    mr_size_t remaining, count;
#if defined(STM32_DMA_GPDMA)
    mr_size_t src_size;
#endif /* defined(STM32_DMA_GPDMA) */

    /* Check empty transfer */
    if (channel->count == 0U) {
        return 0U;
    }

    /* Read hardware remaining count */
    remaining = (mr_size_t)__HAL_DMA_GET_COUNTER(&channel->handle);

#if defined(STM32_DMA_GPDMA)
    /* Convert remaining source bytes to logical items */
    src_size = dma_width_size(channel->config.src_width);
    remaining = (remaining + src_size - 1U) / src_size;
#endif /* defined(STM32_DMA_GPDMA) */

    /* Validate remaining item count */
    if (remaining > channel->count) {
        return 0U;
    }

    /* Calculate completed item count */
    count = channel->count - remaining;

    /* Normalize circular progress */
    if (channel->circular) {
        count %= channel->count;
    }
    return count;
}

static mr_size_t dma_count_offset(const dma_channel_t *channel,
                                  mr_size_t count) {
    return count * dma_width_size(channel->config.src_width);
}

#if defined(DMA_USE_DCACHE)
static mr_size_t dma_memory_size(mr_size_t count, mr_uint32_t adjust,
                                 mr_uint32_t width) {
    /* Convert item count to endpoint byte span */
    return (adjust == MR_DMA_ADJ_INC) ? count * dma_width_size(width)
                                      : dma_width_size(width);
}

static mr_err_t dma_cache_prepare(dma_channel_t *channel,
                                  const mr_dma_transfer_t *transfer) {
    mr_size_t size;
    mr_err_t ret;

    /* Transfer cache ownership to DMA */
    switch (channel->config.direction) {
        case MR_DMA_DIR_M2P: {
            size = dma_memory_size(transfer->count, channel->config.src_adj,
                                   channel->config.src_width);
            return mr_port_dcache_clean(transfer->src, size);
        }
        case MR_DMA_DIR_P2M: {
            size = dma_memory_size(transfer->count, channel->config.dst_adj,
                                   channel->config.dst_width);
            return mr_port_dcache_clean_invalidate(transfer->dst, size);
        }
        case MR_DMA_DIR_M2M: {
            size = dma_memory_size(transfer->count, channel->config.src_adj,
                                   channel->config.src_width);
            ret = mr_port_dcache_clean(transfer->src, size);
            if (ret != 0) {
                return ret;
            }
            size = dma_memory_size(transfer->count, channel->config.dst_adj,
                                   channel->config.dst_width);
            return mr_port_dcache_clean_invalidate(transfer->dst, size);
        }
        default: {
            return -MR_EINVAL;
        }
    }
}

static mr_err_t dma_cache_invalidate_dst(dma_channel_t *channel,
                                         mr_size_t start, mr_size_t end) {
    const mr_uint8_t *dst;
    mr_size_t unit, size;

    /* Skip transfers without destination memory */
    if (channel->config.direction == MR_DMA_DIR_M2P) {
        return 0;
    }

    /* Resolve destination cache unit */
    dst = channel->dst;
    unit = dma_width_size(channel->config.dst_width);
    if (channel->config.dst_adj != MR_DMA_ADJ_INC) {
        return mr_port_dcache_invalidate(dst, unit);
    }

    /* Check completed item range */
    if (end <= start) {
        return 0;
    }

    /* Invalidate completed destination range */
    size = (end - start) * unit;
    return mr_port_dcache_invalidate(dst + (start * unit), size);
}

#if defined(MR_USE_WAIT)
static mr_err_t dma_cache_clean_src(dma_channel_t *channel, mr_size_t start,
                                    mr_size_t end) {
    const mr_uint8_t *src;
    mr_size_t unit, size;

    /* Skip transfers without source memory */
    if (channel->config.direction == MR_DMA_DIR_P2M) {
        return 0;
    }

    /* Resolve source cache unit */
    src = channel->src;
    unit = dma_width_size(channel->config.src_width);
    if (channel->config.src_adj != MR_DMA_ADJ_INC) {
        return mr_port_dcache_clean(src, unit);
    }

    /* Check published item range */
    if (end <= start) {
        return 0;
    }

    /* Clean published source range */
    size = (end - start) * unit;
    return mr_port_dcache_clean(src + (start * unit), size);
}
#endif /* defined(MR_USE_WAIT) */

static mr_err_t dma_cache_complete(dma_channel_t *channel, mr_size_t count) {
    mr_err_t ret;

    /* Skip transfers without destination memory */
    if (channel->config.direction == MR_DMA_DIR_M2P) {
        return 0;
    }

    /* Check completed item range */
    if (count <= channel->cache_count) {
        return 0;
    }

    /* Return newly completed destination items to CPU */
    ret = dma_cache_invalidate_dst(channel, channel->cache_count, count);
    if (ret != 0) {
        return ret;
    }

    /* Advance destination cache ownership */
    channel->cache_count = count;
    return 0;
}
#endif /* defined(DMA_USE_DCACHE) */

#if defined(MR_USE_WAIT)
static void dma_raise_event(dma_channel_t *channel, mr_wait_event_t event) {
    /* Raise subscribed events only */
    if ((channel->event_mask & event) == 0U) {
        return;
    }
    if (MR_OBJECT_IS_INITED((mr_object_t *)&channel->events)) {
        mr_waitable_raise(&channel->events, event, MR_NULL);
    }
}
#endif /* defined(MR_USE_WAIT) */

static void dma_transfer_complete(DMA_HandleTypeDef *handle) {
    dma_channel_t *channel;
#if defined(DMA_USE_DCACHE) && defined(MR_USE_WAIT)
    mr_size_t half;
#endif /* defined(DMA_USE_DCACHE) && defined(MR_USE_WAIT) */
#if defined(DMA_USE_DCACHE)
    mr_err_t ret;
#endif /* defined(DMA_USE_DCACHE) */

    /* Get callback state */
    channel = handle->Parent;
#if defined(DMA_USE_DCACHE)
    ret = 0;
#if defined(MR_USE_WAIT)
    half = channel->half_count;
#endif /* defined(MR_USE_WAIT) */
#endif /* defined(DMA_USE_DCACHE) */

    if (!channel->circular) {
        /* Save completed source-side byte progress */
        mr_atomic_store(&channel->last_offset,
                        (mr_atomic_t)dma_count_offset(channel, channel->count));

#if defined(DMA_USE_DCACHE)
        /* Return destination cache ownership before DONE */
        ret = dma_cache_complete(channel, channel->count);
        if (ret != 0) {
#if defined(MR_USE_WAIT)
            dma_raise_event(channel, MR_DMA_WAIT_ERROR);
#endif /* defined(MR_USE_WAIT) */
            return;
        }
#endif /* defined(DMA_USE_DCACHE) */

#if defined(MR_USE_WAIT)
        /* Notify transfer completion */
        dma_raise_event(channel, MR_DMA_WAIT_DONE);
#endif /* defined(MR_USE_WAIT) */
        return;
    }

#if defined(MR_USE_WAIT)
    /* Handle circular block completion */
    if ((channel->event_mask & MR_DMA_WAIT_DONE) != 0U) {
#if defined(DMA_USE_DCACHE)
        /* Return second destination half before DONE */
        if (channel->config.direction != MR_DMA_DIR_M2P) {
            ret = dma_cache_invalidate_dst(channel, half, channel->count);
            if (ret != 0) {
                dma_raise_event(channel, MR_DMA_WAIT_ERROR);
                channel->cache_count = 0U;
                return;
            }
        }
#endif /* defined(DMA_USE_DCACHE) */

        /* Notify block completion */
        dma_raise_event(channel, MR_DMA_WAIT_DONE);

#if defined(DMA_USE_DCACHE)
        /* Publish second source half before DMA reuse */
        if (channel->config.direction != MR_DMA_DIR_P2M) {
            ret = dma_cache_clean_src(channel, half, channel->count);
            if (ret != 0) {
                dma_raise_event(channel, MR_DMA_WAIT_ERROR);
            }
        }
#endif /* defined(DMA_USE_DCACHE) */
    }
#endif /* defined(MR_USE_WAIT) */

#if defined(DMA_USE_DCACHE)
    /* Reset destination ownership at circular wrap */
    channel->cache_count = 0U;
#endif /* defined(DMA_USE_DCACHE) */
}

static void dma_half_complete(DMA_HandleTypeDef *handle) {
    dma_channel_t *channel;
#if defined(MR_USE_WAIT)
#if defined(DMA_USE_DCACHE)
    mr_size_t half;
    mr_err_t ret;
#endif /* defined(DMA_USE_DCACHE) */
#endif /* defined(MR_USE_WAIT) */

    /* Get callback state */
    channel = handle->Parent;
#if defined(MR_USE_WAIT) && defined(DMA_USE_DCACHE)
    half = channel->half_count;
#endif /* defined(MR_USE_WAIT) && defined(DMA_USE_DCACHE) */

#if defined(MR_USE_WAIT)
#if defined(DMA_USE_DCACHE)
    /* Return first destination half before HALF */
    ret = dma_cache_complete(channel, half);
    if (ret != 0) {
        dma_raise_event(channel, MR_DMA_WAIT_ERROR);
        return;
    }
#endif /* defined(DMA_USE_DCACHE) */

    /* Notify half completion */
    dma_raise_event(channel, MR_DMA_WAIT_HALF);

#if defined(DMA_USE_DCACHE)
    /* Publish first source half before DMA reuse */
    if (channel->circular && (channel->config.direction != MR_DMA_DIR_P2M)) {
        ret = dma_cache_clean_src(channel, 0U, half);
        if (ret != 0) {
            dma_raise_event(channel, MR_DMA_WAIT_ERROR);
        }
    }
#endif /* defined(DMA_USE_DCACHE) */
#else
    MR_IGNORE(channel);
#endif /* defined(MR_USE_WAIT) */
}

static void dma_transfer_error(DMA_HandleTypeDef *handle) {
    dma_channel_t *channel = handle->Parent;
    mr_size_t count, offset;

    /* Preserve progress and cache visibility before ERROR */
    count = dma_current_count(channel);
    offset = dma_count_offset(channel, count);

    /* Save observable progress */
    mr_atomic_store(&channel->last_offset, (mr_atomic_t)offset);

#if defined(DMA_USE_DCACHE)
    /* Return completed destination data to CPU */
    dma_cache_complete(channel, count);
#endif /* defined(DMA_USE_DCACHE) */

#if defined(MR_USE_WAIT)
    /* Notify transfer error */
    dma_raise_event(channel, MR_DMA_WAIT_ERROR);
#else
    MR_IGNORE(channel);
#endif /* defined(MR_USE_WAIT) */
}

#if defined(STM32_DMA_GPDMA)
static mr_err_t dma_build_linked_list(dma_channel_t *dma_channel,
                                      const DMA_TriggerConfTypeDef *trigger,
                                      uint32_t src, uint32_t dst,
                                      uint32_t count) {
    DMA_NodeConfTypeDef node_config = {0};
    mr_err_t ret;

    /* Reset circular queue state */
    ret = mr_stm32_hal_status(HAL_DMAEx_List_ResetQ(&dma_channel->queue));
    if (ret != 0) {
        return ret;
    }

    /* Build one-node circular transfer */
    node_config.NodeType = DMA_GPDMA_LINEAR_NODE;
    node_config.Init = dma_channel->handle.Init;
    node_config.DataHandlingConfig.DataExchange = DMA_EXCHANGE_NONE;
    node_config.DataHandlingConfig.DataAlignment
        = DMA_DATA_RIGHTALIGN_ZEROPADDED;
    node_config.TriggerConfig = *trigger;
    node_config.SrcAddress = src;
    node_config.DstAddress = dst;
    node_config.DataSize = count;
    ret = mr_stm32_hal_status(
        HAL_DMAEx_List_BuildNode(&node_config, &dma_channel->node));
    if (ret != 0) {
        return ret;
    }
    ret = mr_stm32_hal_status(HAL_DMAEx_List_InsertNode_Tail(
        &dma_channel->queue, &dma_channel->node));
    if (ret != 0) {
        return ret;
    }
    ret = mr_stm32_hal_status(
        HAL_DMAEx_List_SetCircularMode(&dma_channel->queue));
    if (ret != 0) {
        return ret;
    }

    /* Attach circular queue to HAL channel */
    return mr_stm32_hal_status(
        HAL_DMAEx_List_LinkQ(&dma_channel->handle, &dma_channel->queue));
}

static mr_err_t dma_init_linear(dma_channel_t *dma_channel,
                                const DMA_TriggerConfTypeDef *trigger) {
    DMA_DataHandlingConfTypeDef data_handling = {0};
    mr_err_t ret;

    /* Initialize linear GPDMA channel */
    ret = mr_stm32_hal_status(HAL_DMA_Init(&dma_channel->handle));
    if (ret != 0) {
        return ret;
    }

    /* Configure linear data handling */
    data_handling.DataAlignment = DMA_DATA_RIGHTALIGN_ZEROPADDED;
    data_handling.DataExchange = DMA_EXCHANGE_NONE;
    ret = mr_stm32_hal_status(
        HAL_DMAEx_ConfigDataHandling(&dma_channel->handle, &data_handling));
    if (ret != 0) {
        return ret;
    }

    /* Configure transfer trigger */
    return mr_stm32_hal_status(
        HAL_DMAEx_ConfigTrigger(&dma_channel->handle, trigger));
}

static mr_err_t dma_set_mode(dma_channel_t *dma_channel, mr_bool_t circular,
                             const DMA_TriggerConfTypeDef *trigger) {
    mr_err_t ret;

    /* Initialize requested HAL mode */
    if (circular) {
        /* Configure linked-list mode */
        dma_channel->handle.InitLinkedList.Priority
            = DMA_LOW_PRIORITY_HIGH_WEIGHT;
        dma_channel->handle.InitLinkedList.LinkStepMode
            = DMA_LSM_FULL_EXECUTION;
        dma_channel->handle.InitLinkedList.LinkAllocatedPort
            = DMA_LINK_ALLOCATED_PORT0;
        dma_channel->handle.InitLinkedList.TransferEventMode
            = DMA_TCEM_BLOCK_TRANSFER;
        dma_channel->handle.InitLinkedList.LinkedListMode
            = DMA_LINKEDLIST_CIRCULAR;

        ret = mr_stm32_hal_status(HAL_DMAEx_List_Init(&dma_channel->handle));
    } else {
        /* Configure linear mode */
        ret = dma_init_linear(dma_channel, trigger);
    }
    if (ret != 0) {
        return ret;
    }

    /* Save active mode */
    dma_channel->circular = (circular != MR_FALSE);
    return 0;
}
#else
static mr_err_t dma_set_mode(dma_channel_t *dma_channel, mr_bool_t circular) {
    uint32_t mode;
    mr_err_t ret;

    /* Resolve requested DMA mode */
    mode = circular ? DMA_CIRCULAR : DMA_NORMAL;
    if (dma_channel->handle.Init.Mode == mode) {
        dma_channel->circular = (circular != MR_FALSE);
        return 0;
    }

    /* Deinitialize current HAL mode */
    ret = mr_stm32_hal_status(HAL_DMA_DeInit(&dma_channel->handle));
    if (ret != 0) {
        return ret;
    }

    /* Initialize requested HAL mode */
    dma_channel->handle.Init.Mode = mode;
    ret = mr_stm32_hal_status(HAL_DMA_Init(&dma_channel->handle));
    if (ret != 0) {
        return ret;
    }

    /* Restore callback context and active mode */
    dma_channel->handle.Parent = dma_channel;
    dma_channel->circular = (circular != MR_FALSE);
    return 0;
}
#endif /* defined(STM32_DMA_GPDMA) */

static mr_err_t dma_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_dma_config_t *config) {
    DMA_InitTypeDef init = (DMA_InitTypeDef){0};
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
#if defined(STM32_DMA_GPDMA)
    const DMA_TriggerConfTypeDef *trigger;
#endif /* defined(STM32_DMA_GPDMA) */
    dma_channel_t *dma_channel;
    HAL_DMA_StateTypeDef state;
    uint32_t request;
    IRQn_Type irqn;
    mr_err_t ret;

    /* Check channel state */
    dma_channel = dma_get_channel(priv, channel);
    if (dma_channel == MR_NULL) {
        return -MR_EINVAL;
    }
    if (dma_hw_busy(dma_channel)) {
        return -MR_EBUSY;
    }

    /* Enable controller clock */
    if (priv->clk_enable == MR_NULL) {
        return -MR_EINVAL;
    }
    priv->clk_enable();

    /* Resolve route resources */
    ret = dma_request(priv, channel, config->direction, &request);
    if (ret != 0) {
        return ret;
    }
#if defined(STM32_DMA_GPDMA)
    trigger = dma_trigger(priv, channel);
    if (trigger == MR_NULL) {
        return -MR_EINVAL;
    }
#endif /* defined(STM32_DMA_GPDMA) */

    /* Build HAL configuration */
    ret = dma_make_config(config, request, &init);
    if (ret != 0) {
        return ret;
    }

    /* Resolve channel IRQ */
    ret = dma_irqn(priv, channel, &irqn);
    if (ret != 0) {
        return ret;
    }

    /* Reinitialize HAL channel */
    state = HAL_DMA_GetState(&dma_channel->handle);
    if (state != HAL_DMA_STATE_RESET) {
#if defined(STM32_DMA_GPDMA)
        ret = mr_stm32_hal_status(
            dma_channel->circular ? HAL_DMAEx_List_DeInit(&dma_channel->handle)
                                  : HAL_DMA_DeInit(&dma_channel->handle));
#else
        ret = mr_stm32_hal_status(HAL_DMA_DeInit(&dma_channel->handle));
#endif /* defined(STM32_DMA_GPDMA) */
        if (ret != 0) {
            return ret;
        }
    }

    /* Initialize HAL channel */
    dma_channel->handle.Instance = dma_instance(priv, channel);
    dma_channel->handle.Init = init;
    dma_channel->handle.Parent = dma_channel;
#if defined(STM32_DMA_GPDMA)
    ret = dma_init_linear(dma_channel, trigger);
#else
    ret = mr_stm32_hal_status(HAL_DMA_Init(&dma_channel->handle));
#endif /* defined(STM32_DMA_GPDMA) */
    if (ret != 0) {
        return ret;
    }

    /* Reset channel transfer state */
    dma_channel->config = *config;
    dma_channel->half_count = 0U;
    dma_channel->count = 0U;
    dma_channel->circular = MR_FALSE;
    mr_atomic_store(&dma_channel->last_offset, 0);
#if defined(MR_USE_WAIT)
    /* Preserve waitable state across channel reconfiguration */
    if (!MR_OBJECT_IS_INITED((mr_object_t *)&dma_channel->events)) {
        mr_waitable_init(&dma_channel->events);
    }
#endif /* defined(MR_USE_WAIT) */

    /* Enable channel IRQ */
    HAL_NVIC_SetPriority(irqn, priv->irq_priority, 0U);
    HAL_NVIC_ClearPendingIRQ(irqn);
    HAL_NVIC_EnableIRQ(irqn);
    return 0;
}

static mr_err_t dma_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_dma_config_t *config) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
    dma_channel_t *dma_channel;

    /* Resolve channel */
    dma_channel = dma_get_channel(priv, channel);
    if (dma_channel == MR_NULL) {
        return -MR_EINVAL;
    }

    /* Check channel initialization */
    if (HAL_DMA_GetState(&dma_channel->handle) == HAL_DMA_STATE_RESET) {
        return -MR_ENODEV;
    }

    /* Return saved configuration */
    *config = dma_channel->config;
    return 0;
}

static mr_err_t dma_start(const mr_driver_t *drv, mr_uint32_t channel,
                          const mr_dma_transfer_t *transfer) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
#if defined(STM32_DMA_GPDMA)
    const DMA_TriggerConfTypeDef *trigger;
#endif /* defined(STM32_DMA_GPDMA) */
    dma_channel_t *dma_channel;
    mr_bool_t circular;
    uint32_t src, dst;
    uint32_t hw_count;
    mr_err_t ret;

    /* Resolve channel */
    dma_channel = dma_get_channel(priv, channel);
    if (dma_channel == MR_NULL) {
        return -MR_EINVAL;
    }

    /* Resolve transfer topology */
    circular = (transfer->next == transfer);
    if ((transfer->next != MR_NULL) && !circular) {
        return -MR_ENOSYS;
    }

#if !defined(STM32_DMA_GPDMA)
    /* Reject classic memory-to-memory circular mode */
    if (circular && (dma_channel->config.direction == MR_DMA_DIR_M2M)) {
        return -MR_ENOSYS;
    }
#endif /* !defined(STM32_DMA_GPDMA) */

    /* Check channel state */
    if (HAL_DMA_GetState(&dma_channel->handle) == HAL_DMA_STATE_RESET) {
        return -MR_ENODEV;
    }
    if (dma_hw_busy(dma_channel)) {
        return -MR_EBUSY;
    }

    /* Validate transfer count */
    ret = dma_transfer_count(dma_channel, transfer->count, &hw_count);
    if (ret != 0) {
        return ret;
    }

    /* Validate transfer endpoints */
    ret = dma_transfer_validate(dma_channel, transfer);
    if (ret != 0) {
        return ret;
    }

#if defined(STM32_DMA_GPDMA)
    /* Resolve channel trigger */
    trigger = dma_trigger(priv, channel);
    if (trigger == MR_NULL) {
        return -MR_EINVAL;
    }
#endif /* defined(STM32_DMA_GPDMA) */

    /* Configure transfer mode */
#if defined(STM32_DMA_GPDMA)
    ret = dma_set_mode(dma_channel, circular, trigger);
#else
    ret = dma_set_mode(dma_channel, circular);
#endif /* defined(STM32_DMA_GPDMA) */
    if (ret != 0) {
        return ret;
    }

    /* Resolve hardware addresses */
    src = (uint32_t)(mr_uintptr_t)transfer->src;
    dst = (uint32_t)(mr_uintptr_t)transfer->dst;

    /* Prepare transfer state */
    dma_channel->handle.XferCpltCallback = dma_transfer_complete;
    dma_channel->handle.XferHalfCpltCallback = dma_half_complete;
    dma_channel->handle.XferErrorCallback = dma_transfer_error;
    dma_channel->count = transfer->count;
    dma_channel->half_count = transfer->count / 2U;
    mr_atomic_store(&dma_channel->last_offset, 0);

#if defined(DMA_USE_DCACHE)
    /* Reset cache ownership state */
    dma_channel->src = transfer->src;
    dma_channel->dst = transfer->dst;
    dma_channel->cache_count = 0U;
#endif /* defined(DMA_USE_DCACHE) */

#if defined(STM32_DMA_GPDMA)
    /* Build circular GPDMA node */
    if (dma_channel->circular) {
        ret = dma_build_linked_list(dma_channel, trigger, src, dst, hw_count);
        if (ret != 0) {
            return ret;
        }
    }
#endif /* defined(STM32_DMA_GPDMA) */

#if defined(DMA_USE_DCACHE)
    /* Transfer cache ownership to DMA */
    ret = dma_cache_prepare(dma_channel, transfer);
    if (ret != 0) {
        return ret;
    }

#if defined(STM32_DMA_GPDMA)
    /* Publish circular node to DMA */
    if (dma_channel->circular) {
        ret = mr_port_dcache_clean(&dma_channel->node,
                                   sizeof(dma_channel->node));
        if (ret != 0) {
            return ret;
        }
    }
#endif /* defined(STM32_DMA_GPDMA) */
#endif /* defined(DMA_USE_DCACHE) */

    /* Start hardware transfer */
#if defined(STM32_DMA_GPDMA)
    if (dma_channel->circular) {
        ret = mr_stm32_hal_status(
            HAL_DMAEx_List_Start_IT(&dma_channel->handle));
    } else {
        ret = mr_stm32_hal_status(
            HAL_DMA_Start_IT(&dma_channel->handle, src, dst, hw_count));
    }
#else
    ret = mr_stm32_hal_status(
        HAL_DMA_Start_IT(&dma_channel->handle, src, dst, hw_count));
#endif /* defined(STM32_DMA_GPDMA) */
    if (ret != 0) {
        return ret;
    }

    /* Apply active event subscriptions */
    dma_apply_events(dma_channel);
    return 0;
}

static mr_err_t dma_stop(const mr_driver_t *drv, mr_uint32_t channel) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
    dma_channel_t *dma_channel;
    HAL_DMA_StateTypeDef state;
    mr_size_t count, offset;
#if defined(DMA_USE_DCACHE)
    mr_err_t cache_ret;
#endif /* defined(DMA_USE_DCACHE) */
    mr_err_t ret;

    /* Resolve channel */
    dma_channel = dma_get_channel(priv, channel);
    if (dma_channel == MR_NULL) {
        return -MR_EINVAL;
    }

    /* Read channel state */
    state = HAL_DMA_GetState(&dma_channel->handle);
    if (state == HAL_DMA_STATE_RESET) {
        return -MR_ENODEV;
    }

    /* Recover errored channel */
    if (state == HAL_DMA_STATE_ERROR) {
        /* Snapshot observable progress */
        count = dma_current_count(dma_channel);
        offset = dma_count_offset(dma_channel, count);
        mr_atomic_store(&dma_channel->last_offset, (mr_atomic_t)offset);

#if defined(DMA_USE_DCACHE)
        /* Return completed destination data to CPU */
        dma_cache_complete(dma_channel, count);
#endif /* defined(DMA_USE_DCACHE) */

        /* Rebuild HAL channel state */
        ret = dma_channel_configure(drv, channel, &dma_channel->config);
        mr_atomic_store(&dma_channel->last_offset, (mr_atomic_t)offset);

        return ret;
    }

#if defined(STM32_DMA_GPDMA)
    /* Reject suspended GPDMA channel */
    if (state == HAL_DMA_STATE_SUSPEND) {
        return -MR_EBUSY;
    }
#endif /* defined(STM32_DMA_GPDMA) */

    /* Skip inactive channel */
    if (state != HAL_DMA_STATE_BUSY) {
        return 0;
    }

    /* Snapshot progress before abort */
    count = dma_current_count(dma_channel);
    offset = dma_count_offset(dma_channel, count);
    mr_atomic_store(&dma_channel->last_offset, (mr_atomic_t)offset);

    /* Abort hardware transfer */
    ret = mr_stm32_hal_status(HAL_DMA_Abort(&dma_channel->handle));
    if (ret != 0) {
        return ret;
    }

#if defined(DMA_USE_DCACHE)
    /* Return completed destination data to CPU */
    cache_ret = dma_cache_complete(dma_channel, count);
    if (cache_ret != 0) {
        return cache_ret;
    }
#endif /* defined(DMA_USE_DCACHE) */
    return 0;
}

static mr_err_t dma_is_busy(const mr_driver_t *drv, mr_uint32_t channel,
                            mr_bool_t *busy) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
    dma_channel_t *dma_channel;

    /* Check channel state */
    dma_channel = dma_get_channel(priv, channel);
    if (dma_channel == MR_NULL) {
        return -MR_EINVAL;
    }
    if (HAL_DMA_GetState(&dma_channel->handle) == HAL_DMA_STATE_RESET) {
        return -MR_ENODEV;
    }
    /* Read hardware activity */
    *busy = dma_hw_busy(dma_channel);
    return 0;
}

static mr_err_t dma_offset(const mr_driver_t *drv, mr_uint32_t channel,
                           mr_size_t *offset) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
    dma_channel_t *dma_channel;
    mr_size_t count, current_offset;

    /* Check channel state */
    dma_channel = dma_get_channel(priv, channel);
    if (dma_channel == MR_NULL) {
        return -MR_EINVAL;
    }
    if (HAL_DMA_GetState(&dma_channel->handle) == HAL_DMA_STATE_RESET) {
        return -MR_ENODEV;
    }
    if (dma_hw_busy(dma_channel)) {
        /* Snapshot active progress */
        count = dma_current_count(dma_channel);
        current_offset = dma_count_offset(dma_channel, count);
        mr_atomic_store(&dma_channel->last_offset, (mr_atomic_t)current_offset);
    }

    /* Return last observable progress */
    *offset = (mr_size_t)mr_atomic_load(&dma_channel->last_offset);
    return 0;
}

#if defined(MR_USE_WAIT)
static mr_err_t dma_event_attach(const mr_driver_t *drv, mr_uint32_t channel,
                                 mr_waiter_t *entry) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
    dma_channel_t *dma_channel;

    /* Check channel state */
    dma_channel = dma_get_channel(priv, channel);
    if (dma_channel == MR_NULL) {
        return -MR_EINVAL;
    }
    if (HAL_DMA_GetState(&dma_channel->handle) == HAL_DMA_STATE_RESET) {
        return -MR_ENODEV;
    }

    /* Attach waiter */
    return mr_waiter_wait(entry, &dma_channel->events);
}

static mr_err_t dma_event_control(const mr_driver_t *drv, mr_uint32_t channel,
                                  mr_uint32_t events, mr_bool_t enable) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
    dma_channel_t *dma_channel;

    /* Check channel and event mask */
    dma_channel = dma_get_channel(priv, channel);
    if (dma_channel == MR_NULL) {
        return -MR_EINVAL;
    }
    if ((events == 0U) || ((events & ~DMA_EVENT_MASK) != 0U)) {
        return -MR_EINVAL;
    }
    if (HAL_DMA_GetState(&dma_channel->handle) == HAL_DMA_STATE_RESET) {
        return -MR_ENODEV;
    }

    /* Reapply subscriptions without dropping mandatory IRQ sources */
    if (enable) {
        dma_channel->event_mask |= events;
    } else {
        dma_channel->event_mask &= ~events;
    }

    /* Apply subscriptions to active hardware */
    if (dma_hw_busy(dma_channel)) {
        dma_apply_events(dma_channel);
    }
    return 0;
}
#endif /* defined(MR_USE_WAIT) */

static void dma_irq_handle(dma_channel_t *channels, mr_size_t channels_num,
                           mr_uint64_t mask) {
    mr_size_t channel;

    /* Dispatch shared IRQ to initialized channels in the route mask */
    for (channel = 0U; channel < channels_num; channel++) {
        if ((mask & (1ULL << channel)) == 0U) {
            continue;
        }
        if (HAL_DMA_GetState(&channels[channel].handle)
            == HAL_DMA_STATE_RESET) {
            continue;
        }
        HAL_DMA_IRQHandler(&channels[channel].handle);
    }
}

/* Instance install */
#define __ST_HT_DMA_IRQ_MASK(_id, _n)                                          \
    MR_HT_DRIVER_PROP_OR(_id, MR_CAT(st_irq_mask_, _n), (1ULL << (_n)))
#define ST_HT_DMA_IRQ(_id, _n)                                                 \
    MR_MACRO_IF(                                                               \
        MR_HT_DRIVER_HAS_PROP(_id, MR_CAT(st_irq_handler_, _n)),               \
        (void MR_HT_DRIVER_PROP(_id, MR_CAT(st_irq_handler_, _n))(void) {      \
            dma_irq_handle(                                                    \
                MR_CAT(__dma_channels_, _id),                                  \
                MR_ARRAY_SIZE(MR_CAT(__dma_channels_, _id)),                   \
                __ST_HT_DMA_IRQ_MASK(_id, _n));                                \
        }),                                                                    \
        ())

#if defined(STM32_DMA_GPDMA)
#define __ST_HT_DMA_TRIGGER_INIT(_elem, _node, _prop)                          \
    .trigger = {                                                               \
        .TriggerMode = MR_HT_NODE_PROP_ELEM_OR(                                \
            _node, _prop, _elem, 1, DMA_TRIGM_BLOCK_TRANSFER),                 \
        .TriggerPolarity = MR_HT_NODE_PROP_ELEM_OR(                            \
            _node, _prop, _elem, 2, DMA_TRIG_POLARITY_MASKED),                 \
        .TriggerSelection =                                                    \
            MR_HT_NODE_PROP_ELEM_OR(_node, _prop, _elem, 3, 0U),               \
    },
#else
#define __ST_HT_DMA_TRIGGER_INIT(_elem, _node, _prop)
#endif /* defined(STM32_DMA_GPDMA) */
#define __ST_HT_DMA_IRQN(_node, _elem)                                         \
    MR_HT_NODE_PROP_OR(_node, MR_CAT(st_irqn_, _elem), (IRQn_Type)0)
#define __ST_HT_DMA_ROUTE_IMPL(_elem, _node, _prop)                            \
    {                                                                          \
        __ST_HT_DMA_TRIGGER_INIT(_elem, _node, _prop)                          \
        .request = MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 0),               \
        .irqn = __ST_HT_DMA_IRQN(_node, _elem),                                \
        .irq_valid = MR_HT_NODE_HAS_PROP(_node, MR_CAT(st_irqn_, _elem)),      \
    }
#define __ST_HT_DMA_INSTANCE(_id)                                              \
    MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, st_dmax),                           \
                (MR_HT_DRIVER_PROP(_id, st_dmax)),                             \
                (MR_HT_DRIVER_PROP(_id, st_gpdmax)))
#define ST_HT_DMA_INSTALL(_id, _ops)                                           \
    static void MR_CAT(__dma_clk_enable_, _id)(void) {                         \
        MR_HT_DRIVER_PROP(_id, st_clken)();                                    \
    }                                                                          \
    static const dma_route_t MR_CAT(__dma_routes_, _id)[] = {                  \
        MR_HT_DRIVER_PROP_ELEM_FOREACH(_id, st_requests,                       \
                                       __ST_HT_DMA_ROUTE_IMPL)                 \
    };                                                                         \
    static dma_channel_t MR_CAT(                                               \
        __dma_channels_, _id)[MR_ARRAY_SIZE(MR_CAT(__dma_routes_, _id))];      \
    static dma_priv_t MR_CAT(__dma_priv_, _id) = {                             \
        .instance = __ST_HT_DMA_INSTANCE(_id),                                 \
        .clk_enable = MR_CAT(__dma_clk_enable_, _id),                          \
        .routes = MR_CAT(__dma_routes_, _id),                                  \
        .routes_num = MR_ARRAY_SIZE(MR_CAT(__dma_routes_, _id)),               \
        .irq_priority = MR_HT_DRIVER_PROP(_id, st_irq_priority),               \
        .channels = MR_CAT(__dma_channels_, _id),                              \
    };                                                                         \
    static mr_dma_data_t MR_CAT(__mr_dma_data_, _id)                           \
        = MR_DMA_DATA_INIT(MR_HT_DRIVER_PROP_OR(                               \
            _id, exist_mask,                                                   \
            ((1ULL << MR_ARRAY_SIZE(MR_CAT(__dma_routes_, _id))) - 1ULL)));    \
    ST_HT_DMA_IRQ(_id, 0)                                                      \
    ST_HT_DMA_IRQ(_id, 1)                                                      \
    ST_HT_DMA_IRQ(_id, 2)                                                      \
    ST_HT_DMA_IRQ(_id, 3)                                                      \
    ST_HT_DMA_IRQ(_id, 4)                                                      \
    ST_HT_DMA_IRQ(_id, 5)                                                      \
    ST_HT_DMA_IRQ(_id, 6)                                                      \
    ST_HT_DMA_IRQ(_id, 7)                                                      \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(dma),                   \
                         MR_DRIVER_TYPE_DMA, &MR_CAT(__mr_dma_data_, _id),     \
                         &MR_CAT(__dma_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(ST_HT_DMA_INSTALL, &__dma_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_DMA) */

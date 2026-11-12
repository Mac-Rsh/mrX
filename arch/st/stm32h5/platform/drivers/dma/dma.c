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
#if defined(MR_USE_DCACHE)
/* Enable DMA cache ownership tracking when data cache support is present */
#define DMA_USE_DCACHE
#include <mrX/port/mr_port_cache.h>
#endif /* defined(MR_USE_DCACHE) */
#include "stm32h5xx_hal_dma.h"
#include "stm32h5xx_hal_dma_ex.h"

/* HardTree compatible */
#define MR_HT_COMPAT                    st_stm32h5_gpdma
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
    DMA_NodeTypeDef node;
    DMA_QListTypeDef queue;
    mr_dma_config_t config;
    mr_size_t size;
    mr_atomic_t last_offset;
#if defined(DMA_USE_DCACHE)
    const void *src;
    void *dst;
    mr_size_t cache_offset;
#endif /* defined(DMA_USE_DCACHE) */
    mr_uint8_t circular: 1;
#if defined(MR_USE_WAIT)
    mr_uint32_t event_mask;
    mr_waitable_t events;
#endif /* defined(MR_USE_WAIT) */
} dma_channel_t;

/* DMA route type */
typedef struct dma_route {
    uint32_t request;
    DMA_TriggerConfTypeDef trigger;
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

/* GPDMA channel register layout definition */
#define DMA_CHANNELS                    (8U)
#define DMA_CHANNEL_OFFSET              (0x50U)
#define DMA_CHANNEL_STRIDE              (0x80U)

/* GPDMA controller count definition */
#if defined(GPDMA2)
#define DMA_CONTROLLERS                 (2U)
#else
#define DMA_CONTROLLERS                 (1U)
#endif /* defined(GPDMA2) */

/* DMA event and interrupt mask definition */
#define DMA_EVENT_MASK                                                         \
    (MR_DMA_WAIT_HALF | MR_DMA_WAIT_DONE | MR_DMA_WAIT_ERROR)
#define DMA_ERROR_IT                                                           \
    (DMA_IT_DTE | DMA_IT_ULE | DMA_IT_USE | DMA_IT_TO)

/* DMA IRQ handle map */
static DMA_HandleTypeDef *__dma_irq_handles[DMA_CONTROLLERS][DMA_CHANNELS];
/* DMA IRQ number map */
static const IRQn_Type __dma_irqn[DMA_CONTROLLERS][DMA_CHANNELS] = {
    {
        GPDMA1_Channel0_IRQn, GPDMA1_Channel1_IRQn,
        GPDMA1_Channel2_IRQn, GPDMA1_Channel3_IRQn,
        GPDMA1_Channel4_IRQn, GPDMA1_Channel5_IRQn,
        GPDMA1_Channel6_IRQn, GPDMA1_Channel7_IRQn,
    },
#if defined(GPDMA2)
    {
        GPDMA2_Channel0_IRQn, GPDMA2_Channel1_IRQn,
        GPDMA2_Channel2_IRQn, GPDMA2_Channel3_IRQn,
        GPDMA2_Channel4_IRQn, GPDMA2_Channel5_IRQn,
        GPDMA2_Channel6_IRQn, GPDMA2_Channel7_IRQn,
    },
#endif /* defined(GPDMA2) */
};
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
/* Transfer direction mapping */
static const uint32_t __dma_direction[] = {
    DMA_PERIPH_TO_MEMORY,
    DMA_MEMORY_TO_PERIPH,
    DMA_MEMORY_TO_MEMORY,
};

static dma_channel_t *dma_get_channel(dma_priv_t *priv, mr_uint32_t channel) {
    /* Reject logical channels without a board route or HAL channel */
    if ((channel >= priv->routes_num) || (channel >= DMA_CHANNELS)) {
        return MR_NULL;
    }
    return &priv->channels[channel];
}

static mr_err_t dma_ctrl_index(const DMA_TypeDef *instance,
                               mr_uint32_t *index) {
    /* Map the controller instance to the IRQ-table index */
    if (instance == GPDMA1) {
        *index = 0U;
        return 0;
    }
#if defined(GPDMA2)
    if (instance == GPDMA2) {
        *index = 1U;
        return 0;
    }
#endif /* defined(GPDMA2) */
    return -MR_EINVAL;
}

static void dma_apply_events(dma_channel_t *channel) {
    mr_bool_t half_required, completion_required;

    half_required = MR_FALSE;
    completion_required = !channel->circular;

#if defined(MR_USE_WAIT)
    half_required = (channel->event_mask & MR_DMA_WAIT_HALF) != 0U;
    completion_required |= (channel->event_mask & MR_DMA_WAIT_DONE) != 0U;
#if defined(DMA_USE_DCACHE)
    /* Reset destination ownership at each circular block boundary, even when
     * a P2M/M2M consumer subscribes only to HALF */
    completion_required |= channel->circular && half_required
                        && (channel->config.direction != MR_DMA_DIR_M2P);
#endif /* defined(DMA_USE_DCACHE) */
#endif /* defined(MR_USE_WAIT) */
    /* Finite completion maintains HAL state; circular completion supports
     * subscribed events and cache ownership */
    if (completion_required) {
        __HAL_DMA_ENABLE_IT(&channel->handle, DMA_IT_TC);
    } else {
        __HAL_DMA_DISABLE_IT(&channel->handle, DMA_IT_TC);
    }
    /* Error recovery must not depend on event subscriptions. */
    __HAL_DMA_ENABLE_IT(&channel->handle, DMA_ERROR_IT);
    if (half_required) {
        __HAL_DMA_ENABLE_IT(&channel->handle, DMA_IT_HT);
    } else {
        __HAL_DMA_DISABLE_IT(&channel->handle, DMA_IT_HT);
    }
}

static mr_size_t dma_memory_width(const dma_channel_t *channel) {
    /* Return the element width of the memory side, in bytes */
    return (channel->config.direction == MR_DMA_DIR_M2P)
             ? (mr_size_t)(1UL << channel->config.src_width)
             : (mr_size_t)(1UL << channel->config.dst_width);
}

static mr_bool_t dma_hw_busy(const dma_channel_t *channel) {
    HAL_DMA_StateTypeDef state;

    /* Treat suspended transfers as active until explicitly stopped */
    state = HAL_DMA_GetState(&channel->handle);

    return ((state == HAL_DMA_STATE_BUSY) || (state == HAL_DMA_STATE_SUSPEND));
}

static mr_err_t dma_width(mr_uint32_t width, const uint32_t *table,
                          uint32_t *hal_width) {
    /* Translate supported element widths to HAL encodings */
    if (width == MR_DMA_WIDTH_64) {
        return -MR_ENOSYS;
    }
    if (width > MR_DMA_WIDTH_32) {
        return -MR_EINVAL;
    }
    *hal_width = table[width];
    return 0;
}

static mr_err_t dma_adjust(mr_uint32_t adjust, uint32_t increment,
                           uint32_t *hal_adjust) {
    /* Translate supported address adjustments to HAL encodings */
    if (adjust == MR_DMA_ADJ_DEC) {
        return -MR_ENOSYS;
    }
    if (adjust > MR_DMA_ADJ_CONST) {
        return -MR_EINVAL;
    }
    *hal_adjust = (adjust == MR_DMA_ADJ_INC) ? increment : 0U;
    return 0;
}

static mr_err_t dma_burst(mr_uint32_t burst, uint32_t *hal_burst) {
    /* Translate supported burst sizes and reject unsupported larger bursts */
    if (burst >= MR_DMA_BURST_128) {
        return (burst <= MR_DMA_BURST_256) ? -MR_ENOSYS : -MR_EINVAL;
    }
    *hal_burst = __dma_burst[burst];
    return 0;
}

static mr_err_t dma_request(const dma_priv_t *priv, mr_uint32_t channel,
                            mr_uint32_t direction, uint32_t *request) {
    /* Memory-to-memory uses the software request; other paths need a route */
    if (direction == MR_DMA_DIR_M2M) {
        *request = DMA_REQUEST_SW;
        return 0;
    }
    if ((priv->routes == MR_NULL) || (channel >= priv->routes_num)) {
        return -MR_EINVAL;
    }
    *request = priv->routes[channel].request;
    return 0;
}

static const DMA_TriggerConfTypeDef *dma_trigger(const dma_priv_t *priv,
                                                 mr_uint32_t channel) {
    /* Return the optional trigger mapping for a routed channel */
    if ((priv->routes == MR_NULL) || (channel >= priv->routes_num)) {
        return MR_NULL;
    }
    return &priv->routes[channel].trigger;
}

static mr_err_t dma_make_config(const mr_dma_config_t *config, uint32_t request,
                                DMA_InitTypeDef *init) {
    mr_err_t ret;

    /* Map data widths */
    ret = dma_width(config->src_width, __dma_src_width, &init->SrcDataWidth);
    if (ret != 0) {
        return ret;
    }
    ret = dma_width(config->dst_width, __dma_dst_width, &init->DestDataWidth);
    if (ret != 0) {
        return ret;
    }

    /* Map address adjustment modes */
    ret = dma_adjust(config->src_adj, DMA_SINC_INCREMENTED, &init->SrcInc);
    if (ret != 0) {
        return ret;
    }
    ret = dma_adjust(config->dst_adj, DMA_DINC_INCREMENTED, &init->DestInc);
    if (ret != 0) {
        return ret;
    }

    /* Map burst length */
    ret = dma_burst(config->src_burst, &init->SrcBurstLength);
    if (ret != 0) {
        return ret;
    }
    ret = dma_burst(config->dst_burst, &init->DestBurstLength);
    if (ret != 0) {
        return ret;
    }

    /* Map direction and request */
    if (config->direction > MR_DMA_DIR_M2M) {
        return -MR_EINVAL;
    }
    init->Direction = __dma_direction[config->direction];
    init->Request
        = (config->direction == MR_DMA_DIR_M2M) ? DMA_REQUEST_SW : request;

    /* Fill the remaining channel initialization fields */
    init->BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    init->Priority = DMA_LOW_PRIORITY_HIGH_WEIGHT;
    /* Port 0 serves peripherals; port 1 serves memory on STM32H5. */
    init->TransferAllocatedPort
        = ((config->direction == MR_DMA_DIR_M2P) ? DMA_SRC_ALLOCATED_PORT1
                                                 : DMA_SRC_ALLOCATED_PORT0)
        | ((config->direction == MR_DMA_DIR_P2M)
               ? DMA_DEST_ALLOCATED_PORT1
               : ((config->direction == MR_DMA_DIR_M2M)
                      ? DMA_DEST_ALLOCATED_PORT1
                      : DMA_DEST_ALLOCATED_PORT0));
    init->TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    init->Mode = DMA_NORMAL;
    return 0;
}

static DMA_Channel_TypeDef *dma_instance(const dma_priv_t *priv,
                                         mr_uint32_t channel) {
    mr_uintptr_t address;

    /* Resolve the channel register block from the controller base address */
    address = (mr_uintptr_t)priv->instance + DMA_CHANNEL_OFFSET
            + ((mr_uintptr_t)channel * DMA_CHANNEL_STRIDE);

    return (DMA_Channel_TypeDef *)address;
}

static mr_size_t dma_current_offset(const dma_channel_t *channel) {
    mr_size_t remaining, offset;

    /* Convert the remaining hardware count to completed data items */
    if (channel->size == 0U) {
        return 0U;
    }
    remaining = (mr_size_t)__HAL_DMA_GET_COUNTER(&channel->handle);
    if (remaining > channel->size) {
        return 0U;
    }
    offset = channel->size - remaining;
    if (channel->circular) {
        offset %= channel->size;
    }
    return offset;
}

#if defined(DMA_USE_DCACHE)
static mr_size_t dma_memory_size(mr_size_t count, mr_uint32_t adjust,
                                 mr_uint32_t width) {
    /* Return the byte span touched by incrementing or fixed addresses */
    return (adjust == MR_DMA_ADJ_INC) ? count : ((mr_size_t)1U << width);
}

static mr_err_t dma_cache_prepare(dma_channel_t *channel,
                                  const mr_dma_transfer_t *transfer) {
    mr_size_t size;
    mr_err_t ret;

    /* Clean source and prepare destination ownership for the transfer */
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
    mr_size_t size;

    /* Return DMA-written memory to the CPU over the completed range */
    dst = channel->dst;
    if (channel->config.direction == MR_DMA_DIR_M2P) {
        return 0;
    }
    if (channel->config.dst_adj != MR_DMA_ADJ_INC) {
        size = (mr_size_t)1U << channel->config.dst_width;
        return mr_port_dcache_invalidate(dst, size);
    }
    if (end <= start) {
        return 0;
    }
    return mr_port_dcache_invalidate(dst + start, end - start);
}

#if defined(MR_USE_WAIT)
static mr_err_t dma_cache_clean_src(dma_channel_t *channel, mr_size_t start,
                                    mr_size_t end) {
    const mr_uint8_t *src;
    mr_size_t size;

    /* Make CPU updates visible before DMA reuses the completed range */
    src = channel->src;
    if (channel->config.direction == MR_DMA_DIR_P2M) {
        return 0;
    }
    if (channel->config.src_adj != MR_DMA_ADJ_INC) {
        size = (mr_size_t)1U << channel->config.src_width;
        return mr_port_dcache_clean(src, size);
    }
    if (end <= start) {
        return 0;
    }
    return mr_port_dcache_clean(src + start, end - start);
}
#endif /* defined(MR_USE_WAIT) */

static mr_err_t dma_cache_complete(dma_channel_t *channel, mr_size_t offset) {
    mr_size_t bytes;
    mr_err_t ret;

    /* Advance destination ownership only after invalidation succeeds */
    bytes = offset * dma_memory_width(channel);
    if ((channel->config.direction == MR_DMA_DIR_M2P)
        || (bytes <= channel->cache_offset)) {
        return 0;
    }
    ret = dma_cache_invalidate_dst(channel, channel->cache_offset, bytes);
    if (ret == 0) {
        channel->cache_offset = bytes;
    }
    return ret;
}
#endif /* defined(DMA_USE_DCACHE) */

#if defined(MR_USE_WAIT)
static void dma_waitable_init(dma_channel_t *channel) {
    if (!MR_OBJECT_IS_INITED((mr_object_t *)&channel->events)) {
        mr_waitable_init(&channel->events);
    }
}

static void dma_raise_event(dma_channel_t *channel, mr_wait_event_t event) {
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
#if defined(DMA_USE_DCACHE)
    mr_err_t ret;
#if defined(MR_USE_WAIT)
    mr_size_t half;
#endif /* defined(MR_USE_WAIT) */
#endif /* defined(DMA_USE_DCACHE) */

    channel = handle->Parent;
#if defined(DMA_USE_DCACHE)
    ret = 0;
#if defined(MR_USE_WAIT)
    half = channel->size / 2U;
#endif /* defined(MR_USE_WAIT) */
#endif /* defined(DMA_USE_DCACHE) */

    if (!channel->circular) {
        /* Public offsets are bytes; channel->size counts data items */
        mr_atomic_store(&channel->last_offset,
                        (mr_atomic_t)(channel->size
                                      * dma_memory_width(channel)));
#if defined(DMA_USE_DCACHE)
        /* Return DMA-written memory even without a DONE subscriber */
        ret = dma_cache_complete(channel, channel->size);
        if (ret != 0) {
#if defined(MR_USE_WAIT)
            dma_raise_event(channel, MR_DMA_WAIT_ERROR);
#endif /* defined(MR_USE_WAIT) */
            return;
        }
#endif /* defined(DMA_USE_DCACHE) */
#if defined(MR_USE_WAIT)
        dma_raise_event(channel, MR_DMA_WAIT_DONE);
#endif /* defined(MR_USE_WAIT) */
        return;
    }

#if defined(MR_USE_WAIT)
    if ((channel->event_mask & MR_DMA_WAIT_DONE) != 0U) {
#if defined(DMA_USE_DCACHE)
        /* Make the completed second half visible before notifying the waiter */
        if (channel->config.direction != MR_DMA_DIR_M2P) {
            ret = dma_cache_invalidate_dst(
                channel, half * dma_memory_width(channel),
                channel->size * dma_memory_width(channel));
            if (ret != 0) {
                dma_raise_event(channel, MR_DMA_WAIT_ERROR);
                channel->cache_offset = 0U;
                return;
            }
        }
#endif /* defined(DMA_USE_DCACHE) */
        dma_raise_event(channel, MR_DMA_WAIT_DONE);
#if defined(DMA_USE_DCACHE)
        /* Make waiter updates visible before DMA reuses this half */
        if (channel->config.direction != MR_DMA_DIR_P2M) {
            ret = dma_cache_clean_src(
                channel, half * dma_memory_width(channel),
                channel->size * dma_memory_width(channel));
            if (ret != 0) {
                dma_raise_event(channel, MR_DMA_WAIT_ERROR);
            }
        }
#endif /* defined(DMA_USE_DCACHE) */
    }
#endif /* defined(MR_USE_WAIT) */
#if defined(DMA_USE_DCACHE)
    /* Restart destination ownership after wrap, even without DONE */
    channel->cache_offset = 0U;
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

    channel = handle->Parent;
#if defined(MR_USE_WAIT) && defined(DMA_USE_DCACHE)
    half = channel->size / 2U;
#endif /* defined(MR_USE_WAIT) && defined(DMA_USE_DCACHE) */

#if defined(MR_USE_WAIT)
#if defined(DMA_USE_DCACHE)
    /* Make DMA-written data visible before notifying the HALF waiter */
    ret = dma_cache_complete(channel, half);
    if (ret != 0) {
        dma_raise_event(channel, MR_DMA_WAIT_ERROR);
        return;
    }
#endif /* defined(DMA_USE_DCACHE) */
    dma_raise_event(channel, MR_DMA_WAIT_HALF);
#if defined(DMA_USE_DCACHE)
    /* Make waiter updates visible before DMA reuses this half */
    if (channel->circular && (channel->config.direction != MR_DMA_DIR_P2M)) {
        ret = dma_cache_clean_src(channel, 0U,
                                  half * dma_memory_width(channel));
        if (ret != 0) {
            dma_raise_event(channel, MR_DMA_WAIT_ERROR);
        }
    }
#endif /* defined(DMA_USE_DCACHE) */
#else
    (void)channel;
#endif /* defined(MR_USE_WAIT) */
}

static void dma_transfer_error(DMA_HandleTypeDef *handle) {
    dma_channel_t *channel;
    mr_size_t offset;

    /* Preserve progress and attempt cache completion before reporting error */
    channel = handle->Parent;
    offset = dma_current_offset(channel);

    mr_atomic_store(&channel->last_offset,
                    (mr_atomic_t)(offset * dma_memory_width(channel)));
#if defined(DMA_USE_DCACHE)
    dma_cache_complete(channel, offset);
#endif /* defined(DMA_USE_DCACHE) */
#if defined(MR_USE_WAIT)
    dma_raise_event(channel, MR_DMA_WAIT_ERROR);
#else
    (void)channel;
#endif /* defined(MR_USE_WAIT) */
}

static mr_err_t dma_build_linked_list(dma_channel_t *dma_channel,
                                      const DMA_TriggerConfTypeDef *trigger,
                                      uint32_t src, uint32_t dst,
                                      uint32_t count) {
    DMA_NodeConfTypeDef node_config;
    mr_err_t ret;

    node_config = (DMA_NodeConfTypeDef){0};
    /* Rebuild the single-node circular list for this transfer */
    ret = mr_stm32h5_hal_status(HAL_DMAEx_List_ResetQ(&dma_channel->queue));
    if (ret != 0) {
        return ret;
    }
    node_config.NodeType = DMA_GPDMA_LINEAR_NODE;
    node_config.Init = dma_channel->handle.Init;
    node_config.DataHandlingConfig.DataExchange = DMA_EXCHANGE_NONE;
    node_config.DataHandlingConfig.DataAlignment
        = DMA_DATA_RIGHTALIGN_ZEROPADDED;
    node_config.TriggerConfig = *trigger;
    node_config.SrcAddress = src;
    node_config.DstAddress = dst;
    node_config.DataSize = count;
    ret = mr_stm32h5_hal_status(
        HAL_DMAEx_List_BuildNode(&node_config, &dma_channel->node));
    if (ret != 0) {
        return ret;
    }
    ret = mr_stm32h5_hal_status(HAL_DMAEx_List_InsertNode_Tail(
        &dma_channel->queue, &dma_channel->node));
    if (ret != 0) {
        return ret;
    }
    ret = mr_stm32h5_hal_status(
        HAL_DMAEx_List_SetCircularMode(&dma_channel->queue));
    if (ret != 0) {
        return ret;
    }
    return mr_stm32h5_hal_status(
        HAL_DMAEx_List_LinkQ(&dma_channel->handle, &dma_channel->queue));
}

static mr_err_t dma_init_linear(dma_channel_t *dma_channel,
                                const DMA_TriggerConfTypeDef *trigger) {
    mr_err_t ret;

    /* Configure the same board trigger used by circular transfers */
    ret = mr_stm32h5_hal_status(HAL_DMA_Init(&dma_channel->handle));
    if (ret != 0) {
        return ret;
    }
    return mr_stm32h5_hal_status(
        HAL_DMAEx_ConfigTrigger(&dma_channel->handle, trigger));
}

static mr_err_t dma_set_mode(dma_channel_t *dma_channel, mr_bool_t circular,
                             const DMA_TriggerConfTypeDef *trigger) {
    mr_err_t ret;

    /* Select linked-list circular mode or direct linear mode */
    if (circular) {
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
        ret = mr_stm32h5_hal_status(HAL_DMAEx_List_Init(&dma_channel->handle));
    } else {
        ret = dma_init_linear(dma_channel, trigger);
    }
    if (ret == 0) {
        dma_channel->circular = (circular != MR_FALSE);
    }
    return ret;
}

static mr_err_t dma_channel_configure(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      const mr_dma_config_t *config) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
    const DMA_TriggerConfTypeDef *trigger;
    dma_channel_t *dma_channel;
    HAL_DMA_StateTypeDef state;
    DMA_InitTypeDef init;
    mr_uint32_t ctrl;
    uint32_t request;
    mr_err_t ret;

    init = (DMA_InitTypeDef){0};
    /* Check parameter */
    dma_channel = dma_get_channel(priv, channel);
    if (dma_channel == MR_NULL) {
        return -MR_EINVAL;
    }
    if (dma_hw_busy(dma_channel)) {
        return -MR_EBUSY;
    }
    ret = dma_ctrl_index(priv->instance, &ctrl);
    if (ret != 0) {
        return ret;
    }

    /* Enable the controller clock */
    if (priv->clk_enable == MR_NULL) {
        return -MR_EINVAL;
    }
    priv->clk_enable();

    /* Convert the mrX configuration to STM32 GPDMA settings */
    ret = dma_request(priv, channel, config->direction, &request);
    if (ret != 0) {
        return ret;
    }
    trigger = dma_trigger(priv, channel);
    if (trigger == MR_NULL) {
        return -MR_EINVAL;
    }
    ret = dma_make_config(config, request, &init);
    if (ret != 0) {
        return ret;
    }

    /* Reset any previous channel mode */
    state = HAL_DMA_GetState(&dma_channel->handle);
    if (state != HAL_DMA_STATE_RESET) {
        ret = mr_stm32h5_hal_status(
            dma_channel->circular ? HAL_DMAEx_List_DeInit(&dma_channel->handle)
                                  : HAL_DMA_DeInit(&dma_channel->handle));
        if (ret != 0) {
            return ret;
        }
    }

    /* Init DMA channel */
    dma_channel->handle.Instance = dma_instance(priv, channel);
    dma_channel->handle.Init = init;
    dma_channel->handle.Parent = dma_channel;
    ret = dma_init_linear(dma_channel, trigger);
    if (ret != 0) {
        return ret;
    }
    dma_channel->config = *config;
    dma_channel->size = 0U;
    dma_channel->circular = MR_FALSE;
    mr_atomic_store(&dma_channel->last_offset, 0);
#if defined(MR_USE_WAIT)
    dma_waitable_init(dma_channel);
#endif /* defined(MR_USE_WAIT) */

    /* Enable this channel's interrupt line */
    __dma_irq_handles[ctrl][channel] = &dma_channel->handle;
    HAL_NVIC_SetPriority(__dma_irqn[ctrl][channel], priv->irq_priority, 0U);
    HAL_NVIC_EnableIRQ(__dma_irqn[ctrl][channel]);
    return 0;
}

static mr_err_t dma_channel_configuration(const mr_driver_t *drv,
                                          mr_uint32_t channel,
                                          mr_dma_config_t *config) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
    dma_channel_t *dma_channel;

    /* Check parameter */
    dma_channel = dma_get_channel(priv, channel);
    if (dma_channel == MR_NULL) {
        return -MR_EINVAL;
    }
    if (HAL_DMA_GetState(&dma_channel->handle) == HAL_DMA_STATE_RESET) {
        return -MR_ENODEV;
    }

    /* Return the last configured settings */
    *config = dma_channel->config;
    return 0;
}

static mr_err_t dma_start(const mr_driver_t *drv, mr_uint32_t channel,
                          const mr_dma_transfer_t *transfer) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
    const DMA_TriggerConfTypeDef *trigger;
    dma_channel_t *dma_channel;
    mr_bool_t circular;
    uint32_t src, dst;
    mr_err_t ret;

    /* Check parameter */
    dma_channel = dma_get_channel(priv, channel);
    if (dma_channel == MR_NULL) {
        return -MR_EINVAL;
    }
    circular = (transfer->next == transfer);
    if ((transfer->next != MR_NULL) && !circular) {
        return -MR_ENOSYS;
    }
    if (HAL_DMA_GetState(&dma_channel->handle) == HAL_DMA_STATE_RESET) {
        return -MR_ENODEV;
    }
    if (dma_hw_busy(dma_channel)) {
        return -MR_EBUSY;
    }
    trigger = dma_trigger(priv, channel);
    if (trigger == MR_NULL) {
        return -MR_EINVAL;
    }
    if (transfer->count > DMA_CBR1_BNDT) {
        return -MR_ERANGE;
    }
    ret = dma_set_mode(dma_channel, circular, trigger);
    if (ret != 0) {
        return ret;
    }

    /* Install the completion callbacks and snapshot this transfer */
    src = (uint32_t)(mr_uintptr_t)transfer->src;
    dst = (uint32_t)(mr_uintptr_t)transfer->dst;
    dma_channel->handle.XferCpltCallback = dma_transfer_complete;
    dma_channel->handle.XferHalfCpltCallback = dma_half_complete;
    dma_channel->handle.XferErrorCallback = dma_transfer_error;
    dma_channel->size = transfer->count;
    mr_atomic_store(&dma_channel->last_offset, 0);
#if defined(DMA_USE_DCACHE)
    dma_channel->src = transfer->src;
    dma_channel->dst = transfer->dst;
    dma_channel->cache_offset = 0U;
#endif /* defined(DMA_USE_DCACHE) */

    /* Build the circular descriptor before cache maintenance and start */
    if (dma_channel->circular) {
        ret = dma_build_linked_list(dma_channel, trigger, src, dst,
                                    (uint32_t)transfer->count);
        if (ret != 0) {
            return ret;
        }
    }
#if defined(DMA_USE_DCACHE)
    /* DMA owns cache maintenance; callers must isolate DMA-written cache
     * lines from unrelated live data or use non-cacheable memory */
    ret = dma_cache_prepare(dma_channel, transfer);
    if (ret != 0) {
        return ret;
    }
    if (dma_channel->circular) {
        ret = mr_port_dcache_clean(&dma_channel->node,
                                   sizeof(dma_channel->node));
        if (ret != 0) {
            return ret;
        }
    }
#endif /* defined(DMA_USE_DCACHE) */

    /* Start the transfer */
    if (dma_channel->circular) {
        ret = mr_stm32h5_hal_status(
            HAL_DMAEx_List_Start_IT(&dma_channel->handle));
    } else {
        ret = mr_stm32h5_hal_status(HAL_DMA_Start_IT(
            &dma_channel->handle, src, dst, (uint32_t)transfer->count));
    }
    if (ret == 0) {
        dma_apply_events(dma_channel);
    }
    return ret;
}

static mr_err_t dma_stop(const mr_driver_t *drv, mr_uint32_t channel) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
    dma_channel_t *dma_channel;
    HAL_DMA_StateTypeDef state;
    mr_size_t offset;
    mr_err_t ret;
#if defined(DMA_USE_DCACHE)
    mr_err_t cache_ret;
#endif /* defined(DMA_USE_DCACHE) */

    /* Check parameter */
    dma_channel = dma_get_channel(priv, channel);
    if (dma_channel == MR_NULL) {
        return -MR_EINVAL;
    }
    state = HAL_DMA_GetState(&dma_channel->handle);
    if (state == HAL_DMA_STATE_RESET) {
        return -MR_ENODEV;
    }
    if (state == HAL_DMA_STATE_ERROR) {
        /* HAL_DMA_Abort() only accepts BUSY. Preserve any DMA-written data,
         * then rebuild the channel from the saved mrX configuration. */
        offset = dma_current_offset(dma_channel);
        mr_atomic_store(&dma_channel->last_offset,
                        (mr_atomic_t)(offset * dma_memory_width(dma_channel)));
#if defined(DMA_USE_DCACHE)
        dma_cache_complete(dma_channel, offset);
#endif /* defined(DMA_USE_DCACHE) */
        ret = dma_channel_configure(drv, channel, &dma_channel->config);
        mr_atomic_store(&dma_channel->last_offset,
                        (mr_atomic_t)(offset * dma_memory_width(dma_channel)));
        return ret;
    }
    if (state == HAL_DMA_STATE_SUSPEND) {
        return -MR_EBUSY;
    }
    if (state != HAL_DMA_STATE_BUSY) {
        return 0;
    }

    /* Save the current progress before aborting the channel */
    offset = dma_current_offset(dma_channel);
    mr_atomic_store(&dma_channel->last_offset,
                    (mr_atomic_t)(offset * dma_memory_width(dma_channel)));
    ret = mr_stm32h5_hal_status(HAL_DMA_Abort(&dma_channel->handle));
#if defined(DMA_USE_DCACHE)
    if (ret == 0) {
        cache_ret = dma_cache_complete(dma_channel, offset);
        if (cache_ret != 0) {
            return cache_ret;
        }
    }
#endif /* defined(DMA_USE_DCACHE) */
    return ret;
}

static mr_err_t dma_is_busy(const mr_driver_t *drv, mr_uint32_t channel,
                            mr_bool_t *busy) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
    dma_channel_t *dma_channel;

    /* Check parameter */
    dma_channel = dma_get_channel(priv, channel);
    if (dma_channel == MR_NULL) {
        return -MR_EINVAL;
    }
    if (HAL_DMA_GetState(&dma_channel->handle) == HAL_DMA_STATE_RESET) {
        return -MR_ENODEV;
    }
    *busy = dma_hw_busy(dma_channel);
    return 0;
}

static mr_err_t dma_offset(const mr_driver_t *drv, mr_uint32_t channel,
                           mr_size_t *offset) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
    dma_channel_t *dma_channel;

    /* Check parameter */
    dma_channel = dma_get_channel(priv, channel);
    if (dma_channel == MR_NULL) {
        return -MR_EINVAL;
    }
    if (HAL_DMA_GetState(&dma_channel->handle) == HAL_DMA_STATE_RESET) {
        return -MR_ENODEV;
    }
    if (dma_hw_busy(dma_channel)) {
        /* Public offsets are bytes; dma_current_offset() counts data items */
        mr_atomic_store(
            &dma_channel->last_offset,
            (mr_atomic_t)(dma_current_offset(dma_channel)
                          * dma_memory_width(dma_channel)));
    }
    *offset = (mr_size_t)mr_atomic_load(&dma_channel->last_offset);
    return 0;
}

#if defined(MR_USE_WAIT)
static mr_err_t dma_event_attach(const mr_driver_t *drv, mr_uint32_t channel,
                                 mr_waiter_t *entry) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
    dma_channel_t *dma_channel;

    /* Check parameter */
    dma_channel = dma_get_channel(priv, channel);
    if (dma_channel == MR_NULL) {
        return -MR_EINVAL;
    }
    if (HAL_DMA_GetState(&dma_channel->handle) == HAL_DMA_STATE_RESET) {
        return -MR_ENODEV;
    }

    /* Attach DMA channel event */
    return mr_waiter_wait(entry, &dma_channel->events);
}

static mr_err_t dma_event_control(const mr_driver_t *drv, mr_uint32_t channel,
                                  mr_uint32_t events, mr_bool_t enable) {
    dma_priv_t *priv = MR_DRIVER_PRIV(drv);
    dma_channel_t *dma_channel;

    /* Check parameter */
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

    /* Recompute subscribed sources and mandatory internal handling. */
    if (enable) {
        dma_channel->event_mask |= events;
    } else {
        dma_channel->event_mask &= ~events;
    }
    if (dma_hw_busy(dma_channel)) {
        dma_apply_events(dma_channel);
    }
    return 0;
}
#endif /* defined(MR_USE_WAIT) */

static void dma_irq_handle(mr_uint32_t ctrl, mr_uint32_t channel) {
    DMA_HandleTypeDef *handle;

    /* Validate the IRQ table slot and forward the interrupt to HAL */
    if ((ctrl >= DMA_CONTROLLERS) || (channel >= DMA_CHANNELS)) {
        return;
    }
    handle = __dma_irq_handles[ctrl][channel];
    if (handle == MR_NULL) {
        return;
    }
    HAL_DMA_IRQHandler(handle);
}

/* Instance install */
#define ST_HT_DMA_IRQ(_id, _n)                                                 \
    MR_MACRO_IF(                                                               \
        MR_HT_DRIVER_HAS_PROP(_id, MR_CAT(st_irq_handler_, _n)),               \
        (void MR_HT_DRIVER_PROP(_id, MR_CAT(st_irq_handler_, _n))(void) {      \
            dma_irq_handle(MR_HT_DRIVER_PROP(_id, st_ctrl), (_n));             \
        }),                                                                    \
        ())

#define __ST_HT_DMA_ROUTE_IMPL(_elem, _node, _prop)                            \
    {                                                                          \
        .request = MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 0),               \
        .trigger = {                                                           \
            .TriggerMode = MR_HT_NODE_PROP_ELEM_OR(                            \
                _node, _prop, _elem, 1, DMA_TRIGM_BLOCK_TRANSFER),             \
            .TriggerPolarity = MR_HT_NODE_PROP_ELEM_OR(                        \
                _node, _prop, _elem, 2, DMA_TRIG_POLARITY_MASKED),             \
            .TriggerSelection =                                                \
                MR_HT_NODE_PROP_ELEM_OR(_node, _prop, _elem, 3, 0U),           \
        },                                                                     \
    }
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
        .instance = MR_HT_DRIVER_PROP(_id, st_gpdmax),                         \
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

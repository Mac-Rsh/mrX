/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_DRIVER_DMA_H__
#define __MR_DRIVER_DMA_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_DRIVER_DMA)
#include <mrX/drivers/mr_driver.h>
#endif /* defined(MR_USE_DRIVER_DMA) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup DMA
 * @{
 */

#if defined(MR_USE_DRIVER_DMA)
/* DMA wait event type definition */
#define MR_DMA_WAIT_HALF_COMPLETE       (1ULL << 0)
#define MR_DMA_WAIT_COMPLETE            (1ULL << 1)

/* DMA data size definition */
typedef enum mr_dma_data_size {
    MR_DMA_DATA_SIZE_BYTE,
    MR_DMA_DATA_SIZE_HALF_WORD,
    MR_DMA_DATA_SIZE_WORD,
    MR_DMA_DATA_SIZE_DWORD,
} mr_dma_data_size_t;

/* DMA address adjust definition */
typedef enum mr_dma_addr_adjust {
    MR_DMA_ADDR_ADJ_INC,
    MR_DMA_ADDR_ADJ_DEC,
    MR_DMA_ADDR_ADJ_CONST,
} mr_dma_addr_adjust_t;

/* DMA reload definition */
typedef enum mr_dma_reload {
    MR_DMA_RELOAD_DISABLE,
    MR_DMA_RELOAD_ENABLE,
} mr_dma_reload_t;

/* DMA burst length definition */
typedef enum mr_dma_burst_len {
    MR_DMA_DMA_BURST_LEN_1,
    MR_DMA_DMA_BURST_LEN_2,
    MR_DMA_DMA_BURST_LEN_4,
    MR_DMA_DMA_BURST_LEN_8,
    MR_DMA_DMA_BURST_LEN_16,
    MR_DMA_DMA_BURST_LEN_32,
    MR_DMA_DMA_BURST_LEN_64,
    MR_DMA_DMA_BURST_LEN_128,
    MR_DMA_DMA_BURST_LEN_256,
} mr_dma_burst_len_t;

/* DMA FIFO threshold definition */
typedef enum mr_dma_fifo_threshold {
    MR_DMA_FIFO_THRESHOLD_EMPTY,
    MR_DMA_FIFO_THRESHOLD_1_4,
    MR_DMA_FIFO_THRESHOLD_1_2,
    MR_DMA_FIFO_THRESHOLD_3_4,
    MR_DMA_FIFO_THRESHOLD_FULL,
} mr_dma_fifo_threshold_t;

/* DMA direction definition */
typedef enum mr_dma_direction {
    MR_DMA_DIR_M2M,
    MR_DMA_DIR_M2P,
    MR_DMA_DIR_P2M,
    MR_DMA_DIR_P2P,
} mr_dma_direction_t;

/* DMA configuration type */
typedef struct mr_dma_config {
#if defined(MR_USE_DRIVER_DMA_WIDTH_64B)
    mr_uint64_t src_addr;
    mr_uint64_t dst_addr;
#else
    mr_uint32_t src_addr;
    mr_uint32_t dst_addr;
#endif /* defined(MR_USE_DRIVER_DMA_WIDTH_64B) */
    mr_uint32_t src_data_size: 2;
    mr_uint32_t dst_data_size: 2;
    mr_uint32_t src_addr_adj: 2;
    mr_uint32_t dst_addr_adj: 2;
    mr_uint32_t src_reload: 1;
    mr_uint32_t dst_reload: 1;
    mr_uint32_t src_burst_len: 4;
    mr_uint32_t dst_burst_len: 4;
    mr_uint32_t fifo_threshold: 3;
    mr_uint32_t direction: 2;
    mr_uint32_t events: 3;
    mr_uint32_t block_size;
    struct mr_dma_config *next;
} mr_dma_config_t;

/* DMA status type */
typedef struct mr_dma_status {
    mr_uint32_t direction: 2;
    mr_uint32_t busy: 1;
    mr_uint32_t pending_size;
} mr_dma_status_t;

/* DMA data type */
MR_DRIVER_CLAZZES_EXPORT(dma);
typedef struct mr_dma_data {
    mr_uint32_t exist_mask;
} mr_dma_data_t;

/* DMA operations type */
typedef struct mr_dma_ops {
    mr_err_t (*channel_config)(const mr_driver_t *drv, mr_uint32_t channel,
                               const mr_dma_config_t *config);
    mr_err_t (*start)(const mr_driver_t *drv, mr_uint32_t channel);
    mr_err_t (*stop)(const mr_driver_t *drv, mr_uint32_t channel);
    mr_err_t (*get_status)(const mr_driver_t *drv, mr_uint32_t channel,
                           mr_dma_status_t *status);
    mr_err_t (*attach)(const mr_driver_t *drv, mr_uint32_t channel,
                       mr_waiter_t *entry);
} mr_dma_ops_t;

/**
 * @brief This macro function initializes a DMA data.
 *
 * @param _exist_mask The DMA channel exist mask.
 */
#define MR_DMA_DATA_INIT(_exist_mask)                                          \
    {                                                                          \
        .exist_mask = (_exist_mask),                                           \
    }

/**
 * @brief This macro function defines a DMA driver.
 *
 * @param _name The DMA driver name.
 * @param _data The DMA driver data.
 * @param _priv The DMA private data.
 * @param _ops The DMA operations.
 */
#define MR_DMA_DEFINE(_name, _data, _priv, _ops)                               \
    MR_DRIVER_DEFINE(_name,                                                    \
                     MR_DRIVER_CLAZZES_STATIK(MR_DRIVER_CLAZZES_FIND(dma)),    \
                     MR_DRIVER_TYPE_DMA, _data, _priv, _ops)

/**
 * @brief This macro function installs a DMA driver.
 *
 * @param _name The DMA driver name.
 * @param _data The DMA driver data.
 * @param _priv The DMA private data.
 * @param _ops The DMA operations.
 * @param _level The init level.
 */
#define MR_DMA_INSTALL(_name, _data, _priv, _ops, _level)                      \
    MR_DRIVER_INSTALL(_name,                                                   \
                      MR_DRIVER_CLAZZES_STATIK(MR_DRIVER_CLAZZES_FIND(dma)),   \
                      MR_DRIVER_TYPE_DMA, _data, _priv, _ops, _level)

/**
 * @brief This function initializes a DMA driver.
 *
 * @param drv The DMA driver.
 * @param data The DMA driver data.
 * @param priv The DMA private data.
 * @param ops The DMA operations.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_dma_init(mr_driver_t *drv, mr_dma_data_t *data,
                               void *priv, mr_dma_ops_t *ops) {
    const mr_driver_clazzes_t *dclazzes = MR_DRIVER_CLAZZES_FIND(dma);
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_STATIK(dclazzes);

    /* Check parameter */
    MR_ASSERT(drv != MR_NULL);
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Init DMA driver */
    return mr_driver_init(drv, dclazz, MR_DRIVER_TYPE_DMA, data, priv, ops);
}

/**
 * @brief This function creates a DMA driver.
 *
 * @param data The DMA driver data.
 * @param priv The DMA private data.
 * @param ops The DMA operations.
 * @return The DMA driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_dma_create(mr_dma_data_t *data, void *priv,
                                     mr_dma_ops_t *ops) {
    const mr_driver_clazzes_t *dclazzes = MR_DRIVER_CLAZZES_FIND(dma);
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_DYNAMIC(dclazzes);

    /* Check parameter */
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Create DMA driver */
    return mr_driver_create(dclazz, MR_DRIVER_TYPE_DMA, data, priv, ops);
}

/**
 * @brief This function deletes a DMA driver.
 *
 * @param drv The DMA driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_dma_del(mr_driver_t *drv) {
    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DMA);

    /* Delete driver */
    return mr_driver_del(drv);
}

/**
 * @brief This function configures a DMA channel.
 *
 * @param drv The DMA driver.
 * @param channel The DMA channel.
 * @param config The DMA channel configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_dma_configure(mr_driver_t *drv, mr_uint32_t channel,
                          mr_dma_config_t *config);

/**
 * @brief This function starts a DMA channel transfer.
 *
 * @param drv The DMA driver.
 * @param channel The DMA channel.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_dma_start(mr_driver_t *drv, mr_uint32_t channel) {
    const mr_dma_ops_t *dma_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DMA);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(channel < 32);

    /* Get DMA operations */
    dma_ops = (mr_dma_ops_t *)MR_DRIVER_OPS(drv);

    /* Start DMA transfer */
    return dma_ops->start(drv, channel);
}

/**
 * @brief This function stops a DMA channel transfer.
 *
 * @param drv The DMA driver.
 * @param channel The DMA channel.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_dma_stop(mr_driver_t *drv, mr_uint32_t channel) {
    const mr_dma_ops_t *dma_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DMA);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(channel < 32);

    /* Get DMA operations */
    dma_ops = (mr_dma_ops_t *)MR_DRIVER_OPS(drv);

    /* Stop DMA transfer */
    return dma_ops->stop(drv, channel);
}

/**
 * @brief This function gets a DMA channel status.
 *
 * @param drv The DMA driver.
 * @param channel The DMA channel.
 * @param status The DMA status.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_dma_status(mr_driver_t *drv, mr_uint32_t channel,
                                 mr_dma_status_t *status) {
    const mr_dma_ops_t *dma_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DMA);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(channel < 32);

    /* Get DMA operations */
    dma_ops = (mr_dma_ops_t *)MR_DRIVER_OPS(drv);

    /* Get DMA status */
    return dma_ops->get_status(drv, channel, status);
}

/**
 * @brief This function attaches a DMA channel interrupt.
 *
 * @param drv The DMA driver.
 * @param channel The DMA channel.
 * @param entry The DMA interrupt entry.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_dma_irq_attach(mr_driver_t *drv, mr_uint32_t channel,
                                     mr_waiter_t *entry) {
    const mr_dma_ops_t *dma_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DMA);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(entry != MR_NULL);
    MR_ASSERT(channel < 32);

    /* Get DMA operations */
    dma_ops = (mr_dma_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if attach is supported */
    if (!dma_ops->attach) {
        return -MR_ENOSYS;
    }

    /* Attach DMA channel irq */
    return dma_ops->attach(drv, channel, entry);
}

/**
 * @brief This function gets a DMA driver.
 *
 * @param drv The DMA driver.
 * @return The DMA driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_dma_get(mr_driver_t *drv) {
    /* Get driver reference */
    return mr_driver_get(drv);
}

/**
 * @brief This function puts a DMA driver.
 *
 * @param drv The DMA driver.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_dma_put(mr_driver_t *drv) {
    /* Put driver reference */
    return mr_driver_put(drv);
}

/* ================================ HardTree ================================ */

/**
 * @brief This macro function installs a DMA instance through hardtree.
 *
 * @param _id The instance id.
 * @param _priv The DMA private data.
 * @param _ops The DMA operations.
 */
#define MR_HT_INST_DMA_INSTALL(_id, _priv, _ops)                               \
    static mr_dma_data_t MR_CAT(__mr_dma_data_, _id) = MR_DMA_DATA_INIT(       \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), exist_mask, 0xffffffffU));  \
    MR_HT_INST_DRIVER_INSTALL(                                                 \
        _id, MR_DRIVER_CLAZZES_STATIK(MR_DRIVER_CLAZZES_FIND(dma)),            \
        MR_DRIVER_TYPE_DMA, &MR_CAT(__mr_dma_data_, _id), _priv, _ops)

/* DMA hardtree specification type */
typedef struct mr_ht_dma_spec {
    mr_driver_t *drv;
    mr_uint8_t channel;
} mr_ht_dma_spec_t;

/**
 * @brief This function configures a DMA channel.
 *
 * @param spec The DMA specification.
 * @param config The DMA channel configuration.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_dma_configure(mr_ht_dma_spec_t *spec,
                                       mr_dma_config_t *config) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Configure DMA channel */
    return mr_dma_configure(spec->drv, spec->channel, config);
}

/**
 * @brief This function starts a DMA transfer.
 *
 * @param spec The DMA specification.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_dma_start(mr_ht_dma_spec_t *spec) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Start DMA transfer */
    return mr_dma_start(spec->drv, spec->channel);
}

/**
 * @brief This function stops a DMA transfer.
 *
 * @param spec The DMA specification.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_dma_stop(mr_ht_dma_spec_t *spec) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Stop DMA transfer */
    return mr_dma_stop(spec->drv, spec->channel);
}

/**
 * @brief This function attaches a DMA interrupt.
 *
 * @param spec The DMA specification.
 * @param entry The DMA interrupt entry.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_dma_attach(mr_ht_dma_spec_t *spec,
                                    mr_waiter_t *entry) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Attach DMA channel irq */
    return mr_dma_irq_attach(spec->drv, spec->channel, entry);
}

/**
 * @brief This macro function creates a DMA specification structure from
 *        hardtree properties.
 *
 * @param _node The node.
 * @param _prop The property name.
 * @return The initialized mr_ht_dma_spec_t structure.
 *
 * @note Property cells: [phandle, channel].
 */
#define MR_HT_DMA_SPEC(_node, _prop)    __MR_HT_DMA_SPEC_IMPL(0, _node, _prop)
#define __MR_HT_DMA_SPEC_IMPL(_elem, _node, _prop)                             \
    {                                                                          \
        .drv                                                                   \
        = MR_HT_DRIVER_FIND(MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 0)),     \
        .channel = (mr_uint8_t)MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 1),   \
    }

/**
 * @brief This macro function iterates over all DMA specification elements
 *        in a property array and creates corresponding structures.
 *
 * @param _node The node.
 * @param _prop The property name.
 *
 * @note Property cells: [phandle, channel].
 */
#define MR_HT_DMA_SPEC_FOREACH(_node, _prop)                                   \
    MR_HT_NODE_PROP_ELEM_FOREACH(_node, _prop, __MR_HT_DMA_SPEC_IMPL)
#endif /* defined(MR_USE_DRIVER_DMA) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_DMA_H__ */

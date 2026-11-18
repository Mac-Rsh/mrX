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
 * @addtogroup Drivers-DMA
 * @{
 */

#if defined(MR_USE_DRIVER_DMA)
/* DMA wait event definition */
#define MR_DMA_WAIT_HALF                (1U << 0)
#define MR_DMA_WAIT_DONE                (1U << 1)
#define MR_DMA_WAIT_ERROR               (1U << 2)

/* DMA width definition */
typedef enum mr_dma_width {
    MR_DMA_WIDTH_8,
    MR_DMA_WIDTH_16,
    MR_DMA_WIDTH_32,
    MR_DMA_WIDTH_64,
} mr_dma_width_t;

/* DMA address adjust definition */
typedef enum mr_dma_adjust {
    MR_DMA_ADJ_INC,
    MR_DMA_ADJ_DEC,
    MR_DMA_ADJ_CONST,
} mr_dma_adjust_t;

/* DMA burst definition */
typedef enum mr_dma_burst {
    MR_DMA_BURST_1,
    MR_DMA_BURST_2,
    MR_DMA_BURST_4,
    MR_DMA_BURST_8,
    MR_DMA_BURST_16,
    MR_DMA_BURST_32,
    MR_DMA_BURST_64,
    MR_DMA_BURST_128,
    MR_DMA_BURST_256,
} mr_dma_burst_t;

/* DMA direction definition */
typedef enum mr_dma_direction {
    MR_DMA_DIR_P2M,
    MR_DMA_DIR_M2P,
    MR_DMA_DIR_M2M,
} mr_dma_direction_t;

/* DMA channel configuration type */
typedef struct mr_dma_config {
    mr_uint32_t src_width: 2;
    mr_uint32_t dst_width: 2;
    mr_uint32_t src_adj: 2;
    mr_uint32_t dst_adj: 2;
    mr_uint32_t src_burst: 4;
    mr_uint32_t dst_burst: 4;
    mr_uint32_t direction: 2;
} mr_dma_config_t;

/* DMA transfer type */
typedef struct mr_dma_transfer {
    struct mr_dma_transfer *next;
    mr_size_t count; /* Logical transfer item count */
    const void *src;
    void *dst;
} mr_dma_transfer_t;

/* DMA data type */
MR_DRIVER_CLAZZES_EXPORT(dma);
typedef struct mr_dma_data {
    mr_uint64_t exist_mask;
} mr_dma_data_t;

/* DMA operations type */
typedef struct mr_dma_ops {
    mr_err_t (*channel_configure)(const mr_driver_t *drv, mr_uint32_t channel,
                                  const mr_dma_config_t *config);
    mr_err_t (*channel_configuration)(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      mr_dma_config_t *config);
    mr_err_t (*start)(const mr_driver_t *drv, mr_uint32_t channel,
                      const mr_dma_transfer_t *transfer);
    mr_err_t (*stop)(const mr_driver_t *drv, mr_uint32_t channel);
    mr_err_t (*is_busy)(const mr_driver_t *drv, mr_uint32_t channel,
                        mr_bool_t *busy);
    mr_err_t (*offset)(const mr_driver_t *drv, mr_uint32_t channel,
                       mr_size_t *offset);
    mr_err_t (*event_attach)(const mr_driver_t *drv, mr_uint32_t channel,
                             mr_waiter_t *entry);
    mr_err_t (*event_control)(const mr_driver_t *drv, mr_uint32_t channel,
                              mr_uint32_t events, mr_bool_t enable);
} mr_dma_ops_t;

/**
 * @brief This macro function initializes DMA data.
 *
 * @param _exist_mask The DMA channel exist mask; bit n represents channel n.
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
    MR_DRIVER_DEFINE(_name, MR_DRIVER_CLAZZES_STATIK(dma), MR_DRIVER_TYPE_DMA, \
                     _data, _priv, _ops)

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
    MR_DRIVER_INSTALL(_name, MR_DRIVER_CLAZZES_STATIK(dma),                    \
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
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_STATIK(dma);

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
 * @param data The dynamically allocated DMA data, freed when the created
 * driver is deleted.
 * @param priv The DMA private data.
 * @param ops The DMA operations.
 * @return The DMA driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_dma_create(mr_dma_data_t *data, void *priv,
                                     mr_dma_ops_t *ops) {
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_DYNAMIC(dma);

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
    MR_DRIVER_ASSERT_TYPE(drv, MR_DRIVER_TYPE_DMA);

    /* Delete driver */
    return mr_driver_del(drv);
}

/**
 * @brief This function configures a DMA channel.
 *
 * @param drv The DMA driver.
 * @param channel The DMA channel number.
 * @param config The DMA channel configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_dma_configure(mr_driver_t *drv, mr_uint32_t channel,
                          const mr_dma_config_t *config);

/**
 * @brief This function gets a DMA channel configuration.
 *
 * @param drv The DMA driver.
 * @param channel The DMA channel number.
 * @param config The DMA channel configuration output.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_dma_configuration(mr_driver_t *drv, mr_uint32_t channel,
                              mr_dma_config_t *config);

/**
 * @brief This function starts a DMA transfer.
 *
 * @param drv The DMA driver.
 * @param channel The DMA channel number.
 * @param transfer The DMA transfer descriptor.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_dma_start(mr_driver_t *drv, mr_uint32_t channel,
                                mr_dma_transfer_t *transfer) {
    const mr_dma_ops_t *dm_ops;
    mr_dma_data_t *dm_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DMA);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(transfer != MR_NULL);
    MR_ASSERT(transfer->count != 0);
    MR_ASSERT(transfer->src != MR_NULL);
    MR_ASSERT(transfer->dst != MR_NULL);

    /* Get DMA operations and data */
    dm_ops = (const mr_dma_ops_t *)MR_DRIVER_OPS(drv);
    dm_data = (mr_dma_data_t *)MR_DRIVER_DATA(drv);
    MR_ASSERT(dm_ops->start != MR_NULL);

    /* Check whether the channel exists */
    if ((channel >= 64U) || (!(dm_data->exist_mask & (1ULL << channel)))) {
        return -MR_EINVAL;
    }

    /* Start transfer */
    return dm_ops->start(drv, channel, transfer);
}

/**
 * @brief This function stops a DMA transfer.
 *
 * @param drv The DMA driver.
 * @param channel The DMA channel number.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_dma_stop(mr_driver_t *drv, mr_uint32_t channel) {
    const mr_dma_ops_t *dm_ops;
    mr_dma_data_t *dm_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DMA);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get DMA operations and data */
    dm_ops = (const mr_dma_ops_t *)MR_DRIVER_OPS(drv);
    dm_data = (mr_dma_data_t *)MR_DRIVER_DATA(drv);
    MR_ASSERT(dm_ops->stop != MR_NULL);

    /* Check whether the channel exists */
    if ((channel >= 64U) || (!(dm_data->exist_mask & (1ULL << channel)))) {
        return -MR_EINVAL;
    }

    /* Stop transfer */
    return dm_ops->stop(drv, channel);
}

/**
 * @brief This function checks if a DMA transfer is busy.
 *
 * @param drv The DMA driver.
 * @param channel The DMA channel number.
 * @param busy The transfer busy state output.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_dma_is_busy(mr_driver_t *drv, mr_uint32_t channel,
                                  mr_bool_t *busy) {
    const mr_dma_ops_t *dm_ops;
    mr_dma_data_t *dm_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DMA);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(busy != MR_NULL);

    /* Get DMA operations and data */
    dm_ops = (const mr_dma_ops_t *)MR_DRIVER_OPS(drv);
    dm_data = (mr_dma_data_t *)MR_DRIVER_DATA(drv);
    MR_ASSERT(dm_ops->is_busy != MR_NULL);

    /* Check whether the channel exists */
    if ((channel >= 64U) || (!(dm_data->exist_mask & (1ULL << channel)))) {
        return -MR_EINVAL;
    }

    /* Get transfer state */
    return dm_ops->is_busy(drv, channel, busy);
}

/**
 * @brief This function gets a DMA transfer offset in bytes.
 *
 * @param drv The DMA driver.
 * @param channel The DMA channel number.
 * @param offset The transfer offset output.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_dma_offset(mr_driver_t *drv, mr_uint32_t channel,
                                 mr_size_t *offset) {
    const mr_dma_ops_t *dm_ops;
    mr_dma_data_t *dm_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DMA);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(offset != MR_NULL);

    /* Get DMA operations and data */
    dm_ops = (const mr_dma_ops_t *)MR_DRIVER_OPS(drv);
    dm_data = (mr_dma_data_t *)MR_DRIVER_DATA(drv);
    MR_ASSERT(dm_ops->offset != MR_NULL);

    /* Check whether the channel exists */
    if ((channel >= 64U) || (!(dm_data->exist_mask & (1ULL << channel)))) {
        return -MR_EINVAL;
    }

    /* Get transfer offset */
    return dm_ops->offset(drv, channel, offset);
}

/**
 * @brief This function enables events for a DMA channel.
 *
 * @param drv The DMA driver.
 * @param channel The DMA channel number.
 * @param events The DMA event mask.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_dma_event_enable(mr_driver_t *drv, mr_uint32_t channel,
                                       mr_uint32_t events) {
    const mr_dma_ops_t *dm_ops;
    mr_dma_data_t *dm_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DMA);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get DMA operations and data */
    dm_ops = (const mr_dma_ops_t *)MR_DRIVER_OPS(drv);
    dm_data = (mr_dma_data_t *)MR_DRIVER_DATA(drv);

    /* Check whether the channel exists */
    if ((channel >= 64U) || (!(dm_data->exist_mask & (1ULL << channel)))) {
        return -MR_EINVAL;
    }

    /* Check whether event control is supported */
    if (!dm_ops->event_control) {
        return -MR_ENOSYS;
    }

    /* Enable DMA events */
    return dm_ops->event_control(drv, channel, events, MR_TRUE);
}

/**
 * @brief This function disables events for a DMA channel.
 *
 * @param drv The DMA driver.
 * @param channel The DMA channel number.
 * @param events The DMA event mask.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_dma_event_disable(mr_driver_t *drv, mr_uint32_t channel,
                                        mr_uint32_t events) {
    const mr_dma_ops_t *dm_ops;
    mr_dma_data_t *dm_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DMA);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get DMA operations and data */
    dm_ops = (const mr_dma_ops_t *)MR_DRIVER_OPS(drv);
    dm_data = (mr_dma_data_t *)MR_DRIVER_DATA(drv);

    /* Check whether the channel exists */
    if ((channel >= 64U) || (!(dm_data->exist_mask & (1ULL << channel)))) {
        return -MR_EINVAL;
    }

    /* Check whether event control is supported */
    if (!dm_ops->event_control) {
        return -MR_ENOSYS;
    }

    /* Disable DMA events */
    return dm_ops->event_control(drv, channel, events, MR_FALSE);
}

/**
 * @brief This function attaches a waiter to a DMA channel event.
 *
 * @param drv The DMA driver.
 * @param channel The DMA channel number.
 * @param entry The DMA event waiter.
 * @return 0 on success, a negative error code on failure.
 * @note The backend may return -MR_ENOSYS when it does not support events.
 */
MR_INLINE mr_err_t mr_dma_event_attach(mr_driver_t *drv, mr_uint32_t channel,
                                       mr_waiter_t *entry) {
    const mr_dma_ops_t *dm_ops;
    mr_dma_data_t *dm_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DMA);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(entry != MR_NULL);

    /* Get DMA operations and data */
    dm_ops = (const mr_dma_ops_t *)MR_DRIVER_OPS(drv);
    dm_data = (mr_dma_data_t *)MR_DRIVER_DATA(drv);

    /* Check whether the channel exists */
    if ((channel >= 64U) || (!(dm_data->exist_mask & (1ULL << channel)))) {
        return -MR_EINVAL;
    }

    /* Check whether event attach is supported */
    if (!dm_ops->event_attach) {
        return -MR_ENOSYS;
    }

    /* Attach DMA channel event */
    return dm_ops->event_attach(drv, channel, entry);
}

/* ================================ HardTree ================================ */

/**
 * @brief This macro function installs a DMA instance through HardTree.
 *
 * @param _id The instance id.
 * @param _priv The DMA private data.
 * @param _ops The DMA operations.
 */
#define MR_HT_DMA_INSTALL(_id, _priv, _ops)                                    \
    static mr_dma_data_t MR_CAT(__mr_dma_data_, _id) = MR_DMA_DATA_INIT(       \
        MR_HT_DRIVER_PROP_OR(_id, exist_mask, 0xffffffffffffffffULL));         \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(dma),                   \
                         MR_DRIVER_TYPE_DMA, &MR_CAT(__mr_dma_data_, _id),     \
                         _priv, _ops)

/* HardTree DMA specification type */
typedef struct mr_ht_dma_spec {
    mr_driver_t *drv;
    mr_uint32_t channel;
} mr_ht_dma_spec_t;

/**
 * @brief This function configures a HardTree DMA specification.
 *
 * @param spec The DMA specification.
 * @param config The DMA channel configuration.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_dma_configure(mr_ht_dma_spec_t *spec,
                                       const mr_dma_config_t *config) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Configure DMA channel */
    return mr_dma_configure(spec->drv, spec->channel, config);
}

/**
 * @brief This function gets a HardTree DMA specification configuration.
 *
 * @param spec The DMA specification.
 * @param config The DMA channel configuration output.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_dma_configuration(mr_ht_dma_spec_t *spec,
                                           mr_dma_config_t *config) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Get DMA channel configuration */
    return mr_dma_configuration(spec->drv, spec->channel, config);
}

/**
 * @brief This function starts a DMA transfer for a HardTree DMA specification.
 *
 * @param spec The DMA specification.
 * @param transfer The DMA transfer descriptor.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_dma_start(mr_ht_dma_spec_t *spec,
                                   mr_dma_transfer_t *transfer) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Start DMA transfer */
    return mr_dma_start(spec->drv, spec->channel, transfer);
}

/**
 * @brief This function stops a DMA transfer for a HardTree DMA specification.
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
 * @brief This function checks if a HardTree DMA specification transfer is busy.
 *
 * @param spec The DMA specification.
 * @param busy The transfer busy state output.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_dma_is_busy(mr_ht_dma_spec_t *spec, mr_bool_t *busy) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Get transfer state */
    return mr_dma_is_busy(spec->drv, spec->channel, busy);
}

/**
 * @brief This function gets a HardTree DMA specification transfer offset in
 * bytes.
 *
 * @param spec The DMA specification.
 * @param offset The transfer offset output.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_dma_offset(mr_ht_dma_spec_t *spec, mr_size_t *offset) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Get transfer offset */
    return mr_dma_offset(spec->drv, spec->channel, offset);
}

/**
 * @brief This function attaches a waiter to a HardTree DMA specification event.
 *
 * @param spec The DMA specification.
 * @param entry The DMA event waiter.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_dma_event_attach(mr_ht_dma_spec_t *spec,
                                          mr_waiter_t *entry) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Attach DMA channel event */
    return mr_dma_event_attach(spec->drv, spec->channel, entry);
}

/**
 * @brief This function enables events for a HardTree DMA specification.
 *
 * @param spec The DMA specification.
 * @param events The DMA event mask.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_dma_event_enable(mr_ht_dma_spec_t *spec,
                                          mr_uint32_t events) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Enable DMA events */
    return mr_dma_event_enable(spec->drv, spec->channel, events);
}

/**
 * @brief This function disables events for a HardTree DMA specification.
 *
 * @param spec The DMA specification.
 * @param events The DMA event mask.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_dma_event_disable(mr_ht_dma_spec_t *spec,
                                           mr_uint32_t events) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Disable DMA events */
    return mr_dma_event_disable(spec->drv, spec->channel, events);
}

/**
 * @brief This macro function creates a DMA specification from HardTree
 * properties.
 *
 * @param _node The node.
 * @param _prop The property name.
 * @return The initialized DMA specification.
 *
 * @note Property cells: [phandle, channel].
 */
#define MR_HT_NODE_DMA_SPEC(_node, _prop)                                      \
    __MR_HT_NODE_DMA_SPEC_IMPL(0, _node, _prop)
#define __MR_HT_NODE_DMA_SPEC_IMPL(_elem, _node, _prop)                        \
    {                                                                          \
        .drv                                                                   \
        = MR_HT_DRIVER_FIND(MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 0)),     \
        .channel = MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 1),               \
    }

/**
 * @brief This macro function creates DMA specifications from a HardTree
 * property array.
 *
 * @param _node The node.
 * @param _prop The property name.
 *
 * @note Property cells: [phandle, channel].
 */
#define MR_HT_NODE_DMA_SPEC_FOREACH(_node, _prop)                              \
    MR_HT_NODE_PROP_ELEM_FOREACH(_node, _prop, __MR_HT_NODE_DMA_SPEC_IMPL)

/**
 * @brief This macro function creates a DMA specification from HardTree driver
 * properties.
 *
 * @param _id The driver instance id.
 * @param _prop The property name.
 * @return The initialized DMA specification.
 *
 * @note Property cells: [phandle, channel].
 */
#define MR_HT_DMA_SPEC(_id, _prop)                                             \
    MR_HT_NODE_DMA_SPEC(MR_HT_DRIVER(_id), _prop)

/**
 * @brief This macro function creates DMA specifications from a HardTree driver
 * property array.
 *
 * @param _id The driver instance id.
 * @param _prop The property name.
 *
 * @note Property cells: [phandle, channel].
 */
#define MR_HT_DMA_SPEC_FOREACH(_id, _prop)                                     \
    MR_HT_NODE_DMA_SPEC_FOREACH(MR_HT_DRIVER(_id), _prop)
#endif /* defined(MR_USE_DRIVER_DMA) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_DMA_H__ */

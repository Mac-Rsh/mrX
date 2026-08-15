/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_DRIVER_DAC_H__
#define __MR_DRIVER_DAC_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_DRIVER_DAC)
#include <mrX/drivers/mr_driver.h>
#endif /* defined(MR_USE_DRIVER_DAC) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup DAC
 * @{
 */

#if defined(MR_USE_DRIVER_DAC)
/* DAC wait event type definition */
#define MR_DAC_WAIT_STREAM_HALF         (1U << 0)
#define MR_DAC_WAIT_STREAM_FULL         (1U << 1)

/* DAC reference definition */
typedef enum mr_dac_ref {
    MR_DAC_REF_INTERNAL,
    MR_DAC_REF_VDD_1,
    MR_DAC_REF_VDD_2,
    MR_DAC_REF_VDD_3,
    MR_DAC_REF_VDD_4,
    MR_DAC_REF_EXTERNAL_0,
    MR_DAC_REF_EXTERNAL_1,
} mr_dac_ref_t;

/* DAC configuration type */
typedef struct mr_dac_config {
    mr_uint8_t ref: 3;
} mr_dac_config_t;

/* DAC sequence type */
typedef struct mr_dac_seq {
    mr_uint32_t channels;
    mr_uint8_t resolution;
    const void *buf;
    mr_size_t size;
} mr_dac_seq_t;

/* DAC data type */
MR_DRIVER_CLAZZES_EXPORT(dac);
typedef struct mr_dac_data {
    mr_uint16_t ref_internal;
    mr_uint32_t exist_mask;
    mr_waitable_t event;
} mr_dac_data_t;

/* DAC operations type */
typedef struct mr_dac_ops {
    mr_err_t (*channel_configure)(const mr_driver_t *drv, mr_uint32_t channel,
                                  const mr_dac_config_t *config);
    mr_err_t (*channel_configuration)(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      mr_dac_config_t *config);
    mr_ssize_t (*write)(const mr_driver_t *drv, mr_dac_seq_t *seq);
#if defined(MR_USE_DRIVER_DAC_STREAM)
    mr_err_t (*stream_start)(const mr_driver_t *drv, mr_dac_seq_t *seq,
                             mr_bool_t circular);
    mr_err_t (*stream_stop)(const mr_driver_t *drv);
    mr_err_t (*stream_is_busy)(const mr_driver_t *drv, mr_bool_t *busy);
    mr_err_t (*stream_offset)(const mr_driver_t *drv, mr_size_t *offset);
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */
} mr_dac_ops_t;

/**
 * @brief This macro function initializes a DAC data.
 *
 * @param _ref_internal The DAC reference internal.
 * @param _exist_mask The DAC exist mask.
 */
#define MR_DAC_DATA_INIT(_ref_internal, _exist_mask)                           \
    {                                                                          \
        .ref_internal = (_ref_internal),                                       \
        .exist_mask = (_exist_mask),                                           \
    }

/**
 * @brief This macro function defines a DAC driver.
 *
 * @param _name The DAC driver name.
 * @param _data The DAC driver data.
 * @param _priv The DAC private data.
 * @param _ops The DAC operations.
 */
#define MR_DAC_DEFINE(_name, _data, _priv, _ops)                               \
    MR_DRIVER_DEFINE(_name, MR_DRIVER_CLAZZES_STATIK(dac), MR_DRIVER_TYPE_DAC, \
                     _data, _priv, _ops)

/**
 * @brief This macro function installs a DAC driver.
 *
 * @param _name The DAC driver name.
 * @param _data The DAC driver data.
 * @param _priv The DAC private data.
 * @param _ops The DAC operations.
 * @param _level The init level.
 */
#define MR_DAC_INSTALL(_name, _data, _priv, _ops, _level)                      \
    MR_DRIVER_INSTALL(_name, MR_DRIVER_CLAZZES_STATIK(dac),                    \
                      MR_DRIVER_TYPE_DAC, _data, _priv, _ops, _level)

/**
 * @brief This function initializes a DAC driver.
 *
 * @param drv The DAC driver.
 * @param data The DAC driver data.
 * @param priv The DAC private data.
 * @param ops The DAC operations.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_dac_init(mr_driver_t *drv, mr_dac_data_t *data,
                               void *priv, mr_dac_ops_t *ops) {
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_STATIK(dac);

    /* Check parameter */
    MR_ASSERT(drv != MR_NULL);
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Init DAC driver */
    return mr_driver_init(drv, dclazz, MR_DRIVER_TYPE_DAC, data, priv, ops);
}

/**
 * @brief This function creates a DAC driver.
 *
 * @param data The DAC driver data.
 * @param priv The DAC private data.
 * @param ops The DAC operations.
 * @return The DAC driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_dac_create(mr_dac_data_t *data, void *priv,
                                     mr_dac_ops_t *ops) {
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_DYNAMIC(dac);

    /* Check parameter */
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Create DAC driver */
    return mr_driver_create(dclazz, MR_DRIVER_TYPE_DAC, data, priv, ops);
}

/**
 * @brief This function deletes a DAC driver.
 *
 * @param drv The DAC driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_dac_del(mr_driver_t *drv) {
    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DAC);

    /* Delete driver */
    return mr_driver_del(drv);
}

/**
 * @brief This function configures a DAC channel.
 *
 * @param drv The DAC driver.
 * @param channel The DAC channel.
 * @param config The DAC channel configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_dac_configure(mr_driver_t *drv, mr_uint32_t channel,
                          mr_dac_config_t *config);

/**
 * @brief This function gets a DAC channel configuration.
 *
 * @param drv The DAC driver.
 * @param channel The DAC channel.
 * @param config The DAC channel configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_dac_configuration(mr_driver_t *drv, mr_uint32_t channel,
                              mr_dac_config_t *config);

/**
 * @brief This function writes a DAC sequence.
 *
 * @param drv The DAC driver.
 * @param seq The DAC sequence.
 * @return The actual size of written data on success, a negative error
 * code on failure.
 */
MR_INLINE mr_ssize_t mr_dac_write(mr_driver_t *drv, mr_dac_seq_t *seq) {
    const mr_dac_ops_t *da_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DAC);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(seq != MR_NULL);
    MR_ASSERT((seq->resolution > 0U) && (seq->resolution <= 32U));
    MR_ASSERT((seq->buf != MR_NULL) || (seq->size == 0));

    /* Get DAC operations */
    da_ops = (mr_dac_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(da_ops->write != MR_NULL);

    /* Write DAC sequence */
    return da_ops->write(drv, seq);
}

#if defined(MR_USE_DRIVER_DAC_STREAM)
/**
 * @brief This function starts a DAC stream.
 *
 * @param drv The DAC driver.
 * @param seq The DAC sequence.
 * @param circular The circular mode.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_dac_stream_start(mr_driver_t *drv, mr_dac_seq_t *seq,
                                       mr_bool_t circular) {
    const mr_dac_ops_t *da_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DAC);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(seq != MR_NULL);
    MR_ASSERT((seq->resolution > 0U) && (seq->resolution <= 32U));
    MR_ASSERT((seq->buf != MR_NULL) || (seq->size == 0));

    /* Get DAC operations */
    da_ops = (mr_dac_ops_t *)MR_DRIVER_OPS(drv);

    /* Start DAC stream */
    return da_ops->stream_start(drv, seq, circular);
}

/**
 * @brief This function checks if a DAC stream is busy.
 *
 * @param drv The DAC driver.
 * @return MR_TRUE if the stream is busy, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_dac_stream_is_busy(mr_driver_t *drv) {
    const mr_dac_ops_t *da_ops;
    mr_bool_t busy;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DAC);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get DAC operations */
    da_ops = (mr_dac_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if operation is supported */
    if (!da_ops->stream_is_busy) {
        return MR_FALSE;
    }

    /* Get stream busy status */
    ret = da_ops->stream_is_busy(drv, &busy);
    if (ret != 0) {
        return MR_FALSE;
    }
    return busy;
}

/**
 * @brief This function stops a DAC stream.
 *
 * @param drv The DAC driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_dac_stream_stop(mr_driver_t *drv) {
    const mr_dac_ops_t *da_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DAC);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get DAC operations */
    da_ops = (mr_dac_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if operation is supported */
    if (!da_ops->stream_stop) {
        return -MR_ENOSYS;
    }

    /* Stop DAC stream */
    return da_ops->stream_stop(drv);
}

/**
 * @brief This function gets the DAC stream offset.
 *
 * @param drv The DAC driver.
 * @param offset The DAC stream offset.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_dac_stream_offset(mr_driver_t *drv, mr_size_t *offset) {
    const mr_dac_ops_t *da_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DAC);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(offset != MR_NULL);

    /* Get DAC operations */
    da_ops = (mr_dac_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if operation is supported */
    if (!da_ops->stream_offset) {
        return -MR_ENOSYS;
    }

    /* Get DAC stream offset */
    return da_ops->stream_offset(drv, offset);
}
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */

/**
 * @brief This function attaches a DAC event.
 *
 * @param drv The DAC driver.
 * @param entry The DAC event entry.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_dac_event_attach(mr_driver_t *drv, mr_waiter_t *entry) {
    mr_dac_data_t *da_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DAC);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(entry != MR_NULL);

    /* Get DAC data */
    da_data = (mr_dac_data_t *)MR_DRIVER_DATA(drv);

    /* Attach DAC event */
    return mr_waiter_wait(entry, &da_data->event);
}

/**
 * @brief This function raises DAC events.
 *
 * @param drv The DAC driver.
 * @param events The DAC events.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_dac_event_raise(mr_driver_t *drv,
                                      mr_wait_event_t events) {
    mr_dac_data_t *da_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DAC);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get DAC data */
    da_data = (mr_dac_data_t *)MR_DRIVER_DATA(drv);

    /* Raise DAC events */
    return mr_waitable_raise(&da_data->event, events, MR_NULL);
}

/**
 * @brief This function gets the DAC internal reference value.
 *
 * @param drv The DAC driver.
 * @return The DAC reference internal value.
 */
MR_INLINE mr_uint16_t mr_dac_ref_internal(mr_driver_t *drv) {
    mr_dac_data_t *da_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DAC);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get DAC data */
    da_data = (mr_dac_data_t *)MR_DRIVER_DATA(drv);

    /* Get DAC reference internal value */
    return da_data->ref_internal;
}

/**
 * @brief This function converts a millivolt value to a raw DAC value.
 *
 * @param ref_mv The reference millivolt value.
 * @param resolution The DAC resolution in bits.
 * @param millivolt The millivolt value.
 * @return The raw DAC value.
 */
MR_INLINE mr_int64_t mr_dac_millivolt2raw(mr_int32_t ref_mv,
                                          mr_uint8_t resolution,
                                          mr_int64_t millivolt) {
    mr_int64_t max_raw;

    /* Check parameter */
    MR_ASSERT((resolution > 0U) && (resolution <= 32U));
    MR_ASSERT(ref_mv > 0);

    /* Calculate raw */
    max_raw = (1LL << resolution) - 1;
    return (millivolt * max_raw) / ref_mv;
}

/**
 * @brief This function converts a microvolt value to a raw DAC value.
 *
 * @param ref_mv The reference millivolt value.
 * @param resolution The DAC resolution in bits.
 * @param microvolt The microvolt value.
 * @return The raw DAC value.
 */
MR_INLINE mr_int64_t mr_dac_microvolt2raw(mr_int32_t ref_mv,
                                          mr_uint8_t resolution,
                                          mr_int64_t microvolt) {
    mr_int64_t max_raw;

    /* Check parameter */
    MR_ASSERT((resolution > 0U) && (resolution <= 32U));
    MR_ASSERT(ref_mv > 0);

    /* Calculate raw */
    max_raw = (1LL << resolution) - 1;
    return (microvolt * max_raw) / ((mr_int64_t)ref_mv * 1000LL);
}

/**
 * @brief This function converts a volt value to a raw DAC value.
 *
 * @param ref_mv The reference millivolt value.
 * @param resolution The DAC resolution in bits.
 * @param volt The volt value.
 * @return The raw DAC value.
 */
MR_INLINE mr_int64_t mr_dac_volt2raw(mr_int32_t ref_mv, mr_uint8_t resolution,
                                     mr_f32_t volt) {
    /* Convert to raw */
    return mr_dac_millivolt2raw(ref_mv, resolution,
                                (mr_int64_t)(volt * 1000.0f));
}

/**
 * @brief This function gets a DAC driver.
 *
 * @param drv The DAC driver.
 * @return The DAC driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_dac_get(mr_driver_t *drv) {
    /* Get driver reference */
    return mr_driver_get(drv);
}

/**
 * @brief This function puts a DAC driver.
 *
 * @param drv The DAC driver.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_dac_put(mr_driver_t *drv) {
    /* Put driver reference */
    return mr_driver_put(drv);
}

/* ================================ HardTree ================================ */

/**
 * @brief This macro function installs a DAC instance through hardtree.
 *
 * @param _id The instance id.
 * @param _priv The DAC private data.
 * @param _ops The DAC operations.
 */
#define MR_HT_DAC_INSTALL(_id, _priv, _ops)                                    \
    static mr_dac_data_t MR_CAT(__mr_dac_data_, _id) = MR_DAC_DATA_INIT(       \
        MR_HT_DRIVER_PROP_OR(_id, ref_internal, 3300),                         \
        MR_HT_DRIVER_PROP_OR(_id, exist_mask, 0xffffffffU));                   \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(dac),                   \
                         MR_DRIVER_TYPE_DAC, &MR_CAT(__mr_dac_data_, _id),     \
                         _priv, _ops)

/* DAC hardtree specification type */
typedef struct mr_ht_dac_spec {
    mr_driver_t *drv;
    mr_uint8_t channel;
    mr_uint8_t resolution;
    mr_int32_t ref_voltage;
    mr_dac_config_t config;
} mr_ht_dac_spec_t;

/**
 * @brief This function configures a DAC specification.
 *
 * @param spec The DAC specification.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_dac_configure(mr_ht_dac_spec_t *spec) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Configure DAC channel */
    return mr_dac_configure(spec->drv, spec->channel, &spec->config);
}

/**
 * @brief This function writes a DAC specification sequence.
 *
 * @param spec The DAC specification.
 * @param seq The DAC sequence.
 * @return The actual size of written data on success, a negative error
 * code on failure.
 */
MR_INLINE mr_ssize_t mr_ht_dac_write(mr_ht_dac_spec_t *spec,
                                     mr_dac_seq_t *seq) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Write DAC sequence */
    return mr_dac_write(spec->drv, seq);
}

#if defined(MR_USE_DRIVER_DAC_STREAM)
/**
 * @brief This function starts a DAC specification stream.
 *
 * @param spec The DAC specification.
 * @param seq The DAC sequence.
 * @param circular The circular mode.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_dac_stream_start(mr_ht_dac_spec_t *spec,
                                          mr_dac_seq_t *seq,
                                          mr_bool_t circular) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Start DAC stream */
    return mr_dac_stream_start(spec->drv, seq, circular);
}

/**
 * @brief This function checks if a DAC specification stream is busy.
 *
 * @param spec The DAC specification.
 * @return MR_TRUE if the stream is busy, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_ht_dac_stream_is_busy(mr_ht_dac_spec_t *spec) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Check stream busy status */
    return mr_dac_stream_is_busy(spec->drv);
}

/**
 * @brief This function stops a DAC specification stream.
 *
 * @param spec The DAC specification.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_dac_stream_stop(mr_ht_dac_spec_t *spec) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Stop DAC stream */
    return mr_dac_stream_stop(spec->drv);
}

/**
 * @brief This function gets the DAC specification stream offset.
 *
 * @param spec The DAC specification.
 * @param offset The DAC stream offset.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_dac_stream_offset(mr_ht_dac_spec_t *spec,
                                           mr_size_t *offset) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Get DAC stream offset */
    return mr_dac_stream_offset(spec->drv, offset);
}
#endif /* defined(MR_USE_DRIVER_DAC_STREAM) */

/**
 * @brief This function converts a DAC specification millivolt value to raw.
 *
 * @param spec The DAC specification.
 * @param millivolt The millivolt value.
 * @return The raw DAC value.
 */
MR_INLINE mr_int64_t mr_ht_dac_millivolt2raw(mr_ht_dac_spec_t *spec,
                                             mr_int64_t millivolt) {
    mr_int32_t ref;

    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Get reference voltage */
    if (spec->ref_voltage == 0) {
        ref = mr_dac_ref_internal(spec->drv);
    } else {
        ref = spec->ref_voltage;
    }

    /* Convert to raw */
    return mr_dac_millivolt2raw(ref, spec->resolution, millivolt);
}

/**
 * @brief This function converts a DAC specification microvolt value to raw.
 *
 * @param spec The DAC specification.
 * @param microvolt The microvolt value.
 * @return The raw DAC value.
 */
MR_INLINE mr_int64_t mr_ht_dac_microvolt2raw(mr_ht_dac_spec_t *spec,
                                             mr_int64_t microvolt) {
    mr_int32_t ref;

    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Get reference voltage */
    if (spec->ref_voltage == 0) {
        ref = mr_dac_ref_internal(spec->drv);
    } else {
        ref = spec->ref_voltage;
    }

    /* Convert to raw */
    return mr_dac_microvolt2raw(ref, spec->resolution, microvolt);
}

/**
 * @brief This function converts a DAC specification volt value to raw.
 *
 * @param spec The DAC specification.
 * @param volt The volt value.
 * @return The raw DAC value.
 */
MR_INLINE mr_int64_t mr_ht_dac_volt2raw(mr_ht_dac_spec_t *spec,
                                        mr_f32_t volt) {
    mr_int32_t ref;

    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Get reference voltage */
    if (spec->ref_voltage == 0) {
        ref = mr_dac_ref_internal(spec->drv);
    } else {
        ref = spec->ref_voltage;
    }

    /* Convert to raw */
    return mr_dac_volt2raw(ref, spec->resolution, volt);
}

/**
 * @brief This macro function creates a DAC specification structure from
 * hardtree properties.
 *
 * @param _node The node.
 * @param _prop The property name.
 * @return The initialized mr_ht_dac_spec_t structure.
 *
 * @note Property cells: [phandle, channel, resolution, ref_voltage, ref].
 */
#define MR_HT_NODE_DAC_SPEC(_node, _prop)                                      \
    __MR_HT_NODE_DAC_SPEC_IMPL(0, _node, _prop)
#define __MR_HT_NODE_DAC_SPEC_IMPL(_elem, _node, _prop)                        \
    {                                                                          \
        .drv = MR_HT_DRIVER_FIND(MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 0)),\
        .channel = (mr_uint8_t)MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 1),   \
        .resolution = MR_HT_NODE_PROP_ELEM_OR(_node, _prop, _elem, 2, 8),      \
        .ref_voltage = MR_HT_NODE_PROP_ELEM_OR(_node, _prop, _elem, 3, 0),     \
        .config                                                                \
        = {.ref = MR_HT_NODE_PROP_ELEM_OR(_node, _prop, _elem, 4,              \
                                          MR_DAC_REF_INTERNAL)}                \
    }

/**
 * @brief This macro function iterates over all DAC specification elements
 * in a property array and creates corresponding structures.
 *
 * @param _node The node.
 * @param _prop The property name.
 *
 * @note Property cells: [phandle, channel, resolution, ref_voltage, ref].
 */
#define MR_HT_NODE_DAC_SPEC_FOREACH(_node, _prop)                              \
    MR_HT_NODE_PROP_ELEM_FOREACH(_node, _prop, __MR_HT_NODE_DAC_SPEC_IMPL)

/**
 * @brief This macro function creates a DAC specification structure from
 * hardtree driver properties.
 *
 * @param _id The driver instance id.
 * @param _prop The property name.
 * @return The initialized mr_ht_dac_spec_t structure.
 *
 * @note Property cells: [phandle, channel, resolution, ref_voltage, ref].
 */
#define MR_HT_DAC_SPEC(_id, _prop)                                             \
    MR_HT_NODE_DAC_SPEC(MR_HT_DRIVER(_id), _prop)

/**
 * @brief This macro function iterates over all DAC specification elements in a
 * hardtree driver property array and creates corresponding structures.
 *
 * @param _id The driver instance id.
 * @param _prop The property name.
 *
 * @note Property cells: [phandle, channel, resolution, ref_voltage, ref].
 */
#define MR_HT_DAC_SPEC_FOREACH(_id, _prop)                                     \
    MR_HT_NODE_DAC_SPEC_FOREACH(MR_HT_DRIVER(_id), _prop)
#endif /* defined(MR_USE_DRIVER_DAC) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_DAC_H__ */

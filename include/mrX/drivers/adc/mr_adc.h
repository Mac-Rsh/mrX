/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_DRIVER_ADC_H__
#define __MR_DRIVER_ADC_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_DRIVER_ADC)
#include <mrX/drivers/mr_driver.h>
#endif /* defined(MR_USE_DRIVER_ADC) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup ADC
 * @{
 */

#if defined(MR_USE_DRIVER_ADC)
/* ADC wait event type definition */
#define MR_ADC_WAIT_STREAM_HALF         (1U << 0)
#define MR_ADC_WAIT_STREAM_FULL         (1U << 1)

/* ADC differential mode definition */
typedef enum mr_adc_diff {
    MR_ADC_DIFF_SINGLE,
    MR_ADC_DIFF_DIFF,
} mr_adc_diff_t;

/* ADC gain definition */
typedef enum mr_adc_gain {
    MR_ADC_GAIN_1,
    MR_ADC_GAIN_1_6,
    MR_ADC_GAIN_1_5,
    MR_ADC_GAIN_1_4,
    MR_ADC_GAIN_2_7,
    MR_ADC_GAIN_1_3,
    MR_ADC_GAIN_2_5,
    MR_ADC_GAIN_1_2,
    MR_ADC_GAIN_2_3,
    MR_ADC_GAIN_4_5,
    MR_ADC_GAIN_2,
    MR_ADC_GAIN_4,
    MR_ADC_GAIN_8,
    MR_ADC_GAIN_16,
    MR_ADC_GAIN_32,
    MR_ADC_GAIN_64,
    MR_ADC_GAIN_128,
} mr_adc_gain_t;

/* ADC reference definition */
typedef enum mr_adc_ref {
    MR_ADC_REF_INTERNAL,
    MR_ADC_REF_VDD_1,
    MR_ADC_REF_VDD_2,
    MR_ADC_REF_VDD_3,
    MR_ADC_REF_VDD_4,
    MR_ADC_REF_EXTERNAL_0,
    MR_ADC_REF_EXTERNAL_1,
} mr_adc_ref_t;

/* ADC configuration type */
typedef struct mr_adc_config {
    mr_uint8_t diff: 1;
    mr_uint8_t ref: 3;
    mr_uint8_t gain;
} mr_adc_config_t;

/* ADC sequence type */
typedef struct mr_adc_seq {
    mr_uint32_t channels;
    mr_uint8_t resolution;
    mr_uint8_t oversample;
    void *buf;
    mr_size_t size;
} mr_adc_seq_t;

/* ADC data type */
MR_DRIVER_CLAZZES_EXPORT(adc);
typedef struct mr_adc_data {
    mr_uint16_t ref_internal;
    mr_uint32_t exist_mask;
    mr_waitable_t event;
} mr_adc_data_t;

/* ADC operations type */
typedef struct mr_adc_ops {
    mr_err_t (*channel_configure)(const mr_driver_t *drv, mr_uint32_t channel,
                                  const mr_adc_config_t *config);
    mr_err_t (*channel_configuration)(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      mr_adc_config_t *config);
    mr_ssize_t (*read)(const mr_driver_t *drv, mr_adc_seq_t *seq);
#if defined(MR_USE_DRIVER_ADC_STREAM)
    mr_err_t (*stream_start)(const mr_driver_t *drv, mr_adc_seq_t *seq,
                             mr_bool_t circular);
    mr_err_t (*stream_stop)(const mr_driver_t *drv);
    mr_err_t (*stream_is_busy)(const mr_driver_t *drv, mr_bool_t *busy);
    mr_err_t (*stream_offset)(const mr_driver_t *drv, mr_size_t *offset);
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */
} mr_adc_ops_t;

/**
 * @brief This macro function initializes an ADC data.
 *
 * @param _ref_internal The ADC reference internal.
 * @param _exist_mask The ADC exist mask.
 */
#define MR_ADC_DATA_INIT(_ref_internal, _exist_mask)                           \
    {                                                                          \
        .ref_internal = (_ref_internal),                                       \
        .exist_mask = (_exist_mask),                                           \
    }

/**
 * @brief This macro function defines an ADC driver.
 *
 * @param _name The ADC driver name.
 * @param _data The ADC driver data.
 * @param _priv The ADC private data.
 * @param _ops The ADC operations.
 */
#define MR_ADC_DEFINE(_name, _data, _priv, _ops)                               \
    MR_DRIVER_DEFINE(_name, MR_DRIVER_CLAZZES_STATIK(adc), MR_DRIVER_TYPE_ADC, \
                     _data, _priv, _ops)

/**
 * @brief This macro function installs an ADC driver.
 *
 * @param _name The ADC driver name.
 * @param _data The ADC driver data.
 * @param _priv The ADC private data.
 * @param _ops The ADC operations.
 * @param _level The init level.
 */
#define MR_ADC_INSTALL(_name, _data, _priv, _ops, _level)                      \
    MR_DRIVER_INSTALL(_name, MR_DRIVER_CLAZZES_STATIK(adc),                    \
                      MR_DRIVER_TYPE_ADC, _data, _priv, _ops, _level)

/**
 * @brief This function initializes an ADC driver.
 *
 * @param drv The ADC driver.
 * @param data The ADC driver data.
 * @param priv The ADC private data.
 * @param ops The ADC operations.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_adc_init(mr_driver_t *drv, mr_adc_data_t *data,
                               void *priv, mr_adc_ops_t *ops) {
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_STATIK(adc);

    /* Check parameter */
    MR_ASSERT(drv != MR_NULL);
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Init ADC driver */
    return mr_driver_init(drv, dclazz, MR_DRIVER_TYPE_ADC, data, priv, ops);
}

/**
 * @brief This function creates an ADC driver.
 *
 * @param data The ADC driver data.
 * @param priv The ADC private data.
 * @param ops The ADC operations.
 * @return The ADC driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_adc_create(mr_adc_data_t *data, void *priv,
                                     mr_adc_ops_t *ops) {
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_DYNAMIC(adc);

    /* Check parameter */
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Create ADC driver */
    return mr_driver_create(dclazz, MR_DRIVER_TYPE_ADC, data, priv, ops);
}

/**
 * @brief This function deletes an ADC driver.
 *
 * @param drv The ADC driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_adc_del(mr_driver_t *drv) {
    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ADC);

    /* Delete driver */
    return mr_driver_del(drv);
}

/**
 * @brief This function configures an ADC channel.
 *
 * @param drv The ADC driver.
 * @param channel The ADC channel.
 * @param config The ADC channel configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_adc_configure(mr_driver_t *drv, mr_uint32_t channel,
                          mr_adc_config_t *config);

/**
 * @brief This function gets an ADC channel configuration.
 *
 * @param drv The ADC driver.
 * @param channel The ADC channel.
 * @param config The ADC channel configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_adc_configuration(mr_driver_t *drv, mr_uint32_t channel,
                              mr_adc_config_t *config);

/**
 * @brief This function reads an ADC sequence.
 *
 * @param drv The ADC driver.
 * @param seq The ADC sequence.
 * @return The actual size of read data on success, a negative error code
 * on failure.
 */
MR_INLINE mr_ssize_t mr_adc_read(mr_driver_t *drv, mr_adc_seq_t *seq) {
    const mr_adc_ops_t *ad_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ADC);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(seq != MR_NULL);
    MR_ASSERT((seq->resolution > 0U) && (seq->resolution <= 32U));
    MR_ASSERT((seq->buf != MR_NULL) || (seq->size == 0));

    /* Get ADC operations */
    ad_ops = (mr_adc_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(ad_ops->read != MR_NULL);

    /* Read ADC sequence */
    return ad_ops->read(drv, seq);
}

#if defined(MR_USE_DRIVER_ADC_STREAM)
/**
 * @brief This function starts an ADC stream.
 *
 * @param drv The ADC driver.
 * @param seq The ADC sequence.
 * @param circular The circular mode.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_adc_stream_start(mr_driver_t *drv, mr_adc_seq_t *seq,
                                       mr_bool_t circular) {
    const mr_adc_ops_t *ad_ops;
    mr_bool_t busy;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ADC);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(seq != MR_NULL);
    MR_ASSERT((seq->resolution > 0U) && (seq->resolution <= 32U));
    MR_ASSERT((seq->buf != MR_NULL) || (seq->size == 0));

    /* Get ADC operations */
    ad_ops = (mr_adc_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if ADC stream is busy */
    if (ad_ops->stream_is_busy && ad_ops->stream_start) {
        ret = ad_ops->stream_is_busy(drv, &busy);
        if (ret != 0) {
            return ret;
        }
        if (busy) {
            return -MR_EBUSY;
        }
    } else {
        /* Not supported */
        return -MR_ENOSYS;
    }

    /* Start ADC stream */
    return ad_ops->stream_start(drv, seq, circular);
}

/**
 * @brief This function checks if an ADC stream is busy.
 *
 * @param drv The ADC driver.
 * @return MR_TRUE if the stream is busy, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_adc_stream_is_busy(mr_driver_t *drv) {
    const mr_adc_ops_t *ad_ops;
    mr_bool_t busy;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ADC);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get ADC operations */
    ad_ops = (mr_adc_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if operation is supported */
    if (!ad_ops->stream_is_busy) {
        return MR_FALSE;
    }

    /* Get stream busy status */
    ret = ad_ops->stream_is_busy(drv, &busy);
    if (ret != 0) {
        return MR_FALSE;
    }
    return busy;
}

/**
 * @brief This function stops an ADC stream.
 *
 * @param drv The ADC driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_adc_stream_stop(mr_driver_t *drv) {
    const mr_adc_ops_t *ad_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ADC);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get ADC operations */
    ad_ops = (mr_adc_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if operation is supported */
    if (!ad_ops->stream_stop) {
        return -MR_ENOSYS;
    }

    /* Stop ADC stream */
    return ad_ops->stream_stop(drv);
}

/**
 * @brief This function gets the ADC stream offset.
 *
 * @param drv The ADC driver.
 * @param offset The ADC stream offset.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_adc_stream_offset(mr_driver_t *drv, mr_size_t *offset) {
    const mr_adc_ops_t *ad_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ADC);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(offset != MR_NULL);

    /* Get ADC operations */
    ad_ops = (mr_adc_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if operation is supported */
    if (!ad_ops->stream_offset) {
        return -MR_ENOSYS;
    }

    /* Get ADC stream offset */
    return ad_ops->stream_offset(drv, offset);
}
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */

/**
 * @brief This function attaches an ADC event.
 *
 * @param drv The ADC driver.
 * @param entry The ADC event entry.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_adc_event_attach(mr_driver_t *drv, mr_waiter_t *entry) {
    mr_adc_data_t *ad_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ADC);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(entry != MR_NULL);

    /* Get ADC data */
    ad_data = (mr_adc_data_t *)MR_DRIVER_DATA(drv);

    /* Attach ADC event */
    return mr_waiter_wait(entry, &ad_data->event);
}

/**
 * @brief This function raises ADC events.
 *
 * @param drv The ADC driver.
 * @param events The ADC events.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_adc_event_raise(mr_driver_t *drv,
                                      mr_wait_event_t events) {
    mr_adc_data_t *ad_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ADC);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get ADC data */
    ad_data = (mr_adc_data_t *)MR_DRIVER_DATA(drv);

    /* Raise ADC events */
    return mr_waitable_raise(&ad_data->event, events, MR_NULL);
}

/**
 * @brief This function gets the ADC internal reference value.
 *
 * @param drv The ADC driver.
 * @return The ADC reference internal value.
 */
MR_INLINE mr_uint16_t mr_adc_ref_internal(mr_driver_t *drv) {
    mr_adc_data_t *ad_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ADC);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get ADC data */
    ad_data = (mr_adc_data_t *)MR_DRIVER_DATA(drv);

    /* Get ADC reference internal value */
    return ad_data->ref_internal;
}

/**
 * @brief This function converts an ADC raw value to millivolts.
 *
 * @param ref_mv The reference millivolt value.
 * @param gain The ADC gain.
 * @param resolution The ADC resolution in bits.
 * @param raw The ADC raw value.
 * @return The millivolt value.
 */
MR_INLINE mr_int64_t mr_adc_raw2millivolt(mr_int32_t ref_mv, mr_adc_gain_t gain,
                                          mr_uint8_t resolution,
                                          mr_int64_t raw) {
    struct mr_adc_gain_md {
        mr_uint8_t mul;
        mr_uint8_t div;
    } gains[] = {
        [MR_ADC_GAIN_1_6] = {  .mul = 1, .div = 6},
        [MR_ADC_GAIN_1_5] = {  .mul = 1, .div = 5},
        [MR_ADC_GAIN_1_4] = {  .mul = 1, .div = 4},
        [MR_ADC_GAIN_2_7] = {  .mul = 2, .div = 7},
        [MR_ADC_GAIN_1_3] = {  .mul = 1, .div = 3},
        [MR_ADC_GAIN_2_5] = {  .mul = 2, .div = 5},
        [MR_ADC_GAIN_1_2] = {  .mul = 1, .div = 2},
        [MR_ADC_GAIN_2_3] = {  .mul = 2, .div = 3},
        [MR_ADC_GAIN_4_5] = {  .mul = 4, .div = 5},
        [MR_ADC_GAIN_1] = {  .mul = 1, .div = 1},
        [MR_ADC_GAIN_2] = {  .mul = 2, .div = 1},
        [MR_ADC_GAIN_4] = {  .mul = 4, .div = 1},
        [MR_ADC_GAIN_8] = {  .mul = 8, .div = 1},
        [MR_ADC_GAIN_16] = { .mul = 16, .div = 1},
        [MR_ADC_GAIN_32] = { .mul = 32, .div = 1},
        [MR_ADC_GAIN_64] = { .mul = 64, .div = 1},
        [MR_ADC_GAIN_128] = {.mul = 128, .div = 1},
    };
    mr_int64_t numerator, denominator;

    /* Check parameter */
    MR_ASSERT((resolution > 0U) && (resolution <= 32U));
    MR_ASSERT(gain < MR_ARRAY_SIZE(gains));

    /* Calculate millivolt */
    numerator = ((mr_int64_t)ref_mv * raw) * gains[gain].div;
    denominator = (mr_int64_t)gains[gain].mul << resolution;
    return numerator / denominator;
}

/**
 * @brief This function converts an ADC raw value to microvolts.
 *
 * @param ref_mv The reference millivolt value.
 * @param gain The ADC gain.
 * @param resolution The ADC resolution in bits.
 * @param raw The ADC raw value.
 * @return The microvolt value.
 */
MR_INLINE mr_int64_t mr_adc_raw2microvolt(mr_int32_t ref_mv, mr_adc_gain_t gain,
                                          mr_uint8_t resolution,
                                          mr_int64_t raw) {
    /* Convert to microvolt */
    return mr_adc_raw2millivolt(ref_mv, gain, resolution, raw * 1000LL);
}

/**
 * @brief This function converts an ADC raw value to volts.
 *
 * @param ref_mv The reference millivolt value.
 * @param gain The ADC gain.
 * @param resolution The ADC resolution in bits.
 * @param raw The ADC raw value.
 * @return The volt value.
 */
MR_INLINE mr_f32_t mr_adc_raw2volt(mr_int32_t ref_mv, mr_adc_gain_t gain,
                                   mr_uint8_t resolution, mr_int64_t raw) {
    mr_f32_t microvolt;

    /* Convert to volt */
    microvolt = (mr_f32_t)mr_adc_raw2microvolt(ref_mv, gain, resolution, raw);
    return microvolt / 1000000.0f;
}

/**
 * @brief This function gets an ADC driver.
 *
 * @param drv The ADC driver.
 * @return The ADC driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_adc_get(mr_driver_t *drv) {
    /* Get driver reference */
    return mr_driver_get(drv);
}

/**
 * @brief This function puts an ADC driver.
 *
 * @param drv The ADC driver.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_adc_put(mr_driver_t *drv) {
    /* Put driver reference */
    return mr_driver_put(drv);
}

/* ================================ HardTree ================================ */

/**
 * @brief This macro function installs an ADC instance through hardtree.
 *
 * @param _id The instance id.
 * @param _priv The ADC private data.
 * @param _ops The ADC operations.
 */
#define MR_HT_ADC_INSTALL(_id, _priv, _ops)                                    \
    static mr_adc_data_t MR_CAT(__mr_adc_data_, _id) = MR_ADC_DATA_INIT(       \
        MR_HT_DRIVER_PROP_OR(_id, ref_internal, 3300),                         \
        MR_HT_DRIVER_PROP_OR(_id, exist_mask, 0xffffffffU));                   \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(adc),                   \
                         MR_DRIVER_TYPE_ADC, &MR_CAT(__mr_adc_data_, _id),     \
                         _priv, _ops)

/* ADC hardtree specification type */
typedef struct mr_ht_adc_spec {
    mr_driver_t *drv;
    mr_uint8_t channel;
    mr_uint8_t resolution;
    mr_int32_t ref_voltage;
    mr_adc_config_t config;
} mr_ht_adc_spec_t;

/**
 * @brief This function configures an ADC specification.
 *
 * @param spec The ADC specification.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_adc_configure(mr_ht_adc_spec_t *spec) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Configure ADC channel */
    return mr_adc_configure(spec->drv, spec->channel, &spec->config);
}

/**
 * @brief This function reads an ADC specification sequence.
 *
 * @param spec The ADC specification.
 * @param seq The ADC sequence.
 * @return The actual size of read data on success, a negative error code
 * on failure.
 */
MR_INLINE mr_ssize_t mr_ht_adc_read(mr_ht_adc_spec_t *spec, mr_adc_seq_t *seq) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Read ADC sequence */
    return mr_adc_read(spec->drv, seq);
}

#if defined(MR_USE_DRIVER_ADC_STREAM)
/**
 * @brief This function starts an ADC specification stream.
 *
 * @param spec The ADC specification.
 * @param seq The ADC sequence.
 * @param circular The circular mode.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_adc_stream_start(mr_ht_adc_spec_t *spec,
                                          mr_adc_seq_t *seq,
                                          mr_bool_t circular) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Start ADC stream */
    return mr_adc_stream_start(spec->drv, seq, circular);
}

/**
 * @brief This function checks if an ADC specification stream is busy.
 *
 * @param spec The ADC specification.
 * @return MR_TRUE if the stream is busy, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_ht_adc_stream_is_busy(mr_ht_adc_spec_t *spec) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Check stream busy status */
    return mr_adc_stream_is_busy(spec->drv);
}

/**
 * @brief This function stops an ADC specification stream.
 *
 * @param spec The ADC specification.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_adc_stream_stop(mr_ht_adc_spec_t *spec) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Stop ADC stream */
    return mr_adc_stream_stop(spec->drv);
}

/**
 * @brief This function gets the ADC specification stream offset.
 *
 * @param spec The ADC specification.
 * @param offset The ADC stream offset.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_adc_stream_offset(mr_ht_adc_spec_t *spec,
                                           mr_size_t *offset) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Get ADC stream offset */
    return mr_adc_stream_offset(spec->drv, offset);
}
#endif /* defined(MR_USE_DRIVER_ADC_STREAM) */

/**
 * @brief This function converts an ADC specification raw value to millivolts.
 *
 * @param spec The ADC specification.
 * @param raw The ADC raw value.
 * @return The millivolt value.
 */
MR_INLINE mr_int64_t mr_ht_adc_raw2millivolt(mr_ht_adc_spec_t *spec,
                                             mr_int64_t raw) {
    mr_int32_t ref;

    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Get reference voltage */
    if (spec->ref_voltage == 0) {
        ref = mr_adc_ref_internal(spec->drv);
    } else {
        ref = spec->ref_voltage;
    }

    /* Convert to millivolt */
    return mr_adc_raw2millivolt(ref, spec->config.gain, spec->resolution, raw);
}

/**
 * @brief This function converts an ADC specification raw value to microvolts.
 *
 * @param spec The ADC specification.
 * @param raw The ADC raw value.
 * @return The microvolt value.
 */
MR_INLINE mr_int64_t mr_ht_adc_raw2microvolt(mr_ht_adc_spec_t *spec,
                                             mr_int64_t raw) {
    mr_int32_t ref;

    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Get reference voltage */
    if (spec->ref_voltage == 0) {
        ref = mr_adc_ref_internal(spec->drv);
    } else {
        ref = spec->ref_voltage;
    }

    /* Convert to microvolt */
    return mr_adc_raw2microvolt(ref, spec->config.gain, spec->resolution, raw);
}

/**
 * @brief This function converts an ADC specification raw value to volts.
 *
 * @param spec The ADC specification.
 * @param raw The ADC raw value.
 * @return The volt value.
 */
MR_INLINE mr_f32_t mr_ht_adc_raw2volt(mr_ht_adc_spec_t *spec, mr_int64_t raw) {
    mr_int32_t ref;

    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Get reference voltage */
    if (spec->ref_voltage == 0) {
        ref = mr_adc_ref_internal(spec->drv);
    } else {
        ref = spec->ref_voltage;
    }

    /* Convert to volt */
    return mr_adc_raw2volt(ref, spec->config.gain, spec->resolution, raw);
}

/**
 * @brief This macro function creates an ADC specification structure from
 * hardtree properties.
 *
 * @param _node The node.
 * @param _prop The property name.
 * @return The initialized mr_ht_adc_spec_t structure.
 *
 * @note Property cells: [phandle, channel, resolution, ref_voltage, diff, ref,
 * gain].
 */
#define MR_HT_NODE_ADC_SPEC(_node, _prop)                                      \
    __MR_HT_NODE_ADC_SPEC_IMPL(0, _node, _prop)
#define __MR_HT_NODE_ADC_SPEC_IMPL(_elem, _node, _prop)                        \
    {                                                                          \
        .drv = MR_HT_DRIVER_FIND(MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 0)),\
        .channel = (mr_uint8_t)MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 1),   \
        .resolution = MR_HT_NODE_PROP_ELEM_OR(_node, _prop, _elem, 2, 8),      \
        .ref_voltage = MR_HT_NODE_PROP_ELEM_OR(_node, _prop, _elem, 3, 0),     \
        .config                                                                \
        = {.diff = MR_HT_NODE_PROP_ELEM_OR(_node, _prop, _elem, 4,             \
                                           MR_ADC_DIFF_SINGLE),                \
           .ref = MR_HT_NODE_PROP_ELEM_OR(_node, _prop, _elem, 5,              \
                                          MR_ADC_REF_INTERNAL),                \
           .gain = MR_HT_NODE_PROP_ELEM_OR(_node, _prop, _elem, 6,             \
                                           MR_ADC_GAIN_1) }                    \
    }

/**
 * @brief This macro function iterates over all ADC specification elements
 * in a property array and creates corresponding structures.
 *
 * @param _node The node.
 * @param _prop The property name.
 *
 * @note Property cells: [phandle, channel, resolution, ref_voltage, diff, ref,
 * gain].
 */
#define MR_HT_NODE_ADC_SPEC_FOREACH(_node, _prop)                              \
    MR_HT_NODE_PROP_ELEM_FOREACH(_node, _prop, __MR_HT_NODE_ADC_SPEC_IMPL)

/**
 * @brief This macro function creates an ADC specification structure from
 * hardtree driver properties.
 *
 * @param _id The driver instance id.
 * @param _prop The property name.
 * @return The initialized mr_ht_adc_spec_t structure.
 *
 * @note Property cells: [phandle, channel, resolution, ref_voltage, diff, ref,
 * gain].
 */
#define MR_HT_ADC_SPEC(_id, _prop)                                             \
    MR_HT_NODE_ADC_SPEC(MR_HT_DRIVER(_id), _prop)

/**
 * @brief This macro function iterates over all ADC specification elements in a
 * hardtree driver property array and creates corresponding structures.
 *
 * @param _id The driver instance id.
 * @param _prop The property name.
 *
 * @note Property cells: [phandle, channel, resolution, ref_voltage, diff, ref,
 * gain].
 */
#define MR_HT_ADC_SPEC_FOREACH(_id, _prop)                                     \
    MR_HT_NODE_ADC_SPEC_FOREACH(MR_HT_DRIVER(_id), _prop)
#endif /* defined(MR_USE_DRIVER_ADC) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_ADC_H__ */

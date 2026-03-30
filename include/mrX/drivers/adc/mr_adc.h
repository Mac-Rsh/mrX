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
#define MR_ADC_WAIT_CONV                (MR_WAIT_IN)
#define MR_ADC_WAIT_CONV_COMPLETE       (MR_WAIT_IN | (1ULL << 0))

/* ADC differential mode definition */
typedef enum mr_adc_diff {
    MR_ADC_DIFF_SINGLE,
    MR_ADC_DIFF_DIFF,
} mr_adc_diff_t;

/* ADC gain definition */
typedef enum mr_adc_gain {
    MR_ADC_GAIN_1_6,
    MR_ADC_GAIN_1_5,
    MR_ADC_GAIN_1_4,
    MR_ADC_GAIN_2_7,
    MR_ADC_GAIN_1_3,
    MR_ADC_GAIN_2_5,
    MR_ADC_GAIN_1_2,
    MR_ADC_GAIN_2_3,
    MR_ADC_GAIN_4_5,
    MR_ADC_GAIN_1,
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
    MR_ADC_REF_VDD_1,
    MR_ADC_REF_VDD_2,
    MR_ADC_REF_VDD_3,
    MR_ADC_REF_VDD_4,
    MR_ADC_REF_INTERNAL,
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
    mr_size_t size;
    void *buf;
} mr_adc_seq_t;

/* ADC data type */
MR_DRIVER_CLAZZES_EXPORT(adc);
typedef struct mr_adc_data {
    mr_uint16_t ref_internal;
    mr_uint32_t exist_mask;
    mr_waitable_t irq;
} mr_adc_data_t;

/* ADC operations type */
typedef struct mr_adc_ops {
    mr_err_t (*channel_configure)(const mr_driver_t *drv, mr_uint32_t channel,
                                  const mr_adc_config_t *config);
    mr_err_t (*channel_configuration)(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      mr_adc_config_t *config);
    mr_ssize_t (*conv)(const mr_driver_t *drv, mr_adc_seq_t *seq);
#if defined(MR_USE_DRIVER_ADC_ASYNC)
    mr_err_t (*conv_async_control)(const mr_driver_t *drv, mr_bool_t enable,
                                   mr_bool_t circular, mr_adc_seq_t *seq);
    mr_err_t (*conv_async_is_busy)(const mr_driver_t *drv, mr_bool_t *busy);
    mr_err_t (*conv_async_offset)(const mr_driver_t *drv, mr_size_t *offset);
#endif /* defined(MR_USE_DRIVER_ADC_ASYNC) */
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
    MR_DRIVER_DEFINE(_name,                                                    \
                     MR_DRIVER_CLAZZES_STATIK(MR_DRIVER_CLAZZES_FIND(adc)),    \
                     MR_DRIVER_TYPE_ADC, _data, _priv, _ops)

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
    MR_DRIVER_INSTALL(_name,                                                   \
                      MR_DRIVER_CLAZZES_STATIK(MR_DRIVER_CLAZZES_FIND(adc)),   \
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
    const mr_driver_clazzes_t *dclazzes = MR_DRIVER_CLAZZES_FIND(adc);
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_STATIK(dclazzes);

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
    const mr_driver_clazzes_t *dclazzes = MR_DRIVER_CLAZZES_FIND(adc);
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_DYNAMIC(dclazzes);

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
 * @brief This function converts an ADC sequence.
 *
 * @param drv The ADC driver.
 * @param seq The ADC sequence.
 * @return The actual size of converted data on success, a negative error code
 * on failure.
 */
MR_INLINE mr_ssize_t mr_adc_conv(mr_driver_t *drv, mr_adc_seq_t *seq) {
    const mr_adc_ops_t *ad_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ADC);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(seq != MR_NULL);

    /* Get ADC operations */
    ad_ops = (mr_adc_ops_t *)MR_DRIVER_OPS(drv);

    /* Convert ADC sequence */
    return ad_ops->conv(drv, seq);
}

/**
 * @brief This function reads an ADC channel.
 *
 * @param drv The ADC driver.
 * @param channel The ADC channel.
 * @param resolution The ADC resolution in bits.
 * @param buf The ADC buffer.
 * @param size The ADC buffer size.
 * @return The actual size of converted data on success, a negative error code
 * on failure.
 */
MR_INLINE mr_ssize_t mr_adc_read(mr_driver_t *drv, mr_uint32_t channel,
                                 mr_uint8_t resolution, void *buf,
                                 mr_uint32_t size) {
    mr_adc_seq_t seq;

    /* Check parameter */
    MR_ASSERT((resolution > 0) && (resolution < 32));
    MR_ASSERT(channel < 32);

    /* Convert ADC sequence as a single channel */
    seq.channels = 1U << channel;
    seq.resolution = resolution;
    seq.oversample = 0;
    seq.size = size;
    seq.buf = buf;

    /* Convert ADC sequence */
    return mr_adc_conv(drv, &seq);
}

#if defined(MR_USE_DRIVER_ADC_ASYNC)
/**
 * @brief This function converts an ADC sequence asynchronously.
 *
 * @param drv The ADC driver.
 * @param seq The ADC sequence.
 * @param circular The circular mode.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_adc_conv_async(mr_driver_t *drv, mr_adc_seq_t *seq,
                                     mr_bool_t circular) {
    const mr_adc_ops_t *ad_ops;
    mr_bool_t busy;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ADC);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(seq != MR_NULL);

    /* Get ADC operations */
    ad_ops = (mr_adc_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if ADC conv async is busy */
    if (ad_ops->conv_async_is_busy) {
        ret = ad_ops->conv_async_is_busy(drv, &busy);
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

    /* Start ADC async conversion */
    return ad_ops->conv_async_control(drv, MR_TRUE, circular, seq);
}

/**
 * @brief This function checks if an ADC conversion is busy.
 *
 * @param drv The ADC driver.
 * @return MR_TRUE if the conversion is busy, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_adc_conv_is_busy(mr_driver_t *drv) {
    const mr_adc_ops_t *ad_ops;
    mr_bool_t busy;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ADC);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get ADC operations */
    ad_ops = (mr_adc_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if operation is supported */
    if (!ad_ops->conv_async_is_busy) {
        return MR_FALSE;
    }

    /* Get conv busy status */
    ret = ad_ops->conv_async_is_busy(drv, &busy);
    if (ret != 0) {
        return MR_FALSE;
    }
    return busy;
}

/**
 * @brief This function aborts an ADC conversion.
 *
 * @param drv The ADC driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_adc_conv_abort(mr_driver_t *drv) {
    const mr_adc_ops_t *ad_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ADC);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get ADC operations */
    ad_ops = (mr_adc_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if operation is supported */
    if (!ad_ops->conv_async_control) {
        return -MR_ENOSYS;
    }

    /* Abort ADC async conversion */
    return ad_ops->conv_async_control(drv, MR_FALSE, MR_FALSE, MR_NULL);
}

/**
 * @brief This function gets the ADC conversion offset.
 *
 * @param drv The ADC driver.
 * @param offset The ADC conv async offset.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_adc_conv_offset(mr_driver_t *drv, mr_size_t *offset) {
    const mr_adc_ops_t *ad_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ADC);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(offset != MR_NULL);

    /* Get ADC operations */
    ad_ops = (mr_adc_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if operation is supported */
    if (!ad_ops->conv_async_offset) {
        return -MR_ENOSYS;
    }

    /* Get ADC conv async offset */
    return ad_ops->conv_async_offset(drv, offset);
}
#endif /* defined(MR_USE_DRIVER_ADC_ASYNC) */

/**
 * @brief This function attaches an ADC interrupt.
 *
 * @param drv The ADC driver.
 * @param entry The ADC interrupt entry.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_adc_irq_attach(mr_driver_t *drv, mr_waiter_t *entry) {
    mr_adc_data_t *ad_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ADC);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(entry != MR_NULL);

    /* Get ADC data */
    ad_data = (mr_adc_data_t *)MR_DRIVER_DATA(drv);

    /* Attach ADC irq */
    return mr_waiter_wait(entry, &ad_data->irq);
}

/**
 * @brief This function handles an ADC interrupt.
 *
 * @param drv The ADC driver.
 * @param events The interrupt events.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_adc_irq_handle(mr_driver_t *drv, mr_uint64_t events) {
    mr_adc_data_t *ad_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_ADC);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    /* Get ADC data */
    ad_data = (mr_adc_data_t *)MR_DRIVER_DATA(drv);

    /* Raise ADC irq events */
    return mr_waitable_raise(&ad_data->irq, events);
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
    mr_int64_t millivolt;

    /* Check parameter */
    MR_ASSERT((resolution > 0) && (resolution < 32));
    MR_ASSERT(gain < MR_ARRAY_SIZE(gains));

    /* Calculate millivolt */
    millivolt = ((mr_int64_t)ref_mv * raw) * gains[gain].mul / gains[gain].div;
    return millivolt >> resolution;
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
#define MR_HT_INST_ADC_INSTALL(_id, _priv, _ops)                               \
    static mr_adc_data_t MR_CAT(__mr_adc_data_, _id) = MR_ADC_DATA_INIT(       \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), ref_internal, 3300),        \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), exist_mask, 0xffffffff));   \
    MR_HT_INST_DRIVER_INSTALL(                                                 \
        _id, MR_DRIVER_CLAZZES_STATIK(MR_DRIVER_CLAZZES_FIND(adc)),            \
        MR_DRIVER_TYPE_ADC, &MR_CAT(__mr_adc_data_, _id), _priv, _ops)

/* ADC hardtree specification type */
typedef struct mr_ht_adc_spec {
    mr_driver_t *drv;
    mr_uint8_t channel;
    mr_adc_config_t config;
    mr_uint8_t resolution;
    mr_uint8_t oversample;
    mr_int32_t ref_mv;
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
 * @brief This function converts an ADC specification sequence.
 *
 * @param spec The ADC specification.
 * @param seq The ADC sequence.
 * @return The actual size of converted data on success, a negative error code
 * on failure.
 */
MR_INLINE mr_ssize_t mr_ht_adc_conv(mr_ht_adc_spec_t *spec, mr_adc_seq_t *seq) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Convert ADC sequence */
    return mr_adc_conv(spec->drv, seq);
}

/**
 * @brief This function reads an ADC specification.
 *
 * @param spec The ADC specification.
 * @param buf The ADC buffer.
 * @param size The ADC buffer size.
 * @return The actual size of converted data on success, a negative error code
 * on failure.
 */
MR_INLINE mr_ssize_t mr_ht_adc_read(mr_ht_adc_spec_t *spec, void *buf,
                                    mr_uint32_t size) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Read ADC data */
    return mr_adc_read(spec->drv, spec->channel, spec->resolution, buf, size);
}

#if defined(MR_USE_DRIVER_ADC_ASYNC)
/**
 * @brief This function converts an ADC specification sequence asynchronously.
 *
 * @param spec The ADC specification.
 * @param seq The ADC sequence.
 * @param circular The circular mode.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_adc_conv_async(mr_ht_adc_spec_t *spec,
                                        mr_adc_seq_t *seq, mr_bool_t circular) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Start ADC async sequence */
    return mr_adc_conv_async(spec->drv, seq, circular);
}

/**
 * @brief This function checks if an ADC specification conversion is busy.
 *
 * @param spec The ADC specification.
 * @return MR_TRUE if the conversion is busy, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_ht_adc_conv_is_busy(mr_ht_adc_spec_t *spec) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Check conv busy status */
    return mr_adc_conv_is_busy(spec->drv);
}

/**
 * @brief This function aborts an ADC specification conversion.
 *
 * @param spec The ADC specification.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_adc_conv_abort(mr_ht_adc_spec_t *spec) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Abort ADC conv */
    return mr_adc_conv_abort(spec->drv);
}

/**
 * @brief This function gets the ADC specification conversion offset.
 *
 * @param spec The ADC specification.
 * @param offset The ADC conv async offset.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_adc_conv_offset(mr_ht_adc_spec_t *spec,
                                         mr_size_t *offset) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Get ADC conv async offset */
    return mr_adc_conv_offset(spec->drv, offset);
}
#endif /* defined(MR_USE_DRIVER_ADC_ASYNC) */

/**
 * @brief This function converts an ADC specification raw value to millivolts.
 *
 * @param spec The ADC specification.
 * @param raw The ADC raw value.
 * @return The millivolt value.
 */
MR_INLINE mr_int64_t mr_ht_adc_raw2millivolt(mr_ht_adc_spec_t *spec,
                                             mr_int64_t raw) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Convert to millivolt */
    return mr_adc_raw2millivolt(spec->ref_mv, spec->config.gain,
                                spec->resolution, raw);
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
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Convert to microvolt */
    return mr_adc_raw2microvolt(spec->ref_mv, spec->config.gain,
                                spec->resolution, raw);
}

/**
 * @brief This function converts an ADC specification raw value to volts.
 *
 * @param spec The ADC specification.
 * @param raw The ADC raw value.
 * @return The volt value.
 */
MR_INLINE mr_f32_t mr_ht_adc_raw2volt(mr_ht_adc_spec_t *spec, mr_int64_t raw) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Convert to volt */
    return mr_adc_raw2volt(spec->ref_mv, spec->config.gain, spec->resolution,
                           raw);
}

/**
 * @brief This macro function creates an ADC specification structure from
 *        hardtree properties.
 *
 * @param _node The node.
 * @param _prop The property name.
 * @return The initialized mr_ht_adc_spec_t structure.
 *
 * @note Property cells: [phandle, channel, diff, ref, gain, resolution,
 * oversample, ref_mv].
 */
#define MR_HT_ADC_SPEC(_node, _prop)    __MR_HT_ADC_SPEC_IMPL(0, _node, _prop)
#define __MR_HT_ADC_SPEC_IMPL(_elem, _node, _prop)                             \
    {                                                                          \
        .drv                                                                   \
        = MR_HT_DRIVER_FIND(MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 0)),     \
        .channel = (mr_uint8_t)MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 1),   \
        .config                                                                \
        = {.diff = MR_HT_NODE_PROP_ELEM_OR(_node, _prop, _elem, 2,             \
                                           MR_ADC_DIFF_SINGLE),                \
           .ref = MR_HT_NODE_PROP_ELEM_OR(_node, _prop, _elem, 3,              \
                                          MR_ADC_REF_INTERNAL),                \
           .gain = MR_HT_NODE_PROP_ELEM_OR(_node, _prop, _elem, 4,             \
                                           MR_ADC_GAIN_1),},                   \
        .resolution = MR_HT_NODE_PROP_ELEM_OR(_node, _prop, _elem, 5, 8),      \
        .oversample = MR_HT_NODE_PROP_ELEM_OR(_node, _prop, _elem, 6, 0),      \
        .ref_mv = MR_HT_NODE_PROP_ELEM_OR(_node, _prop, _elem, 7, 3300),       \
    }

/**
 * @brief This macro function iterates over all ADC specification elements
 *        in a property array and creates corresponding structures.
 *
 * @param _node The node.
 * @param _prop The property name.
 *
 * @note Property cells: [phandle, channel, diff, ref, gain, resolution,
 * oversample, ref_mv].
 */
#define MR_HT_ADC_SPEC_FOREACH(_node, _prop)                                   \
    MR_HT_NODE_PROP_ELEM_FOREACH(_node, _prop, __MR_HT_ADC_SPEC_IMPL)
#endif /* defined(MR_USE_DRIVER_ADC) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_ADC_H__ */

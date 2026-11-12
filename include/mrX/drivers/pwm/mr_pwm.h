/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_DRIVER_PWM_H__
#define __MR_DRIVER_PWM_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_DRIVER_PWM)
#include <mrX/drivers/mr_driver.h>
#endif /* defined(MR_USE_DRIVER_PWM) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Drivers-PWM
 * @{
 */

#if defined(MR_USE_DRIVER_PWM)
/* PWM wait event definition */
#define MR_PWM_WAIT_PERIOD              (1U << 0)
#define MR_PWM_WAIT_FAULT               (1U << 1)

/* PWM alignment definition */
typedef enum mr_pwm_alignment {
    MR_PWM_ALIGN_EDGE_UP,
    MR_PWM_ALIGN_EDGE_DOWN,
    MR_PWM_ALIGN_CENTER,
} mr_pwm_alignment_t;

/* PWM update mode definition */
typedef enum mr_pwm_update_mode {
    MR_PWM_UPDATE_IMMEDIATE,
    MR_PWM_UPDATE_PERIOD,
} mr_pwm_update_mode_t;

/* PWM output polarity definition */
typedef enum mr_pwm_polarity {
    MR_PWM_POLARITY_ACTIVE_HIGH,
    MR_PWM_POLARITY_ACTIVE_LOW,
} mr_pwm_polarity_t;

/* PWM idle state definition */
typedef enum mr_pwm_idle_state {
    MR_PWM_IDLE_INACTIVE,
    MR_PWM_IDLE_ACTIVE,
    MR_PWM_IDLE_HIGH_Z,
} mr_pwm_idle_state_t;

/* PWM output state definition */
typedef enum mr_pwm_output_state {
    MR_PWM_OUTPUT_PWM,
    MR_PWM_OUTPUT_INACTIVE,
    MR_PWM_OUTPUT_ACTIVE,
    MR_PWM_OUTPUT_HIGH_Z,
} mr_pwm_output_state_t;

/* PWM fault mode definition */
typedef enum mr_pwm_fault_mode {
    MR_PWM_FAULT_CYCLE_BY_CYCLE,
    MR_PWM_FAULT_LATCHED,
} mr_pwm_fault_mode_t;

/* PWM signal definition */
typedef enum mr_pwm_signal {
    MR_PWM_SIGNAL_PERIOD,
    MR_PWM_SIGNAL_COMPARE,
} mr_pwm_signal_t;

/* PWM controller configuration type */
typedef struct mr_pwm_config {
    mr_uint64_t period;
    mr_uint32_t cycles;
    mr_uint8_t alignment: 2;
    mr_uint8_t update_mode: 1;
} mr_pwm_config_t;

/* PWM channel configuration type */
typedef struct mr_pwm_channel_config {
    mr_uint64_t pulse;
    mr_uint64_t phase;
    mr_uint64_t dead_rise;
    mr_uint64_t dead_fall;
    mr_uint8_t polarity: 1;
    mr_uint8_t comp_polarity: 1;
    mr_uint8_t idle: 2;
    mr_uint8_t comp_idle: 2;
    mr_uint8_t comp: 1;
} mr_pwm_channel_config_t;

/* PWM channel update type */
typedef struct mr_pwm_update {
    mr_uint32_t channel;
    mr_uint64_t pulse;
    mr_uint64_t phase;
    mr_uint8_t state: 2;
    mr_uint8_t comp_state: 2;
} mr_pwm_update_t;

/* PWM fault configuration type */
typedef struct mr_pwm_fault_config {
    mr_uint64_t filter;
    mr_uint8_t polarity: 1;
    mr_uint8_t mode: 1;
    mr_uint8_t enable: 1;
} mr_pwm_fault_config_t;

/* PWM signal configuration type */
typedef struct mr_pwm_signal_config {
    mr_uint64_t offset;
} mr_pwm_signal_config_t;

/* PWM state type */
typedef struct mr_pwm_state {
    mr_uint32_t running_mask;
    mr_uint32_t fault_mask;
} mr_pwm_state_t;

/* PWM event data type */
typedef struct mr_pwm_event_data {
    mr_uint32_t channel_mask;
    mr_uint32_t fault_mask;
} mr_pwm_event_data_t;

/* PWM data type */
MR_DRIVER_CLAZZES_EXPORT(pwm);
typedef struct mr_pwm_data {
    mr_uint32_t channel_mask;
    mr_uint32_t comp_mask;
    mr_uint32_t fault_mask;
    mr_waitable_t event;
} mr_pwm_data_t;

/* PWM operations type */
typedef struct mr_pwm_ops {
    mr_err_t (*configure)(const mr_driver_t *drv,
                          const mr_pwm_config_t *config);
    mr_err_t (*configuration)(const mr_driver_t *drv, mr_pwm_config_t *config);
    mr_err_t (*channel_configure)(const mr_driver_t *drv, mr_uint32_t channel,
                                  const mr_pwm_channel_config_t *config);
    mr_err_t (*channel_configuration)(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      mr_pwm_channel_config_t *config);
    mr_err_t (*update)(const mr_driver_t *drv, const mr_pwm_update_t *updates,
                       mr_size_t count);
    mr_err_t (*start)(const mr_driver_t *drv, mr_uint32_t channels);
    mr_err_t (*stop)(const mr_driver_t *drv, mr_uint32_t channels);
    mr_err_t (*state)(const mr_driver_t *drv, mr_pwm_state_t *state);
    mr_err_t (*fault_configure)(const mr_driver_t *drv, mr_uint32_t fault,
                                const mr_pwm_fault_config_t *config);
    mr_err_t (*fault_configuration)(const mr_driver_t *drv, mr_uint32_t fault,
                                    mr_pwm_fault_config_t *config);
    mr_err_t (*fault_clear)(const mr_driver_t *drv, mr_uint32_t faults);
    mr_err_t (*signal_configure)(const mr_driver_t *drv, mr_pwm_signal_t signal,
                                 const mr_pwm_signal_config_t *config);
    mr_err_t (*signal_configuration)(const mr_driver_t *drv,
                                     mr_pwm_signal_t signal,
                                     mr_pwm_signal_config_t *config);
    mr_err_t (*signal_control)(const mr_driver_t *drv, mr_pwm_signal_t signal,
                               mr_bool_t enable);
    mr_err_t (*event_control)(const mr_driver_t *drv, mr_uint32_t events,
                              mr_bool_t enable);
} mr_pwm_ops_t;

/**
 * @brief This macro function initializes PWM data.
 *
 * @param _channel_mask The implemented PWM channel mask.
 * @param _comp_mask The channels with complementary outputs.
 * @param _fault_mask The implemented fault input mask.
 */
#define MR_PWM_DATA_INIT(_channel_mask, _comp_mask, _fault_mask)               \
    {                                                                          \
        .channel_mask = (_channel_mask),                                       \
        .comp_mask = (_comp_mask),                                             \
        .fault_mask = (_fault_mask),                                           \
    }

/**
 * @brief This macro function defines a PWM driver.
 *
 * @param _name The PWM driver name.
 * @param _data The PWM driver data.
 * @param _priv The PWM private data.
 * @param _ops The PWM operations.
 */
#define MR_PWM_DEFINE(_name, _data, _priv, _ops)                               \
    MR_DRIVER_DEFINE(_name, MR_DRIVER_CLAZZES_STATIK(pwm), MR_DRIVER_TYPE_PWM, \
                     _data, _priv, _ops)

/**
 * @brief This macro function installs a PWM driver.
 *
 * @param _name The PWM driver name.
 * @param _data The PWM driver data.
 * @param _priv The PWM private data.
 * @param _ops The PWM operations.
 * @param _level The init level.
 */
#define MR_PWM_INSTALL(_name, _data, _priv, _ops, _level)                      \
    MR_DRIVER_INSTALL(_name, MR_DRIVER_CLAZZES_STATIK(pwm),                    \
                      MR_DRIVER_TYPE_PWM, _data, _priv, _ops, _level)

/**
 * @brief This function initializes a PWM driver.
 *
 * @param drv The PWM driver.
 * @param data The PWM driver data.
 * @param priv The PWM private data.
 * @param ops The PWM operations.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_pwm_init(mr_driver_t *drv, mr_pwm_data_t *data,
                               void *priv, mr_pwm_ops_t *ops) {
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_STATIK(pwm);

    /* Check parameter */
    MR_ASSERT(drv != MR_NULL);
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Init PWM driver */
    return mr_driver_init(drv, dclazz, MR_DRIVER_TYPE_PWM, data, priv, ops);
}

/**
 * @brief This function creates a PWM driver.
 *
 * @param data The PWM driver data.
 * @param priv The PWM private data.
 * @param ops The PWM operations.
 * @return The PWM driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_pwm_create(mr_pwm_data_t *data, void *priv,
                                     mr_pwm_ops_t *ops) {
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_DYNAMIC(pwm);

    /* Check parameter */
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Create PWM driver */
    return mr_driver_create(dclazz, MR_DRIVER_TYPE_PWM, data, priv, ops);
}

/**
 * @brief This function deletes a PWM driver.
 *
 * @param drv The PWM driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_pwm_del(mr_driver_t *drv) {
    /* Check parameter */
    MR_DRIVER_ASSERT_TYPE(drv, MR_DRIVER_TYPE_PWM);

    /* Delete driver */
    return mr_driver_del(drv);
}

/**
 * @brief This function configures a PWM controller.
 *
 * @param drv The PWM driver.
 * @param config The PWM controller configuration.
 * @return 0 on success, a negative error code on failure.
 *
 * @note period and all time fields in this API use nanoseconds. cycles == 0
 * selects continuous operation; otherwise the backend emits that many periods.
 */
mr_err_t mr_pwm_configure(mr_driver_t *drv, const mr_pwm_config_t *config);

/**
 * @brief This function gets the effective PWM controller configuration.
 *
 * @param drv The PWM driver.
 * @param config The PWM controller configuration output.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_pwm_configuration(mr_driver_t *drv, mr_pwm_config_t *config);

/**
 * @brief This function configures a PWM channel.
 *
 * @param drv The PWM driver.
 * @param channel The PWM channel.
 * @param config The PWM channel configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_pwm_channel_configure(mr_driver_t *drv, mr_uint32_t channel,
                                  const mr_pwm_channel_config_t *config);

/**
 * @brief This function gets the effective PWM channel configuration.
 *
 * @param drv The PWM driver.
 * @param channel The PWM channel.
 * @param config The PWM channel configuration output.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_pwm_channel_configuration(mr_driver_t *drv, mr_uint32_t channel,
                                      mr_pwm_channel_config_t *config);

/**
 * @brief This function atomically updates PWM channels on success.
 *
 * @param drv The PWM driver.
 * @param updates The PWM channel update array.
 * @param count The number of PWM channel updates.
 * @return 0 on success, a negative error code on failure.
 *
 * @note count == 0 performs no update and updates may be NULL.
 * @note The backend must validate and commit all updates as one transaction.
 * A successful return means the requested updates became one logical commit.
 * If an I/O failure occurs after a non-rollback-capable device has begun its
 * physical commit, the backend returns the error and must not publish the new
 * software state as successful; the caller must resynchronize the device.
 * MR_PWM_UPDATE_PERIOD commits the transaction at a common period boundary.
 */
mr_err_t mr_pwm_update(mr_driver_t *drv, const mr_pwm_update_t *updates,
                       mr_size_t count);

/**
 * @brief This function starts PWM channels.
 *
 * @param drv The PWM driver.
 * @param channels The PWM channel mask.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Returns -MR_EBUSY when any requested channel is already running.
 */
mr_err_t mr_pwm_start(mr_driver_t *drv, mr_uint32_t channels);

/**
 * @brief This function stops PWM channels and applies their configured idle
 * states.
 *
 * @param drv The PWM driver.
 * @param channels The PWM channel mask.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Channels that are already stopped are ignored.
 */
mr_err_t mr_pwm_stop(mr_driver_t *drv, mr_uint32_t channels);

/**
 * @brief This function gets the PWM running and fault state.
 *
 * @param drv The PWM driver.
 * @param state The PWM state output.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_pwm_state(mr_driver_t *drv, mr_pwm_state_t *state) {
    const mr_pwm_ops_t *pwm_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_PWM);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(state != MR_NULL);

    /* Get PWM operations */
    pwm_ops = (const mr_pwm_ops_t *)MR_DRIVER_OPS(drv);
    MR_ASSERT(pwm_ops->state != MR_NULL);

    /* Get PWM state */
    return pwm_ops->state(drv, state);
}

/**
 * @brief This function configures a PWM fault input.
 *
 * @param drv The PWM driver.
 * @param fault The PWM fault input.
 * @param config The PWM fault input configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_pwm_fault_configure(mr_driver_t *drv, mr_uint32_t fault,
                                const mr_pwm_fault_config_t *config);

/**
 * @brief This function gets a PWM fault input configuration.
 *
 * @param drv The PWM driver.
 * @param fault The PWM fault input.
 * @param config The PWM fault input configuration output.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_pwm_fault_configuration(mr_driver_t *drv, mr_uint32_t fault,
                                    mr_pwm_fault_config_t *config);

/**
 * @brief This function clears latched PWM fault inputs.
 *
 * @param drv The PWM driver.
 * @param faults The PWM fault input mask.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_pwm_fault_clear(mr_driver_t *drv, mr_uint32_t faults);

/**
 * @brief This function configures a PWM hardware signal.
 *
 * @param drv The PWM driver.
 * @param signal The PWM signal.
 * @param config The PWM signal configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_pwm_signal_configure(mr_driver_t *drv, mr_pwm_signal_t signal,
                                 const mr_pwm_signal_config_t *config);

/**
 * @brief This function gets a PWM hardware signal configuration.
 *
 * @param drv The PWM driver.
 * @param signal The PWM signal.
 * @param config The PWM signal configuration output.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_pwm_signal_configuration(mr_driver_t *drv, mr_pwm_signal_t signal,
                                     mr_pwm_signal_config_t *config);

/**
 * @brief This function enables a PWM hardware signal.
 *
 * @param drv The PWM driver.
 * @param signal The PWM signal.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Enabling a signal does not start PWM channels or enable wait events.
 */
MR_INLINE mr_err_t mr_pwm_signal_enable(mr_driver_t *drv,
                                        mr_pwm_signal_t signal) {
    const mr_pwm_ops_t *pwm_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_PWM);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get PWM operations */
    pwm_ops = (const mr_pwm_ops_t *)MR_DRIVER_OPS(drv);

    /* Check signal and operation */
    if ((signal != MR_PWM_SIGNAL_PERIOD) && (signal != MR_PWM_SIGNAL_COMPARE)) {
        return -MR_EINVAL;
    }
    if (!pwm_ops->signal_control) {
        return -MR_ENOSYS;
    }

    /* Enable PWM signal */
    return pwm_ops->signal_control(drv, signal, MR_TRUE);
}

/**
 * @brief This function disables a PWM hardware signal.
 *
 * @param drv The PWM driver.
 * @param signal The PWM signal.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Disabling a signal does not stop PWM channels or disable wait events.
 */
MR_INLINE mr_err_t mr_pwm_signal_disable(mr_driver_t *drv,
                                         mr_pwm_signal_t signal) {
    const mr_pwm_ops_t *pwm_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_PWM);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get PWM operations */
    pwm_ops = (const mr_pwm_ops_t *)MR_DRIVER_OPS(drv);

    /* Check signal and operation */
    if ((signal != MR_PWM_SIGNAL_PERIOD) && (signal != MR_PWM_SIGNAL_COMPARE)) {
        return -MR_EINVAL;
    }
    if (!pwm_ops->signal_control) {
        return -MR_ENOSYS;
    }

    /* Disable PWM signal */
    return pwm_ops->signal_control(drv, signal, MR_FALSE);
}

/**
 * @brief This function attaches a PWM event.
 *
 * @param drv The PWM driver.
 * @param entry The PWM event entry.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_pwm_event_attach(mr_driver_t *drv, mr_waiter_t *entry) {
    mr_pwm_data_t *pwm_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_PWM);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(entry != MR_NULL);

    /* Get PWM data */
    pwm_data = (mr_pwm_data_t *)MR_DRIVER_DATA(drv);

    /* Attach PWM event */
    return mr_waiter_wait(entry, &pwm_data->event);
}

/**
 * @brief This function enables PWM events.
 *
 * @param drv The PWM driver.
 * @param events The PWM event mask.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_pwm_event_enable(mr_driver_t *drv, mr_uint32_t events) {
    const mr_pwm_ops_t *pwm_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_PWM);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get PWM operations */
    pwm_ops = (const mr_pwm_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether event control is supported */
    if (!pwm_ops->event_control) {
        return -MR_ENOSYS;
    }

    /* Enable PWM events */
    return pwm_ops->event_control(drv, events, MR_TRUE);
}

/**
 * @brief This function disables PWM events.
 *
 * @param drv The PWM driver.
 * @param events The PWM event mask.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_pwm_event_disable(mr_driver_t *drv, mr_uint32_t events) {
    const mr_pwm_ops_t *pwm_ops;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_PWM);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get PWM operations */
    pwm_ops = (const mr_pwm_ops_t *)MR_DRIVER_OPS(drv);

    /* Check whether event control is supported */
    if (!pwm_ops->event_control) {
        return -MR_ENOSYS;
    }

    /* Disable PWM events */
    return pwm_ops->event_control(drv, events, MR_FALSE);
}

/**
 * @brief This function raises PWM events synchronously.
 *
 * @param drv The PWM driver.
 * @param events The PWM events.
 * @param data The PWM event data, or MR_NULL.
 * @return 0 on success, a negative error code on failure.
 *
 * @note data may be MR_NULL. When provided, it only needs to remain valid for
 * the synchronous waiter dispatch.
 */
MR_INLINE mr_err_t mr_pwm_event_raise(mr_driver_t *drv, mr_uint32_t events,
                                      const mr_pwm_event_data_t *data) {
    mr_pwm_data_t *pwm_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_PWM);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);

    /* Get PWM data */
    pwm_data = (mr_pwm_data_t *)MR_DRIVER_DATA(drv);

    /* Raise PWM events */
    return mr_waitable_raise(&pwm_data->event, (mr_wait_event_t)events,
                             (void *)data);
}

/**
 * @brief This function gets a PWM driver reference.
 *
 * @param drv The PWM driver.
 * @return The PWM driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_pwm_get(mr_driver_t *drv) {
    /* Get driver reference */
    return mr_driver_get(drv);
}

/**
 * @brief This function puts a PWM driver reference.
 *
 * @param drv The PWM driver.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_pwm_put(mr_driver_t *drv) {
    /* Put driver reference */
    return mr_driver_put(drv);
}

/* ================================ HardTree ================================ */

/**
 * @brief This macro function installs a PWM instance through hardtree.
 *
 * @param _id The instance id.
 * @param _priv The PWM private data.
 * @param _ops The PWM operations.
 */
#define MR_HT_PWM_INSTALL(_id, _priv, _ops)                                    \
    static mr_pwm_data_t MR_CAT(__mr_pwm_data_, _id)                           \
        = MR_PWM_DATA_INIT(MR_HT_DRIVER_PROP(_id, channel_mask),               \
                           MR_HT_DRIVER_PROP_OR(_id, comp_mask, 0U),           \
                           MR_HT_DRIVER_PROP_OR(_id, fault_mask, 0U));         \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(pwm),                   \
                         MR_DRIVER_TYPE_PWM, &MR_CAT(__mr_pwm_data_, _id),     \
                         _priv, _ops)

/* HardTree PWM specification type */
typedef struct mr_ht_pwm_spec {
    mr_driver_t *drv;
    mr_uint8_t channel;
} mr_ht_pwm_spec_t;

/**
 * @brief This function configures the controller referenced by a PWM spec.
 *
 * @param spec The PWM specification.
 * @param config The PWM controller configuration.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_pwm_configure(mr_ht_pwm_spec_t *spec,
                                       const mr_pwm_config_t *config) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Configure PWM controller */
    return mr_pwm_configure(spec->drv, config);
}

/**
 * @brief This function gets the controller configuration referenced by a PWM
 * spec.
 *
 * @param spec The PWM specification.
 * @param config The PWM controller configuration output.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_pwm_configuration(mr_ht_pwm_spec_t *spec,
                                           mr_pwm_config_t *config) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Get PWM controller configuration */
    return mr_pwm_configuration(spec->drv, config);
}

/**
 * @brief This function configures a PWM spec channel.
 *
 * @param spec The PWM specification.
 * @param config The PWM channel configuration.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_pwm_channel_configure(
    mr_ht_pwm_spec_t *spec, const mr_pwm_channel_config_t *config) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Configure PWM channel */
    return mr_pwm_channel_configure(spec->drv, spec->channel, config);
}

/**
 * @brief This function gets a PWM spec channel configuration.
 *
 * @param spec The PWM specification.
 * @param config The PWM channel configuration output.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_pwm_channel_configuration(
    mr_ht_pwm_spec_t *spec, mr_pwm_channel_config_t *config) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Get PWM channel configuration */
    return mr_pwm_channel_configuration(spec->drv, spec->channel, config);
}

/**
 * @brief This function updates a PWM spec channel atomically.
 *
 * @param spec The PWM specification.
 * @param pulse The PWM pulse width in nanoseconds.
 * @param phase The PWM phase offset in nanoseconds.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_pwm_update(mr_ht_pwm_spec_t *spec, mr_uint64_t pulse,
                                    mr_uint64_t phase) {
    mr_pwm_update_t update;

    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Init PWM update */
    update.channel = spec->channel;
    update.pulse = pulse;
    update.phase = phase;
    update.state = MR_PWM_OUTPUT_PWM;
    update.comp_state = MR_PWM_OUTPUT_PWM;

    /* Update PWM channel */
    return mr_pwm_update(spec->drv, &update, 1U);
}

/**
 * @brief This function starts a PWM spec channel.
 *
 * @param spec The PWM specification.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_pwm_start(mr_ht_pwm_spec_t *spec) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);
    if (spec->channel >= 32U) {
        return -MR_EINVAL;
    }

    /* Start PWM channel */
    return mr_pwm_start(spec->drv, 1U << spec->channel);
}

/**
 * @brief This function stops a PWM spec channel.
 *
 * @param spec The PWM specification.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_pwm_stop(mr_ht_pwm_spec_t *spec) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);
    if (spec->channel >= 32U) {
        return -MR_EINVAL;
    }

    /* Stop PWM channel */
    return mr_pwm_stop(spec->drv, 1U << spec->channel);
}

/**
 * @brief This function gets the controller state referenced by a PWM spec.
 *
 * @param spec The PWM specification.
 * @param state The PWM state output.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_pwm_state(mr_ht_pwm_spec_t *spec,
                                   mr_pwm_state_t *state) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Get PWM state */
    return mr_pwm_state(spec->drv, state);
}

/**
 * @brief This function attaches an event waiter through a PWM spec.
 *
 * @param spec The PWM specification.
 * @param entry The PWM event entry.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_pwm_event_attach(mr_ht_pwm_spec_t *spec,
                                          mr_waiter_t *entry) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Attach PWM event */
    return mr_pwm_event_attach(spec->drv, entry);
}

/**
 * @brief This function enables controller events through a PWM spec.
 *
 * @param spec The PWM specification.
 * @param events The PWM event mask.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_pwm_event_enable(mr_ht_pwm_spec_t *spec,
                                          mr_uint32_t events) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Enable PWM events */
    return mr_pwm_event_enable(spec->drv, events);
}

/**
 * @brief This function disables controller events through a PWM spec.
 *
 * @param spec The PWM specification.
 * @param events The PWM event mask.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_pwm_event_disable(mr_ht_pwm_spec_t *spec,
                                           mr_uint32_t events) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Disable PWM events */
    return mr_pwm_event_disable(spec->drv, events);
}

/**
 * @brief This function configures a controller signal through a PWM spec.
 *
 * @param spec The PWM specification.
 * @param signal The PWM signal.
 * @param config The PWM signal configuration.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t
    mr_ht_pwm_signal_configure(mr_ht_pwm_spec_t *spec, mr_pwm_signal_t signal,
                               const mr_pwm_signal_config_t *config) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Configure PWM signal */
    return mr_pwm_signal_configure(spec->drv, signal, config);
}

/**
 * @brief This function gets a controller signal configuration through a PWM
 * spec.
 *
 * @param spec The PWM specification.
 * @param signal The PWM signal.
 * @param config The PWM signal configuration output.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_pwm_signal_configuration(
    mr_ht_pwm_spec_t *spec, mr_pwm_signal_t signal,
    mr_pwm_signal_config_t *config) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Get PWM signal configuration */
    return mr_pwm_signal_configuration(spec->drv, signal, config);
}

/**
 * @brief This function enables a controller signal through a PWM spec.
 *
 * @param spec The PWM specification.
 * @param signal The PWM signal.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_pwm_signal_enable(mr_ht_pwm_spec_t *spec,
                                           mr_pwm_signal_t signal) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Enable PWM signal */
    return mr_pwm_signal_enable(spec->drv, signal);
}

/**
 * @brief This function disables a controller signal through a PWM spec.
 *
 * @param spec The PWM specification.
 * @param signal The PWM signal.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_ht_pwm_signal_disable(mr_ht_pwm_spec_t *spec,
                                            mr_pwm_signal_t signal) {
    /* Check parameter */
    MR_ASSERT(spec != MR_NULL);

    /* Disable PWM signal */
    return mr_pwm_signal_disable(spec->drv, signal);
}

/**
 * @brief This macro function creates a PWM specification from HardTree
 * properties.
 *
 * @param _node The node.
 * @param _prop The property name.
 * @return The initialized mr_ht_pwm_spec_t structure.
 *
 * @note Property cells: [phandle, channel].
 */
#define MR_HT_NODE_PWM_SPEC(_node, _prop)                                      \
    __MR_HT_NODE_PWM_SPEC_IMPL(0, _node, _prop)
#define __MR_HT_NODE_PWM_SPEC_IMPL(_elem, _node, _prop)                        \
    {                                                                          \
        .drv                                                                   \
        = MR_HT_DRIVER_FIND(MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 0)),     \
        .channel = (mr_uint8_t)MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 1),   \
    }

/**
 * @brief This macro function creates PWM specifications from a HardTree
 * property array.
 *
 * @param _node The node.
 * @param _prop The property name.
 *
 * @note Property cells: [phandle, channel].
 */
#define MR_HT_NODE_PWM_SPEC_FOREACH(_node, _prop)                              \
    MR_HT_NODE_PROP_ELEM_FOREACH(_node, _prop, __MR_HT_NODE_PWM_SPEC_IMPL)

/**
 * @brief This macro function creates a PWM specification from HardTree driver
 * properties.
 *
 * @param _id The driver instance id.
 * @param _prop The property name.
 * @return The initialized mr_ht_pwm_spec_t structure.
 *
 * @note Property cells: [phandle, channel].
 */
#define MR_HT_PWM_SPEC(_id, _prop)                                             \
    MR_HT_NODE_PWM_SPEC(MR_HT_DRIVER(_id), _prop)

/**
 * @brief This macro function creates PWM specifications from a HardTree driver
 * property array.
 *
 * @param _id The driver instance id.
 * @param _prop The property name.
 *
 * @note Property cells: [phandle, channel].
 */
#define MR_HT_PWM_SPEC_FOREACH(_id, _prop)                                     \
    MR_HT_NODE_PWM_SPEC_FOREACH(MR_HT_DRIVER(_id), _prop)
#endif /* defined(MR_USE_DRIVER_PWM) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_PWM_H__ */

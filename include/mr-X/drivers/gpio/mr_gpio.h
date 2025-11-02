/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_DRIVER_GPIO_H__
#define __MR_DRIVER_GPIO_H__

#include <mr_config.h>
#if defined(MR_USE_DRIVER_GPIO)
#include <mr-X/drivers/mr_driver.h>
#endif /* defined(MR_USE_DRIVER_GPIO) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup GPIO
 * @{
 */

#if defined(MR_USE_DRIVER_GPIO)
/* GPIO direction definition */
#define MR_GPIO_DIR_HIGHZ               (0x0U << 1)
#define MR_GPIO_DIR_INPUT               (0x1U << 1)
#define MR_GPIO_DIR_OUTPUT              (0x2U << 1)
#define MR_GPIO_DIR_MASK                (0x3U << 1)

/* GPIO pull definition */
#define MR_GPIO_PULL_NONE               (0x0U << 3)
#define MR_GPIO_PULL_UP                 (0x1U << 3)
#define MR_GPIO_PULL_DOWN               (0x2U << 3)
#define MR_GPIO_PULL_MASK               (0x3U << 3)

/* GPIO output type definition */
#define MR_GPIO_OTYPE_PP                (0x0U << 5)
#define MR_GPIO_OTYPE_OD                (0x1U << 5)
#define MR_GPIO_OTYPE_MASK              (0x1U << 5)

/* GPIO active level definition */
#define MR_GPIO_ACTIVE_HIGH             (0x0U << 7)
#define MR_GPIO_ACTIVE_LOW              (0x1U << 7)
#define MR_GPIO_ACTIVE_MASK             (0x1U << 7)

#if defined(MR_USE_DRIVER_GPIO_IRQ)
/* GPIO interrupt definition */
#define MR_GPIO_IRQ_EDGE_RISING         (0x1U << 8)
#define MR_GPIO_IRQ_EDGE_FALLING        (0x2U << 8)
#define MR_GPIO_IRQ_EDGE_BOTH           (0x3U << 8)
#define MR_GPIO_IRQ_LEVEL_HIGH          (0x4U << 8)
#define MR_GPIO_IRQ_LEVEL_LOW           (0x5U << 8)
#define MR_GPIO_IRQ_MASK                (0x7U << 8)
#endif /* defined(MR_USE_DRIVER_GPIO_IRQ) */

/* GPIO level definition */
#define MR_GPIO_LEVEL_LOW               (0x0U << 0)
#define MR_GPIO_LEVEL_HIGH              (0x1U << 0)
#define MR_GPIO_LEVEL_MASK              (0x1U << 0)

/* GPIO date type */
MR_DRIVER_DOPSES_EXPORT(gpio);
typedef struct mr_gpio_data {
    mr_uint32_t exist_mask;
    mr_uint32_t invert;
#if defined(MR_USE_DRIVER_GPIO_IRQ)
    mr_waitable_t irq;
#endif /* defined(MR_USE_DRIVER_GPIO_IRQ) */
} mr_gpio_data_t;

/* GPIO operations type */
typedef struct mr_gpio_ops {
    mr_err_t (*pin_mode_set)(const mr_driver_t *drv, mr_uint32_t pin,
                             mr_uint32_t mode);
    mr_err_t (*pin_mode_get)(const mr_driver_t *drv, mr_uint32_t pin,
                             mr_uint32_t *mode);
    mr_err_t (*pin_set)(const mr_driver_t *drv, mr_uint32_t pin,
                        mr_uint8_t value);
    mr_err_t (*pin_get)(const mr_driver_t *drv, mr_uint32_t pin,
                        mr_uint8_t *value);
    mr_err_t (*port_set)(const mr_driver_t *drv, mr_uint32_t mask);
    mr_err_t (*port_get)(const mr_driver_t *drv, mr_uint32_t *mask);
} mr_gpio_ops_t;

/**
 * @brief This macro function initializes a GPIO data.
 *
 * @param _exist_mask The GPIO exist mask.
 * @param _invert The GPIO invert mask.
 */
#define MR_GPIO_DATA_INIT(_exist_mask, _invert)                                \
    {.exist_mask = (_exist_mask), .invert = (_invert)}

/**
 * @brief This macro function defines a GPIO driver.
 *
 * @param _name The GPIO driver name.
 * @param _data The GPIO driver data.
 * @param _priv The GPIO private data.
 * @param _ops The GPIO operations.
 */
#define MR_GPIO_DEFINE(_name, _data, _priv, _ops)                              \
    MR_DRIVER_DEFINE(_name, MR_DRIVER_TYPE_GPIO,                               \
                     MR_DRIVER_DOPSES_STATIK(MR_DRIVER_DOPSES_FIND(gpio)),     \
                     _data, _priv, _ops)

/**
 * @brief This macro function installs a GPIO driver.
 *
 * @param _name The GPIO driver name.
 * @param _data The GPIO driver data.
 * @param _priv The GPIO private data.
 * @param _ops The GPIO operations.
 * @param _level The init level.
 */
#define MR_GPIO_INSTALL(_name, _data, _priv, _ops, _level)                     \
    MR_DRIVER_INSTALL(_name, MR_DRIVER_TYPE_GPIO,                              \
                      MR_DRIVER_DOPSES_STATIK(MR_DRIVER_DOPSES_FIND(gpio)),    \
                      _data, _priv, _ops, _level)

/**
 * @brief This function initializes a GPIO driver.
 *
 * @param drv The GPIO driver.
 * @param data The GPIO driver data.
 * @param priv The GPIO private data.
 * @param ops The GPIO operations.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_gpio_init(mr_driver_t *drv, mr_gpio_data_t *data, void *priv,
                      mr_gpio_ops_t *ops);

/**
 * @brief This function creates a GPIO driver.
 *
 * @param data The GPIO driver data.
 * @param priv The GPIO private data.
 * @param ops The GPIO operations.
 * @return The GPIO driver on success, MR_NULL on failure.
 */
mr_driver_t *mr_gpio_create(mr_gpio_data_t *data, void *priv,
                            mr_gpio_ops_t *ops);

/**
 * @brief This function deletes a GPIO driver.
 *
 * @param drv The GPIO driver.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_gpio_del(mr_driver_t *drv) {
    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);

    /* Delete driver */
    return mr_driver_del(drv);
}

/**
 * @brief This function sets a GPIO pin mode.
 *
 * @param drv The GPIO driver.
 * @param pin The GPIO pin.
 * @param mode The GPIO pin mode.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_gpio_pin_mode_set(const mr_driver_t *drv, mr_uint32_t pin,
                              mr_uint32_t mode);

/**
 * @brief This function gets a GPIO pin mode.
 *
 * @param drv The GPIO driver.
 * @param pin The GPIO pin.
 * @param mode The GPIO pin mode.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_gpio_pin_mode_get(const mr_driver_t *drv, mr_uint32_t pin,
                              mr_uint32_t *mode);

/**
 * @brief This function sets a GPIO pin value.
 *
 * @param drv The GPIO driver.
 * @param pin The GPIO pin.
 * @param value The GPIO pin value.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_gpio_pin_set(const mr_driver_t *drv, mr_uint32_t pin,
                         mr_uint8_t value);

/**
 * @brief This function gets a GPIO pin value.
 *
 * @param drv The GPIO driver.
 * @param pin The GPIO pin.
 * @return The GPIO pin value on success, a negative error code on failure.
 */
int mr_gpio_pin_get(const mr_driver_t *drv, mr_uint32_t pin);

/**
 * @brief This function toggles a GPIO pin.
 *
 * @param drv The GPIO driver.
 * @param pin The GPIO pin.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_gpio_pin_toggle(const mr_driver_t *drv, mr_uint32_t pin);

/**
 * @brief This function sets a GPIO port value.
 *
 * @param drv The GPIO driver.
 * @param mask The GPIO port value mask.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_gpio_port_set(const mr_driver_t *drv, mr_uint32_t mask);

/**
 * @brief This function gets a GPIO port value.
 *
 * @param drv The GPIO driver.
 * @param mask The GPIO port value mask.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_gpio_port_get(const mr_driver_t *drv, mr_uint32_t *mask);

/**
 * @brief This function toggles a GPIO port.
 *
 * @param drv The GPIO driver.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_gpio_port_toggle(const mr_driver_t *drv);

#if defined(MR_USE_DRIVER_GPIO_IRQ)
/**
 * @brief This function attaches a GPIO interrupt.
 *
 * @param drv The GPIO driver.
 * @param entry The GPIO interrupt entry.
 * @return 0 on success, a negative error code on failure.
 *
 * @note The function will handle the event types for you internally, don't
 * worry.
 */
mr_err_t mr_gpio_irq_attach(mr_driver_t *drv, mr_waiter_t *entry);

/**
 * @brief This macro function gets a GPIO interrupt pin from an event.
 *
 * @param _pin The GPIO pin.
 * @return The GPIO interrupt pin.
 */
#define MR_GPIO_IRQ_EVENT_PIN(_event)   ((_event) & 0x0fffffffU)

/**
 * @brief This function handles a GPIO interrupt.
 *
 * @param drv The GPIO driver.
 * @param pin The GPIO pin.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_gpio_irq_handle(mr_driver_t *drv, mr_uint32_t pin);
#endif /* defined(MR_USE_DRIVER_GPIO_IRQ) */

/**
 * @brief This function gets a GPIO driver.
 *
 * @param drv The GPIO driver.
 * @return The GPIO driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_gpio_get(mr_driver_t *drv) {
    /* Get driver reference */
    return mr_driver_get(drv);
}

/**
 * @brief This function puts a GPIO driver.
 *
 * @param drv The GPIO driver.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_gpio_put(mr_driver_t *drv) {
    /* Put driver reference */
    return mr_driver_put(drv);
}

#if defined(MR_USE_DRIVER_HARDTREE)
/**
 * @brief This macro function installs a GPIO instance through hardtree.
 *
 * @param _id The instance id.
 * @param _priv The GPIO private data.
 * @param _ops The GPIO operations.
 */
#define MR_HT_INST_GPIO_INSTALL(_id, _priv, _ops)                              \
    static mr_gpio_data_t __mr_gpio_data_##_id = MR_GPIO_DATA_INIT(            \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), exist_mask, 0xffffffff),    \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), invert, 0));                \
    MR_HT_INST_DRIVER_INSTALL(                                                 \
        _id, MR_DRIVER_TYPE_GPIO,                                              \
        MR_DRIVER_DOPSES_STATIK(MR_DRIVER_DOPSES_FIND(gpio)),                  \
        &__mr_gpio_data_##_id, _priv, _ops);

/**
 * @brief This macro function installs all GPIO instances.
 *
 * @param _priv The GPIO private data.
 * @param _ops The GPIO operations.
 */
#define MR_HT_GPIO_INSTALL_FOREACH(_priv, _ops)                                \
    MR_HT_INST_DRIVER_FOREACH(MR_HT_INST_GPIO_INSTALL, _priv, _ops)
#endif /* defined(MR_USE_DRIVER_HARDTREE) */
#endif /* defined(MR_USE_DRIVER_GPIO) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_GPIO_H__ */

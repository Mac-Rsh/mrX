/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_DRIVER_GPIO_H__
#define __MR_DRIVER_GPIO_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_DRIVER_GPIO)
#include <mrX/drivers/mr_driver.h>
#endif /* defined(MR_USE_DRIVER_GPIO) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup GPIO
 * @{
 */

#if defined(MR_USE_DRIVER_GPIO)
/* GPIO mode definition */
typedef enum mr_gpio_mode {
    MR_GPIO_MODE_ANALOG,
    MR_GPIO_MODE_INPUT,
    MR_GPIO_MODE_OUTPUT,
    MR_GPIO_MODE_IRQ_RISING,
    MR_GPIO_MODE_IRQ_FALLING,
    MR_GPIO_MODE_IRQ_EDGE,
    MR_GPIO_MODE_IRQ_HIGH,
    MR_GPIO_MODE_IRQ_LOW,
} mr_gpio_mode_t;

/* GPIO alternate definition */
typedef enum mr_gpio_alt {
    MR_GPIO_ALT_DISABLE,
    MR_GPIO_ALT_ENABLE,
} mr_gpio_alt_t;

/* GPIO pull definition */
typedef enum mr_gpio_pull {
    MR_GPIO_PULL_NONE,
    MR_GPIO_PULL_UP,
    MR_GPIO_PULL_DOWN,
} mr_gpio_pull_t;

/* GPIO output type definition */
typedef enum mr_gpio_otype {
    MR_GPIO_OTYPE_PP,
    MR_GPIO_OTYPE_OD,
} mr_gpio_otype_t;

/* GPIO level definition */
typedef enum mr_gpio_level {
    MR_GPIO_LEVEL_LOW,
    MR_GPIO_LEVEL_HIGH,
} mr_gpio_level_t;

/* GPIO configuration type */
typedef struct mr_gpio_config {
    mr_uint8_t mode: 3;
    mr_uint8_t alt: 1;
    mr_uint8_t pull: 2;
    mr_uint8_t otype: 1;
    mr_uint8_t level: 1;
} mr_gpio_config_t;

/* GPIO data type */
MR_DRIVER_CLAZZES_EXPORT(gpio);
typedef struct mr_gpio_data {
    mr_uint32_t exist_mask;
    mr_uint32_t invert_mask;
    mr_waitable_t irq;
} mr_gpio_data_t;

/* GPIO operations type */
typedef struct mr_gpio_ops {
    mr_err_t (*pin_configure)(const mr_driver_t *drv, mr_uint32_t pin,
                              const mr_gpio_config_t *config);
    mr_err_t (*pin_configuration)(const mr_driver_t *drv, mr_uint32_t pin,
                                  mr_gpio_config_t *config);
    mr_err_t (*pin_write)(const mr_driver_t *drv, mr_uint32_t pin,
                          mr_uint8_t value);
    mr_err_t (*pin_read)(const mr_driver_t *drv, mr_uint32_t pin,
                         mr_uint8_t *value);
    mr_err_t (*port_write)(const mr_driver_t *drv, mr_uint32_t mask);
    mr_err_t (*port_read)(const mr_driver_t *drv, mr_uint32_t *mask);
} mr_gpio_ops_t;

/**
 * @brief This macro function initializes a GPIO data.
 *
 * @param _exist_mask The GPIO exist mask.
 * @param _invert_mask The GPIO invert mask.
 */
#define MR_GPIO_DATA_INIT(_exist_mask, _invert_mask)                           \
    {.exist_mask = (_exist_mask), .invert_mask = (_invert_mask)}

/**
 * @brief This macro function defines a GPIO driver.
 *
 * @param _name The GPIO driver name.
 * @param _data The GPIO driver data.
 * @param _priv The GPIO private data.
 * @param _ops The GPIO operations.
 */
#define MR_GPIO_DEFINE(_name, _data, _priv, _ops)                              \
    MR_DRIVER_DEFINE(_name,                                                    \
                     MR_DRIVER_CLAZZES_STATIK(MR_DRIVER_CLAZZES_FIND(gpio)),   \
                     MR_DRIVER_TYPE_GPIO, _data, _priv, _ops)

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
    MR_DRIVER_INSTALL(_name,                                                   \
                      MR_DRIVER_CLAZZES_STATIK(MR_DRIVER_CLAZZES_FIND(gpio)),  \
                      MR_DRIVER_TYPE_GPIO, _data, _priv, _ops, _level)

/**
 * @brief This function initializes a GPIO driver.
 *
 * @param drv The GPIO driver.
 * @param data The GPIO driver data.
 * @param priv The GPIO private data.
 * @param ops The GPIO operations.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_gpio_init(mr_driver_t *drv, mr_gpio_data_t *data,
                                void *priv, mr_gpio_ops_t *ops) {
    const mr_driver_clazzes_t *dclazzes = MR_DRIVER_CLAZZES_FIND(gpio);
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_STATIK(dclazzes);

    /* Check parameter */
    MR_ASSERT(drv != MR_NULL);
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Init GPIO driver */
    return mr_driver_init(drv, dclazz, MR_DRIVER_TYPE_GPIO, data, priv, ops);
}

/**
 * @brief This function creates a GPIO driver.
 *
 * @param data The GPIO driver data.
 * @param priv The GPIO private data.
 * @param ops The GPIO operations.
 * @return The GPIO driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_gpio_create(mr_gpio_data_t *data, void *priv,
                                      mr_gpio_ops_t *ops) {
    const mr_driver_clazzes_t *dclazzes = MR_DRIVER_CLAZZES_FIND(gpio);
    const mr_driver_clazz_t *dclazz = MR_DRIVER_CLAZZES_DYNAMIC(dclazzes);

    /* Check parameter */
    MR_ASSERT(data != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Create GPIO driver */
    return mr_driver_create(dclazz, MR_DRIVER_TYPE_GPIO, data, priv, ops);
}

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
 * @brief This function configures a GPIO pin.
 *
 * @param drv The GPIO driver.
 * @param pin The GPIO pin.
 * @param config The GPIO pin configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_gpio_pin_configure(mr_driver_t *drv, mr_uint32_t pin,
                               mr_gpio_config_t *config);

/**
 * @brief This function gets a GPIO pin configuration.
 *
 * @param drv The GPIO driver.
 * @param pin The GPIO pin.
 * @param config The GPIO pin configuration.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_gpio_pin_configuration(mr_driver_t *drv, mr_uint32_t pin,
                                   mr_gpio_config_t *config);

/**
 * @brief This function writes a GPIO pin value.
 *
 * @param drv The GPIO driver.
 * @param pin The GPIO pin.
 * @param value The GPIO pin value.
 * @return The number of bytes written on success, a negative error code on
 * failure.
 */
MR_INLINE int mr_gpio_pin_write(mr_driver_t *drv, mr_uint32_t pin,
                                mr_uint8_t value) {
    const mr_gpio_ops_t *gp_ops;
    mr_gpio_data_t *gp_data;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(pin < 32);

    /* Get GPIO data and operations */
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);
    gp_ops = (mr_gpio_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if pin is existed */
    if (!(gp_data->exist_mask & (1U << pin))) {
        return -MR_EINVAL;
    }

    /* Write pin value */
    value = (!!value) ^ (!!(gp_data->invert_mask & (1U << pin)));
    ret = gp_ops->pin_write(drv, pin, value);
    if (ret != 0) {
        return (int)ret;
    }
    return sizeof(value);
}

/**
 * @brief This function reads a GPIO pin value.
 *
 * @param drv The GPIO driver.
 * @param pin The GPIO pin.
 * @param value The GPIO pin value.
 * @return The number of bytes read on success, a negative error code on
 * failure.
 */
MR_INLINE int mr_gpio_pin_read(mr_driver_t *drv, mr_uint32_t pin,
                               mr_uint8_t *value) {
    const mr_gpio_ops_t *gp_ops;
    mr_gpio_data_t *gp_data;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(pin < 32);

    /* Get GPIO data and operations */
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);
    gp_ops = (mr_gpio_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if pin is existed */
    if (!(gp_data->exist_mask & (1U << pin))) {
        return -MR_EINVAL;
    }

    /* Read pin value */
    ret = gp_ops->pin_read(drv, pin, value);
    if (ret != 0) {
        return (int)ret;
    }
    *value = (!!(*value)) ^ (!!(gp_data->invert_mask & (1U << pin)));
    return sizeof(*value);
}

/**
 * @brief This function configures a GPIO pin as input.
 *
 * @param drv The GPIO driver.
 * @param pin The GPIO pin.
 * @param pull The GPIO pull-up/pull-down.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_gpio_pin_input(mr_driver_t *drv, mr_uint32_t pin,
                                     mr_gpio_pull_t pull) {
    mr_gpio_config_t config;

    /* Configure GPIO pin as input */
    config.pull = (mr_uint8_t)pull & 0x3U;
    config.mode = MR_GPIO_MODE_INPUT;
    config.alt = MR_FALSE;

    /* Configure GPIO pin */
    return mr_gpio_pin_configure(drv, pin, &config);
}

/**
 * @brief This function configures a GPIO pin as output.
 *
 * @param drv The GPIO driver.
 * @param pin The GPIO pin.
 * @param otype The GPIO output type.
 * @param value The GPIO output value.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_gpio_pin_output(mr_driver_t *drv, mr_uint32_t pin,
                                      mr_gpio_otype_t otype, mr_uint8_t value) {
    mr_gpio_config_t config;

    /* Configure GPIO pin as output */
    config.otype = (mr_uint8_t)otype & 0x1U;
    config.mode = MR_GPIO_MODE_OUTPUT;
    config.pull = MR_GPIO_PULL_NONE;
    config.level = value & 0x1U;
    config.alt = MR_FALSE;

    /* Configure GPIO pin */
    return mr_gpio_pin_configure(drv, pin, &config);
}

/**
 * @brief This function toggles a GPIO pin.
 *
 * @param drv The GPIO driver.
 * @param pin The GPIO pin.
 * @return The number of bytes toggle on success, a negative error code on
 * failure.
 */
MR_INLINE int mr_gpio_pin_toggle(mr_driver_t *drv, mr_uint32_t pin) {
    const mr_gpio_ops_t *gp_ops;
    mr_gpio_data_t *gp_data;
    mr_uint8_t value;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(pin < 32);

    /* Get GPIO data and operations */
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);
    gp_ops = (mr_gpio_ops_t *)MR_DRIVER_OPS(drv);

    /* Check if pin is existed */
    if (!(gp_data->exist_mask & (1U << pin))) {
        return -MR_EINVAL;
    }

    /* Read pin value */
    ret = gp_ops->pin_read(drv, pin, &value);
    if (ret != 0) {
        return (int)ret;
    }

    /* Toggle pin value */
    value = (!!value) ^ 0x1U;
    ret = gp_ops->pin_write(drv, pin, value);
    if (ret != 0) {
        return (int)ret;
    }
    return sizeof(value);
}

/**
 * @brief This function writes a GPIO port value mask.
 *
 * @param drv The GPIO driver.
 * @param mask The GPIO port value mask.
 * @return The number of bytes written on success, a negative error code on
 * failure.
 */
MR_INLINE int mr_gpio_port_write(mr_driver_t *drv, mr_uint32_t mask) {
    const mr_gpio_ops_t *gp_ops;
    mr_gpio_data_t *gp_data;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get GPIO data and operations */
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);
    gp_ops = (mr_gpio_ops_t *)MR_DRIVER_OPS(drv);

    /* Write port value mask */
    mask = mask ^ gp_data->invert_mask;
    ret = gp_ops->port_write(drv, mask);
    if (ret != 0) {
        return (int)ret;
    }
    return sizeof(mask);
}

/**
 * @brief This function gets a GPIO port value.
 *
 * @param drv The GPIO driver.
 * @param mask The GPIO port value mask.
 * @return The number of bytes read on success, a negative error code on
 * failure.
 */
MR_INLINE int mr_gpio_port_read(mr_driver_t *drv, mr_uint32_t *mask) {
    const mr_gpio_ops_t *gp_ops;
    mr_gpio_data_t *gp_data;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);
    MR_ASSERT(mask != MR_NULL);

    /* Get GPIO data and operations */
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);
    gp_ops = (mr_gpio_ops_t *)MR_DRIVER_OPS(drv);

    /* Read port value mask */
    ret = gp_ops->port_read(drv, mask);
    if (ret != 0) {
        return (int)ret;
    }
    *mask = *mask ^ gp_data->invert_mask;
    return sizeof(*mask);
}

/**
 * @brief This function toggles a GPIO port.
 *
 * @param drv The GPIO driver.
 * @return The number of bytes toggle on success, a negative error code on
 * failure.
 */
MR_INLINE int mr_gpio_port_toggle(mr_driver_t *drv) {
    const mr_gpio_ops_t *gp_ops;
    mr_uint32_t mask;
    mr_err_t ret;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(MR_DRIVER_OPS(drv) != MR_NULL);

    /* Get GPIO operations */
    gp_ops = (mr_gpio_ops_t *)MR_DRIVER_OPS(drv);

    /* Get port value */
    ret = gp_ops->port_read(drv, &mask);
    if (ret != 0) {
        return (int)ret;
    }

    /* Toggle port value */
    mask = mask ^ 0xffffffffU;
    ret = gp_ops->port_write(drv, mask);
    if (ret != 0) {
        return (int)ret;
    }
    return sizeof(mask);
}

/**
 * @brief This function attaches a GPIO interrupt.
 *
 * @param drv The GPIO driver.
 * @param entry The GPIO interrupt entry.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_gpio_irq_attach(mr_driver_t *drv, mr_waiter_t *entry) {
    mr_gpio_data_t *gp_data;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(entry != MR_NULL);

    /* Get GPIO data */
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);

    /* Attach GPIO irq */
    return mr_waiter_wait(entry, &gp_data->irq);
}

/**
 * @brief This function handles a GPIO interrupt.
 *
 * @param drv The GPIO driver.
 * @param pin The GPIO pin.
 * @return 0 on success, a negative error code on failure.
 */
MR_INLINE mr_err_t mr_gpio_irq_handle(mr_driver_t *drv, mr_uint32_t pin) {
    mr_gpio_data_t *gp_data;
    mr_uint64_t event;

    /* Check parameter */
    MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_GPIO);
    MR_ASSERT(MR_DRIVER_DATA(drv) != MR_NULL);
    MR_ASSERT(pin < 32);

    /* Get GPIO data */
    gp_data = (mr_gpio_data_t *)MR_DRIVER_DATA(drv);

    /* Raise GPIO EXTI event */
    event = MR_WAIT_IN | (1ULL << pin);
    return mr_waitable_raise(&gp_data->irq, event);
}

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

/* ================================ HardTree ================================ */

/**
 * @brief This macro function installs a GPIO instance through hardtree.
 *
 * @param _id The instance id.
 * @param _priv The GPIO private data.
 * @param _ops The GPIO operations.
 */
#define MR_HT_INST_GPIO_INSTALL(_id, _priv, _ops)                              \
    static mr_gpio_data_t __mr_gpio_data_##_id = MR_GPIO_DATA_INIT(            \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), exist_mask, 0xffffffffU),   \
        MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), invert_mask, 0x0U));        \
    MR_HT_INST_DRIVER_INSTALL(                                                 \
        _id, MR_DRIVER_CLAZZES_STATIK(MR_DRIVER_CLAZZES_FIND(gpio)),           \
        MR_DRIVER_TYPE_GPIO, &__mr_gpio_data_##_id, _priv, _ops)

/* GPIO hardtree specification type */
typedef struct mr_ht_gpio_spec {
    mr_driver_t *drv;
    mr_uint8_t pin;
    mr_gpio_config_t config;
} mr_ht_gpio_spec_t;

/**
 * @brief This macro function creates a GPIO specification structure from
 *        hardtree properties.
 *
 * @param _node The node.
 * @param _prop The property name.
 * @return The initialized mr_ht_gpio_spec_t structure.
 *
 * @note Property cells: [phandle, pin, mode, alt, pull, otype, level].
 */
#define MR_HT_GPIO_SPEC(_node, _prop)   __MR_HT_GPIO_SPEC_IMPL(0, _node, _prop)
#define __MR_HT_GPIO_SPEC_IMPL(_elem, _node, _prop)                            \
    {                                                                          \
        .drv                                                                   \
            = MR_HT_DRIVER_FIND(MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 0)), \
            .pin = (mr_uint8_t)MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 1),   \
            .config                                                            \
            = {.mode = MR_HT_NODE_PROP_ELEM_OR(_node, _prop, _elem, 2,         \
                                               MR_GPIO_MODE_ANALOG),           \
               .alt                                                            \
               = MR_HT_NODE_PROP_ELEM_OR(_node, _prop, _elem, 3, MR_FALSE),    \
               .pull = MR_HT_NODE_PROP_ELEM_OR(_node, _prop, _elem, 4,         \
                                               MR_GPIO_PULL_NONE),             \
               .otype = MR_HT_NODE_PROP_ELEM_OR(_node, _prop, _elem, 5,        \
                                                MR_GPIO_OTYPE_PP),             \
               .level = MR_HT_NODE_PROP_ELEM_OR(_node, _prop, _elem, 6,        \
                                                MR_GPIO_LEVEL_LOW),            \
              }                                                                \
    }

/**
 * @brief This macro function iterates over all GPIO specification elements
 *        in a property array and creates corresponding structures.
 *
 * @param _node The node.
 * @param _prop The property name.
 *
 * @note Property cells: [phandle, pin, mode, alt, pull, otype, level].
 */
#define MR_HT_GPIO_SPEC_FOREACH(_node, _prop)                                  \
    MR_HT_NODE_PROP_ELEM_FOREACH(_node, _prop, __MR_HT_GPIO_SPEC_IMPL)
#endif /* defined(MR_USE_DRIVER_GPIO) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_GPIO_H__ */

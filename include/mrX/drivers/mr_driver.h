/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_DRIVER_H__
#define __MR_DRIVER_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_DRIVER)
#include <mrX/drivers/mr_hardtree.h>
#include <mrX/kernel/mr_object.h>
#include <mrX/kernel/mr_wait.h>
#endif /* defined(MR_USE_DRIVER) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Drivers
 * @{
 */

#if defined(MR_USE_DRIVER)
struct mr_driver;

/* Driver flag definition */
#define MR_DRIVER_FLAG_SETUP            (1U << 0)
#define MR_DRIVER_FLAG_LOCK             (1U << 1)
#define MR_DRIVER_FLAG_DYING            (1U << 2)

/* Driver level definition */
#define MR_DRIVER_LEVEL_EARLY           "1"
#define MR_DRIVER_LEVEL_PRIO(_prio)     "2." _prio
#define MR_DRIVER_LEVEL_NORMAL          "3"
#define MR_DRIVER_LEVEL_LAST            "4"
#define MR_DRIVER_LEVEL_DEFER           "9"

/* Driver major type definition */
typedef enum mr_driver_type {
    MR_DRIVER_TYPE_NONE = 0,
    MR_DRIVER_TYPE_RAM = 0x0100,
    MR_DRIVER_TYPE_GPIO = 0x0200,
    MR_DRIVER_TYPE_UART = 0x0300,
    MR_DRIVER_TYPE_I2C = 0x0400,
    MR_DRIVER_TYPE_ADC = 0x0500,
    MR_DRIVER_TYPE_DAC = 0x0600,
    MR_DRIVER_TYPE_DMA = 0x0700,
    MR_DRIVER_TYPE_MASK = 0xff00,
} mr_driver_type_t;

/* Driver method type */
typedef mr_err_t(mr_driver_method_t)(const struct mr_driver *drv);
/* Driver release type */
typedef void(mr_driver_release_t)(const struct mr_driver *drv);

/* Driver clazz type */
typedef struct mr_driver_clazz {
    const char *name;
    mr_driver_method_t *init;
    mr_driver_method_t *deinit;
    mr_driver_release_t *release;
} mr_driver_clazz_t;

/* Driver clazzes type */
typedef struct mr_driver_clazzes {
    const mr_driver_clazz_t statik;
    const mr_driver_clazz_t dynamic;
} mr_driver_clazzes_t;

/* Driver type */
MR_CLAZZES_EXPORT(driver);
typedef struct mr_driver {
    mr_object_t parent;
    const mr_driver_clazz_t *clazz;
    mr_atomic_t flags;
    mr_uint16_t type;
    mr_int16_t state;
    mr_ptr_t data;
    mr_ptr_t priv;
    mr_ptr_t ops;
} mr_driver_t;

/* Driver entry type */
typedef struct mr_driver_entry {
    const char *name;
    mr_driver_t *drv;
} mr_driver_entry_t;

/**
 * @brief This macro function initializes driver clazz.
 *
 * @param _name The driver clazz name.
 * @param _init The init function.
 * @param _deinit The deinit function.
 * @param _release The release function.
 */
#define MR_DRIVER_CLAZZ_INIT(_name, _init, _deinit, _release)                  \
    {                                                                          \
        .name = (_name),                                                       \
        .init = (_init),                                                       \
        .deinit = (_deinit),                                                   \
        .release = (_release),                                                 \
    }

/**
 * @brief This function initializes driver clazz.
 *
 * @param dclazz The driver clazz.
 * @param name The driver clazz name.
 * @param init The init function.
 * @param deinit The deinit function.
 * @param release The release function.
 */
MR_INLINE void mr_driver_clazz_init(mr_driver_clazz_t *dclazz, const char *name,
                                    mr_driver_method_t *init,
                                    mr_driver_method_t *deinit,
                                    mr_driver_release_t *release) {
    /* Check parameter */
    MR_ASSERT(dclazz != MR_NULL);

    /* Init clazz */
    dclazz->name = name;
    dclazz->init = init;
    dclazz->deinit = deinit;
    dclazz->release = release;
}

/**
 * @brief This macro function initializes driver clazzes.
 *
 * @param _name The driver clazz name.
 * @param _init The init function.
 * @param _deinit The deinit function.
 * @param _release The release function.
 */
#define MR_DRIVER_CLAZZES_INIT(_name, _init, _deinit, _release)                \
    {                                                                          \
        .statik = MR_DRIVER_CLAZZ_INIT((_name), (_init), (_deinit), MR_NULL),  \
        .dynamic = MR_DRIVER_CLAZZ_INIT((_name), (_init), (_deinit),           \
                                        (_release)),                           \
    }

/**
 * @brief This macro function initializes driver clazzes.
 *
 * @param _name The driver clazz name.
 * @param _init The init function.
 * @param _deinit The deinit function.
 * @param _release The release function.
 */
#define MR_DRIVER_CLAZZES_DEFINE(_name, _init, _deinit, _release)              \
    const mr_driver_clazzes_t MR_CAT(__mr_driver_clazzes_, _name)              \
        = MR_DRIVER_CLAZZES_INIT(MR_STR(_name), (_init), (_deinit),            \
                                 (_release))

/**
 * @brief This macro function exports driver clazzes.
 *
 * @param _name The driver clazz name.
 */
#define MR_DRIVER_CLAZZES_EXPORT(_name)                                        \
    extern const mr_driver_clazzes_t MR_CAT(__mr_driver_clazzes_, _name)

/**
 * @brief This macro function imports driver clazzes.
 *
 * @param _name The driver clazz name.
 */
#define MR_DRIVER_CLAZZES_IMPORT(_name) MR_DRIVER_CLAZZES_EXPORT(_name)

/**
 * @brief This macro function finds driver clazzes.
 *
 * @param _name The driver clazz name.
 * @return The driver clazzes.
 */
#define MR_DRIVER_CLAZZES_FIND(_name)   (&MR_CAT(__mr_driver_clazzes_, _name))

/**
 * @brief This macro function gets the static clazz from driver clazzes by name.
 *
 * @param _name The driver clazzes name.
 * @return The static driver clazz.
 */
#define MR_DRIVER_CLAZZES_STATIK(_name)                                        \
    (&MR_DRIVER_CLAZZES_FIND(_name)->statik)

/**
 * @brief This macro function gets the dynamic clazz from driver clazzes by
 * name.
 *
 * @param _name The driver clazzes name.
 * @return The dynamic driver clazz.
 */
#define MR_DRIVER_CLAZZES_DYNAMIC(_name)                                       \
    (&MR_DRIVER_CLAZZES_FIND(_name)->dynamic)

/**
 * @brief This macro function initializes a driver.
 *
 * @param _drv The driver.
 * @param _dclazz The driver clazz.
 * @param _type The driver type.
 * @param _data The driver data.
 * @param _priv The private data.
 * @param _ops The private operations.
 */
#define MR_DRIVER_INIT(_drv, _dclazz, _type, _data, _priv, _ops)               \
    {                                                                          \
        .parent = MR_OBJECT_INIT(MR_CLAZZES_STATIK(driver)),                   \
        .clazz = (_dclazz),                                                    \
        .flags = MR_ATOMIC_INIT(0),                                            \
        .type = (_type),                                                       \
        .state = 0,                                                            \
        .data = (mr_ptr_t)(_data),                                             \
        .priv = (mr_ptr_t)(_priv),                                             \
        .ops = (mr_ptr_t)(_ops),                                               \
    }

/**
 * @brief This macro function defines a driver.
 *
 * @param _name The driver name.
 * @param _dclazz The driver clazz.
 * @param _type The driver type.
 * @param _data The driver data.
 * @param _priv The private data.
 * @param _ops The private operations.
 */
#define MR_DRIVER_DEFINE(_name, _dclazz, _type, _data, _priv, _ops)            \
    mr_driver_t MR_CAT(__mr_driver_, _name)                                    \
        = MR_DRIVER_INIT(&MR_CAT(__mr_driver_entry_, _name), (_dclazz),        \
                         (_type), (_data), (_priv), (_ops))

/**
 * @brief This macro function exports a driver.
 *
 * @param _name The driver name.
 */
#define MR_DRIVER_EXPORT(_name)                                                \
    extern mr_driver_t MR_CAT(__mr_driver_, _name)

/**
 * @brief This macro function imports a driver.
 *
 * @param _name The driver name.
 */
#define MR_DRIVER_IMPORT(_name)         MR_DRIVER_EXPORT(_name)

/**
 * @brief This macro function finds a driver.
 *
 * @param _name The driver name.
 * @return The driver.
 */
#define MR_DRIVER_FIND(_name)           (&MR_CAT(__mr_driver_, _name))

/**
 * @brief This macro function installs a driver.
 *
 * @param _name The driver name.
 * @param _dclazz The driver clazz.
 * @param _type The driver type.
 * @param _data The driver data.
 * @param _priv The private data.
 * @param _ops The private operations.
 * @param _level The init level.
 */
#define MR_DRIVER_INSTALL(_name, _dclazz, _type, _data, _priv, _ops, _level)   \
    MR_DRIVER_DEFINE(_name, _dclazz, _type, _data, _priv, _ops);               \
    MR_USED const mr_driver_entry_t MR_CAT(__mr_driver_entry_, _name)          \
        MR_SECTION_ALIGN("mr_driver." _level)                                  \
        = {.name = MR_STR(_name), .drv = MR_DRIVER_FIND(_name)}

/**
 * @brief This function initializes a driver.
 *
 * @param drv The driver.
 * @param dclazz The driver clazz.
 * @param type The driver type.
 * @param data The driver data.
 * @param priv The private data.
 * @param ops The private operations.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_driver_init(mr_driver_t *drv, const mr_driver_clazz_t *dclazz,
                        mr_uint16_t type, void *data, void *priv, void *ops);

/**
 * @brief This function creates a driver.
 *
 * @param dclazz The driver clazz.
 * @param type The driver type.
 * @param data The driver data.
 * @param priv The private data.
 * @param ops The private operations.
 * @return The driver on success, MR_NULL on failure.
 */
mr_driver_t *mr_driver_create(const mr_driver_clazz_t *dclazz, mr_uint16_t type,
                              void *data, void *priv, void *ops);

/**
 * @brief This macro function gets the driver type.
 *
 * @param _drv The driver.
 * @return The driver type.
 */
#define MR_DRIVER_TYPE(_drv)            (((mr_driver_t *)(_drv))->type)

/**
 * @brief This macro function checks if a driver is of a type.
 *
 * @param _drv The driver.
 * @param _type The driver type.
 * @return MR_TRUE if the driver is of the type, MR_FALSE otherwise.
 */
#define MR_DRIVER_IS_TYPE(_drv, _type)  (MR_DRIVER_TYPE(_drv) == (_type))

/**
 * @brief This macro function gets the private data.
 *
 * @param _drv The driver.
 * @return The private data.
 */
#define MR_DRIVER_DATA(_drv)            ((void *)((mr_driver_t *)(_drv))->data)

/**
 * @brief This macro function gets the private data.
 *
 * @param _drv The driver.
 * @return The private data.
 */
#define MR_DRIVER_PRIV(_drv)            ((void *)((mr_driver_t *)(_drv))->priv)

/**
 * @brief This macro function gets the private operations.
 *
 * @param _drv The driver.
 * @return The private operations.
 */
#define MR_DRIVER_OPS(_drv)             ((void *)((mr_driver_t *)(_drv))->ops)

/**
 * @brief This function deletes a driver.
 *
 * @param drv The driver.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_driver_del(mr_driver_t *drv);

/**
 * @brief This macro function checks if a driver is initialized.
 *
 * @param _drv The driver.
 * @return MR_TRUE if the driver is initialized, MR_FALSE otherwise.
 */
#define MR_DRIVER_IS_SETUP(_drv)        ((_drv)->flags & MR_DRIVER_FLAG_SETUP)

/**
 * @brief This function sets up a driver.
 *
 * @param drv The driver.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_driver_setup(mr_driver_t *drv);

/**
 * @brief This macro function checks a driver.
 *
 * @param _drv The driver.
 * @param _type The driver major type.
 */
#define MR_DRIVER_ASSERT(_drv, _type)                                          \
    MR_ASSERT(((_drv) != MR_NULL) && MR_OBJECT_IS_INITED(_drv));               \
    MR_ASSERT(MR_OBJECT_IS_CLAZZES((_drv), driver));                           \
    MR_ASSERT(MR_DRIVER_IS_SETUP(_drv));                                       \
    MR_ASSERT((MR_DRIVER_TYPE(_drv) & MR_DRIVER_TYPE_MASK) == (_type))

/**
 * @brief This function checks if a driver is ready.
 *
 * @param drv The driver.
 * @return MR_TRUE if the driver is ready, MR_FALSE otherwise.
 */
mr_bool_t mr_driver_is_ready(mr_driver_t *drv);

/**
 * @brief This function shuts down a driver.
 *
 * @param drv The driver.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_driver_shutdown(mr_driver_t *drv);

/**
 * @brief This function gets a driver type.
 *
 * @param drv The driver.
 * @return The driver type.
 */
MR_INLINE mr_uint16_t mr_driver_type(mr_driver_t *drv) {
    /* Check parameter */
    MR_ASSERT((drv != MR_NULL) && MR_OBJECT_IS_INITED(drv));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(drv, driver));

    /* Get driver type */
    return (mr_uint16_t)drv->type;
}

/**
 * @brief This function sets a driver state.
 *
 * @param drv The driver.
 * @param errcode The error code.
 */
MR_INLINE void mr_driver_state_set(mr_driver_t *drv, mr_err_t errcode) {
    /* Check parameter */
    MR_ASSERT((drv != MR_NULL) && MR_OBJECT_IS_INITED(drv));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(drv, driver));
    MR_ASSERT(errcode <= 0);

    /* Set driver state */
    drv->state = (mr_int16_t)errcode;
}

/**
 * @brief This function gets a driver state.
 *
 * @param drv The driver.
 * @return The driver state.
 */
MR_INLINE mr_err_t mr_driver_state(mr_driver_t *drv) {
    /* Check parameter */
    MR_ASSERT((drv != MR_NULL) && MR_OBJECT_IS_INITED(drv));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(drv, driver));

    /* Get driver state */
    return (mr_err_t)drv->state;
}

/**
 * @brief This function finds a driver.
 *
 * @param name The driver name.
 * @return The driver on success, MR_NULL on failure.
 *
 * @note The driver must be exported.
 */
mr_driver_t *mr_driver_find(const char *name);

/**
 * @brief This function gets a driver.
 *
 * @param drv The driver.
 * @return The driver on success, MR_NULL on failure.
 */
MR_INLINE mr_driver_t *mr_driver_get(mr_driver_t *drv) {
    /* Get driver reference */
    return (mr_driver_t *)mr_object_get((mr_object_t *)drv);
}

/**
 * @brief This function puts a driver.
 *
 * @param drv The driver.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_driver_put(mr_driver_t *drv) {
    /* Put driver reference */
    return mr_object_put((mr_object_t *)drv);
}

/* ================================ HardTree ================================ */

/**
 * @brief This macro function checks if the driver instance exist.
 *
 * @return MR_TRUE if the driver instance exist, MR_FALSE otherwise.
 */
#define MR_HT_HAS_DRIVER()              MR_HT_HAS_INST(MR_HT_COMPAT)

/**
 * @brief This macro function gets the number of driver instances.
 *
 * @return The number of driver instances.
 */
#define MR_HT_DRIVER_NUM()              MR_HT_INST_NUM(MR_HT_COMPAT)

/**
 * @brief This macro function gets the driver instance.
 *
 * @param _id The instance id.
 * @return The driver instance.
 */
#define MR_HT_DRIVER(_id)               MR_HT_INST(MR_HT_COMPAT, _id)

/**
 * @brief This macro function iterates over all driver instances.
 *
 * @param _fn The function to be called, like: fn(i, ...), where i is the index.
 */
#define MR_HT_DRIVER_FOREACH(_fn, ...)                                         \
    MR_HT_INST_FOREACH(MR_HT_COMPAT, _fn, ##__VA_ARGS__)

/**
 * @brief This macro function checks if the driver property exists.
 *
 * @param _id The instance id.
 * @param _prop The property name.
 * @return MR_TRUE if the property exists, MR_FALSE otherwise.
 */
#define MR_HT_DRIVER_HAS_PROP(_id, _prop)                                      \
    MR_HT_NODE_HAS_PROP(MR_HT_DRIVER(_id), _prop)

/**
 * @brief This macro function gets the property from the driver instance.
 *
 * @param _id The instance id.
 * @param _prop The property name.
 * @return The property value.
 */
#define MR_HT_DRIVER_PROP(_id, _prop)                                          \
    MR_HT_NODE_PROP(MR_HT_DRIVER(_id), _prop)

/**
 * @brief This macro function gets the property from the driver instance or
 * returns a default value.
 *
 * @param _id The instance id.
 * @param _prop The property name.
 * @param _default The default value.
 * @return The property value or the default value.
 */
#define MR_HT_DRIVER_PROP_OR(_id, _prop, _default)                             \
    MR_HT_NODE_PROP_OR(MR_HT_DRIVER(_id), _prop, _default)

/**
 * @brief This macro function checks if an indexed element of a driver property
 * array exists.
 *
 * @param _id The instance id.
 * @param _prop The property name.
 * @param _elem The element name.
 * @param _idx The index into the property array.
 * @return MR_TRUE if the indexed element exists, MR_FALSE otherwise.
 */
#define MR_HT_DRIVER_HAS_PROP_ELEM(_id, _prop, _elem, _idx)                    \
    MR_HT_NODE_HAS_PROP_ELEM(MR_HT_DRIVER(_id), _prop, _elem, _idx)

/**
 * @brief This macro function gets an indexed element from a driver property
 * array.
 *
 * @param _id The instance id.
 * @param _prop The property name.
 * @param _elem The element name.
 * @param _idx The index into the property array.
 * @return The indexed property value.
 */
#define MR_HT_DRIVER_PROP_ELEM(_id, _prop, _elem, _idx)                        \
    MR_HT_NODE_PROP_ELEM(MR_HT_DRIVER(_id), _prop, _elem, _idx)

/**
 * @brief This macro function gets an indexed element from a driver property
 * array or returns a default value.
 *
 * @param _id The instance id.
 * @param _prop The property name.
 * @param _elem The element name.
 * @param _idx The index into the property array.
 * @param _default The default value.
 * @return The indexed property value or the default value.
 */
#define MR_HT_DRIVER_PROP_ELEM_OR(_id, _prop, _elem, _idx, _default)           \
    MR_HT_NODE_PROP_ELEM_OR(MR_HT_DRIVER(_id), _prop, _elem, _idx, _default)

/**
 * @brief This macro function iterates over all indexed elements of a driver
 * property array.
 *
 * @param _id The instance id.
 * @param _prop The property name.
 * @param _fn The function to be called, like: fn(i, ...), where i is the index.
 * @param ... The arguments to be passed to the function.
 */
#define MR_HT_DRIVER_PROP_ELEM_FOREACH(_id, _prop, _fn, ...)                   \
    MR_HT_NODE_PROP_ELEM_FOREACH(MR_HT_DRIVER(_id), _prop, _fn, ##__VA_ARGS__)

/**
 * @brief This macro function gets the driver instance name.
 *
 * @param _id The instance id.
 * @return The driver instance name.
 */
#define MR_HT_DRIVER_NAME(_id)          MR_HT_NODE_NAME(MR_HT_DRIVER(_id))

/**
 * @brief This macro function gets the driver instance path.
 *
 * @param _id The instance id.
 * @return The driver instance path.
 */
#define MR_HT_DRIVER_PATH(_id)          MR_HT_NODE_PATH(MR_HT_DRIVER(_id))

/**
 * @brief This macro function checks if the driver instance has a parent.
 *
 * @param _id The instance id.
 * @return MR_TRUE if the driver instance has a parent, MR_FALSE otherwise.
 */
#define MR_HT_DRIVER_HAS_PARENT(_id)    MR_HT_NODE_HAS_PARENT(MR_HT_DRIVER(_id))

/**
 * @brief This macro function gets the driver instance parent.
 *
 * @param _id The instance id.
 * @return The driver instance parent.
 */
#define MR_HT_DRIVER_PARENT(_id)        MR_HT_NODE_PARENT(MR_HT_DRIVER(_id))

/**
 * @brief This macro function checks if the driver instance has an address.
 *
 * @param _id The instance id.
 * @return MR_TRUE if the driver instance has an address, MR_FALSE otherwise.
 */
#define MR_HT_DRIVER_HAS_ADDR(_id)      MR_HT_NODE_HAS_ADDR(MR_HT_DRIVER(_id))

/**
 * @brief This macro function gets the driver instance address.
 *
 * @param _id The instance id.
 * @return The driver instance address.
 */
#define MR_HT_DRIVER_ADDR(_id)          MR_HT_NODE_ADDR(MR_HT_DRIVER(_id))

/**
 * @brief This macro function installs a driver instance through hardtree.
 *
 * @param _id The instance id.
 * @param _dclazz The driver clazz.
 * @param _type The driver type.
 * @param _data The driver data.
 * @param _priv The private data.
 * @param _ops The private operations.
 */
#define MR_HT_DRIVER_INSTALL(_id, _dclazz, _type, _data, _priv, _ops)          \
    MR_DRIVER_INSTALL(                                                         \
        MR_HT_DRIVER_NAME(_id), _dclazz, _type, _data, _priv, _ops,            \
        MR_HT_DRIVER_PROP_OR(_id, INIT_LEVEL, MR_DRIVER_LEVEL_DEFER))

/**
 * @brief This macro function finds a driver through hardtree.
 *
 * @param _node The node.
 * @return The driver.
 */
#define MR_HT_DRIVER_FIND(_node)        MR_DRIVER_FIND(MR_HT_NODE_NAME(_node))

/* All driver instances defined through hardtree are exported here, and
 * accessing these drivers no longer requires import or export */
#define __MR_HT_DRIVER_EXPORT(_id)                                             \
    MR_DRIVER_EXPORT(MR_HT_NODE_NAME(MR_HT_INST(DRIVER, _id)))
MR_HT_INST_FOREACH(DRIVER, __MR_HT_DRIVER_EXPORT);
#endif /* defined(MR_USE_DRIVER) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_H__ */

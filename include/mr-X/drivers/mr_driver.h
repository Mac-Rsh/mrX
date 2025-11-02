/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_DRIVER_H__
#define __MR_DRIVER_H__

#include <mr_config.h>
#if defined(MR_USE_DRIVER)
#include <mr-X/drivers/mr_hardtree.h>
#include <mr-X/kernel/mr_object.h>
#if defined(MR_USE_DRIVER_IRQ)
#include <mr-X/kernel/mr_wait.h>
#endif /* defined(MR_USE_DRIVER_IRQ) */
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

/* Driver type definition */
enum mr_driver_type {
    MR_DRIVER_TYPE_NONE = 0,
    MR_DRIVER_TYPE_GPIO = 0x0100,
    MR_DRIVER_TYPE_UART = 0x0200,
    MR_DRIVER_TYPE_MASK = 0xff00,
};

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

/* Driver operations type */
typedef struct mr_driver_dops {
    mr_err_t (*init)(const struct mr_driver *drv);
    mr_err_t (*deinit)(const struct mr_driver *drv);
} mr_driver_dops_t;

/* Driver operationses type(naming is just weird) */
typedef struct mr_driver_dopses {
    const mr_driver_dops_t statik;
    const mr_driver_dops_t dynamic;
} mr_driver_dopses_t;

/* Driver type */
MR_CLAZZES_EXPORT(driver);
typedef struct mr_driver {
    mr_object_t parent;
    mr_atomic_t flags;
    mr_uint16_t type;
    mr_int16_t state;
    const mr_driver_dops_t *dops;
    mr_ptr_t data;
    mr_ptr_t priv;
    mr_ptr_t ops;
} mr_driver_t;

/* Driver entry type */
typedef struct mr_driver_entry {
    const char *name;
    mr_driver_t *driver;
} mr_driver_entry_t;

/**
 * @brief This macro function initializes a driver operations.
 *
 * @param _init The init function.
 * @param _deinit The deinit function.
 */
#define MR_DRIVER_DOPS_INIT(_init, _deinit)                                    \
    {.init = (_init), .deinit = (_deinit)}

/**
 * @brief This macro function initializes driver operationses.
 *
 * @param _type The driver type.
 * @param _init The init function.
 * @param _statik_deinit The static deinit function.
 * @param _dynamic_deinit The dynamic deinit function.
 */
#define MR_DRIVER_DOPSES_DEFINE(_type, _init, _statik_deinit, _dynamic_deinit) \
    const mr_driver_dopses_t MR_CAT(__mr_driver_dopses_, _type)                \
        = {.statik = MR_DRIVER_DOPS_INIT(_init, _statik_deinit),               \
           .dynamic = MR_DRIVER_DOPS_INIT(_init, _dynamic_deinit)}

/**
 * @brief This macro function gets the static driver operations of driver
 * operationses.
 *
 * @param _dopses The driver operationses.
 * @return The static driver operations.
 */
#define MR_DRIVER_DOPSES_STATIK(_dopses)                                       \
    (&(_dopses)->statik)

/**
 * @brief This macro function gets the dynamic driver operations of driver
 * operationses.
 *
 * @param _dopses The driver operationses.
 * @return The dynamic driver operations.
 */
#define MR_DRIVER_DOPSES_DYNAMIC(_dopses)                                      \
    (&(_dopses)->dynamic)

/**
 * @brief This macro function exports driver operationses.
 *
 * @param _type The driver type.
 */
#define MR_DRIVER_DOPSES_EXPORT(_type)                                         \
    extern const mr_driver_dopses_t MR_CAT(__mr_driver_dopses_, _type)

/**
 * @brief This macro function finds driver operationses.
 *
 * @param _type The driver type.
 * @return The driver operationses.
 */
#define MR_DRIVER_DOPSES_FIND(_type)    (&MR_CAT(__mr_driver_dopses_, _type))

/**
 * @brief This macro function initializes a driver.
 *
 * @param _drv The driver.
 * @param _type The driver type.
 * @param _dops The driver operations.
 * @param _data The driver data.
 * @param _priv The private data.
 * @param _ops The private operations.
 */
#define MR_DRIVER_INIT(_drv, _type, _dops, _data, _priv, _ops)                 \
    {.parent = MR_OBJECT_INIT(MR_CLAZZES_STATIK(MR_CLAZZES_FIND(driver))),     \
     .flags = MR_ATOMIC_INIT(0),                                               \
     .type = (_type),                                                          \
     .state = 0,                                                               \
     .dops = (_dops),                                                          \
     .data = (_data),                                                          \
     .priv = (_priv),                                                          \
     .ops = (_ops)}

/**
 * @brief This macro function defines a driver.
 *
 * @param _name The driver name.
 * @param _type The driver type.
 * @param _dops The driver operations.
 * @param _data The driver data.
 * @param _priv The private data.
 * @param _ops The private operations.
 */
#define MR_DRIVER_DEFINE(_name, _type, _dops, _data, _priv, _ops)              \
    mr_driver_t MR_CAT(__mr_driver_, _name)                                    \
        = MR_DRIVER_INIT(&MR_CAT(__mr_driver_entry_, _name), (_type), (_dops), \
                         (_data), (_priv), (_ops))

/**
 * @brief This macro function exports a driver.
 *
 * @param _name The driver name.
 */
#define MR_DRIVER_EXPORT(_name)                                                \
    extern mr_driver_t MR_CAT(__mr_driver_, _name)

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
 * @param _type The driver type.
 * @param _dops The driver operations.
 * @param _data The driver data.
 * @param _priv The private data.
 * @param _ops The private operations.
 * @param _level The init level.
 */
#define MR_DRIVER_INSTALL(_name, _type, _dops, _data, _priv, _ops, _level)     \
    MR_DRIVER_DEFINE(_name, _type, _dops, _data, _priv, _ops);                 \
    MR_USED const mr_driver_entry_t MR_CAT(__mr_driver_entry_, _name)          \
        MR_SECTION_ALIGN("mr_driver." _level)                                  \
        = {.name = MR_STR(_name), .driver = MR_DRIVER_FIND(_name)}

/**
 * @brief This macro function imports a driver.
 *
 * @param _name The driver name.
 *
 * @note Why is there a separate import interface? You don't want to deal with
 * complex underlying inclusion relationships either, do you.
 */
#define MR_DRIVER_IMPORT(_name)                                                \
    extern mr_driver_t MR_CAT(__mr_driver_, _name)

/**
 * @brief This function initializes a driver.
 *
 * @param drv The driver.
 * @param type The driver type.
 * @param dops The driver operations.
 * @param data The driver data.
 * @param priv The private data.
 * @param ops The private operations.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_driver_init(mr_driver_t *drv, mr_uint16_t type,
                        const mr_driver_dops_t *dops, void *data, void *priv,
                        void *ops);

/**
 * @brief This function creates a driver.
 *
 * @param type The driver type.
 * @param dops The driver operations.
 * @param data The driver data.
 * @param priv The private data.
 * @param ops The private operations.
 * @return The driver on success, MR_NULL on failure.
 */
mr_driver_t *mr_driver_create(mr_uint16_t type, const mr_driver_dops_t *dops,
                              void *data, void *priv, void *ops);

/**
 * @brief This macro function gets the driver type.
 *
 * @param _drv The driver.
 * @return The driver type.
 */
#define MR_DRIVER_TYPE(_drv)                                                   \
    (((mr_driver_t *)(_drv))->type & MR_DRIVER_TYPE_MASK)

/**
 * @brief This macro function checks if a driver is of a type.
 *
 * @param _drv The driver.
 * @param _type The driver type.
 * @return MR_TRUE if the driver is of the type, MR_FALSE otherwise.
 */
#define MR_DRIVER_IS_TYPE(_drv, _type)                                         \
    (MR_DRIVER_TYPE(_drv) == ((_type) & MR_DRIVER_TYPE_MASK))

/**
 * @brief This macro function gets the private data.
 *
 * @param _drv The driver.
 * @return The private data.
 */
#define MR_DRIVER_DATA(_drv)            ((void *)((mr_driver_t *)(_drv))->data)

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
 * @param _type The driver type.
 */
#define MR_DRIVER_ASSERT(_drv, _type)                                          \
    MR_ASSERT(((_drv) != MR_NULL) && MR_OBJECT_IS_INITED(_drv));               \
    MR_ASSERT(MR_OBJECT_IS_CLAZZES((_drv), driver));                           \
    MR_ASSERT(MR_DRIVER_IS_SETUP(_drv));                                       \
    MR_ASSERT(MR_DRIVER_IS_TYPE((_drv), (_type)))

/**
 * @brief This function shuts down a driver.
 *
 * @param drv The driver.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_driver_shutdown(mr_driver_t *drv);

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

#if defined(MR_USE_DRIVER_HARDTREE)
/**
 * @brief This macro function checks whether the driver instance exist.
 *
 * @return MR_TRUE if the driver instance exist, MR_FALSE otherwise.
 */
#define MR_HT_HAS_INST_DRIVER()         MR_HT_HAS_INST(MR_HT_COMPAT)

/**
 * @brief This macro function gets the driver instance.
 *
 * @param _id The instance id.
 * @return The driver instance.
 */
#define MR_HT_INST_DRIVER(_id)          MR_HT_INST(MR_HT_COMPAT, _id)

/**
 * @brief This macro function installs a driver instance through hardtree.
 *
 * @param _id The instance id.
 * @param _type The driver type.
 * @param _dops The driver operations.
 * @param _data The driver data.
 * @param _priv The private data.
 * @param _ops The private operations.
 */
#define MR_HT_INST_DRIVER_INSTALL(_id, _type, _dops, _data, _priv, _ops)       \
    MR_DRIVER_INSTALL(MR_HT_NODE_NAME(MR_HT_INST_DRIVER(_id)), _type, _dops,   \
                      _data, _priv, _ops,                                      \
                      MR_HT_NODE_PROP_OR(MR_HT_INST_DRIVER(_id), INIT_LEVEL,   \
                                         MR_DRIVER_LEVEL_DEFER))

/**
 * @brief This macro function iterates over all driver instances.
 *
 * @param _fn The function to be called, like: fn(i, ...), where i is the index.
 */
#define MR_HT_INST_DRIVER_FOREACH(_fn, ...)                                    \
    MR_HT_INST_FOREACH(MR_HT_COMPAT, _fn, ##__VA_ARGS__)

/**
 * @brief This macro function finds a driver through hardtree.
 *
 * @param _node The node.
 * @return The driver.
 */
#define MR_HT_DRIVER_FIND(_node)        MR_DRIVER_FIND(MR_HT_NODE_NAME(_node))

/* All driver instances defined through hardtree are exported here, and
 * accessing these drivers no longer requires import or export */
#define __MR_HT_INST_DRIVER_EXPORT(_id)                                        \
    MR_DRIVER_EXPORT(MR_HT_NODE_NAME(MR_HT_INST(DRIVER, _id)))
MR_HT_INST_FOREACH(DRIVER, __MR_HT_INST_DRIVER_EXPORT);
#endif /* defined(MR_USE_DRIVER_HARDTREE) */
#endif /* defined(MR_USE_DRIVER) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_H__ */

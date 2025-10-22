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
#include <drivers/mr_hardtree.h>
#include <mr-X/mr_object.h>
#if defined(MR_USE_DRIVER_IRQ)
#include <mr-X/mr_wait.h>
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
    MR_DRIVER_TYPE_MASK = 0xff00,
};

/* Driver flag definition */
#define MR_DRIVER_FLAG_SETUP            (1U << 0)
#define MR_DRIVER_FLAG_LOCK             (1U << 1)
#define MR_DRIVER_FLAG_DYING            (1U << 2)

/* Driver level definition */
#define MR_DRIVER_LEVEL_EARLY           "1"
#define MR_DRIVER_LEVEL_NORMAL          "2"
#define MR_DRIVER_LEVEL_LAST            "3"
#define MR_DRIVER_LEVEL_DEFER           "9"

/* Driver operations type */
typedef struct mr_driver_dops {
    mr_err_t (*init)(const struct mr_driver *drv);
    mr_err_t (*deinit)(const struct mr_driver *drv);
} mr_driver_dops_t;

/* Driver type */
MR_CLAZZ_EXPORT(driver);
typedef struct mr_driver {
    mr_object_t parent;
    mr_atomic_t flags;
    mr_uint16_t type;
    mr_uint8_t state;
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
 * @param _init The initialization function.
 * @param _deinit The deinitialization function.
 */
#define MR_DRIVER_DOPS_INIT(_init, _deinit)                                    \
    {.init = (_init), .deinit = (_deinit)}

/**
 * @brief This macro function defines a driver operations.
 *
 * @param _type The driver type.
 * @param _init The initialization function.
 * @param _deinit The deinitialization function.
 */
#define MR_DRIVER_DOPS_DEFINE(_type, _init, _deinit)                           \
    const mr_driver_dops_t MR_CAT(__mr_driver_dops_, _type)                    \
        = MR_DRIVER_DOPS_INIT(_init, _deinit)

/**
 * @brief This macro function defines a dynamic driver operations.
 *
 * @param _type The driver type.
 * @param _init The initialization function.
 * @param _deinit The deinitialization function.
 */
#define MR_DRIVER_DOPS_DYNAMIC_DEFINE(_type, _init, _deinit)                   \
    const mr_driver_dops_t MR_CAT(__mr_driver_dops_dynamic_, _type)            \
        = MR_DRIVER_DOPS_INIT(_init, _deinit)

/**
 * @brief This macro function exports a driver operations.
 *
 * @param _type The driver type.
 */
#define MR_DRIVER_DOPS_EXPORT(_type)                                           \
    extern const mr_driver_dops_t MR_CAT(__mr_driver_dops_, _type)

/**
 * @brief This macro function exports a dynamic driver operations.
 *
 * @param _type The driver type.
 */
#define MR_DRIVER_DOPS_DYNAMIC_EXPORT(_type)                                   \
    extern const mr_driver_dops_t MR_CAT(__mr_driver_dops_dynamic_, _type)

/**
 * @brief This macro function finds a driver operations.
 *
 * @param _type The driver type.
 * @return The driver operations.
 */
#define MR_DRIVER_DOPS_FIND(_type)      (&MR_CAT(__mr_driver_dops_, _type))

/**
 * @brief This macro function gets a dynamic driver operations.
 *
 * @param _type The driver type.
 * @return The dynamic driver operations.
 */
#define MR_DRIVER_DOPS_DYNAMIC_FIND(_type)                                     \
    (&MR_CAT(__mr_driver_dops_dynamic_, _type))

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
    {.parent = MR_OBJECT_INIT(MR_CLAZZ_FIND(driver)),                          \
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
 *
 * @note This interface mainly provides a function that supports dynamic search.
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
    MR_ASSERT(MR_OBJECT_CLAZZ_IS((_drv), driver));                             \
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
void mr_driver_state_set(mr_driver_t *drv, mr_err_t errcode);

/**
 * @brief This function gets a driver state.
 *
 * @param drv The driver.
 * @return The driver state.
 */
mr_err_t mr_driver_state(mr_driver_t *drv);

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
 * @brief This macro function finds a driver through hardtree.
 *
 * @param _node The node.
 * @return The driver.
 */
#define MR_DRIVER_HT_FIND(_node)        MR_DRIVER_FIND(MR_HT_NODE_NAME(_node))

/**
 * @brief This macro function installs a driver through hardtree.
 *
 * @param _id The instance id.
 * @param _type The driver type.
 * @param _dops The driver operations.
 * @param _data The driver data.
 * @param _priv The private data.
 * @param _ops The private operations.
 */
#define MR_DRIVER_HT_INSTALL(_id, _type, _dops, _data, _priv, _ops)            \
    MR_DRIVER_INSTALL(MR_HT_INST_NAME(MR_HT_COMPAT, _id), _type, _dops, _data, \
                      _priv, _ops, MR_HT_INST_INIT_LEVEL(MR_HT_COMPAT, _id))

/**
 * @brief This macro function installs all drivers through hardtree.
 *
 * @param _fn The function.
 * @param ... The arguments.
 */
#define MR_DRIVER_HT_INSTALL_FOREACH(_fn, ...)                                 \
    MR_HT_INST_FOREACH(MR_HT_COMPAT, _fn, __VA_ARGS__)

/**
 * @brief This macro function imports a driver through hardtree.
 *
 * @param _node The node.
 */
#define MR_DRIVER_HT_IMPORT(_node)      MR_DRIVER_IMPORT(MR_HT_NODE_NAME(_node))

/**
 * @brief This macro function gets the node from the instance id.
 *
 * @param _id The instance id.
 * @return The node.
 */
#define MR_DRIVER_HT_INST(_id)          MR_HT_INST(MR_HT_COMPAT, _id)
#endif /* defined(MR_USE_DRIVER_HARDTREE) */
#endif /* defined(MR_USE_DRIVER) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_H__ */

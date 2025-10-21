/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_DRIVER_HARDTREE_H__
#define __MR_DRIVER_HARDTREE_H__

#include <drivers/mr_driver.h>
#if defined(MR_USE_DRIVER_HARDTREE)
#include <mr-X/mr_listify.h>
#include <mr_hardgen.h>
#endif /* defined(MR_USE_DRIVER_HARDTREE) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup HardTree
 * @{
 */

/* The implementation mechanism of HT is based on 'zephyr' */
#if defined(MR_USE_DRIVER_HARDTREE)
/**
 * @brief This macro function checks whether the path exists.
 *
 * @param ... The node path.
 * @return MR_TRUE if the path exists, MR_FALSE otherwise.
 */
#define MR_HT_HAS_PATH(...)                                                    \
    MR_MACRO_IS_USED(MR_CAT(MR_HT_PATH(__VA_ARGS__), _EXIST))

/**
 * @brief This macro function gets the node from the path.
 *
 * @param ... The node path.
 * @return The node.
 */
#define MR_HT_PATH(...)                                                        \
    MR_CAT(MR_HT_NODE, MR_CAT_SEP(_S_, __VA_ARGS__))

/**
 * @brief This macro function checks whether the label exists.
 *
 * @param _name The label name.
 * @return MR_TRUE if the label exists, MR_FALSE otherwise.
 */
#define MR_HT_HAS_LABEL(_name)                                                 \
    MR_MACRO_IS_USED(MR_CAT(MR_HT_LABEL(_name), _EXIST))

/**
 * @brief This macro function gets the node from the label.
 *
 * @param _name The label name.
 * @return The node.
 */
#define MR_HT_LABEL(_name)              MR_CAT(MR_HT_LABEL, MR_CAT(_, _name))

/**
 * @brief This macro function checks whether the property exists.
 *
 * @param _node The node.
 * @param _prop The property name.
 * @return MR_TRUE if the property exists, MR_FALSE otherwise.
 */
#define MR_HT_HAS_PROP(_node, _prop)                                           \
    MR_MACRO_IS_USED(MR_CAT(_node, MR_CAT(_P_, _prop), _EXIST))

/**
 * @brief This macro function gets the property from the node.
 *
 * @param _node The node.
 * @param _prop The property name.
 * @return The property value.
 */
#define MR_HT_PROP(_node, _prop)        MR_CAT(_node, MR_CAT(_P_, _prop))

/**
 * @brief This macro function gets the property from the node or returns a
 * default value.
 *
 * @param _node The node.
 * @param _prop The property name.
 * @param _default The default value.
 * @return The property value or the default value.
 */
#define MR_HT_PROP_OR(_node, _prop, _default)                                  \
    MR_MACRO_IF(MR_HT_HAS_PROP(_node, _prop), MR_HT_PROP(_node, _prop),        \
                _default)

/**
 * @brief This macro function checks whether the alias exists.
 *
 * @param _name The alias name.
 * @return MR_TRUE if the alias exists, MR_FALSE otherwise.
 */
#define MR_HT_HAS_ALIAS(_name)                                                 \
    MR_MACRO_IS_USED(MR_CAT(MR_HT_ALIAS(_name), _EXIST))

/**
 * @brief This macro function gets the node from the alias.
 *
 * @param _name The alias name.
 * @return The node.
 */
#define MR_HT_ALIAS(_name)              MR_CAT(MR_HT_ALIAS, MR_CAT(_, _name))

/**
 * @brief This macro function checks whether the instance exists.
 *
 * @param _compat The compatible name.
 * @return MR_TRUE if the instance exists, MR_FALSE otherwise.
 */
#define MR_HT_HAS_INST(_compat)                                                \
    MR_MACRO_IS_USED(MR_CAT(MR_HT_INST_, _compat, _EXIST))

/**
 * @brief This macro function gets the node from the name.
 *
 * @param _compat The compatible name.
 * @param _id The instance id.
 * @return The node.
 */
#define MR_HT_INST(_compat, _id)   MR_CAT(MR_HT_INST_, _compat, _, _id)

/**
 * @brief This macro function iterates over all instances of the compatible
 * name.
 *
 * @param _compat The compatible name.
 * @param _fn The function to be called, like: fn(i, ...), where i is the index.
 * @param ... The arguments to be passed to the function.
 */
#define MR_HT_INST_FOREACH(_compat, _fn, ...)                                  \
    MR_MACRO_IF(MR_HT_HAS_INST(_compat),                                       \
                (MR_CAT(MR_HT_INST_, _compat)(_fn, __VA_ARGS__)), ())
#endif /* defined(MR_USE_DRIVER_HARDTREE) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_HARDTREE_H__ */

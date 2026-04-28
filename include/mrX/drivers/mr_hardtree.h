/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_DRIVER_HARDTREE_H__
#define __MR_DRIVER_HARDTREE_H__

#include <mrX/kernel/mr_listify.h>
#include <mrX/mr_hardgen.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup HardTree
 * @{
 */

/* The implementation mechanism of HT is based on 'zephyr' */

/**
 * @brief This macro function checks if the path exists.
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
    MR_CAT(__MR_HT_N, MR_CAT_SEP(_S_, ##__VA_ARGS__))

/**
 * @brief This macro function checks if the label exists.
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
#define MR_HT_LABEL(_name)              MR_HT_PATH(LABEL, _name)

/**
 * @brief This macro function checks if the alias exists.
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
#define MR_HT_ALIAS(_name)              MR_HT_PATH(ALIAS, _name)

/**
 * @brief This macro function checks if the instances exist.
 *
 * @param _compat The compatible name.
 * @return MR_TRUE if the instance exists, MR_FALSE otherwise.
 */
#define MR_HT_HAS_INST(_compat)                                                \
    MR_MACRO_IS_USED(MR_CAT(MR_HT_PATH(INST, _compat), _EXIST))

/**
 * @brief This macro function gets the number of instances for the compatible
 * name.
 *
 * @param _compat The compatible name.
 * @return The number of instances.
 */
#define MR_HT_INST_NUM(_compat)         MR_CAT(MR_HT_PATH(INST, _compat), _NUM)

/**
 * @brief This macro function gets the node from the compatible name.
 *
 * @param _compat The compatible name.
 * @param _id The instance id.
 * @return The node.
 */
#define MR_HT_INST(_compat, _id)        MR_HT_PATH(INST, _compat, _id)

/**
 * @brief This macro function iterates over all instances of the compatible
 * name.
 *
 * @param _compat The compatible name.
 * @param _fn The function to be called, like: fn(i, ...), where i is the index.
 * @param ... The arguments to be passed to the function.
 */
#define MR_HT_INST_FOREACH(_compat, _fn, ...)                                  \
    MR_MACRO_IF(                                                               \
        MR_HT_HAS_INST(_compat),                                               \
        (MR_LISTIFY(MR_HT_INST_NUM(_compat), (;), _fn, ##__VA_ARGS__)), ())

/**
 * @brief This macro function checks if the property exists.
 *
 * @param _node The node.
 * @param _prop The property name.
 * @return MR_TRUE if the property exists, MR_FALSE otherwise.
 */
#define MR_HT_NODE_HAS_PROP(_node, _prop)                                      \
    MR_MACRO_IS_USED(MR_CAT(_node, MR_CAT_SEP(_P_, _prop), _EXIST))

/**
 * @brief This macro function gets the property from the node.
 *
 * @param _node The node.
 * @param _prop The property name.
 * @return The property value.
 */
#define MR_HT_NODE_PROP(_node, _prop)   MR_CAT(_node, MR_CAT_SEP(_P_, _prop))

/**
 * @brief This macro function gets the property from the node or returns a
 * default value.
 *
 * @param _node The node.
 * @param _prop The property name.
 * @param _default The default value.
 * @return The property value or the default value.
 */
#define MR_HT_NODE_PROP_OR(_node, _prop, _default)                             \
    MR_MACRO_IF(MR_HT_NODE_HAS_PROP(_node, _prop),                             \
                (MR_HT_NODE_PROP(_node, _prop)), (_default))

/**
 * @brief This macro function checks if an indexed element of a property
 * array exists.
 *
 * @param _node The node.
 * @param _prop The property name.
 * @param _elem The element name.
 * @param _idx The index into the property array.
 * @return MR_TRUE if the indexed element exists, MR_FALSE otherwise.
 */
#define MR_HT_NODE_HAS_PROP_ELEM(_node, _prop, _elem, _idx)                    \
    MR_HT_NODE_HAS_PROP(_node, MR_CAT(_prop, _E_, _elem, _I_, _idx))

/**
 * @brief This macro function gets an indexed element from a property array.
 *
 * @param _node The node.
 * @param _prop The property name.
 * @param _elem The element name.
 * @param _idx The index into the property array.
 * @return The indexed property value.
 */
#define MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, _idx)                        \
    MR_HT_NODE_PROP(_node, MR_CAT(_prop, _E_, _elem, _I_, _idx))

/**
 * @brief This macro function gets an indexed element from a property array or
 * returns a default value.
 *
 * @param _node The node.
 * @param _prop The property name.
 * @param _elem The element name.
 * @param _idx The index into the property array.
 * @param _default The default value.
 * @return The indexed property value or the default value.
 */
#define MR_HT_NODE_PROP_ELEM_OR(_node, _prop, _elem, _idx, _default)           \
    MR_MACRO_IF(MR_HT_NODE_HAS_PROP_ELEM(_node, _prop, _elem, _idx),           \
                (MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, _idx)), (_default))

/**
 * @brief This macro function iterates over all indexed elements of a property
 * array.
 *
 * @param _node The node.
 * @param _prop The property name.
 * @param _fn The function to be called, like: fn(i, ...), where i is the index.
 * @param ... The arguments to be passed to the function.
 */
#define MR_HT_NODE_PROP_ELEM_FOREACH(_node, _prop, _fn, ...)                   \
    MR_LISTIFY_NEST(MR_CAT(_node, _P_, _prop, _E, _NUM), (, ), _fn, _node,     \
                    _prop, ##__VA_ARGS__)

/**
 * @brief This macro function gets the node name.
 *
 * @param _node The node.
 * @return The node name.
 */
#define MR_HT_NODE_NAME(_node)          MR_HT_NODE_PROP(_node, NAME)

/**
 * @brief This macro function gets the node path.
 *
 * @param _node The node.
 * @return The node path.
 */
#define MR_HT_NODE_PATH(_node)          MR_HT_NODE_PROP(_node, PATH)

/**
 * @brief This macro function checks if the node has a parent.
 *
 * @param _node The node.
 * @return MR_TRUE if the node has a parent, MR_FALSE otherwise.
 */
#define MR_HT_NODE_HAS_PARENT(_node)    MR_HT_NODE_HAS_PROP(_node, PARENT)

/**
 * @brief This macro function gets the node parent.
 *
 * @param _node The node.
 * @return The node parent.
 */
#define MR_HT_NODE_PARENT(_node)        MR_HT_NODE_PROP(_node, PARENT)

/**
 * @brief This macro function checks if the node has an address.
 *
 * @param _node The node.
 * @return MR_TRUE if the node has an address, MR_FALSE otherwise.
 */
#define MR_HT_NODE_HAS_ADDR(_node)      MR_HT_NODE_HAS_PROP(_node, ADDR)

/**
 * @brief This macro function gets the node address.
 *
 * @param _node The node.
 * @return The node address.
 */
#define MR_HT_NODE_ADDR(_node)          MR_HT_NODE_PROP(_node, ADDR)

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_DRIVER_HARDTREE_H__ */

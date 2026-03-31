/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-03-26        OpenCode       First version
 */

#ifndef __MR_HASHMAP_H__
#define __MR_HASHMAP_H__

#include <mrX/kernel/mr_list.h>
#include <mrX/libc/mr_errno.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Hashmap
 * @{
 */

/* Hashmap type */
typedef struct mr_hashmap {
    mr_list_t *buckets;
    mr_uint32_t bucket_num;
} mr_hashmap_t;

/* Hash node type */
typedef struct mr_hashnode {
    mr_list_t list;
    mr_uint32_t key;
    void *value;
} mr_hashnode_t;

/**
 * @brief This function initializes a hashmap.
 *
 * @param map The hashmap.
 * @param buckets The buckets.
 * @param bucket_num The bucket number.
 */
MR_INLINE void mr_hashmap_init(mr_hashmap_t *map, mr_list_t *buckets,
                               mr_uint32_t bucket_num) {
    mr_uint32_t i;

    /* Init buckets */
    for (i = 0; i < bucket_num; i++) {
        mr_list_init(&buckets[i]);
    }

    /* Init hashmap */
    map->bucket_num = bucket_num;
    map->buckets = buckets;
}

/**
 * @brief This function adds a node to a hashmap.
 *
 * @param map The hashmap.
 * @param node The hash node.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_hashmap_add(mr_hashmap_t *map, mr_hashnode_t *node);

/**
 * @brief This function deletes a node from a hashmap.
 *
 * @param node The hash node.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_hashmap_del(mr_hashnode_t *node);

/**
 * @brief This function finds a node from a hashmap.
 *
 * @param map The hashmap.
 * @param key The node key.
 * @return The hash node on success, MR_NULL on failure.
 */
mr_hashnode_t *mr_hashmap_find(mr_hashmap_t *map, mr_uint32_t key);

/**
 * @brief This function checks if a hashmap is empty.
 *
 * @param map The hashmap.
 * @return MR_TRUE on empty, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_hashmap_is_empty(mr_hashmap_t *map) {
    mr_uint32_t i;

    /* Check if any bucket is not empty */
    for (i = 0; i < map->bucket_num; i++) {
        if (mr_list_is_empty(&map->buckets[i]) == MR_FALSE) {
            return MR_FALSE;
        }
    }
    return MR_TRUE;
}

/**
 * @brief This macro function initializes a hash node.
 *
 * @param _node The hash node.
 * @param _key The node key.
 * @param _value The node value.
 */
#define MR_HASHNODE_INIT(_node, _key, _value)                                  \
    {                                                                          \
        .list = MR_LIST_INIT(&(_node)->list),                                  \
        .key = (_key),                                                         \
        .value = (_value),                                                     \
    }

/**
 * @brief This function initializes a hash node.
 *
 * @param node The hash node.
 * @param key The node key.
 * @param value The node value.
 */
MR_INLINE void mr_hashnode_init(mr_hashnode_t *node, mr_uint32_t key,
                                void *value) {
    /* Init node */
    mr_list_init(&node->list);
    node->key = key;
    node->value = value;
}

/**
 * @brief This macro function checks if a hash node is added.
 *
 * @param _node The hash node.
 * @return MR_TRUE if the hash node is added, MR_FALSE otherwise.
 */
#define MR_HASHNODE_IS_ADDED(_node)     (!mr_list_is_empty(&(_node)->list))

/**
 * @brief This function gets a hash node key.
 *
 * @param node The hash node.
 * @return The hash node key.
 *
 * @note The key of an added node must not be modified directly.
 */
MR_INLINE mr_uint32_t mr_hashnode_key(mr_hashnode_t *node) {
    /* Return node key */
    return node->key;
}

/**
 * @brief This function sets a hash node value.
 *
 * @param node The hash node.
 * @param value The node value.
 */
MR_INLINE void mr_hashnode_value_set(mr_hashnode_t *node, void *value) {
    /* Set node value */
    node->value = value;
}

/**
 * @brief This function gets a hash node value.
 *
 * @param node The hash node.
 * @return The hash node value.
 */
MR_INLINE void *mr_hashnode_value(mr_hashnode_t *node) {
    /* Return node value */
    return node->value;
}

/**
 * @brief This macro function iterates through the hashmap.
 *
 * @param _node The hash node pointer (mr_hashnode_t *).
 * @param _bi The bucket index variable (mr_uint32_t).
 * @param _map The hashmap.
 */
#define MR_HASHMAP_FOR_EACH(_node, _bi, _map)                                   \
    for ((_bi) = 0; (_bi) < (_map)->bucket_num; (_bi)++)                        \
        for ((_node) = MR_LIST_ENTRY((_map)->buckets[_bi].next,                 \
                                     mr_hashnode_t, list);                      \
             &(_node)->list != &(_map)->buckets[_bi];                           \
             (_node) = MR_LIST_ENTRY((_node)->list.next, mr_hashnode_t, list))

/**
 * @brief This macro function iterates through the hashmap in safe order.
 *
 * @param _node The hash node pointer (mr_hashnode_t *).
 * @param _next The next hash node pointer (mr_hashnode_t *).
 * @param _bi The bucket index variable (mr_uint32_t).
 * @param _map The hashmap.
 *
 * @note This macro is safe to delete the current node in the loop body.
 */
#define MR_HASHMAP_FOR_EACH_SAFE(_node, _next, _bi, _map)                       \
    for ((_bi) = 0; (_bi) < (_map)->bucket_num; (_bi)++)                        \
        for ((_node) = MR_LIST_ENTRY((_map)->buckets[_bi].next,                 \
                                     mr_hashnode_t, list),                      \
             (_next) = MR_LIST_ENTRY((_node)->list.next, mr_hashnode_t, list);  \
             &(_node)->list != &(_map)->buckets[_bi];                           \
             (_node) = (_next),                                                  \
             (_next) = MR_LIST_ENTRY((_node)->list.next, mr_hashnode_t, list))

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_HASHMAP_H__ */

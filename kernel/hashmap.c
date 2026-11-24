/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/kernel/mr_hashmap.h>

static mr_uint32_t hashmap_hash(mr_uint32_t key) {
    return key * 2654435761U;
}

static mr_list_t *hashmap_bucket(mr_hashmap_t *map, mr_uint32_t hash) {
    /* Use bitmask when bucket_num is a power of two */
    if ((map->bucket_num & (map->bucket_num - 1U)) == 0U) {
        return &map->buckets[hash & (map->bucket_num - 1U)];
    }

    /* Get bucket */
    return &map->buckets[hash % map->bucket_num];
}

static mr_hashnode_t *hashmap_find(mr_list_t *bucket, mr_uint32_t key) {
    mr_hashnode_t *node;
    mr_list_t *l;

    /* Find node in bucket */
    MR_LIST_FOR_EACH(l, bucket) {
        node = MR_LIST_ENTRY(l, mr_hashnode_t, list);
        if (node->key == key) {
            return node;
        }
    }
    return MR_NULL;
}

mr_hashnode_t *mr_hashmap_find(mr_hashmap_t *map, mr_uint32_t key) {
    /* Check parameter */
    if (map->bucket_num == 0) {
        return MR_NULL;
    }

    /* Lookup node */
    return hashmap_find(hashmap_bucket(map, hashmap_hash(key)), key);
}

mr_err_t mr_hashmap_add(mr_hashmap_t *map, mr_hashnode_t *node) {
    mr_list_t *bucket;

    /* Check parameter */
    if (map->bucket_num == 0) {
        return -MR_EINVAL;
    }

    /* Check whether node is added */
    if (MR_HASHNODE_IS_ADDED(node) == MR_TRUE) {
        return -MR_EBUSY;
    }

    /* Check whether key is exist */
    bucket = hashmap_bucket(map, hashmap_hash(node->key));
    if (hashmap_find(bucket, node->key) != MR_NULL) {
        return -MR_EEXIST;
    }

    /* Add node */
    mr_list_add(bucket, &node->list);
    return 0;
}

mr_err_t mr_hashmap_del(mr_hashnode_t *node) {
    /* Check whether node exists */
    if (MR_HASHNODE_IS_ADDED(node) == MR_FALSE) {
        return -MR_ENOENT;
    }

    /* Delete node */
    mr_list_del(&node->list);
    return 0;
}

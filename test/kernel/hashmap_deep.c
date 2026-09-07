/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_hashmap.h>
#include <mrX/libc/mr_string.h>

/* Constants */

#define TEST_DEEP_BUCKET_NUM 4

/* Helpers */

typedef struct test_deep_item {
    mr_hashnode_t node;
    int value;
} test_deep_item_t;

/* Replicated hash function of the hashmap implementation */
static mr_uint32_t test_deep_hash(mr_uint32_t key) {
    return key * 2654435761U;
}

/* Bucket index of a key for a given bucket number */
static mr_uint32_t test_deep_bucket(mr_uint32_t key, mr_uint32_t bucket_num) {
    return test_deep_hash(key) % bucket_num;
}

/* Find the first key greater than start hashing to the same bucket */
static mr_uint32_t test_deep_next_collision(mr_uint32_t start,
                                            mr_uint32_t bucket_num) {
    mr_uint32_t key;

    for (key = start + 1U; key != start; key++) {
        if (test_deep_bucket(key, bucket_num)
            == test_deep_bucket(start, bucket_num)) {
            return key;
        }
    }
    return start;
}

/* Count every node reachable through the iteration macro */
static int test_deep_count(mr_hashmap_t *map) {
    mr_hashnode_t *node;
    mr_uint32_t bi;
    int count;

    count = 0;
    MR_HASHMAP_FOR_EACH(node, bi, map) {
        count++;
    }
    return count;
}

/* Tests */

/* Test: every node is linked into the bucket its key hashes to */
MR_TEST(hashmap, bucket_index_matches_hash) {
    mr_list_t buckets[8];
    mr_hashmap_t map;
    test_deep_item_t items[20];
    mr_hashnode_t *node;
    mr_uint32_t bi;
    mr_uint32_t i;

    mr_hashmap_init(&map, buckets, MR_ARRAY_SIZE(buckets));
    for (i = 0; i < MR_ARRAY_SIZE(items); i++) {
        items[i].value = (int)i;
        mr_hashnode_init(&items[i].node, i + 1U, &items[i].value);
        MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &items[i].node), 0);
    }

    MR_HASHMAP_FOR_EACH(node, bi, &map) {
        MR_TEST_ASSERT_EQ_UINT(test_deep_bucket(mr_hashnode_key(node),
                                                MR_ARRAY_SIZE(buckets)), bi);
    }
    MR_TEST_ASSERT_EQ_INT(test_deep_count(&map), 20);
}

/* Test: two distinct keys colliding in one bucket coexist and are found */
MR_TEST(hashmap, collision_pair_coexist) {
    mr_list_t buckets[5];
    mr_hashmap_t map;
    test_deep_item_t a;
    test_deep_item_t b;
    mr_uint32_t k1;
    mr_uint32_t k2;

    mr_hashmap_init(&map, buckets, MR_ARRAY_SIZE(buckets));
    k1 = 1;
    k2 = test_deep_next_collision(k1, MR_ARRAY_SIZE(buckets));
    MR_TEST_ASSERT_NE_UINT(k2, k1);
    MR_TEST_EXPECT_EQ_UINT(test_deep_bucket(k1, MR_ARRAY_SIZE(buckets)),
                           test_deep_bucket(k2, MR_ARRAY_SIZE(buckets)));

    mr_hashnode_init(&a.node, k1, &a.value);
    mr_hashnode_init(&b.node, k2, &b.value);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &a.node), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &b.node), 0);

    MR_TEST_ASSERT_EQ_PTR(mr_hashmap_find(&map, k1), &a.node);
    MR_TEST_ASSERT_EQ_PTR(mr_hashmap_find(&map, k2), &b.node);
    MR_TEST_ASSERT_EQ_INT(test_deep_count(&map), 2);
}

/* Test: nodes appended to a colliding bucket keep insertion order */
MR_TEST(hashmap, collision_chain_order_tail_append) {
    mr_list_t buckets[5];
    mr_hashmap_t map;
    test_deep_item_t a;
    test_deep_item_t b;
    mr_hashnode_t *node;
    mr_uint32_t k1;
    mr_uint32_t k2;
    mr_uint32_t bucket;

    mr_hashmap_init(&map, buckets, MR_ARRAY_SIZE(buckets));
    k1 = 1;
    k2 = test_deep_next_collision(k1, MR_ARRAY_SIZE(buckets));
    bucket = test_deep_bucket(k1, MR_ARRAY_SIZE(buckets));

    mr_hashnode_init(&a.node, k1, &a.value);
    mr_hashnode_init(&b.node, k2, &b.value);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &a.node), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &b.node), 0);

    /* Walk the colliding bucket directly through typed entries */
    node = MR_LIST_ENTRY(buckets[bucket].next, mr_hashnode_t, list);
    MR_TEST_ASSERT_EQ_UINT(mr_hashnode_key(node), k1);
    node = MR_LIST_ENTRY(node->list.next, mr_hashnode_t, list);
    MR_TEST_ASSERT_EQ_UINT(mr_hashnode_key(node), k2);
    MR_TEST_ASSERT_EQ_PTR(node->list.next, &buckets[bucket]);
}

/* Test: deleting the middle of a three node collision chain keeps ends */
MR_TEST(hashmap, collision_chain_del_middle) {
    mr_list_t buckets[5];
    mr_hashmap_t map;
    test_deep_item_t a;
    test_deep_item_t b;
    test_deep_item_t c;
    mr_uint32_t k1;
    mr_uint32_t k2;
    mr_uint32_t k3;

    mr_hashmap_init(&map, buckets, MR_ARRAY_SIZE(buckets));
    k1 = 1;
    k2 = test_deep_next_collision(k1, MR_ARRAY_SIZE(buckets));
    k3 = test_deep_next_collision(k2, MR_ARRAY_SIZE(buckets));

    mr_hashnode_init(&a.node, k1, &a.value);
    mr_hashnode_init(&b.node, k2, &b.value);
    mr_hashnode_init(&c.node, k3, &c.value);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &a.node), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &b.node), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &c.node), 0);

    MR_TEST_ASSERT_EQ_INT(mr_hashmap_del(&b.node), 0);
    MR_TEST_ASSERT_EQ_PTR(mr_hashmap_find(&map, k1), &a.node);
    MR_TEST_ASSERT_EQ_PTR(mr_hashmap_find(&map, k2), MR_NULL);
    MR_TEST_ASSERT_EQ_PTR(mr_hashmap_find(&map, k3), &c.node);
    MR_TEST_ASSERT_EQ_INT(test_deep_count(&map), 2);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_is_empty(&map), MR_FALSE);
}

/* Test: a failed duplicate add leaves the map unchanged */
MR_TEST(hashmap, duplicate_add_failure_no_side_effect) {
    mr_list_t buckets[TEST_DEEP_BUCKET_NUM];
    mr_hashmap_t map;
    test_deep_item_t a;
    test_deep_item_t b;

    mr_hashmap_init(&map, buckets, MR_ARRAY_SIZE(buckets));
    mr_hashnode_init(&a.node, 3, &a.value);
    mr_hashnode_init(&b.node, 3, &b.value);

    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &a.node), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &b.node), -MR_EEXIST);
    MR_TEST_ASSERT_EQ_INT(MR_HASHNODE_IS_ADDED(&b.node), MR_FALSE);
    MR_TEST_ASSERT_EQ_PTR(mr_hashmap_find(&map, 3), &a.node);
    MR_TEST_ASSERT_EQ_INT(test_deep_count(&map), 1);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_is_empty(&map), MR_FALSE);
}

/* Test: deleting an already deleted node reports no entry */
MR_TEST(hashmap, del_twice_second_enoent) {
    mr_list_t buckets[TEST_DEEP_BUCKET_NUM];
    mr_hashmap_t map;
    test_deep_item_t item;

    mr_hashmap_init(&map, buckets, MR_ARRAY_SIZE(buckets));
    mr_hashnode_init(&item.node, 5, &item.value);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &item.node), 0);

    MR_TEST_ASSERT_EQ_INT(mr_hashmap_del(&item.node), 0);
    MR_TEST_EXPECT_EQ_INT(mr_hashmap_del(&item.node), -MR_ENOENT);
    MR_TEST_ASSERT_EQ_PTR(mr_hashmap_find(&map, 5), MR_NULL);
    MR_TEST_ASSERT_EQ_INT(test_deep_count(&map), 0);
}

/* Test: a node removed from one map can join another map */
MR_TEST(hashmap, move_node_between_maps) {
    mr_list_t buckets1[TEST_DEEP_BUCKET_NUM];
    mr_list_t buckets2[TEST_DEEP_BUCKET_NUM];
    mr_hashmap_t map1;
    mr_hashmap_t map2;
    test_deep_item_t item;

    mr_hashmap_init(&map1, buckets1, MR_ARRAY_SIZE(buckets1));
    mr_hashmap_init(&map2, buckets2, MR_ARRAY_SIZE(buckets2));
    mr_hashnode_init(&item.node, 8, &item.value);

    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map1, &item.node), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_del(&item.node), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map2, &item.node), 0);
    MR_TEST_ASSERT_EQ_PTR(mr_hashmap_find(&map1, 8), MR_NULL);
    MR_TEST_ASSERT_EQ_PTR(mr_hashmap_find(&map2, 8), &item.node);
    MR_TEST_ASSERT_EQ_INT(test_deep_count(&map1), 0);
    MR_TEST_ASSERT_EQ_INT(test_deep_count(&map2), 1);
}

/* Test: extreme uint32 keys are found with no sign or overflow trouble */
MR_TEST(hashmap, key_extremes) {
    mr_list_t buckets[8];
    mr_hashmap_t map;
    test_deep_item_t items[5];
    mr_uint32_t keys[5];
    mr_hashnode_t *node;
    mr_uint32_t i;

    keys[0] = 0U;
    keys[1] = 1U;
    keys[2] = 0x7FFFFFFFU;
    keys[3] = 0x80000000U;
    keys[4] = 0xFFFFFFFFU;
    mr_hashmap_init(&map, buckets, MR_ARRAY_SIZE(buckets));

    for (i = 0; i < 5; i++) {
        items[i].value = (int)i;
        mr_hashnode_init(&items[i].node, keys[i], &items[i].value);
        MR_TEST_EXPECT_EQ_INT(mr_hashmap_add(&map, &items[i].node), 0);
        MR_TEST_EXPECT_LT_UINT(test_deep_bucket(keys[i], MR_ARRAY_SIZE(buckets)),
                               MR_ARRAY_SIZE(buckets));
    }
    for (i = 0; i < 5; i++) {
        node = mr_hashmap_find(&map, keys[i]);
        MR_TEST_ASSERT_EQ_PTR(node, &items[i].node);
        MR_TEST_ASSERT_EQ_UINT(mr_hashnode_key(node), keys[i]);
    }
    MR_TEST_ASSERT_EQ_INT(test_deep_count(&map), 5);
}

/* Test: a node may change its key while detached and rejoin */
MR_TEST(hashmap, readd_after_del_with_new_key) {
    mr_list_t buckets[TEST_DEEP_BUCKET_NUM];
    mr_hashmap_t map;
    test_deep_item_t item;

    mr_hashmap_init(&map, buckets, MR_ARRAY_SIZE(buckets));
    mr_hashnode_init(&item.node, 100, &item.value);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &item.node), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_del(&item.node), 0);

    item.node.key = 200;
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &item.node), 0);
    MR_TEST_ASSERT_EQ_PTR(mr_hashmap_find(&map, 100), MR_NULL);
    MR_TEST_ASSERT_EQ_PTR(mr_hashmap_find(&map, 200), &item.node);
    MR_TEST_ASSERT_EQ_INT(test_deep_count(&map), 1);
}

/* Test: a zero initialized node is rejected as already added */
MR_TEST(hashmap, add_zeroed_node) {
    mr_list_t buckets[TEST_DEEP_BUCKET_NUM];
    mr_hashmap_t map;
    test_deep_item_t item;

    mr_hashmap_init(&map, buckets, MR_ARRAY_SIZE(buckets));
    mr_memset(&item, 0, sizeof(item));

    /* Potential bug: a zeroed node has list next NULL so it is reported as
       busy instead of invalid, which is safe but arguably misleading */
    MR_TEST_EXPECT_EQ_INT(mr_hashmap_add(&map, &item.node), -MR_EBUSY);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_is_empty(&map), MR_TRUE);
}

/* Test: lookup only inspects the bucket of the replicated hash */
MR_TEST(hashmap, find_only_searches_hash_bucket) {
    mr_list_t buckets[TEST_DEEP_BUCKET_NUM];
    mr_hashmap_t map;
    test_deep_item_t item;
    mr_uint32_t bucket;
    mr_uint32_t wrong;

    mr_hashmap_init(&map, buckets, MR_ARRAY_SIZE(buckets));
    mr_hashnode_init(&item.node, 1, &item.value);

    /* Place the node into a bucket its key does not hash to */
    bucket = test_deep_bucket(1, TEST_DEEP_BUCKET_NUM);
    wrong = (bucket + 1U) % TEST_DEEP_BUCKET_NUM;
    mr_list_add(&buckets[wrong], &item.node.list);

    MR_TEST_EXPECT_EQ_INT(MR_HASHNODE_IS_ADDED(&item.node), MR_TRUE);
    MR_TEST_EXPECT_EQ_PTR(mr_hashmap_find(&map, 1), MR_NULL);
    MR_TEST_ASSERT_EQ_INT(test_deep_count(&map), 1);

    MR_TEST_ASSERT_EQ_INT(mr_hashmap_del(&item.node), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &item.node), 0);
    MR_TEST_ASSERT_EQ_PTR(mr_hashmap_find(&map, 1), &item.node);
}

/* Test: a single bucket map chains every key */
MR_TEST(hashmap, single_bucket_map) {
    mr_list_t buckets[1];
    mr_hashmap_t map;
    test_deep_item_t items[5];
    mr_hashnode_t *node;
    mr_uint32_t bi;
    mr_uint32_t i;
    int count;

    mr_hashmap_init(&map, buckets, MR_ARRAY_SIZE(buckets));
    for (i = 0; i < MR_ARRAY_SIZE(items); i++) {
        items[i].value = (int)i;
        mr_hashnode_init(&items[i].node, i + 10U, &items[i].value);
        MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &items[i].node), 0);
    }
    MR_TEST_ASSERT_EQ_INT(test_deep_count(&map), 5);

    MR_TEST_ASSERT_EQ_INT(mr_hashmap_del(&items[1].node), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_del(&items[3].node), 0);
    count = 0;
    MR_HASHMAP_FOR_EACH(node, bi, &map) {
        MR_TEST_EXPECT_NE_UINT(mr_hashnode_key(node), 11U);
        MR_TEST_EXPECT_NE_UINT(mr_hashnode_key(node), 13U);
        count++;
    }
    MR_TEST_ASSERT_EQ_INT(count, 3);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_is_empty(&map), MR_FALSE);
}

/* Test: iteration macros run zero times on an empty map */
MR_TEST(hashmap, foreach_empty_map) {
    mr_list_t buckets[3];
    mr_hashmap_t map;
    mr_hashnode_t *node;
    mr_hashnode_t *next;
    mr_uint32_t bi;
    int count;

    mr_hashmap_init(&map, buckets, MR_ARRAY_SIZE(buckets));
    count = 0;
    MR_HASHMAP_FOR_EACH(node, bi, &map) {
        count++;
    }
    MR_TEST_ASSERT_EQ_INT(count, 0);

    count = 0;
    MR_HASHMAP_FOR_EACH_SAFE(node, next, bi, &map) {
        count++;
    }
    MR_TEST_ASSERT_EQ_INT(count, 0);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_is_empty(&map), MR_TRUE);
}

/* Test: safe iteration can delete every node including chain tails */
MR_TEST(hashmap, foreach_safe_delete_all) {
    mr_list_t buckets[TEST_DEEP_BUCKET_NUM];
    mr_hashmap_t map;
    test_deep_item_t items[9];
    mr_hashnode_t *node;
    mr_hashnode_t *next;
    mr_uint32_t bi;
    mr_uint32_t i;
    int count;

    mr_hashmap_init(&map, buckets, MR_ARRAY_SIZE(buckets));
    for (i = 0; i < MR_ARRAY_SIZE(items); i++) {
        items[i].value = (int)i;
        mr_hashnode_init(&items[i].node, i + 1U, &items[i].value);
        MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &items[i].node), 0);
    }

    MR_HASHMAP_FOR_EACH_SAFE(node, next, bi, &map) {
        MR_TEST_ASSERT_EQ_INT(mr_hashmap_del(node), 0);
    }

    MR_TEST_ASSERT_EQ_INT(mr_hashmap_is_empty(&map), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(test_deep_count(&map), 0);
    count = 0;
    for (i = 0; i < MR_ARRAY_SIZE(items); i++) {
        if (mr_hashmap_find(&map, i + 1U) != MR_NULL) {
            count++;
        }
    }
    MR_TEST_ASSERT_EQ_INT(count, 0);
}

/* Test: normal iteration walks buckets strictly in index order */
MR_TEST(hashmap, foreach_bucket_order) {
    mr_list_t buckets[TEST_DEEP_BUCKET_NUM];
    mr_hashmap_t map;
    test_deep_item_t items[12];
    mr_hashnode_t *node;
    mr_uint32_t bi;
    mr_uint32_t i;
    mr_uint32_t prev_bucket;
    mr_uint32_t this_bucket;

    mr_hashmap_init(&map, buckets, MR_ARRAY_SIZE(buckets));
    for (i = 0; i < MR_ARRAY_SIZE(items); i++) {
        items[i].value = (int)i;
        mr_hashnode_init(&items[i].node, i, &items[i].value);
        MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &items[i].node), 0);
    }

    prev_bucket = 0;
    MR_HASHMAP_FOR_EACH(node, bi, &map) {
        this_bucket = test_deep_bucket(mr_hashnode_key(node),
                                       TEST_DEEP_BUCKET_NUM);
        MR_TEST_ASSERT_EQ_UINT(this_bucket, bi);
        MR_TEST_EXPECT_GE_UINT(this_bucket, prev_bucket);
        prev_bucket = this_bucket;
    }
    MR_TEST_ASSERT_EQ_INT(test_deep_count(&map), 12);
}

/* Test: a failed add does not stop a later add of the same node */
MR_TEST(hashmap, readd_after_eexist) {
    mr_list_t buckets[TEST_DEEP_BUCKET_NUM];
    mr_hashmap_t map;
    test_deep_item_t a;
    test_deep_item_t b;

    mr_hashmap_init(&map, buckets, MR_ARRAY_SIZE(buckets));
    mr_hashnode_init(&a.node, 21, &a.value);
    mr_hashnode_init(&b.node, 21, &b.value);

    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &a.node), 0);
    MR_TEST_EXPECT_EQ_INT(mr_hashmap_add(&map, &b.node), -MR_EEXIST);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_del(&a.node), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &b.node), 0);
    MR_TEST_ASSERT_EQ_PTR(mr_hashmap_find(&map, 21), &b.node);
    MR_TEST_ASSERT_EQ_INT(test_deep_count(&map), 1);
}

/* Test: a larger map holds many keys with no cross talk */
MR_TEST(hashmap, many_keys_stress) {
    mr_list_t buckets[8];
    mr_hashmap_t map;
    test_deep_item_t items[64];
    mr_hashnode_t *node;
    mr_uint32_t i;

    mr_hashmap_init(&map, buckets, MR_ARRAY_SIZE(buckets));
    for (i = 0; i < MR_ARRAY_SIZE(items); i++) {
        items[i].value = (int)(i * 3);
        mr_hashnode_init(&items[i].node, i * 7U, &items[i].value);
        MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &items[i].node), 0);
    }
    MR_TEST_ASSERT_EQ_INT(test_deep_count(&map), 64);

    for (i = 0; i < MR_ARRAY_SIZE(items); i++) {
        node = mr_hashmap_find(&map, i * 7U);
        MR_TEST_ASSERT_EQ_PTR(node, &items[i].node);
        MR_TEST_ASSERT_EQ_PTR(mr_hashnode_value(node), &items[i].value);
    }

    for (i = 0; i < MR_ARRAY_SIZE(items); i++) {
        MR_TEST_ASSERT_EQ_INT(mr_hashmap_del(&items[i].node), 0);
    }
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_is_empty(&map), MR_TRUE);
    MR_TEST_ASSERT_EQ_PTR(mr_hashmap_find(&map, 0), MR_NULL);
}

#endif /* defined(MR_USE_TEST) */

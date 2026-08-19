/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-03-26        OpenCode       First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_hashmap.h>

/* Constants */

#define TEST_HASHMAP_BUCKET_NUM 4

/* Helpers */

typedef struct test_hashmap_item {
    mr_hashnode_t node;
    int value;
} test_hashmap_item_t;

static void hashmap_init_local(mr_hashmap_t *map, mr_list_t *buckets) {
    mr_hashmap_init(map, buckets, TEST_HASHMAP_BUCKET_NUM);
}

/* Tests */

MR_TEST(hashmap, init) {
    mr_hashmap_t map;
    mr_list_t buckets[3];

    mr_hashmap_init(&map, buckets, MR_ARRAY_SIZE(buckets));
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_is_empty(&map), MR_TRUE);
    MR_TEST_ASSERT_EQ_PTR(mr_hashmap_find(&map, 1), MR_NULL);
}

MR_TEST(hashmap, add_lookup_del) {
    mr_list_t buckets[TEST_HASHMAP_BUCKET_NUM];
    mr_hashmap_t map;
    test_hashmap_item_t item;
    mr_hashnode_t *node;

    hashmap_init_local(&map, buckets);
    mr_hashnode_init(&item.node, 7, &item.value);
    item.value = 70;

    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &item.node), 0);
    MR_TEST_ASSERT_EQ_INT(MR_HASHNODE_IS_ADDED(&item.node), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_is_empty(&map), MR_FALSE);

    node = mr_hashmap_find(&map, 7);
    MR_TEST_ASSERT_EQ_PTR(node, &item.node);
    MR_TEST_ASSERT_EQ_UINT(mr_hashnode_key(node), 7);
    MR_TEST_ASSERT_EQ_PTR(mr_hashnode_value(node), &item.value);

    MR_TEST_ASSERT_EQ_INT(mr_hashmap_del(&item.node), 0);
    MR_TEST_ASSERT_EQ_INT(MR_HASHNODE_IS_ADDED(&item.node), MR_FALSE);
    MR_TEST_ASSERT_EQ_PTR(mr_hashmap_find(&map, 7), MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_is_empty(&map), MR_TRUE);
}

MR_TEST(hashmap, duplicate_key) {
    mr_list_t buckets[TEST_HASHMAP_BUCKET_NUM];
    mr_hashmap_t map;
    test_hashmap_item_t item1;
    test_hashmap_item_t item2;

    hashmap_init_local(&map, buckets);
    mr_hashnode_init(&item1.node, 1, &item1.value);
    mr_hashnode_init(&item2.node, 1, &item2.value);

    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &item1.node), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &item2.node), -MR_EEXIST);
}

MR_TEST(hashmap, duplicate_node) {
    mr_list_t buckets[TEST_HASHMAP_BUCKET_NUM];
    mr_hashmap_t map;
    test_hashmap_item_t item;

    hashmap_init_local(&map, buckets);
    mr_hashnode_init(&item.node, 2, &item.value);

    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &item.node), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &item.node), -MR_EBUSY);
}

MR_TEST(hashmap, collision_lookup) {
    mr_list_t buckets[TEST_HASHMAP_BUCKET_NUM];
    mr_hashmap_t map;
    test_hashmap_item_t items[8];
    mr_hashnode_t *node;
    mr_uint32_t i;

    hashmap_init_local(&map, buckets);
    for (i = 0; i < MR_ARRAY_SIZE(items); i++) {
        items[i].value = (int)(i + 10);
        mr_hashnode_init(&items[i].node, i, &items[i].value);
        MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &items[i].node), 0);
    }

    for (i = 0; i < MR_ARRAY_SIZE(items); i++) {
        node = mr_hashmap_find(&map, i);
        MR_TEST_ASSERT_EQ_PTR(node, &items[i].node);
        MR_TEST_ASSERT_EQ_PTR(mr_hashnode_value(node), &items[i].value);
    }
}

MR_TEST(hashmap, readd_after_del) {
    mr_list_t buckets[TEST_HASHMAP_BUCKET_NUM];
    mr_hashmap_t map;
    test_hashmap_item_t item;

    hashmap_init_local(&map, buckets);
    mr_hashnode_init(&item.node, 9, &item.value);

    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &item.node), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_del(&item.node), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &item.node), 0);
    MR_TEST_ASSERT_EQ_PTR(mr_hashmap_find(&map, 9), &item.node);
}

MR_TEST(hashmap, invalid_operation) {
    test_hashmap_item_t item1;
    mr_hashmap_t map1;
    mr_list_t buckets1[2];
    mr_hashmap_t map2;
    mr_list_t buckets2[2];

    mr_hashmap_init(&map1, buckets1, MR_ARRAY_SIZE(buckets1));
    mr_hashmap_init(&map2, buckets2, MR_ARRAY_SIZE(buckets2));
    mr_hashnode_init(&item1.node, 11, &item1.value);

    MR_TEST_ASSERT_EQ_INT(mr_hashmap_del(&item1.node), -MR_ENOENT);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map1, &item1.node), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map2, &item1.node), -MR_EBUSY);
}

MR_TEST(hashmap, node_helpers) {
    mr_list_t buckets[TEST_HASHMAP_BUCKET_NUM];
    mr_hashmap_t map;
    int value = 11;
    int next = 22;
    mr_hashnode_t node = MR_HASHNODE_INIT(&node, 11, &value);

    hashmap_init_local(&map, buckets);
    MR_TEST_ASSERT_EQ_INT(MR_HASHNODE_IS_ADDED(&node), MR_FALSE);
    MR_TEST_ASSERT_EQ_UINT(mr_hashnode_key(&node), 11);
    MR_TEST_ASSERT_EQ_PTR(mr_hashnode_value(&node), &value);

    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &node), 0);
    mr_hashnode_value_set(&node, &next);
    MR_TEST_ASSERT_EQ_PTR(mr_hashnode_value(mr_hashmap_find(&map, 11)), &next);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_del(&node), 0);
}

MR_TEST(hashmap, empty_buckets) {
    mr_hashmap_t map;
    test_hashmap_item_t item;

    mr_hashmap_init(&map, MR_NULL, 0);
    mr_hashnode_init(&item.node, 1, &item.value);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_is_empty(&map), MR_TRUE);
    MR_TEST_ASSERT_EQ_PTR(mr_hashmap_find(&map, 1), MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &item.node), -MR_EINVAL);
}

MR_TEST(hashmap, foreach) {
    mr_list_t buckets[TEST_HASHMAP_BUCKET_NUM];
    mr_hashmap_t map;
    test_hashmap_item_t items[8];
    mr_hashnode_t *node;
    mr_uint32_t bi;
    mr_uint32_t i;
    int seen[8];
    int count;

    hashmap_init_local(&map, buckets);
    for (i = 0; i < MR_ARRAY_SIZE(items); i++) {
        seen[i] = 0;
        items[i].value = (int)i;
        mr_hashnode_init(&items[i].node, i, &items[i].value);
        MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &items[i].node), 0);
    }

    count = 0;
    MR_HASHMAP_FOR_EACH(node, bi, &map) {
        MR_TEST_ASSERT_LT_UINT(mr_hashnode_key(node), MR_ARRAY_SIZE(items));
        seen[mr_hashnode_key(node)] = 1;
        count++;
    }
    MR_TEST_ASSERT_EQ_INT(count, 8);
    for (i = 0; i < MR_ARRAY_SIZE(items); i++) {
        MR_TEST_ASSERT_EQ_INT(seen[i], 1);
    }
}

MR_TEST(hashmap, foreach_safe_del) {
    mr_list_t buckets[TEST_HASHMAP_BUCKET_NUM];
    mr_hashmap_t map;
    test_hashmap_item_t items[8];
    mr_hashnode_t *node;
    mr_hashnode_t *next;
    mr_uint32_t bi;
    mr_uint32_t i;
    int count;

    hashmap_init_local(&map, buckets);
    for (i = 0; i < MR_ARRAY_SIZE(items); i++) {
        items[i].value = (int)i;
        mr_hashnode_init(&items[i].node, i, &items[i].value);
        MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&map, &items[i].node), 0);
    }

    MR_HASHMAP_FOR_EACH_SAFE(node, next, bi, &map) {
        if ((mr_hashnode_key(node) % 2U) == 0U) {
            MR_TEST_ASSERT_EQ_INT(mr_hashmap_del(node), 0);
        }
    }

    count = 0;
    MR_HASHMAP_FOR_EACH(node, bi, &map) {
        MR_TEST_ASSERT_EQ_UINT(mr_hashnode_key(node) % 2U, 1U);
        count++;
    }
    MR_TEST_ASSERT_EQ_INT(count, 4);
    MR_TEST_ASSERT_EQ_PTR(mr_hashmap_find(&map, 0), MR_NULL);
    MR_TEST_ASSERT_EQ_PTR(mr_hashmap_find(&map, 1), &items[1].node);
}

#endif /* defined(MR_USE_TEST) */

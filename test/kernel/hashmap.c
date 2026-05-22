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

#define TEST_HASHMAP_BUCKET_NUM         4

/* Helpers */

typedef struct test_hashmap_item {
    mr_hashnode_t node;
    int value;
} test_hashmap_item_t;

static mr_list_t test_hashmap_buckets[TEST_HASHMAP_BUCKET_NUM];
static mr_hashmap_t test_hashmap;

static void hashmap_reset(void) {
    /* Reset hashmap */
    mr_hashmap_init(&test_hashmap, test_hashmap_buckets,
                    TEST_HASHMAP_BUCKET_NUM);
}

/* Tests */

static void test_hashmap_init(void) {
    mr_hashmap_t map;
    mr_list_t buckets[3];

    mr_hashmap_init(&map, buckets, MR_ARRAY_SIZE(buckets));
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_is_empty(&map), MR_TRUE);
    MR_TEST_ASSERT_EQ_PTR(mr_hashmap_find(&map, 1), MR_NULL);
}
MR_TEST_EXPORT(hashmap, init, test_hashmap_init);

static void test_hashmap_add_lookup_del(void) {
    test_hashmap_item_t item;
    mr_hashnode_t *node;

    hashmap_reset();
    mr_hashnode_init(&item.node, 7, &item.value);
    item.value = 70;

    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&test_hashmap, &item.node), 0);
    MR_TEST_ASSERT_EQ_INT(MR_HASHNODE_IS_ADDED(&item.node), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_is_empty(&test_hashmap), MR_FALSE);

    node = mr_hashmap_find(&test_hashmap, 7);
    MR_TEST_ASSERT_EQ_PTR(node, &item.node);
    MR_TEST_ASSERT_EQ_UINT(mr_hashnode_key(node), 7);
    MR_TEST_ASSERT_EQ_PTR(mr_hashnode_value(node), &item.value);

    MR_TEST_ASSERT_EQ_INT(mr_hashmap_del(&item.node), 0);
    MR_TEST_ASSERT_EQ_INT(MR_HASHNODE_IS_ADDED(&item.node), MR_FALSE);
    MR_TEST_ASSERT_EQ_PTR(mr_hashmap_find(&test_hashmap, 7), MR_NULL);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_is_empty(&test_hashmap), MR_TRUE);
}
MR_TEST_EXPORT(hashmap, add_lookup_del, test_hashmap_add_lookup_del);

static void test_hashmap_duplicate_key(void) {
    test_hashmap_item_t item1;
    test_hashmap_item_t item2;

    hashmap_reset();
    mr_hashnode_init(&item1.node, 1, &item1.value);
    mr_hashnode_init(&item2.node, 1, &item2.value);

    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&test_hashmap, &item1.node), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&test_hashmap, &item2.node),
                          -MR_EEXIST);
}
MR_TEST_EXPORT(hashmap, duplicate_key, test_hashmap_duplicate_key);

static void test_hashmap_duplicate_node(void) {
    test_hashmap_item_t item;

    hashmap_reset();
    mr_hashnode_init(&item.node, 2, &item.value);

    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&test_hashmap, &item.node), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&test_hashmap, &item.node), -MR_EBUSY);
}
MR_TEST_EXPORT(hashmap, duplicate_node, test_hashmap_duplicate_node);

static void test_hashmap_collision_lookup(void) {
    test_hashmap_item_t items[8];
    mr_hashnode_t *node;
    mr_uint32_t i;

    hashmap_reset();
    for (i = 0; i < MR_ARRAY_SIZE(items); i++) {
        items[i].value = (int)(i + 10);
        mr_hashnode_init(&items[i].node, i, &items[i].value);
        MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&test_hashmap, &items[i].node), 0);
    }

    for (i = 0; i < MR_ARRAY_SIZE(items); i++) {
        node = mr_hashmap_find(&test_hashmap, i);
        MR_TEST_ASSERT_EQ_PTR(node, &items[i].node);
        MR_TEST_ASSERT_EQ_PTR(mr_hashnode_value(node), &items[i].value);
    }
}
MR_TEST_EXPORT(hashmap, collision_lookup, test_hashmap_collision_lookup);

static void test_hashmap_readd_after_del(void) {
    test_hashmap_item_t item;

    hashmap_reset();
    mr_hashnode_init(&item.node, 9, &item.value);

    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&test_hashmap, &item.node), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_del(&item.node), 0);
    MR_TEST_ASSERT_EQ_INT(mr_hashmap_add(&test_hashmap, &item.node), 0);
    MR_TEST_ASSERT_EQ_PTR(mr_hashmap_find(&test_hashmap, 9), &item.node);
}
MR_TEST_EXPORT(hashmap, readd_after_del, test_hashmap_readd_after_del);

static void test_hashmap_invalid_operation(void) {
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
MR_TEST_EXPORT(hashmap, invalid_operation, test_hashmap_invalid_operation);
#endif /* defined(MR_USE_TEST) */

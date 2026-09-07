/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_object.h>
#include <mrX/libc/mr_string.h>

/* Constants */

/* Container type with an embedded object as first member */
typedef struct test_deep_container {
    mr_object_t object;
    int payload;
} test_deep_container_t;

/* Release handler identifiers recorded in the call order trace */
#define TEST_DEEP_BASE_STATIK_ID     1
#define TEST_DEEP_BASE_DYNAMIC_ID    2
#define TEST_DEEP_MID_STATIK_ID      3
#define TEST_DEEP_MID_DYNAMIC_ID     4
#define TEST_DEEP_LEAF_STATIK_ID     5
#define TEST_DEEP_LEAF_DYNAMIC_ID    6

#define TEST_DEEP_ORDER_MAX          16

/* Mock state */

/* Call order trace of release handlers and last released object */
static int test_deep_order[TEST_DEEP_ORDER_MAX];
static int test_deep_order_count;
static mr_object_t *test_deep_last_released;

/* Mock release handlers */

static void test_deep_record(int id, mr_object_t *obj) {
    if (test_deep_order_count < TEST_DEEP_ORDER_MAX) {
        test_deep_order[test_deep_order_count] = id;
    }
    test_deep_order_count++;
    test_deep_last_released = obj;
}

static void test_deep_base_statik_release(struct mr_object *obj) {
    test_deep_record(TEST_DEEP_BASE_STATIK_ID, obj);
}

static void test_deep_base_dynamic_release(struct mr_object *obj) {
    test_deep_record(TEST_DEEP_BASE_DYNAMIC_ID, obj);
}

static void test_deep_mid_statik_release(struct mr_object *obj) {
    test_deep_record(TEST_DEEP_MID_STATIK_ID, obj);
}

static void test_deep_mid_dynamic_release(struct mr_object *obj) {
    test_deep_record(TEST_DEEP_MID_DYNAMIC_ID, obj);
}

static void test_deep_leaf_statik_release(struct mr_object *obj) {
    test_deep_record(TEST_DEEP_LEAF_STATIK_ID, obj);
}

static void test_deep_leaf_dynamic_release(struct mr_object *obj) {
    test_deep_record(TEST_DEEP_LEAF_DYNAMIC_ID, obj);
}

static void test_deep_other_statik_release(struct mr_object *obj) {
    MR_IGNORE(obj);
}

static void test_deep_other_dynamic_release(struct mr_object *obj) {
    MR_IGNORE(obj);
}

/* Define test clazzes (three level inheritance chain plus an unrelated root) */
MR_CLAZZES_DEFINE(deep_base, test_deep_base_statik_release,
                  test_deep_base_dynamic_release, MR_NULL);
MR_CLAZZES_DEFINE(deep_mid, test_deep_mid_statik_release,
                  test_deep_mid_dynamic_release,
                  MR_CLAZZES_STATIK(deep_base));
MR_CLAZZES_DEFINE(deep_leaf, test_deep_leaf_statik_release,
                  test_deep_leaf_dynamic_release,
                  MR_CLAZZES_STATIK(deep_mid));
MR_CLAZZES_DEFINE(deep_other, test_deep_other_statik_release,
                  test_deep_other_dynamic_release, MR_NULL);

/* Reset the release trace between cases */
static void test_deep_reset(void) {
    test_deep_order_count = 0;
    test_deep_last_released = MR_NULL;
}

/* Tests */

/* Test: static leaf release chain runs base first and leaf last */
MR_TEST(object, release_chain_order_statik) {
    mr_object_t obj;
    int expected[3];
    int i;
    mr_err_t ret;

    test_deep_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(deep_leaf));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_object_del(&obj);
    /* Potential bug: the release chain runs super first and derived last,
       which inverts the conventional destructor order, verify intention */
    expected[0] = TEST_DEEP_BASE_STATIK_ID;
    expected[1] = TEST_DEEP_MID_STATIK_ID;
    expected[2] = TEST_DEEP_LEAF_STATIK_ID;
    MR_TEST_EXPECT_EQ_INT(test_deep_order_count, 3);
    for (i = 0; i < 3; i++) {
        MR_TEST_EXPECT_EQ_INT(test_deep_order[i], expected[i]);
    }
}

/* Test: dynamic leaf mixes dynamic and static handlers in one chain */
MR_TEST(object, release_chain_order_dynamic) {
    mr_object_t obj;
    int expected[3];
    int i;
    mr_err_t ret;

    test_deep_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_DYNAMIC(deep_leaf));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_object_del(&obj);
    /* The dynamic leaf clazz has the static mid clazz as its super */
    expected[0] = TEST_DEEP_BASE_STATIK_ID;
    expected[1] = TEST_DEEP_MID_STATIK_ID;
    expected[2] = TEST_DEEP_LEAF_DYNAMIC_ID;
    MR_TEST_EXPECT_EQ_INT(test_deep_order_count, 3);
    for (i = 0; i < 3; i++) {
        MR_TEST_EXPECT_EQ_INT(test_deep_order[i], expected[i]);
    }
}

/* Test: release fires exactly once no matter how many refs were held */
MR_TEST(object, release_once_with_multiple_refs) {
    mr_object_t obj;
    mr_object_t *got;
    int expected[3];
    int i;
    mr_err_t ret;

    test_deep_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(deep_leaf));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    got = mr_object_get(&obj);
    MR_TEST_EXPECT_EQ_PTR(got, &obj);
    got = mr_object_get(&obj);
    MR_TEST_EXPECT_EQ_PTR(got, &obj);

    /* Drop the initial ref, the object must survive */
    mr_object_del(&obj);
    MR_TEST_EXPECT_EQ_INT(test_deep_order_count, 0);

    MR_TEST_EXPECT_EQ_INT(mr_object_put(&obj), MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(test_deep_order_count, 0);

    /* Last put ends the life cycle and releases the whole chain once */
    MR_TEST_EXPECT_EQ_INT(mr_object_put(&obj), MR_TRUE);
    expected[0] = TEST_DEEP_BASE_STATIK_ID;
    expected[1] = TEST_DEEP_MID_STATIK_ID;
    expected[2] = TEST_DEEP_LEAF_STATIK_ID;
    MR_TEST_EXPECT_EQ_INT(test_deep_order_count, 3);
    for (i = 0; i < 3; i++) {
        MR_TEST_EXPECT_EQ_INT(test_deep_order[i], expected[i]);
    }
}

/* Test: del with held refs only drops one reference */
MR_TEST(object, del_with_held_refs_not_terminal) {
    mr_object_t obj;
    mr_object_t *got;
    mr_err_t ret;

    test_deep_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(deep_base));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    got = mr_object_get(&obj);
    MR_TEST_EXPECT_EQ_PTR(got, &obj);

    mr_object_del(&obj);
    MR_TEST_EXPECT_EQ_INT(test_deep_order_count, 0);

    got = mr_object_get(&obj);
    MR_TEST_EXPECT_EQ_PTR(got, &obj);
    MR_TEST_EXPECT_EQ_INT(mr_object_put(&obj), MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(mr_object_put(&obj), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(test_deep_order_count, 1);
}

/* Test: deriving to a dynamic sub clazz of a static parent clazz works */
MR_TEST(object, derive_dynamic_child_of_statik_parent) {
    mr_object_t obj;
    mr_err_t ret;

    test_deep_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(deep_base));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_object_derive(&obj, MR_CLAZZES_DYNAMIC(deep_mid));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        mr_object_del(&obj);
        return;
    }
    MR_TEST_EXPECT_EQ_PTR(MR_OBJECT_CLAZZ(&obj),
                          MR_CLAZZES_DYNAMIC(deep_mid));

    mr_object_del(&obj);
    MR_TEST_EXPECT_EQ_INT(test_deep_order_count, 2);
    MR_TEST_EXPECT_EQ_INT(test_deep_order[0], TEST_DEEP_BASE_STATIK_ID);
    MR_TEST_EXPECT_EQ_INT(test_deep_order[1], TEST_DEEP_MID_DYNAMIC_ID);
}

/* Test: a static sub clazz does not derive from a dynamic parent clazz */
MR_TEST(object, derive_statik_child_of_dynamic_parent) {
    mr_object_t obj;
    mr_err_t ret;

    test_deep_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_DYNAMIC(deep_base));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_object_derive(&obj, MR_CLAZZES_STATIK(deep_mid));
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EPERM);
    MR_TEST_EXPECT_EQ_PTR(MR_OBJECT_CLAZZ(&obj),
                          MR_CLAZZES_DYNAMIC(deep_base));
    mr_object_del(&obj);
}

/* Test: deriving back to a super clazz is rejected */
MR_TEST(object, derive_back_to_super_denied) {
    mr_object_t obj;
    mr_err_t ret;

    test_deep_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(deep_base));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_object_derive(&obj, MR_CLAZZES_STATIK(deep_mid));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        mr_object_del(&obj);
        return;
    }

    ret = mr_object_derive(&obj, MR_CLAZZES_STATIK(deep_base));
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EPERM);
    MR_TEST_EXPECT_EQ_PTR(MR_OBJECT_CLAZZ(&obj),
                          MR_CLAZZES_STATIK(deep_mid));
    mr_object_del(&obj);
}

/* Test: deriving to an unrelated root clazz is rejected */
MR_TEST(object, derive_unrelated_root_denied) {
    mr_object_t obj;
    mr_err_t ret;

    test_deep_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(deep_base));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_object_derive(&obj, MR_CLAZZES_STATIK(deep_other));
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EPERM);
    MR_TEST_EXPECT_EQ_PTR(MR_OBJECT_CLAZZ(&obj),
                          MR_CLAZZES_STATIK(deep_base));
    mr_object_del(&obj);
}

/* Test: kind of checks walk the full clazzes inheritance chain */
MR_TEST(object, is_clazzes_super_chain) {
    mr_object_t obj;
    mr_err_t ret;

    test_deep_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_DYNAMIC(deep_mid));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_CLAZZES(&obj, deep_mid), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_CLAZZES(&obj, deep_base), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_CLAZZES(&obj, deep_leaf), MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_CLAZZES(&obj, deep_other), MR_FALSE);

    mr_object_del(&obj);
    MR_TEST_EXPECT_EQ_INT(test_deep_order_count, 2);
    MR_TEST_EXPECT_EQ_INT(test_deep_order[0], TEST_DEEP_BASE_STATIK_ID);
    MR_TEST_EXPECT_EQ_INT(test_deep_order[1], TEST_DEEP_MID_DYNAMIC_ID);
}

/* Test: re-initializing a deleted object starts a new life cycle */
MR_TEST(object, reinit_after_del_resurrect) {
    mr_object_t obj;
    mr_object_t *got;
    mr_err_t ret;

    test_deep_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(deep_base));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_object_del(&obj);
    MR_TEST_EXPECT_EQ_INT(test_deep_order_count, 1);

    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(deep_base));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }
    MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_INITED(&obj), MR_TRUE);
    got = mr_object_get(&obj);
    MR_TEST_EXPECT_EQ_PTR(got, &obj);
    if (got == MR_NULL) {
        return;
    }

    /* Drop the extra reference before deleting so the lifecycle can end */
    MR_TEST_EXPECT_EQ_INT(mr_object_put(&obj), MR_FALSE);

    mr_object_del(&obj);
    MR_TEST_EXPECT_EQ_INT(test_deep_order_count, 2);
}

/* Test: put after end of life keeps returning true without re-releasing */
MR_TEST(object, put_repeatedly_after_eol) {
    mr_object_t obj;
    mr_err_t ret;

    test_deep_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(deep_base));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_object_del(&obj);
    MR_TEST_EXPECT_EQ_INT(test_deep_order_count, 1);

    MR_TEST_EXPECT_EQ_INT(mr_object_put(&obj), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(mr_object_put(&obj), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(test_deep_order_count, 1);
    MR_TEST_EXPECT_EQ_PTR(mr_object_get(&obj), MR_NULL);
}

/* Test: the release handler receives the very object that was deleted */
MR_TEST(object, release_receives_same_object) {
    mr_object_t obj;
    mr_err_t ret;

    test_deep_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(deep_base));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_object_del(&obj);
    MR_TEST_EXPECT_EQ_INT(test_deep_order_count, 1);
    MR_TEST_EXPECT_EQ_PTR(test_deep_last_released, &obj);
}

/* Test: many get put cycles never trigger an early release */
MR_TEST(object, many_get_put_cycles) {
    mr_object_t obj;
    mr_object_t *got;
    int i;
    mr_err_t ret;

    test_deep_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(deep_base));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    for (i = 0; i < 50; i++) {
        got = mr_object_get(&obj);
        MR_TEST_ASSERT_EQ_PTR(got, &obj);
        if (got == MR_NULL) {
            mr_object_del(&obj);
            return;
        }
        MR_TEST_EXPECT_EQ_INT(mr_object_put(&obj), MR_FALSE);
        MR_TEST_EXPECT_EQ_INT(test_deep_order_count, 0);
    }

    MR_TEST_EXPECT_EQ_INT(mr_object_put(&obj), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(test_deep_order_count, 1);
}

/* Test: object macros and container recovery work on an embedded object */
MR_TEST(object, embedded_object_container) {
    test_deep_container_t c;
    mr_object_t *from_ref;
    mr_err_t ret;

    test_deep_reset();
    mr_memset(&c, 0, sizeof(c));
    ret = mr_object_init(&c.object, MR_CLAZZES_STATIK(deep_base));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    /* Macro casts apply to the container as object is the first member */
    MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_INITED(&c), MR_TRUE);
    MR_TEST_EXPECT_EQ_PTR(MR_OBJECT_CLAZZ(&c),
                          MR_CLAZZES_STATIK(deep_base));

    /* Container recovery from the ref member yields the object itself */
    from_ref = MR_CONTAINER_OF(&c.object.magic_ref, mr_object_t, magic_ref);
    MR_TEST_EXPECT_EQ_PTR(from_ref, &c.object);

    c.payload = 7;
    MR_TEST_EXPECT_EQ_INT(c.payload, 7);
    mr_object_del(&c.object);
    MR_TEST_EXPECT_EQ_INT(test_deep_order_count, 1);
}

/* Test: deriving from an object with a null clazz is safely rejected */
MR_TEST(object, init_null_clazz_derive_eperm) {
    mr_object_t obj;
    mr_err_t ret;

    test_deep_reset();
    ret = mr_object_init(&obj, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_object_derive(&obj, MR_CLAZZES_STATIK(deep_base));
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EPERM);
    MR_TEST_EXPECT_EQ_PTR(MR_OBJECT_CLAZZ(&obj), MR_NULL);
    mr_object_del(&obj);
    MR_TEST_EXPECT_EQ_INT(test_deep_order_count, 0);
}

/* Test: null release handlers in the middle of a chain are skipped */
MR_TEST(object, null_release_middle_chain) {
    mr_clazz_t base;
    mr_clazz_t mid;
    mr_clazz_t leaf;
    mr_object_t obj;
    mr_err_t ret;

    test_deep_reset();
    mr_clazz_init(&base, "base", MR_NULL, MR_NULL);
    mr_clazz_init(&mid, "mid", MR_NULL, &base);
    mr_clazz_init(&leaf, "leaf", test_deep_base_statik_release, &mid);
    ret = mr_object_init(&obj, &leaf);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_object_del(&obj);
    MR_TEST_EXPECT_EQ_INT(test_deep_order_count, 1);
    MR_TEST_EXPECT_EQ_INT(test_deep_order[0], TEST_DEEP_BASE_STATIK_ID);
    MR_TEST_EXPECT_EQ_PTR(test_deep_last_released, &obj);
}

#endif /* defined(MR_USE_TEST) */

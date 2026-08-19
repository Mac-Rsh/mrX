/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_object.h>
#include <mrX/libc/mr_string.h>

/* Mock state */
static int test_object_statik_release_called;
static int test_object_dynamic_release_called;

/* Mock release handlers */
static void test_object_statik_release(struct mr_object *obj) {
    MR_IGNORE(obj);
    test_object_statik_release_called++;
}

static void test_object_dynamic_release(struct mr_object *obj) {
    MR_IGNORE(obj);
    test_object_dynamic_release_called++;
}

/* Define test clazzes */
MR_CLAZZES_DEFINE(test_object, test_object_statik_release,
                  test_object_dynamic_release, MR_NULL);

/* Sub-object mock state */
static int test_sub_object_statik_release_called;
static int test_sub_object_dynamic_release_called;

/* Sub-object mock release handlers */
static void test_sub_object_statik_release(struct mr_object *obj) {
    MR_IGNORE(obj);
    test_sub_object_statik_release_called++;
}

static void test_sub_object_dynamic_release(struct mr_object *obj) {
    MR_IGNORE(obj);
    test_sub_object_dynamic_release_called++;
}

/* Define sub clazzes (inherits from test_object) */
MR_CLAZZES_DEFINE(test_sub_object, test_sub_object_statik_release,
                  test_sub_object_dynamic_release,
                  MR_CLAZZES_STATIK(test_object));

static void test_object_reset(void) {
    test_object_statik_release_called = 0;
    test_object_dynamic_release_called = 0;
    test_sub_object_statik_release_called = 0;
    test_sub_object_dynamic_release_called = 0;
}

static void test_object_expect_put(volatile mr_uint32_t *__test_status,
                                   mr_object_t *obj, int *refs_held,
                                   mr_bool_t expected) {
    mr_bool_t ret;

    if (*refs_held <= 0) {
        MR_TEST_EXPECT_GT_INT(*refs_held, 0);
        return;
    }

    ret = mr_object_put(obj);
    (*refs_held)--;
    MR_TEST_EXPECT_EQ_INT(ret, expected);
    if (ret == MR_TRUE) {
        *refs_held = 0;
    }
}

static void test_object_cleanup_refs(mr_object_t *obj, int *refs_held) {
    while (*refs_held > 0) {
        if (mr_object_put(obj) == MR_TRUE) {
            *refs_held = 0;
            return;
        }
        (*refs_held)--;
    }
}

/* Tests */
MR_TEST(object, init_normal) {
    mr_object_t obj;
    mr_err_t ret;

    test_object_reset();
    mr_memset(&obj, 0, sizeof(obj));
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(test_object));

    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_INITED(&obj), MR_TRUE);
        MR_TEST_EXPECT_EQ_PTR(MR_OBJECT_CLAZZ(&obj),
                              MR_CLAZZES_STATIK(test_object));
        mr_object_del(&obj);
    }
}

MR_TEST(object, init_null_obj) {
    mr_err_t ret;

    test_object_reset();
    ret = mr_object_init(MR_NULL, MR_CLAZZES_STATIK(test_object));
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EINVAL);
}

MR_TEST(object, init_null_clazz) {
    mr_object_t obj;
    mr_err_t ret;

    test_object_reset();
    mr_memset(&obj, 0, sizeof(obj));
    ret = mr_object_init(&obj, MR_NULL);

    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_INITED(&obj), MR_TRUE);
    MR_TEST_EXPECT_EQ_PTR(MR_OBJECT_CLAZZ(&obj), MR_NULL);
    mr_object_del(&obj);
    MR_TEST_EXPECT_EQ_INT(test_object_statik_release_called, 0);
}

MR_TEST(object, init_already_inited) {
    mr_object_t obj;
    mr_err_t ret;

    test_object_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(test_object));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_object_init(&obj, MR_CLAZZES_DYNAMIC(test_object));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    mr_object_del(&obj);
}

MR_TEST(object, del_normal) {
    mr_object_t obj;
    mr_err_t ret;

    test_object_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_DYNAMIC(test_object));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_object_del(&obj);
    MR_TEST_EXPECT_EQ_INT(test_object_dynamic_release_called, 1);
}

MR_TEST(object, del_null) {
    test_object_reset();
    mr_object_del(MR_NULL);
    MR_TEST_ASSERT_EQ_INT(test_object_statik_release_called, 0);
    MR_TEST_ASSERT_EQ_INT(test_object_dynamic_release_called, 0);
}

MR_TEST(object, del_uninit) {
    mr_object_t obj;

    test_object_reset();
    mr_memset(&obj, 0, sizeof(obj));
    mr_object_del(&obj);

    MR_TEST_ASSERT_EQ_INT(test_object_statik_release_called, 0);
    MR_TEST_ASSERT_EQ_INT(test_object_dynamic_release_called, 0);
}

MR_TEST(object, get_put_refcount) {
    mr_object_t obj;
    mr_object_t *ret_obj;
    int refs_held;
    mr_err_t ret;

    test_object_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(test_object));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }
    refs_held = 1;

    ret_obj = mr_object_get(&obj);
    if (ret_obj != MR_NULL) {
        refs_held++;
    }
    MR_TEST_EXPECT_EQ_PTR(ret_obj, &obj);

    ret_obj = mr_object_get(&obj);
    if (ret_obj != MR_NULL) {
        refs_held++;
    }
    MR_TEST_EXPECT_EQ_PTR(ret_obj, &obj);

    test_object_expect_put(__test_status, &obj, &refs_held, MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(test_object_statik_release_called, 0);

    test_object_expect_put(__test_status, &obj, &refs_held, MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(test_object_statik_release_called, 0);

    test_object_expect_put(__test_status, &obj, &refs_held, MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(test_object_statik_release_called, 1);

    test_object_cleanup_refs(&obj, &refs_held);
}

MR_TEST(object, get_null) {
    mr_object_t *ret_obj;

    test_object_reset();
    ret_obj = mr_object_get(MR_NULL);
    MR_TEST_ASSERT_EQ_PTR(ret_obj, MR_NULL);
}

MR_TEST(object, get_uninit) {
    mr_object_t obj;
    mr_object_t *ret_obj;

    test_object_reset();
    mr_memset(&obj, 0, sizeof(obj));
    ret_obj = mr_object_get(&obj);
    MR_TEST_ASSERT_EQ_PTR(ret_obj, MR_NULL);
}

MR_TEST(object, put_null) {
    mr_bool_t ret_bool;

    test_object_reset();
    ret_bool = mr_object_put(MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret_bool, MR_TRUE);
}

MR_TEST(object, put_uninit) {
    mr_object_t obj;
    mr_bool_t ret_bool;

    test_object_reset();
    mr_memset(&obj, 0, sizeof(obj));
    ret_bool = mr_object_put(&obj);
    MR_TEST_ASSERT_EQ_INT(ret_bool, MR_TRUE);
}

MR_TEST(object, is_clazzes) {
    mr_object_t obj;
    mr_err_t ret;

    test_object_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(test_object));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_CLAZZES(&obj, test_object), MR_TRUE);
    mr_object_del(&obj);
}

MR_TEST(object, lifecycle) {
    mr_object_t obj;
    mr_object_t *obj1, *obj2, *obj3;
    int refs_held;
    mr_err_t ret;

    test_object_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_DYNAMIC(test_object));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }
    refs_held = 1;

    obj1 = mr_object_get(&obj);
    if (obj1 != MR_NULL) {
        refs_held++;
    }
    obj2 = mr_object_get(&obj);
    if (obj2 != MR_NULL) {
        refs_held++;
    }
    obj3 = mr_object_get(&obj);
    if (obj3 != MR_NULL) {
        refs_held++;
    }

    MR_TEST_EXPECT_EQ_PTR(obj1, &obj);
    MR_TEST_EXPECT_EQ_PTR(obj2, &obj);
    MR_TEST_EXPECT_EQ_PTR(obj3, &obj);

    test_object_expect_put(__test_status, &obj, &refs_held, MR_FALSE);
    test_object_expect_put(__test_status, &obj, &refs_held, MR_FALSE);
    test_object_expect_put(__test_status, &obj, &refs_held, MR_FALSE);
    test_object_expect_put(__test_status, &obj, &refs_held, MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(test_object_dynamic_release_called, 1);

    test_object_cleanup_refs(&obj, &refs_held);
}

/* mr_object_derive tests */
MR_TEST(object, derive_normal) {
    mr_object_t obj;
    mr_err_t ret;

    test_object_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(test_object));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_object_derive(&obj, MR_CLAZZES_STATIK(test_sub_object));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        mr_object_del(&obj);
        return;
    }
    MR_TEST_EXPECT_EQ_PTR(MR_OBJECT_CLAZZ(&obj),
                          MR_CLAZZES_STATIK(test_sub_object));
    mr_object_del(&obj);
}

MR_TEST(object, derive_null_obj) {
    mr_err_t ret;

    test_object_reset();
    ret = mr_object_derive(MR_NULL, MR_CLAZZES_STATIK(test_object));
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EINVAL);
}

MR_TEST(object, derive_null_clazz) {
    mr_object_t obj;
    mr_err_t ret;

    test_object_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(test_object));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_object_derive(&obj, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EINVAL);
    mr_object_del(&obj);
}

MR_TEST(object, derive_uninit) {
    mr_object_t obj;
    mr_err_t ret;

    test_object_reset();
    mr_memset(&obj, 0, sizeof(obj));
    ret = mr_object_derive(&obj, MR_CLAZZES_STATIK(test_sub_object));
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EINVAL);
}

MR_TEST(object, derive_not_subclass) {
    mr_object_t obj;
    mr_err_t ret;

    test_object_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(test_object));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_object_derive(&obj, MR_CLAZZES_DYNAMIC(test_object));
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EPERM);
    mr_object_del(&obj);
}

MR_TEST(object, derive_same_clazz) {
    mr_object_t obj;
    mr_err_t ret;

    test_object_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(test_object));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_object_derive(&obj, MR_CLAZZES_STATIK(test_object));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    mr_object_del(&obj);
}

MR_TEST(object, derive_del_release) {
    mr_object_t obj;
    mr_err_t ret;

    test_object_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(test_object));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_object_derive(&obj, MR_CLAZZES_DYNAMIC(test_sub_object));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        mr_object_del(&obj);
        return;
    }

    mr_object_del(&obj);
    MR_TEST_EXPECT_EQ_INT(test_object_statik_release_called, 1);
    MR_TEST_EXPECT_EQ_INT(test_sub_object_dynamic_release_called, 1);
}

MR_TEST(object, derive_get_after) {
    mr_object_t obj;
    mr_object_t *ret_obj;
    mr_err_t ret;

    test_object_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(test_object));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_object_derive(&obj, MR_CLAZZES_STATIK(test_sub_object));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        mr_object_del(&obj);
        return;
    }

    ret_obj = mr_object_get(&obj);
    MR_TEST_EXPECT_EQ_PTR(ret_obj, &obj);
    if (ret_obj != MR_NULL) {
        mr_object_put(&obj);
    }
    mr_object_del(&obj);
}

/* mr_clazz_is_kind_of inline tests */
MR_TEST(object, clazz_is_kind_of_self) {
    test_object_reset();
    MR_TEST_EXPECT_EQ_INT(mr_clazz_is_kind_of(MR_CLAZZES_STATIK(test_object),
                                              MR_CLAZZES_STATIK(test_object)),
                          MR_TRUE);
}

MR_TEST(object, clazz_is_kind_of_chain) {
    test_object_reset();
    MR_TEST_EXPECT_EQ_INT(
        mr_clazz_is_kind_of(MR_CLAZZES_STATIK(test_sub_object),
                            MR_CLAZZES_STATIK(test_object)),
        MR_TRUE);
}

MR_TEST(object, clazz_is_kind_of_unrelated) {
    test_object_reset();
    MR_TEST_EXPECT_EQ_INT(
        mr_clazz_is_kind_of(MR_CLAZZES_STATIK(test_object),
                            MR_CLAZZES_STATIK(test_sub_object)),
        MR_FALSE);
}

MR_TEST(object, clazz_is_kind_of_null) {
    test_object_reset();
    MR_TEST_EXPECT_EQ_INT(
        mr_clazz_is_kind_of(MR_NULL, MR_CLAZZES_STATIK(test_object)), MR_FALSE);
}

/* mr_clazz_init runtime test */
MR_TEST(object, clazz_init_runtime) {
    mr_clazz_t runtime_clazz;
    mr_object_t obj;
    mr_err_t ret;

    test_object_reset();
    mr_clazz_init(&runtime_clazz, "runtime", test_object_statik_release,
                  MR_NULL);
    ret = mr_object_init(&obj, &runtime_clazz);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    MR_TEST_EXPECT_EQ_PTR(MR_OBJECT_CLAZZ(&obj), &runtime_clazz);
    mr_object_del(&obj);
    MR_TEST_EXPECT_EQ_INT(test_object_statik_release_called, 1);
}

/* Post-del edge case tests */
MR_TEST(object, get_after_del) {
    mr_object_t obj;
    mr_object_t *ret_obj;
    mr_err_t ret;

    test_object_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(test_object));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_object_del(&obj);
    ret_obj = mr_object_get(&obj);
    MR_TEST_ASSERT_EQ_PTR(ret_obj, MR_NULL);
}

MR_TEST(object, put_after_del) {
    mr_object_t obj;
    mr_bool_t ret_bool;
    mr_err_t ret;

    test_object_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(test_object));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_object_del(&obj);
    ret_bool = mr_object_put(&obj);
    MR_TEST_ASSERT_EQ_INT(ret_bool, MR_TRUE);
}

MR_TEST(object, release_chain_null_release) {
    mr_clazz_t parent;
    mr_clazz_t child;
    mr_object_t obj;
    mr_err_t ret;

    test_object_reset();
    mr_clazz_init(&parent, "parent", MR_NULL, MR_NULL);
    mr_clazz_init(&child, "child", test_object_dynamic_release, &parent);
    ret = mr_object_init(&obj, &child);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    mr_object_del(&obj);
    MR_TEST_EXPECT_EQ_INT(test_object_dynamic_release_called, 1);
}

#endif /* defined(MR_USE_TEST) */

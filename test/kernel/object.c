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

static void test_object_expect_put(mr_object_t *obj, int *refs_held,
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
static void test_object_init_normal(void) {
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
MR_TEST_EXPORT(object, init_normal, test_object_init_normal);

static void test_object_init_null_obj(void) {
    mr_err_t ret;

    test_object_reset();
    ret = mr_object_init(MR_NULL, MR_CLAZZES_STATIK(test_object));
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EINVAL);
}
MR_TEST_EXPORT(object, init_null_obj, test_object_init_null_obj);

static void test_object_init_already_inited(void) {
    mr_object_t obj;
    mr_err_t ret;

    test_object_reset();
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(test_object));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    ret = mr_object_init(&obj, MR_CLAZZES_DYNAMIC(test_object));
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EINVAL);
    mr_object_del(&obj);
}
MR_TEST_EXPORT(object, init_already_inited, test_object_init_already_inited);

static void test_object_del_normal(void) {
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
MR_TEST_EXPORT(object, del_normal, test_object_del_normal);

static void test_object_del_null(void) {
    test_object_reset();
    mr_object_del(MR_NULL);
    MR_TEST_ASSERT_EQ_INT(test_object_statik_release_called, 0);
    MR_TEST_ASSERT_EQ_INT(test_object_dynamic_release_called, 0);
}
MR_TEST_EXPORT(object, del_null, test_object_del_null);

static void test_object_del_uninit(void) {
    mr_object_t obj;

    test_object_reset();
    mr_memset(&obj, 0, sizeof(obj));
    mr_object_del(&obj);

    MR_TEST_ASSERT_EQ_INT(test_object_statik_release_called, 0);
    MR_TEST_ASSERT_EQ_INT(test_object_dynamic_release_called, 0);
}
MR_TEST_EXPORT(object, del_uninit, test_object_del_uninit);

static void test_object_get_put_refcount(void) {
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

    test_object_expect_put(&obj, &refs_held, MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(test_object_statik_release_called, 0);

    test_object_expect_put(&obj, &refs_held, MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(test_object_statik_release_called, 0);

    test_object_expect_put(&obj, &refs_held, MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(test_object_statik_release_called, 1);

    test_object_cleanup_refs(&obj, &refs_held);
}
MR_TEST_EXPORT(object, get_put_refcount, test_object_get_put_refcount);

static void test_object_get_null(void) {
    mr_object_t *ret_obj;

    test_object_reset();
    ret_obj = mr_object_get(MR_NULL);
    MR_TEST_ASSERT_EQ_PTR(ret_obj, MR_NULL);
}
MR_TEST_EXPORT(object, get_null, test_object_get_null);

static void test_object_get_uninit(void) {
    mr_object_t obj;
    mr_object_t *ret_obj;

    test_object_reset();
    mr_memset(&obj, 0, sizeof(obj));
    ret_obj = mr_object_get(&obj);
    MR_TEST_ASSERT_EQ_PTR(ret_obj, MR_NULL);
}
MR_TEST_EXPORT(object, get_uninit, test_object_get_uninit);

static void test_object_put_null(void) {
    mr_bool_t ret_bool;

    test_object_reset();
    ret_bool = mr_object_put(MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret_bool, MR_TRUE);
}
MR_TEST_EXPORT(object, put_null, test_object_put_null);

static void test_object_put_uninit(void) {
    mr_object_t obj;
    mr_bool_t ret_bool;

    test_object_reset();
    mr_memset(&obj, 0, sizeof(obj));
    ret_bool = mr_object_put(&obj);
    MR_TEST_ASSERT_EQ_INT(ret_bool, MR_TRUE);
}
MR_TEST_EXPORT(object, put_uninit, test_object_put_uninit);

static void test_object_is_clazzes(void) {
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
MR_TEST_EXPORT(object, is_clazzes, test_object_is_clazzes);

static void test_object_lifecycle(void) {
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

    test_object_expect_put(&obj, &refs_held, MR_FALSE);
    test_object_expect_put(&obj, &refs_held, MR_FALSE);
    test_object_expect_put(&obj, &refs_held, MR_FALSE);
    test_object_expect_put(&obj, &refs_held, MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(test_object_dynamic_release_called, 1);

    test_object_cleanup_refs(&obj, &refs_held);
}
MR_TEST_EXPORT(object, lifecycle, test_object_lifecycle);

/* mr_object_derive tests */
static void test_object_derive_normal(void) {
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
MR_TEST_EXPORT(object, derive_normal, test_object_derive_normal);

static void test_object_derive_null_obj(void) {
    mr_err_t ret;

    test_object_reset();
    ret = mr_object_derive(MR_NULL, MR_CLAZZES_STATIK(test_object));
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EINVAL);
}
MR_TEST_EXPORT(object, derive_null_obj, test_object_derive_null_obj);

static void test_object_derive_null_clazz(void) {
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
MR_TEST_EXPORT(object, derive_null_clazz, test_object_derive_null_clazz);

static void test_object_derive_uninit(void) {
    mr_object_t obj;
    mr_err_t ret;

    test_object_reset();
    mr_memset(&obj, 0, sizeof(obj));
    ret = mr_object_derive(&obj, MR_CLAZZES_STATIK(test_sub_object));
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EINVAL);
}
MR_TEST_EXPORT(object, derive_uninit, test_object_derive_uninit);

static void test_object_derive_not_subclass(void) {
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
MR_TEST_EXPORT(object, derive_not_subclass, test_object_derive_not_subclass);

static void test_object_derive_del_release(void) {
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
MR_TEST_EXPORT(object, derive_del_release, test_object_derive_del_release);

static void test_object_derive_get_after(void) {
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
MR_TEST_EXPORT(object, derive_get_after, test_object_derive_get_after);

/* mr_clazz_is_kind_of inline tests */
static void test_object_clazz_is_kind_of_self(void) {
    test_object_reset();
    MR_TEST_EXPECT_EQ_INT(
        mr_clazz_is_kind_of(MR_CLAZZES_STATIK(test_object),
                            MR_CLAZZES_STATIK(test_object)), MR_TRUE);
}
MR_TEST_EXPORT(object, clazz_is_kind_of_self,
               test_object_clazz_is_kind_of_self);

static void test_object_clazz_is_kind_of_chain(void) {
    test_object_reset();
    MR_TEST_EXPECT_EQ_INT(
        mr_clazz_is_kind_of(MR_CLAZZES_STATIK(test_sub_object),
                            MR_CLAZZES_STATIK(test_object)), MR_TRUE);
}
MR_TEST_EXPORT(object, clazz_is_kind_of_chain,
               test_object_clazz_is_kind_of_chain);

static void test_object_clazz_is_kind_of_unrelated(void) {
    test_object_reset();
    MR_TEST_EXPECT_EQ_INT(
        mr_clazz_is_kind_of(MR_CLAZZES_STATIK(test_object),
                            MR_CLAZZES_STATIK(test_sub_object)), MR_FALSE);
}
MR_TEST_EXPORT(object, clazz_is_kind_of_unrelated,
               test_object_clazz_is_kind_of_unrelated);

static void test_object_clazz_is_kind_of_null(void) {
    test_object_reset();
    MR_TEST_EXPECT_EQ_INT(
        mr_clazz_is_kind_of(MR_NULL, MR_CLAZZES_STATIK(test_object)),
        MR_FALSE);
}
MR_TEST_EXPORT(object, clazz_is_kind_of_null,
               test_object_clazz_is_kind_of_null);

/* mr_clazz_init runtime test */
static void test_object_clazz_init_runtime(void) {
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
MR_TEST_EXPORT(object, clazz_init_runtime, test_object_clazz_init_runtime);

/* Post-del edge case tests */
static void test_object_get_after_del(void) {
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
MR_TEST_EXPORT(object, get_after_del, test_object_get_after_del);

static void test_object_put_after_del(void) {
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
MR_TEST_EXPORT(object, put_after_del, test_object_put_after_del);
#endif /* defined(MR_USE_TEST) */

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

/* Track release function calls */
static int test_object_statik_release_called = 0;
static int test_object_dynamic_release_called = 0;

/* Mock release handlers */
static void test_object_statik_release(struct mr_object *obj)
{
    MR_IGNORE(obj);
    test_object_statik_release_called++;
}

static void test_object_dynamic_release(struct mr_object *obj)
{
    MR_IGNORE(obj);
    test_object_dynamic_release_called++;
}

/* Define test clazzes */
MR_CLAZZES_DEFINE(test_object, test_object_statik_release,
                  test_object_dynamic_release, MR_NULL);

/* -------------------------------------------------------------------------- */
/* Test: Normal object initialization */
static void test_object_init_normal(void)
{
    mr_object_t obj;
    mr_err_t ret;

    mr_memset(&obj, 0, sizeof(obj));
    ret = mr_object_init(&obj, MR_CLAZZES_STATIK(MR_CLAZZES_FIND(test_object)));

    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&obj), MR_TRUE);
    MR_TEST_ASSERT_EQ_PTR(MR_OBJECT_CLAZZ(&obj), MR_CLAZZES_STATIK(MR_CLAZZES_FIND(test_object)));

    mr_object_del(&obj);
}
MR_TEST_EXPORT(object, init_normal, test_object_init_normal);

/* -------------------------------------------------------------------------- */
/* Test: Initialize with NULL object pointer */
static void test_object_init_null_obj(void)
{
    mr_err_t ret;

    ret = mr_object_init(MR_NULL, MR_CLAZZES_STATIK(MR_CLAZZES_FIND(test_object)));
    MR_TEST_ASSERT_EQ_INT(ret, -MR_EINVAL);
}
MR_TEST_EXPORT(object, init_null_obj, test_object_init_null_obj);

/* -------------------------------------------------------------------------- */
/* Test: Re-initialize already initialized object */
static void test_object_init_already_inited(void)
{
    mr_object_t obj;
    mr_err_t ret;

    mr_object_init(&obj, MR_CLAZZES_STATIK(MR_CLAZZES_FIND(test_object)));
    ret = mr_object_init(&obj, MR_CLAZZES_DYNAMIC(MR_CLAZZES_FIND(test_object)));

    MR_TEST_ASSERT_EQ_INT(ret, -MR_EINVAL);
    mr_object_del(&obj);
}
MR_TEST_EXPORT(object, init_already_inited, test_object_init_already_inited);

/* -------------------------------------------------------------------------- */
/* Test: Normal object deletion */
static void test_object_del_normal(void)
{
    mr_object_t obj;

    test_object_dynamic_release_called = 0;
    mr_object_init(&obj, MR_CLAZZES_DYNAMIC(MR_CLAZZES_FIND(test_object)));
    mr_object_del(&obj);

    MR_TEST_ASSERT_EQ_INT(test_object_dynamic_release_called, 1);
}
MR_TEST_EXPORT(object, del_normal, test_object_del_normal);

/* -------------------------------------------------------------------------- */
/* Test: Delete NULL pointer */
static void test_object_del_null(void)
{
    test_object_statik_release_called = 0;
    mr_object_del(MR_NULL);
    MR_TEST_ASSERT_EQ_INT(test_object_statik_release_called, 0);
}
MR_TEST_EXPORT(object, del_null, test_object_del_null);

/* -------------------------------------------------------------------------- */
/* Test: Delete uninitialized object */
static void test_object_del_uninit(void)
{
    mr_object_t obj;

    test_object_statik_release_called = 0;
    mr_memset(&obj, 0, sizeof(obj));
    mr_object_del(&obj);

    MR_TEST_ASSERT_EQ_INT(test_object_statik_release_called, 0);
}
MR_TEST_EXPORT(object, del_uninit, test_object_del_uninit);

/* -------------------------------------------------------------------------- */
/* Test: Reference counting with get/put */
static void test_object_get_put_refcount(void)
{
    mr_object_t obj;
    mr_object_t *ret_obj;
    mr_bool_t ret_bool;

    test_object_statik_release_called = 0;
    mr_object_init(&obj, MR_CLAZZES_STATIK(MR_CLAZZES_FIND(test_object)));

    ret_obj = mr_object_get(&obj);
    MR_TEST_ASSERT_EQ_PTR(ret_obj, &obj);

    ret_obj = mr_object_get(&obj);
    MR_TEST_ASSERT_EQ_PTR(ret_obj, &obj);

    ret_bool = mr_object_put(&obj);
    MR_TEST_ASSERT_EQ_INT(ret_bool, MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(test_object_statik_release_called, 0);

    ret_bool = mr_object_put(&obj);
    MR_TEST_ASSERT_EQ_INT(ret_bool, MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(test_object_statik_release_called, 0);

    ret_bool = mr_object_put(&obj);
    MR_TEST_ASSERT_EQ_INT(ret_bool, MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(test_object_statik_release_called, 1);
}
MR_TEST_EXPORT(object, get_put_refcount, test_object_get_put_refcount);

/* -------------------------------------------------------------------------- */
/* Test: Get NULL pointer */
static void test_object_get_null(void)
{
    mr_object_t *ret_obj;

    ret_obj = mr_object_get(MR_NULL);
    MR_TEST_ASSERT_EQ_PTR(ret_obj, MR_NULL);
}
MR_TEST_EXPORT(object, get_null, test_object_get_null);

/* -------------------------------------------------------------------------- */
/* Test: Get uninitialized object */
static void test_object_get_uninit(void)
{
    mr_object_t obj;
    mr_object_t *ret_obj;

    mr_memset(&obj, 0, sizeof(obj));
    ret_obj = mr_object_get(&obj);
    MR_TEST_ASSERT_EQ_PTR(ret_obj, MR_NULL);
}
MR_TEST_EXPORT(object, get_uninit, test_object_get_uninit);

/* -------------------------------------------------------------------------- */
/* Test: Put NULL pointer */
static void test_object_put_null(void)
{
    mr_bool_t ret_bool;

    ret_bool = mr_object_put(MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret_bool, MR_TRUE);
}
MR_TEST_EXPORT(object, put_null, test_object_put_null);

/* -------------------------------------------------------------------------- */
/* Test: Put uninitialized object */
static void test_object_put_uninit(void)
{
    mr_object_t obj;
    mr_bool_t ret_bool;

    mr_memset(&obj, 0, sizeof(obj));
    ret_bool = mr_object_put(&obj);
    MR_TEST_ASSERT_EQ_INT(ret_bool, MR_TRUE);
}
MR_TEST_EXPORT(object, put_uninit, test_object_put_uninit);

/* -------------------------------------------------------------------------- */
/* Test: MR_OBJECT_IS_CLAZZES macro */
static void test_object_is_clazzes(void)
{
    mr_object_t obj;

    mr_object_init(&obj, MR_CLAZZES_STATIK(MR_CLAZZES_FIND(test_object)));
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_CLAZZES(&obj, test_object), MR_TRUE);

    mr_object_del(&obj);
}
MR_TEST_EXPORT(object, is_clazzes, test_object_is_clazzes);

/* -------------------------------------------------------------------------- */
/* Test: Complete object lifecycle */
static void test_object_lifecycle(void)
{
    mr_object_t obj;
    mr_object_t *obj1, *obj2, *obj3;

    test_object_dynamic_release_called = 0;
    mr_object_init(&obj, MR_CLAZZES_DYNAMIC(MR_CLAZZES_FIND(test_object)));

    obj1 = mr_object_get(&obj);
    obj2 = mr_object_get(&obj);
    obj3 = mr_object_get(&obj);

    MR_TEST_ASSERT_EQ_PTR(obj1, &obj);
    MR_TEST_ASSERT_EQ_PTR(obj2, &obj);
    MR_TEST_ASSERT_EQ_PTR(obj3, &obj);

    MR_TEST_ASSERT_EQ_INT(mr_object_put(&obj), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(mr_object_put(&obj), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(mr_object_put(&obj), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(mr_object_put(&obj), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(test_object_dynamic_release_called, 1);
}
MR_TEST_EXPORT(object, lifecycle, test_object_lifecycle);
#endif /* defined(MR_USE_TEST) */

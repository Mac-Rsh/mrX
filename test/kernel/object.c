/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mr-X/mr_object.h>

typedef struct test_obj {
    mr_object_t obj;
    int data;
} test_obj_t;

static void test_obj_release(struct mr_object *obj) {
    test_obj_t *t = (test_obj_t *)obj;
    t->data = -1;
}

static const mr_clazz_t test_clazz = MR_CLAZZ_INIT("test", test_obj_release);

/* -------------------------------------------------------------------------- */
/* Test: Static initializer                                                   */
static void test_object_static_init(void) {
    static test_obj_t o
        = {.obj = MR_OBJECT_INIT(&o.o, &test_clazz), .data = 42};

    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&o.obj), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_CLAZZ_IS(&o.obj, &test_clazz), MR_TRUE);
}
MR_TEST_EXPORT(object, static_init, test_object_static_init);

/* -------------------------------------------------------------------------- */
/* Test: Dynamic init / del                                                   */
static void test_object_init_del(void) {
    test_obj_t o;

    MR_TEST_ASSERT_EQ_INT(mr_object_init(&o.obj, &test_clazz), 0);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_IS_INITED(&o.obj), MR_TRUE);

    mr_object_del(&o.obj);
    MR_TEST_ASSERT_EQ_INT(o.data, -1);
}
MR_TEST_EXPORT(object, init_del, test_object_init_del);

/* -------------------------------------------------------------------------- */
/* Test: Get / put reference counting                                         */
static void test_object_refcount(void) {
    mr_object_t *p;
    test_obj_t o;

    mr_object_init(&o.obj, &test_clazz);

    p = mr_object_get(&o.obj);
    MR_TEST_ASSERT_EQ_PTR(p, &o.obj);

    p = mr_object_get(&o.obj);
    MR_TEST_ASSERT_EQ_PTR(p, &o.obj);
    MR_TEST_ASSERT_EQ_INT(mr_object_put(&o.obj), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(mr_object_put(&o.obj), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(mr_object_put(&o.obj), MR_TRUE);
}
MR_TEST_EXPORT(object, refcount, test_object_refcount);

/* -------------------------------------------------------------------------- */
/* Test: Null and double-init protection                                      */
static void test_object_invalid(void) {
    test_obj_t o;

    MR_TEST_ASSERT_EQ_INT(mr_object_init(MR_NULL, &test_clazz), -MR_EINVAL);
    MR_TEST_ASSERT_EQ_INT(mr_object_init(&o.obj, MR_NULL), 0);

    mr_object_init(&o.obj, &test_clazz);
    MR_TEST_ASSERT_EQ_INT(mr_object_init(&o.obj, &test_clazz), -MR_EINVAL);
}
MR_TEST_EXPORT(object, invalid, test_object_invalid);

/* -------------------------------------------------------------------------- */
/* Test: MR_OBJECT_CLAZZ_IS_OR                                                */
static void test_object_clazz_or(void) {
    test_obj_t o;

    mr_object_init(&o.obj, &test_clazz);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_CLAZZ_IS_OR(&o.obj, &test_clazz, MR_NULL),
                          MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_CLAZZ_IS_OR(&o.obj, MR_NULL, &test_clazz),
                          MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(MR_OBJECT_CLAZZ_IS_OR(&o.obj, MR_NULL, MR_NULL),
                          MR_FALSE);
}
MR_TEST_EXPORT(object, clazz_or, test_object_clazz_or);
#endif /* defined(MR_USE_TEST) */

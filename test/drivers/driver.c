/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_DRIVER)
#include <mrX/drivers/mr_driver.h>
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>

/* Mock state */
static int test_driver_init_count;
static int test_driver_deinit_count;
static int test_driver_release_count;

static void test_driver_reset(void) {
    test_driver_init_count = 0;
    test_driver_deinit_count = 0;
    test_driver_release_count = 0;
}

static mr_err_t test_driver_init(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    test_driver_init_count++;
    return 0;
}

static mr_err_t test_driver_deinit(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    test_driver_deinit_count++;
    return 0;
}

static void test_driver_release(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    test_driver_release_count++;
}

static mr_err_t test_driver_init_fail(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    test_driver_init_count++;
    return -1;
}

static mr_err_t test_driver_deinit_fail(const mr_driver_t *drv) {
    MR_IGNORE(drv);
    test_driver_deinit_count++;
    return -1;
}

static const mr_driver_clazz_t test_driver_clazz = MR_DRIVER_CLAZZ_INIT(
    "test_driver", test_driver_init, test_driver_deinit, MR_NULL);

static const mr_driver_clazz_t test_driver_clazz_fail = MR_DRIVER_CLAZZ_INIT(
    "test_driver_fail", test_driver_init_fail, test_driver_deinit, MR_NULL);

static const mr_driver_clazz_t test_driver_release_clazz
    = MR_DRIVER_CLAZZ_INIT("test_driver_release", test_driver_init,
                           test_driver_deinit, test_driver_release);

static const mr_driver_clazz_t test_driver_no_method_clazz
    = MR_DRIVER_CLAZZ_INIT("test_driver_no_method", MR_NULL, MR_NULL, MR_NULL);

static const mr_driver_clazz_t test_driver_deinit_fail_clazz
    = MR_DRIVER_CLAZZ_INIT("test_driver_deinit_fail", test_driver_init,
                           test_driver_deinit_fail, MR_NULL);

MR_DRIVER_CLAZZES_DEFINE(test_driver_pair, test_driver_init, test_driver_deinit,
                         test_driver_release);
MR_DRIVER_CLAZZES_IMPORT(test_driver_pair);

MR_DRIVER_DEFINE(test_driver_uninstalled,
                 MR_DRIVER_CLAZZES_STATIK(test_driver_pair), MR_DRIVER_TYPE_RAM,
                 MR_NULL, MR_NULL, MR_NULL);
MR_DRIVER_IMPORT(test_driver_uninstalled);

MR_TEST(driver, del_without_setup) {
    mr_driver_t *drv = MR_NULL;
    mr_err_t ret;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);

    ret = mr_driver_del(drv);
    drv = MR_NULL;

    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_deinit_count, 0);
}

MR_TEST(driver, del_after_setup) {
    mr_driver_t *drv = MR_NULL;
    mr_err_t ret;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);

    ret = mr_driver_setup(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_init_count, 1);

    ret = mr_driver_del(drv);
    drv = MR_NULL;

    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_deinit_count, 1);
}

MR_TEST(driver, create_default) {
    mr_driver_t *drv = MR_NULL;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(MR_DRIVER_TYPE(drv), MR_DRIVER_TYPE_RAM);

    mr_driver_del(drv);
}

MR_TEST(driver, setup_twice) {
    mr_driver_t *drv = MR_NULL;
    mr_err_t ret;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);

    ret = mr_driver_setup(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_init_count, 1);

    ret = mr_driver_setup(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_init_count, 1);

    mr_driver_del(drv);
}

MR_TEST(driver, shutdown_without_setup) {
    mr_driver_t *drv = MR_NULL;
    mr_err_t ret;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);

    ret = mr_driver_shutdown(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_deinit_count, 0);

    mr_driver_del(drv);
}

MR_TEST(driver, setup_shutdown_cycle) {
    mr_driver_t *drv = MR_NULL;
    mr_err_t ret;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);

    ret = mr_driver_setup(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_init_count, 1);

    ret = mr_driver_shutdown(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_deinit_count, 1);

    mr_driver_del(drv);
}

MR_TEST(driver, setup_shutdown_multiple) {
    mr_driver_t *drv = MR_NULL;
    mr_err_t ret;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);

    ret = mr_driver_setup(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_init_count, 1);

    ret = mr_driver_shutdown(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_deinit_count, 1);

    ret = mr_driver_setup(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_init_count, 2);

    ret = mr_driver_shutdown(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_deinit_count, 2);

    mr_driver_del(drv);
}

MR_TEST(driver, release_without_setup) {
    mr_driver_t *drv = MR_NULL;
    mr_err_t ret;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_release_clazz, MR_DRIVER_TYPE_RAM,
                           MR_NULL, MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);

    ret = mr_driver_del(drv);

    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_init_count, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_deinit_count, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_release_count, 1);
}

MR_TEST(driver, release_after_shutdown) {
    mr_driver_t *drv = MR_NULL;
    mr_err_t ret;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_release_clazz, MR_DRIVER_TYPE_RAM,
                           MR_NULL, MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);

    ret = mr_driver_setup(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);

    ret = mr_driver_shutdown(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_release_count, 0);

    ret = mr_driver_setup(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);

    ret = mr_driver_del(drv);

    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_init_count, 2);
    MR_TEST_EXPECT_EQ_INT(test_driver_deinit_count, 2);
    MR_TEST_EXPECT_EQ_INT(test_driver_release_count, 1);
}

MR_TEST(driver, setup_fail) {
    mr_driver_t *drv = MR_NULL;
    mr_err_t ret;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz_fail, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);

    ret = mr_driver_setup(drv);
    MR_TEST_EXPECT_EQ_INT(ret, -1);
    MR_TEST_EXPECT_EQ_INT(test_driver_init_count, 1);
    MR_TEST_EXPECT_EQ_INT(mr_driver_state(drv), -1);

    mr_driver_del(drv);
}

MR_TEST(driver, is_ready_null) {
    mr_bool_t ready;

    test_driver_reset();

    ready = mr_driver_is_ready(MR_NULL);
    MR_TEST_EXPECT_EQ_INT(ready, MR_FALSE);
}

MR_TEST(driver, is_ready_not_setup) {
    mr_driver_t *drv = MR_NULL;
    mr_bool_t ready;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);

    ready = mr_driver_is_ready(drv);
    MR_TEST_EXPECT_EQ_INT(ready, MR_FALSE);

    mr_driver_del(drv);
}

MR_TEST(driver, is_ready_after_setup) {
    mr_driver_t *drv = MR_NULL;
    mr_bool_t ready;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(mr_driver_setup(drv), 0);

    ready = mr_driver_is_ready(drv);
    MR_TEST_EXPECT_EQ_INT(ready, MR_TRUE);

    mr_driver_del(drv);
}

MR_TEST(driver, is_ready_after_shutdown) {
    mr_driver_t *drv = MR_NULL;
    mr_bool_t ready;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(drv), 0);

    ready = mr_driver_is_ready(drv);
    MR_TEST_EXPECT_EQ_INT(ready, MR_FALSE);

    mr_driver_del(drv);
}

MR_TEST(driver, is_ready_setup_fail) {
    mr_driver_t *drv = MR_NULL;
    mr_bool_t ready;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz_fail, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    mr_driver_setup(drv);

    ready = mr_driver_is_ready(drv);
    MR_TEST_EXPECT_EQ_INT(ready, MR_FALSE);

    mr_driver_del(drv);
}

MR_TEST(driver, state_operations) {
    mr_driver_t *drv = MR_NULL;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);

    MR_TEST_EXPECT_EQ_INT(mr_driver_state(drv), 0);

    mr_driver_state_set(drv, -1);
    MR_TEST_EXPECT_EQ_INT(mr_driver_state(drv), -1);

    mr_driver_state_set(drv, -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(mr_driver_state(drv), -MR_EINVAL);

    mr_driver_state_set(drv, 0);
    MR_TEST_EXPECT_EQ_INT(mr_driver_state(drv), 0);

    mr_driver_del(drv);
}

MR_TEST(driver, type_operations) {
    mr_driver_t *drv = MR_NULL;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_GPIO, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);

    MR_TEST_EXPECT_EQ_INT((int)mr_driver_type(drv), MR_DRIVER_TYPE_GPIO);
    MR_TEST_EXPECT_EQ_INT(MR_DRIVER_IS_TYPE(drv, MR_DRIVER_TYPE_GPIO), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(MR_DRIVER_IS_TYPE(drv, MR_DRIVER_TYPE_RAM), MR_FALSE);

    mr_driver_del(drv);
}

MR_TEST(driver, data_priv_ops) {
    mr_driver_t *drv = MR_NULL;
    int data_val;
    int priv_val;
    int ops_val;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, &data_val,
                           &priv_val, &ops_val);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);

    MR_TEST_EXPECT_EQ_PTR(MR_DRIVER_DATA(drv), &data_val);
    MR_TEST_EXPECT_EQ_PTR(MR_DRIVER_PRIV(drv), &priv_val);
    MR_TEST_EXPECT_EQ_PTR(MR_DRIVER_OPS(drv), &ops_val);

    mr_driver_del(drv);
}

MR_TEST(driver, init_static) {
    mr_driver_t static_drv;
    mr_err_t ret;

    mr_memset(&static_drv, 0, sizeof(static_drv));
    test_driver_reset();

    ret = mr_driver_init(&static_drv, &test_driver_clazz, MR_DRIVER_TYPE_RAM,
                         MR_NULL, MR_NULL, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(ret, 0);

    ret = mr_driver_del(&static_drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_deinit_count, 0);
}

MR_TEST(driver, init_static_shutdown) {
    mr_driver_t static_drv;
    mr_err_t ret;

    mr_memset(&static_drv, 0, sizeof(static_drv));
    test_driver_reset();

    ret = mr_driver_init(&static_drv, &test_driver_clazz, MR_DRIVER_TYPE_RAM,
                         MR_NULL, MR_NULL, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(ret, 0);

    ret = mr_driver_shutdown(&static_drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_deinit_count, 0);

    mr_driver_del(&static_drv);
}

MR_TEST(driver, init_static_lifecycle) {
    mr_driver_t static_drv;
    mr_err_t ret;

    mr_memset(&static_drv, 0, sizeof(static_drv));
    test_driver_reset();

    ret = mr_driver_init(&static_drv, &test_driver_clazz, MR_DRIVER_TYPE_RAM,
                         MR_NULL, MR_NULL, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(ret, 0);

    ret = mr_driver_setup(&static_drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_init_count, 1);

    ret = mr_driver_shutdown(&static_drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_deinit_count, 1);

    mr_driver_del(&static_drv);
}

MR_TEST(driver, get_put) {
    mr_driver_t *drv = MR_NULL;
    mr_bool_t ret;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);

    MR_TEST_EXPECT_EQ_PTR(mr_driver_get(drv), drv);

    ret = mr_driver_put(drv);
    MR_TEST_EXPECT_EQ_INT(ret, MR_FALSE);

    ret = mr_driver_put(drv);
    MR_TEST_EXPECT_EQ_INT(ret, MR_TRUE);
}

MR_TEST(driver, setup_without_method) {
    mr_driver_t *drv = MR_NULL;
    mr_err_t ret;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_no_method_clazz, MR_DRIVER_TYPE_RAM,
                           MR_NULL, MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);

    ret = mr_driver_setup(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_init_count, 0);
    MR_TEST_EXPECT_EQ_INT(mr_driver_state(drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(drv), MR_TRUE);

    ret = mr_driver_shutdown(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_deinit_count, 0);
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(drv), MR_FALSE);

    mr_driver_del(drv);
}

MR_TEST(driver, shutdown_fail_keeps_setup) {
    mr_driver_t *drv = MR_NULL;
    mr_err_t ret;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_deinit_fail_clazz, MR_DRIVER_TYPE_RAM,
                           MR_NULL, MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);

    ret = mr_driver_setup(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_init_count, 1);

    ret = mr_driver_shutdown(drv);
    MR_TEST_EXPECT_EQ_INT(ret, -1);
    MR_TEST_EXPECT_EQ_INT(test_driver_deinit_count, 1);
    MR_TEST_EXPECT_EQ_INT(mr_driver_state(drv), -1);
    MR_TEST_EXPECT_EQ_INT(MR_DRIVER_IS_SETUP(drv), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(drv), MR_FALSE);

    /* Idempotent setup recovers ready without re-running init */
    ret = mr_driver_setup(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_init_count, 1);
    MR_TEST_EXPECT_EQ_INT(mr_driver_state(drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(drv), MR_TRUE);

    ret = mr_driver_shutdown(drv);
    MR_TEST_EXPECT_EQ_INT(ret, -1);
    MR_TEST_EXPECT_EQ_INT(test_driver_deinit_count, 2);
    MR_TEST_EXPECT_EQ_INT(MR_DRIVER_IS_SETUP(drv), MR_TRUE);

    mr_driver_del(drv);
    MR_TEST_EXPECT_EQ_INT(test_driver_deinit_count, 3);
}

MR_TEST(driver, delete_with_extra_reference) {
    mr_driver_t *drv = MR_NULL;
    mr_err_t ret;
    mr_bool_t put_ret;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_release_clazz, MR_DRIVER_TYPE_RAM,
                           MR_NULL, MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);
    MR_TEST_EXPECT_EQ_PTR(mr_driver_get(drv), drv);

    ret = mr_driver_del(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_release_count, 0);

    put_ret = mr_driver_put(drv);
    MR_TEST_EXPECT_EQ_INT(put_ret, MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(test_driver_release_count, 1);
}

MR_TEST(driver, find_internal_and_unknown) {
    mr_driver_t *drv;

    test_driver_reset();

    drv = mr_driver_find("start");
    MR_TEST_EXPECT_EQ_PTR(drv, MR_NULL);

    drv = mr_driver_find("defer");
    MR_TEST_EXPECT_EQ_PTR(drv, MR_NULL);

    drv = mr_driver_find("end");
    MR_TEST_EXPECT_EQ_PTR(drv, MR_NULL);

    drv = mr_driver_find("driver_not_exist");
    MR_TEST_EXPECT_EQ_PTR(drv, MR_NULL);
}

MR_TEST(driver, clazz_pair_and_symbol_lookup) {
    const mr_driver_clazzes_t *clazzes;
    mr_driver_t *defined, *dynamic;

    test_driver_reset();
    clazzes = MR_DRIVER_CLAZZES_FIND(test_driver_pair);
    defined = MR_DRIVER_FIND(test_driver_uninstalled);
    MR_TEST_EXPECT_EQ_PTR(MR_DRIVER_CLAZZES_STATIK(test_driver_pair),
                          &clazzes->statik);
    MR_TEST_EXPECT_EQ_PTR(MR_DRIVER_CLAZZES_DYNAMIC(test_driver_pair),
                          &clazzes->dynamic);
    MR_TEST_EXPECT_EQ_INT(mr_strcmp(clazzes->statik.name, "test_driver_pair"),
                          0);
    MR_TEST_EXPECT_EQ_PTR(mr_driver_find("test_driver_uninstalled"), MR_NULL);
    MR_DRIVER_ASSERT_TYPE(defined, MR_DRIVER_TYPE_RAM);

    MR_TEST_EXPECT_EQ_INT(mr_driver_setup(defined), 0);
    MR_DRIVER_ASSERT(defined, MR_DRIVER_TYPE_RAM);
    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(defined), 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_init_count, 1);
    MR_TEST_EXPECT_EQ_INT(test_driver_deinit_count, 1);
    MR_TEST_EXPECT_EQ_INT(test_driver_release_count, 0);

    dynamic = mr_driver_create(&clazzes->dynamic, MR_DRIVER_TYPE_RAM, MR_NULL,
                               MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(dynamic, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(mr_driver_setup(dynamic), 0);
    MR_TEST_EXPECT_EQ_INT(mr_driver_del(dynamic), 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_init_count, 2);
    MR_TEST_EXPECT_EQ_INT(test_driver_deinit_count, 2);
    MR_TEST_EXPECT_EQ_INT(test_driver_release_count, 1);
}

MR_TEST(driver, static_final_put_and_readiness) {
    mr_driver_t drv;

    test_driver_reset();
    MR_TEST_ASSERT_EQ_INT(mr_object_init(&drv.parent, MR_NULL), 0);
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(&drv), MR_FALSE);
    mr_object_del(&drv.parent);

    MR_TEST_ASSERT_EQ_INT(
        mr_driver_init(&drv, MR_DRIVER_CLAZZES_STATIK(test_driver_pair),
                       MR_DRIVER_TYPE_RAM, MR_NULL, MR_NULL, MR_NULL),
        0);
    MR_TEST_EXPECT_EQ_INT(mr_driver_setup(&drv), 0);
    mr_driver_state_set(&drv, -MR_EIO);
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(&drv), MR_FALSE);
    mr_driver_state_set(&drv, 0);
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(&drv), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(mr_driver_put(&drv), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(test_driver_deinit_count, 1);
    MR_TEST_EXPECT_EQ_INT(test_driver_release_count, 0);
    MR_TEST_EXPECT_EQ_PTR(mr_driver_get(&drv), MR_NULL);
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(&drv), MR_FALSE);
}

#if defined(MR_USE_MALLOC)
typedef struct test_driver_allocation {
    struct test_driver_allocation *next;
} test_driver_allocation_t;

MR_TEST(driver, create_when_heap_exhausted) {
    test_driver_allocation_t *blocks, *block;
    mr_driver_t *drv;

    blocks = MR_NULL;
    while ((block = mr_malloc(sizeof(mr_driver_t))) != MR_NULL) {
        block->next = blocks;
        blocks = block;
    }
    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_EXPECT_EQ_PTR(drv, MR_NULL);
    if (drv) {
        mr_driver_del(drv);
    }
    while (blocks) {
        block = blocks;
        blocks = block->next;
        mr_free(block);
    }
}
#endif /* defined(MR_USE_MALLOC) */

#define MR_HT_COMPAT test_driver_core
#if MR_HT_HAS_DRIVER()
MR_HT_DRIVER_FOREACH(MR_HT_DRIVER_INSTALL,
                     MR_DRIVER_CLAZZES_STATIK(test_driver_pair),
                     MR_DRIVER_TYPE_RAM, MR_NULL, MR_NULL, MR_NULL);

#define TEST_DRIVER_CELL(_elem, _node, _prop)                                  \
    MR_HT_NODE_PROP_ELEM(_node, _prop, _elem, 1)

static const mr_uint32_t __test_driver_cells[] = {
    MR_HT_DRIVER_PROP_ELEM_FOREACH(0, test_values, TEST_DRIVER_CELL),
};

MR_TEST(driver, hardtree_properties_and_registration) {
    mr_driver_t *automatic, *deferred, *found;

    automatic = MR_HT_DRIVER_FIND(MR_HT_LABEL(driver_test_auto));
    deferred = MR_HT_DRIVER_FIND(MR_HT_DRIVER(1));
    MR_TEST_EXPECT_EQ_INT(MR_HT_HAS_DRIVER(), MR_TRUE);
    MR_TEST_EXPECT_EQ_UINT(MR_HT_DRIVER_NUM(), 2U);
    MR_TEST_EXPECT_EQ_INT(
        mr_strcmp(MR_STR(MR_HT_DRIVER_NAME(0)), "driver_test_auto_12"), 0);
    MR_TEST_EXPECT_EQ_INT(
        mr_strcmp(MR_HT_DRIVER_PATH(0), "/driver-test/driver-test-auto@12"), 0);
    MR_TEST_EXPECT_EQ_INT(MR_HT_DRIVER_HAS_PARENT(0), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(
        mr_strcmp(MR_HT_NODE_PATH(MR_HT_DRIVER_PARENT(0)), "/driver-test"), 0);
    MR_TEST_EXPECT_EQ_INT(MR_HT_DRIVER_HAS_ADDR(0), MR_TRUE);
    MR_TEST_EXPECT_EQ_UINT(MR_HT_DRIVER_ADDR(0), 0x12U);
    MR_TEST_EXPECT_EQ_INT(MR_HT_DRIVER_HAS_ADDR(1), MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(MR_HT_DRIVER_HAS_PROP(0, test_values), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(MR_HT_DRIVER_HAS_PROP(1, test_values), MR_FALSE);
    MR_TEST_EXPECT_EQ_UINT(MR_HT_DRIVER_PROP_OR(1, test_values, 21U), 21U);
    MR_TEST_EXPECT_EQ_UINT(MR_HT_DRIVER_PROP(0, test_value), 7U);
    MR_TEST_EXPECT_EQ_UINT(MR_HT_DRIVER_PROP_OR(0, test_value, 21U), 7U);
    MR_TEST_EXPECT_EQ_UINT(MR_HT_DRIVER_PROP_ELEM(0, test_values, 0, 0), 3U);
    MR_TEST_EXPECT_EQ_INT(MR_HT_DRIVER_HAS_PROP_ELEM(0, test_values, 1, 1),
                          MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(MR_HT_DRIVER_HAS_PROP_ELEM(0, test_values, 1, 2),
                          MR_FALSE);
    MR_TEST_EXPECT_EQ_UINT(MR_HT_DRIVER_PROP_ELEM_OR(0, test_values, 1, 1, 21U),
                           13U);
    MR_TEST_EXPECT_EQ_UINT(MR_HT_DRIVER_PROP_ELEM_OR(0, test_values, 1, 2, 21U),
                           21U);
    MR_TEST_EXPECT_EQ_UINT(MR_ARRAY_SIZE(__test_driver_cells), 2U);
    MR_TEST_EXPECT_EQ_UINT(__test_driver_cells[0], 5U);
    MR_TEST_EXPECT_EQ_UINT(__test_driver_cells[1], 13U);

    found = mr_driver_find(MR_STR(MR_HT_DRIVER_NAME(0)));
    MR_TEST_EXPECT_EQ_PTR(found, automatic);
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(automatic), MR_TRUE);
    if (found) {
        MR_TEST_EXPECT_EQ_INT(mr_driver_put(found), MR_FALSE);
    }
    found = mr_driver_find(MR_STR(MR_HT_DRIVER_NAME(1)));
    MR_TEST_EXPECT_EQ_PTR(found, deferred);
    MR_TEST_EXPECT_EQ_INT(mr_driver_is_ready(deferred), MR_FALSE);
    if (found) {
        MR_TEST_EXPECT_EQ_INT(mr_driver_put(found), MR_FALSE);
    }
    MR_TEST_EXPECT_EQ_PTR(mr_driver_find("driver_test_disabled"), MR_NULL);
}
#endif /* MR_HT_HAS_DRIVER() */
#undef MR_HT_COMPAT

#endif /* defined(MR_USE_DRIVER) */
#endif /* defined(MR_USE_TEST) */

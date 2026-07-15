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

static const mr_driver_clazz_t test_driver_clazz
    = MR_DRIVER_CLAZZ_INIT("test_driver", test_driver_init,
                           test_driver_deinit, MR_NULL);

static const mr_driver_clazz_t test_driver_clazz_fail
    = MR_DRIVER_CLAZZ_INIT("test_driver_fail", test_driver_init_fail,
                           test_driver_deinit, MR_NULL);

static const mr_driver_clazz_t test_driver_release_clazz
    = MR_DRIVER_CLAZZ_INIT("test_driver_release", test_driver_init,
                           test_driver_deinit, test_driver_release);

/* Tests */
static void test_driver_del_without_setup(void) {
    mr_driver_t *drv = MR_NULL;
    mr_err_t ret;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_EXPECT_NE_PTR(drv, MR_NULL);

    ret = mr_driver_del(drv);
    drv = MR_NULL;

    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_deinit_count, 0);
}
MR_TEST_EXPORT(driver, del_without_setup, test_driver_del_without_setup);

static void test_driver_del_after_setup(void) {
    mr_driver_t *drv = MR_NULL;
    mr_err_t ret;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_EXPECT_NE_PTR(drv, MR_NULL);

    ret = mr_driver_setup(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_init_count, 1);

    ret = mr_driver_del(drv);
    drv = MR_NULL;

    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_deinit_count, 1);
}
MR_TEST_EXPORT(driver, del_after_setup, test_driver_del_after_setup);

static void test_driver_create_default(void) {
    mr_driver_t *drv = MR_NULL;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_EXPECT_NE_PTR(drv, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(MR_DRIVER_TYPE(drv), MR_DRIVER_TYPE_RAM);

    mr_driver_del(drv);
}
MR_TEST_EXPORT(driver, create_default, test_driver_create_default);

static void test_driver_setup_twice(void) {
    mr_driver_t *drv = MR_NULL;
    mr_err_t ret;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_EXPECT_NE_PTR(drv, MR_NULL);

    ret = mr_driver_setup(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_init_count, 1);

    ret = mr_driver_setup(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_init_count, 1);

    mr_driver_del(drv);
}
MR_TEST_EXPORT(driver, setup_twice, test_driver_setup_twice);

static void test_driver_shutdown_without_setup(void) {
    mr_driver_t *drv = MR_NULL;
    mr_err_t ret;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_EXPECT_NE_PTR(drv, MR_NULL);

    ret = mr_driver_shutdown(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_deinit_count, 0);

    mr_driver_del(drv);
}
MR_TEST_EXPORT(driver, shutdown_without_setup,
               test_driver_shutdown_without_setup);

static void test_driver_setup_shutdown_cycle(void) {
    mr_driver_t *drv = MR_NULL;
    mr_err_t ret;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_EXPECT_NE_PTR(drv, MR_NULL);

    ret = mr_driver_setup(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_init_count, 1);

    ret = mr_driver_shutdown(drv);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_deinit_count, 1);

    mr_driver_del(drv);
}
MR_TEST_EXPORT(driver, setup_shutdown_cycle, test_driver_setup_shutdown_cycle);

static void test_driver_setup_shutdown_multiple(void) {
    mr_driver_t *drv = MR_NULL;
    mr_err_t ret;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_EXPECT_NE_PTR(drv, MR_NULL);

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
MR_TEST_EXPORT(driver, setup_shutdown_multiple,
               test_driver_setup_shutdown_multiple);

static void test_driver_release_without_setup(void) {
    mr_driver_t *drv = MR_NULL;
    mr_err_t ret;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_release_clazz, MR_DRIVER_TYPE_RAM,
                           MR_NULL, MR_NULL, MR_NULL);
    MR_TEST_EXPECT_NE_PTR(drv, MR_NULL);

    ret = mr_driver_del(drv);

    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_init_count, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_deinit_count, 0);
    MR_TEST_EXPECT_EQ_INT(test_driver_release_count, 1);
}
MR_TEST_EXPORT(driver, release_without_setup,
               test_driver_release_without_setup);

static void test_driver_release_after_shutdown(void) {
    mr_driver_t *drv = MR_NULL;
    mr_err_t ret;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_release_clazz, MR_DRIVER_TYPE_RAM,
                           MR_NULL, MR_NULL, MR_NULL);
    MR_TEST_EXPECT_NE_PTR(drv, MR_NULL);

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
MR_TEST_EXPORT(driver, release_after_shutdown,
               test_driver_release_after_shutdown);

static void test_driver_setup_fail(void) {
    mr_driver_t *drv = MR_NULL;
    mr_err_t ret;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz_fail, MR_DRIVER_TYPE_RAM,
                           MR_NULL, MR_NULL, MR_NULL);
    MR_TEST_EXPECT_NE_PTR(drv, MR_NULL);

    ret = mr_driver_setup(drv);
    MR_TEST_EXPECT_EQ_INT(ret, -1);
    MR_TEST_EXPECT_EQ_INT(test_driver_init_count, 1);
    MR_TEST_EXPECT_EQ_INT(mr_driver_state(drv), -1);

    mr_driver_del(drv);
}
MR_TEST_EXPORT(driver, setup_fail, test_driver_setup_fail);

static void test_driver_is_ready_null(void) {
    mr_bool_t ready;

    test_driver_reset();

    ready = mr_driver_is_ready(MR_NULL);
    MR_TEST_EXPECT_EQ_INT(ready, MR_FALSE);
}
MR_TEST_EXPORT(driver, is_ready_null, test_driver_is_ready_null);

static void test_driver_is_ready_not_setup(void) {
    mr_driver_t *drv = MR_NULL;
    mr_bool_t ready;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_EXPECT_NE_PTR(drv, MR_NULL);

    ready = mr_driver_is_ready(drv);
    MR_TEST_EXPECT_EQ_INT(ready, MR_FALSE);

    mr_driver_del(drv);
}
MR_TEST_EXPORT(driver, is_ready_not_setup, test_driver_is_ready_not_setup);

static void test_driver_is_ready_after_setup(void) {
    mr_driver_t *drv = MR_NULL;
    mr_bool_t ready;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_EXPECT_NE_PTR(drv, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(mr_driver_setup(drv), 0);

    ready = mr_driver_is_ready(drv);
    MR_TEST_EXPECT_EQ_INT(ready, MR_TRUE);

    mr_driver_del(drv);
}
MR_TEST_EXPORT(driver, is_ready_after_setup, test_driver_is_ready_after_setup);

static void test_driver_is_ready_after_shutdown(void) {
    mr_driver_t *drv = MR_NULL;
    mr_bool_t ready;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_EXPECT_NE_PTR(drv, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(mr_driver_setup(drv), 0);
    MR_TEST_EXPECT_EQ_INT(mr_driver_shutdown(drv), 0);

    ready = mr_driver_is_ready(drv);
    MR_TEST_EXPECT_EQ_INT(ready, MR_FALSE);

    mr_driver_del(drv);
}
MR_TEST_EXPORT(driver, is_ready_after_shutdown,
               test_driver_is_ready_after_shutdown);

static void test_driver_is_ready_setup_fail(void) {
    mr_driver_t *drv = MR_NULL;
    mr_bool_t ready;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz_fail, MR_DRIVER_TYPE_RAM,
                           MR_NULL, MR_NULL, MR_NULL);
    MR_TEST_EXPECT_NE_PTR(drv, MR_NULL);
    mr_driver_setup(drv);

    ready = mr_driver_is_ready(drv);
    MR_TEST_EXPECT_EQ_INT(ready, MR_FALSE);

    mr_driver_del(drv);
}
MR_TEST_EXPORT(driver, is_ready_setup_fail, test_driver_is_ready_setup_fail);

static void test_driver_state_operations(void) {
    mr_driver_t *drv = MR_NULL;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_EXPECT_NE_PTR(drv, MR_NULL);

    MR_TEST_EXPECT_EQ_INT(mr_driver_state(drv), 0);

    mr_driver_state_set(drv, -1);
    MR_TEST_EXPECT_EQ_INT(mr_driver_state(drv), -1);

    mr_driver_state_set(drv, -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(mr_driver_state(drv), -MR_EINVAL);

    mr_driver_state_set(drv, 0);
    MR_TEST_EXPECT_EQ_INT(mr_driver_state(drv), 0);

    mr_driver_del(drv);
}
MR_TEST_EXPORT(driver, state_operations, test_driver_state_operations);

static void test_driver_type_operations(void) {
    mr_driver_t *drv = MR_NULL;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_GPIO, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_EXPECT_NE_PTR(drv, MR_NULL);

    MR_TEST_EXPECT_EQ_INT((int)mr_driver_type(drv), MR_DRIVER_TYPE_GPIO);
    MR_TEST_EXPECT_EQ_INT(MR_DRIVER_IS_TYPE(drv, MR_DRIVER_TYPE_GPIO),
                          MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(MR_DRIVER_IS_TYPE(drv, MR_DRIVER_TYPE_RAM),
                          MR_FALSE);

    mr_driver_del(drv);
}
MR_TEST_EXPORT(driver, type_operations, test_driver_type_operations);

static void test_driver_data_priv_ops(void) {
    mr_driver_t *drv = MR_NULL;
    int data_val;
    int priv_val;
    int ops_val;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM,
                           &data_val, &priv_val, &ops_val);
    MR_TEST_EXPECT_NE_PTR(drv, MR_NULL);

    MR_TEST_EXPECT_EQ_PTR(MR_DRIVER_DATA(drv), &data_val);
    MR_TEST_EXPECT_EQ_PTR(MR_DRIVER_PRIV(drv), &priv_val);
    MR_TEST_EXPECT_EQ_PTR(MR_DRIVER_OPS(drv), &ops_val);

    mr_driver_del(drv);
}
MR_TEST_EXPORT(driver, data_priv_ops, test_driver_data_priv_ops);

static void test_driver_init_static(void) {
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
MR_TEST_EXPORT(driver, init_static, test_driver_init_static);

static void test_driver_init_static_shutdown(void) {
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
MR_TEST_EXPORT(driver, init_static_shutdown, test_driver_init_static_shutdown);

static void test_driver_init_static_lifecycle(void) {
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
MR_TEST_EXPORT(driver, init_static_lifecycle,
               test_driver_init_static_lifecycle);

static void test_driver_get_put(void) {
    mr_driver_t *drv = MR_NULL;
    mr_bool_t ret;

    test_driver_reset();

    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM, MR_NULL,
                           MR_NULL, MR_NULL);
    MR_TEST_EXPECT_NE_PTR(drv, MR_NULL);

    MR_TEST_EXPECT_EQ_PTR(mr_driver_get(drv), drv);

    ret = mr_driver_put(drv);
    MR_TEST_EXPECT_EQ_INT(ret, MR_FALSE);

    ret = mr_driver_put(drv);
    MR_TEST_EXPECT_EQ_INT(ret, MR_TRUE);
}
MR_TEST_EXPORT(driver, get_put, test_driver_get_put);
#endif /* defined(MR_USE_DRIVER) */
#endif /* defined(MR_USE_TEST) */

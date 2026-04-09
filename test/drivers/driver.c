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

static int test_driver_init_count = 0;
static int test_driver_deinit_count = 0;

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

static const mr_driver_clazz_t test_driver_clazz
    = MR_DRIVER_CLAZZ_INIT("test_driver", test_driver_init, test_driver_deinit);

/* -------------------------------------------------------------------------- */
/* Test: Delete driver without setup                                           */
static void test_driver_del_without_setup(void) {
    mr_driver_t *drv;
    mr_err_t ret;

    test_driver_deinit_count = 0;
    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM,
                           MR_NULL, MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);

    ret = mr_driver_del(drv);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(test_driver_deinit_count, 0);
}
MR_TEST_EXPORT(driver, del_without_setup, test_driver_del_without_setup);

/* -------------------------------------------------------------------------- */
/* Test: Delete driver after setup                                             */
static void test_driver_del_after_setup(void) {
    mr_driver_t *drv;
    mr_err_t ret;

    test_driver_init_count = 0;
    test_driver_deinit_count = 0;
    drv = mr_driver_create(&test_driver_clazz, MR_DRIVER_TYPE_RAM,
                           MR_NULL, MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(drv, MR_NULL);

    ret = mr_driver_setup(drv);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(test_driver_init_count, 1);

    ret = mr_driver_del(drv);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_ASSERT_EQ_INT(test_driver_deinit_count, 1);
}
MR_TEST_EXPORT(driver, del_after_setup, test_driver_del_after_setup);
#endif /* defined(MR_USE_DRIVER) */
#endif /* defined(MR_USE_TEST) */

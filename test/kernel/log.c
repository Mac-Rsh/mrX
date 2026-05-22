/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-04-29        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_LOG)
#include <mrX/kernel/mr_log.h>

/* Tests */
static void test_log_mask_default(void) {
    MR_TEST_ASSERT_EQ_UINT(mr_log_mask(), 0xffffffffU);
}
MR_TEST_EXPORT(log, mask_default, test_log_mask_default);

static void test_log_mask_set_get(void) {
    mr_log_mask_set(0U);
    MR_TEST_EXPECT_EQ_UINT(mr_log_mask(), 0U);

    mr_log_mask_set(0x00000001U);
    MR_TEST_EXPECT_EQ_UINT(mr_log_mask(), 0x00000001U);

    mr_log_mask_set(0x0000001fU);
    MR_TEST_EXPECT_EQ_UINT(mr_log_mask(), 0x0000001fU);

    mr_log_mask_set(0xffffffffU);
    MR_TEST_ASSERT_EQ_UINT(mr_log_mask(), 0xffffffffU);
}
MR_TEST_EXPORT(log, mask_set_get, test_log_mask_set_get);

static void test_log_invalid_level(void) {
    mr_uint32_t saved = mr_log_mask();
    mr_log_mask_set(0xffffffffU);

    mr_log(32U, "should_not_print");
    mr_log(33U, "should_not_print");
    mr_log(100U, "should_not_print");

    mr_log_mask_set(saved);
    MR_TEST_ASSERT_EQ_UINT(mr_log_mask(), saved);
}
MR_TEST_EXPORT(log, invalid_level, test_log_invalid_level);

static void test_log_null_fmt(void) {
    mr_uint32_t saved = mr_log_mask();
    mr_log_mask_set(0xffffffffU);

    mr_log(0U, MR_NULL);

    mr_log_mask_set(saved);
    MR_TEST_ASSERT_EQ_UINT(mr_log_mask(), saved);
}
MR_TEST_EXPORT(log, null_fmt, test_log_null_fmt);

static void test_log_empty_fmt(void) {
    mr_uint32_t saved = mr_log_mask();
    mr_log_mask_set(0xffffffffU);

    mr_log(0U, "");

    mr_log_mask_set(saved);
    MR_TEST_ASSERT_EQ_UINT(mr_log_mask(), saved);
}
MR_TEST_EXPORT(log, empty_fmt, test_log_empty_fmt);

static void test_log_mask_filter(void) {
    mr_uint32_t saved = mr_log_mask();

    mr_log_mask_set(0U);
    mr_log(MR_LOG_LEVEL_ERR, "level_disabled %d", 1);
    MR_TEST_EXPECT_EQ_UINT(mr_log_mask(), 0U);

    mr_log_mask_set(MR_LOG_MASK(MR_LOG_LEVEL_ERR));
    mr_log(MR_LOG_LEVEL_ERR, "level_enabled %d\n", 1);
    MR_TEST_EXPECT_EQ_UINT(mr_log_mask(), MR_LOG_MASK(MR_LOG_LEVEL_ERR));

    mr_log_mask_set(0xffffffffU);
    mr_log(MR_LOG_LEVEL_WRN, "all_enabled %s\n", "test");
    MR_TEST_EXPECT_EQ_UINT(mr_log_mask(), 0xffffffffU);

    mr_log_mask_set(MR_LOG_MASK(MR_LOG_LEVEL_INF) | MR_LOG_MASK(MR_LOG_LEVEL_DBG));
    mr_log(MR_LOG_LEVEL_TRC, "trc_disabled");
    mr_log(MR_LOG_LEVEL_INF, "inf_enabled %x\n", 0x1234);
    mr_log(MR_LOG_LEVEL_DBG, "dbg_enabled %u\n", 42U);
    MR_TEST_EXPECT_EQ_UINT(mr_log_mask(),
                           MR_LOG_MASK(MR_LOG_LEVEL_INF) | MR_LOG_MASK(MR_LOG_LEVEL_DBG));

    mr_log_mask_set(saved);
    MR_TEST_ASSERT_EQ_UINT(mr_log_mask(), saved);
}
MR_TEST_EXPORT(log, mask_filter, test_log_mask_filter);

#endif /* defined(MR_USE_LOG) */
#endif /* defined(MR_USE_TEST) */

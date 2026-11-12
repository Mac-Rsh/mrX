/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#if defined(MR_USE_LOG)
/* Define the tag before including the log header so that the level macros
 * expand into real mr_log calls in this translation unit
 */
#ifndef MR_LOG_TAG
#define MR_LOG_TAG "log_deep"
#endif /* !defined(MR_LOG_TAG) */
#include <mrX/kernel/mr_log.h>

MR_TEST(log, mask_bits_roundtrip) {
    mr_uint32_t saved = mr_log_mask();

    mr_log_mask_set(0x00000001U);
    MR_TEST_EXPECT_EQ_UINT(mr_log_mask(), 0x00000001U);

    mr_log_mask_set(0x00000010U);
    MR_TEST_EXPECT_EQ_UINT(mr_log_mask(), 0x00000010U);

    mr_log_mask_set(0x00008000U);
    MR_TEST_EXPECT_EQ_UINT(mr_log_mask(), 0x00008000U);

    mr_log_mask_set(0x40000000U);
    MR_TEST_EXPECT_EQ_UINT(mr_log_mask(), 0x40000000U);

    mr_log_mask_set(0x80000000U);
    MR_TEST_EXPECT_EQ_UINT(mr_log_mask(), 0x80000000U);

    mr_log_mask_set(saved);
    MR_TEST_ASSERT_EQ_UINT(mr_log_mask(), saved);
}

MR_TEST(log, mask_macro_values) {
    MR_TEST_ASSERT_EQ_UINT(MR_LOG_MASK(MR_LOG_LEVEL_ERR), 0x00000001U);
    MR_TEST_ASSERT_EQ_UINT(MR_LOG_MASK(MR_LOG_LEVEL_WRN), 0x00000002U);
    MR_TEST_ASSERT_EQ_UINT(MR_LOG_MASK(MR_LOG_LEVEL_INF), 0x00000004U);
    MR_TEST_ASSERT_EQ_UINT(MR_LOG_MASK(MR_LOG_LEVEL_DBG), 0x00000008U);
    MR_TEST_ASSERT_EQ_UINT(MR_LOG_MASK(MR_LOG_LEVEL_TRC), 0x00000010U);
    MR_TEST_ASSERT_EQ_UINT(MR_LOG_MASK(31), 0x80000000U);
}

MR_TEST(log, level_upper_boundary) {
    mr_uint32_t saved = mr_log_mask();

    mr_log_mask_set(0xffffffffU);

    /* Level 31 is the last valid level and must pass the range check */
    mr_log(31U, "level_31_valid");
    /* Levels at and above 32 must be silently rejected */
    mr_log(32U, "level_32_invalid");
    mr_log(63U, "level_63_invalid");
    mr_log(0xffffffffU, "level_max_invalid");

    MR_TEST_EXPECT_EQ_UINT(mr_log_mask(), 0xffffffffU);
    mr_log_mask_set(saved);
    MR_TEST_ASSERT_EQ_UINT(mr_log_mask(), saved);
}

MR_TEST(log, null_fmt_all_levels) {
    mr_uint32_t saved = mr_log_mask();

    mr_log_mask_set(0xffffffffU);
    mr_log(MR_LOG_LEVEL_ERR, MR_NULL);
    mr_log(MR_LOG_LEVEL_WRN, MR_NULL);
    mr_log(MR_LOG_LEVEL_INF, MR_NULL);
    mr_log(MR_LOG_LEVEL_DBG, MR_NULL);
    mr_log(MR_LOG_LEVEL_TRC, MR_NULL);
    mr_log(31U, MR_NULL);

    MR_TEST_EXPECT_EQ_UINT(mr_log_mask(), 0xffffffffU);
    mr_log_mask_set(saved);
    MR_TEST_ASSERT_EQ_UINT(mr_log_mask(), saved);
}

MR_TEST(log, empty_fmt_all_levels) {
    mr_uint32_t saved = mr_log_mask();

    mr_log_mask_set(0xffffffffU);
    mr_log(MR_LOG_LEVEL_ERR, "");
    mr_log(MR_LOG_LEVEL_WRN, "");
    mr_log(MR_LOG_LEVEL_INF, "");
    mr_log(MR_LOG_LEVEL_DBG, "");
    mr_log(MR_LOG_LEVEL_TRC, "");
    mr_log(31U, "");

    MR_TEST_EXPECT_EQ_UINT(mr_log_mask(), 0xffffffffU);
    mr_log_mask_set(saved);
    MR_TEST_ASSERT_EQ_UINT(mr_log_mask(), saved);
}

MR_TEST(log, macros_no_args) {
    mr_uint32_t saved = mr_log_mask();

    mr_log_mask_set(0xffffffffU);
    /* Empty variable argument list must compile with the GNU comma swap */
    MR_LOG_ERR("no_args_err");
    MR_LOG_WRN("no_args_wrn");
    MR_LOG_INF("no_args_inf");
    MR_LOG_DBG("no_args_dbg");
    MR_LOG_TRC("no_args_trc");

    MR_TEST_EXPECT_EQ_UINT(mr_log_mask(), 0xffffffffU);
    mr_log_mask_set(saved);
    MR_TEST_ASSERT_EQ_UINT(mr_log_mask(), saved);
}

MR_TEST(log, macros_with_args) {
    mr_uint32_t saved = mr_log_mask();

    mr_log_mask_set(0xffffffffU);
    MR_LOG_ERR("err %d %s", -1, "e");
    MR_LOG_WRN("wrn %u %c", 1U, 'w');
    MR_LOG_INF("inf %x %p", 0xbeef, (void *)&saved);
    MR_LOG_DBG("dbg %lld %llu", 9223372036854775807LL, 18446744073709551615ULL);
    MR_LOG_TRC("trc %08d %s", -42, "pad");

    MR_TEST_EXPECT_EQ_UINT(mr_log_mask(), 0xffffffffU);
    mr_log_mask_set(saved);
    MR_TEST_ASSERT_EQ_UINT(mr_log_mask(), saved);
}

MR_TEST(log, macros_long_message) {
    mr_uint32_t saved = mr_log_mask();

    mr_log_mask_set(0xffffffffU);
    /* Messages far beyond the 64 byte printf buffer must be truncated
     * internally without overflowing or crashing
     */
    MR_LOG_INF("this_is_a_long_log_message_designed_to_exceed_the_sixty_four_"
               "byte_internal_printf_buffer_limit_for_truncation");
    MR_LOG_DBG("long_with_args %d %s %d %s %d %s %d %s %d %s", 1, "aa", 2, "bb",
               3, "cc", 4, "dd", 5, "ee");

    MR_TEST_EXPECT_EQ_UINT(mr_log_mask(), 0xffffffffU);
    mr_log_mask_set(saved);
    MR_TEST_ASSERT_EQ_UINT(mr_log_mask(), saved);
}

MR_TEST(log, macros_filter_err_only) {
    mr_uint32_t saved = mr_log_mask();

    mr_log_mask_set(MR_LOG_MASK(MR_LOG_LEVEL_ERR));
    /* Disabled levels must not emit anything, enabled ones must not
     * disturb the mask
     */
    MR_LOG_TRC("filtered_out");
    MR_LOG_DBG("filtered_out");
    MR_LOG_INF("filtered_out");
    MR_LOG_WRN("filtered_out");
    MR_LOG_ERR("filtered_in %d", 1);

    MR_TEST_EXPECT_EQ_UINT(mr_log_mask(), MR_LOG_MASK(MR_LOG_LEVEL_ERR));
    mr_log_mask_set(saved);
    MR_TEST_ASSERT_EQ_UINT(mr_log_mask(), saved);
}

MR_TEST(log, macros_filter_trc_only) {
    mr_uint32_t saved = mr_log_mask();

    mr_log_mask_set(MR_LOG_MASK(MR_LOG_LEVEL_TRC));
    MR_LOG_ERR("filtered_out");
    MR_LOG_WRN("filtered_out");
    MR_LOG_INF("filtered_out");
    MR_LOG_DBG("filtered_out");
    MR_LOG_TRC("filtered_in %s", "trc");

    MR_TEST_EXPECT_EQ_UINT(mr_log_mask(), MR_LOG_MASK(MR_LOG_LEVEL_TRC));
    mr_log_mask_set(saved);
    MR_TEST_ASSERT_EQ_UINT(mr_log_mask(), saved);
}

MR_TEST(log, direct_filter_boundary) {
    mr_uint32_t saved = mr_log_mask();

    mr_log_mask_set(MR_LOG_MASK(MR_LOG_LEVEL_TRC)
                    | MR_LOG_MASK(MR_LOG_LEVEL_DBG));
    mr_log(MR_LOG_LEVEL_ERR, "blocked_err");
    mr_log(MR_LOG_LEVEL_WRN, "blocked_wrn");
    mr_log(MR_LOG_LEVEL_INF, "blocked_inf");
    mr_log(MR_LOG_LEVEL_DBG, "allowed_dbg %d", 1);
    mr_log(MR_LOG_LEVEL_TRC, "allowed_trc %s", "t");

    MR_TEST_EXPECT_EQ_UINT(mr_log_mask(), MR_LOG_MASK(MR_LOG_LEVEL_TRC)
                                              | MR_LOG_MASK(MR_LOG_LEVEL_DBG));
    mr_log_mask_set(saved);
    MR_TEST_ASSERT_EQ_UINT(mr_log_mask(), saved);
}

MR_TEST(log, interleave_with_printf) {
    mr_uint32_t saved = mr_log_mask();

    mr_log_mask_set(0xffffffffU);
    /* Logging between printfs must leave the printf path intact, exact
     * return values prove the static format buffer is not corrupted
     */
    MR_TEST_ASSERT_EQ_INT(mr_printf("before %d|", 1), 9);
    MR_LOG_INF("between %d", 2);
    MR_TEST_ASSERT_EQ_INT(mr_printf("after %d|", 3), 8);

    mr_log_mask_set(saved);
    MR_TEST_ASSERT_EQ_UINT(mr_log_mask(), saved);
}

MR_TEST(log, many_args_and_percent) {
    mr_uint32_t saved = mr_log_mask();

    mr_log_mask_set(0xffffffffU);
    MR_LOG_DBG("many %d %d %d %d %d %d %d %d", 1, 2, 3, 4, 5, 6, 7, 8);
    MR_LOG_WRN("percent %% %x %s", 0xff, "tail");
    MR_LOG_ERR("no_newline_at_all");

    MR_TEST_EXPECT_EQ_UINT(mr_log_mask(), 0xffffffffU);
    mr_log_mask_set(saved);
    MR_TEST_ASSERT_EQ_UINT(mr_log_mask(), saved);
}

#endif /* defined(MR_USE_LOG) */
#endif /* defined(MR_USE_TEST) */

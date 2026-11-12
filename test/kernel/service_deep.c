/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-08-30        MacRsh        Deep test version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_service.h>

/* Helpers */
typedef struct {
    int first;
    int middle;
    int last;
} deep_svc_container_t;

MR_TEST(service, deep_container_of_first) {
    deep_svc_container_t obj;
    deep_svc_container_t *out;

    obj.first = 1;
    obj.middle = 2;
    obj.last = 3;
    out = MR_CONTAINER_OF(&obj.first, deep_svc_container_t, first);
    MR_TEST_ASSERT_EQ_PTR(out, &obj);
    MR_TEST_ASSERT_EQ_INT(out->middle, 2);
    MR_TEST_ASSERT_EQ_INT(out->last, 3);
}

MR_TEST(service, deep_container_of_last) {
    deep_svc_container_t obj;
    deep_svc_container_t *out;

    obj.first = 7;
    obj.middle = 8;
    obj.last = 9;
    out = MR_CONTAINER_OF(&obj.last, deep_svc_container_t, last);
    MR_TEST_ASSERT_EQ_PTR(out, &obj);
    MR_TEST_ASSERT_EQ_INT(out->first, 7);
    MR_TEST_ASSERT_EQ_INT(out->middle, 8);
}

MR_TEST(service, deep_container_of_array) {
    deep_svc_container_t objs[3];
    deep_svc_container_t *out;
    int i;

    for (i = 0; i < 3; i++) {
        objs[i].first = i;
        objs[i].last = 100 + i;
    }
    for (i = 0; i < 3; i++) {
        out = MR_CONTAINER_OF(&objs[i].middle, deep_svc_container_t, middle);
        MR_TEST_ASSERT_EQ_PTR(out, &objs[i]);
        MR_TEST_ASSERT_EQ_INT(out->first, i);
        MR_TEST_ASSERT_EQ_INT(out->last, 100 + i);
    }
}

MR_TEST(service, deep_min_max_unsigned) {
    MR_TEST_ASSERT_EQ_UINT(MR_MIN(0U, 1U), 0U);
    MR_TEST_ASSERT_EQ_UINT(MR_MAX(0U, 1U), 1U);
    MR_TEST_ASSERT_EQ_UINT(MR_MAX(0xFFFFFFFEU, 1U), 0xFFFFFFFEU);
    MR_TEST_ASSERT_EQ_UINT(MR_MIN(0xFFFFFFFEU, 1U), 1U);
    MR_TEST_ASSERT_EQ_UINT(MR_MAX(7U, 7U), 7U);
    MR_TEST_ASSERT_EQ_UINT(MR_MIN(7U, 7U), 7U);
    MR_TEST_ASSERT_EQ_INT(MR_MAX(-1, -5), -1);
    MR_TEST_ASSERT_EQ_INT(MR_MIN(-5, -1), -5);
}

MR_TEST(service, deep_align_edges) {
    MR_TEST_ASSERT_EQ_UINT(MR_ALIGN_UP(1U, 1U), 1U);
    MR_TEST_ASSERT_EQ_UINT(MR_ALIGN_DOWN(1U, 1U), 1U);
    MR_TEST_ASSERT_EQ_UINT(MR_ALIGN_DOWN(0U, 8U), 0U);
    MR_TEST_ASSERT_EQ_UINT(MR_ALIGN_DOWN(0x17U, 8U), 0x10U);
    MR_TEST_ASSERT_EQ_UINT(MR_ALIGN_DOWN(0xFFFFFFFFU, 16U), 0xFFFFFFF0U);
    MR_TEST_ASSERT_EQ_UINT(MR_ALIGN_UP(0x10000001U, 0x10000000U), 0x20000000U);
    MR_TEST_ASSERT_EQ_UINT(MR_ALIGN_UP(0x20000000U, 0x10000000U), 0x20000000U);
}

MR_TEST(service, deep_round_div_negative) {
    /* Results round half away from zero */
    MR_TEST_ASSERT_EQ_INT(MR_ROUND_DIV(-5, 2), -3);
    MR_TEST_ASSERT_EQ_INT(MR_ROUND_DIV(-1, 2), -1);
    MR_TEST_ASSERT_EQ_INT(MR_ROUND_DIV(1, -2), -1);
    MR_TEST_ASSERT_EQ_INT(MR_ROUND_DIV(-5, -2), 3);
    MR_TEST_ASSERT_EQ_INT(MR_ROUND_DIV(-4, 2), -2);
    MR_TEST_ASSERT_EQ_INT(MR_ROUND_DIV(4, -2), -2);
    MR_TEST_ASSERT_EQ_INT(MR_ROUND_DIV(0, -2), 0);
}

MR_TEST(service, deep_round_div_direction) {
    MR_TEST_ASSERT_EQ_INT(MR_ROUND_DIV(3, 2), 2);
    MR_TEST_ASSERT_EQ_INT(MR_ROUND_DIV(-3, 2), -2);
    MR_TEST_ASSERT_EQ_INT(MR_ROUND_DIV(7, 4), 2);
    MR_TEST_ASSERT_EQ_INT(MR_ROUND_DIV(-7, 4), -2);
}

MR_TEST(service, deep_va_args_num_edges) {
    MR_TEST_ASSERT_EQ_INT(MR_VA_ARGS_NUM(), 0);
    MR_TEST_ASSERT_EQ_INT(MR_VA_ARGS_NUM(a), 1);
    MR_TEST_ASSERT_EQ_INT(MR_VA_ARGS_NUM(a, b), 2);
    MR_TEST_ASSERT_EQ_INT(MR_VA_ARGS_NUM(a, b, c, d, e), 5);
    MR_TEST_ASSERT_EQ_INT(MR_VA_ARGS_NUM(1, 2, 3, 4, 5, 6, 7, 8, 9, 10), 10);
}

MR_TEST(service, deep_cat_and_sep) {
    MR_TEST_ASSERT_EQ_INT(MR_CAT(1, 2, 3), 123);
    MR_TEST_ASSERT_EQ_INT(MR_CAT(1, 2, 3, 4, 5), 12345);
    MR_TEST_ASSERT_EQ_INT(MR_CAT_SEP(1, 2, 3), 1213);
    MR_TEST_ASSERT_EQ_INT(MR_CAT_SEP(7, 8), 78);
}

MR_TEST(service, deep_macro_is_used) {
    MR_TEST_ASSERT_EQ_INT(MR_MACRO_IS_USED(MR_USE_TEST), 1);
#if defined(MR_USE_TIMER)
    MR_TEST_ASSERT_EQ_INT(MR_MACRO_IS_USED(MR_USE_TIMER), 1);
#endif /* defined(MR_USE_TIMER) */
    /* A macro defined to a value other than 1 must not count as used */
    MR_TEST_EXPECT_EQ_INT(MR_MACRO_IS_USED(MR_CFG_LOG_LEVEL), 0);
}

MR_TEST(service, deep_parity_edges) {
    mr_uint32_t val;
    mr_uint8_t byte;

    /* Zero size input has no bits so it is even */
    val = 0xFFFFFFFF;
    MR_TEST_ASSERT_EQ_INT(mr_parity_even(&val, 0), MR_TRUE);
    MR_TEST_ASSERT_EQ_INT(mr_parity_odd(&val, 0), MR_FALSE);

    val = 0x80000000;
    MR_TEST_ASSERT_EQ_INT(mr_parity_even(&val, sizeof(val)), MR_FALSE);
    MR_TEST_ASSERT_EQ_INT(mr_parity_odd(&val, sizeof(val)), MR_TRUE);

    val = 0xFFFFFFFF;
    MR_TEST_ASSERT_EQ_INT(mr_parity_even(&val, sizeof(val)), MR_TRUE);

    byte = 0x01;
    MR_TEST_ASSERT_EQ_INT(mr_parity_odd(&byte, sizeof(byte)), MR_TRUE);

    /* A multi byte buffer counts bits across byte boundaries */
    val = 0x0000FF00;
    MR_TEST_ASSERT_EQ_INT(mr_parity_even(&val, sizeof(val)), MR_TRUE);
    /* The first two bytes hold an even number of set bits on either byte
       order so the odd parity check must report MR_FALSE */
    MR_TEST_ASSERT_EQ_INT(mr_parity_odd(&val, 2), MR_FALSE);
}

MR_TEST(service, deep_make_local_shapes) {
    deep_svc_container_t *obj;
    mr_uint32_t *u;

    obj = MR_MAKE_LOCAL(deep_svc_container_t, 1, 2, 3);
    MR_TEST_ASSERT_EQ_INT(obj->first, 1);
    MR_TEST_ASSERT_EQ_INT(obj->middle, 2);
    MR_TEST_ASSERT_EQ_INT(obj->last, 3);

    u = MR_MAKE_LOCAL(mr_uint32_t, 0xDEADBEEF);
    MR_TEST_ASSERT_EQ_UINT(*u, 0xDEADBEEFU);
}

#endif /* defined(MR_USE_TEST) */

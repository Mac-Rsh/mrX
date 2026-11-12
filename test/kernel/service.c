/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_service.h>
#include <mrX/libc/mr_string.h>

typedef struct {
    int a;
    char b;
} test_container_t;

MR_TEST(service, container_of) {
    test_container_t *out;
    test_container_t obj;
    char *p;

    obj.a = 123;
    obj.b = 'x';
    p = &obj.b;
    out = MR_CONTAINER_OF(p, test_container_t, b);
    MR_TEST_ASSERT_EQ_PTR(out, &obj);
    MR_TEST_ASSERT_EQ_INT(out->a, 123);
    MR_TEST_ASSERT_EQ_INT(out->b, 'x');
}

MR_TEST(service, array_size) {
    int arr[10];

    MR_TEST_ASSERT_EQ_INT(MR_ARRAY_SIZE(arr), 10);
}

MR_TEST(service, make_local) {
    test_container_t *obj;
    int *p;

    obj = MR_MAKE_LOCAL(test_container_t, 123, 'x');
    MR_TEST_ASSERT_EQ_INT(obj->a, 123);
    MR_TEST_ASSERT_EQ_INT(obj->b, 'x');

    p = MR_MAKE_LOCAL(int, 99);
    MR_TEST_ASSERT_EQ_INT(*p, 99);
}

MR_TEST(service, min_max) {
    MR_TEST_ASSERT_EQ_INT(MR_MIN(3, 7), 3);
    MR_TEST_ASSERT_EQ_INT(MR_MAX(3, 7), 7);
    MR_TEST_ASSERT_EQ_INT(MR_MIN(3, 3), 3);
    MR_TEST_ASSERT_EQ_INT(MR_MAX(-1, -5), -1);
}

MR_TEST(service, align) {
    MR_TEST_ASSERT_EQ_INT(MR_ALIGN_UP(5, 8), 8);
    MR_TEST_ASSERT_EQ_INT(MR_ALIGN_UP(8, 8), 8);
    MR_TEST_ASSERT_EQ_INT(MR_ALIGN_UP(0, 8), 0);
    MR_TEST_ASSERT_EQ_INT(MR_ALIGN_UP(9, 8), 16);
    MR_TEST_ASSERT_EQ_INT(MR_ALIGN_DOWN(5, 8), 0);
    MR_TEST_ASSERT_EQ_INT(MR_ALIGN_DOWN(8, 8), 8);
    MR_TEST_ASSERT_EQ_INT(MR_ALIGN_DOWN(15, 8), 8);
}

MR_TEST(service, round_div) {
    MR_TEST_ASSERT_EQ_INT(MR_ROUND_DIV(5, 2), 3);
    MR_TEST_ASSERT_EQ_INT(MR_ROUND_DIV(4, 2), 2);
    MR_TEST_ASSERT_EQ_INT(MR_ROUND_DIV(1, 2), 1);
    MR_TEST_ASSERT_EQ_INT(MR_ROUND_DIV(0, 2), 0);
}

MR_TEST(service, str_cat_va) {
    MR_TEST_ASSERT_EQ_INT(mr_strcmp(MR_STR(hello), "hello"), 0);
    MR_TEST_ASSERT_EQ_INT(MR_CAT(12, 34), 1234);
    MR_TEST_ASSERT_EQ_INT(MR_VA_ARGS_NUM(a), 1);
    MR_TEST_ASSERT_EQ_INT(MR_VA_ARGS_NUM(a, b, c), 3);
}

MR_TEST(service, parity) {
    mr_uint16_t i, tmp;
    mr_uint8_t odd, j;

    for (i = 0; i < 65535; i++) {
        for (odd = 0, j = 0; j < 16; j++) {
            tmp = (i >> j) & 0x1U;
            odd ^= (mr_uint8_t)tmp;
        }

        MR_TEST_ASSERT_EQ_INT(mr_parity_even(&i, sizeof(i)), !odd);
        MR_TEST_ASSERT_EQ_INT(mr_parity_odd(&i, sizeof(i)), odd);
    }
}

#endif /* defined(MR_USE_TEST) */

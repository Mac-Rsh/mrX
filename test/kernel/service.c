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

typedef struct {
    int a;
    char b;
} test_container_t;

/* -------------------------------------------------------------------------- */
/* Test: MR_CONTAINER_OF                                                      */
static void test_service_container_of(void) {
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
MR_TEST_EXPORT(service, container_of, test_service_container_of);

/* -------------------------------------------------------------------------- */
/* Test: MR_ARRAY_SIZE                                                        */
static void test_service_array_size(void) {
    int arr[10];

    MR_TEST_ASSERT_EQ_INT(MR_ARRAY_SIZE(arr), 10);
}
MR_TEST_EXPORT(service, array_size, test_service_array_size);

/* -------------------------------------------------------------------------- */
/* Test: MR_MAKE_LOCAL                                                        */
static void test_service_make_local(void) {
    test_container_t *obj;
    int *p;

    obj = MR_MAKE_LOCAL(test_container_t, 123, 'x');
    MR_TEST_ASSERT_EQ_INT(obj->a, 123);
    MR_TEST_ASSERT_EQ_INT(obj->b, 'x');

    p = MR_MAKE_LOCAL(int, 99);
    MR_TEST_ASSERT_EQ_INT(*p, 99);
}
MR_TEST_EXPORT(service, make_local, test_service_make_local);

/* -------------------------------------------------------------------------- */
/* Test: MR_MIN / MR_MAX                                                      */
static void test_service_min_max(void) {
    MR_TEST_ASSERT_EQ_INT(MR_MIN(3, 7), 3);
    MR_TEST_ASSERT_EQ_INT(MR_MAX(3, 7), 7);
}
MR_TEST_EXPORT(service, min_max, test_service_min_max);

/* -------------------------------------------------------------------------- */
/* Test: MR_ALIGN_UP / MR_ALIGN_DOWN                                          */
static void test_service_align(void) {
    MR_TEST_ASSERT_EQ_INT(MR_ALIGN_UP(5, 8), 8);
    MR_TEST_ASSERT_EQ_INT(MR_ALIGN_UP(8, 8), 8);
    MR_TEST_ASSERT_EQ_INT(MR_ALIGN_DOWN(5, 8), 0);
    MR_TEST_ASSERT_EQ_INT(MR_ALIGN_DOWN(8, 8), 8);
}
MR_TEST_EXPORT(service, align, test_service_align);

/* -------------------------------------------------------------------------- */
/* Test: parity_even / parity_odd                                          */
static void test_service_parity(void) {
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
MR_TEST_EXPORT(service, parity, test_service_parity);
#endif /* defined(MR_USE_TEST) */

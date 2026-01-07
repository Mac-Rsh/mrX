/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_slab.h>

#define TEST_SLAB_OBJ_SIZE 64
#define TEST_SLAB_OBJ_NUM  16
static mr_uint8_t test_slab_buf[TEST_SLAB_OBJ_SIZE * TEST_SLAB_OBJ_NUM];
static mr_slab_t test_slab
    = MR_SLAB_INIT(test_slab_buf, TEST_SLAB_OBJ_SIZE, TEST_SLAB_OBJ_NUM);

/* -------------------------------------------------------------------------- */
/* Utility: fill/verify canary pattern                                        */
static void canary_fill(void *p, mr_size_t usable) {
    mr_uint8_t *data;
    mr_size_t i;

    for (data = (mr_uint8_t *)p, i = 0; i < usable; ++i) {
        data[i] = (mr_uint8_t)(i & 0xFFU);
    }
}

static int canary_verify(void *p, mr_size_t usable) {
    mr_uint8_t *data;
    mr_size_t i;

    for (data = (mr_uint8_t *)p, i = 0; i < usable; ++i) {
        if (data[i] != (mr_uint8_t)(i & 0xFFU)) {
            return 0;
        }
    }
    return 1;
}

/* -------------------------------------------------------------------------- */
/* Test: allocate, write pattern, verify, free                                */
static void test_slab_basic(void) {
    void *p;

    p = mr_slab_alloc(&test_slab);
    MR_TEST_ASSERT_NE_PTR(p, MR_NULL);

    canary_fill(p, TEST_SLAB_OBJ_SIZE);
    MR_TEST_ASSERT_EQ_INT(canary_verify(p, TEST_SLAB_OBJ_SIZE), 1);

    mr_slab_free(&test_slab, p);
}
MR_TEST_EXPORT(slab, basic, test_slab_basic);

/* -------------------------------------------------------------------------- */
/* Test: multiple allocations                                                 */
static void test_slab_multi_alloc(void) {
    void *arr[TEST_SLAB_OBJ_NUM];
    int i;

    for (i = 0; i < TEST_SLAB_OBJ_NUM; i++) {
        arr[i] = mr_slab_alloc(&test_slab);
        MR_TEST_ASSERT_NE_PTR(arr[i], MR_NULL);
        canary_fill(arr[i], TEST_SLAB_OBJ_SIZE);
    }
    MR_TEST_ASSERT_EQ_PTR(mr_slab_alloc(&test_slab), MR_NULL);
    for (i = 0; i < TEST_SLAB_OBJ_NUM; i++) {
        MR_TEST_ASSERT_EQ_INT(canary_verify(arr[i], TEST_SLAB_OBJ_SIZE), 1);
        mr_slab_free(&test_slab, arr[i]);
    }

    for (i = 0; i < TEST_SLAB_OBJ_NUM; i++) {
        arr[i] = mr_slab_alloc(&test_slab);
        MR_TEST_ASSERT_NE_PTR(arr[i], MR_NULL);
    }
    for (i = 0; i < TEST_SLAB_OBJ_NUM; i++) {
        mr_slab_free(&test_slab, arr[i]);
    }
}
MR_TEST_EXPORT(slab, multi_alloc, test_slab_multi_alloc);

/* -------------------------------------------------------------------------- */
/* Test: interleave allocate and free                                         */
static void test_slab_interleave(void) {
    void *arr[TEST_SLAB_OBJ_NUM];
    int i;

    for (i = 0; i < TEST_SLAB_OBJ_NUM; i++) {
        arr[i] = mr_slab_alloc(&test_slab);
        MR_TEST_ASSERT_NE_PTR(arr[i], MR_NULL);
        canary_fill(arr[i], TEST_SLAB_OBJ_SIZE);
    }
    for (i = 0; i < TEST_SLAB_OBJ_NUM; i += 2) {
        mr_slab_free(&test_slab, arr[i]);
    }

    for (i = 0; i < TEST_SLAB_OBJ_NUM; i += 2) {
        arr[i] = mr_slab_alloc(&test_slab);
        MR_TEST_ASSERT_NE_PTR(arr[i], MR_NULL);
        canary_fill(arr[i], TEST_SLAB_OBJ_SIZE);
    }
    for (i = 0; i < TEST_SLAB_OBJ_NUM; i++) {
        mr_slab_free(&test_slab, arr[i]);
    }
}
MR_TEST_EXPORT(slab, interleave, test_slab_interleave);

/* -------------------------------------------------------------------------- */
/* Test: free invalid memory                                                  */
static void test_slab_invalid_free(void) {
    mr_uint8_t invalid_buf[TEST_SLAB_OBJ_SIZE];
    void *p = mr_slab_alloc(&test_slab);

    MR_TEST_ASSERT_NE_PTR(p, MR_NULL);
    mr_slab_free(&test_slab, MR_NULL);
    mr_slab_free(&test_slab, invalid_buf);
    mr_slab_free(&test_slab, (void *)0xDEADBEEF);
    mr_slab_free(&test_slab, p);
}
MR_TEST_EXPORT(slab, invalid_free, test_slab_invalid_free);
#endif /* defined(MR_USE_TEST) */

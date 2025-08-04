/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_TEST_H__
#define __MR_TEST_H__

#include <test/mr_check.h>
#if defined(MR_USE_TEST)
#include <libc/mr_compiler.h>
#endif /* defined(MR_USE_TEST) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Test
 * @{
 */

#if defined(MR_USE_TEST)
/* Test expect and assert equality macros definition */
#define MR_TEST_EXPECT_EQ_INT(_a, _b)                                          \
    __MR_TEST_EXPECT(__MR_TEST_EQ(_a, _b, mr_int64_t, "%lld"))
#define MR_TEST_EXPECT_EQ_UINT(_a, _b)                                         \
    __MR_TEST_EXPECT(__MR_TEST_EQ(_a, _b, mr_uint64_t, "%llu"))
#define MR_TEST_EXPECT_EQ_PTR(_a, _b)                                          \
    __MR_TEST_EXPECT(__MR_TEST_EQ(_a, _b, mr_ptr_t, "%p"))
#define MR_TEST_EXPECT_EQ_FLOAT(_a, _b)                                        \
    __MR_TEST_EXPECT(__MR_TEST_EQ(_a, _b, float, "%f"))
#define MR_TEST_EXPECT_EQ_DOUBLE(_a, _b)                                       \
    __MR_TEST_EXPECT(__MR_TEST_EQ(_a, _b, double, "%f"))
#define MR_TEST_ASSERT_EQ_INT(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_EQ(_a, _b, mr_int64_t, "%lld"))
#define MR_TEST_ASSERT_EQ_UINT(_a, _b)                                         \
    __MR_TEST_ASSERT(__MR_TEST_EQ(_a, _b, mr_uint64_t, "%llu"))
#define MR_TEST_ASSERT_EQ_PTR(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_EQ(_a, _b, mr_ptr_t, "%p"))
#define MR_TEST_ASSERT_EQ_FLOAT(_a, _b)                                        \
    __MR_TEST_ASSERT(__MR_TEST_EQ(_a, _b, float, "%f"))
#define MR_TEST_ASSERT_EQ_DOUBLE(_a, _b)                                       \
    __MR_TEST_ASSERT(__MR_TEST_EQ(_a, _b, double, "%f"))

/* Test expect and assert inequality macros definition */
#define MR_TEST_EXPECT_NE_INT(_a, _b)                                          \
    __MR_TEST_EXPECT(__MR_TEST_NE(_a, _b, mr_int64_t, "%lld"))
#define MR_TEST_EXPECT_NE_UINT(_a, _b)                                         \
    __MR_TEST_EXPECT(__MR_TEST_NE(_a, _b, mr_uint64_t, "%llu"))
#define MR_TEST_EXPECT_NE_PTR(_a, _b)                                          \
    __MR_TEST_EXPECT(__MR_TEST_NE(_a, _b, mr_ptr_t, "%p"))
#define MR_TEST_EXPECT_NE_FLOAT(_a, _b)                                        \
    __MR_TEST_EXPECT(__MR_TEST_NE(_a, _b, float, "%f"))
#define MR_TEST_EXPECT_NE_DOUBLE(_a, _b)                                       \
    __MR_TEST_EXPECT(__MR_TEST_NE(_a, _b, double, "%f"))
#define MR_TEST_ASSERT_NE_INT(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_NE(_a, _b, mr_int64_t, "%lld"))
#define MR_TEST_ASSERT_NE_UINT(_a, _b)                                         \
    __MR_TEST_ASSERT(__MR_TEST_NE(_a, _b, mr_uint64_t, "%llu"))
#define MR_TEST_ASSERT_NE_PTR(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_NE(_a, _b, mr_ptr_t, "%p"))
#define MR_TEST_ASSERT_NE_FLOAT(_a, _b)                                        \
    __MR_TEST_ASSERT(__MR_TEST_NE(_a, _b, float, "%f"))
#define MR_TEST_ASSERT_NE_DOUBLE(_a, _b)                                       \
    __MR_TEST_ASSERT(__MR_TEST_NE(_a, _b, double, "%f"))

/* Test expect and assert less than macros definition */
#define MR_TEST_EXPECT_LT_INT(_a, _b)                                          \
    __MR_TEST_EXPECT(__MR_TEST_LT(_a, _b, mr_int64_t, "%lld"))
#define MR_TEST_EXPECT_LT_UINT(_a, _b)                                         \
    __MR_TEST_EXPECT(__MR_TEST_LT(_a, _b, mr_uint64_t, "%llu"))
#define MR_TEST_EXPECT_LT_PTR(_a, _b)                                          \
    __MR_TEST_EXPECT(__MR_TEST_LT(_a, _b, mr_ptr_t, "%p"))
#define MR_TEST_EXPECT_LT_FLOAT(_a, _b)                                        \
    __MR_TEST_EXPECT(__MR_TEST_LT(_a, _b, float, "%f"))
#define MR_TEST_EXPECT_LT_DOUBLE(_a, _b)                                       \
    __MR_TEST_EXPECT(__MR_TEST_LT(_a, _b, double, "%f"))
#define MR_TEST_ASSERT_LT_INT(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_LT(_a, _b, mr_int64_t, "%lld"))
#define MR_TEST_ASSERT_LT_UINT(_a, _b)                                         \
    __MR_TEST_ASSERT(__MR_TEST_LT(_a, _b, mr_uint64_t, "%llu"))
#define MR_TEST_ASSERT_LT_PTR(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_LT(_a, _b, mr_ptr_t, "%p"))
#define MR_TEST_ASSERT_LT_FLOAT(_a, _b)                                        \
    __MR_TEST_ASSERT(__MR_TEST_LT(_a, _b, float, "%f"))
#define MR_TEST_ASSERT_LT_DOUBLE(_a, _b)                                       \
    __MR_TEST_ASSERT(__MR_TEST_LT(_a, _b, double, "%f"))

/* Test expect and assert less than or equal macros definition */
#define MR_TEST_EXPECT_LE_INT(_a, _b)                                          \
    __MR_TEST_EXPECT(__MR_TEST_LE(_a, _b, mr_int64_t, "%lld"))
#define MR_TEST_EXPECT_LE_UINT(_a, _b)                                         \
    __MR_TEST_EXPECT(__MR_TEST_LE(_a, _b, mr_uint64_t, "%llu"))
#define MR_TEST_EXPECT_LE_PTR(_a, _b)                                          \
    __MR_TEST_EXPECT(__MR_TEST_LE(_a, _b, mr_ptr_t, "%p"))
#define MR_TEST_EXPECT_LE_FLOAT(_a, _b)                                        \
    __MR_TEST_EXPECT(__MR_TEST_LE(_a, _b, float, "%f"))
#define MR_TEST_EXPECT_LE_DOUBLE(_a, _b)                                       \
    __MR_TEST_EXPECT(__MR_TEST_LE(_a, _b, double, "%f"))
#define MR_TEST_ASSERT_LE_INT(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_LE(_a, _b, mr_int64_t, "%lld"))
#define MR_TEST_ASSERT_LE_UINT(_a, _b)                                         \
    __MR_TEST_ASSERT(__MR_TEST_LE(_a, _b, mr_uint64_t, "%llu"))
#define MR_TEST_ASSERT_LE_PTR(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_LE(_a, _b, mr_ptr_t, "%p"))
#define MR_TEST_ASSERT_LE_FLOAT(_a, _b)                                        \
    __MR_TEST_ASSERT(__MR_TEST_LE(_a, _b, float, "%f"))
#define MR_TEST_ASSERT_LE_DOUBLE(_a, _b)                                       \
    __MR_TEST_ASSERT(__MR_TEST_LE(_a, _b, double, "%f"))

/* Test expect and assert greater than macros definition */
#define MR_TEST_EXPECT_GT_INT(_a, _b)                                          \
    __MR_TEST_EXPECT(__MR_TEST_GT(_a, _b, mr_int64_t, "%lld"))
#define MR_TEST_EXPECT_GT_UINT(_a, _b)                                         \
    __MR_TEST_EXPECT(__MR_TEST_GT(_a, _b, mr_uint64_t, "%llu"))
#define MR_TEST_EXPECT_GT_PTR(_a, _b)                                          \
    __MR_TEST_EXPECT(__MR_TEST_GT(_a, _b, mr_ptr_t, "%p"))
#define MR_TEST_EXPECT_GT_FLOAT(_a, _b)                                        \
    __MR_TEST_EXPECT(__MR_TEST_GT(_a, _b, float, "%f"))
#define MR_TEST_EXPECT_GT_DOUBLE(_a, _b)                                       \
    __MR_TEST_EXPECT(__MR_TEST_GT(_a, _b, double, "%f"))
#define MR_TEST_ASSERT_GT_INT(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_GT(_a, _b, mr_int64_t, "%lld"))
#define MR_TEST_ASSERT_GT_UINT(_a, _b)                                         \
    __MR_TEST_ASSERT(__MR_TEST_GT(_a, _b, mr_uint64_t, "%llu"))
#define MR_TEST_ASSERT_GT_PTR(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_GT(_a, _b, mr_ptr_t, "%p"))
#define MR_TEST_ASSERT_GT_FLOAT(_a, _b)                                        \
    __MR_TEST_ASSERT(__MR_TEST_GT(_a, _b, float, "%f"))
#define MR_TEST_ASSERT_GT_DOUBLE(_a, _b)                                       \
    __MR_TEST_ASSERT(__MR_TEST_GT(_a, _b, double, "%f"))

/* Test expect and assert greater than or equal macros definition */
#define MR_TEST_EXPECT_GE_INT(_a, _b)                                          \
    __MR_TEST_EXPECT(__MR_TEST_GE(_a, _b, mr_int64_t, "%lld"))
#define MR_TEST_EXPECT_GE_UINT(_a, _b)                                         \
    __MR_TEST_EXPECT(__MR_TEST_GE(_a, _b, mr_uint64_t, "%llu"))
#define MR_TEST_EXPECT_GE_PTR(_a, _b)                                          \
    __MR_TEST_EXPECT(__MR_TEST_GE(_a, _b, mr_ptr_t, "%p"))
#define MR_TEST_EXPECT_GE_FLOAT(_a, _b)                                        \
    __MR_TEST_EXPECT(__MR_TEST_GE(_a, _b, float, "%f"))
#define MR_TEST_EXPECT_GE_DOUBLE(_a, _b)                                       \
    __MR_TEST_EXPECT(__MR_TEST_GE(_a, _b, double, "%f"))
#define MR_TEST_ASSERT_GE_INT(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_GE(_a, _b, mr_int64_t, "%lld"))
#define MR_TEST_ASSERT_GE_UINT(_a, _b)                                         \
    __MR_TEST_ASSERT(__MR_TEST_GE(_a, _b, mr_uint64_t, "%llu"))
#define MR_TEST_ASSERT_GE_PTR(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_GE(_a, _b, mr_ptr_t, "%p"))
#define MR_TEST_ASSERT_GE_FLOAT(_a, _b)                                        \
    __MR_TEST_ASSERT(__MR_TEST_GE(_a, _b, float, "%f"))
#define MR_TEST_ASSERT_GE_DOUBLE(_a, _b)                                       \
    __MR_TEST_ASSERT(__MR_TEST_GE(_a, _b, double, "%f"))

/* Testcase entry type */
typedef void(mr_testcase_entry)(void);

/* Testcase type */
typedef struct MR_ALIGN(MR_ALIGN_SIZE) mr_testcase {
    const char *suite;
    const char *name;
    mr_testcase_entry *entry;
} mr_testcase_t;

/* Testcase export macro definition */
#define __MR_TEST_EXPORT(_suite, _name, _entry, _level)                        \
    MR_USED const mr_testcase_t __mr_testcase_##_suite##_##_name MR_SECTION(   \
        "mr_testcase." _level)                                                 \
        = {.suite = #_suite, .name = #_name, .entry = (_entry)}

/**
 * @brief This macro function defines a testcase.
 *
 * @param _suite The testcase suite name.
 * @param _name The testcase name.
 * @param _entry The testcase function.
 */
#define MR_TEST_EXPORT(_suite, _name, _entry)                                  \
    __MR_TEST_EXPORT(_suite, _name, _entry, "1")

/**
 * @brief This function runs all testcases.
 */
int mr_test_all_run(void);
#endif /* defined(MR_USE_TEST) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_TEST_H__ */

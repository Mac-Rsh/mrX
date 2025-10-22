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
    __MR_TEST_EXPECT(__MR_TEST_COND_INT(_a, EQ, _b))
#define MR_TEST_EXPECT_EQ_UINT(_a, _b)                                         \
    __MR_TEST_EXPECT(__MR_TEST_COND_UINT(_a, EQ, _b))
#define MR_TEST_EXPECT_EQ_PTR(_a, _b)                                          \
    __MR_TEST_EXPECT(__MR_TEST_COND_PTR(_a, EQ, _b))
#define MR_TEST_EXPECT_EQ_FLOAT(_a, _b)                                        \
    __MR_TEST_EXPECT(__MR_TEST_COND_NEAR(_a, _b, 1e-4f))
#define MR_TEST_EXPECT_EQ_DOUBLE(_a, _b)                                       \
    __MR_TEST_EXPECT(__MR_TEST_COND_NEAR(_a, _b, 1e-8))
#define MR_TEST_ASSERT_EQ_INT(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_COND_INT(_a, EQ, _b))
#define MR_TEST_ASSERT_EQ_UINT(_a, _b)                                         \
    __MR_TEST_ASSERT(__MR_TEST_COND_UINT(_a, EQ, _b))
#define MR_TEST_ASSERT_EQ_PTR(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_COND_PTR(_a, EQ, _b))
#define MR_TEST_ASSERT_EQ_FLOAT(_a, _b)                                        \
    __MR_TEST_ASSERT(__MR_TEST_COND_NEAR(_a, _b, 1e-4f))
#define MR_TEST_ASSERT_EQ_DOUBLE(_a, _b)                                       \
    __MR_TEST_ASSERT(__MR_TEST_COND_NEAR(_a, _b, 1e-8))

/* Test expect and assert inequality macros definition */
#define MR_TEST_EXPECT_NE_INT(_a, _b)                                          \
    __MR_TEST_EXPECT(__MR_TEST_COND_INT(_a, NE, _b))
#define MR_TEST_EXPECT_NE_UINT(_a, _b)                                         \
    __MR_TEST_EXPECT(__MR_TEST_COND_UINT(_a, NE, _b))
#define MR_TEST_EXPECT_NE_PTR(_a, _b)                                          \
    __MR_TEST_EXPECT(__MR_TEST_COND_PTR(_a, NE, _b))
#define MR_TEST_EXPECT_NE_FLOAT(_a, _b)                                        \
    __MR_TEST_EXPECT(__MR_TEST_COND_FLOAT(_a, NE, _b))
#define MR_TEST_EXPECT_NE_DOUBLE(_a, _b)                                       \
    __MR_TEST_EXPECT(__MR_TEST_COND_DOUBLE(_a, NE, _b))
#define MR_TEST_ASSERT_NE_INT(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_COND_INT(_a, NE, _b))
#define MR_TEST_ASSERT_NE_UINT(_a, _b)                                         \
    __MR_TEST_ASSERT(__MR_TEST_COND_UINT(_a, NE, _b))
#define MR_TEST_ASSERT_NE_PTR(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_COND_PTR(_a, NE, _b))
#define MR_TEST_ASSERT_NE_FLOAT(_a, _b)                                        \
    __MR_TEST_ASSERT(__MR_TEST_COND_FLOAT(_a, NE, _b))
#define MR_TEST_ASSERT_NE_DOUBLE(_a, _b)                                       \
    __MR_TEST_ASSERT(__MR_TEST_COND_DOUBLE(_a, NE, _b))

/* Test expect and assert less than macros definition */
#define MR_TEST_EXPECT_LT_INT(_a, _b)                                          \
    __MR_TEST_EXPECT(__MR_TEST_COND_INT(_a, LT, _b))
#define MR_TEST_EXPECT_LT_UINT(_a, _b)                                         \
    __MR_TEST_EXPECT(__MR_TEST_COND_UINT(_a, LT, _b))
#define MR_TEST_EXPECT_LT_PTR(_a, _b)                                          \
    __MR_TEST_EXPECT(__MR_TEST_COND_PTR(_a, LT, _b))
#define MR_TEST_EXPECT_LT_FLOAT(_a, _b)                                        \
    __MR_TEST_EXPECT(__MR_TEST_COND_FLOAT(_a, LT, _b))
#define MR_TEST_EXPECT_LT_DOUBLE(_a, _b)                                       \
    __MR_TEST_EXPECT(__MR_TEST_COND_DOUBLE(_a, LT, _b))
#define MR_TEST_ASSERT_LT_INT(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_COND_INT(_a, LT, _b))
#define MR_TEST_ASSERT_LT_UINT(_a, _b)                                         \
    __MR_TEST_ASSERT(__MR_TEST_COND_UINT(_a, LT, _b))
#define MR_TEST_ASSERT_LT_PTR(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_COND_PTR(_a, LT, _b))
#define MR_TEST_ASSERT_LT_FLOAT(_a, _b)                                        \
    __MR_TEST_ASSERT(__MR_TEST_COND_FLOAT(_a, LT, _b))
#define MR_TEST_ASSERT_LT_DOUBLE(_a, _b)                                       \
    __MR_TEST_ASSERT(__MR_TEST_COND_DOUBLE(_a, LT, _b))

/* Test expect and assert less than or equal macros definition */
#define MR_TEST_EXPECT_LE_INT(_a, _b)                                          \
    __MR_TEST_EXPECT(__MR_TEST_COND_INT(_a, LE, _b))
#define MR_TEST_EXPECT_LE_UINT(_a, _b)                                         \
    __MR_TEST_EXPECT(__MR_TEST_COND_UINT(_a, LE, _b))
#define MR_TEST_EXPECT_LE_PTR(_a, _b)                                          \
    __MR_TEST_EXPECT(__MR_TEST_COND_PTR(_a, LE, _b))
#define MR_TEST_EXPECT_LE_FLOAT(_a, _b)                                        \
    __MR_TEST_EXPECT(__MR_TEST_COND_FLOAT(_a, LE, _b))
#define MR_TEST_EXPECT_LE_DOUBLE(_a, _b)                                       \
    __MR_TEST_EXPECT(__MR_TEST_COND_DOUBLE(_a, LE, _b))
#define MR_TEST_ASSERT_LE_INT(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_COND_INT(_a, LE, _b))
#define MR_TEST_ASSERT_LE_UINT(_a, _b)                                         \
    __MR_TEST_ASSERT(__MR_TEST_COND_UINT(_a, LE, _b))
#define MR_TEST_ASSERT_LE_PTR(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_COND_PTR(_a, LE, _b))
#define MR_TEST_ASSERT_LE_FLOAT(_a, _b)                                        \
    __MR_TEST_ASSERT(__MR_TEST_COND_FLOAT(_a, LE, _b))
#define MR_TEST_ASSERT_LE_DOUBLE(_a, _b)                                       \
    __MR_TEST_ASSERT(__MR_TEST_COND_DOUBLE(_a, LE, _b))

/* Test expect and assert greater than macros definition */
#define MR_TEST_EXPECT_GT_INT(_a, _b)                                          \
    __MR_TEST_EXPECT(__MR_TEST_COND_INT(_a, GT, _b))
#define MR_TEST_EXPECT_GT_UINT(_a, _b)                                         \
    __MR_TEST_EXPECT(__MR_TEST_COND_UINT(_a, GT, _b))
#define MR_TEST_EXPECT_GT_PTR(_a, _b)                                          \
    __MR_TEST_EXPECT(__MR_TEST_COND_PTR(_a, GT, _b))
#define MR_TEST_EXPECT_GT_FLOAT(_a, _b)                                        \
    __MR_TEST_EXPECT(__MR_TEST_COND_FLOAT(_a, GT, _b))
#define MR_TEST_EXPECT_GT_DOUBLE(_a, _b)                                       \
    __MR_TEST_EXPECT(__MR_TEST_COND_DOUBLE(_a, GT, _b))
#define MR_TEST_ASSERT_GT_INT(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_COND_INT(_a, GT, _b))
#define MR_TEST_ASSERT_GT_UINT(_a, _b)                                         \
    __MR_TEST_ASSERT(__MR_TEST_COND_UINT(_a, GT, _b))
#define MR_TEST_ASSERT_GT_PTR(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_COND_PTR(_a, GT, _b))
#define MR_TEST_ASSERT_GT_FLOAT(_a, _b)                                        \
    __MR_TEST_ASSERT(__MR_TEST_COND_FLOAT(_a, GT, _b))
#define MR_TEST_ASSERT_GT_DOUBLE(_a, _b)                                       \
    __MR_TEST_ASSERT(__MR_TEST_COND_DOUBLE(_a, GT, _b))

/* Test expect and assert greater than or equal macros definition */
#define MR_TEST_EXPECT_GE_INT(_a, _b)                                          \
    __MR_TEST_EXPECT(__MR_TEST_COND_INT(_a, GE, _b))
#define MR_TEST_EXPECT_GE_UINT(_a, _b)                                         \
    __MR_TEST_EXPECT(__MR_TEST_COND_UINT(_a, GE, _b))
#define MR_TEST_EXPECT_GE_PTR(_a, _b)                                          \
    __MR_TEST_EXPECT(__MR_TEST_COND_PTR(_a, GE, _b))
#define MR_TEST_EXPECT_GE_FLOAT(_a, _b)                                        \
    __MR_TEST_EXPECT(__MR_TEST_COND_FLOAT(_a, GE, _b))
#define MR_TEST_EXPECT_GE_DOUBLE(_a, _b)                                       \
    __MR_TEST_EXPECT(__MR_TEST_COND_DOUBLE(_a, GE, _b))
#define MR_TEST_ASSERT_GE_INT(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_COND_INT(_a, GE, _b))
#define MR_TEST_ASSERT_GE_UINT(_a, _b)                                         \
    __MR_TEST_ASSERT(__MR_TEST_COND_UINT(_a, GE, _b))
#define MR_TEST_ASSERT_GE_PTR(_a, _b)                                          \
    __MR_TEST_ASSERT(__MR_TEST_COND_PTR(_a, GE, _b))
#define MR_TEST_ASSERT_GE_FLOAT(_a, _b)                                        \
    __MR_TEST_ASSERT(__MR_TEST_COND_FLOAT(_a, GE, _b))
#define MR_TEST_ASSERT_GE_DOUBLE(_a, _b)                                       \
    __MR_TEST_ASSERT(__MR_TEST_COND_DOUBLE(_a, GE, _b))

/* Test expect and assert near macros definition */
#define MR_TEST_EXPECT_NEAR(_a, _b, _e)                                        \
    __MR_TEST_EXPECT(__MR_TEST_COND_NEAR(_a, _b, _e))
#define MR_TEST_ASSERT_NEAR(_a, _b, _e)                                        \
    __MR_TEST_ASSERT(__MR_TEST_COND_NEAR(_a, _b, _e))

/* Testcase entry type */
typedef void(mr_testcase_entry)(void);

/* Testcase type */
typedef struct mr_testcase {
    const char *suite;
    const char *name;
    mr_testcase_entry *entry;
} mr_testcase_t;

/* Testcase export macro definition */
#define __MR_TEST_EXPORT(_suite, _name, _entry, _level)                        \
    MR_USED const mr_testcase_t MR_CAT(__mr_testcase_, _suite, _, _name)       \
        MR_SECTION_ALIGN("mr_testcase." _level)                                \
        = {.suite = MR_STR(_suite), .name = MR_STR(_name), .entry = (_entry)}

/**
 * @brief This macro function exports a testcase.
 *
 * @param _suite The testcase suite name.
 * @param _name The testcase name.
 * @param _entry The testcase function.
 */
#define MR_TEST_EXPORT(_suite, _name, _entry)                                  \
    __MR_TEST_EXPORT(_suite, _name, _entry, "1")

/**
 * @brief This function runs a testcase.
 *
 * @param suite The testcase suite name.
 * @param name The testcase name.
 */
void mr_testcase_run(const char *suite, const char *name);

/**
 * @brief This function runs a testsuite.
 *
 * @param suite The testsuite name.
 */
void mr_testsuite_run(const char *suite);

/**
 * @brief This function runs all testcases.
 *
 * @return MR_TRUE on success, MR_FALSE on failure.
 */
mr_bool_t mr_test_run(void);
#endif /* defined(MR_USE_TEST) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_TEST_H__ */

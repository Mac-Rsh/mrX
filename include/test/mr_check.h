/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_TEST_CHECK_H__
#define __MR_TEST_CHECK_H__

#include <mr_config.h>
#if defined(MR_USE_TEST)
#include <mr-X/mr_printf.h>
#include <libc/mr_types.h>
#endif /* defined(MR_USE_TEST) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Test
 * @{
 */

#if defined(MR_USE_TEST)
/* External test status */
extern mr_uint32_t __mr_test_status;

/* Test pass magic number definition */
#define __MR_TEST_MAGIC_PASS            (0x70617373U)

/**
 * @brief This macro function checks if the condition is true.
 *
 * @param _a The first operand.
 * @param _cond The condition.
 * @param _b The second operand.
 * @param _type The operand type.
 * @param _fmt The format string for the operand.
 */
#define __MR_TEST_CHECK(_a, _cond, _b, _type, _fmt)                            \
    do {                                                                       \
        _type __a = (_type)(_a);                                               \
        _type __b = (_type)(_b);                                               \
        if (!(__a _cond __b)) {                                                \
            mr_printf("%s:%d: Test Failure\n", __FILE__, __LINE__);            \
            mr_printf("Expectation failed: %s %s %s\n", #_a, #_cond, #_b);     \
            mr_printf("  %s = "_fmt                                            \
                      "\n",                                                    \
                      #_a, __a);                                               \
            mr_printf("  %s = "_fmt                                            \
                      "\n",                                                    \
                      #_b, __b);                                               \
            __mr_test_status = ~__MR_TEST_MAGIC_PASS;                          \
        }                                                                      \
    } while (0)

/* Test check macros definition */
#define __MR_TEST_EQ(_a, _b, _type, _fmt)                                      \
    __MR_TEST_CHECK(_a, ==, _b, _type, _fmt)
#define __MR_TEST_NE(_a, _b, _type, _fmt)                                      \
    __MR_TEST_CHECK(_a, !=, _b, _type, _fmt)
#define __MR_TEST_LT(_a, _b, _type, _fmt)                                      \
    __MR_TEST_CHECK(_a, <, _b, _type, _fmt)
#define __MR_TEST_LE(_a, _b, _type, _fmt)                                      \
    __MR_TEST_CHECK(_a, <=, _b, _type, _fmt)
#define __MR_TEST_GT(_a, _b, _type, _fmt)                                      \
    __MR_TEST_CHECK(_a, >, _b, _type, _fmt)
#define __MR_TEST_GE(_a, _b, _type, _fmt)                                      \
    __MR_TEST_CHECK(_a, >=, _b, _type, _fmt)

/* Test expect and assert macros definition */
#define __MR_TEST_EXPECT(_check)        _check
#define __MR_TEST_ASSERT(_check)                                               \
    do {                                                                       \
        _check;                                                                \
        if (__mr_test_status == (~__MR_TEST_MAGIC_PASS)) {                     \
            return;                                                            \
        }                                                                      \
    } while (0)
#endif /* defined(MR_USE_TEST) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_TEST_CHECK_H__ */

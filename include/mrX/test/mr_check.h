/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_TEST_CHECK_H__
#define __MR_TEST_CHECK_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_TEST)
#include <mrX/kernel/mr_service.h>
#include <mrX/kernel/mr_printf.h>
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
extern volatile mr_uint32_t __mr_test_status;

/* Test pass magic number definition */
#define __MR_TEST_MAGIC_PASS            (0x70617373U)

/* Test printf macro definition */
#define __MR_TEST_PRINTF(_desc, _a_str, _a, _b_str, _b, _fmt)                  \
    do {                                                                       \
        mr_printf("%s:%d\n", __FILE__, __LINE__);                              \
        mr_printf("Expectation failed: %s\n", _desc);                          \
        mr_printf("  %s = "_fmt                                                \
                  "\n",                                                        \
                  (_a_str), (_a));                                             \
        mr_printf("  %s = "_fmt                                                \
                  "\n",                                                        \
                  (_b_str), (_b));                                             \
    } while (0)

/* Test check macro definition */
#define __MR_TEST_CHECK(_a, _cond, _b, _type, _fmt)                            \
    do {                                                                       \
        _type __a = (_type)(_a);                                               \
        _type __b = (_type)(_b);                                               \
        if (!(__a _cond __b)) {                                                \
            __MR_TEST_PRINTF(MR_STR(_a) " " MR_STR(_cond) " " MR_STR(_b),      \
                             MR_STR(_a), __a, MR_STR(_b), __b, _fmt);          \
            __mr_test_status = ~__MR_TEST_MAGIC_PASS;                          \
        }                                                                      \
    } while (0)
#define __MR_TEST_CHECK_NEAR(_a, _b, _e, _type, _fmt)                          \
    do {                                                                       \
        _type __a = (_type)(_a);                                               \
        _type __b = (_type)(_b);                                               \
        _type __e = (_type)(_e);                                               \
        _type __c = (__a > __b) ? (__a - __b) : (__b - __a);                   \
        if (!(__c <= __e)) {                                                   \
            __MR_TEST_PRINTF(                                                  \
                "| " MR_STR(_a) " - " MR_STR(_b) " | <= " MR_STR(_e),          \
                MR_STR(_a), __a, MR_STR(_b), __b, _fmt);                       \
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
#define __MR_TEST_NEAR(_a, _b, _e, _type, _fmt)                                \
    __MR_TEST_CHECK_NEAR(_a, _b, _e, _type, _fmt)

/* Test condition macros definition */
#define __MR_TEST_COND_INT(_a, _cond, _b)                                      \
    MR_CAT(__MR_TEST_, _cond)(_a, _b, mr_int64_t, "%lld")
#define __MR_TEST_COND_UINT(_a, _cond, _b)                                     \
    MR_CAT(__MR_TEST_, _cond)(_a, _b, mr_uint64_t, "%llu")
#define __MR_TEST_COND_PTR(_a, _cond, _b)                                      \
    MR_CAT(__MR_TEST_, _cond)(_a, _b, mr_ptr_t, "%p")
#define __MR_TEST_COND_FLOAT(_a, _cond, _b)                                    \
    MR_CAT(__MR_TEST_, _cond)(_a, _b, mr_f32_t, "%f")
#define __MR_TEST_COND_DOUBLE(_a, _cond, _b)                                   \
    MR_CAT(__MR_TEST_, _cond)(_a, _b, mr_f64_t, "%lf")
#define __MR_TEST_COND_NEAR(_a, _b, _e)                                        \
    __MR_TEST_NEAR(_a, _b, _e, mr_f64_t, "%lf")

/* Test expect and assert macros definition */
#define __MR_TEST_EXPECT(_check)        _check
#define __MR_TEST_ASSERT(_check)                                               \
    do {                                                                       \
        mr_uint32_t __s = __mr_test_status;                                    \
        __mr_test_status = __MR_TEST_MAGIC_PASS;                               \
        _check;                                                                \
        if (__mr_test_status == (~__MR_TEST_MAGIC_PASS)) {                     \
            return;                                                            \
        }                                                                      \
        __mr_test_status = __s;                                                \
    } while (0)
#endif /* defined(MR_USE_TEST) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_TEST_CHECK_H__ */

/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_SERVICE_H__
#define __MR_SERVICE_H__

#include <mrX/libc/mr_compiler.h>
#include <mrX/libc/mr_types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Service
 * @{
 */

/**
 * @brief This macro function gets the container of a member.
 *
 * @param _ptr The member pointer.
 * @param _type The container type.
 * @param _member The member.
 * @return The member container.
 */
#define MR_CONTAINER_OF(_ptr, _type, _member)                                  \
    ((void *)((char *)(_ptr) - (char *)((void *)&(((_type *)0)->_member))))

/**
 * @brief This macro function gets the size of an array.
 *
 * @param _array The array.
 * @return The array size.
 */
#define MR_ARRAY_SIZE(_array)           (sizeof(_array) / sizeof((_array)[0]))

/**
 * @brief This macro function gets the minimum value.
 *
 * @param _a The first value.
 * @param _b The second value.
 * @return The minimum value.
 *
 * @note Each argument may be evaluated more than once; expressions with side
 * effects are not supported.
 */
#define MR_MIN(_a, _b)                  (((_a) < (_b)) ? (_a) : (_b))

/**
 * @brief This macro function gets the maximum value.
 *
 * @param _a The first value.
 * @param _b The second value.
 * @return The maximum value.
 *
 * @note Each argument may be evaluated more than once; expressions with side
 * effects are not supported.
 */
#define MR_MAX(_a, _b)                  (((_a) > (_b)) ? (_a) : (_b))

/**
 * @brief This macro function aligns the value up.
 *
 * @param _x The value.
 * @param _ali The alignment.
 * @return The aligned value.
 */
#define MR_ALIGN_UP(_x, _ali)           (((_x) + (_ali) - 1) & ~((_ali) - 1))

/**
 * @brief This macro function aligns the value down.
 *
 * @param _x The value.
 * @param _ali The alignment.
 * @return The aligned value.
 */
#define MR_ALIGN_DOWN(_x, _ali)         ((_x) & ~((_ali) - 1))

/**
 * @brief This macro function performs integer division with rounding.
 *
 * @param _a The dividend.
 * @param _b The divisor.
 * @return The rounded quotient.
 */
#define MR_ROUND_DIV(_a, _b)                                                   \
    (((_a) + ((((_a) ^ (_b)) >= 0) ? (_b) : -(_b)) / 2) / (_b))

/**
 * @brief This macro function reverses the byte order of a 16-bit value.
 *
 * @param _value The value to reverse.
 * @return The value with reversed byte order.
 *
 * @note '_value' is evaluated multiple times and must have no side effects.
 */
#define MR_BSWAP16(_value)                                                     \
    ((mr_uint16_t)(((((mr_uint16_t)(_value)) & 0x00ffU) << 8)                  \
                   | ((((mr_uint16_t)(_value)) & 0xff00U) >> 8)))

/**
 * @brief This macro function reverses the byte order of a 32-bit value.
 *
 * @param _value The value to reverse.
 * @return The value with reversed byte order.
 *
 * @note '_value' is evaluated multiple times and must have no side effects.
 */
#define MR_BSWAP32(_value)                                                     \
    ((mr_uint32_t)(((((mr_uint32_t)(_value)) & 0x000000ffU) << 24)             \
                   | ((((mr_uint32_t)(_value)) & 0x0000ff00U) << 8)            \
                   | ((((mr_uint32_t)(_value)) & 0x00ff0000U) >> 8)            \
                   | ((((mr_uint32_t)(_value)) & 0xff000000U) >> 24)))

/**
 * @brief This macro function reverses the byte order of a 64-bit value.
 *
 * @param _value The value to reverse.
 * @return The value with reversed byte order.
 *
 * @note '_value' is evaluated multiple times and must have no side effects.
 */
#define MR_BSWAP64(_value)                                                     \
    ((mr_uint64_t)(((((mr_uint64_t)(_value)) & 0x00000000000000ffULL) << 56)   \
                   | ((((mr_uint64_t)(_value)) & 0x000000000000ff00ULL) << 40) \
                   | ((((mr_uint64_t)(_value)) & 0x0000000000ff0000ULL) << 24) \
                   | ((((mr_uint64_t)(_value)) & 0x00000000ff000000ULL) << 8)  \
                   | ((((mr_uint64_t)(_value)) & 0x000000ff00000000ULL) >> 8)  \
                   | ((((mr_uint64_t)(_value)) & 0x0000ff0000000000ULL) >> 24) \
                   | ((((mr_uint64_t)(_value)) & 0x00ff000000000000ULL) >> 40) \
                   | ((((mr_uint64_t)(_value)) & 0xff00000000000000ULL)        \
                      >> 56)))

/**
 * @brief This macro function makes a local variable.
 *
 * @param _type The local variable type.
 * @param ... The local variable value.
 */
#define MR_MAKE_LOCAL(_type, ...)       (&((_type){__VA_ARGS__}))

/**
 * @brief This macro function gets the number of arguments.
 *
 * @param ... The arguments
 * @return The number of arguments(0-63).
 */
#define MR_VA_ARGS_NUM(...)                                                    \
    __MR_VA_ARGS_NUM(, ##__VA_ARGS__, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54,  \
                     53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40,   \
                     39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26,   \
                     25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12,   \
                     11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define __MR_VA_ARGS_NUM(                                                      \
    _0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, \
    _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, \
    _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, \
    _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, _59, _60, _61, \
    _62, _63, _n, ...)                                                         \
    _n

/**
 * @brief This macro function gets the string length.
 *
 * @param _s The string.
 * @return The string length.
 */
#define MR_STR(_s)                      __MR_STR_IMPL(_s)
#define __MR_STR_IMPL(_s)               #_s

/**
 * @brief This macro function concatenates n strings.
 *
 * @param ... The strings.
 * @return The concatenated string.
 */
#define MR_CAT(...)                                                            \
    __MR_CAT(__MR_CAT_, MR_VA_ARGS_NUM(__VA_ARGS__))(__VA_ARGS__)
#define __MR_CAT_IMPL(_a, ...)          _a##__VA_ARGS__
#define __MR_CAT(_a, ...)               __MR_CAT_IMPL(_a, __VA_ARGS__)
#define __MR_CAT_0(_a, ...)
#define __MR_CAT_1(_a, ...)             _a
#define __MR_CAT_2(_a, ...)             __MR_CAT(_a, __MR_CAT_1(__VA_ARGS__))
#define __MR_CAT_3(_a, ...)             __MR_CAT(_a, __MR_CAT_2(__VA_ARGS__))
#define __MR_CAT_4(_a, ...)             __MR_CAT(_a, __MR_CAT_3(__VA_ARGS__))
#define __MR_CAT_5(_a, ...)             __MR_CAT(_a, __MR_CAT_4(__VA_ARGS__))
#define __MR_CAT_6(_a, ...)             __MR_CAT(_a, __MR_CAT_5(__VA_ARGS__))
#define __MR_CAT_7(_a, ...)             __MR_CAT(_a, __MR_CAT_6(__VA_ARGS__))
#define __MR_CAT_8(_a, ...)             __MR_CAT(_a, __MR_CAT_7(__VA_ARGS__))
#define __MR_CAT_9(_a, ...)             __MR_CAT(_a, __MR_CAT_8(__VA_ARGS__))
#define __MR_CAT_10(_a, ...)            __MR_CAT(_a, __MR_CAT_9(__VA_ARGS__))
#define __MR_CAT_11(_a, ...)            __MR_CAT(_a, __MR_CAT_10(__VA_ARGS__))
#define __MR_CAT_12(_a, ...)            __MR_CAT(_a, __MR_CAT_11(__VA_ARGS__))
#define __MR_CAT_13(_a, ...)            __MR_CAT(_a, __MR_CAT_12(__VA_ARGS__))
#define __MR_CAT_14(_a, ...)            __MR_CAT(_a, __MR_CAT_13(__VA_ARGS__))
#define __MR_CAT_15(_a, ...)            __MR_CAT(_a, __MR_CAT_14(__VA_ARGS__))

/**
 * @brief This macro function concatenates n strings with a separator.
 *
 * @param _s The separator.
 * @param ... The strings.
 * @return The concatenated string.
 */
#define MR_CAT_SEP(_s, ...)                                                    \
    MR_CAT(__MR_CAT_SEP_, MR_VA_ARGS_NUM(__VA_ARGS__))(_s, ##__VA_ARGS__)
#define __MR_CAT_SEP_0(_s, _a, ...)
#define __MR_CAT_SEP_1(_s, _a, ...)     MR_CAT(_s, _a)
#define __MR_CAT_SEP_2(_s, _a, ...)                                            \
    MR_CAT(_s, _a, __MR_CAT_SEP_1(_s, ##__VA_ARGS__))
#define __MR_CAT_SEP_3(_s, _a, ...)                                            \
    MR_CAT(_s, _a, __MR_CAT_SEP_2(_s, ##__VA_ARGS__))
#define __MR_CAT_SEP_4(_s, _a, ...)                                            \
    MR_CAT(_s, _a, __MR_CAT_SEP_3(_s, ##__VA_ARGS__))
#define __MR_CAT_SEP_5(_s, _a, ...)                                            \
    MR_CAT(_s, _a, __MR_CAT_SEP_4(_s, ##__VA_ARGS__))
#define __MR_CAT_SEP_6(_s, _a, ...)                                            \
    MR_CAT(_s, _a, __MR_CAT_SEP_5(_s, ##__VA_ARGS__))
#define __MR_CAT_SEP_7(_s, _a, ...)                                            \
    MR_CAT(_s, _a, __MR_CAT_SEP_6(_s, ##__VA_ARGS__))
#define __MR_CAT_SEP_8(_s, _a, ...)                                            \
    MR_CAT(_s, _a, __MR_CAT_SEP_7(_s, ##__VA_ARGS__))
#define __MR_CAT_SEP_9(_s, _a, ...)                                            \
    MR_CAT(_s, _a, __MR_CAT_SEP_8(_s, ##__VA_ARGS__))
#define __MR_CAT_SEP_10(_s, _a, ...)                                           \
    MR_CAT(_s, _a, __MR_CAT_SEP_9(_s, ##__VA_ARGS__))
#define __MR_CAT_SEP_11(_s, _a, ...)                                           \
    MR_CAT(_s, _a, __MR_CAT_SEP_10(_s, ##__VA_ARGS__))
#define __MR_CAT_SEP_12(_s, _a, ...)                                           \
    MR_CAT(_s, _a, __MR_CAT_SEP_11(_s, ##__VA_ARGS__))
#define __MR_CAT_SEP_13(_s, _a, ...)                                           \
    MR_CAT(_s, _a, __MR_CAT_SEP_12(_s, ##__VA_ARGS__))
#define __MR_CAT_SEP_14(_s, _a, ...)                                           \
    MR_CAT(_s, _a, __MR_CAT_SEP_13(_s, ##__VA_ARGS__))
#define __MR_CAT_SEP_15(_s, _a, ...)                                           \
    MR_CAT(_s, _a, __MR_CAT_SEP_14(_s, ##__VA_ARGS__))

/**
 * @brief This macro function strips the brackets from a code.
 *
 * @param ... The code.
 */
#define MR_DE_BRACKET(...)              __VA_ARGS__

/**
 * @brief This macro function conditionally compiles a code.
 *
 * @param _c The condition.
 * @param _if The code to compile if the condition is true.
 * @param _else The code to compile if the condition is false.
 * @return The code to compile.
 *
 * @note If '_c' == 1, 'MR_CAT(__MR_MACRO_IF_TRUE, _c)' becomes
 * '__MR_MACRO_IF_TRUE1', which is defined as '__CONDTRUE1,'. The trailing comma
 * artificially creates an extra argument, turning the call into
 * '__MR_MACRO_IF_IMPL(__CONDTRUE1, _if, _else)'. Here '_if' and '_else' are
 * still subject to immediate expansion. If '_if' contains an unprotected comma,
 * it will be split into separate arguments and the parameter list will be
 * corrupted. To prevent this, '_if' must be delivered inside an extra pair of
 * parentheses '()'. Those parentheses would, however, break most C statements,
 * so we need to strip them afterward. By pasting 'MR_DE_BRACKET' in front of
 * '()' we obtain 'MR_DE_BRACKET (_if)' which expands a second time into
 * '__VA_ARGS__' and the protective parentheses vanish, leaving valid C code.
 * Isn't that amazing? If you don't get it, just paste this whole block into any
 * AI and let it walk you through.
 */
#define MR_MACRO_IF(_c, _if, _else)                                            \
    __MR_MACRO_IF(MR_CAT(__MR_MACRO_IF_TRUE, _c), _if, _else)
#define __MR_MACRO_IF_IMPL(_c, _e, ...) MR_DE_BRACKET _e
#define __MR_MACRO_IF_TRUE1             __CONDTRUE1,
#define __MR_MACRO_IF(_c, _if, _else)   __MR_MACRO_IF_IMPL(_c _if, _else)

/**
 * @brief This macro function checks if a macro is used.
 *
 * @param _m The macro.
 * @return MR_TRUE if the macro is used, MR_FALSE otherwise.
 *
 * @note The macro must 1.
 */
#define MR_MACRO_IS_USED(_m)            MR_MACRO_IF(_m, (1), (0))

/**
 * @brief This macro function negates a macro boolean condition.
 *
 * @param _c The condition.
 * @return 1 if the condition is not 1, 0 otherwise.
 */
#define MR_MACRO_NOT(_c)                MR_MACRO_IF(_c, (0), (1))

/**
 * @brief This macro function performs logical AND on two macro conditions.
 *
 * @param _a The first condition.
 * @param _b The second condition.
 * @return 1 if both conditions are 1, 0 otherwise.
 */
#define MR_MACRO_AND(_a, _b)                                                   \
    MR_CAT(__MR_MACRO_AND_, MR_MACRO_IS_USED(_a), MR_MACRO_IS_USED(_b))
#define __MR_MACRO_AND_00               0
#define __MR_MACRO_AND_01               0
#define __MR_MACRO_AND_10               0
#define __MR_MACRO_AND_11               1

/**
 * @brief This macro function performs logical OR on two macro conditions.
 *
 * @param _a The first condition.
 * @param _b The second condition.
 * @return 1 if either condition is 1, 0 otherwise.
 */
#define MR_MACRO_OR(_a, _b)                                                    \
    MR_CAT(__MR_MACRO_OR_, MR_MACRO_IS_USED(_a), MR_MACRO_IS_USED(_b))
#define __MR_MACRO_OR_00                0
#define __MR_MACRO_OR_01                1
#define __MR_MACRO_OR_10                1
#define __MR_MACRO_OR_11                1

/**
 * @brief This function verifies the parity(even).
 *
 * @param src The source.
 * @param size The verify size.
 * @return MR_TRUE on even, MR_FALSE on odd.
 */
MR_INLINE mr_bool_t mr_parity_even(const void *src, mr_size_t size) {
    const mr_uint8_t bits16[16]
        = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
    mr_uint8_t *s, parity;
    mr_size_t i;

    /* Bitwise parity */
    for (s = (mr_uint8_t *)src, parity = 0, i = 0; i < size; ++i) {
        parity += bits16[s[i] & (0x0fU)];
        parity += bits16[s[i] >> 4];
    }
    return (parity & 0x01U) == 0;
}

/**
 * @brief This function verifies the parity(odd).
 *
 * @param src The source.
 * @param size The verify size.
 * @return MR_TRUE on odd, MR_FALSE on even.
 */
MR_INLINE mr_bool_t mr_parity_odd(const void *src, mr_size_t size) {
    /* Even parity negation */
    return !mr_parity_even(src, size);
}

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_SERVICE_H__ */

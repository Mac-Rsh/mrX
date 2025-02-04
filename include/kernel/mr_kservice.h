/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_SERVICE_H__
#define __MR_SERVICE_H__

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
 * @param _ptr The pointer to the member.
 * @param _type The type of the container.
 * @param _member The member of the container.
 * @return The container of the member.
 */
#define MR_CONTAINER_OF(_ptr, _type, _member)                                  \
    ((void *)((char *)(_ptr) - (char *)((void *)&(((_type *)0)->_member))))

/**
 * @brief This macro function gets the size of an array.
 *
 * @param _array The array.
 * @return The size of the array.
 */
#define MR_ARRAY_SIZE(_array)           (sizeof(_array) / sizeof(_array[0]))

/**
 * @brief This macro function aligns a value upward.
 *
 * @param _val The value.
 * @param _ali The alignment.
 * @return The aligned value.
 */
#define MR_ALIGN_UP(_val, _ali)         (((_val) + (_ali) - 1) & ~((_ali) - 1))

/**
 * @brief This macro function aligns a value downward.
 *
 * @param _val The value.
 * @param _ali The alignment.
 * @return The aligned value.
 */
#define MR_ALIGN_DOWN(_val, _ali)       ((_val) & ~((_ali) - 1))

/**
 * @brief This macro function checks if a bit is set.
 *
 * @param _val The value.
 * @param _mask The mask.
 * @return MR_TRUE if the bit is set, MR_FALSE otherwise.
 */
#define MR_BIT_IS_SET(_val, _mask)      (((_val) & (_mask)) == (_mask))

/**
 * @brief This macro function sets a bit.
 *
 * @param _val The value.
 * @param _mask The mask.
 */
#define MR_BIT_SET(_val, _mask)         ((_val) |= (_mask))

/**
 * @brief This macro function clear a bit.
 *
 * @param _val The value.
 * @param _mask The mask.
 */
#define MR_BIT_CLR(_val, _mask)         ((_val) &= ~(_mask))

/**
 * @brief This macro function makes a local variable.
 *
 * @param _type The type of the local variable.
 * @param ... The arguments of the local variable.
 */
#define MR_MAKE_LOCAL(_type, ...)       (&((_type){__VA_ARGS__}))

/**
 * @brief This macro function gets the minimum value.
 *
 * @param _a The first value.
 * @param _b The second value.
 * @return The minimum value.
 */
#define MR_MIN(_a, _b)                  (((_a) < (_b)) ? (_a) : (_b))

/**
 * @brief This macro function gets the maximum value.
 *
 * @param _a The first value.
 * @param _b The second value.
 * @return The maximum value.
 */
#define MR_MAX(_a, _b)                  (((_a) > (_b)) ? (_a) : (_b))

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_SERVICE_H__ */

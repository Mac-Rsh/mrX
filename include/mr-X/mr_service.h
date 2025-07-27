/**
 * @copyright (c) 2024-2025, MacRsh
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
 * @brief This macro function makes a local variable.
 *
 * @param _type The local variable type.
 * @param ... The local variable value.
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

/**
 * @brief This macro function aligns the value up.
 *
 * @param _x The value.
 * @param _ali The alignment.
 * @return The aligned value.
 */
#define MR_ALIGN_UP(_x, _ali)           (((_x) + (_ali)-1) & ~((_ali)-1))

/**
 * @brief This macro function aligns the value down.
 *
 * @param _x The value.
 * @param _ali The alignment.
 * @return The aligned value.
 */
#define MR_ALIGN_DOWN(_x, _ali)         ((_x) & ~((_ali)-1))

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_SERVICE_H__ */

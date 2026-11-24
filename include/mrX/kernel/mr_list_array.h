/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_LIST_ARRAY_H__
#define __MR_LIST_ARRAY_H__

#include <mrX/kernel/mr_listify.h>
#include <mrX/kernel/mr_list.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup List
 * @{
 */

/**
 * @brief This macro function initializes a list array with a given number.
 *
 * @param _list The list array to initialize.
 * @param _list_num The list number.
 *
 * @note The list number is limited by MR_LISTIFY (currently up to 4095).
 */
#define MR_LISTS_INIT(_list, _list_num)                                        \
    {MR_LISTIFY(_list_num, (, ), __MR_LIST_ARRAY_INIT_IMPL, _list)}
#define __MR_LIST_ARRAY_INIT_IMPL(_i, _list)                                   \
    MR_LIST_INIT(&(_list)[_i])

/**
 * @brief This macro function initializes a list array.
 *
 * @param _list The list array to initialize.
 *
 * @note The array size is limited by MR_LISTIFY (currently up to 4095).
 */
#define MR_LIST_ARRAY_INIT(_list)                                              \
    MR_LISTS_INIT(_list, MR_ARRAY_SIZE(_list))

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_LIST_ARRAY_H__ */

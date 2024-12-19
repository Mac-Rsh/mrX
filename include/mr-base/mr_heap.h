/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_HEAP_H__
#define __MR_HEAP_H__

#include <mr_config.h>
#ifdef MR_USE_LIBC
#include <stdlib.h>
#else
#include <mr-base/mr_types.h>
#endif /* MR_USE_LIBC */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Heap
 * @{
 */

/* Heap definition */
#ifdef MR_USE_LIBC
#define mr_malloc(_size)                malloc(_size)
#define mr_free(_memory)                free(_memory)
#define mr_malloc_usable_size(_memory)  malloc_usable_size(_memory)
#define mr_calloc(_num, _size)          calloc(_num, _size)
#define mr_realloc(_memory, _size)      realloc(_memory, _size)
#else
/* Heap block type */
typedef struct mr_heap_block
{
    struct mr_heap_block *next;
    mr_size_t size;
} mr_heap_block_t;

/**
 * @brief This function allocates memory.
 *
 * @param size The size of memory to allocate.
 * @return The allocated memory.
 */
void *mr_malloc(mr_size_t size);

/**
 * @brief This function free memory.
 *
 * @param memory The memory to free.
 */
void mr_free(void *memory);

/**
 * @brief This function returns the usable size of memory.
 *
 * @param memory The memory.
 * @return The usable size of memory.
 */
mr_size_t mr_malloc_usable_size(void *memory);

/**
 * @brief This function allocates memory.
 *
 * @param num The number of blocks to allocate.
 * @param size The size of each block.
 * @return The allocated memory.
 */
void *mr_calloc(mr_size_t num, mr_size_t size);

/**
 * @brief This function reallocates memory.
 *
 * @param memory The memory to reallocate.
 * @param size The new size of memory.
 * @return The reallocated memory.
 */
void *mr_realloc(void *memory, mr_size_t size);
#endif /* MR_USE_LIBC */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_HEAP_H__ */

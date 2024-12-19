/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include "mr-base/mr_heap.h"
#ifndef MR_USE_LIBC
#include <mr-base/mr_errno.h>
#include <mr-base/mr_autoinit.h>
#include <mr-base/mr_service.h>
#include <mr_hw.h>

#if (MR_CFG_HEAP_SIZE < 16)
#error "Heap size is too small(must >= 16)"
#endif /* MR_CFG_HEAP_SIZE */
static mr_uint8_t heap_memory[MR_CFG_HEAP_SIZE];
static mr_heap_block_t heap_start;

static mr_err_t mr_heap_init(void) {
    mr_heap_block_t *block;

    /* Initialize the first block */
    block = (mr_heap_block_t *)&heap_memory;
    block->next = MR_NULL;
    block->size = sizeof(heap_memory) - sizeof(mr_heap_block_t);
    heap_start.next = block;
    return MR_EOK;
}
MR_INIT_EXPORT(mr_heap_init, MR_INIT_LEVEL_SYSTEM);

static void __heap_insert_block(mr_heap_block_t *block) {
    mr_heap_block_t *prev;

    /* Search for the previous block */
    prev = &heap_start;
    while (((prev->next != MR_NULL) && (prev->next < block))) {
        prev = prev->next;
    }

    /* Insert the block */
    if (prev->next != MR_NULL) {
        /* Merge with the previous block */
        if ((void *)(((mr_uint8_t *)prev) + sizeof(mr_heap_block_t)
                     + prev->size)
            == (void *)block) {
            prev->size += block->size + sizeof(mr_heap_block_t);
            block = prev;
        }

        /* Merge with the next block */
        if ((void *)(((mr_uint8_t *)block) + sizeof(mr_heap_block_t)
                     + block->size)
            == (void *)prev->next) {
            block->size += prev->next->size + sizeof(mr_heap_block_t);
            block->next = prev->next->next;
            if (block != prev) {
                prev->next = block;
                block = prev;
            }
        }
    }

    /* Insert the block */
    if (block != prev) {
        block->next = prev->next;
        prev->next = block;
    }
}

void *mr_malloc(size_t size) {
    mr_heap_block_t *prev, *block, *new;
    mr_uint8_t *memory;
    size_t residual;
    int mask;

    /* Disable interrupt */
    mask = mr_hw_irq_save();

    /* Check size and residual memory */
    prev = &heap_start;
    block = heap_start.next;
    if ((size == 0) || (size > (MR_UINT32_MAX >> 1) || (block == MR_NULL))) {
        memory = MR_NULL;
        goto _exit;
    }

    /* Align the size to the next multiple of 4 bytes */
    size = MR_ALIGN_UP(size, 4);

    /* Search for and take blocks that match the criteria */
    while (block->size < size) {
        if (block->next == MR_NULL) {
            memory = MR_NULL;
            goto _exit;
        }
        prev = block;
        block = block->next;
    }
    prev->next = block->next;

    /* Allocate memory */
    memory = ((mr_uint8_t *)block + sizeof(mr_heap_block_t));
    residual = block->size - size;

    /* Set the block information */
    block->size = size;
    block->next = MR_NULL;

    /* Check if we need to allocate a new block */
    if (residual > (sizeof(mr_heap_block_t) << 1)) {
        new = (mr_heap_block_t *)(memory + size);

        /* Set the new block information */
        new->size = residual - sizeof(mr_heap_block_t);
        new->next = MR_NULL;

        /* Insert the new block */
        __heap_insert_block(new);
    }

_exit:
    /* Enable interrupt */
    mr_hw_irq_restore(mask);
    return memory;
}

void mr_free(void *memory) {
    mr_heap_block_t *block;
    int mask;

    if (memory == MR_NULL) {
        return;
    }

    /* Disable interrupt */
    mask = mr_hw_irq_save();

    /* Check the block */
    block = (mr_heap_block_t *)((mr_uint8_t *)memory - sizeof(mr_heap_block_t));
    if (block->size != 0) {
        /* Insert the free block */
        __heap_insert_block(block);
    }

    /* Enable interrupt */
    mr_hw_irq_restore(mask);
}

size_t mr_malloc_usable_size(void *memory) {
    mr_heap_block_t *block;

    if (memory == MR_NULL) {
        return 0;
    }

    /* Get the block information */
    block = (mr_heap_block_t *)((mr_uint8_t *)memory - sizeof(mr_heap_block_t));
    return block->size;
}

void *mr_calloc(size_t num, size_t size) {
    size_t total;
    void *memory;

    /* Calculate the total size */
    total = num * size;
    if (total == 0) {
        return MR_NULL;
    }

    /* Allocate memory */
    memory = mr_malloc(total);
    if (memory == MR_NULL) {
        /* Not enough memory */
        return MR_NULL;
    }

    /* Initialize the memory to 0 */
    memset(memory, 0, total);
    return memory;
}

void *mr_realloc(void *memory, size_t size) {
    size_t old_size;
    void *new;

    /* Get the old size */
    old_size = mr_malloc_usable_size(memory);

    /* Reallocate memory */
    new = mr_malloc(size);
    if (new == MR_NULL) {
        /* Not enough memory */
        return MR_NULL;
    }

    /* Copy the memory data to the new memory */
    memcpy(new, memory, old_size);
    mr_free(memory);
    return new;
}

#endif /* MR_USE_LIBC */

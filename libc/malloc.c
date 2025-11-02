/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mr-X/libc/mr_malloc.h>
#if defined(MR_USE_MALLOC)
#include <mr-X/port/mr_port_irq.h>
#include <mr-X/libc/mr_string.h>

/* Heap memory */
#if !defined(MR_CFG_HEAP_SIZE)
#define MR_CFG_HEAP_SIZE                (4 * 1024)
#endif /* !defined(MR_CFG_HEAP_SIZE) */
#if (MR_CFG_HEAP_SIZE < 32)
#error "Heap size is too small(must >= 32)"
#endif /* MR_CFG_HEAP_SIZE */
static mr_uint8_t MR_ALIGN(MR_ALIGN_SIZE) __heap[MR_CFG_HEAP_SIZE];
/* Heap block head */
static mr_heap_block_t __blk_head = {.next = MR_NULL};

/* Heap definition */
#define HEAP_ALIGN(_val, _ali)          (((_val) + (_ali) - 1) & ~((_ali) - 1))
#define HEAP_SPLIT_THRESHOLD            (sizeof(mr_heap_block_t) << 2)

static void heap_insert_block(mr_heap_block_t *blk) {
    mr_heap_block_t *prev;
    mr_size_t size;
    void *merge;

    /* Search for the previous block */
    for (prev = &__blk_head; prev->next; prev = prev->next) {
        if (prev->next < blk) {
            continue;
        }
        break;
    }

    /* Insert the block */
    if (prev->next) {
        /* Merge with the previous block */
        size = sizeof(mr_heap_block_t) + prev->size;
        merge = (mr_uint8_t *)prev + size;
        if (merge == (void *)blk) {
            prev->size += blk->size + sizeof(mr_heap_block_t);
            blk = prev;
        }

        /* Merge with the next block */
        size = sizeof(mr_heap_block_t) + blk->size;
        merge = (mr_uint8_t *)blk + size;
        if (merge == (void *)prev->next) {
            blk->size += prev->next->size + sizeof(mr_heap_block_t);
            blk->next = prev->next->next;
            if (blk != prev) {
                prev->next = blk;
                blk = prev;
            }
        }
    }

    /* Insert the block */
    if (blk != prev) {
        blk->next = prev->next;
        prev->next = blk;
    }
}

void *mr_malloc(mr_size_t size) {
    mr_heap_block_t *prev, *blk, *new;
    mr_size_t res;
    void *mem;
    int mask;

    /* Check parameter */
    if (size == 0) {
        return MR_NULL;
    }

    /* Disable interrupt */
    mask = mr_port_irq_disable();

    /* Check if the heap is empty */
    prev = &__blk_head;
    if (!prev->next) {
        mem = MR_NULL;
        goto _exit;
    }

    /* Align the size */
    size = HEAP_ALIGN(size, MR_ALIGN_SIZE);
    if (size == 0) {
        /* Overflow */
        mem = MR_NULL;
        goto _exit;
    }

    /* Search for and take blocks that match the criteria */
    for (blk = prev->next; blk->size < size; blk = blk->next) {
        if (!blk->next) {
            mem = MR_NULL;
            goto _exit;
        }
        prev = blk;
    }

    /* Allocate memory */
    mem = (mr_uint8_t *)blk + sizeof(mr_heap_block_t);
    res = blk->size - size;
    prev->next = blk->next;
    blk->next = MR_NULL;
    blk->magic = MR_HEAP_MAGIC;

    /* Check if we need to allocate a new block */
    if (res > HEAP_SPLIT_THRESHOLD) {
        /* Shrink the old block */
        blk->size = size;

        /* Set the new block information */
        new = (mr_heap_block_t *)((mr_uint8_t *)mem + size);
        new->size = res - sizeof(mr_heap_block_t);
        new->next = MR_NULL;
        new->magic = ~MR_HEAP_MAGIC;

        /* Insert the new block */
        heap_insert_block(new);
    }
_exit:
    /* Enable interrupt */
    mr_port_irq_enable(mask);
    return mem;
}

void mr_free(void *mem) {
    mr_heap_block_t *blk;
    int mask;

    /* Check parameter */
    if (!mem) {
        return;
    }

    /* Disable interrupt */
    mask = mr_port_irq_disable();

    /* Check the block */
    blk = (mr_heap_block_t *)((mr_uint8_t *)mem - sizeof(mr_heap_block_t));
    if (blk->magic == MR_HEAP_MAGIC) {
        /* Insert the free block */
        heap_insert_block(blk);

        /* Mark the block as free */
        blk->magic = ~MR_HEAP_MAGIC;
    }

    /* Enable interrupt */
    mr_port_irq_enable(mask);
}

mr_size_t mr_malloc_usable_size(void *mem) {
    mr_heap_block_t *blk;

    /* Check parameter */
    if (!mem) {
        return 0;
    }

    /* Get the block information */
    blk = (mr_heap_block_t *)((mr_uint8_t *)mem - sizeof(mr_heap_block_t));
    if (blk->magic != MR_HEAP_MAGIC) {
        return 0;
    }
    return blk->size;
}

void *mr_calloc(mr_size_t num, mr_size_t size) {
    mr_size_t total;
    void *mem;

    /* Calculate the total size */
    total = num * size;
    if (total == 0) {
        return MR_NULL;
    }

    /* Allocate memory */
    mem = mr_malloc(total);
    if (!mem) {
        /* Not enough memory */
        return MR_NULL;
    }

    /* Set the memory to 0 */
    mr_memset(mem, 0, total);
    return mem;
}

void *mr_realloc(void *mem, mr_size_t size) {
    mr_size_t old_size;
    void *new;

    /* Get the old size */
    if (mem) {
        old_size = mr_malloc_usable_size(mem);
    } else {
        old_size = 0;
    }

    /* Reallocate memory */
    new = mr_malloc(size);
    if (!new && (size > 0)) {
        /* Not enough memory */
        return MR_NULL;
    }

    /* Copy the memory data to the new memory */
    mr_memcpy(new, mem, old_size < size ? old_size : size);

    /* Free the old memory */
    mr_free(mem);
    return new;
}

static mr_heap_block_t *heap_block_make(mr_ptr_t mem, mr_size_t size) {
    mr_uintptr_t offset;
    mr_heap_block_t *blk;

    /* Set the block information */
    blk = (mr_heap_block_t *)HEAP_ALIGN((mr_uintptr_t)mem, MR_ALIGN_SIZE);
    offset = (mr_uintptr_t)blk - (mr_uintptr_t)mem;
    size = size - sizeof(mr_heap_block_t);
    blk->size = size - offset;
    blk->next = MR_NULL;
    blk->magic = ~MR_HEAP_MAGIC;
    return blk;
}

void mr_heap_region_add(const mr_heap_region_t *regions) {
    mr_heap_block_t *blk, *blk_head;
    const mr_heap_region_t *rgn;
    mr_ptr_t mem;

    for (rgn = regions; rgn->address && (rgn->size > 0); rgn++) {
        /* Check if the block is too small */
        if (rgn->size < ((HEAP_SPLIT_THRESHOLD * 2) + MR_ALIGN_SIZE)) {
            continue;
        }

        /* Make a head block and an isolation belt */
        blk_head = heap_block_make(rgn->address, rgn->size);
        mem = (mr_uint8_t *)blk_head + sizeof(mr_heap_block_t);
        mem = (mr_uint8_t *)mem + MR_ALIGN_SIZE;
        blk_head->size -= MR_ALIGN_SIZE;

        /* Make a new block */
        blk = heap_block_make(mem, blk_head->size);
        blk_head->size = 0;

        /* Insert the blocks */
        heap_insert_block(blk_head);
        heap_insert_block(blk);
    }
}

void mr_libc_malloc_init(void) {
    mr_heap_block_t *blk;

    /* Make the first block */
    blk = heap_block_make(__heap, sizeof(__heap));

    /* Insert the first block */
    __blk_head.next = blk;
    __blk_head.size = 0;
}
#endif /* defined(MR_USE_MALLOC) */

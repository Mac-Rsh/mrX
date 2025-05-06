/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <libc/mr_malloc.h>
#if (!defined(MR_USE_LIBC_MALLOC)) && (!defined(MR_USE_3PARTY_MALLOC))
#include <port/mr_port_irq.h>
#include <libc/mr_string.h>

#if !defined(MR_CFG_HEAP_SIZE)
#define MR_CFG_HEAP_SIZE                (4 * 1024)
#endif /* !defined(MR_CFG_HEAP_SIZE) */
#if (MR_CFG_HEAP_SIZE < 24)
#error "Heap size is too small(must >= 24)"
#endif /* MR_CFG_HEAP_SIZE */
/* Heap memory */
static mr_uint8_t heap[MR_CFG_HEAP_SIZE];
/* Heap block head */
static mr_memblk_t blk_head = {.size = 1};

/* Heap magic number */
#define HEAP_MAGIC                      (0x6d722d58U)
/* Heap align macro function */
#define HEAP_ALIGN(_val, _ali)          (((_val) + (_ali)-1) & ~((_ali)-1))

static void heap_insert_block(mr_memblk_t *blk) {
    mr_memblk_t *prev;

    /* Search for the previous block */
    for (prev = &blk_head; prev->next; prev = prev->next) {
        if (prev->next < blk) {
            continue;
        }
        break;
    }

    /* Insert the block */
    if (prev->next) {
        /* Merge with the previous block */
        if ((void *)(((mr_uint8_t *)prev) + sizeof(mr_memblk_t) + prev->size)
            == (void *)blk) {
            prev->size += blk->size + sizeof(mr_memblk_t);
            blk = prev;
        }

        /* Merge with the next block */
        if ((void *)(((mr_uint8_t *)blk) + sizeof(mr_memblk_t) + blk->size)
            == (void *)prev->next) {
            blk->size += prev->next->size + sizeof(mr_memblk_t);
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
    mr_memblk_t *prev, *blk, *new;
    mr_size_t res;
    void *mem;
    int mask;

    /* Disable interrupt */
    mask = mr_port_irq_save();

    /* Initialize the heap block */
    if (blk_head.size != 0) {
        blk = (mr_memblk_t *)heap;
        blk->size = sizeof(heap) - sizeof(mr_memblk_t);
        blk->magic = ~HEAP_MAGIC;
        blk->next = MR_NULL;
        blk_head.next = blk;
        blk_head.size = 0;
    }

    /* Check memory and size */
    if ((!blk_head.next) || (size == 0)) {
        mem = MR_NULL;
        goto _exit;
    }

    /* Align the size to the next multiple of sizeof(void *) */
    size = HEAP_ALIGN(size, sizeof(void *));

    /* Search for and take blocks that match the criteria */
    for (prev = &blk_head, blk = prev->next; blk->size < size;
         prev = blk, blk = blk->next) {
        if (!blk->next) {
            mem = MR_NULL;
            goto _exit;
        }
    }

    /* Allocate memory */
    mem = ((mr_uint8_t *)blk + sizeof(mr_memblk_t));
    res = blk->size - size;
    prev->next = blk->next;
    blk->magic = HEAP_MAGIC;
    blk->next = MR_NULL;
    blk->size = size;

    /* Check if we need to allocate a new block */
    if (res > (sizeof(mr_memblk_t) << 1)) {
        new = (mr_memblk_t *)((mr_uint8_t *)mem + size);

        /* Set the new block information */
        new->size = res - sizeof(mr_memblk_t);
        new->magic = ~HEAP_MAGIC;
        new->next = MR_NULL;

        /* Insert the new block */
        heap_insert_block(new);
    }

_exit:
    /* Enable interrupt */
    mr_port_irq_restore(mask);
    return mem;
}

void mr_free(void *mem) {
    mr_memblk_t *blk;
    int mask;

    /* Check arguments */
    if (!mem) {
        return;
    }

    /* Disable interrupt */
    mask = mr_port_irq_save();

    /* Check the block */
    blk = (mr_memblk_t *)((mr_uint8_t *)mem - sizeof(mr_memblk_t));
    if ((blk->magic == HEAP_MAGIC) && (blk->size != 0)) {
        /* Clear the block magic */
        blk->magic = ~HEAP_MAGIC;

        /* Insert the free block */
        heap_insert_block(blk);
    }

    /* Enable interrupt */
    mr_port_irq_restore(mask);
}

mr_size_t mr_malloc_usable_size(void *mem) {
    mr_memblk_t *blk;

    /* Check arguments */
    if (!mem) {
        return 0;
    }

    /* Get the block information */
    blk = (mr_memblk_t *)((mr_uint8_t *)mem - sizeof(mr_memblk_t));
    if (blk->magic != HEAP_MAGIC) {
        /* Not a heap block */
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
    old_size = mr_malloc_usable_size(mem);

    /* Reallocate memory */
    new = mr_malloc(size);
    if (!new) {
        /* Not enough memory */
        return MR_NULL;
    }

    /* Copy the memory data to the new memory */
    mr_memcpy(new, mem, old_size < size ? old_size : size);

    /* Free the old memory */
    mr_free(mem);
    return new;
}
#endif /* !defined(MR_USE_LIBC_MALLOC) && !defined(MR_USE_3PARTY_MALLOC) */

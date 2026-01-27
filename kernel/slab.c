/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/kernel/mr_slab.h>
#include <mrX/libc/mr_string.h>

/* Slab map definition */
#define SLAB_MAP_NUM(_num)              (((_num) + 31) >> 5)

void *mr_slab_alloc(mr_slab_t *slab) {
    mr_uint32_t i, bit, mask, off;
    mr_atomic_t last;

    for (i = 0; i < SLAB_MAP_NUM(slab->num); i++) {
        /* Get current bitmap */
        last = mr_atomic_load(&slab->map[i]);

        do {
            /* Get free bit */
            bit = (mr_uint32_t)mr_ffs32(~((mr_uint32_t)last));
            if (bit == 0) {
                /* No free bit */
                break;
            }

            /* Try to mark a bit as allocated */
            mask = (mr_uint32_t)last | (1U << (bit - 1));
        } while (!mr_atomic_compare_exchange(&slab->map[i], &last, mask));

        /* Check if memory is allocated */
        if (bit == 0) {
            continue;
        }

        /* Get bit index */
        off = (i << 5) + (bit - 1);
        if (off >= slab->num) {
            /* Out of range */
            return MR_NULL;
        }

        /* Return memory */
        return slab->buf + (off * slab->size);
    }
    return MR_NULL;
}

void mr_slab_free(mr_slab_t *slab, void *mem) {
    mr_uint32_t off, i, bit;

    /* Check parameter */
    if ((mem >= (void *)(slab->buf + (slab->num * slab->size)))
        || (mem < (void *)slab->buf)) {
        return;
    }

    /* Get bit index */
    off = (mr_uint32_t)((mr_uint8_t *)mem - slab->buf) / slab->size;
    bit = off & 0x1fU;
    i = off >> 5;

    /* Mark a bit as free */
    mr_atomic_fetch_and(&slab->map[i], ~(1U << bit));
}

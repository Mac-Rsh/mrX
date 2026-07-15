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
#define SLAB_MAP_NUM(_num)                                                     \
    (((_num) / MR_SLAB_MAP_BITS) + (((_num) % MR_SLAB_MAP_BITS) != 0U))

void *mr_slab_alloc(mr_slab_t *slab) {
    mr_uint32_t i, bit, off, valid;
    mr_uint64_t mask;
    mr_atomic_t last, desired;

    for (i = 0; i < SLAB_MAP_NUM(slab->num); i++) {
        /* Get valid bitmap */
        valid = MR_MIN(slab->num - (i * MR_SLAB_MAP_BITS), MR_SLAB_MAP_BITS);
        mask = valid == MR_SLAB_MAP_BITS ? MR_UINT64_MAX
                                         : ((1ULL << valid) - 1ULL);

        /* Get current bitmap */
        last = mr_atomic_load(&slab->map[i]);

        do {
            /* Get free bit */
            bit = (mr_uint32_t)mr_ffs64(~((mr_uint64_t)last) & mask);
            if (bit == 0) {
                /* No free bit */
                break;
            }

            /* Try to mark a bit as allocated */
            desired = last | (mr_atomic_t)(1ULL << (bit - 1U));
        } while (!mr_atomic_compare_exchange(&slab->map[i], &last, desired));

        /* Check if memory is allocated */
        if (bit == 0) {
            continue;
        }

        /* Get bit index */
        off = (i * MR_SLAB_MAP_BITS) + (bit - 1U);

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

    /* Get memory offset */
    off = (mr_uint32_t)((mr_uint8_t *)mem - slab->buf);
    if ((off % slab->size) != 0U) {
        /* Memory is not an object start */
        return;
    }

    /* Get bit index */
    off /= slab->size;
    bit = off % MR_SLAB_MAP_BITS;
    i = off / MR_SLAB_MAP_BITS;

    /* Mark a bit as free */
    mr_atomic_fetch_and(&slab->map[i], ~(mr_atomic_t)(1ULL << bit));
}

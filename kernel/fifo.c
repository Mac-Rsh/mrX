/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mr-X/mr_fifo.h>
#include <libc/mr_assert.h>
#include <libc/mr_string.h>
#include <libc/mr_malloc.h>

/* Fifo align macro function */
#define FIFO_ALIGN_POW2_UP(_size)                                              \
    (((_size) & ((_size)-1)) ? (1U << mr_fls32(_size)) : (_size))
#define FIFO_ALIGN_POW2_DOWN(_size)     (1U << (mr_fls32(_size) - 1))

mr_uint32_t mr_fifo_init(mr_fifo_t *fifo, void *buf, mr_uint32_t size,
                         mr_uint32_t elemsz) {
    /* Check parameter */
    MR_ASSERT((fifo != MR_NULL) && (buf != MR_NULL));
    MR_ASSERT((size >= 2) && (elemsz > 0) && (elemsz <= size));

    /* Align size(down to power of 2) */
    size = FIFO_ALIGN_POW2_DOWN(size / elemsz);

    /* Init fifo */
    fifo->buf = buf;
    fifo->mask = size - 1;
    mr_atomic_init(&fifo->ridx, 0);
    mr_atomic_init(&fifo->widx, 0);
    fifo->elemsz = elemsz;
    return size;
}

mr_uint32_t mr_fifo_alloc(mr_fifo_t *fifo, mr_uint32_t num,
                          mr_uint32_t elemsz) {
    mr_uint32_t size;
    void *buf;

    /* Check parameter */
    MR_ASSERT(fifo != MR_NULL);
    MR_ASSERT((num > 0) && (elemsz > 0));

    /* Align size(up to power of 2) */
    size = FIFO_ALIGN_POW2_UP(num * elemsz);

    /* Allocate buffer */
    buf = mr_realloc(fifo->buf, size);
    if (!buf) {
        /* Not enough memory */
        return 0;
    }

    /* Init fifo */
    return mr_fifo_init(fifo, buf, size, elemsz);
}

void mr_fifo_free(mr_fifo_t *fifo) {
    /* Check parameter */
    MR_ASSERT(fifo != MR_NULL);

    /* Check need to free */
    if (!fifo->buf) {
        return;
    }

    /* Free buffer */
    mr_free(fifo->buf);
    fifo->buf = MR_NULL;
}

mr_uint32_t mr_fifo_push(mr_fifo_t *fifo, const void *data) {
    mr_uint32_t index;

    /* Check parameter */
    MR_ASSERT(fifo != MR_NULL);
    MR_ASSERT(data != MR_NULL);

    /* Check if fifo is full */
    if (mr_fifo_is_full(fifo)) {
        return 0;
    }

    /* Get write index */
    index = (mr_uint32_t)mr_atomic_load(&fifo->widx) & fifo->mask;
    if (fifo->elemsz != 1) {
        index *= fifo->elemsz;
    }

    /* Push data */
    mr_memcpy((mr_uint8_t *)fifo->buf + index, data, fifo->elemsz);
    mr_atomic_fetch_add(&fifo->widx, 1);
    return 1;
}

mr_uint32_t mr_fifo_pop(mr_fifo_t *fifo, void *data) {
    mr_uint32_t index;

    /* Check parameter */
    MR_ASSERT(fifo != MR_NULL);
    MR_ASSERT(data != MR_NULL);

    /* Check if fifo is empty */
    if (mr_fifo_is_empty(fifo)) {
        return 0;
    }

    /* Get read index */
    index = (mr_uint32_t)mr_atomic_load(&fifo->ridx) & fifo->mask;
    if (fifo->elemsz != 1) {
        index *= fifo->elemsz;
    }

    /* Pop data */
    mr_memcpy(data, (mr_uint8_t *)fifo->buf + index, fifo->elemsz);
    mr_atomic_fetch_add(&fifo->ridx, 1);
    return 1;
}

mr_uint32_t mr_fifo_write(mr_fifo_t *fifo, const void *buf, mr_uint32_t num) {
    mr_uint32_t avail, num2, index, size, n;

    /* Check parameter */
    MR_ASSERT(fifo != MR_NULL);
    MR_ASSERT((buf != MR_NULL) || (num == 0));

    /* Limit number */
    avail = mr_fifo_avail(fifo);
    if (num > avail) {
        num = avail;
    }
    if (num == 0) {
        return 0;
    }
    num2 = num;

    /* Get write index */
    index = (mr_uint32_t)mr_atomic_load(&fifo->widx) & fifo->mask;
    size = fifo->mask + 1;
    if (fifo->elemsz != 1) {
        index *= fifo->elemsz;
        size *= fifo->elemsz;
        num *= fifo->elemsz;
    }

    /* Push buffer */
    n = MR_MIN(num, size - index);
    mr_memcpy((mr_uint8_t *)fifo->buf + index, buf, n);
    mr_memcpy(fifo->buf, (mr_uint8_t *)buf + n, num - n);
    mr_atomic_fetch_add(&fifo->widx, (mr_atomic_t)num2);
    return num2;
}

mr_uint32_t mr_fifo_overwrite(mr_fifo_t *fifo, const void *buf,
                              mr_uint32_t num) {
    mr_uint32_t cap, avail;

    /* Check parameter */
    MR_ASSERT(fifo != MR_NULL);
    MR_ASSERT((buf != MR_NULL) || (num == 0));

    /* Limit number */
    cap = fifo->mask + 1;
    if (num > cap) {
        buf = (mr_uint8_t *)buf + ((num - cap) * fifo->elemsz);
        num = cap;
    }

    /* Limit size */
    avail = mr_fifo_avail(fifo);
    if (num > avail) {
        mr_fifo_skip(fifo, num - avail);
    }

    /* Push buffer */
    return mr_fifo_write(fifo, buf, num);
}

mr_uint32_t mr_fifo_peek(mr_fifo_t *fifo, void *buf, mr_uint32_t num) {
    mr_uint32_t avail, num2, index, size, n;

    /* Check parameter */
    MR_ASSERT(fifo != MR_NULL);
    MR_ASSERT((buf != MR_NULL) || (num == 0));

    /* Limit number */
    avail = mr_fifo_len(fifo);
    if (num > avail) {
        num = avail;
    }
    if (num == 0) {
        return 0;
    }
    num2 = num;

    /* Get read index */
    index = (mr_uint32_t)mr_atomic_load(&fifo->ridx) & fifo->mask;
    size = fifo->mask + 1;
    if (fifo->elemsz != 1) {
        index *= fifo->elemsz;
        size *= fifo->elemsz;
        num *= fifo->elemsz;
    }

    /* Peek buffer */
    n = MR_MIN(num, size - index);
    mr_memcpy(buf, (mr_uint8_t *)fifo->buf + index, n);
    mr_memcpy((mr_uint8_t *)buf + n, fifo->buf, num - n);
    return num2;
}

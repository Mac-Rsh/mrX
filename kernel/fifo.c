/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/kernel/mr_fifo.h>
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>

/* Fifo align macro function */
#define FIFO_ALIGN_POW2_DOWN(_size)     (1U << (mr_fls32(_size) - 1))

mr_uint32_t mr_fifo_init(mr_fifo_t *fifo, void *buf, mr_uint32_t size,
                         mr_uint32_t elemsz) {
    /* Check parameter */
    if ((buf == MR_NULL) || (elemsz == 0) || (size < elemsz)) {
        return 0;
    }

    /* Align size(down to power of 2) */
    size /= elemsz;
    if (size == 0) {
        return 0;
    }
    size = FIFO_ALIGN_POW2_DOWN(size);

    /* Init fifo */
    mr_atomic_init(&fifo->ridx, 0);
    mr_atomic_init(&fifo->widx, 0);
    fifo->elemsz = elemsz;
    fifo->mask = size - 1;
    fifo->buf = buf;
    return size;
}

mr_uint32_t mr_fifo_alloc(mr_fifo_t *fifo, mr_uint32_t num,
                          mr_uint32_t elemsz) {
    mr_uint32_t cap, size;
    int shift;
    void *buf;

    /* Check parameter */
    if ((fifo == MR_NULL) || (num == 0) || (elemsz == 0)) {
        return 0;
    }
    if (num > (MR_UINT32_MAX / elemsz)) {
        return 0;
    }

    /* Align capacity(up to power of 2) */
    shift = mr_fls32(num - 1U);
    if (shift < 32) {
        cap = 1U << shift;
    } else {
        return 0;
    }

    /* Get buffer size */
    if (cap <= (MR_UINT32_MAX / elemsz)) {
        size = cap * elemsz;
    } else {
        return 0;
    }

    /* Allocate buffer */
    buf = mr_malloc(size);
    if (!buf) {
        /* Not enough memory */
        return 0;
    }

    /* Init fifo */
    return mr_fifo_init(fifo, buf, size, elemsz);
}

void mr_fifo_free(mr_fifo_t *fifo) {
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

    /* Check if fifo is not initialized */
    if (!fifo->buf) {
        return 0;
    }

    /* Check if fifo is full */
    if (mr_fifo_is_full(fifo)) {
        return 0;
    }

    /* Get write index */
    index = (mr_uint32_t)mr_atomic_load(&fifo->widx) & fifo->mask;
    if (fifo->elemsz != 1U) {
        index *= fifo->elemsz;
    }

    /* Push data */
    mr_memcpy(fifo->buf + index, data, fifo->elemsz);
    mr_atomic_fetch_add(&fifo->widx, 1);
    return 1;
}

mr_uint32_t mr_fifo_pop(mr_fifo_t *fifo, void *data) {
    mr_uint32_t index;

    /* Check if fifo is not initialized */
    if (!fifo->buf) {
        return 0;
    }

    /* Check if fifo is empty */
    if (mr_fifo_is_empty(fifo)) {
        return 0;
    }

    /* Get read index */
    index = (mr_uint32_t)mr_atomic_load(&fifo->ridx) & fifo->mask;
    if (fifo->elemsz != 1U) {
        index *= fifo->elemsz;
    }

    /* Pop data */
    mr_memcpy(data, fifo->buf + index, fifo->elemsz);
    mr_atomic_fetch_add(&fifo->ridx, 1);
    return 1;
}

mr_uint32_t mr_fifo_write(mr_fifo_t *fifo, const void *buf, mr_uint32_t num) {
    mr_uint32_t avail, num2, index, size, n;

    /* Check if fifo is not initialized */
    if (!fifo->buf) {
        return 0;
    }

    /* Limit number */
    avail = mr_fifo_avail(fifo);
    if (num > avail) {
        num = avail;
    }
    if (num == 0U) {
        return 0;
    }
    num2 = num;

    /* Get write index */
    index = (mr_uint32_t)mr_atomic_load(&fifo->widx) & fifo->mask;
    size = fifo->mask + 1U;
    if (fifo->elemsz != 1U) {
        index *= fifo->elemsz;
        size *= fifo->elemsz;
        num *= fifo->elemsz;
    }

    /* Push buffer */
    n = MR_MIN(num, size - index);
    mr_memcpy(fifo->buf + index, buf, n);
    mr_memcpy(fifo->buf, (mr_uint8_t *)buf + n, num - n);
    mr_atomic_fetch_add(&fifo->widx, (mr_atomic_t)num2);
    return num2;
}

mr_uint32_t mr_fifo_overwrite(mr_fifo_t *fifo, const void *buf,
                              mr_uint32_t num) {
    mr_uint32_t cap, avail;

    /* Check if fifo is not initialized */
    if (!fifo->buf) {
        return 0;
    }

    /* Limit number */
    cap = fifo->mask + 1U;
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

    /* Check if fifo is not initialized */
    if (!fifo->buf) {
        return 0;
    }

    /* Limit number */
    avail = mr_fifo_len(fifo);
    if (num > avail) {
        num = avail;
    }
    if (num == 0U) {
        return 0;
    }
    num2 = num;

    /* Get read index */
    index = (mr_uint32_t)mr_atomic_load(&fifo->ridx) & fifo->mask;
    size = fifo->mask + 1U;
    if (fifo->elemsz != 1U) {
        index *= fifo->elemsz;
        size *= fifo->elemsz;
        num *= fifo->elemsz;
    }

    /* Peek buffer */
    n = MR_MIN(num, size - index);
    mr_memcpy(buf, fifo->buf + index, n);
    mr_memcpy((mr_uint8_t *)buf + n, fifo->buf, num - n);
    return num2;
}

mr_uint32_t mr_fifo_write_claim(mr_fifo_t *fifo, mr_uint32_t off, void **buf,
                                mr_uint32_t want) {
    mr_uint32_t avail, index;

    /* Check if fifo is not initialized */
    if (!fifo->buf) {
        return 0;
    }

    /* Limit wants */
    avail = mr_fifo_avail(fifo);
    if (off >= avail) {
        return 0;
    }
    want = MR_MIN(want, avail - off);
    if (want == 0U) {
        return 0;
    }

    /* Get write index */
    index = ((mr_uint32_t)mr_atomic_load(&fifo->widx) + off) & fifo->mask;
    want = MR_MIN(want, (fifo->mask + 1U) - index);

    /* Claim buffer */
    *buf = fifo->buf + index * fifo->elemsz;
    return want;
}

mr_uint32_t mr_fifo_overwrite_claim(mr_fifo_t *fifo, mr_uint32_t off,
                                    void **buf, mr_uint32_t want) {
    mr_uint32_t cap, index;

    /* Check if fifo is not initialized */
    if (!fifo->buf) {
        return 0;
    }

    /* Limit wants */
    cap = fifo->mask + 1U;
    if (off >= cap) {
        return 0;
    }
    want = MR_MIN(want, cap - off);
    if (want == 0U) {
        return 0;
    }

    /* Get write index */
    index = ((mr_uint32_t)mr_atomic_load(&fifo->widx) + off) & fifo->mask;
    want = MR_MIN(want, cap - index);

    /* Claim buffer */
    *buf = fifo->buf + (index * fifo->elemsz);
    return want;
}

mr_uint32_t mr_fifo_read_claim(mr_fifo_t *fifo, mr_uint32_t off, void **buf,
                               mr_uint32_t want) {
    mr_uint32_t avail, index;

    /* Check if fifo is not initialized */
    if (!fifo->buf) {
        return 0;
    }

    /* Limit wants */
    avail = mr_fifo_len(fifo);
    if (off >= avail) {
        return 0;
    }
    want = MR_MIN(want, avail - off);
    if (want == 0U) {
        return 0;
    }

    /* Get read index */
    index = ((mr_uint32_t)mr_atomic_load(&fifo->ridx) + off) & fifo->mask;
    want = MR_MIN(want, (fifo->mask + 1U) - index);

    /* Claim buffer */
    *buf = fifo->buf + (index * fifo->elemsz);
    return want;
}

/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_fifo.h>
#include <libc/mr_assert.h>
#include <libc/mr_string.h>
#include <libc/mr_malloc.h>

/* Fifo align macro function */
#define FIFO_ALIGN_POW2_UP(_size)                                              \
    (((_size) & ((_size)-1)) ? (1U << mr_fls32(_size)) : (_size))
#define FIFO_ALIGN_POW2_DOWN(_size)     (1U << (mr_fls32(_size) - 1))

mr_uint32_t mr_fifo_init(mr_fifo_t *fifo, void *buf, mr_uint32_t size,
                         mr_uint32_t elem_size) {
    /* Check parameter */
    MR_ASSERT((fifo != MR_NULL) && (buf != MR_NULL));
    MR_ASSERT((size >= 2) && (elem_size > 0) && (elem_size <= size));

    /* Align size(down to power of 2) */
    size = FIFO_ALIGN_POW2_DOWN(size / elem_size);

    /* Init fifo */
    fifo->buf = buf;
    fifo->mask = size - 1;
    fifo->in = fifo->out = 0;
    fifo->elem_size = elem_size;
    return size;
}

mr_uint32_t mr_fifo_alloc(mr_fifo_t *fifo, mr_uint32_t num,
                          mr_uint32_t elem_size) {
    mr_uint32_t size;
    void *buf;

    /* Check parameter */
    MR_ASSERT(fifo != MR_NULL);
    MR_ASSERT((num > 0) && (elem_size > 0));

    /* Align size(up to power of 2) */
    size = FIFO_ALIGN_POW2_UP(num * elem_size);

    /* Allocate buffer */
    buf = mr_realloc(fifo->buf, size);
    if (!buf) {
        /* Not enough memory */
        return 0;
    }

    /* Init fifo */
    return mr_fifo_init(fifo, buf, size, elem_size);
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
    mr_uint32_t in;

    /* Check parameter */
    MR_ASSERT(fifo != MR_NULL);
    MR_ASSERT(data != MR_NULL);

    /* Check if fifo is full */
    if (mr_fifo_is_full(fifo)) {
        return 0;
    }

    /* Get in index */
    in = fifo->in & fifo->mask;
    if (fifo->elem_size != 1) {
        in *= fifo->elem_size;
    }

    /* Push data */
    mr_memcpy((mr_uint8_t *)fifo->buf + in, data, fifo->elem_size);
    fifo->in += 1;
    return 1;
}

mr_uint32_t mr_fifo_pop(mr_fifo_t *fifo, void *data) {
    mr_uint32_t out;

    /* Check parameter */
    MR_ASSERT(fifo != MR_NULL);
    MR_ASSERT(data != MR_NULL);

    /* Check if fifo is empty */
    if (mr_fifo_is_empty(fifo)) {
        return 0;
    }

    /* Get out index */
    out = fifo->out & fifo->mask;
    if (fifo->elem_size != 1) {
        out *= fifo->elem_size;
    }

    /* Pop data */
    mr_memcpy(data, (mr_uint8_t *)fifo->buf + out, fifo->elem_size);
    fifo->out += 1;
    return 1;
}

mr_uint32_t mr_fifo_in(mr_fifo_t *fifo, const void *buf, mr_uint32_t num) {
    mr_uint32_t avail, num2, in, size, n;

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

    /* Get in index */
    in = fifo->in & fifo->mask;
    size = fifo->mask + 1;
    if (fifo->elem_size != 1) {
        in *= fifo->elem_size;
        size *= fifo->elem_size;
        num *= fifo->elem_size;
    }
    n = MR_MIN(num, size - in);

    /* Push buffer */
    mr_memcpy((mr_uint8_t *)fifo->buf + in, buf, n);
    mr_memcpy(fifo->buf, (mr_uint8_t *)buf + n, num - n);
    fifo->in += num2;
    return num2;
}

mr_uint32_t mr_fifo_in_overwrite(mr_fifo_t *fifo, const void *buf,
                                 mr_uint32_t num) {
    mr_uint32_t capacity, avail;

    /* Check parameter */
    MR_ASSERT(fifo != MR_NULL);
    MR_ASSERT((buf != MR_NULL) || (num == 0));

    /* Limit number */
    capacity = fifo->mask + 1;
    if (num > capacity) {
        buf = (mr_uint8_t *)buf + ((num - capacity) * fifo->elem_size);
        num = capacity;
    }

    /* Limit size */
    avail = mr_fifo_avail(fifo);
    if (num > avail) {
        mr_fifo_skip(fifo, num - avail);
    }

    /* Push buffer */
    return mr_fifo_in(fifo, buf, num);
}

mr_uint32_t mr_fifo_peek(mr_fifo_t *fifo, void *buf, mr_uint32_t num) {
    mr_uint32_t avail, num2, out, size, n;

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

    /* Get out index */
    out = fifo->out & fifo->mask;
    size = fifo->mask + 1;
    if (fifo->elem_size != 1) {
        out *= fifo->elem_size;
        size *= fifo->elem_size;
        num *= fifo->elem_size;
    }

    /* Peek buffer */
    n = MR_MIN(num, size - out);
    mr_memcpy(buf, (mr_uint8_t *)fifo->buf + out, n);
    mr_memcpy((mr_uint8_t *)buf + n, fifo->buf, num - n);
    return num2;
}

/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_kfifo.h>
#include <libc/mr_string.h>
#include <libc/mr_malloc.h>

/* Kfifo align macro function */
#define KFIFO_ALIGN_POW2_UP(_size)                                             \
    (((_size) & ((_size) - 1)) ? (1U << mr_fls32(_size)) : (_size))
#define KFIFO_ALIGN_POW2_DOWN(_size)                                           \
    (1U << (mr_fls32(_size) - 1))

mr_uint32_t mr_kfifo_init(mr_kfifo_t *kfifo, void *buf, mr_uint32_t size,
                          mr_uint32_t elem_size) {
    /* Check arguments */
    if ((!buf) || (size < 2) || (elem_size == 0) || (elem_size > size)) {
        return 0;
    }

    /* Align size(down to power of 2) */
    size = KFIFO_ALIGN_POW2_DOWN(size / elem_size);

    /* Init kfifo */
    kfifo->buf = buf;
    kfifo->mask = size - 1;
    kfifo->in = kfifo->out = 0;
    kfifo->elem_size = elem_size;
    return size;
}

mr_uint32_t mr_kfifo_alloc(mr_kfifo_t *kfifo, mr_uint32_t size,
                           mr_uint32_t elem_size) {
    void *buf;

    /* Check arguments */
    if ((size == 0) || (elem_size == 0) || (elem_size > size)) {
        return 0;
    }

    /* Align size(up to power of 2) */
    size = KFIFO_ALIGN_POW2_UP(size);

    /* Allocate buffer */
    if (kfifo->buf) {
        buf = mr_realloc(kfifo->buf, size);
    } else {
        buf = mr_malloc(size);
    }
    if (!buf) {
        /* Not enough memory */
        return 0;
    }

    /* Init kfifo */
    return mr_kfifo_init(kfifo, buf, size, elem_size);
}

void mr_kfifo_free(mr_kfifo_t *kfifo) {
    /* Check need to free */
    if (!kfifo->buf) {
        return;
    }

    /* Free buffer */
    mr_free(kfifo->buf);
    kfifo->buf = MR_NULL;
}

mr_uint32_t mr_kfifo_push(mr_kfifo_t *kfifo, const void *data) {
    mr_uint32_t in;

    /* Check if kfifo is full */
    if (mr_kfifo_is_full(kfifo)) {
        return 0;
    }

    /* Get in index */
    in = kfifo->in & kfifo->mask;
    if (kfifo->elem_size != 1) {
        in *= kfifo->elem_size;
    }

    /* Push data */
    mr_memcpy((mr_uint8_t *)kfifo->buf + in, data, kfifo->elem_size);
    kfifo->in += 1;
    return 1;
}

mr_uint32_t mr_kfifo_pop(mr_kfifo_t *kfifo, void *data) {
    mr_uint32_t out;

    /* Check if kfifo is empty */
    if (mr_kfifo_is_empty(kfifo)) {
        return 0;
    }

    /* Get out index */
    out = kfifo->out & kfifo->mask;
    if (kfifo->elem_size != 1) {
        out *= kfifo->elem_size;
    }

    /* Pop data */
    mr_memcpy(data, (mr_uint8_t *)kfifo->buf + out, kfifo->elem_size);
    kfifo->out += 1;
    return 1;
}

mr_uint32_t mr_kfifo_in(mr_kfifo_t *kfifo, const void *buf, mr_uint32_t num) {
    mr_uint32_t avail, num2, in, size, n;

    /* Limit number */
    avail = mr_kfifo_avail(kfifo);
    if (num > avail) {
        num = avail;
    }
    if (num == 0) {
        return 0;
    }
    num2 = num;

    /* Get in index */
    in = kfifo->in & kfifo->mask;
    size = kfifo->mask + 1;
    if (kfifo->elem_size != 1) {
        in *= kfifo->elem_size;
        size *= kfifo->elem_size;
        num *= kfifo->elem_size;
    }
    n = MR_MIN(num, size - in);

    /* Push buffer */
    mr_memcpy((mr_uint8_t *)kfifo->buf + in, buf, n);
    mr_memcpy(kfifo->buf, ((mr_uint8_t *)buf) + n, num - n);
    kfifo->in += num2;
    return num2;
}

mr_uint32_t mr_kfifo_in_overwrite(mr_kfifo_t *kfifo, const void *buf,
                                  mr_uint32_t num) {
    mr_uint32_t capacity, avail;

    /* Limit number */
    capacity = kfifo->mask + 1;
    if (num > capacity) {
        buf = ((mr_uint8_t *)buf) + ((num - capacity) * kfifo->elem_size);
        num = capacity;
    }

    /* Limit size */
    avail = mr_kfifo_avail(kfifo);
    if (num > avail) {
        mr_kfifo_skip(kfifo, num - avail);
    }

    /* Push buffer */
    return mr_kfifo_in(kfifo, buf, num);
}

mr_uint32_t mr_kfifo_peek(mr_kfifo_t *kfifo, void *buf, mr_uint32_t num) {
    mr_uint32_t avail, num2, out, size, n;

    /* Limit number */
    avail = mr_kfifo_len(kfifo);
    if (num > avail) {
        num = avail;
    }
    if (num == 0) {
        return 0;
    }
    num2 = num;

    /* Get out index */
    out = kfifo->out & kfifo->mask;
    size = kfifo->mask + 1;
    if (kfifo->elem_size != 1) {
        out *= kfifo->elem_size;
        size *= kfifo->elem_size;
        num *= kfifo->elem_size;
    }

    /* Peek buffer */
    n = MR_MIN(num, size - out);
    mr_memcpy(buf, (mr_uint8_t *)kfifo->buf + out, n);
    mr_memcpy((mr_uint8_t *)buf + n, kfifo->buf, num - n);
    return num2;
}

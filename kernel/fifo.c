/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/kernel/mr_service.h>
#include <mrX/kernel/mr_fifo.h>
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>

/* FIFO capacity max definition */
#define FIFO_CAPACITY_MAX               (((mr_uint32_t)MR_INT32_MAX / 2U) + 1U)

mr_uint32_t mr_fifo_init(mr_fifo_t *fifo, void *buf, mr_uint32_t size,
                         mr_uint32_t elemsz) {
    mr_uint32_t capacity;

    /* Check parameter */
    if ((!fifo) || (buf == MR_NULL) || (elemsz == 0) || (size < elemsz)) {
        return 0;
    }

    /* Get capacity */
    capacity = size / elemsz;
    if (capacity > FIFO_CAPACITY_MAX) {
        return 0;
    }

    /* Init fifo */
    mr_atomic_init(&fifo->ridx, 0);
    mr_atomic_init(&fifo->widx, 0);
    fifo->capacity = capacity;
    fifo->elemsz = elemsz;
    fifo->buf = buf;
    return capacity;
}

mr_uint32_t mr_fifo_alloc(mr_fifo_t *fifo, mr_uint32_t num,
                          mr_uint32_t elemsz) {
    mr_uint32_t size;
    void *buf;

    /* Check parameter */
    if ((fifo == MR_NULL) || (num > FIFO_CAPACITY_MAX) || (elemsz == 0)) {
        return 0;
    }
    if (num > (MR_UINT32_MAX / elemsz)) {
        return 0;
    }

    /* Get buffer size */
    size = num * elemsz;

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

MR_INLINE mr_uint32_t fifo_cursor_to_index(mr_fifo_t *fifo,
                                           mr_uint32_t cursor) {
    /* Fold cursor into buffer range */
    if (cursor >= fifo->capacity) {
        cursor -= fifo->capacity;
    }
    return (mr_uint32_t)cursor;
}

MR_INLINE mr_uint32_t fifo_cursor_advance(mr_fifo_t *fifo, mr_uint32_t cursor,
                                          mr_uint32_t num) {
    mr_uint32_t period;

    /* Get cursor period */
    period = fifo->capacity * 2U;

    /* Advance and wrap cursor */
    cursor += num;
    if (cursor >= period) {
        cursor -= period;
    }
    return cursor;
}

MR_INLINE void fifo_cursor_add(mr_fifo_t *fifo, volatile mr_atomic_t *cursor,
                               mr_uint32_t num) {
    mr_atomic_t curr, next;

    /* Get current cursor */
    curr = mr_atomic_load(cursor);

    /* Advance cursor atomically */
    do {
        next = (mr_atomic_t)fifo_cursor_advance(fifo, (mr_uint32_t)curr, num);
    } while (!mr_atomic_compare_exchange(cursor, &curr, next));
}

mr_uint32_t mr_fifo_push(mr_fifo_t *fifo, const void *data) {
    mr_uint32_t cursor, index;

    /* Check whether fifo is not initialized */
    if (!fifo->buf) {
        return 0;
    }

    /* Check whether fifo is full */
    if (mr_fifo_is_full(fifo)) {
        return 0;
    }

    /* Get write index */
    cursor = (mr_uint32_t)mr_atomic_load(&fifo->widx);
    index = fifo_cursor_to_index(fifo, cursor);
    if (fifo->elemsz != 1U) {
        index *= fifo->elemsz;
    }

    /* Push data */
    mr_memcpy(fifo->buf + index, data, fifo->elemsz);
    fifo_cursor_add(fifo, &fifo->widx, 1U);
    return 1;
}

mr_uint32_t mr_fifo_pop(mr_fifo_t *fifo, void *data) {
    mr_uint32_t cursor, index;

    /* Check whether fifo is not initialized */
    if (!fifo->buf) {
        return 0;
    }

    /* Check whether fifo is empty */
    if (mr_fifo_is_empty(fifo)) {
        return 0;
    }

    /* Get read index */
    cursor = (mr_uint32_t)mr_atomic_load(&fifo->ridx);
    index = fifo_cursor_to_index(fifo, cursor);
    if (fifo->elemsz != 1U) {
        index *= fifo->elemsz;
    }

    /* Pop data */
    mr_memcpy(data, fifo->buf + index, fifo->elemsz);
    fifo_cursor_add(fifo, &fifo->ridx, 1U);
    return 1;
}

mr_uint32_t mr_fifo_write(mr_fifo_t *fifo, const void *buf, mr_uint32_t num) {
    mr_uint32_t avail, num2, cursor, index, size, n;

    /* Check whether fifo is not initialized */
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
    cursor = (mr_uint32_t)mr_atomic_load(&fifo->widx);
    index = fifo_cursor_to_index(fifo, cursor);
    size = fifo->capacity;
    if (fifo->elemsz != 1U) {
        index *= fifo->elemsz;
        size *= fifo->elemsz;
        num *= fifo->elemsz;
    }

    /* Push buffer */
    n = MR_MIN(num, size - index);
    mr_memcpy(fifo->buf + index, buf, n);
    mr_memcpy(fifo->buf, (mr_uint8_t *)buf + n, num - n);
    fifo_cursor_add(fifo, &fifo->widx, num2);
    return num2;
}

mr_uint32_t mr_fifo_overwrite(mr_fifo_t *fifo, const void *buf,
                              mr_uint32_t num) {
    mr_uint32_t cap, avail;

    /* Check whether fifo is not initialized */
    if (!fifo->buf) {
        return 0;
    }

    /* Limit number */
    cap = fifo->capacity;
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
    mr_uint32_t avail, num2, cursor, index, size, n;

    /* Check whether fifo is not initialized */
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
    cursor = (mr_uint32_t)mr_atomic_load(&fifo->ridx);
    index = fifo_cursor_to_index(fifo, cursor);
    size = fifo->capacity;
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

mr_uint32_t mr_fifo_read(mr_fifo_t *fifo, void *buf, mr_uint32_t num) {
    mr_atomic_t curr, next;
    mr_uint32_t n;

    /* Check whether fifo is not initialized */
    if (!fifo->buf) {
        return 0;
    }

    do {
        /* Get current read cursor */
        curr = mr_atomic_load(&fifo->ridx);

        /* Peek buffer */
        n = mr_fifo_peek(fifo, buf, num);
        if (n == 0U) {
            return 0;
        }

        /* Get next read cursor */
        next = (mr_atomic_t)fifo_cursor_advance(fifo, (mr_uint32_t)curr, n);

        /* Try to advance read cursor */
    } while (!mr_atomic_compare_exchange(&fifo->ridx, &curr, next));
    return n;
}

mr_uint32_t mr_fifo_skip(mr_fifo_t *fifo, mr_uint32_t num) {
    /* Skip buffer */
    num = MR_MIN(num, mr_fifo_len(fifo));
    fifo_cursor_add(fifo, &fifo->ridx, num);
    return num;
}

mr_uint32_t mr_fifo_write_claim(mr_fifo_t *fifo, mr_uint32_t off, void **buf,
                                mr_uint32_t want) {
    mr_uint32_t avail, cursor, index;

    /* Check whether fifo is not initialized */
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
    cursor = (mr_uint32_t)mr_atomic_load(&fifo->widx);
    cursor = fifo_cursor_advance(fifo, cursor, off);
    index = fifo_cursor_to_index(fifo, cursor);
    want = MR_MIN(want, fifo->capacity - index);

    /* Claim buffer */
    *buf = fifo->buf + index * fifo->elemsz;
    return want;
}

mr_uint32_t mr_fifo_write_finish(mr_fifo_t *fifo, mr_uint32_t num) {
    /* Check whether fifo is not initialized */
    if (!fifo->buf) {
        return 0;
    }

    /* Finish buffer */
    num = MR_MIN(num, mr_fifo_avail(fifo));
    fifo_cursor_add(fifo, &fifo->widx, num);
    return num;
}

mr_uint32_t mr_fifo_overwrite_claim(mr_fifo_t *fifo, mr_uint32_t off,
                                    void **buf, mr_uint32_t want) {
    mr_uint32_t cap, cursor, index;

    /* Check whether fifo is not initialized */
    if (!fifo->buf) {
        return 0;
    }

    /* Limit wants */
    cap = fifo->capacity;
    if (off >= cap) {
        return 0;
    }
    want = MR_MIN(want, cap - off);
    if (want == 0U) {
        return 0;
    }

    /* Get write index */
    cursor = (mr_uint32_t)mr_atomic_load(&fifo->widx);
    cursor = fifo_cursor_advance(fifo, cursor, off);
    index = fifo_cursor_to_index(fifo, cursor);
    want = MR_MIN(want, cap - index);

    /* Claim buffer */
    *buf = fifo->buf + (index * fifo->elemsz);
    return want;
}

mr_uint32_t mr_fifo_overwrite_finish(mr_fifo_t *fifo, mr_uint32_t num) {
    mr_uint32_t avail;

    /* Check whether fifo is not initialized */
    if (!fifo->buf) {
        return 0;
    }

    /* Finish buffer */
    num = MR_MIN(num, mr_fifo_size(fifo));
    avail = mr_fifo_avail(fifo);
    if (avail < num) {
        fifo_cursor_add(fifo, &fifo->ridx, num - avail);
    }
    fifo_cursor_add(fifo, &fifo->widx, num);
    return num;
}

mr_uint32_t mr_fifo_read_claim(mr_fifo_t *fifo, mr_uint32_t off, void **buf,
                               mr_uint32_t want) {
    mr_uint32_t avail, cursor, index;

    /* Check whether fifo is not initialized */
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
    cursor = (mr_uint32_t)mr_atomic_load(&fifo->ridx);
    cursor = fifo_cursor_advance(fifo, cursor, off);
    index = fifo_cursor_to_index(fifo, cursor);
    want = MR_MIN(want, fifo->capacity - index);

    /* Claim buffer */
    *buf = fifo->buf + (index * fifo->elemsz);
    return want;
}

void mr_fifo_read_finish(mr_fifo_t *fifo, mr_uint32_t num) {
    /* Check whether fifo is not initialized */
    if (!fifo->buf) {
        return;
    }

    /* Finish buffer */
    num = MR_MIN(num, mr_fifo_len(fifo));
    fifo_cursor_add(fifo, &fifo->ridx, num);
}

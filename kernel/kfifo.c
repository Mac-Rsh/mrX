/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_kfifo.h>
#include <libc/mr_string.h>

MR_INLINE mr_uint32_t kfifo_align_down_pow2(mr_uint32_t val) {
    if (val == 0) {
        return 0;
    }
    val |= val >> 1;
    val |= val >> 2;
    val |= val >> 4;
    val |= val >> 8;
    val |= val >> 16;
    return val - (val >> 1);
}

void mr_kfifo_init(mr_kfifo_t *kfifo, void *buf, mr_uint32_t size) {
    kfifo->buf = buf;
    kfifo->in = 0;
    kfifo->out = 0;

    /* Align 2^n(down) */
    kfifo->size = kfifo_align_down_pow2(size);
}

mr_uint32_t mr_kfifo_in(mr_kfifo_t *kfifo, const void *buf, mr_uint32_t size) {
    mr_uint32_t s;

    /* Get available size */
    size = MR_MIN(size, mr_kfifo_avail(kfifo));
    if (size == 0) {
        return 0;
    }

    /* In end of kfifo */
    s = MR_MIN(size, kfifo->size - (kfifo->in & (kfifo->size - 1)));
    mr_memcpy(kfifo->buf + (kfifo->in & (kfifo->size - 1)), buf, s);

    /* In start of kfifo */
    mr_memcpy(kfifo->buf, (mr_uint8_t *)buf + s, size - s);

    /* Update in index */
    kfifo->in += size;
    return size;
}

mr_uint32_t mr_kfifo_out(mr_kfifo_t *kfifo, void *buf, mr_uint32_t size) {
    /* Peek kfifo */
    size = mr_kfifo_peek(kfifo, buf, size);

    /* Update out index */
    kfifo->out += size;
    return size;
}

mr_uint32_t mr_kfifo_in_overwrite(mr_kfifo_t *kfifo, const void *buf,
                                  mr_uint32_t size) {
    mr_uint32_t s;

    /* Skip the useless data */
    s = MR_MIN(size, mr_kfifo_size(kfifo));
    if (s > size) {
        buf = (mr_uint8_t *)buf + (size - s);
        size = s;
    }

    /* Give space to kfifo */
    s = mr_kfifo_avail(kfifo);
    if (s < size) {
        mr_kfifo_skip(kfifo, size - s);
    }

    /* In kfifo */
    return mr_kfifo_in(kfifo, buf, size);
}

mr_uint32_t mr_kfifo_peek(mr_kfifo_t *kfifo, void *buf, mr_uint32_t size) {
    mr_uint32_t s;

    /* Get available size */
    size = MR_MIN(size, mr_kfifo_len(kfifo));
    if (size == 0) {
        return 0;
    }

    /* Out end of kfifo */
    s = MR_MIN(size, kfifo->size - (kfifo->out & (kfifo->size - 1)));
    mr_memcpy(buf, kfifo->buf + (kfifo->out & (kfifo->size - 1)), s);

    /* Out start of kfifo */
    mr_memcpy((mr_uint8_t *)buf + s, kfifo->buf, size - s);
    return size;
}

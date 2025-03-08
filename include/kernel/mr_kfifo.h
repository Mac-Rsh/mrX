/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_KFIFO_H__
#define __MR_KFIFO_H__

#include <kernel/mr_kservice.h>
#include <libc/mr_compiler.h>
#include <libc/mr_types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Kfifo
 * @{
 */

/* Kfifo type */
typedef struct mr_kfifo {
    mr_uint8_t *buf;
    mr_uint32_t size;
    mr_uint32_t in;
    mr_uint32_t out;
} mr_kfifo_t;

/**
 * @brief This function initializes a kfifo.
 *
 * @param kfifo The kfifo to initialize.
 * @param buf The buffer of the kfifo.
 * @param size The size of the kfifo.
 */
void mr_kfifo_init(mr_kfifo_t *kfifo, void *buf, mr_uint32_t size);

/**
 * @brief This function puts data into a kfifo.
 *
 * @param kfifo The kfifo to put data into.
 * @param buf The data to put into the kfifo.
 * @param size The size of the data.
 * @return The number of bytes put into the kfifo.
 */
mr_uint32_t mr_kfifo_in(mr_kfifo_t *kfifo, const void *buf, mr_uint32_t size);

/**
 * @brief This function takes data out of a kfifo.
 *
 * @param kfifo The kfifo to take data out of.
 * @param buf The data to take out of the kfifo.
 * @param size The size of the data.
 * @return The number of bytes taken out of the kfifo.
 */
mr_uint32_t mr_kfifo_out(mr_kfifo_t *kfifo, void *buf, mr_uint32_t size);

/**
 * @brief This function puts data into a kfifo, overwriting old data.
 *
 * @param kfifo The kfifo to put data into.
 * @param buf The data to put into the kfifo.
 * @param size The size of the data.
 * @return The number of bytes put into the kfifo.
 */
mr_uint32_t mr_kfifo_in_overwrite(mr_kfifo_t *kfifo, const void *buf,
                                  mr_uint32_t size);

/**
 * @brief This function peeks data out of a kfifo.
 *
 * @param kfifo The kfifo to peek data out of.
 * @param buf The data to peek out of the kfifo.
 * @param size The size of the data.
 * @return The number of bytes peeked out of the kfifo.
 */
mr_uint32_t mr_kfifo_peek(mr_kfifo_t *kfifo, void *buf, mr_uint32_t size);

/**
 * @brief This function gets the size of a kfifo.
 *
 * @param kfifo The kfifo to get the size of.
 * @return The size of the kfifo.
 */
MR_INLINE mr_uint32_t mr_kfifo_size(mr_kfifo_t *kfifo) {
    return kfifo->size;
}

/**
 * @brief This function gets the number of available space bytes in a kfifo.
 *
 * @param kfifo The kfifo to get the number of available space bytes in.
 * @return The number of available space bytes in the kfifo.
 */
MR_INLINE mr_uint32_t mr_kfifo_avail(mr_kfifo_t *kfifo) {
    return kfifo->size - (kfifo->in - kfifo->out);
}

/**
 * @brief This function gets the number of data bytes in a kfifo.
 *
 * @param kfifo The kfifo to get the number of data bytes in.
 * @return The number of data bytes in the kfifo.
 */
MR_INLINE mr_uint32_t mr_kfifo_len(mr_kfifo_t *kfifo) {
    return kfifo->in - kfifo->out;
}

/**
 * @brief This function resets a kfifo.
 *
 * @param kfifo The kfifo to reset.
 */
MR_INLINE void mr_kfifo_reset(mr_kfifo_t *kfifo) {
    kfifo->in = kfifo->out = 0;
}

/**
 * @brief This function skips data in a kfifo.
 *
 * @param kfifo The kfifo to skip data in.
 * @param size The size of the data to skip.
 * @return The number of bytes skipped.
 */
MR_INLINE mr_uint32_t mr_kfifo_skip(mr_kfifo_t *kfifo, mr_uint32_t size) {
    kfifo->out += MR_MIN(size, mr_kfifo_len(kfifo));
    return size;
}

/**
 * @brief This function checks if a kfifo is empty.
 *
 * @param kfifo The kfifo to check.
 * @return MR_TRUE if the kfifo is empty, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_kfifo_is_empty(mr_kfifo_t *kfifo) {
    return (kfifo->in == kfifo->out) ? MR_TRUE : MR_FALSE;
}

/**
 * @brief This function checks if a kfifo is full.
 *
 * @param kfifo The kfifo to check.
 * @return MR_TRUE if the kfifo is full, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_kfifo_is_full(mr_kfifo_t *kfifo) {
    return (kfifo->in == (kfifo->out + kfifo->size)) ? MR_TRUE : MR_FALSE;
}

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_KFIFO_H__ */

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
    mr_uint32_t mask;
    mr_uint32_t elem_size;
    volatile mr_uint32_t in;
    volatile mr_uint32_t out;
} mr_kfifo_t;

/**
 * @brief This function initializes a kfifo.
 *
 * @param kfifo The kfifo.
 * @param buf The buffer.
 * @param size The buffer size(bytes).
 * @param elem_size The element size(bytes).
 * @return The element capacity(number).
 *
 * @note The buffer will be aligned down to a power of 2.
 */
mr_uint32_t mr_kfifo_init(mr_kfifo_t *kfifo, void *buf, mr_uint32_t size,
                          mr_uint32_t elem_size);

/**
 * @brief This function allocates a kfifo.
 *
 * @param kfifo The kfifo.
 * @param size The buffer size(bytes).
 * @param elem_size The element size(bytes).
 * @return The element capacity(number).
 *
 * @note The buffer will be aligned up to a power of 2.
 */
mr_uint32_t mr_kfifo_alloc(mr_kfifo_t *kfifo, mr_uint32_t size,
                           mr_uint32_t elem_size);

/**
 * @brief This function frees a kfifo.
 *
 * @param kfifo The kfifo.
 */
void mr_kfifo_free(mr_kfifo_t *kfifo);

/**
 * @brief This function pushes data to a kfifo.
 *
 * @param kfifo The kfifo.
 * @param data The data.
 * @return The actual pushed element number.
 */
mr_uint32_t mr_kfifo_push(mr_kfifo_t *kfifo, const void *data);

/**
 * @brief This function pops data from a kfifo.
 *
 * @param kfifo The kfifo.
 * @param data The data.
 * @return The actual popped element number.
 */
mr_uint32_t mr_kfifo_pop(mr_kfifo_t *kfifo, void *data);

/**
 * @brief This function puts data to a kfifo.
 *
 * @param kfifo The kfifo.
 * @param buf The buffer.
 * @param num The buffer number.
 * @return The actual pushed element number.
 */
mr_uint32_t mr_kfifo_in(mr_kfifo_t *kfifo, const void *buf, mr_uint32_t num);

/**
 * @brief This function puts data to a kfifo with overwrite.
 *
 * @param kfifo The kfifo.
 * @param buf The buffer.
 * @param num The buffer number.
 * @return The actual pushed element number.
 */
mr_uint32_t mr_kfifo_in_overwrite(mr_kfifo_t *kfifo, const void *buf,
                                  mr_uint32_t num);

/**
 * @brief This function peeks data from a kfifo.
 *
 * @param kfifo The kfifo.
 * @param buf The buffer.
 * @param num The buffer number.
 * @return The actual peeked element number.
 */
mr_uint32_t mr_kfifo_peek(mr_kfifo_t *kfifo, void *buf, mr_uint32_t num);

/**
 * @brief This function gets data from a kfifo.
 *
 * @param kfifo The kfifo.
 * @param buf The buffer.
 * @param num The buffer number.
 * @return The actual popped element number.
 */
MR_INLINE mr_uint32_t mr_kfifo_out(mr_kfifo_t *kfifo, void *buf,
                                   mr_uint32_t num) {
    /* Pop buffer */
    num = mr_kfifo_peek(kfifo, buf, num);
    kfifo->out += num;
    return num;
}

/**
 * @brief This function gets the element number in a kfifo.
 *
 * @param kfifo The kfifo.
 * @return The element number.
 */
MR_INLINE mr_uint32_t mr_kfifo_len(mr_kfifo_t *kfifo) {
    /* Get kfifo element number */
    return kfifo->in - kfifo->out;
}

/**
 * @brief This function skips elements in a kfifo.
 *
 * @param kfifo The kfifo.
 * @param num The element number.
 * @return The actual skipped element number.
 */
MR_INLINE mr_uint32_t mr_kfifo_skip(mr_kfifo_t *kfifo, mr_uint32_t num) {
    /* Skip buffer */
    kfifo->out += MR_MIN(num, mr_kfifo_len(kfifo));
    return num;
}

/**
 * @brief This function gets the available space in a kfifo.
 *
 * @param kfifo The kfifo.
 * @return The available element number.
 */
MR_INLINE mr_uint32_t mr_kfifo_avail(mr_kfifo_t *kfifo) {
    /* Get kfifo available space */
    return (kfifo->mask + 1) - mr_kfifo_len(kfifo);
}

/**
 * @brief This function resets a kfifo.
 *
 * @param kfifo The kfifo.
 */
MR_INLINE void mr_kfifo_reset(mr_kfifo_t *kfifo) {
    /* Reset kfifo */
    kfifo->in = kfifo->out = 0;
}

/**
 * @brief This function checks if a kfifo is empty.
 *
 * @param kfifo The kfifo.
 * @return MR_TRUE on empty, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_kfifo_is_empty(mr_kfifo_t *kfifo) {
    /* Check if kfifo is empty */
    return kfifo->in == kfifo->out;
}

/**
 * @brief This function checks if a kfifo is full.
 *
 * @param kfifo The kfifo.
 * @return MR_TRUE on full, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_kfifo_is_full(mr_kfifo_t *kfifo) {
    /* Check if kfifo is full */
    return mr_kfifo_len(kfifo) == (kfifo->mask + 1);
}

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_KFIFO_H__ */

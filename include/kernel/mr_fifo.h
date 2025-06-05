/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_KNL_FIFO_H__
#define __MR_KNL_FIFO_H__

#include <kernel/mr_service.h>
#include <libc/mr_compiler.h>
#include <libc/mr_types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Fifo
 * @{
 */

/* Fifo type */
typedef struct mr_fifo {
    mr_uint8_t *buf;
    mr_uint32_t mask;
    mr_uint32_t elem_size;
    volatile mr_uint32_t in;
    volatile mr_uint32_t out;
} mr_fifo_t;

/**
 * @brief This function initializes a fifo.
 *
 * @param fifo The fifo.
 * @param buf The buffer.
 * @param size The buffer size(bytes).
 * @param elem_size The element size(bytes).
 * @return The element capacity(number).
 *
 * @note The buffer will be aligned down to a power of 2.
 */
mr_uint32_t mr_fifo_init(mr_fifo_t *fifo, void *buf, mr_uint32_t size,
                         mr_uint32_t elem_size);

/**
 * @brief This function allocates a fifo.
 *
 * @param fifo The fifo.
 * @param size The buffer size(bytes).
 * @param elem_size The element size(bytes).
 * @return The element capacity(number).
 *
 * @note The buffer will be aligned up to a power of 2.
 */
mr_uint32_t mr_fifo_alloc(mr_fifo_t *fifo, mr_uint32_t size,
                          mr_uint32_t elem_size);

/**
 * @brief This function frees a fifo.
 *
 * @param fifo The fifo.
 */
void mr_fifo_free(mr_fifo_t *fifo);

/**
 * @brief This function pushes data to a fifo.
 *
 * @param fifo The fifo.
 * @param data The data.
 * @return The actual pushed element number.
 */
mr_uint32_t mr_fifo_push(mr_fifo_t *fifo, const void *data);

/**
 * @brief This function pops data from a fifo.
 *
 * @param fifo The fifo.
 * @param data The data.
 * @return The actual popped element number.
 */
mr_uint32_t mr_fifo_pop(mr_fifo_t *fifo, void *data);

/**
 * @brief This function puts data to a fifo.
 *
 * @param fifo The fifo.
 * @param buf The buffer.
 * @param num The buffer number.
 * @return The actual pushed element number.
 */
mr_uint32_t mr_fifo_in(mr_fifo_t *fifo, const void *buf, mr_uint32_t num);

/**
 * @brief This function puts data to a fifo with overwrite.
 *
 * @param fifo The fifo.
 * @param buf The buffer.
 * @param num The buffer number.
 * @return The actual pushed element number.
 */
mr_uint32_t mr_fifo_in_overwrite(mr_fifo_t *fifo, const void *buf,
                                 mr_uint32_t num);

/**
 * @brief This function peeks data from a fifo.
 *
 * @param fifo The fifo.
 * @param buf The buffer.
 * @param num The buffer number.
 * @return The actual peeked element number.
 */
mr_uint32_t mr_fifo_peek(mr_fifo_t *fifo, void *buf, mr_uint32_t num);

/**
 * @brief This function gets data from a fifo.
 *
 * @param fifo The fifo.
 * @param buf The buffer.
 * @param num The buffer number.
 * @return The actual popped element number.
 */
MR_INLINE mr_uint32_t mr_fifo_out(mr_fifo_t *fifo, void *buf, mr_uint32_t num) {
    /* Pop buffer */
    num = mr_fifo_peek(fifo, buf, num);
    fifo->out += num;
    return num;
}

/**
 * @brief This function gets the element number in a fifo.
 *
 * @param fifo The fifo.
 * @return The element number.
 */
MR_INLINE mr_uint32_t mr_fifo_len(mr_fifo_t *fifo) {
    /* Get fifo element number */
    return fifo->in - fifo->out;
}

/**
 * @brief This function skips elements in a fifo.
 *
 * @param fifo The fifo.
 * @param num The element number.
 * @return The actual skipped element number.
 */
MR_INLINE mr_uint32_t mr_fifo_skip(mr_fifo_t *fifo, mr_uint32_t num) {
    /* Skip buffer */
    fifo->out += MR_MIN(num, mr_fifo_len(fifo));
    return num;
}

/**
 * @brief This function gets the available space in a fifo.
 *
 * @param fifo The fifo.
 * @return The available element number.
 */
MR_INLINE mr_uint32_t mr_fifo_avail(mr_fifo_t *fifo) {
    /* Get fifo available space */
    return (fifo->mask + 1) - mr_fifo_len(fifo);
}

/**
 * @brief This function resets a fifo.
 *
 * @param fifo The fifo.
 */
MR_INLINE void mr_fifo_reset(mr_fifo_t *fifo) {
    /* Reset fifo */
    fifo->in = fifo->out = 0;
}

/**
 * @brief This function checks if a fifo is empty.
 *
 * @param fifo The fifo.
 * @return MR_TRUE on empty, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_fifo_is_empty(mr_fifo_t *fifo) {
    /* Check if fifo is empty */
    return fifo->in == fifo->out;
}

/**
 * @brief This function checks if a fifo is full.
 *
 * @param fifo The fifo.
 * @return MR_TRUE on full, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_fifo_is_full(mr_fifo_t *fifo) {
    /* Check if fifo is full */
    return mr_fifo_len(fifo) == (fifo->mask + 1);
}

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_KNL_FIFO_H__ */

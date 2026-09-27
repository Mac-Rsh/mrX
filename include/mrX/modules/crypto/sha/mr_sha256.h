/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-20        MacRsh        First version
 */

#ifndef __MR_MODULE_CRYPTO_SHA256_H__
#define __MR_MODULE_CRYPTO_SHA256_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_MODULE_CRYPTO_SHA256)
#include <mrX/libc/mr_types.h>
#endif /* defined(MR_USE_MODULE_CRYPTO_SHA256) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Crypto-SHA-256
 * @{
 */

#if defined(MR_USE_MODULE_CRYPTO_SHA256)
/* SHA-256 size definition */
#define MR_SHA256_BLOCK_SIZE            (64U)
#define MR_SHA256_DIGEST_SIZE           (32U)
#define MR_SHA256_STATE_WORDS           (8U)

/* SHA-256 type */
typedef struct mr_sha256 {
    mr_uint32_t state[MR_SHA256_STATE_WORDS];
    mr_uint8_t buffer[MR_SHA256_BLOCK_SIZE];
    mr_uint32_t buffer_size;
    mr_uint64_t total_size;
} mr_sha256_t;

/**
 * @brief This function initializes a SHA-256 object.
 *
 * @param sha256 The SHA-256 object.
 */
void mr_sha256_init(mr_sha256_t *sha256);

/**
 * @brief This function updates a SHA-256 object with data.
 *
 * @param sha256 The SHA-256 object.
 * @param data The input data.
 * @param size The input data size.
 *
 * @note Data may be MR_NULL when size is 0.
 */
void mr_sha256_update(mr_sha256_t *sha256, const void *data, mr_size_t size);

/**
 * @brief This function finalizes a SHA-256 object.
 *
 * @param sha256 The SHA-256 object.
 * @param digest The 32-byte digest output.
 *
 * @note The SHA-256 object is cleared before return.
 */
void mr_sha256_final(mr_sha256_t *sha256,
                     mr_uint8_t digest[MR_SHA256_DIGEST_SIZE]);

/**
 * @brief This function calculates SHA-256 for a complete buffer.
 *
 * @param data The input data.
 * @param size The input data size.
 * @param digest The 32-byte digest output.
 *
 * @note Data may be MR_NULL when size is 0.
 */
void mr_sha256(const void *data, mr_size_t size,
               mr_uint8_t digest[MR_SHA256_DIGEST_SIZE]);
#endif /* defined(MR_USE_MODULE_CRYPTO_SHA256) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_MODULE_CRYPTO_SHA256_H__ */

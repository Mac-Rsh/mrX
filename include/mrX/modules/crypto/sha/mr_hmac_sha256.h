/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-22        MacRsh        First version
 */

#ifndef __MR_MODULE_CRYPTO_HMAC_SHA256_H__
#define __MR_MODULE_CRYPTO_HMAC_SHA256_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_MODULE_CRYPTO_HMAC_SHA256)
#include <mrX/modules/crypto/sha/mr_sha256.h>
#endif /* defined(MR_USE_MODULE_CRYPTO_HMAC_SHA256) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Crypto-HMAC-SHA-256
 * @{
 */

#if defined(MR_USE_MODULE_CRYPTO_HMAC_SHA256)
/* HMAC-SHA-256 type */
typedef struct mr_hmac_sha256 {
    mr_sha256_t inner;
    mr_sha256_t outer;
} mr_hmac_sha256_t;

/**
 * @brief This function initializes an HMAC-SHA-256 object.
 *
 * @param hmac The HMAC-SHA-256 object.
 * @param key The authentication key.
 * @param key_size The authentication key size.
 *
 * @note Key may be MR_NULL when key_size is 0.
 */
void mr_hmac_sha256_init(mr_hmac_sha256_t *hmac, const void *key,
                         mr_size_t key_size);

/**
 * @brief This function updates an HMAC-SHA-256 object with data.
 *
 * @param hmac The HMAC-SHA-256 object.
 * @param data The input data.
 * @param size The input data size.
 *
 * @note Data may be MR_NULL when size is 0.
 */
void mr_hmac_sha256_update(mr_hmac_sha256_t *hmac, const void *data,
                           mr_size_t size);

/**
 * @brief This function finalizes an HMAC-SHA-256 object.
 *
 * @param hmac The HMAC-SHA-256 object.
 * @param auth The 32-byte authentication tag output.
 *
 * @note The HMAC-SHA-256 object is cleared before return.
 */
void mr_hmac_sha256_final(mr_hmac_sha256_t *hmac,
                          mr_uint8_t auth[MR_SHA256_DIGEST_SIZE]);

/**
 * @brief This function calculates HMAC-SHA-256 for a complete buffer.
 *
 * @param key The authentication key.
 * @param key_size The authentication key size.
 * @param data The input data.
 * @param size The input data size.
 * @param auth The 32-byte authentication tag output.
 *
 * @note Key and data may be MR_NULL when their corresponding size is 0.
 */
void mr_hmac_sha256(const void *key, mr_size_t key_size, const void *data,
                    mr_size_t size, mr_uint8_t auth[MR_SHA256_DIGEST_SIZE]);
#endif /* defined(MR_USE_MODULE_CRYPTO_HMAC_SHA256) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_MODULE_CRYPTO_HMAC_SHA256_H__ */

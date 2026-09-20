/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-20        MacRsh        First version
 */

#ifndef __MR_MODULE_CRYPTO_ECDSA_P256_H__
#define __MR_MODULE_CRYPTO_ECDSA_P256_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_MODULE_CRYPTO_ECDSA_P256)
#include <mrX/modules/crypto/ecc/mr_p256.h>
#include <mrX/modules/crypto/sha/mr_sha256.h>
#endif /* defined(MR_USE_MODULE_CRYPTO_ECDSA_P256) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Crypto-ECDSA-P256
 * @{
 */

#if defined(MR_USE_MODULE_CRYPTO_ECDSA_P256)
/* ECDSA P-256 signature type */
typedef struct mr_ecdsa_p256_signature {
    mr_uint8_t r[MR_P256_SCALAR_SIZE];
    mr_uint8_t s[MR_P256_SCALAR_SIZE];
} mr_ecdsa_p256_signature_t;

/**
 * @brief This function verifies a SHA-256 digest signature with ECDSA P-256.
 *
 * @param public_key The signer public key.
 * @param digest The 32-byte SHA-256 digest.
 * @param signature The raw fixed-width r/s signature.
 * @return MR_TRUE if the signature is valid, otherwise MR_FALSE.
 */
mr_bool_t mr_ecdsa_p256_verify(
    const mr_p256_public_key_t *public_key,
    const mr_uint8_t digest[MR_SHA256_DIGEST_SIZE],
    const mr_ecdsa_p256_signature_t *signature);
#endif /* defined(MR_USE_MODULE_CRYPTO_ECDSA_P256) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_MODULE_CRYPTO_ECDSA_P256_H__ */

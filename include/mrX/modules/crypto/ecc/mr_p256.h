/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-20        MacRsh        First version
 */

#ifndef __MR_MODULE_CRYPTO_P256_H__
#define __MR_MODULE_CRYPTO_P256_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_MODULE_CRYPTO_ECC_P256)
#include <mrX/libc/mr_errno.h>
#include <mrX/libc/mr_types.h>
#endif /* defined(MR_USE_MODULE_CRYPTO_ECC_P256) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Crypto-P-256
 * @{
 */

#if defined(MR_USE_MODULE_CRYPTO_ECC_P256)
/* P-256 size definition */
#define MR_P256_SCALAR_SIZE             (32U)
#define MR_P256_COORD_SIZE              (32U)

/* P-256 public key type */
typedef struct mr_p256_public_key {
    mr_uint8_t x[MR_P256_COORD_SIZE];
    mr_uint8_t y[MR_P256_COORD_SIZE];
} mr_p256_public_key_t;

/**
 * @brief This function validates a P-256 private scalar.
 *
 * @param private_key The 32-byte private scalar in big-endian form.
 * @return MR_TRUE if the private key is in [1, n-1], otherwise MR_FALSE.
 */
mr_bool_t mr_p256_private_key_valid(
    const mr_uint8_t private_key[MR_P256_SCALAR_SIZE]);

/**
 * @brief This function validates a P-256 public point.
 *
 * @param public_key The affine public point in big-endian coordinates.
 * @return MR_TRUE if the point is a valid P-256 point, otherwise MR_FALSE.
 */
mr_bool_t mr_p256_public_key_valid(const mr_p256_public_key_t *public_key);

/**
 * @brief This function derives a P-256 public key from a private key.
 *
 * @param private_key The 32-byte private scalar in big-endian form.
 * @param public_key The derived affine public point.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_p256_public_key(const mr_uint8_t private_key[MR_P256_SCALAR_SIZE],
                            mr_p256_public_key_t *public_key);
#endif /* defined(MR_USE_MODULE_CRYPTO_ECC_P256) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_MODULE_CRYPTO_P256_H__ */

/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-21        MacRsh        First version
 */

#ifndef __MR_MODULE_CRYPTO_P256_H__
#define __MR_MODULE_CRYPTO_P256_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_MODULE_CRYPTO_P256)
#include <mrX/libc/mr_types.h>
#endif /* defined(MR_USE_MODULE_CRYPTO_P256) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Crypto-P-256
 * @{
 */

#if defined(MR_USE_MODULE_CRYPTO_P256)
/* P-256 size definition */
#define MR_P256_SCALAR_SIZE             (32U)
#define MR_P256_COORD_SIZE              (32U)
#define MR_P256_VALUE_WORDS             (8U)
#define MR_P256_PRODUCT_WORDS           (16U)
#define MR_P256_EXT_WORDS               (9U)

/* P-256 value type */
typedef struct mr_p256_value {
    mr_uint32_t v[MR_P256_VALUE_WORDS];
} mr_p256_value_t;

/* P-256 point type */
typedef struct mr_p256_point {
    mr_p256_value_t x;
    mr_p256_value_t y;
    mr_bool_t infinity;
} mr_p256_point_t;

/* P-256 Jacobian point type */
typedef struct mr_p256_jacobian {
    mr_p256_value_t x;
    mr_p256_value_t y;
    mr_p256_value_t z;
} mr_p256_jacobian_t;

/* P-256 public key type */
typedef struct mr_p256_public_key {
    mr_uint8_t x[MR_P256_COORD_SIZE];
    mr_uint8_t y[MR_P256_COORD_SIZE];
} mr_p256_public_key_t;

/* P-256 modular arithmetic working type */
typedef struct mr_p256_mod {
    mr_uint32_t product[MR_P256_PRODUCT_WORDS];
    mr_uint32_t ext0[MR_P256_EXT_WORDS];
    mr_uint32_t ext1[MR_P256_EXT_WORDS];
} mr_p256_mod_t;

/* P-256 nested arithmetic working type */
typedef union mr_p256_nested {
    struct {
        mr_p256_jacobian_t result;
        mr_p256_value_t value[11];
    } point;
    struct {
        mr_p256_value_t value[3];
        mr_p256_value_t inverse[3];
    } affine;
    mr_p256_value_t valid[6];
} mr_p256_nested_t;

/* P-256 type */
typedef struct mr_p256 {
    mr_p256_jacobian_t jacobian[2];
    mr_p256_nested_t nested;
    mr_p256_mod_t mod;
} mr_p256_t;

/**
 * @brief This function loads a big-endian P-256 value.
 *
 * @param value The output P-256 value.
 * @param bytes The 32-byte big-endian input.
 */
void mr_p256_value_from_be(mr_p256_value_t *value,
                           const mr_uint8_t bytes[MR_P256_SCALAR_SIZE]);

/**
 * @brief This function validates a P-256 scalar.
 *
 * @param scalar The scalar to validate.
 * @return MR_TRUE if the scalar is in [1, n-1], otherwise MR_FALSE.
 */
mr_bool_t mr_p256_scalar_valid(const mr_p256_value_t *scalar);

/**
 * @brief This function reduces a P-256 scalar modulo the curve order.
 *
 * @param scalar The scalar to reduce.
 */
void mr_p256_scalar_reduce(mr_p256_value_t *scalar);

/**
 * @brief This function calculates a P-256 scalar modular inverse.
 *
 * @param p256 The P-256 object used as working memory.
 * @param out The inverse scalar.
 * @param scalar The input scalar.
 * @return MR_TRUE on success, otherwise MR_FALSE.
 */
mr_bool_t mr_p256_scalar_inv(mr_p256_t *p256, mr_p256_value_t *out,
                             const mr_p256_value_t *scalar);

/**
 * @brief This function multiplies two P-256 scalars modulo the curve order.
 *
 * @param p256 The P-256 object used as working memory.
 * @param out The result scalar.
 * @param a The first scalar.
 * @param b The second scalar.
 */
void mr_p256_scalar_mul(mr_p256_t *p256, mr_p256_value_t *out,
                        const mr_p256_value_t *a, const mr_p256_value_t *b);

/**
 * @brief This function converts a public key to a P-256 point.
 *
 * @param point The output point.
 * @param public_key The input public key.
 */
void mr_p256_point_from_public(mr_p256_point_t *point,
                               const mr_p256_public_key_t *public_key);

/**
 * @brief This function validates a P-256 point.
 *
 * @param p256 The P-256 object used as working memory.
 * @param point The point to validate.
 * @return MR_TRUE if the point is valid, otherwise MR_FALSE.
 */
mr_bool_t mr_p256_point_valid(mr_p256_t *p256, const mr_p256_point_t *point);

/**
 * @brief This function multiplies the P-256 generator by a scalar.
 *
 * @param p256 The P-256 object used as working memory.
 * @param out The output point.
 * @param scalar The scalar multiplier.
 *
 * @note The current implementation is not constant-time and should not be
 * used for secret scalar multiplication without a side-channel review.
 */
void mr_p256_point_mul_generator(mr_p256_t *p256, mr_p256_point_t *out,
                                 const mr_p256_value_t *scalar);

/**
 * @brief This function multiplies a P-256 point by a scalar.
 *
 * @param p256 The P-256 object used as working memory.
 * @param out The output point.
 * @param point The input point.
 * @param scalar The scalar multiplier.
 *
 * @note The current implementation is not constant-time and should not be
 * used for secret scalar multiplication without a side-channel review.
 */
void mr_p256_point_mul(mr_p256_t *p256, mr_p256_point_t *out,
                       const mr_p256_point_t *point,
                       const mr_p256_value_t *scalar);

/**
 * @brief This function adds two P-256 points.
 *
 * @param p256 The P-256 object used as working memory.
 * @param out The output point.
 * @param a The first point.
 * @param b The second point.
 */
void mr_p256_point_add(mr_p256_t *p256, mr_p256_point_t *out,
                       const mr_p256_point_t *a, const mr_p256_point_t *b);
#endif /* defined(MR_USE_MODULE_CRYPTO_P256) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_MODULE_CRYPTO_P256_H__ */

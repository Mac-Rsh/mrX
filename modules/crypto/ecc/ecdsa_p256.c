/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-21        MacRsh        First version
 */

#include <mrX/modules/crypto/ecc/mr_ecdsa_p256.h>
#if defined(MR_USE_MODULE_CRYPTO_ECDSA_P256)
#include <mrX/libc/mr_assert.h>

/* ECDSA P-256 memory zeroize */
static void ecdsa_p256_zeroize(void *data, mr_size_t size) {
    volatile mr_uint8_t *bytes;

    bytes = data;
    while (size-- != 0U) {
        *bytes++ = 0U;
    }
}

mr_bool_t mr_ecdsa_p256_verify(
    mr_ecdsa_p256_t *ecdsa, const mr_p256_public_key_t *public_key,
    const mr_uint8_t digest[MR_ECDSA_P256_DIGEST_SIZE],
    const mr_ecdsa_p256_signature_t *signature) {
    mr_p256_point_t *public_point, *p1, *p2, *sum;
    mr_p256_value_t *r, *s, *z, *w, *u1, *u2, *v;
    mr_bool_t valid;
    mr_uint32_t i;

    /* Check parameter */
    MR_ASSERT(ecdsa != MR_NULL);
    MR_ASSERT(public_key != MR_NULL);
    MR_ASSERT(digest != MR_NULL);
    MR_ASSERT(signature != MR_NULL);

    /* Get working memory */
    public_point = &ecdsa->point[0];
    p1 = &ecdsa->point[1];
    p2 = &ecdsa->point[2];
    sum = &ecdsa->point[3];
    r = &ecdsa->value[0];
    s = &ecdsa->value[1];
    z = &ecdsa->value[2];
    w = &ecdsa->value[3];
    u1 = &ecdsa->value[4];
    u2 = &ecdsa->value[5];
    v = &ecdsa->value[6];
    valid = MR_FALSE;

    /* Load signature and public key */
    mr_p256_value_from_be(r, signature->r);
    mr_p256_value_from_be(s, signature->s);
    mr_p256_point_from_public(public_point, public_key);
    if (!mr_p256_scalar_valid(r) || !mr_p256_scalar_valid(s)
        || !mr_p256_point_valid(&ecdsa->p256, public_point)) {
        goto _exit;
    }

    /* Compute u1*G + u2*Q */
    mr_p256_value_from_be(z, digest);
    mr_p256_scalar_reduce(z);
    if (!mr_p256_scalar_inv(&ecdsa->p256, w, s)) {
        goto _exit;
    }
    mr_p256_scalar_mul(&ecdsa->p256, u1, z, w);
    mr_p256_scalar_mul(&ecdsa->p256, u2, r, w);
    mr_p256_point_mul_generator(&ecdsa->p256, p1, u1);
    mr_p256_point_mul(&ecdsa->p256, p2, public_point, u2);
    mr_p256_point_add(&ecdsa->p256, sum, p1, p2);
    if (sum->infinity) {
        goto _exit;
    }

    /* Compare v with r */
    *v = sum->x;
    mr_p256_scalar_reduce(v);
    valid = MR_TRUE;
    for (i = 0U; i < MR_P256_VALUE_WORDS; i++) {
        if (v->v[i] != r->v[i]) {
            valid = MR_FALSE;
            break;
        }
    }
_exit:
    /* Clear working memory */
    ecdsa_p256_zeroize(ecdsa, sizeof(*ecdsa));
    return valid;
}
#endif /* defined(MR_USE_MODULE_CRYPTO_ECDSA_P256) */

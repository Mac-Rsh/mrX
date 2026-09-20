/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-20        MacRsh        First version
 */

#include <mrX/modules/crypto/ecc/mr_p256.h>
#if defined(MR_USE_MODULE_CRYPTO_ECC_P256)
#include <mrX/libc/mr_assert.h>
#include <mrX/libc/mr_string.h>
#if defined(MR_USE_MODULE_CRYPTO_ECDSA_P256)
#include <mrX/modules/crypto/ecc/mr_ecdsa_p256.h>
#endif /* defined(MR_USE_MODULE_CRYPTO_ECDSA_P256) */

/* P-256 size definition */
#define P256_WORDS                      (8U)
#define P256_PRODUCT_WORDS              (16U)
#define P256_EXT_WORDS                  (9U)
#define P256_WORD_SIZE                  (4U)
#define P256_WORD_BITS                  (32U)
#define P256_BIT_SIZE                   (MR_P256_SCALAR_SIZE * 8U)
#define P256_PRODUCT_BITS               (P256_BIT_SIZE * 2U)

/* P-256 integer type */
typedef struct p256_int {
    mr_uint32_t v[P256_WORDS];
} p256_int_t;

/* P-256 affine point type */
typedef struct p256_affine {
    p256_int_t x;
    p256_int_t y;
    mr_bool_t infinity;
} p256_affine_t;

/* P-256 memory zeroize */
static void p256_zeroize(void *data, mr_size_t size) {
    volatile mr_uint8_t *bytes = data;

    while (size-- != 0U) {
        *bytes++ = 0U;
    }
}

/* P-256 curve parameter definition */
static const p256_int_t __p256_p = {
    {
     0xffffffffU, 0xffffffffU,
     0xffffffffU, 0x00000000U,
     0x00000000U, 0x00000000U,
     0x00000001U, 0xffffffffU,
     }
};

static const p256_int_t __p256_b = {
    {
     0x27d2604bU, 0x3bce3c3eU,
     0xcc53b0f6U, 0x651d06b0U,
     0x769886bcU, 0xb3ebbd55U,
     0xaa3a93e7U, 0x5ac635d8U,
     }
};

static const p256_int_t __p256_n = {
    {
     0xfc632551U, 0xf3b9cac2U,
     0xa7179e84U, 0xbce6faadU,
     0xffffffffU, 0xffffffffU,
     0x00000000U, 0xffffffffU,
     }
};

static const p256_affine_t __p256_g = {
    .x = {{
        0xd898c296U,
        0xf4a13945U,
        0x2deb33a0U,
        0x77037d81U,
        0x63a440f2U,
        0xf8bce6e5U,
        0xe12c4247U,
        0x6b17d1f2U,
    }},
    .y = {{
        0x37bf51f5U,
        0xcbb64068U,
        0x6b315eceU,
        0x2bce3357U,
        0x7c0f9e16U,
        0x8ee7eb4aU,
        0xfe1a7f9bU,
        0x4fe342e2U,
    }},
    .infinity = MR_FALSE,
};

/* P-256 Jacobian point type */
typedef struct p256_jacobian {
    p256_int_t x;
    p256_int_t y;
    p256_int_t z;
} p256_jacobian_t;

static void p256_int_zero(p256_int_t *value) {
    mr_memset(value, 0, sizeof(*value));
}

static void p256_int_one(p256_int_t *value) {
    p256_int_zero(value);
    value->v[0] = 1U;
}

static mr_bool_t p256_int_is_zero(const p256_int_t *value) {
    mr_uint32_t acc = 0U;
    mr_uint32_t i;

    for (i = 0U; i < P256_WORDS; i++) {
        acc |= value->v[i];
    }
    return (acc == 0U) ? MR_TRUE : MR_FALSE;
}

static int p256_int_cmp(const p256_int_t *a, const p256_int_t *b) {
    mr_uint32_t i;

    for (i = P256_WORDS; i != 0U; i--) {
        if (a->v[i - 1U] > b->v[i - 1U]) {
            return 1;
        }
        if (a->v[i - 1U] < b->v[i - 1U]) {
            return -1;
        }
    }
    return 0;
}

static mr_uint32_t p256_sub_words(mr_uint32_t *out, const mr_uint32_t *a,
                                  const mr_uint32_t *b, mr_uint32_t words) {
    mr_uint64_t ai;
    mr_uint64_t bi;
    mr_uint64_t diff;
    mr_uint32_t borrow = 0U;
    mr_uint32_t i;

    for (i = 0U; i < words; i++) {
        ai = a[i];
        bi = (mr_uint64_t)b[i] + borrow;
        diff = ai - bi;
        out[i] = (mr_uint32_t)diff;
        borrow = (ai < bi) ? 1U : 0U;
    }
    return borrow;
}

static void p256_int_sub_raw(p256_int_t *out, const p256_int_t *a,
                             const p256_int_t *b) {
    (void)p256_sub_words(out->v, a->v, b->v, P256_WORDS);
}

static void p256_int_from_be(p256_int_t *out,
                             const mr_uint8_t bytes[MR_P256_SCALAR_SIZE]) {
    mr_uint32_t i;
    mr_uint32_t off;

    for (i = 0U; i < P256_WORDS; i++) {
        off = MR_P256_SCALAR_SIZE - ((i + 1U) * P256_WORD_SIZE);
        out->v[i] = ((mr_uint32_t)bytes[off] << 24U)
                  | ((mr_uint32_t)bytes[off + 1U] << 16U)
                  | ((mr_uint32_t)bytes[off + 2U] << 8U)
                  | (mr_uint32_t)bytes[off + 3U];
    }
}

static void p256_int_to_be(const p256_int_t *value,
                           mr_uint8_t bytes[MR_P256_SCALAR_SIZE]) {
    mr_uint32_t i;
    mr_uint32_t off;
    mr_uint32_t word;

    for (i = 0U; i < P256_WORDS; i++) {
        off = MR_P256_SCALAR_SIZE - ((i + 1U) * P256_WORD_SIZE);
        word = value->v[i];
        bytes[off] = (mr_uint8_t)(word >> 24U);
        bytes[off + 1U] = (mr_uint8_t)(word >> 16U);
        bytes[off + 2U] = (mr_uint8_t)(word >> 8U);
        bytes[off + 3U] = (mr_uint8_t)word;
    }
}

static int p256_ext_cmp(const mr_uint32_t a[P256_EXT_WORDS],
                        const mr_uint32_t b[P256_EXT_WORDS]) {
    mr_uint32_t i;

    for (i = P256_EXT_WORDS; i != 0U; i--) {
        if (a[i - 1U] > b[i - 1U]) {
            return 1;
        }
        if (a[i - 1U] < b[i - 1U]) {
            return -1;
        }
    }
    return 0;
}

static void p256_mod_add(p256_int_t *out, const p256_int_t *a,
                         const p256_int_t *b, const p256_int_t *mod) {
    mr_uint32_t sum[P256_EXT_WORDS] = {0};
    mr_uint32_t modulus[P256_EXT_WORDS] = {0};
    mr_uint64_t value;
    mr_uint64_t carry = 0U;
    mr_uint32_t i;

    for (i = 0U; i < P256_WORDS; i++) {
        value = (mr_uint64_t)a->v[i] + b->v[i] + carry;
        sum[i] = (mr_uint32_t)value;
        carry = value >> P256_WORD_BITS;
        modulus[i] = mod->v[i];
    }
    sum[P256_WORDS] = (mr_uint32_t)carry;
    if (p256_ext_cmp(sum, modulus) >= 0) {
        (void)p256_sub_words(sum, sum, modulus, P256_EXT_WORDS);
    }
    for (i = 0U; i < P256_WORDS; i++) {
        out->v[i] = sum[i];
    }
}

static void p256_mod_sub(p256_int_t *out, const p256_int_t *a,
                         const p256_int_t *b, const p256_int_t *mod) {
    p256_int_t diff;

    if (p256_int_cmp(a, b) >= 0) {
        p256_int_sub_raw(out, a, b);
        return;
    }
    p256_int_sub_raw(&diff, b, a);
    p256_int_sub_raw(out, mod, &diff);
}

static void p256_mul_raw(mr_uint32_t out[P256_PRODUCT_WORDS],
                         const p256_int_t *a, const p256_int_t *b) {
    mr_uint64_t value;
    mr_uint64_t carry;
    mr_uint32_t i, j, k;

    mr_memset(out, 0, sizeof(mr_uint32_t) * P256_PRODUCT_WORDS);
    for (i = 0U; i < P256_WORDS; i++) {
        carry = 0U;
        for (j = 0U; j < P256_WORDS; j++) {
            value = (mr_uint64_t)a->v[i] * b->v[j] + out[i + j] + carry;
            out[i + j] = (mr_uint32_t)value;
            carry = value >> P256_WORD_BITS;
        }
        k = i + P256_WORDS;
        while ((carry != 0U) && (k < P256_PRODUCT_WORDS)) {
            value = (mr_uint64_t)out[k] + carry;
            out[k] = (mr_uint32_t)value;
            carry = value >> 32U;
            k++;
        }
    }
}

static void p256_mod_reduce_product(
    p256_int_t *out, const mr_uint32_t product[P256_PRODUCT_WORDS],
    const p256_int_t *mod) {
    mr_uint32_t rem[P256_EXT_WORDS] = {0};
    mr_uint32_t modulus[P256_EXT_WORDS] = {0};
    mr_uint64_t value;
    mr_uint32_t bit;
    mr_uint32_t i;
    mr_uint32_t carry;

    for (i = 0U; i < P256_WORDS; i++) {
        modulus[i] = mod->v[i];
    }
    for (bit = P256_PRODUCT_BITS; bit != 0U; bit--) {
        carry = (product[(bit - 1U) / P256_WORD_BITS]
                 >> ((bit - 1U) % P256_WORD_BITS))
              & 1U;
        for (i = 0U; i < P256_EXT_WORDS; i++) {
            value = ((mr_uint64_t)rem[i] << 1U) | carry;
            rem[i] = (mr_uint32_t)value;
            carry = (mr_uint32_t)(value >> P256_WORD_BITS);
        }
        if (p256_ext_cmp(rem, modulus) >= 0) {
            (void)p256_sub_words(rem, rem, modulus, P256_EXT_WORDS);
        }
    }
    for (i = 0U; i < P256_WORDS; i++) {
        out->v[i] = rem[i];
    }
}

static void p256_mod_mul(p256_int_t *out, const p256_int_t *a,
                         const p256_int_t *b, const p256_int_t *mod) {
    mr_uint32_t product[P256_PRODUCT_WORDS];

    p256_mul_raw(product, a, b);
    p256_mod_reduce_product(out, product, mod);
    p256_zeroize(product, sizeof(product));
}

static void p256_mod_square(p256_int_t *out, const p256_int_t *a,
                            const p256_int_t *mod) {
    p256_mod_mul(out, a, a, mod);
}

static void p256_sub_small(p256_int_t *out, const p256_int_t *value,
                           mr_uint32_t small) {
    mr_uint64_t ai;
    mr_uint64_t bi;
    mr_uint32_t borrow = small;
    mr_uint32_t i;

    *out = *value;
    for (i = 0U; (i < P256_WORDS) && (borrow != 0U); i++) {
        ai = out->v[i];
        bi = borrow;
        out->v[i] = (mr_uint32_t)(ai - bi);
        borrow = (ai < bi) ? 1U : 0U;
    }
}

static mr_bool_t p256_mod_inv(p256_int_t *out, const p256_int_t *value,
                              const p256_int_t *mod) {
    p256_int_t exponent;
    p256_int_t result;
    p256_int_t base;
    mr_uint32_t bit;

    if (p256_int_is_zero(value)) {
        p256_int_zero(out);
        return MR_FALSE;
    }
    p256_sub_small(&exponent, mod, 2U);
    p256_int_one(&result);
    base = *value;
    for (bit = P256_BIT_SIZE; bit != 0U; bit--) {
        p256_mod_square(&result, &result, mod);
        if (((exponent.v[(bit - 1U) / P256_WORD_BITS]
              >> ((bit - 1U) % P256_WORD_BITS))
             & 1U)
            != 0U) {
            p256_mod_mul(&result, &result, &base, mod);
        }
    }
    *out = result;
    p256_zeroize(&exponent, sizeof(exponent));
    p256_zeroize(&result, sizeof(result));
    p256_zeroize(&base, sizeof(base));
    return MR_TRUE;
}

static void p256_fp_add(p256_int_t *out, const p256_int_t *a,
                        const p256_int_t *b) {
    p256_mod_add(out, a, b, &__p256_p);
}

static void p256_fp_sub(p256_int_t *out, const p256_int_t *a,
                        const p256_int_t *b) {
    p256_mod_sub(out, a, b, &__p256_p);
}

static void p256_fp_mul(p256_int_t *out, const p256_int_t *a,
                        const p256_int_t *b) {
    p256_mod_mul(out, a, b, &__p256_p);
}

static void p256_fp_square(p256_int_t *out, const p256_int_t *a) {
    p256_mod_square(out, a, &__p256_p);
}

static mr_bool_t p256_fp_inv(p256_int_t *out, const p256_int_t *value) {
    return p256_mod_inv(out, value, &__p256_p);
}

static void p256_fp_double(p256_int_t *out, const p256_int_t *value) {
    p256_fp_add(out, value, value);
}

static void p256_fp_times4(p256_int_t *out, const p256_int_t *value) {
    p256_int_t tmp;

    p256_fp_double(&tmp, value);
    p256_fp_double(out, &tmp);
}

static void p256_fp_times8(p256_int_t *out, const p256_int_t *value) {
    p256_int_t tmp;

    p256_fp_times4(&tmp, value);
    p256_fp_double(out, &tmp);
}

static void p256_mod_n_mul(p256_int_t *out, const p256_int_t *a,
                           const p256_int_t *b) {
    p256_mod_mul(out, a, b, &__p256_n);
}

static mr_bool_t p256_mod_n_inv(p256_int_t *out, const p256_int_t *value) {
    return p256_mod_inv(out, value, &__p256_n);
}

static void p256_mod_n_reduce(p256_int_t *value) {
    while (p256_int_cmp(value, &__p256_n) >= 0) {
        p256_int_sub_raw(value, value, &__p256_n);
    }
}

static mr_bool_t p256_scalar_valid(const p256_int_t *scalar) {
    if (p256_int_is_zero(scalar)) {
        return MR_FALSE;
    }
    return (p256_int_cmp(scalar, &__p256_n) < 0) ? MR_TRUE : MR_FALSE;
}

static void p256_jacobian_infinity(p256_jacobian_t *point) {
    p256_int_zero(&point->x);
    p256_int_one(&point->y);
    p256_int_zero(&point->z);
}

static mr_bool_t p256_jacobian_is_infinity(const p256_jacobian_t *point) {
    return p256_int_is_zero(&point->z);
}

static void p256_jacobian_from_affine(p256_jacobian_t *out,
                                      const p256_affine_t *point) {
    if (point->infinity) {
        p256_jacobian_infinity(out);
        return;
    }
    out->x = point->x;
    out->y = point->y;
    p256_int_one(&out->z);
}

static void p256_affine_from_jacobian(p256_affine_t *out,
                                      const p256_jacobian_t *point) {
    p256_int_t inv, inv2, inv3;

    if (p256_jacobian_is_infinity(point)) {
        p256_int_zero(&out->x);
        p256_int_zero(&out->y);
        out->infinity = MR_TRUE;
        return;
    }
    (void)p256_fp_inv(&inv, &point->z);
    p256_fp_square(&inv2, &inv);
    p256_fp_mul(&inv3, &inv2, &inv);
    p256_fp_mul(&out->x, &point->x, &inv2);
    p256_fp_mul(&out->y, &point->y, &inv3);
    out->infinity = MR_FALSE;
    p256_zeroize(&inv, sizeof(inv));
    p256_zeroize(&inv2, sizeof(inv2));
    p256_zeroize(&inv3, sizeof(inv3));
}

static void p256_point_double_jacobian(p256_jacobian_t *out,
                                       const p256_jacobian_t *point) {
    p256_jacobian_t result;
    p256_int_t delta, gamma, beta, alpha;
    p256_int_t t1, t2, gamma2;

    if (p256_jacobian_is_infinity(point) || p256_int_is_zero(&point->y)) {
        p256_jacobian_infinity(out);
        return;
    }

    p256_fp_square(&delta, &point->z);
    p256_fp_square(&gamma, &point->y);
    p256_fp_mul(&beta, &point->x, &gamma);

    p256_fp_sub(&t1, &point->x, &delta);
    p256_fp_add(&t2, &point->x, &delta);
    p256_fp_mul(&alpha, &t1, &t2);
    p256_fp_double(&t1, &alpha);
    p256_fp_add(&alpha, &alpha, &t1);

    p256_fp_square(&result.x, &alpha);
    p256_fp_times8(&t1, &beta);
    p256_fp_sub(&result.x, &result.x, &t1);

    p256_fp_add(&t1, &point->y, &point->z);
    p256_fp_square(&result.z, &t1);
    p256_fp_sub(&result.z, &result.z, &gamma);
    p256_fp_sub(&result.z, &result.z, &delta);

    p256_fp_times4(&t1, &beta);
    p256_fp_sub(&t1, &t1, &result.x);
    p256_fp_mul(&result.y, &alpha, &t1);
    p256_fp_square(&gamma2, &gamma);
    p256_fp_times8(&t2, &gamma2);
    p256_fp_sub(&result.y, &result.y, &t2);

    *out = result;
}

static void p256_point_add_mixed(p256_jacobian_t *out, const p256_jacobian_t *a,
                                 const p256_affine_t *b) {
    p256_jacobian_t result;
    p256_int_t z1z1, u2, s2, h, hh, i, j, r, v;
    p256_int_t t1, t2;

    if (b->infinity) {
        *out = *a;
        return;
    }
    if (p256_jacobian_is_infinity(a)) {
        p256_jacobian_from_affine(out, b);
        return;
    }

    p256_fp_square(&z1z1, &a->z);
    p256_fp_mul(&u2, &b->x, &z1z1);
    p256_fp_mul(&t1, &a->z, &z1z1);
    p256_fp_mul(&s2, &b->y, &t1);

    if (p256_int_cmp(&u2, &a->x) == 0) {
        if (p256_int_cmp(&s2, &a->y) == 0) {
            p256_point_double_jacobian(out, a);
        } else {
            p256_jacobian_infinity(out);
        }
        return;
    }

    p256_fp_sub(&h, &u2, &a->x);
    p256_fp_square(&hh, &h);
    p256_fp_times4(&i, &hh);
    p256_fp_mul(&j, &h, &i);
    p256_fp_sub(&r, &s2, &a->y);
    p256_fp_double(&r, &r);
    p256_fp_mul(&v, &a->x, &i);

    p256_fp_square(&result.x, &r);
    p256_fp_sub(&result.x, &result.x, &j);
    p256_fp_double(&t1, &v);
    p256_fp_sub(&result.x, &result.x, &t1);

    p256_fp_sub(&t1, &v, &result.x);
    p256_fp_mul(&result.y, &r, &t1);
    p256_fp_mul(&t2, &a->y, &j);
    p256_fp_double(&t2, &t2);
    p256_fp_sub(&result.y, &result.y, &t2);

    p256_fp_add(&t1, &a->z, &h);
    p256_fp_square(&result.z, &t1);
    p256_fp_sub(&result.z, &result.z, &z1z1);
    p256_fp_sub(&result.z, &result.z, &hh);
    *out = result;
}

static void p256_scalar_mult(p256_affine_t *out, const p256_affine_t *point,
                             const p256_int_t *scalar) {
    p256_jacobian_t acc;
    p256_jacobian_t tmp;
    mr_uint32_t bit;

    p256_jacobian_infinity(&acc);
    for (bit = P256_BIT_SIZE; bit != 0U; bit--) {
        p256_point_double_jacobian(&tmp, &acc);
        acc = tmp;
        if (((scalar->v[(bit - 1U) / P256_WORD_BITS]
              >> ((bit - 1U) % P256_WORD_BITS))
             & 1U)
            != 0U) {
            p256_point_add_mixed(&tmp, &acc, point);
            acc = tmp;
        }
    }
    p256_affine_from_jacobian(out, &acc);
    p256_zeroize(&acc, sizeof(acc));
    p256_zeroize(&tmp, sizeof(tmp));
}

static void p256_point_add(p256_affine_t *out, const p256_affine_t *a,
                           const p256_affine_t *b) {
    p256_jacobian_t jac;

    if (a->infinity) {
        *out = *b;
        return;
    }
    p256_jacobian_from_affine(&jac, a);
    p256_point_add_mixed(&jac, &jac, b);
    p256_affine_from_jacobian(out, &jac);
    p256_zeroize(&jac, sizeof(jac));
}

static mr_bool_t p256_point_valid(const p256_affine_t *point) {
    p256_int_t left, right, x2, x3, three_x;
    p256_int_t two_x;
    mr_bool_t valid;

    if (point->infinity || (p256_int_cmp(&point->x, &__p256_p) >= 0)
        || (p256_int_cmp(&point->y, &__p256_p) >= 0)) {
        return MR_FALSE;
    }
    p256_fp_square(&left, &point->y);
    p256_fp_square(&x2, &point->x);
    p256_fp_mul(&x3, &x2, &point->x);
    p256_fp_double(&two_x, &point->x);
    p256_fp_add(&three_x, &two_x, &point->x);
    p256_fp_sub(&right, &x3, &three_x);
    p256_fp_add(&right, &right, &__p256_b);
    valid = (p256_int_cmp(&left, &right) == 0) ? MR_TRUE : MR_FALSE;
    mr_memset(&left, 0, sizeof(left));
    mr_memset(&right, 0, sizeof(right));
    mr_memset(&x2, 0, sizeof(x2));
    mr_memset(&x3, 0, sizeof(x3));
    mr_memset(&three_x, 0, sizeof(three_x));
    mr_memset(&two_x, 0, sizeof(two_x));
    return valid;
}

static void p256_point_from_public(p256_affine_t *point,
                                   const mr_p256_public_key_t *public_key) {
    p256_int_from_be(&point->x, public_key->x);
    p256_int_from_be(&point->y, public_key->y);
    point->infinity = MR_FALSE;
}

static void p256_point_to_public(const p256_affine_t *point,
                                 mr_p256_public_key_t *public_key) {
    p256_int_to_be(&point->x, public_key->x);
    p256_int_to_be(&point->y, public_key->y);
}

mr_bool_t mr_p256_private_key_valid(
    const mr_uint8_t private_key[MR_P256_SCALAR_SIZE]) {
    p256_int_t scalar;
    mr_bool_t valid;

    MR_ASSERT(private_key != MR_NULL);

    p256_int_from_be(&scalar, private_key);
    valid = p256_scalar_valid(&scalar);
    p256_zeroize(&scalar, sizeof(scalar));
    return valid;
}

mr_bool_t mr_p256_public_key_valid(const mr_p256_public_key_t *public_key) {
    p256_affine_t point;
    mr_bool_t valid;

    MR_ASSERT(public_key != MR_NULL);

    p256_point_from_public(&point, public_key);
    valid = p256_point_valid(&point);
    p256_zeroize(&point, sizeof(point));
    return valid;
}

mr_err_t mr_p256_public_key(const mr_uint8_t private_key[MR_P256_SCALAR_SIZE],
                            mr_p256_public_key_t *public_key) {
    p256_int_t scalar;
    p256_affine_t point;

    MR_ASSERT(private_key != MR_NULL);
    MR_ASSERT(public_key != MR_NULL);

    p256_int_from_be(&scalar, private_key);
    if (!p256_scalar_valid(&scalar)) {
        p256_zeroize(&scalar, sizeof(scalar));
        return -MR_EINVAL;
    }
    p256_scalar_mult(&point, &__p256_g, &scalar);
    if (point.infinity) {
        p256_zeroize(&scalar, sizeof(scalar));
        p256_zeroize(&point, sizeof(point));
        return -MR_EIO;
    }
    p256_point_to_public(&point, public_key);
    p256_zeroize(&scalar, sizeof(scalar));
    p256_zeroize(&point, sizeof(point));
    return 0;
}

#if defined(MR_USE_MODULE_CRYPTO_ECDSA_P256)
static void ecdsa_hash_scalar(p256_int_t *out,
                              const mr_uint8_t digest[MR_SHA256_DIGEST_SIZE]) {
    p256_int_from_be(out, digest);
    p256_mod_n_reduce(out);
}

mr_bool_t mr_ecdsa_p256_verify(const mr_p256_public_key_t *public_key,
                               const mr_uint8_t digest[MR_SHA256_DIGEST_SIZE],
                               const mr_ecdsa_p256_signature_t *signature) {
    p256_affine_t public_point, p1, p2, sum;
    p256_int_t r, s, z, w, u1, u2, v;
    mr_bool_t valid = MR_FALSE;

    MR_ASSERT(public_key != MR_NULL);
    MR_ASSERT(digest != MR_NULL);
    MR_ASSERT(signature != MR_NULL);

    p256_int_from_be(&r, signature->r);
    p256_int_from_be(&s, signature->s);
    p256_point_from_public(&public_point, public_key);
    if (!p256_scalar_valid(&r) || !p256_scalar_valid(&s)
        || !p256_point_valid(&public_point)) {
        goto out;
    }

    ecdsa_hash_scalar(&z, digest);
    if (!p256_mod_n_inv(&w, &s)) {
        goto out;
    }
    p256_mod_n_mul(&u1, &z, &w);
    p256_mod_n_mul(&u2, &r, &w);
    p256_scalar_mult(&p1, &__p256_g, &u1);
    p256_scalar_mult(&p2, &public_point, &u2);
    p256_point_add(&sum, &p1, &p2);
    if (sum.infinity) {
        goto out;
    }
    v = sum.x;
    p256_mod_n_reduce(&v);
    valid = (p256_int_cmp(&v, &r) == 0) ? MR_TRUE : MR_FALSE;

out:
    mr_memset(&public_point, 0, sizeof(public_point));
    mr_memset(&p1, 0, sizeof(p1));
    mr_memset(&p2, 0, sizeof(p2));
    mr_memset(&sum, 0, sizeof(sum));
    mr_memset(&r, 0, sizeof(r));
    mr_memset(&s, 0, sizeof(s));
    mr_memset(&z, 0, sizeof(z));
    mr_memset(&w, 0, sizeof(w));
    mr_memset(&u1, 0, sizeof(u1));
    mr_memset(&u2, 0, sizeof(u2));
    mr_memset(&v, 0, sizeof(v));
    return valid;
}
#endif /* defined(MR_USE_MODULE_CRYPTO_ECDSA_P256) */

#endif /* defined(MR_USE_MODULE_CRYPTO_ECC_P256) */

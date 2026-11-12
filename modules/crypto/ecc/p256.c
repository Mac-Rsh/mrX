/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-20        MacRsh        First version
 */

#include <mrX/modules/crypto/ecc/mr_p256.h>
#if defined(MR_USE_MODULE_CRYPTO_P256)
#include <mrX/libc/mr_assert.h>
#include <mrX/libc/mr_string.h>

/* P-256 curve and group parameters definition */
static const mr_p256_value_t __p256_p = {
    {
     0xffffffffU, 0xffffffffU,
     0xffffffffU, 0x00000000U,
     0x00000000U, 0x00000000U,
     0x00000001U, 0xffffffffU,
     }
};
static const mr_p256_value_t __p256_b = {
    {
     0x27d2604bU, 0x3bce3c3eU,
     0xcc53b0f6U, 0x651d06b0U,
     0x769886bcU, 0xb3ebbd55U,
     0xaa3a93e7U, 0x5ac635d8U,
     }
};
static const mr_p256_value_t __p256_n = {
    {
     0xfc632551U, 0xf3b9cac2U,
     0xa7179e84U, 0xbce6faadU,
     0xffffffffU, 0xffffffffU,
     0x00000000U, 0xffffffffU,
     }
};
static const mr_p256_point_t __p256_g = {
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

/* P-256 word and buffer sizes definition */
#define P256_WORDS                      MR_P256_VALUE_WORDS
#define P256_PRODUCT_WORDS              MR_P256_PRODUCT_WORDS
#define P256_EXT_WORDS                  MR_P256_EXT_WORDS
#define P256_WORD_SIZE                  (4U)
#define P256_WORD_BITS                  (32U)
#define P256_BIT_SIZE                   (MR_P256_SCALAR_SIZE * 8U)
#define P256_PRODUCT_BITS               (P256_BIT_SIZE * 2U)

static void p256_int_one(mr_p256_value_t *value) {
    /* Put one in the least significant limb */
    mr_memset(value, 0, sizeof(*value));
    value->v[0] = 1U;
}

static mr_bool_t p256_int_is_zero(const mr_p256_value_t *value) {
    mr_uint32_t acc, i;

    /* Fold all limbs into a single zero check */
    for (acc = 0U, i = 0U; i < P256_WORDS; i++) {
        acc |= value->v[i];
    }
    return (acc == 0U);
}

static int p256_int_cmp(const mr_p256_value_t *a, const mr_p256_value_t *b) {
    mr_uint32_t i;

    /* Let the highest differing limb decide the order */
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
    mr_uint64_t ai, bi, diff;
    mr_uint32_t borrow, i;

    /* Propagate the borrow from low to high limbs */
    for (borrow = 0U, i = 0U; i < words; i++) {
        ai = a[i];
        bi = (mr_uint64_t)b[i] + borrow;
        diff = ai - bi;
        out[i] = (mr_uint32_t)diff;
        borrow = (ai < bi) ? 1U : 0U;
    }

    /* Report underflow across the full width */
    return borrow;
}

static void p256_int_sub_raw(mr_p256_value_t *out, const mr_p256_value_t *a,
                             const mr_p256_value_t *b) {
    /* Keep only the low 256 bits of the difference */
    p256_sub_words(out->v, a->v, b->v, P256_WORDS);
}

static void p256_int_from_be(mr_p256_value_t *out,
                             const mr_uint8_t bytes[MR_P256_SCALAR_SIZE]) {
    mr_uint32_t off, i;

    /* Decode big-endian bytes into low-word-first limbs */
    for (i = 0U; i < P256_WORDS; i++) {
        off = MR_P256_SCALAR_SIZE - ((i + 1U) * P256_WORD_SIZE);
        out->v[i] = ((mr_uint32_t)bytes[off] << 24U)
                  | ((mr_uint32_t)bytes[off + 1U] << 16U)
                  | ((mr_uint32_t)bytes[off + 2U] << 8U)
                  | (mr_uint32_t)bytes[off + 3U];
    }
}

static int p256_ext_cmp(const mr_uint32_t a[P256_EXT_WORDS],
                        const mr_uint32_t b[P256_EXT_WORDS]) {
    mr_uint32_t i;

    /* Compare the extra limb before the low 256 bits */
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

static void p256_mod_add(mr_p256_t *arith, mr_p256_value_t *out,
                         const mr_p256_value_t *a, const mr_p256_value_t *b,
                         const mr_p256_value_t *mod) {
    mr_uint32_t *modulus, *sum;
    mr_uint64_t value, carry;
    mr_uint32_t i;

    /* Retain the carry in the extra limb */
    modulus = arith->mod.ext1;
    sum = arith->mod.ext0;
    carry = 0U;
    for (i = 0U; i < P256_WORDS; i++) {
        value = (mr_uint64_t)a->v[i] + b->v[i] + carry;
        sum[i] = (mr_uint32_t)value;
        carry = value >> P256_WORD_BITS;
        modulus[i] = mod->v[i];
    }
    sum[P256_WORDS] = (mr_uint32_t)carry;
    modulus[P256_WORDS] = 0U;

    /* Reduced operands give sum < 2*mod */
    if (p256_ext_cmp(sum, modulus) >= 0) {
        p256_sub_words(sum, sum, modulus, P256_EXT_WORDS);
    }

    /* Copy the reduced sum after consuming both inputs */
    for (i = 0U; i < P256_WORDS; i++) {
        out->v[i] = sum[i];
    }
}

static void p256_mod_sub(mr_p256_value_t *out, const mr_p256_value_t *a,
                         const mr_p256_value_t *b, const mr_p256_value_t *mod) {
    mr_p256_value_t diff;

    /* Subtract directly when the difference is nonnegative */
    if (p256_int_cmp(a, b) >= 0) {
        p256_int_sub_raw(out, a, b);
        return;
    }

    /* Compute mod - (b - a) when a - b would underflow */
    p256_int_sub_raw(&diff, b, a);
    p256_int_sub_raw(out, mod, &diff);
}

static void p256_mul_raw(mr_uint32_t out[P256_PRODUCT_WORDS],
                         const mr_p256_value_t *a, const mr_p256_value_t *b) {
    mr_uint64_t value, carry;
    mr_uint32_t i, j;

    /* Accumulate shifted rows into the full-width product */
    mr_memset(out, 0, sizeof(mr_uint32_t) * P256_PRODUCT_WORDS);
    for (i = 0U; i < P256_WORDS; i++) {
        for (carry = 0U, j = 0U; j < P256_WORDS; j++) {
            value = (mr_uint64_t)a->v[i] * b->v[j] + out[i + j] + carry;
            out[i + j] = (mr_uint32_t)value;
            carry = value >> P256_WORD_BITS;
        }
        /* Store carry in the next untouched limb */
        out[i + P256_WORDS] = (mr_uint32_t)carry;
    }
}

static void p256_mod_reduce_product(
    mr_p256_t *arith, mr_p256_value_t *out,
    const mr_uint32_t product[P256_PRODUCT_WORDS], const mr_p256_value_t *mod) {
    mr_uint32_t *modulus, *rem;
    mr_uint32_t carry, bit, i;
    mr_uint64_t value;

    /* Zero-extend the modulus and start with remainder zero */
    modulus = arith->mod.ext1;
    rem = arith->mod.ext0;
    mr_memset(modulus, 0, sizeof(arith->mod.ext1));
    mr_memset(rem, 0, sizeof(arith->mod.ext0));
    for (i = 0U; i < P256_WORDS; i++) {
        modulus[i] = mod->v[i];
    }

    /* Shift in one bit and restore rem < mod */
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
            p256_sub_words(rem, rem, modulus, P256_EXT_WORDS);
        }
    }

    /* The reduced remainder fits in 256 bits */
    for (i = 0U; i < P256_WORDS; i++) {
        out->v[i] = rem[i];
    }
}

static void p256_mod_mul(mr_p256_t *arith, mr_p256_value_t *out,
                         const mr_p256_value_t *a, const mr_p256_value_t *b,
                         const mr_p256_value_t *mod) {
    mr_uint32_t *product;

    /* Stage the full product before reducing into out */
    product = arith->mod.product;
    p256_mul_raw(product, a, b);
    p256_mod_reduce_product(arith, out, product, mod);

    /* Clear the full-width scratch buffer */
    mr_memset(product, 0, sizeof(arith->mod.product));
}

static void p256_mod_square(mr_p256_t *arith, mr_p256_value_t *out,
                            const mr_p256_value_t *a,
                            const mr_p256_value_t *mod) {
    p256_mod_mul(arith, out, a, a, mod);
}

static void p256_sub_small(mr_p256_value_t *out, const mr_p256_value_t *value,
                           mr_uint32_t small) {
    mr_uint32_t borrow, i;
    mr_uint64_t ai, bi;

    /* Subtract small from the low limb, then propagate any borrow */
    borrow = small;
    *out = *value;
    for (i = 0U; (i < P256_WORDS) && (borrow != 0U); i++) {
        ai = out->v[i];
        bi = borrow;
        out->v[i] = (mr_uint32_t)(ai - bi);
        borrow = (ai < bi) ? 1U : 0U;
    }
}

static mr_bool_t p256_mod_inv(mr_p256_t *arith, mr_p256_value_t *out,
                              const mr_p256_value_t *value,
                              const mr_p256_value_t *mod) {
    mr_p256_value_t *exponent, *result, *base;
    mr_uint32_t bit;

    /* Keep inversion scratch separate from affine values */
    exponent = &arith->nested.affine.inverse[0];
    result = &arith->nested.affine.inverse[1];
    base = &arith->nested.affine.inverse[2];

    /* Zero has no multiplicative inverse */
    if (p256_int_is_zero(value)) {
        mr_memset(out, 0, sizeof(*out));
        return MR_FALSE;
    }

    /* Set up Fermat inversion with exponent mod - 2 */
    p256_sub_small(exponent, mod, 2U);
    p256_int_one(result);
    *base = *value;

    /* Raise the input to the exponent by square-and-multiply */
    for (bit = P256_BIT_SIZE; bit != 0U; bit--) {
        p256_mod_square(arith, result, result, mod);
        if (((exponent->v[(bit - 1U) / P256_WORD_BITS]
              >> ((bit - 1U) % P256_WORD_BITS))
             & 1U)
            != 0U) {
            p256_mod_mul(arith, result, result, base, mod);
        }
    }

    /* Save the inverse and clear the temporaries */
    *out = *result;
    mr_memset(exponent, 0, sizeof(*exponent));
    mr_memset(result, 0, sizeof(*result));
    mr_memset(base, 0, sizeof(*base));
    return MR_TRUE;
}

static void p256_fp_add(mr_p256_t *arith, mr_p256_value_t *out,
                        const mr_p256_value_t *a, const mr_p256_value_t *b) {
    p256_mod_add(arith, out, a, b, &__p256_p);
}

static void p256_fp_sub(mr_p256_value_t *out, const mr_p256_value_t *a,
                        const mr_p256_value_t *b) {
    p256_mod_sub(out, a, b, &__p256_p);
}

static void p256_fp_mul(mr_p256_t *arith, mr_p256_value_t *out,
                        const mr_p256_value_t *a, const mr_p256_value_t *b) {
    p256_mod_mul(arith, out, a, b, &__p256_p);
}

static void p256_fp_square(mr_p256_t *arith, mr_p256_value_t *out,
                           const mr_p256_value_t *a) {
    p256_mod_square(arith, out, a, &__p256_p);
}

static mr_bool_t p256_fp_inv(mr_p256_t *arith, mr_p256_value_t *out,
                             const mr_p256_value_t *value) {
    return p256_mod_inv(arith, out, value, &__p256_p);
}

static void p256_fp_double(mr_p256_t *arith, mr_p256_value_t *out,
                           const mr_p256_value_t *value) {
    p256_fp_add(arith, out, value, value);
}

static void p256_fp_times4(mr_p256_t *arith, mr_p256_value_t *out,
                           const mr_p256_value_t *value) {
    p256_fp_double(arith, out, value);
    p256_fp_double(arith, out, out);
}

static void p256_fp_times8(mr_p256_t *arith, mr_p256_value_t *out,
                           const mr_p256_value_t *value) {
    p256_fp_times4(arith, out, value);
    p256_fp_double(arith, out, out);
}

static void p256_mod_n_mul(mr_p256_t *arith, mr_p256_value_t *out,
                           const mr_p256_value_t *a, const mr_p256_value_t *b) {
    p256_mod_mul(arith, out, a, b, &__p256_n);
}

static mr_bool_t p256_mod_n_inv(mr_p256_t *arith, mr_p256_value_t *out,
                                const mr_p256_value_t *value) {
    return p256_mod_inv(arith, out, value, &__p256_n);
}

static void p256_mod_n_reduce(mr_p256_value_t *value) {
    /* Subtract at most once because n > 2^255 */
    if (p256_int_cmp(value, &__p256_n) >= 0) {
        p256_int_sub_raw(value, value, &__p256_n);
    }
}

static mr_bool_t p256_scalar_valid(const mr_p256_value_t *scalar) {
    /* Require a nonzero scalar below the group order */
    if (p256_int_is_zero(scalar)) {
        return MR_FALSE;
    }
    return (p256_int_cmp(scalar, &__p256_n) < 0);
}

static void p256_jacobian_infinity(mr_p256_jacobian_t *point) {
    /* Represent infinity with Z = 0 */
    mr_memset(&point->x, 0, sizeof(point->x));
    p256_int_one(&point->y);
    mr_memset(&point->z, 0, sizeof(point->z));
}

static mr_bool_t p256_jacobian_is_infinity(const mr_p256_jacobian_t *point) {
    return p256_int_is_zero(&point->z);
}

static void p256_jacobian_from_affine(mr_p256_jacobian_t *out,
                                      const mr_p256_point_t *point) {
    /* Preserve infinity during the coordinate conversion */
    if (point->infinity) {
        p256_jacobian_infinity(out);
        return;
    }

    /* Lift a finite affine point with Z = 1 */
    out->x = point->x;
    out->y = point->y;
    p256_int_one(&out->z);
}

static void p256_affine_from_jacobian(mr_p256_t *arith, mr_p256_point_t *out,
                                      const mr_p256_jacobian_t *point) {
    mr_p256_value_t *inv, *inv2, *inv3;

    /* Reserve scratch for the inverse powers of Z */
    inv = &arith->nested.affine.value[0];
    inv2 = &arith->nested.affine.value[1];
    inv3 = &arith->nested.affine.value[2];

    /* Return affine infinity without inverting zero */
    if (p256_jacobian_is_infinity(point)) {
        mr_memset(&out->x, 0, sizeof(out->x));
        mr_memset(&out->y, 0, sizeof(out->y));
        out->infinity = MR_TRUE;
        return;
    }

    /* Recover X/Z^2 and Y/Z^3 with one inversion */
    p256_fp_inv(arith, inv, &point->z);
    p256_fp_square(arith, inv2, inv);
    p256_fp_mul(arith, inv3, inv2, inv);
    p256_fp_mul(arith, &out->x, &point->x, inv2);
    p256_fp_mul(arith, &out->y, &point->y, inv3);
    out->infinity = MR_FALSE;

    /* Clear the inverse powers after conversion */
    mr_memset(inv, 0, sizeof(*inv));
    mr_memset(inv2, 0, sizeof(*inv2));
    mr_memset(inv3, 0, sizeof(*inv3));
}

static void p256_point_double_jacobian(mr_p256_t *arith,
                                       mr_p256_jacobian_t *out,
                                       const mr_p256_jacobian_t *point) {
    mr_p256_value_t *delta, *gamma, *beta, *alpha, *gamma2, *t1, *t2;
    mr_p256_jacobian_t *result;

    /* Stage the result so out may alias point */
    result = &arith->nested.point.result;
    delta = &arith->nested.point.value[0];
    gamma = &arith->nested.point.value[1];
    beta = &arith->nested.point.value[2];
    alpha = &arith->nested.point.value[3];
    t1 = &arith->nested.point.value[4];
    t2 = &arith->nested.point.value[5];
    gamma2 = &arith->nested.point.value[6];

    /* Return infinity for infinity or Y = 0 */
    if (p256_jacobian_is_infinity(point) || p256_int_is_zero(&point->y)) {
        p256_jacobian_infinity(out);
        return;
    }

    /* Prepare delta = Z^2, gamma = Y^2 and beta = X*gamma */
    p256_fp_square(arith, delta, &point->z);
    p256_fp_square(arith, gamma, &point->y);
    p256_fp_mul(arith, beta, &point->x, gamma);

    /* Factor the slope term using a = -3 */
    p256_fp_sub(t1, &point->x, delta);
    p256_fp_add(arith, t2, &point->x, delta);
    p256_fp_mul(arith, alpha, t1, t2);
    p256_fp_double(arith, t1, alpha);
    p256_fp_add(arith, alpha, alpha, t1);

    /* Compute the doubled point's X coordinate */
    p256_fp_square(arith, &result->x, alpha);
    p256_fp_times8(arith, t1, beta);
    p256_fp_sub(&result->x, &result->x, t1);

    /* Compute Z3 = 2*Y*Z using the squared sum */
    p256_fp_add(arith, t1, &point->y, &point->z);
    p256_fp_square(arith, &result->z, t1);
    p256_fp_sub(&result->z, &result->z, gamma);
    p256_fp_sub(&result->z, &result->z, delta);

    /* Compute Y3 from the slope term and X3 */
    p256_fp_times4(arith, t1, beta);
    p256_fp_sub(t1, t1, &result->x);
    p256_fp_mul(arith, &result->y, alpha, t1);
    p256_fp_square(arith, gamma2, gamma);
    p256_fp_times8(arith, t2, gamma2);
    p256_fp_sub(&result->y, &result->y, t2);

    *out = *result;
}

static void p256_point_add_mixed(mr_p256_t *arith, mr_p256_jacobian_t *out,
                                 const mr_p256_jacobian_t *a,
                                 const mr_p256_point_t *b) {
    mr_p256_value_t *z1z1, *u2, *s2, *h, *hh, *i, *j;
    mr_p256_value_t *r, *v, *t1, *t2;
    mr_p256_jacobian_t *result;

    /* Stage the sum for in-place point addition */
    result = &arith->nested.point.result;
    z1z1 = &arith->nested.point.value[0];
    u2 = &arith->nested.point.value[1];
    s2 = &arith->nested.point.value[2];
    h = &arith->nested.point.value[3];
    hh = &arith->nested.point.value[4];
    i = &arith->nested.point.value[5];
    j = &arith->nested.point.value[6];
    r = &arith->nested.point.value[7];
    v = &arith->nested.point.value[8];
    t1 = &arith->nested.point.value[9];
    t2 = &arith->nested.point.value[10];

    /* Handle the additive identity */
    if (b->infinity) {
        *out = *a;
        return;
    }
    if (p256_jacobian_is_infinity(a)) {
        p256_jacobian_from_affine(out, b);
        return;
    }

    /* Scale affine coordinates by Z^2 and Z^3 */
    p256_fp_square(arith, z1z1, &a->z);
    p256_fp_mul(arith, u2, &b->x, z1z1);
    p256_fp_mul(arith, t1, &a->z, z1z1);
    p256_fp_mul(arith, s2, &b->y, t1);

    /* Double equal points or cancel opposite points */
    if (p256_int_cmp(u2, &a->x) == 0) {
        if (p256_int_cmp(s2, &a->y) == 0) {
            p256_point_double_jacobian(arith, out, a);
        } else {
            p256_jacobian_infinity(out);
        }
        return;
    }

    /* Derive mixed-add terms from coordinate differences */
    p256_fp_sub(h, u2, &a->x);
    p256_fp_square(arith, hh, h);
    p256_fp_times4(arith, i, hh);
    p256_fp_mul(arith, j, h, i);
    p256_fp_sub(r, s2, &a->y);
    p256_fp_double(arith, r, r);
    p256_fp_mul(arith, v, &a->x, i);

    /* Compute X3 = r^2 - j - 2*v */
    p256_fp_square(arith, &result->x, r);
    p256_fp_sub(&result->x, &result->x, j);
    p256_fp_double(arith, t1, v);
    p256_fp_sub(&result->x, &result->x, t1);

    /* Compute Y3 using the new X coordinate */
    p256_fp_sub(t1, v, &result->x);
    p256_fp_mul(arith, &result->y, r, t1);
    p256_fp_mul(arith, t2, &a->y, j);
    p256_fp_double(arith, t2, t2);
    p256_fp_sub(&result->y, &result->y, t2);

    /* Compute Z3 = 2*Z1*h using the squared sum */
    p256_fp_add(arith, t1, &a->z, h);
    p256_fp_square(arith, &result->z, t1);
    p256_fp_sub(&result->z, &result->z, z1z1);
    p256_fp_sub(&result->z, &result->z, hh);
    *out = *result;
}

static void p256_scalar_mult(mr_p256_t *arith, mr_p256_point_t *out,
                             const mr_p256_point_t *point,
                             const mr_p256_value_t *scalar) {
    mr_p256_jacobian_t *acc, *tmp;
    mr_uint32_t bit;

    /* Start with infinity as the accumulator */
    acc = &arith->jacobian[0];
    tmp = &arith->jacobian[1];
    p256_jacobian_infinity(acc);

    /* Double and add from the top bit */
    for (bit = P256_BIT_SIZE; bit != 0U; bit--) {
        p256_point_double_jacobian(arith, tmp, acc);
        *acc = *tmp;
        if (((scalar->v[(bit - 1U) / P256_WORD_BITS]
              >> ((bit - 1U) % P256_WORD_BITS))
             & 1U)
            != 0U) {
            p256_point_add_mixed(arith, tmp, acc, point);
            *acc = *tmp;
        }
    }

    /* Convert to affine only after processing all bits */
    p256_affine_from_jacobian(arith, out, acc);

    /* Clear both Jacobian work points */
    mr_memset(acc, 0, sizeof(*acc));
    mr_memset(tmp, 0, sizeof(*tmp));
}

static void p256_point_add(mr_p256_t *arith, mr_p256_point_t *out,
                           const mr_p256_point_t *a, const mr_p256_point_t *b) {
    mr_p256_jacobian_t *jac;

    jac = &arith->jacobian[0];

    /* Return b directly when a is infinity */
    if (a->infinity) {
        *out = *b;
        return;
    }

    /* Lift a for mixed addition and convert the sum back */
    p256_jacobian_from_affine(jac, a);
    p256_point_add_mixed(arith, jac, jac, b);
    p256_affine_from_jacobian(arith, out, jac);
    mr_memset(jac, 0, sizeof(*jac));
}

static mr_bool_t p256_point_valid(mr_p256_t *arith,
                                  const mr_p256_point_t *point) {
    mr_p256_value_t *left, *right, *three_x, *two_x;
    mr_p256_value_t *x2, *x3;
    mr_bool_t valid;

    /* Reserve workspace for both sides of the curve equation */
    left = &arith->nested.valid[0];
    right = &arith->nested.valid[1];
    x2 = &arith->nested.valid[2];
    x3 = &arith->nested.valid[3];
    three_x = &arith->nested.valid[4];
    two_x = &arith->nested.valid[5];

    /* Require a finite point with canonical field coordinates */
    if (point->infinity || (p256_int_cmp(&point->x, &__p256_p) >= 0)
        || (p256_int_cmp(&point->y, &__p256_p) >= 0)) {
        return MR_FALSE;
    }

    /* Check y^2 = x^3 - 3x + b modulo the field prime */
    p256_fp_square(arith, left, &point->y);
    p256_fp_square(arith, x2, &point->x);
    p256_fp_mul(arith, x3, x2, &point->x);
    p256_fp_double(arith, two_x, &point->x);
    p256_fp_add(arith, three_x, two_x, &point->x);
    p256_fp_sub(right, x3, three_x);
    p256_fp_add(arith, right, right, &__p256_b);

    /* Compare both sides before clearing the temporaries */
    valid = (p256_int_cmp(left, right) == 0);
    mr_memset(left, 0, sizeof(*left));
    mr_memset(right, 0, sizeof(*right));
    mr_memset(x2, 0, sizeof(*x2));
    mr_memset(x3, 0, sizeof(*x3));
    mr_memset(three_x, 0, sizeof(*three_x));
    mr_memset(two_x, 0, sizeof(*two_x));
    return valid;
}

static void p256_point_from_public(mr_p256_point_t *point,
                                   const mr_p256_public_key_t *public_key) {
    /* Decode the affine coordinates as supplied */
    p256_int_from_be(&point->x, public_key->x);
    p256_int_from_be(&point->y, public_key->y);
    point->infinity = MR_FALSE;
}

void mr_p256_value_from_be(mr_p256_value_t *value,
                           const mr_uint8_t bytes[MR_P256_SCALAR_SIZE]) {
    /* Check parameter */
    MR_ASSERT(value != MR_NULL);
    MR_ASSERT(bytes != MR_NULL);

    /* Decode into the internal limb order */
    p256_int_from_be(value, bytes);
}

mr_bool_t mr_p256_scalar_valid(const mr_p256_value_t *scalar) {
    /* Check parameter */
    MR_ASSERT(scalar != MR_NULL);

    /* Check the scalar range [1, n - 1] */
    return p256_scalar_valid(scalar);
}

void mr_p256_scalar_reduce(mr_p256_value_t *scalar) {
    /* Check parameter */
    MR_ASSERT(scalar != MR_NULL);

    /* Reduce in place modulo the group order */
    p256_mod_n_reduce(scalar);
}

mr_bool_t mr_p256_scalar_inv(mr_p256_t *p256, mr_p256_value_t *out,
                             const mr_p256_value_t *scalar) {
    /* Check parameter */
    MR_ASSERT(p256 != MR_NULL);
    MR_ASSERT(out != MR_NULL);
    MR_ASSERT(scalar != MR_NULL);

    /* Invert modulo the group order */
    return p256_mod_n_inv(p256, out, scalar);
}

void mr_p256_scalar_mul(mr_p256_t *p256, mr_p256_value_t *out,
                        const mr_p256_value_t *a, const mr_p256_value_t *b) {
    /* Check parameter */
    MR_ASSERT(p256 != MR_NULL);
    MR_ASSERT(out != MR_NULL);
    MR_ASSERT(a != MR_NULL);
    MR_ASSERT(b != MR_NULL);

    /* Multiply scalars modulo the group order */
    p256_mod_n_mul(p256, out, a, b);
}

void mr_p256_point_from_public(mr_p256_point_t *point,
                               const mr_p256_public_key_t *public_key) {
    /* Check parameter */
    MR_ASSERT(point != MR_NULL);
    MR_ASSERT(public_key != MR_NULL);

    /* Decode coordinates before any curve validation */
    p256_point_from_public(point, public_key);
}

mr_bool_t mr_p256_point_valid(mr_p256_t *p256, const mr_p256_point_t *point) {
    /* Check parameter */
    MR_ASSERT(p256 != MR_NULL);
    MR_ASSERT(point != MR_NULL);

    /* Check field bounds and curve membership */
    return p256_point_valid(p256, point);
}

void mr_p256_point_mul_generator(mr_p256_t *p256, mr_p256_point_t *out,
                                 const mr_p256_value_t *scalar) {
    /* Check parameter */
    MR_ASSERT(p256 != MR_NULL);
    MR_ASSERT(out != MR_NULL);
    MR_ASSERT(scalar != MR_NULL);

    /* Use the fixed curve generator as the base */
    p256_scalar_mult(p256, out, &__p256_g, scalar);
}

void mr_p256_point_mul(mr_p256_t *p256, mr_p256_point_t *out,
                       const mr_p256_point_t *point,
                       const mr_p256_value_t *scalar) {
    /* Check parameter */
    MR_ASSERT(p256 != MR_NULL);
    MR_ASSERT(out != MR_NULL);
    MR_ASSERT(point != MR_NULL);
    MR_ASSERT(scalar != MR_NULL);

    /* Multiply with the caller-supplied point as the base */
    p256_scalar_mult(p256, out, point, scalar);
}

void mr_p256_point_add(mr_p256_t *p256, mr_p256_point_t *out,
                       const mr_p256_point_t *a, const mr_p256_point_t *b) {
    /* Check parameter */
    MR_ASSERT(p256 != MR_NULL);
    MR_ASSERT(out != MR_NULL);
    MR_ASSERT(a != MR_NULL);
    MR_ASSERT(b != MR_NULL);

    /* Add the affine inputs and return an affine point */
    p256_point_add(p256, out, a, b);
}
#endif /* defined(MR_USE_MODULE_CRYPTO_P256) */

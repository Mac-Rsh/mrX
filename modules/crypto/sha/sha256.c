/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-20        MacRsh        First version
 */

#include <mrX/modules/crypto/sha/mr_sha256.h>
#if defined(MR_USE_MODULE_CRYPTO_SHA256)
#include <mrX/libc/mr_assert.h>
#include <mrX/libc/mr_string.h>

/* SHA-256 round constant */
static const mr_uint32_t __sha256_k[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

/* SHA-256 definition */
#define SHA256_ROUND_NUM                (MR_ARRAY_SIZE(__sha256_k))
#define SHA256_LENGTH_SIZE              (8U)
#define SHA256_ROTR(_x, _n)                                                    \
    (((_x) >> (_n)) | ((_x) << (32U - (_n))))
#define SHA256_CH(_x, _y, _z)           (((_x) & (_y)) ^ (~(_x) & (_z)))
#define SHA256_MAJ(_x, _y, _z)                                                 \
    (((_x) & (_y)) ^ ((_x) & (_z)) ^ ((_y) & (_z)))
#define SHA256_BSIG0(_x)                                                       \
    (SHA256_ROTR((_x), 2U) ^ SHA256_ROTR((_x), 13U) ^ SHA256_ROTR((_x), 22U))
#define SHA256_BSIG1(_x)                                                       \
    (SHA256_ROTR((_x), 6U) ^ SHA256_ROTR((_x), 11U) ^ SHA256_ROTR((_x), 25U))
#define SHA256_SSIG0(_x)                                                       \
    (SHA256_ROTR((_x), 7U) ^ SHA256_ROTR((_x), 18U) ^ ((_x) >> 3U))
#define SHA256_SSIG1(_x)                                                       \
    (SHA256_ROTR((_x), 17U) ^ SHA256_ROTR((_x), 19U) ^ ((_x) >> 10U))

static void sha256_zeroize(void *data, mr_size_t size) {
    volatile mr_uint8_t *bytes;

    bytes = data;
    while (size-- != 0U) {
        *bytes++ = 0U;
    }
}

static mr_uint32_t sha256_load_be32(const mr_uint8_t *bytes) {
    return ((mr_uint32_t)bytes[0] << 24U) | ((mr_uint32_t)bytes[1] << 16U)
         | ((mr_uint32_t)bytes[2] << 8U) | (mr_uint32_t)bytes[3];
}

static void sha256_store_be32(mr_uint8_t *bytes, mr_uint32_t value) {
    bytes[0] = (mr_uint8_t)(value >> 24U);
    bytes[1] = (mr_uint8_t)(value >> 16U);
    bytes[2] = (mr_uint8_t)(value >> 8U);
    bytes[3] = (mr_uint8_t)value;
}

static void sha256_transform(mr_sha256_t *sha256,
                             const mr_uint8_t block[MR_SHA256_BLOCK_SIZE]) {
    mr_uint32_t a, b, c, d, e, f, g, h;
    mr_uint32_t w[SHA256_ROUND_NUM];
    mr_uint32_t t1, t2;
    mr_uint32_t i;

    /* Load block */
    for (i = 0U; i < (MR_SHA256_BLOCK_SIZE / 4U); i++) {
        w[i] = sha256_load_be32(block + (i * 4U));
    }
    /* Expand message schedule */
    for (; i < SHA256_ROUND_NUM; i++) {
        w[i] = SHA256_SSIG1(w[i - 2U]) + w[i - 7U] + SHA256_SSIG0(w[i - 15U])
             + w[i - 16U];
    }

    a = sha256->state[0];
    b = sha256->state[1];
    c = sha256->state[2];
    d = sha256->state[3];
    e = sha256->state[4];
    f = sha256->state[5];
    g = sha256->state[6];
    h = sha256->state[7];

    /* Compress */
    for (i = 0U; i < SHA256_ROUND_NUM; i++) {
        t1 = h + SHA256_BSIG1(e) + SHA256_CH(e, f, g) + __sha256_k[i] + w[i];
        t2 = SHA256_BSIG0(a) + SHA256_MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    /* Add compressed chunk */
    sha256->state[0] += a;
    sha256->state[1] += b;
    sha256->state[2] += c;
    sha256->state[3] += d;
    sha256->state[4] += e;
    sha256->state[5] += f;
    sha256->state[6] += g;
    sha256->state[7] += h;

    /* Clear message schedule */
    sha256_zeroize(w, sizeof(w));
}

void mr_sha256_init(mr_sha256_t *sha256) {
    /* Check parameter */
    MR_ASSERT(sha256 != MR_NULL);

    /* Init SHA-256 */
    sha256_zeroize(sha256, sizeof(*sha256));
    sha256->state[0] = 0x6a09e667U;
    sha256->state[1] = 0xbb67ae85U;
    sha256->state[2] = 0x3c6ef372U;
    sha256->state[3] = 0xa54ff53aU;
    sha256->state[4] = 0x510e527fU;
    sha256->state[5] = 0x9b05688cU;
    sha256->state[6] = 0x1f83d9abU;
    sha256->state[7] = 0x5be0cd19U;
    sha256->total_size = 0U;
    sha256->buffer_size = 0U;
}

void mr_sha256_update(mr_sha256_t *sha256, const void *data, mr_size_t size) {
    const mr_uint8_t *bytes;
    mr_size_t take;

    /* Check parameter */
    MR_ASSERT(sha256 != MR_NULL);
    MR_ASSERT((data != MR_NULL) || (size == 0U));

    if (size == 0U) {
        return;
    }

    /* Consume input */
    bytes = data;
    sha256->total_size += (mr_uint64_t)size;
    if (sha256->buffer_size != 0U) {
        take = MR_SHA256_BLOCK_SIZE - sha256->buffer_size;
        if (take > size) {
            take = size;
        }
        mr_memcpy(sha256->buffer + sha256->buffer_size, bytes, take);
        sha256->buffer_size += (mr_uint32_t)take;
        bytes += take;
        size -= take;
        if (sha256->buffer_size == MR_SHA256_BLOCK_SIZE) {
            sha256_transform(sha256, sha256->buffer);
            sha256->buffer_size = 0U;
        }
    }
    while (size >= MR_SHA256_BLOCK_SIZE) {
        sha256_transform(sha256, bytes);
        bytes += MR_SHA256_BLOCK_SIZE;
        size -= MR_SHA256_BLOCK_SIZE;
    }
    if (size != 0U) {
        mr_memcpy(sha256->buffer, bytes, size);
        sha256->buffer_size = (mr_uint32_t)size;
    }
}

void mr_sha256_final(mr_sha256_t *sha256,
                     mr_uint8_t digest[MR_SHA256_DIGEST_SIZE]) {
    mr_uint64_t bits;
    mr_uint32_t i;

    /* Check parameter */
    MR_ASSERT(sha256 != MR_NULL);
    MR_ASSERT(digest != MR_NULL);

    /* Pad message */
    bits = sha256->total_size * 8U;
    sha256->buffer[sha256->buffer_size++] = 0x80U;
    if (sha256->buffer_size > (MR_SHA256_BLOCK_SIZE - SHA256_LENGTH_SIZE)) {
        mr_memset(sha256->buffer + sha256->buffer_size, 0,
                  MR_SHA256_BLOCK_SIZE - sha256->buffer_size);
        sha256_transform(sha256, sha256->buffer);
        sha256->buffer_size = 0U;
    }
    mr_memset(sha256->buffer + sha256->buffer_size, 0,
              (MR_SHA256_BLOCK_SIZE - SHA256_LENGTH_SIZE)
                  - sha256->buffer_size);
    for (i = 0U; i < 8U; i++) {
        sha256->buffer[(MR_SHA256_BLOCK_SIZE - 1U) - i]
            = (mr_uint8_t)(bits >> (i * 8U));
    }
    sha256_transform(sha256, sha256->buffer);

    /* Output digest */
    for (i = 0U; i < 8U; i++) {
        sha256_store_be32(digest + (i * 4U), sha256->state[i]);
    }

    /* Clear working memory */
    sha256_zeroize(sha256, sizeof(*sha256));
}

void mr_sha256(const void *data, mr_size_t size,
               mr_uint8_t digest[MR_SHA256_DIGEST_SIZE]) {
    mr_sha256_t sha256;

    mr_sha256_init(&sha256);
    mr_sha256_update(&sha256, data, size);
    mr_sha256_final(&sha256, digest);
}

#endif /* defined(MR_USE_MODULE_CRYPTO_SHA256) */

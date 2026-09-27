/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-22        MacRsh        First version
 */

#include <mrX/modules/crypto/sha/mr_hmac_sha256.h>
#if defined(MR_USE_MODULE_CRYPTO_HMAC_SHA256)
#include <mrX/libc/mr_assert.h>
#include <mrX/libc/mr_string.h>

/* HMAC-SHA-256 definition */
#define HMAC_SHA256_IPAD                (0x36U)
#define HMAC_SHA256_OPAD                (0x5cU)

static void hmac_sha256_zeroize(void *data, mr_size_t size) {
    volatile mr_uint8_t *bytes;

    bytes = data;
    while (size-- != 0U) {
        *bytes++ = 0U;
    }
}

void mr_hmac_sha256_init(mr_hmac_sha256_t *hmac, const void *key,
                         mr_size_t key_size) {
    mr_uint8_t key_block[MR_SHA256_BLOCK_SIZE];
    mr_uint8_t key_digest[MR_SHA256_DIGEST_SIZE];
    mr_uint32_t i;

    /* Check parameter */
    MR_ASSERT(hmac != MR_NULL);
    MR_ASSERT((key != MR_NULL) || (key_size == 0U));

    /* Normalize key */
    mr_memset(key_block, 0, sizeof(key_block));
    if (key_size > MR_SHA256_BLOCK_SIZE) {
        mr_sha256(key, key_size, key_digest);
        mr_memcpy(key_block, key_digest, sizeof(key_digest));
    } else if (key_size != 0U) {
        mr_memcpy(key_block, key, key_size);
    }

    /* Init inner hash */
    for (i = 0U; i < MR_SHA256_BLOCK_SIZE; i++) {
        key_block[i] ^= HMAC_SHA256_IPAD;
    }
    mr_sha256_init(&hmac->inner);
    mr_sha256_update(&hmac->inner, key_block, sizeof(key_block));

    /* Convert ipad to opad and init outer hash */
    for (i = 0U; i < MR_SHA256_BLOCK_SIZE; i++) {
        key_block[i] ^= (HMAC_SHA256_IPAD ^ HMAC_SHA256_OPAD);
    }
    mr_sha256_init(&hmac->outer);
    mr_sha256_update(&hmac->outer, key_block, sizeof(key_block));

    /* Clear key material */
    hmac_sha256_zeroize(key_digest, sizeof(key_digest));
    hmac_sha256_zeroize(key_block, sizeof(key_block));
}

void mr_hmac_sha256_update(mr_hmac_sha256_t *hmac, const void *data,
                           mr_size_t size) {
    /* Check parameter */
    MR_ASSERT(hmac != MR_NULL);
    MR_ASSERT((data != MR_NULL) || (size == 0U));

    /* Update inner hash */
    mr_sha256_update(&hmac->inner, data, size);
}

void mr_hmac_sha256_final(mr_hmac_sha256_t *hmac,
                          mr_uint8_t auth[MR_SHA256_DIGEST_SIZE]) {
    mr_uint8_t digest[MR_SHA256_DIGEST_SIZE];

    /* Check parameter */
    MR_ASSERT(hmac != MR_NULL);
    MR_ASSERT(auth != MR_NULL);

    /* Finalize inner and outer hash */
    mr_sha256_final(&hmac->inner, digest);
    mr_sha256_update(&hmac->outer, digest, sizeof(digest));
    mr_sha256_final(&hmac->outer, auth);

    /* Clear working memory */
    hmac_sha256_zeroize(digest, sizeof(digest));
    hmac_sha256_zeroize(hmac, sizeof(*hmac));
}

void mr_hmac_sha256(const void *key, mr_size_t key_size, const void *data,
                    mr_size_t size, mr_uint8_t auth[MR_SHA256_DIGEST_SIZE]) {
    mr_hmac_sha256_t hmac;

    /* Check parameter */
    MR_ASSERT((key != MR_NULL) || (key_size == 0U));
    MR_ASSERT((data != MR_NULL) || (size == 0U));
    MR_ASSERT(auth != MR_NULL);

    /* Calculate HMAC-SHA-256 */
    mr_hmac_sha256_init(&hmac, key, key_size);
    mr_hmac_sha256_update(&hmac, data, size);
    mr_hmac_sha256_final(&hmac, auth);
}
#endif /* defined(MR_USE_MODULE_CRYPTO_HMAC_SHA256) */

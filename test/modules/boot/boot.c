/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-22        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST)
#include <mrX/modules/boot/mr_boot.h>
#if defined(MR_USE_MODULE_BOOT)
#include <mrX/libc/mr_string.h>
#include <setjmp.h>
#include <stddef.h>

#define TEST_BOOT_PRODUCT_ID   (0x58585858U)
#define TEST_BOOT_SLOT_SIZE    (512U)
#define TEST_BOOT_STATE_SIZE   (256U)
#define TEST_BOOT_SLOT_A       (0U)
#define TEST_BOOT_SLOT_B       (TEST_BOOT_SLOT_SIZE)
#define TEST_BOOT_RECOVERY     (TEST_BOOT_SLOT_SIZE * 2U)
#define TEST_BOOT_STATE_A      (TEST_BOOT_SLOT_SIZE * 3U)
#define TEST_BOOT_STATE_B      (TEST_BOOT_STATE_A + TEST_BOOT_STATE_SIZE)
#define TEST_BOOT_STORAGE_SIZE (TEST_BOOT_STATE_B + TEST_BOOT_STATE_SIZE)
#define TEST_BOOT_SN_SIZE      (8U)
#define TEST_BOOT_SIGNATURE    (0xA5U)

typedef struct test_boot_store {
    mr_uint8_t image[TEST_BOOT_STORAGE_SIZE];
    mr_boot_source_t source;
    mr_uint32_t rollback;
    mr_err_t read_error;
    mr_bool_t read_fault;
    mr_uint64_t read_fault_offset;
    mr_err_t read_fault_error;
    mr_int64_t read_short;
    mr_err_t program_error;
    mr_bool_t program_fault;
    mr_uint64_t program_fault_offset;
    mr_err_t program_fault_error;
    mr_int64_t program_short;
    mr_uint32_t program_calls;
    mr_err_t erase_error;
    mr_uint32_t erase_fail_at;
    mr_uint32_t erase_calls;
    mr_err_t rollback_read_error;
    mr_err_t rollback_update_error;
    mr_uint32_t rollback_updates;
    mr_uint32_t jump_fail_left;
    mr_uint64_t jump_offset;
    mr_uint32_t jump_calls;
    jmp_buf boot_env;
    mr_bool_t boot_armed;
    mr_bool_t corrupt_program;
    jmp_buf reset_env;
    mr_bool_t reset_armed;
    mr_uint64_t reset_offset;
    mr_uint8_t hash_acc;
    mr_size_t hash_len;
    mr_uint32_t hash_begins;
    mr_uint32_t hash_bytes;
    mr_err_t hash_error;
    mr_uint32_t verify_calls;
    mr_err_t verify_error;
    mr_err_t auth_error;
    mr_uint8_t sn[TEST_BOOT_SN_SIZE];
    mr_size_t sn_size;
    mr_ssize_t sn_error;
} test_boot_store_t;

static const mr_uint8_t test_boot_payload[] = {
    0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
};

static void test_boot_store_reset(test_boot_store_t *store) {
    /* Blank storage is erased, not zero */
    mr_memset(store, 0, sizeof(*store));
    mr_memset(store->image, 0xFF, sizeof(store->image));
    store->read_short = -1;
    store->program_short = -1;
}

static mr_bool_t test_boot_contains(mr_uint64_t offset, mr_uint64_t size) {
    if ((size == 0U) || (size > TEST_BOOT_STORAGE_SIZE)) {
        return MR_FALSE;
    }
    return (offset <= (TEST_BOOT_STORAGE_SIZE - size));
}

static mr_int64_t test_boot_read(mr_boot_t *boot, mr_uint64_t offset,
                                 void *data, mr_uint64_t size) {
    test_boot_store_t *store = boot->priv;

    /* Inject read failures */
    if (store->read_error != 0) {
        return store->read_error;
    }
    if (store->read_fault && (offset == store->read_fault_offset)) {
        return store->read_fault_error;
    }
    if (!test_boot_contains(offset, size)) {
        return -MR_EINVAL;
    }
    if ((store->read_short >= 0) && ((mr_uint64_t)store->read_short < size)) {
        size = (mr_uint64_t)store->read_short;
    }
    mr_memcpy(data, &store->image[(mr_size_t)offset], (mr_size_t)size);
    return (mr_int64_t)size;
}

static mr_int64_t test_boot_program(mr_boot_t *boot, mr_uint64_t offset,
                                    const void *data, mr_uint64_t size) {
    test_boot_store_t *store = boot->priv;

    /* Inject program failures */
    store->program_calls++;
    if (store->program_error != 0) {
        return store->program_error;
    }
    if (store->program_fault && (offset == store->program_fault_offset)) {
        return store->program_fault_error;
    }
    if (!test_boot_contains(offset, size)) {
        return -MR_EINVAL;
    }
    if ((store->program_short >= 0)
        && ((mr_uint64_t)store->program_short < size)) {
        size = (mr_uint64_t)store->program_short;
    }
    mr_memcpy(&store->image[(mr_size_t)offset], data, (mr_size_t)size);
    if (store->corrupt_program && (size != 0U)) {
        store->image[(mr_size_t)offset] ^= 0x01U;
    }
    if (store->reset_armed && (offset == store->reset_offset)) {
        store->reset_armed = MR_FALSE;
        longjmp(store->reset_env, 1);
    }
    return (mr_int64_t)size;
}

static mr_err_t test_boot_erase(mr_boot_t *boot, mr_uint64_t offset,
                                mr_uint64_t size) {
    test_boot_store_t *store = boot->priv;

    /* Inject erase failures before touching storage */
    store->erase_calls++;
    if (store->erase_error != 0) {
        return store->erase_error;
    }
    if ((store->erase_fail_at != 0U)
        && (store->erase_calls == store->erase_fail_at)) {
        return -MR_EIO;
    }
    if (!test_boot_contains(offset, size)) {
        return -MR_EINVAL;
    }
    mr_memset(&store->image[(mr_size_t)offset], 0xFF, (mr_size_t)size);
    return 0;
}

static mr_ssize_t test_boot_sn_read(mr_boot_t *boot, void *data,
                                    mr_size_t size) {
    test_boot_store_t *store = boot->priv;
    mr_size_t count;

    /* Read the stored serial number */
    if (store->sn_error != 0) {
        return store->sn_error;
    }
    count = store->sn_size;
    if (count > size) {
        count = size;
    }
    mr_memcpy(data, store->sn, count);
    return (mr_ssize_t)count;
}

static mr_ssize_t test_boot_sn_write(mr_boot_t *boot, const void *data,
                                     mr_size_t size) {
    test_boot_store_t *store = boot->priv;

    /* Replace the stored serial number */
    if (store->sn_error != 0) {
        return store->sn_error;
    }
    if (size > sizeof(store->sn)) {
        return -MR_ERANGE;
    }
    mr_memcpy(store->sn, data, size);
    store->sn_size = size;
    return (mr_ssize_t)size;
}

static mr_boot_source_t test_boot_source(mr_boot_t *boot) {
    test_boot_store_t *store = boot->priv;

    return store->source;
}

static mr_err_t test_boot_rollback_read(mr_boot_t *boot, mr_uint32_t *version) {
    test_boot_store_t *store = boot->priv;

    if (store->rollback_read_error != 0) {
        return store->rollback_read_error;
    }
    *version = store->rollback;
    return 0;
}

static mr_err_t test_boot_rollback_update(mr_boot_t *boot,
                                          mr_uint32_t version) {
    test_boot_store_t *store = boot->priv;

    store->rollback_updates++;
    if (store->rollback_update_error != 0) {
        return store->rollback_update_error;
    }
    store->rollback = version;
    return 0;
}

static mr_err_t test_boot_jump(mr_boot_t *boot, mr_uint64_t offset) {
    test_boot_store_t *store = boot->priv;

    /* A returning jump is a failed boot; success leaves mr_boot_start */
    store->jump_calls++;
    store->jump_offset = offset;
    if (store->jump_fail_left != 0U) {
        store->jump_fail_left--;
        return -MR_EIO;
    }
    if (store->boot_armed) {
        store->boot_armed = MR_FALSE;
        longjmp(store->boot_env, 1);
    }
    return -MR_EIO;
}

static void test_boot_digest(const void *data, mr_size_t size,
                             mr_uint8_t digest[MR_BOOT_SHA256_SIZE]) {
    const mr_uint8_t *bytes = data;
    mr_uint8_t acc;
    mr_size_t i;

    mr_memset(digest, 0, MR_BOOT_SHA256_SIZE);
    acc = 0U;
    for (i = 0U; i < size; i++) {
        acc = (mr_uint8_t)(acc ^ bytes[i]);
    }
    digest[0] = acc;
    digest[1] = (mr_uint8_t)size;
    digest[2] = (mr_uint8_t)(size >> 8);
}

static void test_boot_auth_tag(const void *data, mr_size_t size,
                               mr_uint8_t tag[MR_BOOT_SHA256_SIZE]) {
    const mr_uint8_t *bytes = data;
    mr_size_t i;

    mr_memset(tag, 0, MR_BOOT_SHA256_SIZE);
    tag[0] = 0x5CU;
    for (i = 0U; i < size; i++) {
        tag[0] = (mr_uint8_t)(tag[0] ^ bytes[i]);
        tag[1] = (mr_uint8_t)(tag[1] + bytes[i]);
    }
}

#if defined(MR_USE_MODULE_BOOT_SHA256)
static mr_err_t test_boot_hash_begin(mr_boot_t *boot) {
    test_boot_store_t *store = boot->priv;

    if (store->hash_error != 0) {
        return store->hash_error;
    }
    store->hash_begins++;
    store->hash_acc = 0U;
    store->hash_len = 0U;
    return 0;
}

static mr_err_t test_boot_hash_update(mr_boot_t *boot, const void *data,
                                      mr_size_t size) {
    test_boot_store_t *store = boot->priv;
    const mr_uint8_t *bytes = data;
    mr_size_t i;

    if (store->hash_error != 0) {
        return store->hash_error;
    }
    for (i = 0U; i < size; i++) {
        store->hash_acc = (mr_uint8_t)(store->hash_acc ^ bytes[i]);
    }
    store->hash_len += size;
    store->hash_bytes += (mr_uint32_t)size;
    return 0;
}

static mr_err_t test_boot_hash_end(mr_boot_t *boot,
                                   mr_uint8_t digest[MR_BOOT_SHA256_SIZE]) {
    test_boot_store_t *store = boot->priv;

    if (store->hash_error != 0) {
        return store->hash_error;
    }
    mr_memset(digest, 0, MR_BOOT_SHA256_SIZE);
    digest[0] = store->hash_acc;
    digest[1] = (mr_uint8_t)store->hash_len;
    digest[2] = (mr_uint8_t)(store->hash_len >> 8);
    return 0;
}
#endif /* defined(MR_USE_MODULE_BOOT_SHA256) */

#if defined(MR_USE_MODULE_BOOT_ECDSA_P256)
static void test_boot_sign_manifest(mr_boot_manifest_t *manifest) {
    mr_uint8_t digest[MR_BOOT_SHA256_SIZE];

    test_boot_digest(manifest, sizeof(*manifest) - sizeof(manifest->signature),
                     digest);
    manifest->signature[0] = TEST_BOOT_SIGNATURE;
    manifest->signature[1] = digest[0];
    manifest->signature[2] = digest[1];
    manifest->signature[3] = digest[2];
}

static mr_err_t
    test_boot_verify(mr_boot_t *boot, mr_uint8_t key_id,
                     const mr_uint8_t digest[MR_BOOT_SHA256_SIZE],
                     const mr_uint8_t signature[MR_BOOT_ECDSA_P256_SIZE]) {
    test_boot_store_t *store = boot->priv;

    store->verify_calls++;
    if (store->verify_error != 0) {
        return store->verify_error;
    }
    if ((key_id != 1U) || (signature[0] != TEST_BOOT_SIGNATURE)
        || (signature[1] != digest[0]) || (signature[2] != digest[1])
        || (signature[3] != digest[2])) {
        return -MR_EINVAL;
    }
    return 0;
}
#endif /* defined(MR_USE_MODULE_BOOT_ECDSA_P256) */

#if defined(MR_USE_MODULE_BOOT_AUTH)
static mr_err_t test_boot_auth(mr_boot_t *boot, const void *data,
                               mr_size_t size,
                               mr_uint8_t auth[MR_BOOT_SHA256_SIZE]) {
    test_boot_store_t *store = boot->priv;

    if (store->auth_error != 0) {
        return store->auth_error;
    }
    test_boot_auth_tag(data, size, auth);
    return 0;
}
#endif /* defined(MR_USE_MODULE_BOOT_AUTH) */

static void test_boot_ops_init(mr_boot_ops_t *ops) {
    /* Host storage has no hardware rollback counter */
    ops->read = test_boot_read;
    ops->program = test_boot_program;
    ops->erase = test_boot_erase;
    ops->sn_read = test_boot_sn_read;
    ops->sn_write = test_boot_sn_write;
    ops->source = test_boot_source;
    ops->rollback_read = MR_NULL;
    ops->rollback_update = MR_NULL;
    ops->jump = test_boot_jump;
#if defined(MR_USE_MODULE_BOOT_SHA256)
    ops->hash_begin = test_boot_hash_begin;
    ops->hash_update = test_boot_hash_update;
    ops->hash_end = test_boot_hash_end;
#endif /* defined(MR_USE_MODULE_BOOT_SHA256) */
#if defined(MR_USE_MODULE_BOOT_ECDSA_P256)
    ops->verify = test_boot_verify;
#endif /* defined(MR_USE_MODULE_BOOT_ECDSA_P256) */
#if defined(MR_USE_MODULE_BOOT_AUTH)
    ops->auth = test_boot_auth;
#endif /* defined(MR_USE_MODULE_BOOT_AUTH) */
}

static void test_boot_config_set(mr_boot_config_t *config) {
    mr_memset(config, 0, sizeof(*config));
    config->product_id = TEST_BOOT_PRODUCT_ID;
    config->slot_a_offset = TEST_BOOT_SLOT_A;
    config->slot_a_size = TEST_BOOT_SLOT_SIZE;
    config->slot_b_offset = TEST_BOOT_SLOT_B;
    config->slot_b_size = TEST_BOOT_SLOT_SIZE;
    config->state_a_offset = TEST_BOOT_STATE_A;
    config->state_b_offset = TEST_BOOT_STATE_B;
    config->state_size = TEST_BOOT_STATE_SIZE;
#if defined(MR_USE_MODULE_BOOT_RECOVERY)
    config->recovery_offset = TEST_BOOT_RECOVERY;
    config->recovery_size = TEST_BOOT_SLOT_SIZE;
#endif /* defined(MR_USE_MODULE_BOOT_RECOVERY) */
}

static mr_err_t test_boot_open_config(mr_boot_t *boot, test_boot_store_t *store,
                                      const mr_boot_ops_t *ops,
                                      const mr_boot_config_t *config) {
    mr_memset(boot, 0, sizeof(*boot));
    return mr_boot_init(boot, config, ops, store);
}

static mr_err_t test_boot_open(mr_boot_t *boot, test_boot_store_t *store,
                               const mr_boot_ops_t *ops) {
    mr_boot_config_t config;

    test_boot_config_set(&config);
    return test_boot_open_config(boot, store, ops, &config);
}

static void test_boot_close(mr_boot_t *boot, mr_err_t ret) {
    if (ret == 0) {
        mr_boot_del(boot);
    }
}

static mr_err_t test_boot_boot(mr_boot_t *boot) {
    test_boot_store_t *store = boot->priv;
    mr_err_t ret;

    /* Success never returns from jump */
    if (setjmp(store->boot_env) != 0) {
        return 0;
    }
    store->boot_armed = MR_TRUE;
    ret = mr_boot_start(boot);
    store->boot_armed = MR_FALSE;
    return ret;
}

static void test_boot_manifest_set(mr_boot_manifest_t *manifest,
                                   mr_uint32_t product_id, mr_uint32_t security,
                                   mr_uint64_t image_size) {
    mr_memset(manifest, 0, sizeof(*manifest));
    manifest->product_id = product_id;
    manifest->firmware_version = 3U;
    manifest->security_version = security;
    manifest->image_size = image_size;
    manifest->key_id = 1U;
}

static void test_boot_seal(mr_boot_manifest_t *manifest, const void *image,
                           mr_size_t size) {
    manifest->image_size = size;
#if defined(MR_USE_MODULE_BOOT_SHA256)
    test_boot_digest(image, size, manifest->image_hash);
#else
    (void)image;
#endif /* defined(MR_USE_MODULE_BOOT_SHA256) */
#if defined(MR_USE_MODULE_BOOT_ECDSA_P256)
    test_boot_sign_manifest(manifest);
#endif /* defined(MR_USE_MODULE_BOOT_ECDSA_P256) */
}

static void test_boot_plant_state(test_boot_store_t *store, mr_size_t offset,
                                  mr_uint32_t sequence, mr_uint32_t security,
                                  mr_uint8_t active, mr_uint8_t invalid_a,
                                  mr_uint8_t invalid_b) {
    mr_boot_state_t state;

    mr_memset(&state, 0, sizeof(state));
    state.sequence = sequence;
    state.security_version = security;
    state.active_slot = active;
    state.invalid[MR_BOOT_SLOT_A] = invalid_a;
    state.invalid[MR_BOOT_SLOT_B] = invalid_b;
#if defined(MR_USE_MODULE_BOOT_AUTH)
    test_boot_auth_tag(&state, sizeof(state) - sizeof(state.auth), state.auth);
#endif /* defined(MR_USE_MODULE_BOOT_AUTH) */
    mr_memcpy(&store->image[offset], &state, sizeof(state));
}

static void test_boot_plant_image(test_boot_store_t *store, mr_size_t offset,
                                  mr_uint32_t product_id, mr_uint32_t security,
                                  mr_uint64_t image_size) {
    mr_boot_manifest_t manifest;
    mr_size_t count;

    test_boot_manifest_set(&manifest, product_id, security, image_size);
    count = sizeof(test_boot_payload);
    if ((mr_uint64_t)count > image_size) {
        count = (mr_size_t)image_size;
    }
    if ((count != 0U) && (image_size == (mr_uint64_t)count)) {
        test_boot_seal(&manifest, test_boot_payload, count);
    }
    mr_memcpy(&store->image[offset], &manifest, sizeof(manifest));
    if (count != 0U) {
        mr_memcpy(&store->image[offset + sizeof(manifest)], test_boot_payload,
                  count);
    }
}

static void test_boot_load_state(const test_boot_store_t *store,
                                 mr_size_t offset, mr_boot_state_t *state) {
    mr_memcpy(state, &store->image[offset], sizeof(*state));
}

static void test_boot_expect_state(volatile mr_uint32_t *__test_status,
                                   const mr_boot_state_t *state,
                                   mr_uint32_t sequence, mr_uint32_t security,
                                   mr_uint8_t active, mr_uint8_t invalid_a,
                                   mr_uint8_t invalid_b) {
    MR_TEST_EXPECT_EQ_UINT(state->sequence, sequence);
    MR_TEST_EXPECT_EQ_UINT(state->security_version, security);
    MR_TEST_EXPECT_EQ_UINT(state->active_slot, active);
    MR_TEST_EXPECT_EQ_UINT(state->invalid[MR_BOOT_SLOT_A], invalid_a);
    MR_TEST_EXPECT_EQ_UINT(state->invalid[MR_BOOT_SLOT_B], invalid_b);
}

static void test_boot_expect_image(volatile mr_uint32_t *__test_status,
                                   const test_boot_store_t *store,
                                   mr_size_t offset, mr_uint32_t security) {
    mr_boot_manifest_t manifest;

    mr_memcpy(&manifest, &store->image[offset], sizeof(manifest));
    MR_TEST_EXPECT_EQ_UINT(manifest.product_id, TEST_BOOT_PRODUCT_ID);
    MR_TEST_EXPECT_EQ_UINT(manifest.firmware_version, 3U);
    MR_TEST_EXPECT_EQ_UINT(manifest.security_version, security);
    MR_TEST_EXPECT_EQ_UINT(manifest.image_size, sizeof(test_boot_payload));
    MR_TEST_EXPECT_EQ_UINT(manifest.key_id, 1U);
    MR_TEST_EXPECT_EQ_INT(mr_memcmp(&store->image[offset + sizeof(manifest)],
                                    test_boot_payload,
                                    sizeof(test_boot_payload)),
                          0);
}

static void test_boot_expect_blank(volatile mr_uint32_t *__test_status,
                                   const test_boot_store_t *store,
                                   mr_size_t offset) {
    MR_TEST_EXPECT_EQ_UINT(store->image[offset], 0xFFU);
}

MR_TEST(boot, config_range) {
    test_boot_store_t store;
    mr_boot_ops_t ops;
    mr_boot_config_t config;
    mr_boot_manifest_t manifest;
    mr_boot_t boot;
    mr_err_t ret;

    test_boot_store_reset(&store);
    test_boot_ops_init(&ops);
    test_boot_config_set(&config);

    /* Slot and state must hold their records */
    config.slot_a_size = sizeof(mr_boot_manifest_t) - 1U;
    ret = test_boot_open_config(&boot, &store, &ops, &config);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ERANGE);
    test_boot_close(&boot, ret);

    test_boot_config_set(&config);
    config.state_size = sizeof(mr_boot_state_t) - 1U;
    ret = test_boot_open_config(&boot, &store, &ops, &config);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ERANGE);
    test_boot_close(&boot, ret);

    /* A region may end on the last byte, but not past it */
    test_boot_config_set(&config);
    config.slot_a_offset = MR_UINT64_MAX - TEST_BOOT_SLOT_SIZE + 1U;
    ret = test_boot_open_config(&boot, &store, &ops, &config);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_ERANGE);
    test_boot_close(&boot, ret);

    /* Minimum lengths are accepted, and then reject a one-byte image */
    test_boot_config_set(&config);
    config.slot_a_size = sizeof(mr_boot_manifest_t);
    config.slot_b_offset = sizeof(mr_boot_manifest_t);
    config.slot_b_size = sizeof(mr_boot_manifest_t);
    config.state_size = sizeof(mr_boot_state_t);
    config.state_b_offset = config.state_a_offset + config.state_size;
    ret = test_boot_open_config(&boot, &store, &ops, &config);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        test_boot_manifest_set(&manifest, TEST_BOOT_PRODUCT_ID, 0U, 1U);
        MR_TEST_EXPECT_EQ_INT(mr_boot_update_begin(&boot, &manifest),
                              -MR_ERANGE);
        MR_TEST_EXPECT_EQ_INT(boot.update.active, MR_FALSE);
        mr_boot_del(&boot);
    }
}

MR_TEST(boot, config_overlap) {
    test_boot_store_t store;
    mr_boot_ops_t ops;
    mr_boot_config_t config;
    mr_boot_t boot;
    mr_err_t ret;

    test_boot_store_reset(&store);
    test_boot_ops_init(&ops);

    /* Slots may touch and still be distinct */
    test_boot_config_set(&config);
    config.slot_b_offset = 100U;
    ret = test_boot_open_config(&boot, &store, &ops, &config);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EINVAL);
    test_boot_close(&boot, ret);

    test_boot_config_set(&config);
    config.state_a_offset = TEST_BOOT_SLOT_B;
    ret = test_boot_open_config(&boot, &store, &ops, &config);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EINVAL);
    test_boot_close(&boot, ret);

    test_boot_config_set(&config);
    config.state_b_offset = config.state_a_offset + 1U;
    ret = test_boot_open_config(&boot, &store, &ops, &config);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EINVAL);
    test_boot_close(&boot, ret);

    test_boot_config_set(&config);
    ret = test_boot_open_config(&boot, &store, &ops, &config);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    test_boot_close(&boot, ret);
}

MR_TEST(boot, lifecycle) {
    test_boot_store_t store;
    mr_boot_ops_t ops;
    mr_boot_config_t config;
    mr_boot_t boot;
    mr_boot_t *created;
    mr_err_t ret;

    /* Blank storage boots slot A without writing */
    test_boot_store_reset(&store);
    test_boot_ops_init(&ops);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_PTR(boot.ops, &ops);
    MR_TEST_EXPECT_EQ_PTR(boot.priv, &store);
    MR_TEST_EXPECT_EQ_UINT(boot.config.product_id, TEST_BOOT_PRODUCT_ID);
    MR_TEST_EXPECT_EQ_INT(boot.launch.slot, MR_BOOT_SLOT_MAX);
    MR_TEST_EXPECT_EQ_INT(boot.update.active, MR_FALSE);
    test_boot_expect_state(__test_status, &boot.state, 0U, 0U,
                           (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    MR_TEST_EXPECT_EQ_UINT(store.erase_calls, 0U);
    MR_TEST_EXPECT_EQ_UINT(store.program_calls, 0U);
    MR_TEST_EXPECT_EQ_INT(mr_boot_del(&boot), 0);
    MR_TEST_EXPECT_EQ_PTR(boot.ops, MR_NULL);
    MR_TEST_EXPECT_EQ_PTR(boot.priv, MR_NULL);
    MR_TEST_EXPECT_EQ_INT(boot.launch.slot, MR_BOOT_SLOT_MAX);
    MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_INITED(&boot), MR_FALSE);

    /* Dynamic object follows the same storage and reference rules */
    test_boot_store_reset(&store);
    test_boot_config_set(&config);
    created = mr_boot_create(&config, &ops, &store);
    MR_TEST_ASSERT_NE_PTR(created, MR_NULL);
    MR_TEST_EXPECT_EQ_PTR(created->priv, &store);
    MR_TEST_EXPECT_EQ_UINT(created->state.active_slot, MR_BOOT_SLOT_A);
    MR_TEST_EXPECT_EQ_PTR(mr_boot_get(created), created);
    MR_TEST_EXPECT_EQ_INT(mr_boot_put(created), MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(mr_boot_del(created), 0);

    /* Create rejects the same layout init rejects */
    test_boot_config_set(&config);
    config.slot_b_offset = 1U;
    created = mr_boot_create(&config, &ops, &store);
    MR_TEST_EXPECT_EQ_PTR(created, MR_NULL);
    if (created != MR_NULL) {
        mr_boot_del(created);
    }
}

MR_TEST(boot, state_selects_copy) {
    test_boot_store_t store;
    mr_boot_ops_t ops;
    mr_boot_t boot;
    mr_err_t ret;

    test_boot_ops_init(&ops);

    /* Newer copy wins, including across the sequence wrap */
    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 9U, 1U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    test_boot_plant_state(&store, TEST_BOOT_STATE_B, 4U, 2U,
                          (mr_uint8_t)MR_BOOT_SLOT_B, 0U, 0U);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_expect_state(__test_status, &boot.state, 9U, 1U,
                           (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    mr_boot_del(&boot);

    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 2U, 1U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    test_boot_plant_state(&store, TEST_BOOT_STATE_B, 5U, 4U,
                          (mr_uint8_t)MR_BOOT_SLOT_B, 0U, 1U);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_expect_state(__test_status, &boot.state, 5U, 4U,
                           (mr_uint8_t)MR_BOOT_SLOT_B, 0U, 1U);
    mr_boot_del(&boot);

    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 1U, 1U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    test_boot_plant_state(&store, TEST_BOOT_STATE_B, 0xFFFFFFF0U, 8U,
                          (mr_uint8_t)MR_BOOT_SLOT_B, 0U, 0U);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_expect_state(__test_status, &boot.state, 1U, 1U,
                           (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    mr_boot_del(&boot);

    /* A tie keeps the later copy */
    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 7U, 1U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    test_boot_plant_state(&store, TEST_BOOT_STATE_B, 7U, 4U,
                          (mr_uint8_t)MR_BOOT_SLOT_B, 1U, 0U);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_expect_state(__test_status, &boot.state, 7U, 4U,
                           (mr_uint8_t)MR_BOOT_SLOT_B, 1U, 0U);
    mr_boot_del(&boot);

    /* One usable copy is enough, and a bad slot name is not usable */
    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 3U, 6U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_expect_state(__test_status, &boot.state, 3U, 6U,
                           (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    mr_boot_del(&boot);

    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_B, 3U, 6U,
                          (mr_uint8_t)MR_BOOT_SLOT_B, 0U, 0U);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_expect_state(__test_status, &boot.state, 3U, 6U,
                           (mr_uint8_t)MR_BOOT_SLOT_B, 0U, 0U);
    mr_boot_del(&boot);

    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 3U, 1U,
                          (mr_uint8_t)MR_BOOT_SLOT_RECOVERY, 0U, 0U);
    test_boot_plant_state(&store, TEST_BOOT_STATE_B, 4U, 2U,
                          (mr_uint8_t)MR_BOOT_SLOT_B, 0U, 0U);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_expect_state(__test_status, &boot.state, 4U, 2U,
                           (mr_uint8_t)MR_BOOT_SLOT_B, 0U, 0U);
    mr_boot_del(&boot);

    test_boot_store_reset(&store);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_expect_state(__test_status, &boot.state, 0U, 0U,
                           (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    mr_boot_del(&boot);

    /* A hard read error is returned only when no copy is usable */
    test_boot_store_reset(&store);
    store.read_error = -MR_EIO;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EIO);
    test_boot_close(&boot, ret);

    test_boot_store_reset(&store);
    store.read_short = 1;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EIO);
    test_boot_close(&boot, ret);

    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_B, 8U, 3U,
                          (mr_uint8_t)MR_BOOT_SLOT_B, 0U, 0U);
    store.read_fault = MR_TRUE;
    store.read_fault_offset = TEST_BOOT_STATE_A;
    store.read_fault_error = -MR_EIO;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_expect_state(__test_status, &boot.state, 8U, 3U,
                           (mr_uint8_t)MR_BOOT_SLOT_B, 0U, 0U);
    mr_boot_del(&boot);

    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 8U, 3U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    store.read_fault = MR_TRUE;
    store.read_fault_offset = TEST_BOOT_STATE_B;
    store.read_fault_error = -MR_EIO;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_expect_state(__test_status, &boot.state, 8U, 3U,
                           (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    mr_boot_del(&boot);
}

MR_TEST(boot, rollback_counter) {
    test_boot_store_t store;
    mr_boot_ops_t ops;
    mr_boot_t boot;
    mr_err_t ret;

    /* Both rollback operations are required before the counter is used */
    test_boot_store_reset(&store);
    test_boot_ops_init(&ops);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 2U, 9U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    store.rollback = 3U;
    ops.rollback_read = test_boot_rollback_read;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_UINT(boot.state.security_version, 9U);
    MR_TEST_EXPECT_EQ_UINT(store.rollback_updates, 0U);
    mr_boot_del(&boot);

    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 2U, 9U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    store.rollback = 3U;
    ops.rollback_read = test_boot_rollback_read;
    ops.rollback_update = test_boot_rollback_update;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
#if defined(MR_USE_MODULE_BOOT_AUTH)
    /* Authenticated state ahead of the counter raises the counter */
    MR_TEST_EXPECT_EQ_UINT(boot.state.security_version, 9U);
    MR_TEST_EXPECT_EQ_UINT(store.rollback, 9U);
    MR_TEST_EXPECT_EQ_UINT(store.rollback_updates, 1U);
#else
    MR_TEST_EXPECT_EQ_UINT(boot.state.security_version, 3U);
    MR_TEST_EXPECT_EQ_UINT(store.rollback_updates, 0U);
#endif /* defined(MR_USE_MODULE_BOOT_AUTH) */
    MR_TEST_EXPECT_EQ_UINT(boot.state.sequence, 2U);
    mr_boot_del(&boot);

    /* A newer hardware counter replaces a lower authenticated version */
    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 2U, 3U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    store.rollback = 8U;
    ops.rollback_read = test_boot_rollback_read;
    ops.rollback_update = test_boot_rollback_update;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_UINT(boot.state.security_version, 8U);
    MR_TEST_EXPECT_EQ_UINT(store.rollback_updates, 0U);
    mr_boot_del(&boot);

    /* Blank storage takes the hardware counter and does not raise it */
    test_boot_store_reset(&store);
    store.rollback = 6U;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_UINT(boot.state.security_version, 6U);
    MR_TEST_EXPECT_EQ_UINT(store.rollback_updates, 0U);
    mr_boot_del(&boot);

#if defined(MR_USE_MODULE_BOOT_AUTH)
    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 2U, 9U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    store.rollback = 3U;
    store.rollback_update_error = -MR_EIO;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EIO);
    MR_TEST_EXPECT_EQ_UINT(store.rollback, 3U);
    test_boot_close(&boot, ret);
    store.rollback_update_error = 0;
#endif /* defined(MR_USE_MODULE_BOOT_AUTH) */

    store.rollback_read_error = -MR_EIO;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EIO);
    test_boot_close(&boot, ret);
}

MR_TEST(boot, update_commits) {
    test_boot_store_t store;
    mr_boot_ops_t ops;
    mr_boot_manifest_t manifest;
    mr_boot_state_t state_a, state_b;
    mr_boot_t boot;
    mr_err_t ret;

    test_boot_store_reset(&store);
    test_boot_ops_init(&ops);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    /* Active A, so the first image lands in B */
    test_boot_manifest_set(&manifest, TEST_BOOT_PRODUCT_ID, 1U,
                           sizeof(test_boot_payload));
    test_boot_seal(&manifest, test_boot_payload, sizeof(test_boot_payload));
    ret = mr_boot_update_begin(&boot, &manifest);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(boot.update.active, MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(boot.update.slot, MR_BOOT_SLOT_B);
    MR_TEST_EXPECT_EQ_UINT(boot.update.offset, TEST_BOOT_SLOT_B);
    ret = mr_boot_update_write(&boot, test_boot_payload, 3U);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    ret = mr_boot_update_write(&boot, &test_boot_payload[3],
                               sizeof(test_boot_payload) - 3U);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_UINT(boot.update.written, sizeof(test_boot_payload));
    ret = mr_boot_update_end(&boot);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(boot.update.active, MR_FALSE);
    test_boot_expect_image(__test_status, &store, TEST_BOOT_SLOT_B, 1U);
    test_boot_expect_blank(__test_status, &store, TEST_BOOT_SLOT_A);
    test_boot_expect_state(__test_status, &boot.state, 1U, 1U,
                           (mr_uint8_t)MR_BOOT_SLOT_B, 0U, 0U);

    /* The next image lands back in A and both state copies match */
    test_boot_manifest_set(&manifest, TEST_BOOT_PRODUCT_ID, 2U,
                           sizeof(test_boot_payload));
    test_boot_seal(&manifest, test_boot_payload, sizeof(test_boot_payload));
    ret = mr_boot_update_begin(&boot, &manifest);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(boot.update.slot, MR_BOOT_SLOT_A);
    ret = mr_boot_update_write(&boot, test_boot_payload,
                               sizeof(test_boot_payload));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    ret = mr_boot_update_end(&boot);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_expect_image(__test_status, &store, TEST_BOOT_SLOT_A, 2U);
    test_boot_expect_image(__test_status, &store, TEST_BOOT_SLOT_B, 1U);
    test_boot_expect_state(__test_status, &boot.state, 2U, 2U,
                           (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    test_boot_load_state(&store, TEST_BOOT_STATE_A, &state_a);
    test_boot_load_state(&store, TEST_BOOT_STATE_B, &state_b);
    test_boot_expect_state(__test_status, &state_a, 2U, 2U,
                           (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    test_boot_expect_state(__test_status, &state_b, 2U, 2U,
                           (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    MR_TEST_EXPECT_EQ_INT(mr_boot_update_end(&boot), -MR_EINVAL);
    mr_boot_del(&boot);
}

MR_TEST(boot, update_rejects) {
    test_boot_store_t store;
    mr_boot_ops_t ops;
    mr_boot_manifest_t manifest;
    mr_boot_t boot;
    mr_err_t ret;

    test_boot_store_reset(&store);
    test_boot_ops_init(&ops);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 1U, 5U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    test_boot_manifest_set(&manifest, TEST_BOOT_PRODUCT_ID + 1U, 5U, 1U);
    MR_TEST_EXPECT_EQ_INT(mr_boot_update_begin(&boot, &manifest), -MR_EINVAL);
    test_boot_manifest_set(&manifest, TEST_BOOT_PRODUCT_ID, 5U, 0U);
    MR_TEST_EXPECT_EQ_INT(mr_boot_update_begin(&boot, &manifest), -MR_EINVAL);
    test_boot_manifest_set(&manifest, TEST_BOOT_PRODUCT_ID, 4U, 1U);
    MR_TEST_EXPECT_EQ_INT(mr_boot_update_begin(&boot, &manifest), -MR_EPERM);
    test_boot_manifest_set(&manifest, TEST_BOOT_PRODUCT_ID, 5U,
                           TEST_BOOT_SLOT_SIZE);
    MR_TEST_EXPECT_EQ_INT(mr_boot_update_begin(&boot, &manifest), -MR_ERANGE);
    MR_TEST_EXPECT_EQ_INT(boot.update.active, MR_FALSE);

    /* The same security version may begin, and a second begin is busy */
    test_boot_manifest_set(&manifest, TEST_BOOT_PRODUCT_ID, 5U, 1U);
    test_boot_seal(&manifest, test_boot_payload, 1U);
    ret = mr_boot_update_begin(&boot, &manifest);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        MR_TEST_EXPECT_EQ_INT(mr_boot_update_begin(&boot, &manifest),
                              -MR_EBUSY);
        MR_TEST_EXPECT_EQ_INT(boot.update.active, MR_TRUE);
        mr_boot_update_abort(&boot);
    }
    mr_boot_del(&boot);

    test_boot_store_reset(&store);
    test_boot_ops_init(&ops);
    ops.erase = MR_NULL;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_manifest_set(&manifest, TEST_BOOT_PRODUCT_ID, 0U, 1U);
    MR_TEST_EXPECT_EQ_INT(mr_boot_update_begin(&boot, &manifest), -MR_ENOSYS);
    mr_boot_del(&boot);

    test_boot_store_reset(&store);
    test_boot_ops_init(&ops);
    ops.program = MR_NULL;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(mr_boot_update_begin(&boot, &manifest), -MR_ENOSYS);
    mr_boot_del(&boot);
}

MR_TEST(boot, update_failures) {
    test_boot_store_t store;
    mr_boot_ops_t ops;
    mr_boot_manifest_t manifest;
    mr_boot_state_t saved;
    mr_boot_t boot;
    mr_uint8_t chunk;
    mr_err_t ret;

    test_boot_store_reset(&store);
    test_boot_ops_init(&ops);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_manifest_set(&manifest, TEST_BOOT_PRODUCT_ID, 1U,
                           sizeof(test_boot_payload));
    test_boot_seal(&manifest, test_boot_payload, sizeof(test_boot_payload));

    /* Writes outside an active update are rejected */
    MR_TEST_EXPECT_EQ_INT(mr_boot_update_write(&boot, test_boot_payload, 1U),
                          -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(mr_boot_update_end(&boot), -MR_EINVAL);
    ret = mr_boot_update_begin(&boot, &manifest);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(mr_boot_update_write(&boot, test_boot_payload, 0U),
                          0);
    MR_TEST_EXPECT_EQ_UINT(boot.update.written, 0U);
    MR_TEST_EXPECT_EQ_INT(mr_boot_update_write(&boot, test_boot_payload,
                                               sizeof(test_boot_payload) + 1U),
                          -MR_ERANGE);
    MR_TEST_EXPECT_EQ_INT(boot.update.active, MR_TRUE);
    MR_TEST_EXPECT_EQ_UINT(boot.update.written, 0U);

    /* A short program drops the update */
    store.program_short = 1;
    ret = mr_boot_update_write(&boot, test_boot_payload,
                               sizeof(test_boot_payload));
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EIO);
    MR_TEST_EXPECT_EQ_INT(boot.update.active, MR_FALSE);
    store.program_short = -1;
    MR_TEST_EXPECT_EQ_INT(mr_boot_update_end(&boot), -MR_EINVAL);
    mr_boot_del(&boot);

    /* Abort leaves a partial image uncommitted */
    test_boot_store_reset(&store);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_boot_update_begin(&boot, &manifest);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    chunk = test_boot_payload[0];
    ret = mr_boot_update_write(&boot, &chunk, 1U);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(mr_boot_update_end(&boot), -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(boot.update.active, MR_TRUE);
    mr_boot_update_abort(&boot);
    MR_TEST_EXPECT_EQ_INT(boot.update.active, MR_FALSE);
    MR_TEST_EXPECT_EQ_INT(mr_boot_update_write(&boot, &chunk, 1U), -MR_EINVAL);
    MR_TEST_EXPECT_EQ_UINT(boot.state.sequence, 0U);
    mr_boot_del(&boot);

    /* Manifest program failure keeps the previous state */
    test_boot_store_reset(&store);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_boot_update_begin(&boot, &manifest);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_boot_update_write(&boot, test_boot_payload,
                               sizeof(test_boot_payload));
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    store.program_fault = MR_TRUE;
    store.program_fault_offset = TEST_BOOT_SLOT_B;
    store.program_fault_error = -MR_EIO;
    ret = mr_boot_update_end(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EIO);
    MR_TEST_EXPECT_EQ_INT(boot.update.active, MR_FALSE);
    test_boot_expect_state(__test_status, &boot.state, 0U, 0U,
                           (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    test_boot_expect_blank(__test_status, &store, TEST_BOOT_STATE_A);
    mr_boot_del(&boot);

    /* The first state copy still wins when the second erase fails */
    test_boot_store_reset(&store);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_boot_update_begin(&boot, &manifest);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_boot_update_write(&boot, test_boot_payload,
                               sizeof(test_boot_payload));
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    store.erase_calls = 0U;
    store.erase_fail_at = 2U;
    ret = mr_boot_update_end(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EIO);
    MR_TEST_EXPECT_EQ_INT(boot.update.active, MR_FALSE);
    test_boot_expect_state(__test_status, &boot.state, 1U, 1U,
                           (mr_uint8_t)MR_BOOT_SLOT_B, 0U, 0U);
    test_boot_load_state(&store, TEST_BOOT_STATE_A, &saved);
    test_boot_expect_state(__test_status, &saved, 1U, 1U,
                           (mr_uint8_t)MR_BOOT_SLOT_B, 0U, 0U);
    test_boot_expect_blank(__test_status, &store, TEST_BOOT_STATE_B);
    mr_boot_del(&boot);

    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_expect_state(__test_status, &boot.state, 1U, 1U,
                           (mr_uint8_t)MR_BOOT_SLOT_B, 0U, 0U);
    mr_boot_del(&boot);
}

MR_TEST(boot, select_slot) {
    test_boot_store_t store;
    mr_boot_ops_t ops;
    mr_boot_manifest_t manifest;
    mr_boot_t boot;
    mr_err_t ret;

    test_boot_store_reset(&store);
    test_boot_ops_init(&ops);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 1U, 2U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    test_boot_plant_image(&store, TEST_BOOT_SLOT_A, TEST_BOOT_PRODUCT_ID, 2U,
                          sizeof(test_boot_payload));
    test_boot_plant_image(&store, TEST_BOOT_SLOT_B, TEST_BOOT_PRODUCT_ID, 2U,
                          sizeof(test_boot_payload));
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    ret = mr_boot_select(&boot, MR_BOOT_SLOT_A);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(boot.launch.slot, MR_BOOT_SLOT_A);
    MR_TEST_EXPECT_EQ_UINT(boot.launch.offset,
                           TEST_BOOT_SLOT_A + sizeof(mr_boot_manifest_t));
    ret = mr_boot_select(&boot, MR_BOOT_SLOT_B);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(boot.launch.slot, MR_BOOT_SLOT_B);
    MR_TEST_EXPECT_EQ_INT(mr_boot_select(&boot, MR_BOOT_SLOT_MAX), -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(boot.launch.slot, MR_BOOT_SLOT_MAX);
#if defined(MR_USE_MODULE_BOOT_RECOVERY)
    MR_TEST_EXPECT_EQ_INT(mr_boot_select(&boot, MR_BOOT_SLOT_RECOVERY),
                          -MR_EINVAL);
#else
    MR_TEST_EXPECT_EQ_INT(mr_boot_select(&boot, MR_BOOT_SLOT_RECOVERY),
                          -MR_ENOSYS);
#endif /* defined(MR_USE_MODULE_BOOT_RECOVERY) */
    mr_boot_del(&boot);

    /* Manifest fields are checked before an image is launched */
    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 1U, 2U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 1U, 0U);
    test_boot_plant_image(&store, TEST_BOOT_SLOT_A, TEST_BOOT_PRODUCT_ID + 1U,
                          2U, sizeof(test_boot_payload));
    test_boot_plant_image(&store, TEST_BOOT_SLOT_B, TEST_BOOT_PRODUCT_ID, 1U,
                          sizeof(test_boot_payload));
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(mr_boot_select(&boot, MR_BOOT_SLOT_A), -MR_EPERM);
    MR_TEST_EXPECT_EQ_INT(mr_boot_select(&boot, MR_BOOT_SLOT_B), -MR_EPERM);
    mr_boot_del(&boot);

    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 1U, 2U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    test_boot_plant_image(&store, TEST_BOOT_SLOT_A, TEST_BOOT_PRODUCT_ID + 1U,
                          2U, sizeof(test_boot_payload));
    test_boot_plant_image(&store, TEST_BOOT_SLOT_B, TEST_BOOT_PRODUCT_ID, 2U,
                          0U);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(mr_boot_select(&boot, MR_BOOT_SLOT_A), -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(mr_boot_select(&boot, MR_BOOT_SLOT_B), -MR_EINVAL);
    test_boot_manifest_set(&manifest, TEST_BOOT_PRODUCT_ID, 2U,
                           TEST_BOOT_SLOT_SIZE);
    mr_memcpy(&store.image[TEST_BOOT_SLOT_A], &manifest, sizeof(manifest));
    MR_TEST_EXPECT_EQ_INT(mr_boot_select(&boot, MR_BOOT_SLOT_A), -MR_EINVAL);
    mr_boot_del(&boot);

    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 1U, 0U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(mr_boot_select(&boot, MR_BOOT_SLOT_A), -MR_EINVAL);
    store.read_error = -MR_EIO;
    MR_TEST_EXPECT_EQ_INT(mr_boot_select(&boot, MR_BOOT_SLOT_A), -MR_EIO);
    store.read_error = 0;
    store.read_short = 4;
    MR_TEST_EXPECT_EQ_INT(mr_boot_select(&boot, MR_BOOT_SLOT_A), -MR_EIO);
    mr_boot_del(&boot);

    /* A failed select still clears the previous launch */
    test_boot_store_reset(&store);
    test_boot_plant_image(&store, TEST_BOOT_SLOT_A, TEST_BOOT_PRODUCT_ID, 0U,
                          sizeof(test_boot_payload));
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_boot_select(&boot, MR_BOOT_SLOT_A);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_manifest_set(&manifest, TEST_BOOT_PRODUCT_ID, 0U, 1U);
    test_boot_seal(&manifest, test_boot_payload, 1U);
    ret = mr_boot_update_begin(&boot, &manifest);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(mr_boot_select(&boot, MR_BOOT_SLOT_B), -MR_EBUSY);
    MR_TEST_EXPECT_EQ_INT(boot.launch.slot, MR_BOOT_SLOT_MAX);
    mr_boot_del(&boot);
}

MR_TEST(boot, start_sources) {
    test_boot_store_t store;
    mr_boot_ops_t ops;
    mr_boot_manifest_t manifest;
    mr_boot_state_t saved;
    mr_boot_source_t source;
    mr_boot_t boot;
    mr_uint32_t jumps;
    mr_err_t ret;

    test_boot_store_reset(&store);
    test_boot_ops_init(&ops);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 4U, 1U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    test_boot_plant_image(&store, TEST_BOOT_SLOT_A, TEST_BOOT_PRODUCT_ID, 1U,
                          sizeof(test_boot_payload));
    test_boot_plant_image(&store, TEST_BOOT_SLOT_B, TEST_BOOT_PRODUCT_ID, 1U,
                          sizeof(test_boot_payload));
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    /* Power-on and restart stay on the active image */
    source = MR_BOOT_SOURCE_POWER_ON;
    while (source <= MR_BOOT_SOURCE_RESTART) {
        store.source = source;
        jumps = store.jump_calls;
        ret = test_boot_boot(&boot);
        MR_TEST_EXPECT_EQ_INT(ret, 0);
        MR_TEST_EXPECT_EQ_UINT(store.jump_calls, jumps + 1U);
        MR_TEST_EXPECT_EQ_UINT(store.jump_offset,
                               TEST_BOOT_SLOT_A + sizeof(mr_boot_manifest_t));
        MR_TEST_EXPECT_EQ_UINT(boot.state.sequence, 4U);
        source = (mr_boot_source_t)(source + 1);
    }

    store.source = MR_BOOT_SOURCE_RESCUE;
    jumps = store.jump_calls;
    MR_TEST_EXPECT_EQ_INT(mr_boot_start(&boot), 0);
    MR_TEST_EXPECT_EQ_UINT(store.jump_calls, jumps);
    MR_TEST_EXPECT_EQ_INT(boot.launch.slot, MR_BOOT_SLOT_MAX);

    store.source = MR_BOOT_SOURCE_MAX;
    MR_TEST_EXPECT_EQ_INT(mr_boot_start(&boot), -MR_EINVAL);
#if defined(MR_USE_MODULE_BOOT_RECOVERY)
    store.source = MR_BOOT_SOURCE_RECOVERY;
    MR_TEST_EXPECT_EQ_INT(mr_boot_start(&boot), 0);
    MR_TEST_EXPECT_EQ_INT(boot.launch.slot, MR_BOOT_SLOT_MAX);
    MR_TEST_EXPECT_EQ_UINT(boot.state.sequence, 4U);
#else
    store.source = MR_BOOT_SOURCE_RECOVERY;
    MR_TEST_EXPECT_EQ_INT(mr_boot_start(&boot), -MR_ENOSYS);
#endif /* defined(MR_USE_MODULE_BOOT_RECOVERY) */
    MR_TEST_EXPECT_EQ_UINT(store.jump_calls, jumps);
    mr_boot_del(&boot);

    /* Rollback commits the other slot before jumping */
    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 4U, 1U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    test_boot_plant_image(&store, TEST_BOOT_SLOT_A, TEST_BOOT_PRODUCT_ID, 1U,
                          sizeof(test_boot_payload));
    test_boot_plant_image(&store, TEST_BOOT_SLOT_B, TEST_BOOT_PRODUCT_ID, 1U,
                          sizeof(test_boot_payload));
    store.source = MR_BOOT_SOURCE_ROLLBACK;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = test_boot_boot(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_UINT(store.jump_offset,
                           TEST_BOOT_SLOT_B + sizeof(mr_boot_manifest_t));
    test_boot_expect_state(__test_status, &boot.state, 5U, 1U,
                           (mr_uint8_t)MR_BOOT_SLOT_B, 1U, 0U);
    test_boot_load_state(&store, TEST_BOOT_STATE_A, &saved);
    test_boot_expect_state(__test_status, &saved, 5U, 1U,
                           (mr_uint8_t)MR_BOOT_SLOT_B, 1U, 0U);
    mr_boot_del(&boot);

    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 4U, 1U,
                          (mr_uint8_t)MR_BOOT_SLOT_B, 0U, 0U);
    test_boot_plant_image(&store, TEST_BOOT_SLOT_A, TEST_BOOT_PRODUCT_ID, 1U,
                          sizeof(test_boot_payload));
    test_boot_plant_image(&store, TEST_BOOT_SLOT_B, TEST_BOOT_PRODUCT_ID, 1U,
                          sizeof(test_boot_payload));
    store.source = MR_BOOT_SOURCE_ROLLBACK;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = test_boot_boot(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_UINT(store.jump_offset,
                           TEST_BOOT_SLOT_A + sizeof(mr_boot_manifest_t));
    test_boot_expect_state(__test_status, &boot.state, 5U, 1U,
                           (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 1U);
    mr_boot_del(&boot);

    /* Rollback does not save when the other slot cannot boot */
    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 4U, 1U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 1U);
    test_boot_plant_image(&store, TEST_BOOT_SLOT_A, TEST_BOOT_PRODUCT_ID, 1U,
                          sizeof(test_boot_payload));
    test_boot_plant_image(&store, TEST_BOOT_SLOT_B, TEST_BOOT_PRODUCT_ID, 1U,
                          sizeof(test_boot_payload));
    store.source = MR_BOOT_SOURCE_ROLLBACK;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    jumps = store.jump_calls;
    MR_TEST_EXPECT_EQ_INT(mr_boot_start(&boot), -MR_EPERM);
    MR_TEST_EXPECT_EQ_UINT(store.jump_calls, jumps);
    MR_TEST_EXPECT_EQ_UINT(boot.state.sequence, 4U);
    test_boot_expect_blank(__test_status, &store, TEST_BOOT_STATE_B);
    ops.erase = MR_NULL;
    MR_TEST_EXPECT_EQ_INT(mr_boot_start(&boot), -MR_EPERM);
    mr_boot_del(&boot);

    test_boot_store_reset(&store);
    test_boot_ops_init(&ops);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 4U, 1U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    test_boot_plant_image(&store, TEST_BOOT_SLOT_B, TEST_BOOT_PRODUCT_ID, 1U,
                          sizeof(test_boot_payload));
    store.source = MR_BOOT_SOURCE_ROLLBACK;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ops.erase = MR_NULL;
    MR_TEST_EXPECT_EQ_INT(mr_boot_start(&boot), -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_UINT(store.jump_calls, 0U);
    MR_TEST_EXPECT_EQ_UINT(boot.state.sequence, 4U);
    mr_boot_del(&boot);

    /* Start refuses an open update and missing boot operations */
    test_boot_store_reset(&store);
    test_boot_ops_init(&ops);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_manifest_set(&manifest, TEST_BOOT_PRODUCT_ID, 0U, 1U);
    test_boot_seal(&manifest, test_boot_payload, 1U);
    ret = mr_boot_update_begin(&boot, &manifest);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(mr_boot_start(&boot), -MR_EBUSY);
    mr_boot_del(&boot);

    test_boot_store_reset(&store);
    test_boot_ops_init(&ops);
    ops.source = MR_NULL;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(mr_boot_start(&boot), -MR_ENOSYS);
    mr_boot_del(&boot);

    test_boot_store_reset(&store);
    test_boot_ops_init(&ops);
    ops.jump = MR_NULL;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(mr_boot_start(&boot), -MR_ENOSYS);
    mr_boot_del(&boot);
}

MR_TEST(boot, serial_number) {
    test_boot_store_t store;
    mr_boot_ops_t ops;
    mr_boot_t boot;
    mr_uint8_t sn[TEST_BOOT_SN_SIZE];
    const mr_uint8_t written[] = {0x53U, 0x4EU, 0x30U, 0x31U};
    mr_err_t ret;

    test_boot_store_reset(&store);
    test_boot_ops_init(&ops);
    ops.sn_read = MR_NULL;
    ops.sn_write = MR_NULL;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(mr_boot_sn_read(&boot, sn, sizeof(sn)), -MR_ENOSYS);
    MR_TEST_EXPECT_EQ_INT(mr_boot_sn_write(&boot, written, sizeof(written)),
                          -MR_ENOSYS);
    mr_boot_del(&boot);

    test_boot_store_reset(&store);
    test_boot_ops_init(&ops);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(mr_boot_sn_write(&boot, written, sizeof(written)),
                          (mr_ssize_t)sizeof(written));
    mr_memset(sn, 0, sizeof(sn));
    MR_TEST_EXPECT_EQ_INT(mr_boot_sn_read(&boot, sn, sizeof(sn)),
                          (mr_ssize_t)sizeof(written));
    MR_TEST_EXPECT_EQ_INT(mr_memcmp(sn, written, sizeof(written)), 0);
    store.sn_error = -MR_EIO;
    MR_TEST_EXPECT_EQ_INT(mr_boot_sn_read(&boot, sn, sizeof(sn)), -MR_EIO);
    MR_TEST_EXPECT_EQ_INT(mr_boot_sn_write(&boot, written, sizeof(written)),
                          -MR_EIO);
    mr_boot_del(&boot);
}

static void test_boot_plant_apps(test_boot_store_t *store, mr_uint8_t active) {
    test_boot_plant_state(store, TEST_BOOT_STATE_A, 4U, 1U, active, 0U, 0U);
    test_boot_plant_state(store, TEST_BOOT_STATE_B, 4U, 1U, active, 0U, 0U);
    test_boot_plant_image(store, TEST_BOOT_SLOT_A, TEST_BOOT_PRODUCT_ID, 1U,
                          sizeof(test_boot_payload));
    test_boot_plant_image(store, TEST_BOOT_SLOT_B, TEST_BOOT_PRODUCT_ID, 1U,
                          sizeof(test_boot_payload));
}

MR_TEST(boot, boot_fallback) {
    test_boot_store_t store;
    mr_boot_ops_t ops;
    mr_boot_state_t saved;
    mr_boot_t boot;
    mr_uint32_t sequence;
    mr_err_t ret;

    test_boot_ops_init(&ops);

    /* Active image missing: power-on activates the other APP before boot */
    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 4U, 1U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    test_boot_plant_image(&store, TEST_BOOT_SLOT_B, TEST_BOOT_PRODUCT_ID, 1U,
                          sizeof(test_boot_payload));
    store.source = MR_BOOT_SOURCE_POWER_ON;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = test_boot_boot(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_UINT(store.jump_offset,
                           TEST_BOOT_SLOT_B + sizeof(mr_boot_manifest_t));
    test_boot_expect_state(__test_status, &boot.state, 5U, 1U,
                           (mr_uint8_t)MR_BOOT_SLOT_B, 0U, 0U);
    mr_boot_del(&boot);

    /* Both APPs missing: recovery, or rescue when recovery is absent */
    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 4U, 1U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    store.source = MR_BOOT_SOURCE_RESTART;
#if defined(MR_USE_MODULE_BOOT_RECOVERY)
    test_boot_plant_image(&store, TEST_BOOT_RECOVERY, TEST_BOOT_PRODUCT_ID, 0U,
                          sizeof(test_boot_payload));
#endif /* defined(MR_USE_MODULE_BOOT_RECOVERY) */
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    sequence = boot.state.sequence;
    ret = test_boot_boot(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
#if defined(MR_USE_MODULE_BOOT_RECOVERY)
    MR_TEST_EXPECT_EQ_UINT(store.jump_offset,
                           TEST_BOOT_RECOVERY + sizeof(mr_boot_manifest_t));
    MR_TEST_EXPECT_EQ_UINT(store.jump_calls, 1U);
#else
    MR_TEST_EXPECT_EQ_UINT(store.jump_calls, 0U);
    MR_TEST_EXPECT_EQ_INT(boot.launch.slot, MR_BOOT_SLOT_MAX);
#endif /* defined(MR_USE_MODULE_BOOT_RECOVERY) */
    MR_TEST_EXPECT_EQ_UINT(boot.state.sequence, sequence);
    test_boot_load_state(&store, TEST_BOOT_STATE_A, &saved);
    MR_TEST_EXPECT_EQ_UINT(saved.sequence, 4U);
    mr_boot_del(&boot);

    /* Recovery image rejected: rescue, still without an A/B write */
    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 4U, 1U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    store.source = MR_BOOT_SOURCE_POWER_ON;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = test_boot_boot(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_UINT(store.jump_calls, 0U);
    MR_TEST_EXPECT_EQ_INT(boot.launch.slot, MR_BOOT_SLOT_MAX);
    test_boot_expect_blank(__test_status, &store, TEST_BOOT_STATE_B);
    mr_boot_del(&boot);

    /* Watchdog retires the active APP before the other APP runs */
    test_boot_store_reset(&store);
    test_boot_plant_apps(&store, (mr_uint8_t)MR_BOOT_SLOT_A);
    store.source = MR_BOOT_SOURCE_WATCHDOG;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = test_boot_boot(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_UINT(store.jump_offset,
                           TEST_BOOT_SLOT_B + sizeof(mr_boot_manifest_t));
    test_boot_expect_state(__test_status, &boot.state, 6U, 1U,
                           (mr_uint8_t)MR_BOOT_SLOT_B, 1U, 0U);
    mr_boot_del(&boot);

    /* Watchdog still retires the active APP when the other APP is missing */
    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 4U, 1U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    test_boot_plant_image(&store, TEST_BOOT_SLOT_A, TEST_BOOT_PRODUCT_ID, 1U,
                          sizeof(test_boot_payload));
    store.source = MR_BOOT_SOURCE_WATCHDOG;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = test_boot_boot(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    test_boot_expect_state(__test_status, &boot.state, 5U, 1U,
                           (mr_uint8_t)MR_BOOT_SLOT_A, 1U, 0U);
    MR_TEST_EXPECT_EQ_INT(boot.launch.slot, MR_BOOT_SLOT_MAX);
    mr_boot_del(&boot);

    /* Update source adopts a verified inactive image */
    test_boot_store_reset(&store);
    test_boot_plant_apps(&store, (mr_uint8_t)MR_BOOT_SLOT_A);
    store.source = MR_BOOT_SOURCE_UPDATE;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = test_boot_boot(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_UINT(store.jump_offset,
                           TEST_BOOT_SLOT_B + sizeof(mr_boot_manifest_t));
    test_boot_expect_state(__test_status, &boot.state, 5U, 1U,
                           (mr_uint8_t)MR_BOOT_SLOT_B, 0U, 0U);
    mr_boot_del(&boot);

    /* A bad staged image is retired before the old APP continues */
    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 4U, 1U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    test_boot_plant_image(&store, TEST_BOOT_SLOT_A, TEST_BOOT_PRODUCT_ID, 1U,
                          sizeof(test_boot_payload));
    store.source = MR_BOOT_SOURCE_UPDATE;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = test_boot_boot(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_UINT(store.jump_offset,
                           TEST_BOOT_SLOT_A + sizeof(mr_boot_manifest_t));
    test_boot_expect_state(__test_status, &boot.state, 5U, 1U,
                           (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 1U);
    mr_boot_del(&boot);

    /* The first APP jump to return is retired; the other APP runs */
    test_boot_store_reset(&store);
    test_boot_plant_apps(&store, (mr_uint8_t)MR_BOOT_SLOT_A);
    store.source = MR_BOOT_SOURCE_POWER_ON;
    store.jump_fail_left = 1U;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = test_boot_boot(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_UINT(store.jump_calls, 2U);
    MR_TEST_EXPECT_EQ_UINT(store.jump_offset,
                           TEST_BOOT_SLOT_B + sizeof(mr_boot_manifest_t));
    test_boot_expect_state(__test_status, &boot.state, 6U, 1U,
                           (mr_uint8_t)MR_BOOT_SLOT_B, 1U, 0U);
    mr_boot_del(&boot);

    /* Every APP and recovery jump returning ends in rescue */
    test_boot_store_reset(&store);
    test_boot_plant_apps(&store, (mr_uint8_t)MR_BOOT_SLOT_A);
#if defined(MR_USE_MODULE_BOOT_RECOVERY)
    test_boot_plant_image(&store, TEST_BOOT_RECOVERY, TEST_BOOT_PRODUCT_ID, 0U,
                          sizeof(test_boot_payload));
#endif /* defined(MR_USE_MODULE_BOOT_RECOVERY) */
    store.source = MR_BOOT_SOURCE_POWER_ON;
    store.jump_fail_left = 4U;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = test_boot_boot(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(boot.launch.slot, MR_BOOT_SLOT_MAX);
    test_boot_load_state(&store, TEST_BOOT_STATE_A, &saved);
    MR_TEST_EXPECT_EQ_UINT(saved.invalid[MR_BOOT_SLOT_A], 1U);
    MR_TEST_EXPECT_EQ_UINT(saved.invalid[MR_BOOT_SLOT_B], 1U);
    mr_boot_del(&boot);
}

#if defined(MR_USE_MODULE_BOOT_RECOVERY)
MR_TEST(boot, recovery_slot) {
    test_boot_store_t store;
    mr_boot_ops_t ops;
    mr_boot_manifest_t manifest;
    mr_boot_state_t saved;
    mr_boot_t boot;
    mr_err_t ret;

    test_boot_ops_init(&ops);

    /* Recovery ignores the APP security floor and does not save state */
    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 4U, 9U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    test_boot_plant_image(&store, TEST_BOOT_RECOVERY, TEST_BOOT_PRODUCT_ID, 1U,
                          sizeof(test_boot_payload));
    store.source = MR_BOOT_SOURCE_RECOVERY;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_boot_select(&boot, MR_BOOT_SLOT_RECOVERY);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(boot.launch.slot, MR_BOOT_SLOT_RECOVERY);
    MR_TEST_EXPECT_EQ_UINT(boot.state.sequence, 4U);
    ret = test_boot_boot(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_UINT(store.jump_offset,
                           TEST_BOOT_RECOVERY + sizeof(mr_boot_manifest_t));
    MR_TEST_EXPECT_EQ_UINT(boot.state.sequence, 4U);
    test_boot_load_state(&store, TEST_BOOT_STATE_A, &saved);
    MR_TEST_EXPECT_EQ_UINT(saved.sequence, 4U);
    MR_TEST_EXPECT_EQ_UINT(saved.active_slot, MR_BOOT_SLOT_A);
    mr_boot_del(&boot);

    /* A bad recovery manifest becomes rescue */
    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 4U, 1U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    test_boot_manifest_set(&manifest, TEST_BOOT_PRODUCT_ID + 1U, 1U,
                           sizeof(test_boot_payload));
    mr_memcpy(&store.image[TEST_BOOT_RECOVERY], &manifest, sizeof(manifest));
    store.source = MR_BOOT_SOURCE_RECOVERY;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = test_boot_boot(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_UINT(store.jump_calls, 0U);
    MR_TEST_EXPECT_EQ_INT(boot.launch.slot, MR_BOOT_SLOT_MAX);
    MR_TEST_EXPECT_EQ_UINT(boot.state.sequence, 4U);
    mr_boot_del(&boot);

    /* A recovery jump that returns also becomes rescue */
    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 4U, 1U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    test_boot_plant_image(&store, TEST_BOOT_RECOVERY, TEST_BOOT_PRODUCT_ID, 1U,
                          sizeof(test_boot_payload));
    store.source = MR_BOOT_SOURCE_RECOVERY;
    store.jump_fail_left = 1U;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = test_boot_boot(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_UINT(store.jump_calls, 1U);
    MR_TEST_EXPECT_EQ_INT(boot.launch.slot, MR_BOOT_SLOT_MAX);
    test_boot_load_state(&store, TEST_BOOT_STATE_A, &saved);
    MR_TEST_EXPECT_EQ_UINT(saved.sequence, 4U);
    MR_TEST_EXPECT_EQ_UINT(saved.invalid[MR_BOOT_SLOT_A], 0U);
    mr_boot_del(&boot);
}
#endif /* defined(MR_USE_MODULE_BOOT_RECOVERY) */

#if defined(MR_USE_MODULE_BOOT_SHA256)
MR_TEST(boot, image_integrity) {
    test_boot_store_t store;
    mr_boot_ops_t ops;
    mr_boot_manifest_t manifest;
    mr_boot_t boot;
    mr_err_t ret;

    test_boot_ops_init(&ops);
    test_boot_store_reset(&store);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);

    /* A wrong image hash is rejected before the slot becomes active */
    test_boot_manifest_set(&manifest, TEST_BOOT_PRODUCT_ID, 1U,
                           sizeof(test_boot_payload));
#if defined(MR_USE_MODULE_BOOT_ECDSA_P256)
    test_boot_sign_manifest(&manifest);
#endif /* defined(MR_USE_MODULE_BOOT_ECDSA_P256) */
    ret = mr_boot_update_begin(&boot, &manifest);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_boot_update_write(&boot, test_boot_payload,
                               sizeof(test_boot_payload));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    ret = mr_boot_update_end(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EINVAL);
    MR_TEST_EXPECT_EQ_UINT(boot.state.active_slot, MR_BOOT_SLOT_A);
    mr_boot_del(&boot);

    /* program() can report a full write while changing a stored byte */
    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 2U, 0U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    test_boot_plant_image(&store, TEST_BOOT_SLOT_A, TEST_BOOT_PRODUCT_ID, 0U,
                          sizeof(test_boot_payload));
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_manifest_set(&manifest, TEST_BOOT_PRODUCT_ID, 1U,
                           sizeof(test_boot_payload));
    test_boot_seal(&manifest, test_boot_payload, sizeof(test_boot_payload));
    ret = mr_boot_update_begin(&boot, &manifest);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    store.corrupt_program = MR_TRUE;
    ret = mr_boot_update_write(&boot, test_boot_payload,
                               sizeof(test_boot_payload));
    store.corrupt_program = MR_FALSE;
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    ret = mr_boot_update_end(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EINVAL);
    MR_TEST_EXPECT_EQ_INT(boot.update.active, MR_FALSE);
    MR_TEST_EXPECT_EQ_UINT(boot.state.active_slot, MR_BOOT_SLOT_A);
    MR_TEST_EXPECT_EQ_UINT(boot.state.sequence, 2U);
    mr_boot_del(&boot);

    /* A planted image with a bad hash does not select */
    test_boot_store_reset(&store);
    test_boot_plant_image(&store, TEST_BOOT_SLOT_A, TEST_BOOT_PRODUCT_ID, 1U,
                          sizeof(test_boot_payload));
    store.image[TEST_BOOT_SLOT_A + offsetof(mr_boot_manifest_t, image_hash)]
        ^= 0x01U;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    store.hash_begins = 0U;
    ret = mr_boot_select(&boot, MR_BOOT_SLOT_A);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EINVAL);
    mr_boot_del(&boot);
}
#endif /* defined(MR_USE_MODULE_BOOT_SHA256) */

#if defined(MR_USE_MODULE_BOOT_ECDSA_P256)
MR_TEST(boot, manifest_signature) {
    test_boot_store_t store;
    mr_boot_ops_t ops;
    mr_boot_manifest_t manifest;
    mr_boot_t boot;
    mr_err_t ret;

    test_boot_ops_init(&ops);
    test_boot_store_reset(&store);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_manifest_set(&manifest, TEST_BOOT_PRODUCT_ID, 1U,
                           sizeof(test_boot_payload));
    test_boot_digest(test_boot_payload, sizeof(test_boot_payload),
                     manifest.image_hash);
    store.verify_calls = 0U;
    store.erase_calls = 0U;
    ret = mr_boot_update_begin(&boot, &manifest);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EINVAL);
    MR_TEST_EXPECT_EQ_UINT(store.verify_calls, 1U);
    MR_TEST_EXPECT_EQ_UINT(store.erase_calls, 0U);
    MR_TEST_EXPECT_EQ_INT(boot.update.active, MR_FALSE);
    mr_boot_del(&boot);

    test_boot_store_reset(&store);
    test_boot_plant_image(&store, TEST_BOOT_SLOT_A, TEST_BOOT_PRODUCT_ID, 1U,
                          sizeof(test_boot_payload));
    store.image[TEST_BOOT_SLOT_A + offsetof(mr_boot_manifest_t, signature)]
        = 0U;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_INT(mr_boot_select(&boot, MR_BOOT_SLOT_A), -MR_EINVAL);
    mr_boot_del(&boot);
}
#endif /* defined(MR_USE_MODULE_BOOT_ECDSA_P256) */

#if defined(MR_USE_MODULE_BOOT_AUTH)
MR_TEST(boot, state_auth) {
    test_boot_store_t store;
    mr_boot_ops_t ops;
    mr_boot_t boot;
    mr_err_t ret;

    test_boot_ops_init(&ops);

    /* A tampered copy is ignored; the intact copy remains */
    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 3U, 1U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    test_boot_plant_state(&store, TEST_BOOT_STATE_B, 8U, 2U,
                          (mr_uint8_t)MR_BOOT_SLOT_B, 0U, 0U);
    store.image[TEST_BOOT_STATE_B] ^= 0x01U;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_expect_state(__test_status, &boot.state, 3U, 1U,
                           (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    mr_boot_del(&boot);

    /* Both copies tampered looks like empty storage */
    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 3U, 1U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    store.image[TEST_BOOT_STATE_A] ^= 0x01U;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_expect_state(__test_status, &boot.state, 0U, 0U,
                           (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    mr_boot_del(&boot);

    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 3U, 1U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    store.auth_error = -MR_EIO;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EIO);
    test_boot_close(&boot, ret);
}
#endif /* defined(MR_USE_MODULE_BOOT_AUTH) */

#if defined(MR_USE_MODULE_BOOT_FAST)
MR_TEST(boot, fast_cache) {
    test_boot_store_t store;
    mr_boot_ops_t ops;
    mr_boot_manifest_t manifest;
    mr_boot_state_t state;
    mr_uint8_t digest[MR_BOOT_SHA256_SIZE];
    mr_boot_t boot;
    mr_err_t ret;

    test_boot_ops_init(&ops);
    test_boot_store_reset(&store);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_manifest_set(&manifest, TEST_BOOT_PRODUCT_ID, 1U,
                           sizeof(test_boot_payload));
    test_boot_seal(&manifest, test_boot_payload, sizeof(test_boot_payload));
    ret = mr_boot_update_begin(&boot, &manifest);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_boot_update_write(&boot, test_boot_payload,
                               sizeof(test_boot_payload));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    ret = mr_boot_update_end(&boot);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_digest(&manifest, sizeof(manifest), digest);
    MR_TEST_EXPECT_EQ_INT(mr_memcmp(boot.state.manifest_digest[MR_BOOT_SLOT_B],
                                    digest, sizeof(digest)),
                          0);
    mr_boot_del(&boot);

    /* An unchanged manifest skips the image hash and the signature */
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    store.hash_begins = 0U;
    store.hash_bytes = 0U;
    store.verify_calls = 0U;
    ret = mr_boot_select(&boot, MR_BOOT_SLOT_B);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_UINT(store.hash_begins, 1U);
    MR_TEST_EXPECT_EQ_UINT(store.hash_bytes, sizeof(manifest));
    MR_TEST_EXPECT_EQ_UINT(store.verify_calls, 0U);
    mr_boot_del(&boot);

    /* A stale cache misses while the unchanged manifest still verifies */
    test_boot_load_state(&store, TEST_BOOT_STATE_A, &state);
    state.manifest_digest[MR_BOOT_SLOT_B][0] ^= 0x01U;
#if defined(MR_USE_MODULE_BOOT_AUTH)
    test_boot_auth_tag(&state, sizeof(state) - sizeof(state.auth), state.auth);
#endif /* defined(MR_USE_MODULE_BOOT_AUTH) */
    mr_memcpy(&store.image[TEST_BOOT_STATE_A], &state, sizeof(state));
    mr_memcpy(&store.image[TEST_BOOT_STATE_B], &state, sizeof(state));
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    store.hash_begins = 0U;
    store.verify_calls = 0U;
    ret = mr_boot_select(&boot, MR_BOOT_SLOT_B);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_GT_UINT(store.hash_begins, 1U);
#if defined(MR_USE_MODULE_BOOT_ECDSA_P256)
    MR_TEST_EXPECT_GT_UINT(store.verify_calls, 0U);
#endif /* defined(MR_USE_MODULE_BOOT_ECDSA_P256) */
    mr_boot_del(&boot);
}
#endif /* defined(MR_USE_MODULE_BOOT_FAST) */

MR_TEST(boot, power_loss) {
    test_boot_store_t store;
    mr_boot_ops_t ops;
    mr_boot_manifest_t manifest;
    mr_boot_state_t saved;
    mr_boot_t boot;
    mr_err_t ret;

    test_boot_ops_init(&ops);

    /* Half an image, then a reset, still boots the old APP */
    test_boot_store_reset(&store);
    test_boot_plant_state(&store, TEST_BOOT_STATE_A, 4U, 1U,
                          (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    test_boot_plant_image(&store, TEST_BOOT_SLOT_A, TEST_BOOT_PRODUCT_ID, 1U,
                          sizeof(test_boot_payload));
    store.source = MR_BOOT_SOURCE_POWER_ON;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_manifest_set(&manifest, TEST_BOOT_PRODUCT_ID, 2U,
                           sizeof(test_boot_payload));
    test_boot_seal(&manifest, test_boot_payload, sizeof(test_boot_payload));
    ret = mr_boot_update_begin(&boot, &manifest);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_boot_update_write(&boot, test_boot_payload, 1U);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    mr_boot_del(&boot);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = test_boot_boot(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_UINT(store.jump_offset,
                           TEST_BOOT_SLOT_A + sizeof(mr_boot_manifest_t));
    MR_TEST_EXPECT_EQ_UINT(boot.state.sequence, 4U);
    mr_boot_del(&boot);

    /* A finished update is the image that runs after the next init */
    test_boot_store_reset(&store);
    store.source = MR_BOOT_SOURCE_POWER_ON;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_manifest_set(&manifest, TEST_BOOT_PRODUCT_ID, 1U,
                           sizeof(test_boot_payload));
    test_boot_seal(&manifest, test_boot_payload, sizeof(test_boot_payload));
    ret = mr_boot_update_begin(&boot, &manifest);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_boot_update_write(&boot, test_boot_payload,
                               sizeof(test_boot_payload));
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    ret = mr_boot_update_end(&boot);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    mr_boot_del(&boot);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = test_boot_boot(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_UINT(boot.state.active_slot, MR_BOOT_SLOT_B);
    MR_TEST_EXPECT_EQ_UINT(store.jump_offset,
                           TEST_BOOT_SLOT_B + sizeof(mr_boot_manifest_t));
    mr_boot_del(&boot);

    /* Manifest is stored, then power fails before state A is touched */
    test_boot_store_reset(&store);
    test_boot_plant_apps(&store, (mr_uint8_t)MR_BOOT_SLOT_A);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_boot_update_begin(&boot, &manifest);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_boot_update_write(&boot, test_boot_payload,
                               sizeof(test_boot_payload));
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (setjmp(store.reset_env) == 0) {
        store.reset_offset = TEST_BOOT_SLOT_B;
        store.reset_armed = MR_TRUE;
        mr_boot_update_end(&boot);
        store.reset_armed = MR_FALSE;
    }
    mr_boot_del(&boot);
    store.source = MR_BOOT_SOURCE_POWER_ON;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = test_boot_boot(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_UINT(boot.state.active_slot, MR_BOOT_SLOT_A);
    MR_TEST_EXPECT_EQ_UINT(store.jump_offset,
                           TEST_BOOT_SLOT_A + sizeof(mr_boot_manifest_t));
    mr_boot_del(&boot);

    /* state A erase fails: the new manifest is not committed */
    test_boot_store_reset(&store);
    test_boot_plant_apps(&store, (mr_uint8_t)MR_BOOT_SLOT_A);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_boot_update_begin(&boot, &manifest);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_boot_update_write(&boot, test_boot_payload,
                               sizeof(test_boot_payload));
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    store.erase_calls = 0U;
    store.erase_fail_at = 1U;
    ret = mr_boot_update_end(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EIO);
    mr_boot_del(&boot);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_expect_state(__test_status, &boot.state, 4U, 1U,
                           (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    mr_boot_del(&boot);

    /* state A program fails after its erase: the older copy still boots */
    test_boot_store_reset(&store);
    test_boot_plant_apps(&store, (mr_uint8_t)MR_BOOT_SLOT_A);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_boot_update_begin(&boot, &manifest);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_boot_update_write(&boot, test_boot_payload,
                               sizeof(test_boot_payload));
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    store.program_fault = MR_TRUE;
    store.program_fault_offset = TEST_BOOT_STATE_A;
    store.program_fault_error = -MR_EIO;
    ret = mr_boot_update_end(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EIO);
    mr_boot_del(&boot);
    store.program_fault = MR_FALSE;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_expect_state(__test_status, &boot.state, 4U, 1U,
                           (mr_uint8_t)MR_BOOT_SLOT_A, 0U, 0U);
    store.source = MR_BOOT_SOURCE_POWER_ON;
    ret = test_boot_boot(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_UINT(store.jump_offset,
                           TEST_BOOT_SLOT_A + sizeof(mr_boot_manifest_t));
    mr_boot_del(&boot);

    /* state B program fails: the first copy is enough after the next init */
    test_boot_store_reset(&store);
    test_boot_plant_apps(&store, (mr_uint8_t)MR_BOOT_SLOT_A);
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_boot_update_begin(&boot, &manifest);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    ret = mr_boot_update_write(&boot, test_boot_payload,
                               sizeof(test_boot_payload));
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    store.program_fault = MR_TRUE;
    store.program_fault_offset = TEST_BOOT_STATE_B;
    store.program_fault_error = -MR_EIO;
    ret = mr_boot_update_end(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, -MR_EIO);
    mr_boot_del(&boot);
    store.program_fault = MR_FALSE;
    store.source = MR_BOOT_SOURCE_POWER_ON;
    ret = test_boot_open(&boot, &store, &ops);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    test_boot_load_state(&store, TEST_BOOT_STATE_A, &saved);
    test_boot_expect_state(__test_status, &saved, 5U, 1U,
                           (mr_uint8_t)MR_BOOT_SLOT_B, 0U, 0U);
    ret = test_boot_boot(&boot);
    MR_TEST_EXPECT_EQ_INT(ret, 0);
    MR_TEST_EXPECT_EQ_UINT(boot.state.active_slot, MR_BOOT_SLOT_B);
    MR_TEST_EXPECT_EQ_UINT(store.jump_offset,
                           TEST_BOOT_SLOT_B + sizeof(mr_boot_manifest_t));
    mr_boot_del(&boot);
}
#endif /* defined(MR_USE_MODULE_BOOT) */
#endif /* defined(MR_USE_TEST) */

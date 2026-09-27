/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-21        MacRsh        First version
 */

#ifndef __MR_MODULE_BOOT_H__
#define __MR_MODULE_BOOT_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_MODULE_BOOT)
#include <mrX/kernel/mr_object.h>
#endif /* defined(MR_USE_MODULE_BOOT) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Boot
 * @{
 */

#if defined(MR_USE_MODULE_BOOT)
struct mr_boot;

/* Boot cryptographic size definition */
#define MR_BOOT_SHA256_SIZE             (32U)
#define MR_BOOT_ECDSA_P256_SIZE         (64U)

/* Boot source definition */
typedef enum mr_boot_source {
    MR_BOOT_SOURCE_POWER_ON,
    MR_BOOT_SOURCE_RESTART,
    MR_BOOT_SOURCE_WATCHDOG,
    MR_BOOT_SOURCE_UPDATE,
    MR_BOOT_SOURCE_ROLLBACK,
    MR_BOOT_SOURCE_RECOVERY,
    MR_BOOT_SOURCE_RESCUE,
    MR_BOOT_SOURCE_MAX,
} mr_boot_source_t;

/* Boot slot definition */
typedef enum mr_boot_slot {
    MR_BOOT_SLOT_A,
    MR_BOOT_SLOT_B,
    MR_BOOT_SLOT_RECOVERY,
    MR_BOOT_SLOT_MAX,
} mr_boot_slot_t;

/* Boot manifest type */
typedef struct mr_boot_manifest {
    mr_uint32_t product_id;
    mr_uint32_t firmware_version;
    mr_uint32_t security_version;
    mr_uint64_t image_size;
    mr_uint8_t key_id;
    mr_uint8_t image_hash[MR_BOOT_SHA256_SIZE];
    mr_uint8_t signature[MR_BOOT_ECDSA_P256_SIZE];
} MR_PACKED mr_boot_manifest_t;

/* Boot configuration type */
typedef struct mr_boot_config {
    mr_uint32_t product_id;
    mr_uint64_t slot_a_offset;
    mr_uint64_t slot_a_size;
    mr_uint64_t slot_b_offset;
    mr_uint64_t slot_b_size;
#if defined(MR_USE_MODULE_BOOT_RECOVERY)
    mr_uint64_t recovery_offset;
    mr_uint64_t recovery_size;
#endif /* defined(MR_USE_MODULE_BOOT_RECOVERY) */
    mr_uint64_t state_a_offset;
    mr_uint64_t state_b_offset;
    mr_uint64_t state_size;
} mr_boot_config_t;

/* Boot state type */
typedef struct mr_boot_state {
    mr_uint32_t sequence;
    mr_uint32_t security_version;
#if defined(MR_USE_MODULE_BOOT_FAST)
    mr_uint8_t manifest_digest[MR_BOOT_SLOT_MAX][MR_BOOT_SHA256_SIZE];
#endif /* defined(MR_USE_MODULE_BOOT_FAST) */
    mr_uint8_t invalid[MR_BOOT_SLOT_MAX];
    mr_uint8_t active_slot;
#if defined(MR_USE_MODULE_BOOT_AUTH)
    mr_uint8_t auth[MR_BOOT_SHA256_SIZE];
#endif /* defined(MR_USE_MODULE_BOOT_AUTH) */
} MR_PACKED mr_boot_state_t;

/* Boot update type */
typedef struct mr_boot_update {
    mr_boot_manifest_t manifest;
    mr_uint64_t offset;
    mr_uint64_t size;
    mr_uint64_t written;
    mr_boot_slot_t slot;
    mr_bool_t active;
} mr_boot_update_t;

/* Boot launch type */
typedef struct mr_boot_launch {
    mr_boot_slot_t slot;
    mr_uint64_t offset;
} mr_boot_launch_t;

#if defined(MR_USE_MODULE_BOOT_SHA256)
/* Boot verification type */
typedef struct mr_boot_verify {
#if !defined(MR_CFG_BOOT_VERIFY_BUFFER_SIZE)
#define MR_CFG_BOOT_VERIFY_BUFFER_SIZE  (256U)
#endif /* !defined(MR_CFG_BOOT_VERIFY_BUFFER_SIZE) */
    mr_uint8_t buffer[MR_CFG_BOOT_VERIFY_BUFFER_SIZE];
} mr_boot_verify_t;
#endif /* defined(MR_USE_MODULE_BOOT_SHA256) */

/* Boot operation type */
typedef struct mr_boot_ops {
    mr_int64_t (*read)(struct mr_boot *boot, mr_uint64_t offset, void *data,
                       mr_uint64_t size);
    mr_int64_t (*program)(struct mr_boot *boot, mr_uint64_t offset,
                          const void *data, mr_uint64_t size);
    mr_err_t (*erase)(struct mr_boot *boot, mr_uint64_t offset,
                      mr_uint64_t size);
    mr_ssize_t (*sn_read)(struct mr_boot *boot, void *data, mr_size_t size);
    mr_ssize_t (*sn_write)(struct mr_boot *boot, const void *data,
                           mr_size_t size);
#if defined(MR_USE_MODULE_BOOT_SHA256)
    mr_err_t (*hash_begin)(struct mr_boot *boot);
    mr_err_t (*hash_update)(struct mr_boot *boot, const void *data,
                            mr_size_t size);
    mr_err_t (*hash_end)(struct mr_boot *boot,
                         mr_uint8_t digest[MR_BOOT_SHA256_SIZE]);
#endif /* defined(MR_USE_MODULE_BOOT_SHA256) */
#if defined(MR_USE_MODULE_BOOT_AUTH)
    mr_err_t (*auth)(struct mr_boot *boot, const void *data, mr_size_t size,
                     mr_uint8_t auth[MR_BOOT_SHA256_SIZE]);
#endif /* defined(MR_USE_MODULE_BOOT_AUTH) */
#if defined(MR_USE_MODULE_BOOT_ECDSA_P256)
    mr_err_t (*verify)(struct mr_boot *boot, mr_uint8_t key_id,
                       const mr_uint8_t digest[MR_BOOT_SHA256_SIZE],
                       const mr_uint8_t signature[MR_BOOT_ECDSA_P256_SIZE]);
#endif /* defined(MR_USE_MODULE_BOOT_ECDSA_P256) */
    mr_boot_source_t (*source)(struct mr_boot *boot);
    mr_err_t (*rollback_read)(struct mr_boot *boot, mr_uint32_t *version);
    mr_err_t (*rollback_update)(struct mr_boot *boot, mr_uint32_t version);
    mr_err_t (*jump)(struct mr_boot *boot, mr_uint64_t offset);
} mr_boot_ops_t;

/* Boot object type */
MR_CLAZZES_EXPORT(boot);
typedef struct mr_boot {
    mr_object_t parent;
    mr_boot_config_t config;
    mr_boot_state_t state;
    mr_boot_update_t update;
    mr_boot_launch_t launch;
#if defined(MR_USE_MODULE_BOOT_SHA256)
    mr_boot_verify_t verify;
#endif /* defined(MR_USE_MODULE_BOOT_SHA256) */
    const mr_boot_ops_t *ops;
    void *priv;
} mr_boot_t;

/**
 * @brief This function initializes an Boot object.
 *
 * @param boot The Boot object.
 * @param config The Boot configuration.
 * @param ops The platform operations.
 * @param priv The platform private data.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_boot_init(mr_boot_t *boot, const mr_boot_config_t *config,
                      const mr_boot_ops_t *ops, void *priv);

/**
 * @brief This function creates an Boot object.
 *
 * @param config The Boot configuration.
 * @param ops The platform operations.
 * @param priv The platform private data.
 * @return The Boot object on success, MR_NULL on failure.
 */
mr_boot_t *mr_boot_create(const mr_boot_config_t *config,
                          const mr_boot_ops_t *ops, void *priv);

/**
 * @brief This function deletes an Boot object.
 *
 * @param boot The Boot object.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_boot_del(mr_boot_t *boot);

/**
 * @brief This function begins an update.
 *
 * @param boot The Boot object.
 * @param manifest The firmware manifest.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_boot_update_begin(mr_boot_t *boot, mr_boot_manifest_t *manifest);

/**
 * @brief This function writes the next sequential firmware chunk.
 *
 * @param boot The Boot object.
 * @param data The firmware bytes.
 * @param size The firmware byte count.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_boot_update_write(mr_boot_t *boot, const void *data,
                              mr_size_t size);

/**
 * @brief This function verifies and commits the current update.
 *
 * @param boot The Boot object.
 * @return 0 on success, a negative error code on failure.
 *
 * @note A successful update becomes the active APP slot.
 */
mr_err_t mr_boot_update_end(mr_boot_t *boot);

/**
 * @brief This function aborts the current update.
 *
 * @param boot The Boot object.
 */
void mr_boot_update_abort(mr_boot_t *boot);

/**
 * @brief This function reads the device SN.
 *
 * @param boot The Boot object.
 * @param data The SN buffer.
 * @param size The buffer size.
 * @return The number of bytes read on success, a negative error code on
 * failure.
 */
mr_ssize_t mr_boot_sn_read(mr_boot_t *boot, void *data, mr_size_t size);

/**
 * @brief This function writes the device SN.
 *
 * @param boot The Boot object.
 * @param data The SN data.
 * @param size The SN byte count.
 * @return The number of bytes written on success, a negative error code on
 * failure.
 */
mr_ssize_t mr_boot_sn_write(mr_boot_t *boot, const void *data, mr_size_t size);

/**
 * @brief This function selects and prepares a boot slot.
 *
 * @param boot The Boot object.
 * @param slot The boot slot.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_boot_select(mr_boot_t *boot, mr_boot_slot_t slot);

/**
 * @brief This function starts Boot according to the current source and state.
 *
 * @param boot The Boot object.
 * @return 0 on success, a negative error code on failure.
 *
 * @note A return from jump is a failed boot. An APP failure is retired and
 * the search continues with the other APP, then recovery, then rescue.
 * Watchdog retires the active APP before that search. An update source tries
 * the inactive APP first and keeps the old APP when that image does not
 * verify. Rescue, and a recovery boot, do not rewrite the A/B record.
 */
mr_err_t mr_boot_start(mr_boot_t *boot);

/**
 * @brief This function gets an Boot object.
 *
 * @param boot The Boot object.
 * @return The Boot object on success, MR_NULL on failure.
 */
MR_INLINE mr_boot_t *mr_boot_get(mr_boot_t *boot) {
    /* Get Boot reference */
    return (mr_boot_t *)mr_object_get((mr_object_t *)boot);
}

/**
 * @brief This function puts an Boot object.
 *
 * @param boot The Boot object.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_boot_put(mr_boot_t *boot) {
    /* Put Boot reference */
    return mr_object_put((mr_object_t *)boot);
}
#endif /* defined(MR_USE_MODULE_BOOT) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_MODULE_BOOT_H__ */

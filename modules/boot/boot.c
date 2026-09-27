/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-22        MacRsh        First version
 */

#include <mrX/modules/boot/mr_boot.h>
#if defined(MR_USE_MODULE_BOOT)
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>

/* Boot clazzes(static & dynamic) */
static void boot_statik_release(mr_object_t *obj);
static void boot_dynamic_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(boot, boot_statik_release, boot_dynamic_release, MR_NULL);

static mr_bool_t boot_region_is_valid(mr_uint64_t offset, mr_uint64_t size,
                                      mr_uint64_t min_size) {
    return ((size >= min_size) && (offset <= (MR_UINT64_MAX - size)));
}

static mr_bool_t boot_region_overlaps(mr_uint64_t a_offset, mr_uint64_t a_size,
                                      mr_uint64_t b_offset,
                                      mr_uint64_t b_size) {
    if (a_offset <= b_offset) {
        return (a_size > (b_offset - a_offset));
    }
    return (b_size > (a_offset - b_offset));
}

static mr_err_t boot_config_check(const mr_boot_config_t *config) {
    const mr_uint64_t manifest_size = sizeof(mr_boot_manifest_t);
    const mr_uint64_t state_size = sizeof(mr_boot_state_t);

    /* Check region bounds */
    if ((!boot_region_is_valid(config->slot_a_offset, config->slot_a_size,
                               manifest_size))
        || (!boot_region_is_valid(config->slot_b_offset, config->slot_b_size,
                                  manifest_size))
        || (!boot_region_is_valid(config->state_a_offset, config->state_size,
                                  state_size))
        || (!boot_region_is_valid(config->state_b_offset, config->state_size,
                                  state_size))) {
        return -MR_ERANGE;
    }
#if defined(MR_USE_MODULE_BOOT_RECOVERY)
    if (!boot_region_is_valid(config->recovery_offset, config->recovery_size,
                              manifest_size)) {
        return -MR_ERANGE;
    }
#endif /* defined(MR_USE_MODULE_BOOT_RECOVERY) */

    /* Check region overlap */
    if (boot_region_overlaps(config->slot_a_offset, config->slot_a_size,
                             config->slot_b_offset, config->slot_b_size)
        || boot_region_overlaps(config->slot_a_offset, config->slot_a_size,
                                config->state_a_offset, config->state_size)
        || boot_region_overlaps(config->slot_a_offset, config->slot_a_size,
                                config->state_b_offset, config->state_size)
        || boot_region_overlaps(config->slot_b_offset, config->slot_b_size,
                                config->state_a_offset, config->state_size)
        || boot_region_overlaps(config->slot_b_offset, config->slot_b_size,
                                config->state_b_offset, config->state_size)
        || boot_region_overlaps(config->state_a_offset, config->state_size,
                                config->state_b_offset, config->state_size)) {
        return -MR_EINVAL;
    }
#if defined(MR_USE_MODULE_BOOT_RECOVERY)
    if (boot_region_overlaps(config->recovery_offset, config->recovery_size,
                             config->slot_a_offset, config->slot_a_size)
        || boot_region_overlaps(config->recovery_offset, config->recovery_size,
                                config->slot_b_offset, config->slot_b_size)
        || boot_region_overlaps(config->recovery_offset, config->recovery_size,
                                config->state_a_offset, config->state_size)
        || boot_region_overlaps(config->recovery_offset, config->recovery_size,
                                config->state_b_offset, config->state_size)) {
        return -MR_EINVAL;
    }
#endif /* defined(MR_USE_MODULE_BOOT_RECOVERY) */
    return 0;
}

static mr_err_t boot_read(mr_boot_t *boot, mr_uint64_t offset, void *data,
                          mr_uint64_t size) {
    mr_int64_t ret;

    /* Read data */
    ret = boot->ops->read(boot, offset, data, size);
    if (ret < 0) {
        return (mr_err_t)ret;
    }
    if ((mr_uint64_t)ret != size) {
        return -MR_EIO;
    }
    return 0;
}

static mr_err_t boot_program(mr_boot_t *boot, mr_uint64_t offset,
                             const void *data, mr_uint64_t size) {
    mr_int64_t ret;

    /* Program data */
    ret = boot->ops->program(boot, offset, data, size);
    if (ret < 0) {
        return (mr_err_t)ret;
    }
    if ((mr_uint64_t)ret != size) {
        return -MR_EIO;
    }
    return 0;
}

#if defined(MR_USE_MODULE_BOOT_AUTH) || defined(MR_USE_MODULE_BOOT_SHA256)
static mr_bool_t boot_bytes_equal(const mr_uint8_t *a, const mr_uint8_t *b,
                                  mr_size_t size) {
    mr_uint8_t diff;

    /* Compare without an early exit */
    diff = 0U;
    while (size != 0U) {
        size--;
        diff |= (mr_uint8_t)(a[size] ^ b[size]);
    }
    return (diff == 0U);
}
#endif /* defined(MR_USE_MODULE_BOOT_AUTH) ||                                  \
          defined(MR_USE_MODULE_BOOT_SHA256) */

#if defined(MR_USE_MODULE_BOOT_SHA256)
static mr_err_t boot_hash(mr_boot_t *boot, const void *data, mr_size_t size,
                          mr_uint8_t digest[MR_BOOT_SHA256_SIZE]) {
    mr_err_t ret;

    /* Hash data */
    ret = boot->ops->hash_begin(boot);
    if (ret != 0) {
        return ret;
    }
    ret = boot->ops->hash_update(boot, data, size);
    if (ret != 0) {
        return ret;
    }
    return boot->ops->hash_end(boot, digest);
}

static mr_err_t boot_hash_image(mr_boot_t *boot, mr_uint64_t offset,
                                mr_uint64_t size,
                                mr_uint8_t digest[MR_BOOT_SHA256_SIZE]) {
    mr_size_t chunk;
    mr_err_t ret;

    /* Start image hash */
    ret = boot->ops->hash_begin(boot);
    if (ret != 0) {
        return ret;
    }

    /* Hash image */
    while (size != 0U) {
        chunk = (size > sizeof(boot->verify.buffer))
                  ? sizeof(boot->verify.buffer)
                  : (mr_size_t)size;
        ret = boot_read(boot, offset, boot->verify.buffer, chunk);
        if (ret != 0) {
            return ret;
        }
        ret = boot->ops->hash_update(boot, boot->verify.buffer, chunk);
        if (ret != 0) {
            return ret;
        }
        offset += chunk;
        size -= chunk;
    }
    return boot->ops->hash_end(boot, digest);
}
#endif /* defined(MR_USE_MODULE_BOOT_SHA256) */

static mr_err_t boot_state_check(mr_boot_t *boot, mr_boot_state_t *state) {
#if defined(MR_USE_MODULE_BOOT_AUTH)
    mr_uint8_t tag[MR_BOOT_SHA256_SIZE];
    mr_size_t size;
    mr_err_t ret;

    /* Authenticate every byte before the tag */
    size = sizeof(*state) - sizeof(state->auth);
    ret = boot->ops->auth(boot, state, size, tag);
    if (ret != 0) {
        mr_memset(tag, 0, sizeof(tag));
        return ret;
    }
    if (!boot_bytes_equal(state->auth, tag, sizeof(tag))) {
        ret = -MR_ENOENT;
    } else {
        ret = 0;
    }
    mr_memset(tag, 0, sizeof(tag));
    if (ret != 0) {
        return ret;
    }
#else
    (void)boot;
#endif /* defined(MR_USE_MODULE_BOOT_AUTH) */

    /* Active slot names an APP */
    if ((state->active_slot != (mr_uint8_t)MR_BOOT_SLOT_A)
        && (state->active_slot != (mr_uint8_t)MR_BOOT_SLOT_B)) {
        return -MR_ENOENT;
    }
    return 0;
}

static mr_bool_t boot_sequence_is_newer(mr_uint32_t a, mr_uint32_t b) {
    mr_uint32_t delta;

    /* Ahead inside half of the sequence space */
    delta = a - b;
    return ((delta != 0U) && (delta < 0x80000000U));
}

static mr_err_t boot_rollback_sync(mr_boot_t *boot, mr_bool_t state_loaded) {
    mr_uint32_t version;
    mr_err_t ret;

    /* Use state when no hardware rollback counter is available */
    if ((!boot->ops->rollback_read) || (!boot->ops->rollback_update)) {
        return 0;
    }

    /* Read the rollback counter */
    ret = boot->ops->rollback_read(boot, &version);
    if (ret != 0) {
        return ret;
    }
#if defined(MR_USE_MODULE_BOOT_AUTH)
    /* Raise a counter that lags an authenticated version */
    if (state_loaded && (boot->state.security_version > version)) {
        ret = boot->ops->rollback_update(boot, boot->state.security_version);
        if (ret != 0) {
            return ret;
        }
        version = boot->state.security_version;
    }
#else
    (void)state_loaded;
#endif /* defined(MR_USE_MODULE_BOOT_AUTH) */
    boot->state.security_version = version;
    return 0;
}

static mr_err_t boot_state_save(mr_boot_t *boot, mr_boot_state_t *state) {
    mr_err_t ret;

    /* Check operation */
    if ((!boot->ops->erase) || (!boot->ops->program)) {
        return -MR_ENOSYS;
    }
#if defined(MR_USE_MODULE_BOOT_AUTH)
    if (!boot->ops->auth) {
        return -MR_ENOSYS;
    }
#endif /* defined(MR_USE_MODULE_BOOT_AUTH) */

    /* Prepare state */
    state->sequence++;
#if defined(MR_USE_MODULE_BOOT_AUTH)
    ret = boot->ops->auth(boot, state, sizeof(*state) - sizeof(state->auth),
                          state->auth);
    if (ret != 0) {
        return ret;
    }
#endif /* defined(MR_USE_MODULE_BOOT_AUTH) */

    /* Save first copy */
    ret = boot->ops->erase(boot, boot->config.state_a_offset,
                           boot->config.state_size);
    if (ret != 0) {
        return ret;
    }
    ret = boot_program(boot, boot->config.state_a_offset, state,
                       sizeof(*state));
    if (ret != 0) {
        return ret;
    }
    mr_memcpy(&boot->state, state, sizeof(boot->state));

    /* Save second copy */
    ret = boot->ops->erase(boot, boot->config.state_b_offset,
                           boot->config.state_size);
    if (ret != 0) {
        return ret;
    }
    return boot_program(boot, boot->config.state_b_offset, state,
                        sizeof(*state));
}

static mr_err_t boot_state_load(mr_boot_t *boot) {
    mr_boot_state_t state[2];
    mr_boot_state_t *chosen;
    mr_uint64_t offset[2];
    mr_err_t ret[2];
    mr_err_t error;
    mr_size_t i;

    /* Read both copies */
    offset[0] = boot->config.state_a_offset;
    offset[1] = boot->config.state_b_offset;
    for (i = 0; i < MR_ARRAY_SIZE(state); i++) {
        ret[i] = boot_read(boot, offset[i], &state[i], sizeof(state[i]));
        if (ret[i] == 0) {
            ret[i] = boot_state_check(boot, &state[i]);
        }
    }

    /* Keep the newer usable copy; a tie keeps the later one */
    chosen = MR_NULL;
    error = 0;
    for (i = 0; i < MR_ARRAY_SIZE(state); i++) {
        if (ret[i] != 0) {
            if ((ret[i] != -MR_ENOENT) && (error == 0)) {
                error = ret[i];
            }
            continue;
        }
        /* Prefer the newer copy; keep the later one on a tie */
        if ((!chosen) || (state[i].sequence == chosen->sequence)
            || boot_sequence_is_newer(state[i].sequence, chosen->sequence)) {
            chosen = &state[i];
        }
    }
    if (chosen) {
        mr_memcpy(&boot->state, chosen, sizeof(boot->state));
        return boot_rollback_sync(boot, MR_TRUE);
    }
    if (error != 0) {
        return error;
    }

    /* Start from slot A */
    mr_memset(&boot->state, 0, sizeof(boot->state));
    boot->state.active_slot = (mr_uint8_t)MR_BOOT_SLOT_A;
    return boot_rollback_sync(boot, MR_FALSE);
}

static mr_err_t boot_init(mr_boot_t *boot, const mr_clazz_t *clazz,
                          const mr_boot_config_t *config,
                          const mr_boot_ops_t *ops, void *priv) {
    mr_err_t ret;

    /* Check configuration */
    ret = boot_config_check(config);
    if (ret != 0) {
        return ret;
    }

    /* Init Boot */
    mr_memcpy(&boot->config, config, sizeof(boot->config));
    mr_memset(&boot->state, 0, sizeof(boot->state));
    mr_memset(&boot->update, 0, sizeof(boot->update));
    mr_memset(&boot->launch, 0, sizeof(boot->launch));
    boot->launch.slot = MR_BOOT_SLOT_MAX;
#if defined(MR_USE_MODULE_BOOT_SHA256)
    mr_memset(&boot->verify, 0, sizeof(boot->verify));
#endif /* defined(MR_USE_MODULE_BOOT_SHA256) */
    boot->ops = ops;
    boot->priv = priv;

    /* Load persistent state */
    ret = boot_state_load(boot);
    if (ret != 0) {
        return ret;
    }

    /* Init object */
    return mr_object_init((mr_object_t *)boot, clazz);
}

mr_err_t mr_boot_init(mr_boot_t *boot, const mr_boot_config_t *config,
                      const mr_boot_ops_t *ops, void *priv) {
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(boot);

    /* Check parameter */
    MR_ASSERT(boot != MR_NULL);
    MR_ASSERT(config != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Init Boot */
    return boot_init(boot, clazz, config, ops, priv);
}

mr_boot_t *mr_boot_create(const mr_boot_config_t *config,
                          const mr_boot_ops_t *ops, void *priv) {
    const mr_clazz_t *clazz = MR_CLAZZES_DYNAMIC(boot);
    mr_boot_t *boot;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(config != MR_NULL);
    MR_ASSERT(ops != MR_NULL);

    /* Create Boot */
    boot = mr_malloc(sizeof(mr_boot_t));
    if (!boot) {
        return MR_NULL;
    }

    /* Init Boot */
    ret = boot_init(boot, clazz, config, ops, priv);
    if (ret != 0) {
        mr_free(boot);
        return MR_NULL;
    }
    return boot;
}

mr_err_t mr_boot_del(mr_boot_t *boot) {
    /* Check parameter */
    MR_ASSERT((boot != MR_NULL) && MR_OBJECT_IS_INITED(boot));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(boot, boot));

    /* Delete object */
    mr_object_del((mr_object_t *)boot);
    return 0;
}

mr_err_t mr_boot_update_begin(mr_boot_t *boot, mr_boot_manifest_t *manifest) {
#if defined(MR_USE_MODULE_BOOT_ECDSA_P256)
    mr_uint8_t digest[MR_BOOT_SHA256_SIZE];
#endif /* defined(MR_USE_MODULE_BOOT_ECDSA_P256) */
    mr_boot_slot_t slot;
    mr_uint64_t offset, size;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((boot != MR_NULL) && MR_OBJECT_IS_INITED(boot));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(boot, boot));
    MR_ASSERT(manifest != MR_NULL);

    /* Check update */
    if (boot->update.active) {
        return -MR_EBUSY;
    }
    if ((!boot->ops->erase) || (!boot->ops->program)) {
        return -MR_ENOSYS;
    }
#if defined(MR_USE_MODULE_BOOT_SHA256)
    if ((!boot->ops->hash_begin) || (!boot->ops->hash_update)
        || (!boot->ops->hash_end)) {
        return -MR_ENOSYS;
    }
#endif /* defined(MR_USE_MODULE_BOOT_SHA256) */
#if defined(MR_USE_MODULE_BOOT_ECDSA_P256)
    if (!boot->ops->verify) {
        return -MR_ENOSYS;
    }
#endif /* defined(MR_USE_MODULE_BOOT_ECDSA_P256) */
#if defined(MR_USE_MODULE_BOOT_AUTH)
    if (!boot->ops->auth) {
        return -MR_ENOSYS;
    }
#endif /* defined(MR_USE_MODULE_BOOT_AUTH) */
    if ((manifest->product_id != boot->config.product_id)
        || (manifest->image_size == 0U)) {
        return -MR_EINVAL;
    }
    if (manifest->security_version < boot->state.security_version) {
        return -MR_EPERM;
    }

    /* Select inactive slot */
    if (boot->state.active_slot == (mr_uint8_t)MR_BOOT_SLOT_A) {
        slot = MR_BOOT_SLOT_B;
        offset = boot->config.slot_b_offset;
        size = boot->config.slot_b_size;
    } else {
        slot = MR_BOOT_SLOT_A;
        offset = boot->config.slot_a_offset;
        size = boot->config.slot_a_size;
    }
    if (manifest->image_size > (size - sizeof(*manifest))) {
        return -MR_ERANGE;
    }

#if defined(MR_USE_MODULE_BOOT_ECDSA_P256)
    /* Check manifest signature before erasing the target slot */
    ret = boot_hash(boot, manifest,
                    sizeof(*manifest) - sizeof(manifest->signature), digest);
    if (ret != 0) {
        return ret;
    }
    ret = boot->ops->verify(boot, manifest->key_id, digest,
                            manifest->signature);
    if (ret != 0) {
        return ret;
    }
#endif /* defined(MR_USE_MODULE_BOOT_ECDSA_P256) */

    /* Erase target slot */
    ret = boot->ops->erase(boot, offset, size);
    if (ret != 0) {
        return ret;
    }
#if defined(MR_USE_MODULE_BOOT_SHA256)
    /* Start image hash */
    ret = boot->ops->hash_begin(boot);
    if (ret != 0) {
        return ret;
    }
#endif /* defined(MR_USE_MODULE_BOOT_SHA256) */

    /* Begin update */
    mr_memcpy(&boot->update.manifest, manifest, sizeof(boot->update.manifest));
    boot->update.offset = offset;
    boot->update.size = size;
    boot->update.written = 0U;
    boot->update.slot = slot;
    boot->update.active = MR_TRUE;
    return 0;
}

mr_err_t mr_boot_update_write(mr_boot_t *boot, const void *data,
                              mr_size_t size) {
    mr_uint64_t offset;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((boot != MR_NULL) && MR_OBJECT_IS_INITED(boot));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(boot, boot));
    MR_ASSERT(data != MR_NULL);

    /* Check update */
    if (!boot->update.active) {
        return -MR_EINVAL;
    }
    if ((mr_uint64_t)size
        > (boot->update.manifest.image_size - boot->update.written)) {
        return -MR_ERANGE;
    }
    if (size == 0U) {
        return 0;
    }

    /* Program image */
    offset = boot->update.offset + sizeof(boot->update.manifest)
           + boot->update.written;
    ret = boot_program(boot, offset, data, size);
    if (ret != 0) {
        boot->update.active = MR_FALSE;
        return ret;
    }
#if defined(MR_USE_MODULE_BOOT_SHA256)
    /* Update image hash */
    ret = boot->ops->hash_update(boot, data, size);
    if (ret != 0) {
        boot->update.active = MR_FALSE;
        return ret;
    }
#endif /* defined(MR_USE_MODULE_BOOT_SHA256) */
    boot->update.written += size;
    return 0;
}

mr_err_t mr_boot_update_end(mr_boot_t *boot) {
#if defined(MR_USE_MODULE_BOOT_SHA256)
    mr_uint8_t digest[MR_BOOT_SHA256_SIZE];
#endif /* defined(MR_USE_MODULE_BOOT_SHA256) */
    mr_boot_state_t state;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((boot != MR_NULL) && MR_OBJECT_IS_INITED(boot));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(boot, boot));

    /* Check update */
    if (!boot->update.active) {
        return -MR_EINVAL;
    }
    if (boot->update.written != boot->update.manifest.image_size) {
        return -MR_EINVAL;
    }

#if defined(MR_USE_MODULE_BOOT_SHA256)
    /* Check image hash */
    ret = boot->ops->hash_end(boot, digest);
    if (ret != 0) {
        goto _exit;
    }
    if (!boot_bytes_equal(digest, boot->update.manifest.image_hash,
                          sizeof(digest))) {
        ret = -MR_EINVAL;
        goto _exit;
    }

    /* Re-read the programmed image before it can become active */
    ret = boot_hash_image(boot,
                          boot->update.offset + sizeof(boot->update.manifest),
                          boot->update.manifest.image_size, digest);
    if (ret != 0) {
        goto _exit;
    }
    if (!boot_bytes_equal(digest, boot->update.manifest.image_hash,
                          sizeof(digest))) {
        ret = -MR_EINVAL;
        goto _exit;
    }
#endif /* defined(MR_USE_MODULE_BOOT_SHA256) */

    /* Prepare state */
    mr_memcpy(&state, &boot->state, sizeof(state));
    state.active_slot = (mr_uint8_t)boot->update.slot;
    state.invalid[boot->update.slot] = 0U;
    state.security_version = boot->update.manifest.security_version;
#if defined(MR_USE_MODULE_BOOT_FAST)
    /* Cache manifest digest */
    ret = boot_hash(boot, &boot->update.manifest, sizeof(boot->update.manifest),
                    state.manifest_digest[boot->update.slot]);
    if (ret != 0) {
        goto _exit;
    }
#endif /* defined(MR_USE_MODULE_BOOT_FAST) */

    /* Commit manifest */
    ret = boot_program(boot, boot->update.offset, &boot->update.manifest,
                       sizeof(boot->update.manifest));
    if (ret != 0) {
        goto _exit;
    }

    /* Commit state */
    ret = boot_state_save(boot, &state);
    if (ret != 0) {
        goto _exit;
    }

    /* Finish update */
    mr_memset(&boot->update, 0, sizeof(boot->update));
    return 0;
_exit:
#if defined(MR_USE_MODULE_BOOT_SHA256)
    mr_memset(digest, 0, sizeof(digest));
#endif /* defined(MR_USE_MODULE_BOOT_SHA256) */
    boot->update.active = MR_FALSE;
    return ret;
}

void mr_boot_update_abort(mr_boot_t *boot) {
    /* Check parameter */
    MR_ASSERT((boot != MR_NULL) && MR_OBJECT_IS_INITED(boot));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(boot, boot));

    /* Abort update */
    mr_memset(&boot->update, 0, sizeof(boot->update));
}

mr_ssize_t mr_boot_sn_read(mr_boot_t *boot, void *data, mr_size_t size) {
    /* Check parameter */
    MR_ASSERT((boot != MR_NULL) && MR_OBJECT_IS_INITED(boot));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(boot, boot));
    MR_ASSERT(data != MR_NULL);

    /* Read serial number */
    if (!boot->ops->sn_read) {
        return -MR_ENOSYS;
    }
    return boot->ops->sn_read(boot, data, size);
}

mr_ssize_t mr_boot_sn_write(mr_boot_t *boot, const void *data, mr_size_t size) {
    /* Check parameter */
    MR_ASSERT((boot != MR_NULL) && MR_OBJECT_IS_INITED(boot));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(boot, boot));
    MR_ASSERT(data != MR_NULL);

    /* Write serial number */
    if (!boot->ops->sn_write) {
        return -MR_ENOSYS;
    }
    return boot->ops->sn_write(boot, data, size);
}

mr_err_t mr_boot_select(mr_boot_t *boot, mr_boot_slot_t slot) {
#if defined(MR_USE_MODULE_BOOT_SHA256)
    mr_uint8_t digest[MR_BOOT_SHA256_SIZE];
#endif /* defined(MR_USE_MODULE_BOOT_SHA256) */
    mr_boot_manifest_t manifest;
    mr_uint64_t offset, size;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((boot != MR_NULL) && MR_OBJECT_IS_INITED(boot));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(boot, boot));

    /* Check slot */
    mr_memset(&boot->launch, 0, sizeof(boot->launch));
    boot->launch.slot = MR_BOOT_SLOT_MAX;
    if (boot->update.active) {
        return -MR_EBUSY;
    }
    if (slot >= MR_BOOT_SLOT_MAX) {
        return -MR_EINVAL;
    }
    if (!boot->ops->read) {
        return -MR_ENOSYS;
    }
#if defined(MR_USE_MODULE_BOOT_SHA256)
    if ((!boot->ops->hash_begin) || (!boot->ops->hash_update)
        || (!boot->ops->hash_end)) {
        return -MR_ENOSYS;
    }
#endif /* defined(MR_USE_MODULE_BOOT_SHA256) */
#if defined(MR_USE_MODULE_BOOT_ECDSA_P256)
    if (!boot->ops->verify) {
        return -MR_ENOSYS;
    }
#endif /* defined(MR_USE_MODULE_BOOT_ECDSA_P256) */

    /* Get slot */
    switch (slot) {
        case MR_BOOT_SLOT_A: {
            offset = boot->config.slot_a_offset;
            size = boot->config.slot_a_size;
            break;
        }
        case MR_BOOT_SLOT_B: {
            offset = boot->config.slot_b_offset;
            size = boot->config.slot_b_size;
            break;
        }
#if defined(MR_USE_MODULE_BOOT_RECOVERY)
        case MR_BOOT_SLOT_RECOVERY: {
            offset = boot->config.recovery_offset;
            size = boot->config.recovery_size;
            break;
        }
#endif /* defined(MR_USE_MODULE_BOOT_RECOVERY) */
        default: {
            return -MR_ENOSYS;
        }
    }
    if ((slot != MR_BOOT_SLOT_RECOVERY) && boot->state.invalid[slot]) {
        return -MR_EPERM;
    }

    /* Read manifest */
    ret = boot_read(boot, offset, &manifest, sizeof(manifest));
    if (ret != 0) {
        return ret;
    }

    /* Check manifest */
    if ((manifest.product_id != boot->config.product_id)
        || (manifest.image_size > (size - sizeof(manifest)))
        || (manifest.image_size == 0U)) {
        return -MR_EINVAL;
    }
    if ((manifest.security_version < boot->state.security_version)
        && (slot != MR_BOOT_SLOT_RECOVERY)) {
        return -MR_EPERM;
    }

#if defined(MR_USE_MODULE_BOOT_FAST)
    /* Trust a previously verified manifest */
    ret = boot_hash(boot, &manifest, sizeof(manifest), digest);
    if (ret != 0) {
        return ret;
    }
    if (boot_bytes_equal(digest, boot->state.manifest_digest[slot],
                         sizeof(digest))) {
        boot->launch.slot = slot;
        boot->launch.offset = offset + sizeof(manifest);
        return 0;
    }
#endif /* defined(MR_USE_MODULE_BOOT_FAST) */

#if defined(MR_USE_MODULE_BOOT_ECDSA_P256)
    /* Check manifest signature */
    ret = boot_hash(boot, &manifest,
                    sizeof(manifest) - sizeof(manifest.signature), digest);
    if (ret != 0) {
        return ret;
    }
    ret = boot->ops->verify(boot, manifest.key_id, digest, manifest.signature);
    if (ret != 0) {
        return ret;
    }
#endif /* defined(MR_USE_MODULE_BOOT_ECDSA_P256) */

#if defined(MR_USE_MODULE_BOOT_SHA256)
    /* Check image hash */
    ret = boot_hash_image(boot, offset + sizeof(manifest), manifest.image_size,
                          digest);
    if (ret != 0) {
        return ret;
    }
    if (!boot_bytes_equal(digest, manifest.image_hash, sizeof(digest))) {
        return -MR_EINVAL;
    }
#endif /* defined(MR_USE_MODULE_BOOT_SHA256) */

    /* Select slot */
    boot->launch.offset = offset + sizeof(manifest);
    boot->launch.slot = slot;
    return 0;
}

static mr_boot_slot_t boot_other_slot(mr_uint8_t active) {
    if (active == (mr_uint8_t)MR_BOOT_SLOT_A) {
        return MR_BOOT_SLOT_B;
    }
    return MR_BOOT_SLOT_A;
}

static void boot_rescue(mr_boot_t *boot) {
    /* Stay in the bootloader */
    mr_memset(&boot->launch, 0, sizeof(boot->launch));
    boot->launch.slot = MR_BOOT_SLOT_MAX;
}

static mr_err_t boot_invalidate(mr_boot_t *boot, mr_boot_slot_t slot) {
    mr_boot_state_t state;

    /* Recovery failure must not rewrite the A/B record */
    if (slot >= MR_BOOT_SLOT_RECOVERY) {
        return 0;
    }
    mr_memcpy(&state, &boot->state, sizeof(state));
    state.invalid[slot] = 1U;
    return boot_state_save(boot, &state);
}

static mr_err_t boot_activate(mr_boot_t *boot, mr_boot_slot_t slot) {
    mr_boot_state_t state;

    mr_memcpy(&state, &boot->state, sizeof(state));
    state.invalid[slot] = 0U;
    state.active_slot = (mr_uint8_t)slot;
    return boot_state_save(boot, &state);
}

static mr_err_t boot_enter_recovery(mr_boot_t *boot) {
#if defined(MR_USE_MODULE_BOOT_RECOVERY)
    mr_err_t ret;

    /* A recovery boot does not change the A/B record */
    ret = mr_boot_select(boot, MR_BOOT_SLOT_RECOVERY);
    if (ret != 0) {
        boot_rescue(boot);
        return 0;
    }
    (void)boot->ops->jump(boot, boot->launch.offset);
    boot_rescue(boot);
    return 0;
#else
    boot_rescue(boot);
    return 0;
#endif /* defined(MR_USE_MODULE_BOOT_RECOVERY) */
}

static mr_err_t boot_boot_candidate(mr_boot_t *boot, mr_boot_slot_t slot,
                                    mr_bool_t commit) {
    mr_err_t ret;

    /* Prepare this image */
    ret = mr_boot_select(boot, slot);
    if (ret != 0) {
        return ret;
    }
    if (commit) {
        ret = boot_activate(boot, slot);
        if (ret != 0) {
            return ret;
        }
    }

    /* A return means the image did not run */
    (void)boot->ops->jump(boot, boot->launch.offset);
    return -MR_EIO;
}

static mr_err_t boot_fallback(mr_boot_t *boot, mr_boot_slot_t failed) {
    mr_boot_slot_t other;
    mr_err_t ret;

    /* Retire the APP that returned, then try the other APP */
    if (failed < MR_BOOT_SLOT_RECOVERY) {
        ret = boot_invalidate(boot, failed);
        if (ret != 0) {
            return ret;
        }
        other = boot_other_slot((mr_uint8_t)failed);
        ret = boot_boot_candidate(boot, other, MR_TRUE);
        if (ret == -MR_EIO) {
            ret = boot_invalidate(boot, other);
            if (ret != 0) {
                return ret;
            }
        }
    }
    return boot_enter_recovery(boot);
}

static mr_err_t boot_boot_installed(mr_boot_t *boot) {
    mr_boot_slot_t active, other;
    mr_err_t ret;

    /* Active APP, then the other APP, then recovery or rescue */
    active = (mr_boot_slot_t)boot->state.active_slot;
    other = boot_other_slot(boot->state.active_slot);
    ret = boot_boot_candidate(boot, active, MR_FALSE);
    if (ret == -MR_EIO) {
        return boot_fallback(boot, active);
    }
    if (ret == 0) {
        return 0;
    }
    ret = boot_boot_candidate(boot, other, MR_TRUE);
    if (ret == -MR_EIO) {
        return boot_fallback(boot, other);
    }
    return boot_enter_recovery(boot);
}

static mr_err_t boot_boot_watchdog(mr_boot_t *boot) {
    mr_boot_slot_t active, other;
    mr_err_t ret;

    /* Retire the APP that the watchdog caught before trying the other */
    active = (mr_boot_slot_t)boot->state.active_slot;
    ret = boot_invalidate(boot, active);
    if (ret != 0) {
        return ret;
    }
    other = boot_other_slot((mr_uint8_t)active);
    ret = boot_boot_candidate(boot, other, MR_TRUE);
    if (ret == -MR_EIO) {
        ret = boot_invalidate(boot, other);
        if (ret != 0) {
            return ret;
        }
        return boot_enter_recovery(boot);
    }
    if (ret != 0) {
        return boot_enter_recovery(boot);
    }
    return 0;
}

static mr_err_t boot_boot_update(mr_boot_t *boot) {
    mr_boot_slot_t active, staged;
    mr_err_t ret;

    /* An external update occupies the inactive slot */
    active = (mr_boot_slot_t)boot->state.active_slot;
    staged = boot_other_slot(boot->state.active_slot);
    ret = boot_boot_candidate(boot, staged, MR_TRUE);
    if (ret == -MR_EIO) {
        return boot_fallback(boot, staged);
    }
    if (ret == 0) {
        return 0;
    }

    /* Retire an invalid staged image before keeping the old APP */
    if ((ret == -MR_EINVAL) || (ret == -MR_EPERM)) {
        ret = boot_invalidate(boot, staged);
        if (ret != 0) {
            return ret;
        }
    }

    /* Keep the old APP */
    ret = boot_boot_candidate(boot, active, MR_FALSE);
    if (ret == -MR_EIO) {
        return boot_fallback(boot, active);
    }
    if (ret != 0) {
        return boot_enter_recovery(boot);
    }
    return 0;
}

mr_err_t mr_boot_start(mr_boot_t *boot) {
    mr_boot_source_t source;
    mr_boot_state_t state;
    mr_boot_slot_t slot;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((boot != MR_NULL) && MR_OBJECT_IS_INITED(boot));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(boot, boot));

    /* Check operation */
    if ((!boot->ops->source) || (!boot->ops->jump)) {
        return -MR_ENOSYS;
    }
    if (boot->update.active) {
        return -MR_EBUSY;
    }

    /* Select boot slot */
    source = boot->ops->source(boot);
    switch (source) {
        case MR_BOOT_SOURCE_POWER_ON:
        case MR_BOOT_SOURCE_RESTART: {
            return boot_boot_installed(boot);
        }
        case MR_BOOT_SOURCE_WATCHDOG: {
            return boot_boot_watchdog(boot);
        }
        case MR_BOOT_SOURCE_UPDATE: {
            return boot_boot_update(boot);
        }
        case MR_BOOT_SOURCE_ROLLBACK: {
            slot = boot_other_slot(boot->state.active_slot);
            ret = mr_boot_select(boot, slot);
            if (ret != 0) {
                return ret;
            }
            mr_memcpy(&state, &boot->state, sizeof(state));
            state.invalid[state.active_slot] = 1U;
            state.active_slot = (mr_uint8_t)slot;
            ret = boot_state_save(boot, &state);
            if (ret != 0) {
                return ret;
            }
            (void)boot->ops->jump(boot, boot->launch.offset);
            return boot_fallback(boot, slot);
        }
        case MR_BOOT_SOURCE_RECOVERY: {
#if defined(MR_USE_MODULE_BOOT_RECOVERY)
            return boot_enter_recovery(boot);
#else
            return -MR_ENOSYS;
#endif /* defined(MR_USE_MODULE_BOOT_RECOVERY) */
        }
        case MR_BOOT_SOURCE_RESCUE: {
            boot_rescue(boot);
            return 0;
        }
        default: {
            return -MR_EINVAL;
        }
    }
}

static mr_boot_t *boot_obj_release(mr_object_t *obj) {
    mr_boot_t *boot;

    /* Get Boot */
    boot = MR_CONTAINER_OF(obj, mr_boot_t, parent);

    /* Delete Boot */
    mr_memset(&boot->state, 0, sizeof(boot->state));
    mr_memset(&boot->update, 0, sizeof(boot->update));
    mr_memset(&boot->launch, 0, sizeof(boot->launch));
    boot->launch.slot = MR_BOOT_SLOT_MAX;
#if defined(MR_USE_MODULE_BOOT_SHA256)
    mr_memset(&boot->verify, 0, sizeof(boot->verify));
#endif /* defined(MR_USE_MODULE_BOOT_SHA256) */
    boot->ops = MR_NULL;
    boot->priv = MR_NULL;
    return boot;
}

static void boot_statik_release(mr_object_t *obj) {
    /* Release Boot */
    boot_obj_release(obj);
}

static void boot_dynamic_release(mr_object_t *obj) {
    mr_boot_t *boot;

    /* Release Boot */
    boot = boot_obj_release(obj);

    /* Free Boot */
    mr_free(boot);
}
#endif /* defined(MR_USE_MODULE_BOOT) */

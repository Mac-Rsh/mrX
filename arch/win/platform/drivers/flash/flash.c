/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-11-25        MacRsh        First version
 */

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif /* _CRT_SECURE_NO_WARNINGS */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_FLASH)
#include <mrX/drivers/flash/mr_flash.h>
#include <mrX/libc/mr_string.h>
#include <stdio.h>

#define WIN_FLASH_DEFAULT_PATH          "win_flash.bin"
#define WIN_FLASH_DEFAULT_SIZE          (256U * 1024U)
#define WIN_FLASH_DEFAULT_ERASE_SIZE    (4096U)
#define WIN_FLASH_DEFAULT_PROGRAM_SIZE  (4U)
#define WIN_FLASH_DEFAULT_PAGE_SIZE     (256U)
#define WIN_FLASH_CHUNK_SIZE            (256U)

/* Driver FLASH operations */
static mr_err_t flash_probe(const mr_driver_t *drv, mr_flash_info_t *info);
static mr_err_t flash_region(const mr_driver_t *drv, mr_uint32_t index,
                             mr_flash_region_t *region);
static mr_ssize_t flash_read(const mr_driver_t *drv, mr_uint64_t offset,
                             mr_uint8_t *buf, mr_size_t size);
static mr_ssize_t flash_program(const mr_driver_t *drv, mr_uint64_t offset,
                                const mr_uint8_t *buf, mr_size_t size);
static mr_err_t flash_erase(const mr_driver_t *drv, mr_uint64_t offset,
                            mr_uint64_t size);

static const mr_flash_ops_t __win_flash_ops = {
    .probe = flash_probe,
    .region = flash_region,
    .read = flash_read,
    .program = flash_program,
    .erase = flash_erase,
};

static mr_err_t win_flash_fill_locked(FILE *fp, mr_uint64_t offset,
                                      mr_uint64_t size, mr_uint8_t byte) {
    mr_uint8_t chunk[WIN_FLASH_CHUNK_SIZE];
    mr_uint64_t left = size;

    if (size == 0U) {
        return 0;
    }
    if (_fseeki64(fp, (__int64)offset, SEEK_SET) != 0) {
        return -MR_EIO;
    }

    mr_memset(chunk, byte, sizeof(chunk));
    while (left > 0U) {
        mr_size_t n = (left > sizeof(chunk)) ? sizeof(chunk) : (mr_size_t)left;
        if (fwrite(chunk, 1U, n, fp) != n) {
            return -MR_EIO;
        }
        left -= n;
    }
    fflush(fp);
    return 0;
}

static void win_flash_apply_defaults(mr_sim_flash_t *sim) {
    if ((!sim->cfg.path) || (sim->cfg.path[0] == '\0')) {
        sim->cfg.path = WIN_FLASH_DEFAULT_PATH;
    }
    if (sim->cfg.size == 0U) {
        sim->cfg.size = WIN_FLASH_DEFAULT_SIZE;
    }
    if (sim->cfg.erase_size == 0U) {
        sim->cfg.erase_size = WIN_FLASH_DEFAULT_ERASE_SIZE;
    }
    if (sim->cfg.program_size == 0U) {
        sim->cfg.program_size = WIN_FLASH_DEFAULT_PROGRAM_SIZE;
    }
}

static mr_err_t win_flash_open_locked(mr_sim_flash_t *sim) {
    __int64 cur_size;
    mr_err_t ret;

    if (sim->fp) {
        return 0;
    }

    win_flash_apply_defaults(sim);

    sim->fp = fopen(sim->cfg.path, "r+b");
    if (!sim->fp) {
        sim->fp = fopen(sim->cfg.path, "w+b");
        if (!sim->fp) {
            return -MR_EIO;
        }
    }

    if (_fseeki64(sim->fp, 0, SEEK_END) != 0) {
        fclose(sim->fp);
        sim->fp = MR_NULL;
        return -MR_EIO;
    }
    cur_size = _ftelli64(sim->fp);
    if (cur_size < 0) {
        fclose(sim->fp);
        sim->fp = MR_NULL;
        return -MR_EIO;
    }

    if ((mr_uint64_t)cur_size < sim->cfg.size) {
        ret = win_flash_fill_locked(sim->fp, (mr_uint64_t)cur_size,
                                    sim->cfg.size - (mr_uint64_t)cur_size,
                                    sim->cfg.erased_byte);
        if (ret != 0) {
            fclose(sim->fp);
            sim->fp = MR_NULL;
            return ret;
        }
    }
    return 0;
}

static mr_bool_t win_flash_byte_check(mr_uint8_t old_val, mr_uint8_t new_val,
                                      mr_uint8_t erased_byte, mr_bool_t strict,
                                      mr_uint8_t *out_val) {
    if (erased_byte == 0xFFU) {
        if (strict && ((old_val & new_val) != new_val)) {
            return MR_FALSE;
        }
        *out_val = (mr_uint8_t)(old_val & new_val);
        return MR_TRUE;
    }
    if (erased_byte == 0x00U) {
        if (strict && ((old_val | new_val) != new_val)) {
            return MR_FALSE;
        }
        *out_val = (mr_uint8_t)(old_val | new_val);
        return MR_TRUE;
    }
    if (strict && (old_val != erased_byte) && (old_val != new_val)) {
        return MR_FALSE;
    }
    *out_val = new_val;
    return MR_TRUE;
}

static mr_err_t win_flash_program_bytes_locked(mr_sim_flash_t *sim,
                                               mr_uint64_t offset,
                                               const mr_uint8_t *buf,
                                               mr_size_t size) {
    mr_uint8_t old_chunk[WIN_FLASH_CHUNK_SIZE];
    mr_uint8_t new_chunk[WIN_FLASH_CHUNK_SIZE];
    mr_size_t done, n, i;

    if (size == 0U) {
        return 0;
    }

    /* Pass 1: validate strict NOR programming rules before modifying disk */
    if (sim->cfg.strict) {
        for (done = 0U; done < size; done += n) {
            n = ((size - done) > sizeof(old_chunk)) ? sizeof(old_chunk)
                                                    : (size - done);
            if (_fseeki64(sim->fp, (__int64)(offset + done), SEEK_SET) != 0) {
                return -MR_EIO;
            }
            if (fread(old_chunk, 1U, n, sim->fp) != n) {
                return -MR_EIO;
            }
            for (i = 0U; i < n; i++) {
                mr_uint8_t out_b;
                if (!win_flash_byte_check(old_chunk[i], buf[done + i],
                                          sim->cfg.erased_byte, MR_TRUE,
                                          &out_b)) {
                    return -MR_EIO;
                }
            }
        }
    }

    /* Pass 2: apply NOR bit transition and commit */
    for (done = 0U; done < size; done += n) {
        n = ((size - done) > sizeof(old_chunk)) ? sizeof(old_chunk)
                                                : (size - done);
        if (_fseeki64(sim->fp, (__int64)(offset + done), SEEK_SET) != 0) {
            return -MR_EIO;
        }
        if (fread(old_chunk, 1U, n, sim->fp) != n) {
            return -MR_EIO;
        }
        for (i = 0U; i < n; i++) {
            win_flash_byte_check(old_chunk[i], buf[done + i],
                                 sim->cfg.erased_byte, MR_FALSE, &new_chunk[i]);
        }
        if (_fseeki64(sim->fp, (__int64)(offset + done), SEEK_SET) != 0) {
            return -MR_EIO;
        }
        if (fwrite(new_chunk, 1U, n, sim->fp) != n) {
            return -MR_EIO;
        }
    }
    fflush(sim->fp);
    return 0;
}

static mr_err_t flash_probe(const mr_driver_t *drv, mr_flash_info_t *info) {
    mr_sim_flash_t *sim = (mr_sim_flash_t *)MR_DRIVER_PRIV(drv);
    int state;
    mr_err_t ret;

    if (!sim) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    ret = win_flash_open_locked(sim);
    if (ret == 0) {
        info->size = sim->cfg.size;
        info->program_size = sim->cfg.program_size;
        info->page_size = sim->cfg.page_size;
        info->regions_num = 1U;
        mr_memset(info->erased_value, sim->cfg.erased_byte,
                  sizeof(info->erased_value));
    }
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return ret;
}

static mr_err_t flash_region(const mr_driver_t *drv, mr_uint32_t index,
                             mr_flash_region_t *region) {
    mr_sim_flash_t *sim = (mr_sim_flash_t *)MR_DRIVER_PRIV(drv);
    int state;

    if ((!sim) || (index != 0U)) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    region->offset = 0U;
    region->size = sim->cfg.size;
    region->erase_size = sim->cfg.erase_size;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_ssize_t flash_read(const mr_driver_t *drv, mr_uint64_t offset,
                             mr_uint8_t *buf, mr_size_t size) {
    mr_sim_flash_t *sim = (mr_sim_flash_t *)MR_DRIVER_PRIV(drv);
    int state;
    mr_err_t ret;

    if (!sim) {
        return -MR_EINVAL;
    }
    if (size == 0U) {
        return 0;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    ret = win_flash_open_locked(sim);
    if (ret != 0) {
        mr_spinlock_unlock_irqrestore(&sim->lock, state);
        return ret;
    }
    if ((_fseeki64(sim->fp, (__int64)offset, SEEK_SET) != 0)
        || (fread(buf, 1U, (size_t)size, sim->fp) != (size_t)size)) {
        mr_spinlock_unlock_irqrestore(&sim->lock, state);
        return -MR_EIO;
    }
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return (mr_ssize_t)size;
}

static mr_ssize_t flash_program(const mr_driver_t *drv, mr_uint64_t offset,
                                const mr_uint8_t *buf, mr_size_t size) {
    mr_sim_flash_t *sim = (mr_sim_flash_t *)MR_DRIVER_PRIV(drv);
    mr_sim_flash_power_loss_cb_t pl_cb = MR_NULL;
    void *pl_user = MR_NULL;
    mr_bool_t pl_fired = MR_FALSE;
    int state;
    mr_err_t ret;

    if (!sim) {
        return -MR_EINVAL;
    }
    if (((offset % sim->cfg.program_size) != 0U)
        || ((size % sim->cfg.program_size) != 0U)) {
        return -MR_EINVAL;
    }
    if (size == 0U) {
        return 0;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    ret = win_flash_open_locked(sim);
    if (ret != 0) {
        mr_spinlock_unlock_irqrestore(&sim->lock, state);
        return ret;
    }

    if (sim->power_loss_armed) {
        if (sim->power_loss_countdown == 0U) {
            mr_size_t partial = (sim->power_loss_partial < size)
                                    ? sim->power_loss_partial
                                    : size;
            if (partial > 0U) {
                (void)win_flash_program_bytes_locked(sim, offset, buf, partial);
            }
            sim->power_loss_armed = MR_FALSE;
            sim->power_loss_triggered = MR_TRUE;
            pl_cb = sim->power_loss_cb;
            pl_user = sim->power_loss_user_data;
            pl_fired = MR_TRUE;
            ret = -MR_EIO;
        } else {
            sim->power_loss_countdown--;
            ret = win_flash_program_bytes_locked(sim, offset, buf, size);
        }
    } else {
        ret = win_flash_program_bytes_locked(sim, offset, buf, size);
    }
    mr_spinlock_unlock_irqrestore(&sim->lock, state);

    if (pl_fired && pl_cb) {
        pl_cb((mr_driver_t *)drv, pl_user);
    }
    return (ret == 0) ? (mr_ssize_t)size : (mr_ssize_t)ret;
}

static mr_err_t flash_erase(const mr_driver_t *drv, mr_uint64_t offset,
                            mr_uint64_t size) {
    mr_sim_flash_t *sim = (mr_sim_flash_t *)MR_DRIVER_PRIV(drv);
    mr_sim_flash_power_loss_cb_t pl_cb = MR_NULL;
    void *pl_user = MR_NULL;
    mr_bool_t pl_fired = MR_FALSE;
    int state;
    mr_err_t ret;

    if (!sim) {
        return -MR_EINVAL;
    }
    if (((offset % sim->cfg.erase_size) != 0U)
        || ((size % sim->cfg.erase_size) != 0U)) {
        return -MR_EINVAL;
    }
    if (size == 0U) {
        return 0;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    ret = win_flash_open_locked(sim);
    if (ret != 0) {
        mr_spinlock_unlock_irqrestore(&sim->lock, state);
        return ret;
    }

    if (sim->power_loss_armed) {
        if (sim->power_loss_countdown == 0U) {
            mr_uint64_t partial = ((mr_uint64_t)sim->power_loss_partial < size)
                                      ? (mr_uint64_t)sim->power_loss_partial
                                      : size;
            if (partial > 0U) {
                (void)win_flash_fill_locked(sim->fp, offset, partial,
                                            sim->cfg.erased_byte);
            }
            sim->power_loss_armed = MR_FALSE;
            sim->power_loss_triggered = MR_TRUE;
            pl_cb = sim->power_loss_cb;
            pl_user = sim->power_loss_user_data;
            pl_fired = MR_TRUE;
            ret = -MR_EIO;
        } else {
            sim->power_loss_countdown--;
            ret = win_flash_fill_locked(sim->fp, offset, size,
                                        sim->cfg.erased_byte);
        }
    } else {
        ret = win_flash_fill_locked(sim->fp, offset, size,
                                    sim->cfg.erased_byte);
    }
    mr_spinlock_unlock_irqrestore(&sim->lock, state);

    if (pl_fired && pl_cb) {
        pl_cb((mr_driver_t *)drv, pl_user);
    }
    return ret;
}

/* =========================== Simulation Helpers =========================== */

mr_err_t mr_sim_flash_init(mr_driver_t *drv, mr_sim_flash_t *sim,
                           const mr_sim_flash_config_t *cfg) {
    mr_err_t ret;

    if ((!drv) || (!sim)) {
        return -MR_EINVAL;
    }

    mr_memset(sim, 0, sizeof(*sim));
    mr_spinlock_init(&sim->lock);
    if (cfg) {
        sim->cfg = *cfg;
    } else {
        sim->cfg.path = WIN_FLASH_DEFAULT_PATH;
        sim->cfg.size = WIN_FLASH_DEFAULT_SIZE;
        sim->cfg.erase_size = WIN_FLASH_DEFAULT_ERASE_SIZE;
        sim->cfg.program_size = WIN_FLASH_DEFAULT_PROGRAM_SIZE;
        sim->cfg.page_size = WIN_FLASH_DEFAULT_PAGE_SIZE;
        sim->cfg.erased_byte = 0xFFU;
        sim->cfg.strict = MR_TRUE;
    }
    win_flash_apply_defaults(sim);

    ret = mr_flash_init(drv, &sim->data, sim,
                        (mr_flash_ops_t *)&__win_flash_ops);
    if (ret != 0) {
        return ret;
    }
    ret = mr_driver_setup(drv);
    if (ret != 0) {
        mr_flash_del(drv);
        return ret;
    }
    return 0;
}

mr_err_t mr_sim_flash_deinit(mr_driver_t *drv) {
    mr_sim_flash_t *sim;
    int state;

    if (!drv) {
        return -MR_EINVAL;
    }

    sim = (mr_sim_flash_t *)MR_DRIVER_PRIV(drv);
    if (sim) {
        state = mr_spinlock_lock_irqsave(&sim->lock);
        if (sim->fp) {
            fflush(sim->fp);
            fclose(sim->fp);
            sim->fp = MR_NULL;
        }
        mr_spinlock_unlock_irqrestore(&sim->lock, state);
    }
    mr_driver_shutdown(drv);
    return mr_flash_del(drv);
}

mr_err_t mr_sim_flash_reset(mr_driver_t *drv) {
    mr_sim_flash_t *sim;
    int state;
    mr_err_t ret;

    if (!drv) {
        return -MR_EINVAL;
    }

    sim = (mr_sim_flash_t *)MR_DRIVER_PRIV(drv);
    if (!sim) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    ret = win_flash_open_locked(sim);
    if (ret == 0) {
        ret = win_flash_fill_locked(sim->fp, 0U, sim->cfg.size,
                                    sim->cfg.erased_byte);
        sim->power_loss_armed = MR_FALSE;
        sim->power_loss_triggered = MR_FALSE;
        sim->power_loss_countdown = 0U;
        sim->power_loss_partial = 0U;
        sim->power_loss_cb = MR_NULL;
        sim->power_loss_user_data = MR_NULL;
    }
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return ret;
}

mr_err_t mr_sim_flash_power_loss_arm(mr_driver_t *drv, mr_uint32_t op_countdown,
                                     mr_size_t partial_bytes,
                                     mr_sim_flash_power_loss_cb_t cb,
                                     void *user_data) {
    mr_sim_flash_t *sim;
    int state;

    if (!drv) {
        return -MR_EINVAL;
    }
    sim = (mr_sim_flash_t *)MR_DRIVER_PRIV(drv);
    if (!sim) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    sim->power_loss_armed = MR_TRUE;
    sim->power_loss_triggered = MR_FALSE;
    sim->power_loss_countdown = op_countdown;
    sim->power_loss_partial = partial_bytes;
    sim->power_loss_cb = cb;
    sim->power_loss_user_data = user_data;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

void mr_sim_flash_power_loss_disarm(mr_driver_t *drv) {
    mr_sim_flash_t *sim;
    int state;

    if (!drv) {
        return;
    }
    sim = (mr_sim_flash_t *)MR_DRIVER_PRIV(drv);
    if (!sim) {
        return;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    sim->power_loss_armed = MR_FALSE;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
}

mr_bool_t mr_sim_flash_power_loss_triggered(mr_driver_t *drv) {
    mr_sim_flash_t *sim;
    int state;
    mr_bool_t triggered;

    if (!drv) {
        return MR_FALSE;
    }
    sim = (mr_sim_flash_t *)MR_DRIVER_PRIV(drv);
    if (!sim) {
        return MR_FALSE;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    triggered = sim->power_loss_triggered;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return triggered;
}

/* ============================ HardTree Install ============================ */
#define MR_HT_COMPAT                    win_flash
#if MR_HT_HAS_DRIVER()
#define WIN_HT_FLASH_INSTALL(_id, _ops)                                        \
    static mr_sim_flash_t MR_CAT(__win_flash_priv_, _id) = {                   \
        .cfg = {                                                               \
            .path = MR_HT_DRIVER_PROP_OR(_id, win_path,                        \
                                         WIN_FLASH_DEFAULT_PATH),              \
            .size = MR_HT_DRIVER_PROP_OR(_id, win_size,                        \
                                         WIN_FLASH_DEFAULT_SIZE),              \
            .erase_size = MR_HT_DRIVER_PROP_OR(_id, win_erase_size,            \
                                               WIN_FLASH_DEFAULT_ERASE_SIZE),  \
            .program_size                                                      \
                = MR_HT_DRIVER_PROP_OR(_id, win_program_size,                  \
                                       WIN_FLASH_DEFAULT_PROGRAM_SIZE),        \
            .page_size = MR_HT_DRIVER_PROP_OR(_id, win_page_size,              \
                                              WIN_FLASH_DEFAULT_PAGE_SIZE),    \
            .erased_byte = MR_HT_DRIVER_PROP_OR(_id, win_erased_byte, 0xFFU),  \
            .strict = MR_HT_DRIVER_PROP_OR(_id, win_strict, MR_TRUE),          \
        },                                                                     \
        .lock = MR_SPINLOCK_INIT(),                                            \
    };                                                                         \
    MR_HT_FLASH_INSTALL(_id, &MR_CAT(__win_flash_priv_, _id), _ops)

MR_HT_DRIVER_FOREACH(WIN_HT_FLASH_INSTALL, &__win_flash_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_FLASH) */

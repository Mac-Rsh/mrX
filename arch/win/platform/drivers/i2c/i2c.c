/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-11-25        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_I2C)
#include <mrX/drivers/i2c/mr_i2c.h>
#include <mrX/libc/mr_string.h>

/* Driver I2C operations */
static mr_err_t i2c_configure(const mr_driver_t *drv,
                              const mr_i2c_config_t *config);
static mr_err_t i2c_configuration(const mr_driver_t *drv,
                                  mr_i2c_config_t *config);
static mr_ssize_t i2c_transfer(const mr_driver_t *drv, const mr_i2c_msg_t *msgs,
                               mr_size_t num);

static const mr_i2c_ops_t __win_i2c_ops = {
    .configure = i2c_configure,
    .configuration = i2c_configuration,
    .transfer = i2c_transfer,
};

static mr_err_t i2c_configure(const mr_driver_t *drv,
                              const mr_i2c_config_t *config) {
    mr_sim_i2c_t *sim = (mr_sim_i2c_t *)MR_DRIVER_PRIV(drv);
    int state;

    if ((!sim) || (!config)) {
        return -MR_EINVAL;
    }
    if (config->role != MR_I2C_ROLE_MASTER) {
        return -MR_ENOSYS;
    }
    if (config->clock_rate == 0U) {
        return -MR_EINVAL;
    }
    if ((!config->addr10 && (config->own_addr > 0x7FU))
        || (config->addr10 && (config->own_addr > 0x3FFU))) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    sim->cfg = *config;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_err_t i2c_configuration(const mr_driver_t *drv,
                                  mr_i2c_config_t *config) {
    mr_sim_i2c_t *sim = (mr_sim_i2c_t *)MR_DRIVER_PRIV(drv);
    int state;

    if ((!sim) || (!config)) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    *config = sim->cfg;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_bool_t i2c_find_dev(mr_sim_i2c_t *sim, mr_uint16_t addr,
                              mr_bool_t addr10, mr_sim_i2c_dev_t *out) {
    int state;
    mr_size_t i;

    state = mr_spinlock_lock_irqsave(&sim->lock);
    for (i = 0U; i < MR_SIM_I2C_DEV_MAX; i++) {
        if (sim->dev_used[i] && (sim->devs[i].addr == addr)
            && (sim->devs[i].addr10 == addr10)) {
            *out = sim->devs[i];
            mr_spinlock_unlock_irqrestore(&sim->lock, state);
            return MR_TRUE;
        }
    }
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return MR_FALSE;
}

static mr_ssize_t i2c_transfer(const mr_driver_t *drv, const mr_i2c_msg_t *msgs,
                               mr_size_t num) {
    mr_sim_i2c_t *sim = (mr_sim_i2c_t *)MR_DRIVER_PRIV(drv);
    mr_size_t i, seg_start;

    if (!sim) {
        return -MR_EINVAL;
    }
    if (num == 0U) {
        return 0;
    }
    if (!msgs) {
        return -MR_EINVAL;
    }

    /* Validate all messages upfront */
    for (i = 0U; i < num; i++) {
        mr_bool_t addr10 = (msgs[i].flags & MR_I2C_MSG_ADDR10) ? MR_TRUE : MR_FALSE;

        if ((msgs[i].flags & MR_I2C_MSG_STOP)
            && (msgs[i].flags & MR_I2C_MSG_NOSTOP)) {
            return -MR_EINVAL;
        }
        if ((msgs[i].len > 0U) && (!msgs[i].buf)) {
            return -MR_EINVAL;
        }
        if ((!addr10 && (msgs[i].addr > 0x7FU))
            || (addr10 && (msgs[i].addr > 0x3FFU))) {
            return -MR_EINVAL;
        }
    }
    if (msgs[num - 1U].flags & MR_I2C_MSG_NOSTOP) {
        return -MR_ENOSYS;
    }

    /* Dispatch segments grouped by STOP or address change */
    seg_start = 0U;
    while (seg_start < num) {
        mr_uint16_t addr = msgs[seg_start].addr;
        mr_bool_t addr10
            = (msgs[seg_start].flags & MR_I2C_MSG_ADDR10) ? MR_TRUE : MR_FALSE;
        mr_sim_i2c_dev_t dev;
        mr_size_t seg_end = seg_start;
        mr_err_t ret;

        while (seg_end < num) {
            mr_bool_t cur_addr10
                = (msgs[seg_end].flags & MR_I2C_MSG_ADDR10) ? MR_TRUE : MR_FALSE;
            if ((msgs[seg_end].addr != addr) || (cur_addr10 != addr10)) {
                break;
            }
            seg_end++;
            if (msgs[seg_end - 1U].flags & MR_I2C_MSG_STOP) {
                break;
            }
        }

        if (!i2c_find_dev(sim, addr, addr10, &dev) || !dev.transfer) {
            return -MR_EIO;
        }
        ret = dev.transfer(dev.priv, &msgs[seg_start], seg_end - seg_start);
        if (ret != 0) {
            return (mr_ssize_t)ret;
        }
        seg_start = seg_end;
    }
    return (mr_ssize_t)num;
}

/* =========================== Fake I2C EEPROM ============================== */

static mr_err_t win_i2c_eeprom_xfer(void *priv, const mr_i2c_msg_t *msgs,
                                    mr_size_t num) {
    mr_sim_i2c_eeprom_t *ee = (mr_sim_i2c_eeprom_t *)priv;
    mr_size_t i, j;

    if ((!ee) || (!ee->mem) || (ee->size == 0U)) {
        return -MR_EIO;
    }

    for (i = 0U; i < num; i++) {
        const mr_i2c_msg_t *m = &msgs[i];

        if (m->flags & MR_I2C_MSG_READ) {
            for (j = 0U; j < m->len; j++) {
                m->buf[j] = ee->mem[ee->cursor % ee->size];
                ee->cursor = (ee->cursor + 1U) % ee->size;
            }
        } else {
            mr_size_t offset = 0U;

            if (m->len < ee->addr_bytes) {
                return -MR_EINVAL;
            }
            if (ee->addr_bytes == 1U) {
                ee->cursor = m->buf[0];
                offset = 1U;
            } else if (ee->addr_bytes == 2U) {
                ee->cursor = ((mr_size_t)m->buf[0] << 8) | m->buf[1];
                offset = 2U;
            }
            for (j = offset; j < m->len; j++) {
                ee->mem[ee->cursor % ee->size] = m->buf[j];
                ee->cursor = (ee->cursor + 1U) % ee->size;
            }
        }
    }
    return 0;
}

/* =========================== Simulation Helpers =========================== */

mr_err_t mr_sim_i2c_init(mr_driver_t *drv, mr_sim_i2c_t *sim) {
    mr_err_t ret;

    if ((!drv) || (!sim)) {
        return -MR_EINVAL;
    }

    mr_memset(sim, 0, sizeof(*sim));
    mr_spinlock_init(&sim->lock);
    sim->cfg.clock_rate = 100000U;
    sim->cfg.role = MR_I2C_ROLE_MASTER;

    ret = mr_i2c_init(drv, &sim->data, sim, (mr_i2c_ops_t *)&__win_i2c_ops);
    if (ret != 0) {
        return ret;
    }
    ret = mr_driver_setup(drv);
    if (ret != 0) {
        mr_i2c_del(drv);
        return ret;
    }
    return 0;
}

mr_err_t mr_sim_i2c_attach(mr_driver_t *drv, const mr_sim_i2c_dev_t *dev) {
    mr_sim_i2c_t *sim;
    int state;
    mr_size_t i, free_slot = MR_SIM_I2C_DEV_MAX;

    if ((!drv) || (!dev) || (!dev->transfer)) {
        return -MR_EINVAL;
    }
    sim = (mr_sim_i2c_t *)MR_DRIVER_PRIV(drv);
    if (!sim) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    for (i = 0U; i < MR_SIM_I2C_DEV_MAX; i++) {
        if (sim->dev_used[i] && (sim->devs[i].addr == dev->addr)
            && (sim->devs[i].addr10 == dev->addr10)) {
            mr_spinlock_unlock_irqrestore(&sim->lock, state);
            return -MR_EBUSY;
        }
        if (!sim->dev_used[i] && (free_slot == MR_SIM_I2C_DEV_MAX)) {
            free_slot = i;
        }
    }
    if (free_slot == MR_SIM_I2C_DEV_MAX) {
        mr_spinlock_unlock_irqrestore(&sim->lock, state);
        return -MR_ENOMEM;
    }
    sim->devs[free_slot] = *dev;
    sim->dev_used[free_slot] = MR_TRUE;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

mr_err_t mr_sim_i2c_detach(mr_driver_t *drv, mr_uint16_t addr,
                           mr_bool_t addr10) {
    mr_sim_i2c_t *sim;
    int state;
    mr_size_t i;

    if (!drv) {
        return -MR_EINVAL;
    }
    sim = (mr_sim_i2c_t *)MR_DRIVER_PRIV(drv);
    if (!sim) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    for (i = 0U; i < MR_SIM_I2C_DEV_MAX; i++) {
        if (sim->dev_used[i] && (sim->devs[i].addr == addr)
            && (sim->devs[i].addr10 == addr10)) {
            sim->dev_used[i] = MR_FALSE;
            mr_spinlock_unlock_irqrestore(&sim->lock, state);
            return 0;
        }
    }
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return -MR_ENOENT;
}

mr_err_t mr_sim_i2c_eeprom_attach(mr_driver_t *drv, mr_sim_i2c_eeprom_t *eeprom,
                                  mr_uint16_t addr, mr_uint8_t *mem,
                                  mr_size_t size, mr_uint8_t addr_bytes) {
    mr_sim_i2c_dev_t dev;

    if ((!drv) || (!eeprom) || (!mem) || (size == 0U)
        || ((addr_bytes != 1U) && (addr_bytes != 2U))) {
        return -MR_EINVAL;
    }

    eeprom->mem = mem;
    eeprom->size = size;
    eeprom->addr_bytes = addr_bytes;
    eeprom->cursor = 0U;

    dev.addr = addr;
    dev.addr10 = MR_FALSE;
    dev.transfer = win_i2c_eeprom_xfer;
    dev.priv = eeprom;
    return mr_sim_i2c_attach(drv, &dev);
}

/* ============================ HardTree Install ============================ */
#define MR_HT_COMPAT                    win_i2c
#if MR_HT_HAS_DRIVER()
#define WIN_HT_I2C_INSTALL(_id, _ops)                                          \
    static mr_sim_i2c_t MR_CAT(__win_i2c_priv_, _id) = {                       \
        .cfg = {                                                               \
            .clock_rate = 100000U,                                             \
            .role = MR_I2C_ROLE_MASTER,                                        \
        },                                                                     \
        .lock = MR_SPINLOCK_INIT(),                                            \
    };                                                                         \
    MR_HT_I2C_INSTALL(_id, &MR_CAT(__win_i2c_priv_, _id), _ops)

MR_HT_DRIVER_FOREACH(WIN_HT_I2C_INSTALL, &__win_i2c_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_I2C) */

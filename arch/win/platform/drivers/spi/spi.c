/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-11-25        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_SPI)
#include <mrX/drivers/spi/mr_spi.h>
#include <mrX/libc/mr_string.h>

/* SPI EEPROM opcodes (25LCxx / AT25 compatible) */
#define WIN_SPI_EE_CMD_WRSR             (0x01U)
#define WIN_SPI_EE_CMD_WRITE            (0x02U)
#define WIN_SPI_EE_CMD_READ             (0x03U)
#define WIN_SPI_EE_CMD_WRDI             (0x04U)
#define WIN_SPI_EE_CMD_RDSR             (0x05U)
#define WIN_SPI_EE_CMD_WREN             (0x06U)

/* Driver SPI operations */
static mr_err_t spi_configure(const mr_driver_t *drv, mr_uint32_t cs,
                              const mr_spi_config_t *config);
static mr_err_t spi_configuration(const mr_driver_t *drv, mr_uint32_t cs,
                                  mr_spi_config_t *config);
static mr_ssize_t spi_transfer(const mr_driver_t *drv, mr_uint32_t cs,
                               const mr_spi_msg_t *msgs, mr_size_t num);

static const mr_spi_ops_t __win_spi_ops = {
    .configure = spi_configure,
    .configuration = spi_configuration,
    .transfer = spi_transfer,
};

static mr_size_t spi_elem_bytes(mr_uint8_t data_bits) {
    if (data_bits <= 8U) {
        return 1U;
    }
    if (data_bits <= 16U) {
        return 2U;
    }
    return 4U;
}

static mr_err_t spi_configure(const mr_driver_t *drv, mr_uint32_t cs,
                              const mr_spi_config_t *config) {
    mr_sim_spi_t *sim = (mr_sim_spi_t *)MR_DRIVER_PRIV(drv);
    int state;

    if ((!sim) || (!config) || (cs >= sim->cs_num)) {
        return -MR_EINVAL;
    }
    if (config->role != MR_SPI_ROLE_MASTER) {
        return -MR_ENOSYS;
    }
    if ((config->clock_rate != 0U)
        && ((config->data_bits < 4U) || (config->data_bits > 32U))) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    sim->configs[cs] = *config;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_err_t spi_configuration(const mr_driver_t *drv, mr_uint32_t cs,
                                  mr_spi_config_t *config) {
    mr_sim_spi_t *sim = (mr_sim_spi_t *)MR_DRIVER_PRIV(drv);
    int state;

    if ((!sim) || (!config) || (cs >= sim->cs_num)) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    *config = sim->configs[cs];
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

static mr_ssize_t spi_transfer(const mr_driver_t *drv, mr_uint32_t cs,
                               const mr_spi_msg_t *msgs, mr_size_t num) {
    mr_sim_spi_t *sim = (mr_sim_spi_t *)MR_DRIVER_PRIV(drv);
    mr_spi_config_t cfg;
    mr_sim_spi_dev_t dev;
    mr_bool_t has_dev;
    mr_size_t elem, i;
    int state;
    mr_err_t ret;

    if ((!sim) || (cs >= sim->cs_num)) {
        return -MR_EINVAL;
    }
    if (num == 0U) {
        return 0;
    }
    if (!msgs) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    cfg = sim->configs[cs];
    has_dev = sim->dev_used[cs];
    if (has_dev) {
        dev = sim->devs[cs];
    }
    mr_spinlock_unlock_irqrestore(&sim->lock, state);

    if (cfg.clock_rate == 0U) {
        return -MR_ENODEV;
    }

    elem = spi_elem_bytes(cfg.data_bits);
    for (i = 0U; i < num; i++) {
        if (((msgs[i].tx_len % elem) != 0U)
            || ((msgs[i].rx_len % elem) != 0U)) {
            return -MR_EINVAL;
        }
        if ((msgs[i].tx_len > 0U) && !msgs[i].tx_buf) {
            return -MR_EINVAL;
        }
        if ((msgs[i].rx_len > 0U) && !msgs[i].rx_buf) {
            return -MR_EINVAL;
        }
    }

    if (has_dev && dev.transfer) {
        ret = dev.transfer(dev.priv, msgs, num);
        if (ret != 0) {
            return (mr_ssize_t)ret;
        }
        return (mr_ssize_t)num;
    }

    /* No device attached: MISO floats high (0xFF) */
    for (i = 0U; i < num; i++) {
        if (msgs[i].rx_buf && (msgs[i].rx_len > 0U)) {
            mr_memset(msgs[i].rx_buf, 0xFF, msgs[i].rx_len);
        }
    }
    return (mr_ssize_t)num;
}

/* =========================== Fake SPI EEPROM ============================== */

static mr_err_t win_spi_eeprom_xfer(void *priv, const mr_spi_msg_t *msgs,
                                    mr_size_t num) {
    mr_sim_spi_eeprom_t *ee = (mr_sim_spi_eeprom_t *)priv;
    mr_uint8_t cmd = 0U;
    mr_size_t addr = 0U;
    mr_uint8_t addr_got = 0U;
    mr_bool_t has_cmd = MR_FALSE;
    mr_size_t i, j;

    if ((!ee) || (!ee->mem) || (ee->size == 0U)) {
        return -MR_EIO;
    }

    for (i = 0U; i < num; i++) {
        const mr_uint8_t *tx = (const mr_uint8_t *)msgs[i].tx_buf;
        mr_uint8_t *rx = (mr_uint8_t *)msgs[i].rx_buf;
        mr_size_t len = (msgs[i].tx_len > msgs[i].rx_len) ? msgs[i].tx_len
                                                          : msgs[i].rx_len;

        for (j = 0U; j < len; j++) {
            mr_uint8_t out_b = (tx && (j < msgs[i].tx_len)) ? tx[j] : 0xFFU;
            mr_uint8_t in_b = 0xFFU;

            if (!has_cmd) {
                cmd = out_b;
                has_cmd = MR_TRUE;
                if (cmd == WIN_SPI_EE_CMD_WREN) {
                    ee->write_enable = MR_TRUE;
                } else if (cmd == WIN_SPI_EE_CMD_WRDI) {
                    ee->write_enable = MR_FALSE;
                }
            } else if (cmd == WIN_SPI_EE_CMD_RDSR) {
                in_b = ee->write_enable ? 0x02U : 0x00U;
            } else if ((cmd == WIN_SPI_EE_CMD_READ)
                       || (cmd == WIN_SPI_EE_CMD_WRITE)) {
                if (addr_got < ee->addr_bytes) {
                    addr = (addr << 8) | out_b;
                    addr_got++;
                } else if (cmd == WIN_SPI_EE_CMD_READ) {
                    in_b = ee->mem[addr % ee->size];
                    addr = (addr + 1U) % ee->size;
                } else if (ee->write_enable) {
                    ee->mem[addr % ee->size] = out_b;
                    addr = (addr + 1U) % ee->size;
                }
            }

            if (rx && (j < msgs[i].rx_len)) {
                rx[j] = in_b;
            }
        }
    }

    if (has_cmd && (cmd == WIN_SPI_EE_CMD_WRITE)) {
        ee->write_enable = MR_FALSE;
    }
    return 0;
}

/* =========================== Simulation Helpers =========================== */

mr_err_t mr_sim_spi_init(mr_driver_t *drv, mr_sim_spi_t *sim,
                         mr_uint32_t cs_num) {
    mr_err_t ret;

    if ((!drv) || (!sim) || (cs_num == 0U) || (cs_num > MR_SIM_SPI_CS_MAX)) {
        return -MR_EINVAL;
    }

    mr_memset(sim, 0, sizeof(*sim));
    mr_spinlock_init(&sim->lock);
    sim->cs_num = cs_num;

    ret = mr_spi_init(drv, &sim->data, sim, (mr_spi_ops_t *)&__win_spi_ops);
    if (ret != 0) {
        return ret;
    }
    ret = mr_driver_setup(drv);
    if (ret != 0) {
        mr_spi_del(drv);
        return ret;
    }
    return 0;
}

mr_err_t mr_sim_spi_attach(mr_driver_t *drv, const mr_sim_spi_dev_t *dev) {
    mr_sim_spi_t *sim;
    int state;

    if ((!drv) || (!dev) || (!dev->transfer)) {
        return -MR_EINVAL;
    }
    sim = (mr_sim_spi_t *)MR_DRIVER_PRIV(drv);
    if ((!sim) || (dev->cs >= sim->cs_num)) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    if (sim->dev_used[dev->cs]) {
        mr_spinlock_unlock_irqrestore(&sim->lock, state);
        return -MR_EBUSY;
    }
    sim->devs[dev->cs] = *dev;
    sim->dev_used[dev->cs] = MR_TRUE;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

mr_err_t mr_sim_spi_detach(mr_driver_t *drv, mr_uint32_t cs) {
    mr_sim_spi_t *sim;
    int state;

    if (!drv) {
        return -MR_EINVAL;
    }
    sim = (mr_sim_spi_t *)MR_DRIVER_PRIV(drv);
    if ((!sim) || (cs >= sim->cs_num)) {
        return -MR_EINVAL;
    }

    state = mr_spinlock_lock_irqsave(&sim->lock);
    if (!sim->dev_used[cs]) {
        mr_spinlock_unlock_irqrestore(&sim->lock, state);
        return -MR_ENOENT;
    }
    sim->dev_used[cs] = MR_FALSE;
    mr_spinlock_unlock_irqrestore(&sim->lock, state);
    return 0;
}

mr_err_t mr_sim_spi_eeprom_attach(mr_driver_t *drv, mr_sim_spi_eeprom_t *eeprom,
                                  mr_uint32_t cs, mr_uint8_t *mem,
                                  mr_size_t size, mr_uint8_t addr_bytes) {
    mr_sim_spi_dev_t dev;

    if ((!drv) || (!eeprom) || (!mem) || (size == 0U)
        || ((addr_bytes != 1U) && (addr_bytes != 2U))) {
        return -MR_EINVAL;
    }

    eeprom->mem = mem;
    eeprom->size = size;
    eeprom->addr_bytes = addr_bytes;
    eeprom->write_enable = MR_FALSE;

    dev.cs = cs;
    dev.transfer = win_spi_eeprom_xfer;
    dev.priv = eeprom;
    return mr_sim_spi_attach(drv, &dev);
}

/* ============================ HardTree Install ============================ */
#define MR_HT_COMPAT                    win_spi
#if MR_HT_HAS_DRIVER()
#define WIN_HT_SPI_INSTALL(_id, _ops)                                          \
    static mr_sim_spi_t MR_CAT(__win_spi_priv_, _id) = {                       \
        .cs_num = MR_HT_DRIVER_PROP_OR(_id, win_cs_num, 4U),                   \
        .lock = MR_SPINLOCK_INIT(),                                            \
    };                                                                         \
    MR_HT_SPI_INSTALL(_id, &MR_CAT(__win_spi_priv_, _id), _ops)

MR_HT_DRIVER_FOREACH(WIN_HT_SPI_INSTALL, &__win_spi_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_SPI) */

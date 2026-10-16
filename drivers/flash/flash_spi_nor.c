/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/drivers/flash/mr_flash.h>
#if defined(MR_USE_DRIVER_FLASH) && defined(MR_USE_DRIVER_SPI)
#include <mrX/drivers/spi/mr_spi.h>
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/kernel/mr_clock.h>
#include <mrX/libc/mr_string.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    flash_spi_nor
#if MR_HT_HAS_DRIVER()

/* Driver FLASH operations */
static mr_err_t drv_probe(const mr_driver_t *drv, mr_flash_info_t *info);
static mr_err_t drv_region(const mr_driver_t *drv, mr_uint32_t index,
                           mr_flash_region_t *region);
static mr_ssize_t drv_read(const mr_driver_t *drv, mr_uint64_t offset,
                           mr_uint8_t *buf, mr_size_t size);
static mr_ssize_t drv_program(const mr_driver_t *drv, mr_uint64_t offset,
                              const mr_uint8_t *buf, mr_size_t size);
static mr_err_t drv_erase(const mr_driver_t *drv, mr_uint64_t offset,
                          mr_uint64_t size);
MR_UNUSED static const mr_flash_ops_t __drv_flash_spi_nor_ops = {
    .probe = drv_probe,
    .region = drv_region,
    .read = drv_read,
    .program = drv_program,
    .erase = drv_erase,
};

/* SPI NOR cmd definition */
#define SPI_NOR_CMD_READ                (0x03U)
#define SPI_NOR_CMD_READ_4B             (0x13U)
#define SPI_NOR_CMD_READ_STATUS         (0x05U)
#define SPI_NOR_CMD_READ_STATUS_2       (0x07U)
#define SPI_NOR_CMD_WRITE_DISABLE       (0x04U)
#define SPI_NOR_CMD_WRITE_ENABLE        (0x06U)
#define SPI_NOR_CMD_PAGE_PROGRAM        (0x02U)
#define SPI_NOR_CMD_PAGE_PROGRAM_4B     (0x12U)
#define SPI_NOR_CMD_READ_SFDP           (0x5aU)
#define SPI_NOR_CMD_READ_ID             (0x9fU)
#define SPI_NOR_CMD_READ_FLAG_STATUS    (0x70U)
#define SPI_NOR_CMD_CLEAR_FLAG_STATUS   (0x50U)
#define SPI_NOR_CMD_CLEAR_STATUS        (0x30U)

/* SPI NOR erase cmd definition */
#define SPI_NOR_CMD_ERASE_4K            (0x20U)
#define SPI_NOR_CMD_ERASE_4K_4B         (0x21U)
#define SPI_NOR_CMD_ERASE_32K           (0x52U)
#define SPI_NOR_CMD_ERASE_32K_4B        (0x5cU)
#define SPI_NOR_CMD_ERASE_64K           (0xd8U)
#define SPI_NOR_CMD_ERASE_64K_4B        (0xdcU)
#define SPI_NOR_CMD_ERASE_256K          (0xd8U)

/* SPI NOR status definition */
#define SPI_NOR_STATUS_BUSY             (1U << 0)
#define SPI_NOR_STATUS_WEL              (1U << 1)
#define SPI_NOR_STATUS_2_CYPRESS_ERROR  ((1U << 5) | (1U << 6))
#define SPI_NOR_FLAG_STATUS_READY       (1U << 7)
#define SPI_NOR_FLAG_STATUS_ERROR       ((1U << 4) | (1U << 5))

/* SPI NOR SFDP definition */
#define SPI_NOR_SFDP_HEADER_SIZE        (8U)
#define SPI_NOR_SFDP_SIGNATURE          "SFDP"
#define SPI_NOR_SFDP_MAJOR              (1U)
#define SPI_NOR_BFPT_ID_LSB             (0x00U)
#define SPI_NOR_BFPT_ID_MSB             (0xffU)
#define SPI_NOR_BFPT_DWORDS_MIN         (9U)
#define SPI_NOR_BFPT_DWORDS_USED        (11U)
#define SPI_NOR_SMPT_ID_LSB             (0x81U)
#define SPI_NOR_SMPT_ID_MSB             (0xffU)
#define SPI_NOR_4BAIT_ID_LSB            (0x84U)
#define SPI_NOR_4BAIT_ID_MSB            (0xffU)
#define SPI_NOR_4BAIT_DWORDS            (2U)

/* SPI NOR SMPT definition */
#define SPI_NOR_SMPT_CMD_ADDR_SHIFT     (22U)
#define SPI_NOR_SMPT_CMD_DUMMY_SHIFT    (16U)
#define SPI_NOR_SMPT_CMD_OPCODE_SHIFT   (8U)
#define SPI_NOR_SMPT_MAP_REGIONS_SHIFT  (16U)
#define SPI_NOR_SMPT_MAP_ID_SHIFT       (8U)
#define SPI_NOR_SMPT_DESC_MAP           (1U << 1)
#define SPI_NOR_SMPT_DESC_END           (1U << 0)
#define SPI_NOR_SMPT_REGION_ERASE_MASK  (0x0fU)
#define SPI_NOR_SMPT_REGION_SIZE_SHIFT  (8U)

/* SPI NOR 4BAIT definition */
#define SPI_NOR_4BAIT_READ              (1U << 0)
#define SPI_NOR_4BAIT_PROGRAM           (1U << 6)
#define SPI_NOR_4BAIT_ERASE_SHIFT       (9U)

/* SPI NOR limit definition */
#define SPI_NOR_3B_ADDRESS_LIMIT        (0x01000000ULL)
#define SPI_NOR_4B_ADDRESS_LIMIT        (0x100000000ULL)
#define SPI_NOR_DENSITY_LIMIT_BITS      (1ULL << 35)
#define SPI_NOR_DEFAULT_PAGE_SIZE       (256U)
#define SPI_NOR_VERIFY_BUFFER_SIZE      (64U)
#define SPI_NOR_JEDEC_ID_SIZE           (3U)
#define SPI_NOR_ERASE_TYPES_MAX         (4U)
#define SPI_NOR_POLL_DELAY_US           (1000U)
#define SPI_NOR_DEFAULT_CLOCK_RATE      (100000U)
#define SPI_NOR_PROGRAM_TIMEOUT_MS      (100U)
#define SPI_NOR_ERASE_TIMEOUT_MS        (10000U)

/* SPI NOR part flag definition */
#define SPI_NOR_PART_SKIP_SFDP          (1U << 0)
#define SPI_NOR_PART_4B_OPCODES         (1U << 1)
#define SPI_NOR_PART_ERASE_4K           (1U << 2)
#define SPI_NOR_PART_ERASE_32K          (1U << 3)
#define SPI_NOR_PART_ERASE_64K          (1U << 4)
#define SPI_NOR_PART_ERASE_256K         (1U << 5)
#define SPI_NOR_PART_MICRON_FSR         (1U << 6)
#define SPI_NOR_PART_CYPRESS_SR         (1U << 7)

/* SPI NOR erase type */
typedef struct spi_nor_erase_type {
    mr_uint32_t size;
    mr_uint8_t opcode;
} spi_nor_erase_type_t;

/* SPI NOR parameter header type */
typedef struct spi_nor_param_header {
    mr_uint32_t pointer;
    mr_uint8_t dwords;
    mr_uint8_t minor;
    mr_bool_t present;
} spi_nor_param_header_t;

/* SPI NOR parameter tables type */
typedef struct spi_nor_tables {
    spi_nor_param_header_t bfpt;
    spi_nor_param_header_t smpt;
    spi_nor_param_header_t bait;
} spi_nor_tables_t;

/* SPI NOR geometry type */
typedef struct spi_nor_geometry {
    mr_uint64_t size;
    mr_uint32_t page_size;
    mr_uint32_t erase_size;
    spi_nor_erase_type_t erase_types[SPI_NOR_ERASE_TYPES_MAX];
    mr_uint8_t erase_mask;
    mr_uint8_t addr_size;
    mr_uint8_t read_opcode;
    mr_uint8_t program_opcode;
} spi_nor_geometry_t;

/* SPI NOR part type */
typedef struct spi_nor_part {
    mr_uint8_t id[SPI_NOR_JEDEC_ID_SIZE];
    mr_uint64_t size;
    mr_uint8_t flags;
} spi_nor_part_t;

/* SPI NOR verification type */
typedef enum spi_nor_check {
    SPI_NOR_CHECK_MATCH,
    SPI_NOR_CHECK_PROGRAMMABLE,
    SPI_NOR_CHECK_ERASED,
} spi_nor_check_t;

/* SPI NOR status type */
typedef enum spi_nor_status_type {
    SPI_NOR_STATUS_BASIC,
    SPI_NOR_STATUS_MICRON,
    SPI_NOR_STATUS_CYPRESS,
} spi_nor_status_type_t;

/* Driver FLASH private data type */
typedef struct drv_flash_spi_nor_priv {
    mr_ht_spi_spec_t spi;
    mr_spi_config_t config;
    spi_nor_geometry_t geometry;
    mr_spinlock_t lock;
    mr_uint32_t program_timeout_ms;
    mr_uint32_t erase_timeout_ms;
    spi_nor_status_type_t status_type;
} drv_flash_spi_nor_priv_t;

/* SPI NOR compatibility table */
static const spi_nor_part_t spi_nor_parts[] = {
    /* ST */
    {
     .id = {0x20U, 0x20U, 0x18U},
     .size = 0x01000000ULL,
     .flags = SPI_NOR_PART_SKIP_SFDP | SPI_NOR_PART_ERASE_256K,
     },
    /* Winbond */
    {
     .id = {0xefU, 0x40U, 0x18U},
     .size = 0x01000000ULL,
     .flags = SPI_NOR_PART_ERASE_4K | SPI_NOR_PART_ERASE_32K
     | SPI_NOR_PART_ERASE_64K,
     },
    {
     .id = {0xefU, 0x40U, 0x19U},
     .size = 0x02000000ULL,
     .flags = SPI_NOR_PART_4B_OPCODES | SPI_NOR_PART_ERASE_4K
     | SPI_NOR_PART_ERASE_32K | SPI_NOR_PART_ERASE_64K,
     },
    /* Macronix */
    {
     .id = {0xc2U, 0x20U, 0x18U},
     .size = 0x01000000ULL,
     .flags = SPI_NOR_PART_ERASE_4K | SPI_NOR_PART_ERASE_64K,
     },
    {
     .id = {0xc2U, 0x20U, 0x19U},
     .size = 0x02000000ULL,
     .flags = SPI_NOR_PART_4B_OPCODES | SPI_NOR_PART_ERASE_4K
     | SPI_NOR_PART_ERASE_64K,
     },
    {
     .id = {0xc2U, 0x20U, 0x1aU},
     .size = 0x04000000ULL,
     .flags = SPI_NOR_PART_4B_OPCODES | SPI_NOR_PART_ERASE_4K
     | SPI_NOR_PART_ERASE_32K | SPI_NOR_PART_ERASE_64K,
     },
    /* GigaDevice */
    {
     .id = {0xc8U, 0x40U, 0x18U},
     .size = 0x01000000ULL,
     .flags = SPI_NOR_PART_ERASE_4K | SPI_NOR_PART_ERASE_32K
     | SPI_NOR_PART_ERASE_64K,
     },
    {
     .id = {0xc8U, 0x40U, 0x19U},
     .size = 0x02000000ULL,
     .flags = SPI_NOR_PART_4B_OPCODES | SPI_NOR_PART_ERASE_4K
     | SPI_NOR_PART_ERASE_32K | SPI_NOR_PART_ERASE_64K,
     },
    /* Micron */
    {
     .id = {0x20U, 0xbaU, 0x18U},
     .size = 0x01000000ULL,
     .flags = SPI_NOR_PART_ERASE_4K | SPI_NOR_PART_ERASE_64K
     | SPI_NOR_PART_MICRON_FSR,
     },
    {
     .id = {0x20U, 0xbaU, 0x19U},
     .size = 0x02000000ULL,
     .flags = SPI_NOR_PART_4B_OPCODES | SPI_NOR_PART_ERASE_4K
     | SPI_NOR_PART_ERASE_64K | SPI_NOR_PART_MICRON_FSR,
     },
    /* Infineon (Cypress) */
    {
     .id = {0x01U, 0x60U, 0x18U},
     .size = 0x01000000ULL,
     .flags = SPI_NOR_PART_ERASE_4K | SPI_NOR_PART_ERASE_64K
     | SPI_NOR_PART_CYPRESS_SR,
     },
    {
     .id = {0x01U, 0x60U, 0x19U},
     .size = 0x02000000ULL,
     .flags = SPI_NOR_PART_4B_OPCODES | SPI_NOR_PART_ERASE_4K
     | SPI_NOR_PART_ERASE_64K | SPI_NOR_PART_CYPRESS_SR,
     },
};

static mr_uint32_t spi_nor_le32(const mr_uint8_t *buf) {
    /* Decode little-endian value */
    return (mr_uint32_t)buf[0] | ((mr_uint32_t)buf[1] << 8)
         | ((mr_uint32_t)buf[2] << 16) | ((mr_uint32_t)buf[3] << 24);
}

static mr_err_t spi_nor_transfer(drv_flash_spi_nor_priv_t *priv,
                                 const mr_uint8_t *cmd, mr_size_t cmd_size,
                                 const void *tx_buf, void *rx_buf,
                                 mr_size_t data_size) {
    mr_ssize_t transferred;
    mr_spi_msg_t msg[2];
    mr_size_t count;

    /* Keep chip select active across the cmd and data phases */
    msg[0].tx_buf = cmd;
    msg[0].tx_len = cmd_size;
    msg[0].rx_buf = MR_NULL;
    msg[0].rx_len = 0U;
    msg[1].tx_buf = tx_buf;
    msg[1].tx_len = (tx_buf != MR_NULL) ? data_size : 0U;
    msg[1].rx_buf = rx_buf;
    msg[1].rx_len = (rx_buf != MR_NULL) ? data_size : 0U;
    count = (data_size != 0U) ? 2U : 1U;

    /* Transfer messages */
    transferred = mr_ht_spi_transfer(&priv->spi, msg, count);
    if (transferred < 0) {
        return (mr_err_t)transferred;
    }
    return (transferred == (mr_ssize_t)count) ? 0 : -MR_EIO;
}

static mr_err_t spi_nor_command(drv_flash_spi_nor_priv_t *priv,
                                mr_uint8_t opcode) {
    /* Send command */
    return spi_nor_transfer(priv, &opcode, sizeof(opcode), MR_NULL, MR_NULL,
                            0U);
}

static mr_err_t spi_nor_read_register(drv_flash_spi_nor_priv_t *priv,
                                      mr_uint8_t opcode, mr_uint8_t *value) {
    /* Read register */
    return spi_nor_transfer(priv, &opcode, sizeof(opcode), MR_NULL, value,
                            sizeof(*value));
}

static mr_err_t spi_nor_read_status(drv_flash_spi_nor_priv_t *priv,
                                    mr_uint8_t *status) {
    /* Read status register */
    return spi_nor_read_register(priv, SPI_NOR_CMD_READ_STATUS, status);
}

static mr_err_t spi_nor_clear_error(drv_flash_spi_nor_priv_t *priv) {
    mr_err_t ret;

    /* Clear vendor error state */
    if (priv->status_type == SPI_NOR_STATUS_CYPRESS) {
        return spi_nor_command(priv, SPI_NOR_CMD_CLEAR_STATUS);
    }
    ret = spi_nor_command(priv, SPI_NOR_CMD_CLEAR_FLAG_STATUS);
    if (ret != 0) {
        return ret;
    }
    return spi_nor_command(priv, SPI_NOR_CMD_WRITE_DISABLE);
}

static mr_err_t spi_nor_wait_ready(drv_flash_spi_nor_priv_t *priv,
                                   mr_uint32_t timeout_ms) {
    mr_uint8_t vendor_status, status;
    mr_tick_t timeout_ticks, start;
    mr_uint32_t polls;
    mr_bool_t ready;
    mr_err_t ret;

    /* Initialize timeout */
    timeout_ticks = MR_TICK_MSEC(timeout_ms);
    if (timeout_ticks == 0U) {
        timeout_ticks = 1U;
    }
    start = mr_clock_tick();
    polls = 0U;

    /* Wait until ready */
    while (1) {
        ret = spi_nor_read_status(priv, &status);
        if (ret != 0) {
            return ret;
        }
        ready = ((status & SPI_NOR_STATUS_BUSY) == 0U) ? MR_TRUE : MR_FALSE;

        /* Check vendor status */
        if (priv->status_type == SPI_NOR_STATUS_CYPRESS) {
            ret = spi_nor_read_register(priv, SPI_NOR_CMD_READ_STATUS_2,
                                        &vendor_status);
            if (ret != 0) {
                return ret;
            }
            if ((vendor_status & SPI_NOR_STATUS_2_CYPRESS_ERROR) != 0U) {
                ret = spi_nor_clear_error(priv);
                if (ret != 0) {
                    return ret;
                }
                return -MR_EIO;
            }
        } else if (priv->status_type == SPI_NOR_STATUS_MICRON) {
            ret = spi_nor_read_register(priv, SPI_NOR_CMD_READ_FLAG_STATUS,
                                        &vendor_status);
            if (ret != 0) {
                return ret;
            }
            if (ready && ((vendor_status & SPI_NOR_FLAG_STATUS_ERROR) != 0U)) {
                ret = spi_nor_clear_error(priv);
                if (ret != 0) {
                    return ret;
                }
                return -MR_EIO;
            }
            ready
                = ready && ((vendor_status & SPI_NOR_FLAG_STATUS_READY) != 0U);
        }
        if (ready) {
            return 0;
        }

        /* Poll count also bounds the wait when the system tick is stopped */
        if (((mr_tick_t)(mr_clock_tick() - start) >= timeout_ticks)
            || (polls >= timeout_ms)) {
            return -MR_ETIMEDOUT;
        }
        mr_clock_busy_wait(SPI_NOR_POLL_DELAY_US);
        polls++;
    }
}

static mr_err_t spi_nor_write_enable(drv_flash_spi_nor_priv_t *priv) {
    mr_uint8_t status;
    mr_err_t ret;

    /* Enable write */
    ret = spi_nor_command(priv, SPI_NOR_CMD_WRITE_ENABLE);
    if (ret != 0) {
        return ret;
    }

    /* Verify write enable */
    ret = spi_nor_read_status(priv, &status);
    if (ret != 0) {
        return ret;
    }
    if ((status & (SPI_NOR_STATUS_BUSY | SPI_NOR_STATUS_WEL))
        != SPI_NOR_STATUS_WEL) {
        return -MR_EIO;
    }
    return 0;
}

static mr_err_t spi_nor_addr_cmd(drv_flash_spi_nor_priv_t *priv,
                                 mr_uint8_t opcode, mr_uint32_t addr,
                                 const void *tx_buf, void *rx_buf,
                                 mr_size_t data_size) {
    mr_size_t cmd_size;
    mr_uint64_t limit;
    mr_uint8_t cmd[5];
    mr_uint8_t i;

    /* Check address range */
    if (priv->geometry.addr_size == 3U) {
        limit = SPI_NOR_3B_ADDRESS_LIMIT;
    } else if (priv->geometry.addr_size == 4U) {
        limit = SPI_NOR_4B_ADDRESS_LIMIT;
    } else {
        return -MR_ENOSYS;
    }
    if (((mr_uint64_t)addr >= limit)
        || ((mr_uint64_t)data_size > (limit - addr))) {
        return -MR_EINVAL;
    }

    /* Build address command */
    cmd[0] = opcode;
    for (i = 0U; i < priv->geometry.addr_size; i++) {
        cmd[i + 1U]
            = (mr_uint8_t)(addr >> ((priv->geometry.addr_size - i - 1U) * 8U));
    }
    cmd_size = (mr_size_t)priv->geometry.addr_size + 1U;
    return spi_nor_transfer(priv, cmd, cmd_size, tx_buf, rx_buf, data_size);
}

static mr_err_t spi_nor_detect_read(drv_flash_spi_nor_priv_t *priv,
                                    mr_uint8_t opcode, mr_uint32_t addr,
                                    mr_uint8_t addr_size,
                                    mr_uint8_t dummy_cycles,
                                    mr_uint8_t *value) {
    mr_size_t cmd_size;
    mr_uint8_t cmd[6];
    mr_uint8_t i;

    /* Check detection command format */
    if (((addr_size != 0U) && (addr_size != 3U) && (addr_size != 4U))
        || ((dummy_cycles != 0U) && (dummy_cycles != 8U)) || (opcode == 0U)
        || (opcode == 0xffU)) {
        return -MR_ENOSYS;
    }

    /* Build detection command */
    cmd[0] = opcode;
    for (i = 0U; i < addr_size; i++) {
        cmd[i + 1U] = (mr_uint8_t)(addr >> ((addr_size - i - 1U) * 8U));
    }
    cmd_size = (mr_size_t)addr_size + 1U;
    if (dummy_cycles != 0U) {
        cmd[cmd_size] = 0U;
        cmd_size++;
    }
    return spi_nor_transfer(priv, cmd, cmd_size, MR_NULL, value,
                            sizeof(*value));
}

static mr_err_t spi_nor_read_sfdp(drv_flash_spi_nor_priv_t *priv,
                                  mr_uint32_t addr, void *buf, mr_size_t size) {
    mr_uint8_t cmd[5];

    /* Check SFDP address range */
    if ((addr >= SPI_NOR_3B_ADDRESS_LIMIT)
        || (size > (SPI_NOR_3B_ADDRESS_LIMIT - addr))) {
        return -MR_EINVAL;
    }

    /* Read SFDP data */
    cmd[0] = SPI_NOR_CMD_READ_SFDP;
    cmd[1] = (mr_uint8_t)(addr >> 16);
    cmd[2] = (mr_uint8_t)(addr >> 8);
    cmd[3] = (mr_uint8_t)addr;
    cmd[4] = 0U;
    return spi_nor_transfer(priv, cmd, sizeof(cmd), MR_NULL, buf, size);
}

static mr_err_t spi_nor_read_id(drv_flash_spi_nor_priv_t *priv,
                                mr_uint8_t *id) {
    mr_uint8_t opcode;
    mr_err_t ret;

    /* Read JEDEC manufacturer and device identifiers */
    opcode = SPI_NOR_CMD_READ_ID;
    ret = spi_nor_transfer(priv, &opcode, sizeof(opcode), MR_NULL, id,
                           SPI_NOR_JEDEC_ID_SIZE);
    if (ret != 0) {
        return ret;
    }

    /* Reject floating or unresponsive buses */
    if (((id[0] == 0U) && (id[1] == 0U) && (id[2] == 0U))
        || ((id[0] == 0xffU) && (id[1] == 0xffU) && (id[2] == 0xffU))) {
        return -MR_ENODEV;
    }
    return 0;
}

static const spi_nor_part_t *spi_nor_find_part(const mr_uint8_t *id) {
    mr_size_t i;

    /* Find an exact three-byte JEDEC identifier */
    for (i = 0U; i < MR_ARRAY_SIZE(spi_nor_parts); i++) {
        if (mr_memcmp(id, spi_nor_parts[i].id, SPI_NOR_JEDEC_ID_SIZE) == 0) {
            return &spi_nor_parts[i];
        }
    }
    return MR_NULL;
}

static void spi_nor_select_header(spi_nor_param_header_t *selected,
                                  const mr_uint8_t *header) {
    mr_uint32_t pointer;

    /* Keep the newest supported revision and its longest table */
    if ((header[2] != SPI_NOR_SFDP_MAJOR)
        || (selected->present
            && ((header[1] < selected->minor)
                || ((header[1] == selected->minor)
                    && (header[3] <= selected->dwords))))) {
        return;
    }

    /* Decode the 24-bit parameter table pointer */
    pointer = (mr_uint32_t)header[4] | ((mr_uint32_t)header[5] << 8)
            | ((mr_uint32_t)header[6] << 16);
    selected->pointer = pointer;
    selected->dwords = header[3];
    selected->minor = header[1];
    selected->present = MR_TRUE;
}

static mr_err_t spi_nor_validate_header(const spi_nor_param_header_t *header) {
    mr_uint32_t table_size;

    /* Ignore an absent optional parameter table */
    if (!header->present) {
        return 0;
    }

    /* Validate the complete DWORD-aligned SFDP table range */
    table_size = (mr_uint32_t)header->dwords * sizeof(mr_uint32_t);
    if ((table_size == 0U)
        || ((header->pointer & (sizeof(mr_uint32_t) - 1U)) != 0U)
        || (table_size > (SPI_NOR_3B_ADDRESS_LIMIT - header->pointer))) {
        return -MR_EIO;
    }
    return 0;
}

static mr_err_t spi_nor_read_headers(drv_flash_spi_nor_priv_t *priv,
                                     spi_nor_tables_t *tables,
                                     mr_bool_t *available) {
    mr_uint8_t sfdp_header[SPI_NOR_SFDP_HEADER_SIZE], param_header[8];
    mr_uint32_t param_count, i;
    mr_err_t ret;

    /* Start with no discovered parameter tables */
    mr_memset(tables, 0, sizeof(*tables));
    *available = MR_FALSE;

    /* Read and validate the SFDP header */
    ret = spi_nor_read_sfdp(priv, 0U, sfdp_header, sizeof(sfdp_header));
    if (ret != 0) {
        return ret;
    }
    if ((mr_memcmp(sfdp_header, SPI_NOR_SFDP_SIGNATURE, 4U) != 0)
        || (sfdp_header[5] != SPI_NOR_SFDP_MAJOR)
        || (sfdp_header[7] != 0xffU)) {
        return 0;
    }

    /* Select the newest supported parameter tables */
    param_count = (mr_uint32_t)sfdp_header[6] + 1U;
    for (i = 0U; i < param_count; i++) {
        ret = spi_nor_read_sfdp(
            priv, SPI_NOR_SFDP_HEADER_SIZE + i * sizeof(param_header),
            param_header, sizeof(param_header));
        if (ret != 0) {
            return ret;
        }
        if ((param_header[0] == SPI_NOR_BFPT_ID_LSB)
            && (param_header[7] == SPI_NOR_BFPT_ID_MSB)) {
            spi_nor_select_header(&tables->bfpt, param_header);
        } else if ((param_header[0] == SPI_NOR_SMPT_ID_LSB)
                   && (param_header[7] == SPI_NOR_SMPT_ID_MSB)) {
            spi_nor_select_header(&tables->smpt, param_header);
        } else if ((param_header[0] == SPI_NOR_4BAIT_ID_LSB)
                   && (param_header[7] == SPI_NOR_4BAIT_ID_MSB)) {
            spi_nor_select_header(&tables->bait, param_header);
        }
    }

    /* Treat a missing or truncated BFPT as unavailable SFDP */
    if (!tables->bfpt.present
        || (tables->bfpt.dwords < SPI_NOR_BFPT_DWORDS_MIN)) {
        return 0;
    }

    /* Validate every selected parameter table pointer */
    ret = spi_nor_validate_header(&tables->bfpt);
    if (ret == 0) {
        ret = spi_nor_validate_header(&tables->smpt);
    }
    if (ret == 0) {
        ret = spi_nor_validate_header(&tables->bait);
    }
    if (ret != 0) {
        return ret;
    }
    *available = MR_TRUE;
    return 0;
}

static mr_err_t spi_nor_read_bfpt(drv_flash_spi_nor_priv_t *priv,
                                  const spi_nor_param_header_t *header,
                                  mr_uint8_t *table,
                                  mr_uint32_t *table_dwords) {
    mr_size_t table_size;

    /* Read selected parameter table */
    *table_dwords
        = MR_MIN((mr_uint32_t)header->dwords, SPI_NOR_BFPT_DWORDS_USED);
    table_size = (mr_size_t)*table_dwords * sizeof(mr_uint32_t);
    return spi_nor_read_sfdp(priv, header->pointer, table, table_size);
}

static void spi_nor_set_erase_type(spi_nor_geometry_t *geometry,
                                   mr_uint8_t index, mr_uint32_t size,
                                   mr_uint8_t opcode) {
    /* Store an erase type at its SFDP-defined index */
    geometry->erase_types[index].size = size;
    geometry->erase_types[index].opcode = opcode;
    geometry->erase_mask |= (mr_uint8_t)(1U << index);
}

static mr_err_t spi_nor_parse_bfpt(const mr_uint8_t *table,
                                   mr_uint32_t table_dwords,
                                   spi_nor_geometry_t *geometry) {
    mr_uint32_t address_support, density, exponent, erase_index;
    mr_uint64_t density_bits;

    /* Initialize geometry defaults */
    mr_memset(geometry, 0, sizeof(*geometry));
    geometry->page_size = SPI_NOR_DEFAULT_PAGE_SIZE;
    geometry->read_opcode = SPI_NOR_CMD_READ;
    geometry->program_opcode = SPI_NOR_CMD_PAGE_PROGRAM;

    /* Decode supported address width */
    address_support = (spi_nor_le32(table) >> 17) & 0x03U;
    if (address_support <= 1U) {
        geometry->addr_size = 3U;
    } else if (address_support == 2U) {
        geometry->addr_size = 4U;
    } else {
        return -MR_EIO;
    }

    /* Decode flash density */
    density = spi_nor_le32(table + sizeof(mr_uint32_t));
    if ((density & 0x80000000U) != 0U) {
        exponent = density & 0x7fffffffU;
        if (exponent < 3U) {
            return -MR_EIO;
        }
        if (exponent > 35U) {
            return -MR_ENOSYS;
        }
        density_bits = 1ULL << exponent;
    } else {
        density_bits = (mr_uint64_t)density + 1ULL;
    }
    if (((density_bits & 0x07U) != 0U) || (density_bits < 8U)) {
        return -MR_EIO;
    }
    if (density_bits > SPI_NOR_DENSITY_LIMIT_BITS) {
        return -MR_ENOSYS;
    }

    /* Decode page size */
    geometry->size = density_bits / 8U;
    if (table_dwords >= 11U) {
        exponent
            = (spi_nor_le32(table + 10U * sizeof(mr_uint32_t)) >> 4) & 0x0fU;
        if (exponent == 0U) {
            return -MR_EIO;
        }
        geometry->page_size = 1UL << exponent;
    }

    /* Decode every advertised erase type */
    for (erase_index = 0U; erase_index < SPI_NOR_ERASE_TYPES_MAX;
         erase_index++) {
        exponent = table[28U + erase_index * 2U];
        if (exponent == 0U) {
            continue;
        }
        if ((exponent > 24U) || (table[29U + erase_index * 2U] == 0U)
            || (table[29U + erase_index * 2U] == 0xffU)) {
            return -MR_EIO;
        }
        spi_nor_set_erase_type(geometry, (mr_uint8_t)erase_index,
                               1UL << exponent, table[29U + erase_index * 2U]);
    }
    if (geometry->erase_mask == 0U) {
        return -MR_ENOSYS;
    }
    return 0;
}

static mr_err_t spi_nor_read_sfdp_dword(drv_flash_spi_nor_priv_t *priv,
                                        mr_uint32_t pointer, mr_uint32_t index,
                                        mr_uint32_t *value) {
    mr_uint8_t data[sizeof(mr_uint32_t)];
    mr_err_t ret;

    /* Read and decode one table DWORD */
    ret = spi_nor_read_sfdp(priv, pointer + index * sizeof(mr_uint32_t), data,
                            sizeof(data));
    if (ret != 0) {
        return ret;
    }
    *value = spi_nor_le32(data);
    return 0;
}

static mr_err_t spi_nor_parse_smpt(drv_flash_spi_nor_priv_t *priv,
                                   const spi_nor_param_header_t *header,
                                   spi_nor_geometry_t *geometry) {
    mr_uint64_t region_size, total_size, region_offset;
    mr_uint32_t index, region_count, i, type, unit;
    mr_uint8_t detection_count, map_id, value;
    mr_uint32_t descriptor, address, region;
    mr_uint8_t dummy_cycles, opcode, mask;
    mr_uint8_t common_mask, address_code;
    mr_err_t ret;

    /* Execute configuration detection descriptors */
    index = 0U;
    map_id = 0U;
    detection_count = 0U;
    while (index < header->dwords) {
        ret = spi_nor_read_sfdp_dword(priv, header->pointer, index,
                                      &descriptor);
        if (ret != 0) {
            return ret;
        }
        if ((descriptor & SPI_NOR_SMPT_DESC_MAP) != 0U) {
            break;
        }
        if ((index + 1U) >= header->dwords) {
            return -MR_EIO;
        }
        if (detection_count >= 8U) {
            return -MR_EIO;
        }
        ret = spi_nor_read_sfdp_dword(priv, header->pointer, index + 1U,
                                      &address);
        if (ret != 0) {
            return ret;
        }

        /* Decode and issue the detection read */
        address_code
            = (mr_uint8_t)((descriptor >> SPI_NOR_SMPT_CMD_ADDR_SHIFT) & 0x03U);
        if (address_code == 1U) {
            address_code = 3U;
        } else if (address_code == 2U) {
            address_code = 4U;
        } else if (address_code == 3U) {
            address_code = geometry->addr_size;
        }
        dummy_cycles = (mr_uint8_t)((descriptor >> SPI_NOR_SMPT_CMD_DUMMY_SHIFT)
                                    & 0x0fU);
        if (dummy_cycles == 0x0fU) {
            dummy_cycles = 0U;
        }
        opcode = (mr_uint8_t)(descriptor >> SPI_NOR_SMPT_CMD_OPCODE_SHIFT);
        mask = (mr_uint8_t)(descriptor >> 24);
        ret = spi_nor_detect_read(priv, opcode, address, address_code,
                                  dummy_cycles, &value);
        if (ret != 0) {
            return ret;
        }
        map_id
            = (mr_uint8_t)((map_id << 1) | (((value & mask) != 0U) ? 1U : 0U));
        detection_count++;
        index += 2U;
    }

    /* Find the map selected by the detection result */
    while (index < header->dwords) {
        ret = spi_nor_read_sfdp_dword(priv, header->pointer, index,
                                      &descriptor);
        if (ret != 0) {
            return ret;
        }
        if ((descriptor & SPI_NOR_SMPT_DESC_MAP) == 0U) {
            return -MR_EIO;
        }
        region_count
            = ((descriptor >> SPI_NOR_SMPT_MAP_REGIONS_SHIFT) & 0xffU) + 1U;
        if ((index + region_count) >= header->dwords) {
            return -MR_EIO;
        }

        /* Intersect erase support across all regions in the selected map */
        if (((descriptor >> SPI_NOR_SMPT_MAP_ID_SHIFT) & 0xffU) == map_id) {
            common_mask = SPI_NOR_SMPT_REGION_ERASE_MASK;
            total_size = 0U;
            region_offset = 0U;
            for (i = 0U; i < region_count; i++) {
                ret = spi_nor_read_sfdp_dword(priv, header->pointer,
                                              index + i + 1U, &region);
                if (ret != 0) {
                    return ret;
                }
                mask = (mr_uint8_t)(region & SPI_NOR_SMPT_REGION_ERASE_MASK);
                region_size
                    = (((mr_uint64_t)region >> SPI_NOR_SMPT_REGION_SIZE_SHIFT)
                       + 1ULL)
                   << 8;
                /* Retain erase types aligned with every region */
                for (type = 0U; type < SPI_NOR_ERASE_TYPES_MAX; type++) {
                    unit = geometry->erase_types[type].size;
                    if (((mask & (1U << type)) != 0U)
                        && ((unit == 0U) || ((region_offset % unit) != 0U)
                            || ((region_size % unit) != 0U))) {
                        mask &= (mr_uint8_t) ~(1U << type);
                    }
                }
                common_mask &= mask;
                total_size += region_size;
                region_offset += region_size;
            }
            if ((total_size != geometry->size) || (common_mask == 0U)) {
                return -MR_ENOSYS;
            }
            geometry->erase_mask &= common_mask;
            if (geometry->erase_mask == 0U) {
                return -MR_ENOSYS;
            }
            return 0;
        }

        /* Continue until the last map descriptor */
        index += region_count + 1U;
        if ((descriptor & SPI_NOR_SMPT_DESC_END) != 0U) {
            break;
        }
    }
    return -MR_ENOSYS;
}

static mr_err_t spi_nor_parse_4bait(drv_flash_spi_nor_priv_t *priv,
                                    const spi_nor_param_header_t *header,
                                    spi_nor_geometry_t *geometry) {
    mr_uint8_t table[SPI_NOR_4BAIT_DWORDS * sizeof(mr_uint32_t)];
    mr_uint32_t capabilities, erase_opcodes, i;
    mr_uint8_t supported, opcode;
    mr_err_t ret;

    /* Require dedicated read, program, and table payload support */
    if (!header->present || (header->dwords < SPI_NOR_4BAIT_DWORDS)) {
        return -MR_ENOSYS;
    }
    ret = spi_nor_read_sfdp(priv, header->pointer, table, sizeof(table));
    if (ret != 0) {
        return ret;
    }
    capabilities = spi_nor_le32(table);
    if ((capabilities & (SPI_NOR_4BAIT_READ | SPI_NOR_4BAIT_PROGRAM))
        != (SPI_NOR_4BAIT_READ | SPI_NOR_4BAIT_PROGRAM)) {
        return -MR_ENOSYS;
    }

    /* Replace erase opcodes with supported four-byte variants */
    erase_opcodes = spi_nor_le32(table + sizeof(mr_uint32_t));
    supported
        = (mr_uint8_t)((capabilities >> SPI_NOR_4BAIT_ERASE_SHIFT) & 0x0fU);
    for (i = 0U; i < SPI_NOR_ERASE_TYPES_MAX; i++) {
        if ((geometry->erase_mask & (1U << i)) == 0U) {
            continue;
        }
        opcode = (mr_uint8_t)(erase_opcodes >> (i * 8U));
        if (((supported & (1U << i)) == 0U) || (opcode == 0U)
            || (opcode == 0xffU)) {
            geometry->erase_mask &= (mr_uint8_t) ~(1U << i);
            continue;
        }
        geometry->erase_types[i].opcode = opcode;
    }
    if (geometry->erase_mask == 0U) {
        return -MR_ENOSYS;
    }

    /* Select dedicated four-byte read and program instructions */
    geometry->addr_size = 4U;
    geometry->read_opcode = SPI_NOR_CMD_READ_4B;
    geometry->program_opcode = SPI_NOR_CMD_PAGE_PROGRAM_4B;
    return 0;
}

static mr_err_t spi_nor_apply_part_4b(const spi_nor_part_t *part,
                                      spi_nor_geometry_t *geometry) {
    mr_uint32_t i;
    mr_uint8_t opcode;

    /* Require an explicit compatibility-table guarantee */
    if ((part == MR_NULL) || ((part->flags & SPI_NOR_PART_4B_OPCODES) == 0U)) {
        return -MR_ENOSYS;
    }

    /* Convert known erase instructions to four-byte forms */
    for (i = 0U; i < SPI_NOR_ERASE_TYPES_MAX; i++) {
        if ((geometry->erase_mask & (1U << i)) == 0U) {
            continue;
        }
        opcode = geometry->erase_types[i].opcode;
        if (opcode == SPI_NOR_CMD_ERASE_4K) {
            opcode = SPI_NOR_CMD_ERASE_4K_4B;
        } else if (opcode == SPI_NOR_CMD_ERASE_32K) {
            opcode = SPI_NOR_CMD_ERASE_32K_4B;
        } else if (opcode == SPI_NOR_CMD_ERASE_64K) {
            opcode = SPI_NOR_CMD_ERASE_64K_4B;
        } else {
            geometry->erase_mask &= (mr_uint8_t) ~(1U << i);
            continue;
        }
        geometry->erase_types[i].opcode = opcode;
    }
    if (geometry->erase_mask == 0U) {
        return -MR_ENOSYS;
    }

    /* Select dedicated four-byte read and program instructions */
    geometry->addr_size = 4U;
    geometry->read_opcode = SPI_NOR_CMD_READ_4B;
    geometry->program_opcode = SPI_NOR_CMD_PAGE_PROGRAM_4B;
    return 0;
}

static void spi_nor_add_part_erase(spi_nor_geometry_t *geometry,
                                   mr_uint8_t *index, mr_uint32_t size,
                                   mr_uint8_t opcode) {
    /* Append one compatibility-table erase type */
    spi_nor_set_erase_type(geometry, *index, size, opcode);
    (*index)++;
}

static mr_err_t spi_nor_apply_part(const spi_nor_part_t *part,
                                   spi_nor_geometry_t *geometry) {
    mr_uint8_t erase_4k, erase_32k, erase_64k, index;

    /* Initialize trusted compatibility-table geometry */
    mr_memset(geometry, 0, sizeof(*geometry));
    geometry->size = part->size;
    geometry->page_size = SPI_NOR_DEFAULT_PAGE_SIZE;
    geometry->addr_size = 3U;
    geometry->read_opcode = SPI_NOR_CMD_READ;
    geometry->program_opcode = SPI_NOR_CMD_PAGE_PROGRAM;
    erase_4k = SPI_NOR_CMD_ERASE_4K;
    erase_32k = SPI_NOR_CMD_ERASE_32K;
    erase_64k = SPI_NOR_CMD_ERASE_64K;
    if ((part->flags & SPI_NOR_PART_4B_OPCODES) != 0U) {
        geometry->addr_size = 4U;
        geometry->read_opcode = SPI_NOR_CMD_READ_4B;
        geometry->program_opcode = SPI_NOR_CMD_PAGE_PROGRAM_4B;
        erase_4k = SPI_NOR_CMD_ERASE_4K_4B;
        erase_32k = SPI_NOR_CMD_ERASE_32K_4B;
        erase_64k = SPI_NOR_CMD_ERASE_64K_4B;
    }

    /* Add only erase types explicitly guaranteed by the part entry */
    index = 0U;
    if ((part->flags & SPI_NOR_PART_ERASE_4K) != 0U) {
        spi_nor_add_part_erase(geometry, &index, 4U * 1024U, erase_4k);
    }
    if ((part->flags & SPI_NOR_PART_ERASE_32K) != 0U) {
        spi_nor_add_part_erase(geometry, &index, 32U * 1024U, erase_32k);
    }
    if ((part->flags & SPI_NOR_PART_ERASE_64K) != 0U) {
        spi_nor_add_part_erase(geometry, &index, 64U * 1024U, erase_64k);
    }
    if ((part->flags & SPI_NOR_PART_ERASE_256K) != 0U) {
        /* M25P128 uses 0xD8 for its 256 KiB sector erase */
        spi_nor_add_part_erase(geometry, &index, 256U * 1024U,
                               SPI_NOR_CMD_ERASE_256K);
    }
    if (geometry->erase_mask == 0U) {
        return -MR_ENOSYS;
    }
    return 0;
}

static void spi_nor_set_status_type(drv_flash_spi_nor_priv_t *priv,
                                    const spi_nor_part_t *part) {
    /* Select only status extensions guaranteed by an exact part match */
    priv->status_type = SPI_NOR_STATUS_BASIC;
    if ((part != MR_NULL) && ((part->flags & SPI_NOR_PART_MICRON_FSR) != 0U)) {
        priv->status_type = SPI_NOR_STATUS_MICRON;
    } else if ((part != MR_NULL)
               && ((part->flags & SPI_NOR_PART_CYPRESS_SR) != 0U)) {
        priv->status_type = SPI_NOR_STATUS_CYPRESS;
    }
}

static mr_err_t spi_nor_finalize_geometry(spi_nor_geometry_t *geometry) {
    spi_nor_erase_type_t *type;
    mr_uint32_t erase_size, i;

    /* Validate addressable capacity and page geometry */
    if ((geometry->size == 0U) || (geometry->size > SPI_NOR_4B_ADDRESS_LIMIT)
        || (geometry->page_size == 0U)
        || ((geometry->page_size & (geometry->page_size - 1U)) != 0U)) {
        return -MR_EIO;
    }
    if (((geometry->addr_size == 3U)
         && (geometry->size > SPI_NOR_3B_ADDRESS_LIMIT))
        || ((geometry->addr_size != 3U) && (geometry->addr_size != 4U))) {
        return -MR_ENOSYS;
    }

    /* Find the minimum usable erase unit */
    erase_size = 0U;
    for (i = 0U; i < SPI_NOR_ERASE_TYPES_MAX; i++) {
        if ((geometry->erase_mask & (1U << i)) == 0U) {
            continue;
        }
        type = &geometry->erase_types[i];
        if ((type->size == 0U) || (type->opcode == 0U)
            || (type->opcode == 0xffU)
            || ((type->size & (type->size - 1U)) != 0U)
            || ((geometry->size % type->size) != 0U)) {
            return -MR_EIO;
        }
        if ((erase_size == 0U) || (type->size < erase_size)) {
            erase_size = type->size;
        }
    }
    if ((erase_size == 0U) || (geometry->page_size > erase_size)) {
        return -MR_ENOSYS;
    }
    geometry->erase_size = erase_size;
    return 0;
}

static mr_err_t spi_nor_check_data(drv_flash_spi_nor_priv_t *priv,
                                   mr_uint32_t addr, const mr_uint8_t *expected,
                                   mr_size_t size, spi_nor_check_t check) {
    mr_uint8_t data[SPI_NOR_VERIFY_BUFFER_SIZE];
    mr_size_t chunk_size, i;
    mr_err_t ret;

    /* Read and check data in chunks */
    while (size != 0U) {
        chunk_size = MR_MIN(size, sizeof(data));
        ret = spi_nor_addr_cmd(priv, priv->geometry.read_opcode, addr, MR_NULL,
                               data, chunk_size);
        if (ret != 0) {
            return ret;
        }

        /* Check requested data condition */
        switch (check) {
            case SPI_NOR_CHECK_MATCH: {
                if (mr_memcmp(data, expected, chunk_size) != 0) {
                    return -MR_EIO;
                }
                break;
            }
            case SPI_NOR_CHECK_PROGRAMMABLE: {
                for (i = 0U; i < chunk_size; i++) {
                    if ((data[i] & expected[i]) != expected[i]) {
                        return -MR_EIO;
                    }
                }
                break;
            }
            case SPI_NOR_CHECK_ERASED: {
                for (i = 0U; i < chunk_size; i++) {
                    if (data[i] != 0xffU) {
                        return -MR_EIO;
                    }
                }
                break;
            }
            default: {
                return -MR_EINVAL;
            }
        }

        /* Advance to next chunk */
        if (expected != MR_NULL) {
            expected += chunk_size;
        }
        addr += (mr_uint32_t)chunk_size;
        size -= chunk_size;
    }
    return 0;
}

static const spi_nor_erase_type_t *spi_nor_select_erase_type(
    const spi_nor_geometry_t *geometry, mr_uint64_t offset, mr_uint64_t size) {
    const spi_nor_erase_type_t *selected, *type;
    mr_uint32_t i;

    /* Select the largest aligned erase type that fits the remainder */
    selected = MR_NULL;
    for (i = 0U; i < SPI_NOR_ERASE_TYPES_MAX; i++) {
        if ((geometry->erase_mask & (1U << i)) == 0U) {
            continue;
        }
        type = &geometry->erase_types[i];
        if (((offset % type->size) == 0U) && (size >= type->size)
            && ((selected == MR_NULL) || (type->size > selected->size))) {
            selected = type;
        }
    }
    return selected;
}

static mr_err_t drv_probe(const mr_driver_t *drv, mr_flash_info_t *info) {
    mr_uint8_t bfpt[SPI_NOR_BFPT_DWORDS_USED * sizeof(mr_uint32_t)];
    drv_flash_spi_nor_priv_t *priv = MR_DRIVER_PRIV(drv);
    spi_nor_geometry_t geometry, saved_geometry;
    mr_uint8_t id[SPI_NOR_JEDEC_ID_SIZE];
    const spi_nor_part_t *part;
    mr_bool_t sfdp_available;
    mr_uint32_t bfpt_dwords;
    spi_nor_tables_t tables;
    mr_err_t ret;

    /* Check SPI binding */
    if ((priv->spi.drv == MR_NULL) || (priv->config.clock_rate == 0U)) {
        return -MR_EINVAL;
    }

    /* Check SPI mode */
    if ((priv->config.mode != MR_SPI_MODE_0)
        && (priv->config.mode != MR_SPI_MODE_3)) {
        return -MR_EINVAL;
    }

    /* Check operation timeouts */
    if ((priv->program_timeout_ms == 0U) || (priv->erase_timeout_ms == 0U)) {
        return -MR_EINVAL;
    }

    /* Prepare the parent SPI controller and this chip select */
    ret = mr_driver_setup(priv->spi.drv);
    if (ret != 0) {
        return ret;
    }
    ret = mr_ht_spi_configure(&priv->spi, &priv->config);
    if (ret != 0) {
        return ret;
    }
    spi_nor_set_status_type(priv, MR_NULL);
    ret = spi_nor_wait_ready(priv, priv->erase_timeout_ms);
    if (ret != 0) {
        return ret;
    }

    /* Identify the device and select optional vendor status handling */
    ret = spi_nor_read_id(priv, id);
    if (ret != 0) {
        return ret;
    }
    part = spi_nor_find_part(id);
    spi_nor_set_status_type(priv, part);
    ret = spi_nor_wait_ready(priv, priv->erase_timeout_ms);
    if (ret != 0) {
        return ret;
    }

    /* Use an explicit override only for a known SFDP exception */
    if ((part != MR_NULL) && ((part->flags & SPI_NOR_PART_SKIP_SFDP) != 0U)) {
        ret = spi_nor_apply_part(part, &geometry);
    } else {
        ret = spi_nor_read_headers(priv, &tables, &sfdp_available);
        if (ret != 0) {
            return ret;
        }

        /* Fall back only when SFDP is absent and the JEDEC ID is known */
        if (!sfdp_available) {
            if (part == MR_NULL) {
                return -MR_ENOSYS;
            }
            ret = spi_nor_apply_part(part, &geometry);
        } else {
            ret = spi_nor_read_bfpt(priv, &tables.bfpt, bfpt, &bfpt_dwords);
            if (ret == 0) {
                ret = spi_nor_parse_bfpt(bfpt, bfpt_dwords, &geometry);
            }

            /* Cross-check SFDP capacity against an exact JEDEC match */
            if ((ret == 0) && (part != MR_NULL)
                && (geometry.size != part->size)) {
                ret = -MR_EIO;
            }
            if ((ret == 0) && tables.smpt.present) {
                ret = spi_nor_parse_smpt(priv, &tables.smpt, &geometry);
            }

            /* Require explicit 4-byte opcodes, including 4-byte-only parts */
            if ((ret == 0)
                && ((geometry.size > SPI_NOR_3B_ADDRESS_LIMIT)
                    || (geometry.addr_size == 4U))) {
                if (tables.bait.present) {
                    saved_geometry = geometry;
                    ret = spi_nor_parse_4bait(priv, &tables.bait, &geometry);
                    if (ret == -MR_ENOSYS) {
                        geometry = saved_geometry;
                        ret = spi_nor_apply_part_4b(part, &geometry);
                    }
                } else {
                    ret = spi_nor_apply_part_4b(part, &geometry);
                }
            }
        }
    }
    if (ret == 0) {
        ret = spi_nor_finalize_geometry(&geometry);
    }
    if (ret != 0) {
        return ret;
    }

    /* Publish geometry only after the complete probe succeeds */
    priv->geometry = geometry;
    info->size = geometry.size;
    info->program_size = 1U;
    info->page_size = geometry.page_size;
    info->regions_num = 1U;
    info->erased_value = 0xffU;
    return 0;
}

static mr_err_t drv_region(const mr_driver_t *drv, mr_uint32_t index,
                           mr_flash_region_t *region) {
    drv_flash_spi_nor_priv_t *priv = MR_DRIVER_PRIV(drv);

    /* Check region index */
    if (index != 0U) {
        return -MR_EINVAL;
    }

    /* Publish uniform region */
    region->offset = 0U;
    region->size = priv->geometry.size;
    region->erase_size = priv->geometry.erase_size;
    return 0;
}

static mr_ssize_t drv_read(const mr_driver_t *drv, mr_uint64_t offset,
                           mr_uint8_t *buf, mr_size_t size) {
    drv_flash_spi_nor_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_err_t ret;

    /* Check operation */
    if (size == 0U) {
        return 0;
    }

    /* Lock device */
    if (!mr_spinlock_try_lock(&priv->lock)) {
        return -MR_EBUSY;
    }

    /* Wait for a preceding device operation before reading */
    ret = spi_nor_wait_ready(priv, priv->erase_timeout_ms);
    if (ret == 0) {
        ret = spi_nor_addr_cmd(priv, priv->geometry.read_opcode,
                               (mr_uint32_t)offset, MR_NULL, buf, size);
    }

    /* Unlock device */
    mr_spinlock_unlock(&priv->lock);
    return (ret != 0) ? (mr_ssize_t)ret : (mr_ssize_t)size;
}

static mr_ssize_t drv_program(const mr_driver_t *drv, mr_uint64_t offset,
                              const mr_uint8_t *buf, mr_size_t size) {
    drv_flash_spi_nor_priv_t *priv = MR_DRIVER_PRIV(drv);
    mr_size_t chunk_size, done;
    mr_uint32_t addr;
    mr_err_t ret;

    /* Check operation */
    if (size == 0U) {
        return 0;
    }

    /* Lock device */
    if (!mr_spinlock_try_lock(&priv->lock)) {
        return -MR_EBUSY;
    }

    /* Reject impossible zero-to-one transitions before changing the device */
    ret = spi_nor_wait_ready(priv, priv->erase_timeout_ms);
    if (ret == 0) {
        ret = spi_nor_check_data(priv, (mr_uint32_t)offset, buf, size,
                                 SPI_NOR_CHECK_PROGRAMMABLE);
    }
    done = 0U;

    /* Split the request at every page-program boundary */
    while ((ret == 0) && (done < size)) {
        addr = (mr_uint32_t)(offset + done);
        chunk_size
            = priv->geometry.page_size - (addr % priv->geometry.page_size);
        chunk_size = MR_MIN(size - done, chunk_size);

        ret = spi_nor_write_enable(priv);
        if (ret == 0) {
            ret = spi_nor_addr_cmd(priv, priv->geometry.program_opcode, addr,
                                   buf + done, MR_NULL, chunk_size);
        }
        if (ret == 0) {
            ret = spi_nor_wait_ready(priv, priv->program_timeout_ms);
        }
        if (ret == 0) {
            ret = spi_nor_check_data(priv, addr, buf + done, chunk_size,
                                     SPI_NOR_CHECK_MATCH);
        }
        if (ret == 0) {
            done += chunk_size;
        }
    }

    /* Unlock device */
    mr_spinlock_unlock(&priv->lock);
    return (ret != 0) ? (mr_ssize_t)ret : (mr_ssize_t)done;
}

static mr_err_t drv_erase(const mr_driver_t *drv, mr_uint64_t offset,
                          mr_uint64_t size) {
    drv_flash_spi_nor_priv_t *priv = MR_DRIVER_PRIV(drv);
    const spi_nor_erase_type_t *erase_type;
    mr_uint32_t addr;
    mr_err_t ret;

    /* Check erase request */
    if (size == 0U) {
        return 0;
    }
    if (((offset % priv->geometry.erase_size) != 0U)
        || ((size % priv->geometry.erase_size) != 0U)) {
        return -MR_EINVAL;
    }

    /* Lock device */
    if (!mr_spinlock_try_lock(&priv->lock)) {
        return -MR_EBUSY;
    }

    /* Erase with the largest aligned supported type at each position */
    ret = spi_nor_wait_ready(priv, priv->erase_timeout_ms);
    while ((ret == 0) && (size != 0U)) {
        erase_type = spi_nor_select_erase_type(&priv->geometry, offset, size);
        if (erase_type == MR_NULL) {
            ret = -MR_EINVAL;
            break;
        }
        addr = (mr_uint32_t)offset;
        ret = spi_nor_write_enable(priv);
        if (ret == 0) {
            ret = spi_nor_addr_cmd(priv, erase_type->opcode, addr, MR_NULL,
                                   MR_NULL, 0U);
        }
        if (ret == 0) {
            ret = spi_nor_wait_ready(priv, priv->erase_timeout_ms);
        }
        if (ret == 0) {
            ret = spi_nor_check_data(priv, addr, MR_NULL, erase_type->size,
                                     SPI_NOR_CHECK_ERASED);
        }
        if (ret == 0) {
            offset += erase_type->size;
            size -= erase_type->size;
        }
    }

    /* Unlock device */
    mr_spinlock_unlock(&priv->lock);
    return ret;
}

/* Instance install */
#define FLASH_SPI_NOR_HT_FLASH_INSTALL(_id, _ops)                              \
    static drv_flash_spi_nor_priv_t MR_CAT(__drv_flash_spi_nor_priv_, _id) = { \
        .spi = MR_HT_SPI_SPEC(_id, spi),                                       \
        .config = {                                                            \
            .clock_rate = MR_HT_DRIVER_PROP_OR(                                \
                _id, clock_rate, SPI_NOR_DEFAULT_CLOCK_RATE),                  \
            .data_bits = 8U,                                                   \
            .mode = MR_HT_DRIVER_PROP_OR(_id, mode, MR_SPI_MODE_0),            \
            .bit_order = MR_SPI_BIT_ORDER_MSB,                                 \
            .role = MR_SPI_ROLE_MASTER,                                        \
        },                                                                     \
        .lock = MR_SPINLOCK_INIT(),                                            \
        .program_timeout_ms = MR_HT_DRIVER_PROP_OR(                            \
            _id, program_timeout_ms, SPI_NOR_PROGRAM_TIMEOUT_MS),              \
        .erase_timeout_ms = MR_HT_DRIVER_PROP_OR(                              \
            _id, erase_timeout_ms, SPI_NOR_ERASE_TIMEOUT_MS),                  \
    };                                                                         \
    MR_HT_FLASH_INSTALL(_id, &MR_CAT(__drv_flash_spi_nor_priv_, _id), _ops)
MR_HT_DRIVER_FOREACH(FLASH_SPI_NOR_HT_FLASH_INSTALL, &__drv_flash_spi_nor_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_FLASH) && defined(MR_USE_DRIVER_SPI) */

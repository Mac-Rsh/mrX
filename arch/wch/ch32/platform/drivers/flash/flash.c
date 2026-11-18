/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_FLASH)
#include <mrX/drivers/flash/mr_flash.h>
#include <mrX/port/mr_port_irq.h>
#include <mrX/libc/mr_string.h>

/* HardTree compatible */
#define MR_HT_COMPAT                    wch_ch32_flash
#if MR_HT_HAS_DRIVER()

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
MR_UNUSED static const mr_flash_ops_t __flash_ops = {
    .probe = flash_probe,
    .region = flash_region,
    .read = flash_read,
    .program = flash_program,
    .erase = flash_erase,
};

#if defined(MR_USE_CH32VM00X) || defined(MR_USE_CH32V205)
#define CH32_FLASH_USE_PAGE_BUFFER
#endif /* CH32 EVT SDK family */

/* Flash geometry definition */
#if defined(CH32_FLASH_USE_PAGE_BUFFER)
#define CH32_FLASH_ERASE_SIZE           (256U)
#define CH32_FLASH_PROGRAM_SIZE         (256U)
#else
#define CH32_FLASH_PROGRAM_SIZE         (2U)
#if defined(MR_USE_CH32V00X)
#include "ch32v00x_flash.h"
#define CH32_FLASH_ERASE_SIZE           (1024U)
#elif defined(MR_USE_CH32V10X)
#include "ch32v10x_flash.h"
#define CH32_FLASH_ERASE_SIZE           (1024U)
#elif defined(MR_USE_CH32V20X)
#include "ch32v20x_flash.h"
#define CH32_FLASH_ERASE_SIZE           (4096U)
#elif defined(MR_USE_CH32V4X7)
#include "ch32v4x7_flash.h"
#define CH32_FLASH_ERASE_SIZE           (4096U)
#else
#include "ch32v30x_flash.h"
#define CH32_FLASH_ERASE_SIZE           (4096U)
#endif /* CH32 EVT SDK family */
#endif /* defined(CH32_FLASH_USE_PAGE_BUFFER) */

#if defined(MR_USE_CH32V4X7)
/* Flash access clock definition */
#define CH32_FLASH_CLOCK_SHIFT          (25U)
#define CH32_FLASH_CLOCK_MAX            (100000000U)
#endif /* defined(MR_USE_CH32V4X7) */

#if defined(MR_USE_CH32V20X) || defined(MR_USE_CH32V30X)
/* Flash enhanced read status definition */
#define CH32_FLASH_ENHANCED_READ        (1U << 7)
#endif /* CH32V20x and CH32V30x Flash */

#if defined(CH32_FLASH_USE_PAGE_BUFFER)
/* Flash control register definition */
#define CH32_FLASH_FAST_LOCK            (1U << 15)
#define CH32_FLASH_LOCK                 (1U << 7)
#define CH32_FLASH_START                (1U << 6)
#define CH32_FLASH_PAGE_PROGRAM         (1U << 16)
#define CH32_FLASH_PAGE_ERASE           (1U << 17)
#define CH32_FLASH_BUFFER_LOAD          (1U << 18)
#define CH32_FLASH_BUFFER_RESET         (1U << 19)

/* Flash status register definition */
#define CH32_FLASH_BUSY                 (1U << 0)
#define CH32_FLASH_WRITE_PROTECT        (1U << 4)
#define CH32_FLASH_END                  (1U << 5)
#define CH32_FLASH_WAKE_FLAG            (1U << 6)
#if defined(MR_USE_CH32VM00X)
#define CH32_FLASH_BOOT_MODE            (1U << 14)
#endif /* defined(MR_USE_CH32VM00X) */

/* Flash key definition */
#define CH32_FLASH_KEY1                 (0x45670123U)
#define CH32_FLASH_KEY2                 (0xcdef89abU)

/* Flash poll budget definition */
#define CH32_FLASH_POLL_COUNT           (0x100000U)
#endif /* defined(CH32_FLASH_USE_PAGE_BUFFER) */

/* Flash window type */
typedef struct flash_window {
    mr_uint32_t offset, size, limit;
} flash_window_t;

static mr_err_t flash_probe(const mr_driver_t *drv, mr_flash_info_t *info) {
    const flash_window_t *priv = MR_DRIVER_PRIV(drv);

    /* Check FLASH window */
    if ((priv->limit == 0U) || (priv->size == 0U)
        || (priv->offset >= priv->limit)
        || (priv->size > priv->limit - priv->offset)
        || ((priv->offset % CH32_FLASH_ERASE_SIZE) != 0U)
        || ((priv->size % CH32_FLASH_ERASE_SIZE) != 0U)) {
        return -MR_EINVAL;
    }

    /* Get FLASH geometry */
    info->size = priv->size;
    info->program_size = CH32_FLASH_PROGRAM_SIZE;
#if defined(CH32_FLASH_USE_PAGE_BUFFER)
    info->page_size = CH32_FLASH_PROGRAM_SIZE;
#else
    info->page_size = 0U;
#endif /* defined(CH32_FLASH_USE_PAGE_BUFFER) */
    info->regions_num = 1U;
#if defined(MR_USE_CH32V20X) || defined(MR_USE_CH32V30X)                       \
    || defined(MR_USE_CH32V4X7)
    /* Erased bytes read as 0x39 at even addresses and 0xe3 at odd addresses */
    info->erased_value[0] = 0x39U;
    info->erased_value[1] = 0xe3U;
    info->erased_value[2] = 0x39U;
    info->erased_value[3] = 0xe3U;
#else
    mr_memset(info->erased_value, 0xff, sizeof(info->erased_value));
#endif /* CH32V20x, CH32V30x or CH32V4x7 */
    return 0;
}

static mr_err_t flash_region(const mr_driver_t *drv, mr_uint32_t index,
                             mr_flash_region_t *region) {
    const flash_window_t *priv = MR_DRIVER_PRIV(drv);

    MR_IGNORE(index);

    /* Get the uniform erase region */
    region->offset = 0U;
    region->size = priv->size;
    region->erase_size = CH32_FLASH_ERASE_SIZE;
    return 0;
}

static mr_err_t flash_check_clock(void) {
#if defined(MR_USE_CH32V4X7)
    mr_uint32_t clock_mode, divider;

    /* SCKMOD selects HCLK divided by 1, 2 or 4 */
    clock_mode = FLASH->CTLR & FLASH_CTLR_SCKMOD;
    divider = 1U << (clock_mode >> CH32_FLASH_CLOCK_SHIFT);
    if ((clock_mode > FLASH_Access_SYSTEM_FOURTH)
        || (SystemCoreClock > CH32_FLASH_CLOCK_MAX * divider)) {
        return -MR_ENOSYS;
    }
#elif defined(MR_USE_CH32V20X) || defined(MR_USE_CH32V30X)
    /* EVT specifies a 60 MHz limit for accesses to the flash array */
    if (SystemCoreClock > (((FLASH->CTLR & (1U << 25)) != 0U)
                              ? 60000000U : 120000000U)) {
        return -MR_ENOSYS;
    }
#endif /* CH32 EVT SDK family */
    return 0;
}

static mr_ssize_t flash_read(const mr_driver_t *drv, mr_uint64_t offset,
                             mr_uint8_t *buf, mr_size_t size) {
    const flash_window_t *priv = MR_DRIVER_PRIV(drv);
    mr_uintptr_t address;
    mr_err_t ret;

    /* Check parameter */
    if (size == 0U) {
        return 0;
    }

    /* Check FLASH clock */
    ret = flash_check_clock();
    if (ret != 0) {
        return ret;
    }

    /* Read the memory-mapped FLASH */
    address = FLASH_BASE + priv->offset + (mr_uintptr_t)offset;
    mr_memcpy(buf, (const void *)address, size);
    return (mr_ssize_t)size;
}

#if defined(CH32_FLASH_USE_PAGE_BUFFER)
static void flash_clear_status(mr_uint32_t flags) {
#if defined(MR_USE_CH32VM00X)
    /* Preserve BOOT_MODE while the boot lock is open */
    flags |= FLASH->STATR & CH32_FLASH_BOOT_MODE;
#endif /* defined(MR_USE_CH32VM00X) */
    /* Write one to leave the RW0 wake flag intact */
    FLASH->STATR = flags | CH32_FLASH_WAKE_FLAG;
}

static mr_err_t flash_wait(void) {
    mr_uint32_t remaining, status;

    /* Wait flash controller idle */
    for (remaining = CH32_FLASH_POLL_COUNT; remaining != 0U; remaining--) {
        status = FLASH->STATR;
        if ((status & CH32_FLASH_BUSY) == 0U) {
            if ((status & CH32_FLASH_END) != 0U) {
                flash_clear_status(CH32_FLASH_END);
            }
            return ((status & CH32_FLASH_WRITE_PROTECT) != 0U) ? -MR_EPERM : 0;
        }
    }
    return -MR_ETIMEDOUT;
}

static mr_err_t flash_begin(void) {
    /* Check controller state */
    if ((FLASH->STATR & CH32_FLASH_BUSY) != 0U) {
        return -MR_EBUSY;
    }

    /* Unlock FLASH */
    flash_clear_status(CH32_FLASH_WRITE_PROTECT | CH32_FLASH_END);
    FLASH->KEYR = CH32_FLASH_KEY1;
    FLASH->KEYR = CH32_FLASH_KEY2;
    FLASH->MODEKEYR = CH32_FLASH_KEY1;
    FLASH->MODEKEYR = CH32_FLASH_KEY2;
    if ((FLASH->CTLR & (CH32_FLASH_LOCK | CH32_FLASH_FAST_LOCK)) != 0U) {
        FLASH->CTLR |= CH32_FLASH_LOCK | CH32_FLASH_FAST_LOCK;
        return -MR_EPERM;
    }
    return 0;
}

static void flash_end(void) {
    /* Lock FLASH */
    FLASH->CTLR &= ~(CH32_FLASH_PAGE_PROGRAM | CH32_FLASH_PAGE_ERASE);
    FLASH->CTLR |= CH32_FLASH_FAST_LOCK | CH32_FLASH_LOCK;
}

static mr_err_t flash_program_unit(mr_uintptr_t address,
                                   const mr_uint8_t *buf) {
    mr_uint32_t value;
    mr_err_t ret;
    mr_size_t i;

    /* The fast controller stages a full page in its internal word buffer */
    FLASH->CTLR |= CH32_FLASH_PAGE_PROGRAM | CH32_FLASH_BUFFER_RESET;
    ret = flash_wait();
    for (i = 0U; (i < CH32_FLASH_PROGRAM_SIZE) && (ret == 0); i += 4U) {
        /* Load buffer words */
        value = (mr_uint32_t)buf[i] | ((mr_uint32_t)buf[i + 1U] << 8)
                | ((mr_uint32_t)buf[i + 2U] << 16)
                | ((mr_uint32_t)buf[i + 3U] << 24);
        *(volatile uint32_t *)(address + i) = value;
        FLASH->CTLR |= CH32_FLASH_BUFFER_LOAD;
        ret = flash_wait();
    }
    if (ret == 0) {
        /* Program the staged page */
        FLASH->ADDR = address;
        FLASH->CTLR |= CH32_FLASH_START;
        ret = flash_wait();
    }
    FLASH->CTLR &= ~CH32_FLASH_PAGE_PROGRAM;
    return ret;
}

static mr_err_t flash_erase_unit(mr_uintptr_t address) {
    mr_err_t ret;

    /* Erase one page */
    FLASH->CTLR |= CH32_FLASH_PAGE_ERASE;
    FLASH->ADDR = address;
    FLASH->CTLR |= CH32_FLASH_START;
    ret = flash_wait();
    FLASH->CTLR &= ~CH32_FLASH_PAGE_ERASE;
    return ret;
}
#else
static mr_err_t flash_status(FLASH_Status status) {
    /* Translate flash status */
    switch (status) {
        case FLASH_COMPLETE: {
            return 0;
        }
        case FLASH_TIMEOUT: {
            return -MR_ETIMEDOUT;
        }
        case FLASH_ERROR_WRP: {
            return -MR_EPERM;
        }
        default: {
            return -MR_EIO;
        }
    }
}

static mr_err_t flash_begin(void) {
    /* Check controller state */
    if ((FLASH->STATR & FLASH_FLAG_BSY) != 0U) {
        return -MR_EBUSY;
    }
#if defined(MR_USE_CH32V20X) || defined(MR_USE_CH32V30X)
    /* Program and erase require enhanced read mode to be inactive */
    if ((FLASH->STATR & CH32_FLASH_ENHANCED_READ) != 0U) {
        return -MR_EBUSY;
    }
#endif /* CH32V20x and CH32V30x Flash */

    /* Unlock FLASH */
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR);
#if defined(FLASH_FLAG_PGERR)
    FLASH_ClearFlag(FLASH_FLAG_PGERR);
#endif /* defined(FLASH_FLAG_PGERR) */
    FLASH_Unlock();
    if ((FLASH->CTLR & (1U << 7)) != 0U) {
        return -MR_EPERM;
    }
    return 0;
}

static void flash_end(void) {
    /* Lock FLASH */
    FLASH_Lock();
}

static mr_err_t flash_program_unit(mr_uintptr_t address,
                                   const mr_uint8_t *buf) {
    mr_uint16_t value;

    /* Program one halfword */
    value = (mr_uint16_t)buf[0] | ((mr_uint16_t)buf[1] << 8);
    return flash_status(FLASH_ProgramHalfWord(address, value));
}

static mr_err_t flash_erase_unit(mr_uintptr_t address) {
    /* Erase one page */
    return flash_status(FLASH_ErasePage(address));
}
#endif /* defined(CH32_FLASH_USE_PAGE_BUFFER) */

static mr_ssize_t flash_program(const mr_driver_t *drv, mr_uint64_t offset,
                                const mr_uint8_t *buf, mr_size_t size) {
    const flash_window_t *priv = MR_DRIVER_PRIV(drv);
    mr_uintptr_t address;
    mr_err_t ret;
    mr_size_t i;
    int mask;

    /* Check parameter */
    if (size == 0U) {
        return 0;
    }
    if (((offset % CH32_FLASH_PROGRAM_SIZE) != 0U)
        || ((size % CH32_FLASH_PROGRAM_SIZE) != 0U)) {
        return -MR_EINVAL;
    }

    /* Check FLASH clock */
    ret = flash_check_clock();
    if (ret != 0) {
        return ret;
    }

    /* The array stalls instruction fetches during program and erase */
    mask = mr_port_irq_disable();

    /* Unlock FLASH */
    ret = flash_begin();
    if (ret == 0) {
        /* Program units */
        address = FLASH_BASE + priv->offset + (mr_uintptr_t)offset;
        for (i = 0U; (i < size) && (ret == 0); i += CH32_FLASH_PROGRAM_SIZE) {
            ret = flash_program_unit(address + i, buf + i);
        }

        /* Lock FLASH */
        flash_end();
        __asm__ volatile ("fence.i" ::: "memory");
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return (ret == 0) ? (mr_ssize_t)size : ret;
}

static mr_err_t flash_erase(const mr_driver_t *drv, mr_uint64_t offset,
                            mr_uint64_t size) {
    const flash_window_t *priv = MR_DRIVER_PRIV(drv);
    mr_uintptr_t address;
    mr_err_t ret;
    mr_size_t i;
    int mask;

    /* Check parameter */
    if (size == 0U) {
        return 0;
    }
    if (((offset % CH32_FLASH_ERASE_SIZE) != 0U)
        || ((size % CH32_FLASH_ERASE_SIZE) != 0U)) {
        return -MR_EINVAL;
    }

    /* Check FLASH clock */
    ret = flash_check_clock();
    if (ret != 0) {
        return ret;
    }

    /* The array stalls instruction fetches during program and erase */
    mask = mr_port_irq_disable();

    /* Unlock FLASH */
    ret = flash_begin();
    if (ret == 0) {
        /* Erase units */
        address = FLASH_BASE + priv->offset + (mr_uintptr_t)offset;
        for (i = 0U; (i < size) && (ret == 0); i += CH32_FLASH_ERASE_SIZE) {
            ret = flash_erase_unit(address + i);
        }

        /* Lock FLASH */
        flash_end();
        __asm__ volatile ("fence.i" ::: "memory");
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}

/* Instance install */
#define CH32_HT_FLASH_INSTALL(_id, _ops)                                       \
    static const flash_window_t MR_CAT(__flash_window_, _id) = {               \
        .offset = MR_HT_DRIVER_PROP(_id, wch_flash_offset),                    \
        .size = MR_HT_DRIVER_PROP(_id, wch_flash_size),                        \
        .limit = MR_HT_DRIVER_PROP(_id, wch_flash_limit),                      \
    };                                                                         \
    MR_HT_FLASH_INSTALL(_id, &MR_CAT(__flash_window_, _id), _ops)
MR_HT_DRIVER_FOREACH(CH32_HT_FLASH_INSTALL, &__flash_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_FLASH) */

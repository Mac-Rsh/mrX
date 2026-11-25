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
#include <mrX/libc/mr_string.h>
#if defined(MR_USE_DCACHE) || defined(MR_USE_ICACHE)
#include <mrX/port/mr_port_cache.h>
#define FLASH_USE_CACHE
#endif /* defined(MR_USE_DCACHE) || defined(MR_USE_ICACHE) */

/* HardTree compatible */
#define MR_HT_COMPAT                    st_stm32_flash
#if MR_HT_HAS_DRIVER()
#if !defined(FLASH_TYPEPROGRAM_QUADWORD) || !defined(FLASH_TYPEERASE_SECTORS)
#error "STM32 FLASH backend currently supports quadword sector FLASH only"
#endif /* !defined(FLASH_TYPEPROGRAM_QUADWORD)                                 \
        || !defined(FLASH_TYPEERASE_SECTORS) */

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

/* Flash erase and program geometry definition */
#if defined(FLASH_SECTOR_SIZE)
#define STM32_FLASH_SECTOR_SIZE         FLASH_SECTOR_SIZE
#else
#define STM32_FLASH_SECTOR_SIZE         (8U * 1024U)
#endif /* defined(FLASH_SECTOR_SIZE) */
#define STM32_FLASH_WORD_SIZE           (16U)
#if defined(STM32H503xx) || defined(STM32H523xx) || defined(STM32H533xx)
#define STM32_FLASH_BANK_NUM            (1U)
#else
#if defined(FLASH_BANK_2)
#define STM32_FLASH_BANK_NUM            (2U)
#else
#define STM32_FLASH_BANK_NUM            (1U)
#endif /* defined(FLASH_BANK_2) */
#endif /* defined(STM32H503xx) || defined(STM32H523xx)                         \
        || defined(STM32H533xx) */

#if defined(FLASH_USE_CACHE)
static void flash_cache_drop(mr_uint32_t addr, mr_uint32_t size) {
    /* Invalidate cached flash contents */
#if defined(MR_USE_DCACHE)
    mr_port_dcache_invalidate((const void *)addr, size);
#endif /* defined(MR_USE_DCACHE) */
#if defined(MR_USE_ICACHE)
    mr_port_icache_invalidate((const void *)addr, size);
#endif /* defined(MR_USE_ICACHE) */
}
#endif /* defined(FLASH_USE_CACHE) */

static void flash_locate(mr_uint64_t offset, uint32_t *banks,
                         uint32_t *sector) {
    mr_uint64_t bank_size;

    /* Convert the offset to bank-local sector coordinates */
    bank_size = (mr_uint64_t)FLASH_SIZE / STM32_FLASH_BANK_NUM;
#if (STM32_FLASH_BANK_NUM > 1U)
    if (offset >= bank_size) {
        *banks = FLASH_BANK_2;
        *sector = (uint32_t)((offset - bank_size) / STM32_FLASH_SECTOR_SIZE);
        return;
    }
#endif /* STM32_FLASH_BANK_NUM > 1U */
    *banks = FLASH_BANK_1;
    *sector = (uint32_t)(offset / STM32_FLASH_SECTOR_SIZE);
}

static mr_err_t flash_probe(const mr_driver_t *drv, mr_flash_info_t *info) {
    MR_IGNORE(drv);

    /* Get FLASH geometry */
    info->size = FLASH_SIZE;
    info->program_size = STM32_FLASH_WORD_SIZE;
    info->page_size = 0U;
    info->regions_num = 1U;
    mr_memset(info->erased_value, 0xff, sizeof(info->erased_value));
    return 0;
}

static mr_err_t flash_region(const mr_driver_t *drv, mr_uint32_t index,
                             mr_flash_region_t *region) {
    MR_IGNORE(drv);

    /* Check parameter */
    if (index != 0U) {
        return -MR_EINVAL;
    }

    /* Get the uniform erase region */
    region->offset = 0U;
    region->size = FLASH_SIZE;
    region->erase_size = STM32_FLASH_SECTOR_SIZE;
    return 0;
}

static mr_ssize_t flash_read(const mr_driver_t *drv, mr_uint64_t offset,
                             mr_uint8_t *buf, mr_size_t size) {
    MR_IGNORE(drv);

    /* Read the memory-mapped FLASH */
    mr_memcpy(buf, (const void *)(FLASH_BASE + (mr_uint32_t)offset), size);
    return (mr_ssize_t)size;
}

static mr_err_t flash_program_word(mr_uint32_t addr, const uint32_t *data) {
    /* Program one 128-bit quadword */
    return mr_stm32_hal_status(
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, addr, (uint32_t)data));
}

static mr_err_t flash_program_merge(mr_uint32_t addr, mr_uint32_t skip,
                                    const mr_uint8_t *buf, mr_uint32_t size) {
    uint32_t word[STM32_FLASH_WORD_SIZE / sizeof(uint32_t)];
    const mr_uint8_t *bytes;
    mr_uint32_t i;

    /* HAL writes the whole quadword, so untouched bytes must be erased */
    mr_memcpy(word, (const void *)addr, sizeof(word));
    bytes = (const mr_uint8_t *)word;
    for (i = 0U; i < STM32_FLASH_WORD_SIZE; i++) {
        if (((i < skip) || (i >= (skip + size))) && (bytes[i] != 0xFFU)) {
            return -MR_EIO;
        }
    }

    /* Merge and program the quadword */
    mr_memcpy((mr_uint8_t *)word + skip, buf, size);
    return flash_program_word(addr, word);
}

static mr_ssize_t flash_program(const mr_driver_t *drv, mr_uint64_t offset,
                                const mr_uint8_t *buf, mr_size_t size) {
    uint32_t word[STM32_FLASH_WORD_SIZE / sizeof(uint32_t)];
    mr_uint32_t addr, done, chunk, skip;
#if defined(FLASH_USE_CACHE)
    mr_uint32_t base;
#endif /* defined(FLASH_USE_CACHE) */
    mr_err_t ret;

    MR_IGNORE(drv);

    /* Check parameter */
    if (size == 0U) {
        return 0;
    }
    addr = FLASH_BASE + (mr_uint32_t)offset;
#if defined(FLASH_USE_CACHE)
    base = addr;
#endif /* defined(FLASH_USE_CACHE) */
    done = 0U;

    /* Unlock FLASH */
    ret = mr_stm32_hal_status(HAL_FLASH_Unlock());
    if (ret != 0) {
        return ret;
    }

    /* Program the partial head quadword */
    if ((addr & (STM32_FLASH_WORD_SIZE - 1U)) != 0U) {
        skip = addr & (STM32_FLASH_WORD_SIZE - 1U);
        chunk = STM32_FLASH_WORD_SIZE - skip;

        if (chunk > (mr_uint32_t)size) {
            chunk = (mr_uint32_t)size;
        }
        addr -= skip;
        ret = flash_program_merge(addr, skip, buf, chunk);
        if (ret != 0) {
            goto _lock;
        }
        addr += STM32_FLASH_WORD_SIZE;
        buf += chunk;
        done += chunk;
    }

    /* Stage quadwords to align a potentially unaligned source */
    while (((mr_uint32_t)size - done) >= STM32_FLASH_WORD_SIZE) {
        mr_memcpy(word, buf, sizeof(word));
        ret = flash_program_word(addr, word);
        if (ret != 0) {
            goto _lock;
        }
        addr += STM32_FLASH_WORD_SIZE;
        buf += STM32_FLASH_WORD_SIZE;
        done += STM32_FLASH_WORD_SIZE;
    }

    /* Program the partial tail quadword */
    if (done != (mr_uint32_t)size) {
        ret = flash_program_merge(addr, 0U, buf, (mr_uint32_t)size - done);
        if (ret != 0) {
            goto _lock;
        }
        done = (mr_uint32_t)size;
    }
    ret = 0;
_lock:
    /* Lock FLASH, preserving the first programming error */
    if (ret != 0) {
        HAL_FLASH_Lock();
#if defined(FLASH_USE_CACHE)
        if (done != 0U) {
            flash_cache_drop(base, done);
        }
#endif /* defined(FLASH_USE_CACHE) */
        return (mr_ssize_t)ret;
    }
    ret = mr_stm32_hal_status(HAL_FLASH_Lock());
    if (ret != 0) {
#if defined(FLASH_USE_CACHE)
        if (done != 0U) {
            flash_cache_drop(base, done);
        }
#endif /* defined(FLASH_USE_CACHE) */
        return ret;
    }

#if defined(FLASH_USE_CACHE)
    /* Invalidate cached FLASH contents */
    flash_cache_drop(base, done);
#endif /* defined(FLASH_USE_CACHE) */
    return (mr_ssize_t)done;
}

static mr_err_t flash_erase(const mr_driver_t *drv, mr_uint64_t offset,
                            mr_uint64_t size) {
    uint32_t banks, sector, sector_error;
#if defined(FLASH_USE_CACHE)
    mr_uint32_t base, erased;
#endif /* defined(FLASH_USE_CACHE) */
    FLASH_EraseInitTypeDef init = {0};
    mr_err_t ret;

    MR_IGNORE(drv);

    /* Check parameter */
    if ((offset > (mr_uint64_t)MR_UINT32_MAX)
        || (size > (mr_uint64_t)MR_UINT32_MAX)
        || ((offset % STM32_FLASH_SECTOR_SIZE) != 0U)
        || ((size % STM32_FLASH_SECTOR_SIZE) != 0U)) {
        return -MR_EINVAL;
    }
    if (size == 0U) {
        return 0;
    }

#if defined(FLASH_USE_CACHE)
    base = FLASH_BASE + (mr_uint32_t)offset;
    erased = 0U;
#endif /* defined(FLASH_USE_CACHE) */

    /* Unlock FLASH */
    ret = mr_stm32_hal_status(HAL_FLASH_Unlock());
    if (ret != 0) {
        return ret;
    }

    /* Resolve each sector separately to support cross-bank erases */
    while (size != 0U) {
        flash_locate(offset, &banks, &sector);
        init.TypeErase = FLASH_TYPEERASE_SECTORS;
        init.Banks = banks;
        init.Sector = sector;
        init.NbSectors = 1U;
        sector_error = 0xFFFFFFFFU;
        ret = mr_stm32_hal_status(HAL_FLASHEx_Erase(&init, &sector_error));
        if (ret != 0) {
            break;
        }
        offset += STM32_FLASH_SECTOR_SIZE;
        size -= STM32_FLASH_SECTOR_SIZE;
#if defined(FLASH_USE_CACHE)
        erased += STM32_FLASH_SECTOR_SIZE;
#endif /* defined(FLASH_USE_CACHE) */
    }

    /* Lock FLASH, preserving the first erase error */
    if (HAL_FLASH_Lock() != HAL_OK) {
        ret = (ret != 0) ? ret : -MR_EIO;
    }

#if defined(FLASH_USE_CACHE)
    /* Invalidate cached FLASH contents for all erased sectors */
    if (erased != 0U) {
        flash_cache_drop(base, erased);
    }
#endif /* defined(FLASH_USE_CACHE) */
    return ret;
}

/* Instance install */
#define ST_HT_FLASH_INSTALL(_id, _ops)  MR_HT_FLASH_INSTALL(_id, MR_NULL, _ops)
MR_HT_DRIVER_FOREACH(ST_HT_FLASH_INSTALL, &__flash_ops);
#endif /* MR_HT_HAS_DRIVER() */
#endif /* defined(MR_USE_DRIVER_FLASH) */

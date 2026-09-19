/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-18        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#include <mrX/port/mr_port_cache.h>

#define STM32H5_CACHE_LINE_SIZE         (16U)

#if defined(MR_USE_DCACHE)
#include "stm32h5xx_hal_dcache.h"

static DCACHE_HandleTypeDef __dcache = {
    .Instance = DCACHE1,
};

static mr_err_t dcache_status(HAL_StatusTypeDef status) {
    mr_err_t ret = mr_stm32h5_hal_status(status);

    if (ret == 0) {
        __DSB();
    }
    return ret;
}

static mr_err_t dcache_range(const void *addr, mr_size_t size,
                             const uint32_t **aligned_addr,
                             uint32_t *aligned_size) {
    const mr_uintptr_t mask = STM32H5_CACHE_LINE_SIZE - 1U;
    const mr_uintptr_t max = ~(mr_uintptr_t)0U;
    mr_uintptr_t start = (mr_uintptr_t)addr;
    mr_uintptr_t end;

    if ((mr_uint64_t)size > (mr_uint64_t)(max - start)) {
        return -MR_ERANGE;
    }
    end = start + (mr_uintptr_t)size;
    if (end > (max - mask)) {
        return -MR_ERANGE;
    }
    start &= ~mask;
    end = (end + mask) & ~mask;
    if ((end - start) > MR_UINT32_MAX) {
        return -MR_ERANGE;
    }
    *aligned_addr = (const uint32_t *)start;
    *aligned_size = (uint32_t)(end - start);
    return 0;
}

mr_err_t mr_port_dcache_enable(void) {
    return dcache_status(HAL_DCACHE_Enable(&__dcache));
}

mr_err_t mr_port_dcache_disable(void) {
    mr_err_t ret;

    /* Preserve dirty data before disabling the cache */
    ret = mr_port_dcache_clean_all();
    if (ret != 0) {
        return ret;
    }
    return dcache_status(HAL_DCACHE_Disable(&__dcache));
}

mr_size_t mr_port_dcache_line_size(void) {
    return STM32H5_CACHE_LINE_SIZE;
}

mr_err_t mr_port_dcache_clean(const void *addr, mr_size_t size) {
    const uint32_t *aligned_addr;
    uint32_t aligned_size;
    mr_err_t ret;

    if (size == 0U) {
        return 0;
    }
    ret = dcache_range(addr, size, &aligned_addr, &aligned_size);
    if (ret != 0) {
        return ret;
    }
    return dcache_status(
        HAL_DCACHE_CleanByAddr(&__dcache, aligned_addr, aligned_size));
}

mr_err_t mr_port_dcache_invalidate(const void *addr, mr_size_t size) {
    const uint32_t *aligned_addr;
    uint32_t aligned_size;
    mr_err_t ret;

    if (size == 0U) {
        return 0;
    }
    ret = dcache_range(addr, size, &aligned_addr, &aligned_size);
    if (ret != 0) {
        return ret;
    }
    return dcache_status(
        HAL_DCACHE_InvalidateByAddr(&__dcache, aligned_addr, aligned_size));
}

mr_err_t mr_port_dcache_clean_invalidate(const void *addr, mr_size_t size) {
    const uint32_t *aligned_addr;
    uint32_t aligned_size;
    mr_err_t ret;

    if (size == 0U) {
        return 0;
    }
    ret = dcache_range(addr, size, &aligned_addr, &aligned_size);
    if (ret != 0) {
        return ret;
    }
    return dcache_status(
        HAL_DCACHE_CleanInvalidByAddr(&__dcache, aligned_addr, aligned_size));
}

mr_err_t mr_port_dcache_clean_all(void) {
    return dcache_status(HAL_DCACHE_CleanByAddr(
        &__dcache, (const uint32_t *)0U, MR_UINT32_MAX));
}

mr_err_t mr_port_dcache_invalidate_all(void) {
    return dcache_status(HAL_DCACHE_Invalidate(&__dcache));
}

mr_err_t mr_port_dcache_clean_invalidate_all(void) {
    return dcache_status(HAL_DCACHE_CleanInvalidByAddr(
        &__dcache, (const uint32_t *)0U, MR_UINT32_MAX));
}
#endif /* defined(MR_USE_DCACHE) */

#if defined(MR_USE_ICACHE)
#include "stm32h5xx_hal_icache.h"

static mr_err_t icache_status(HAL_StatusTypeDef status) {
    mr_err_t ret = mr_stm32h5_hal_status(status);

    if (ret == 0) {
        __DSB();
        __ISB();
    }
    return ret;
}

mr_err_t mr_port_icache_enable(void) {
    return icache_status(HAL_ICACHE_Enable());
}

mr_err_t mr_port_icache_disable(void) {
    return icache_status(HAL_ICACHE_Disable());
}

mr_size_t mr_port_icache_line_size(void) {
    return STM32H5_CACHE_LINE_SIZE;
}

mr_err_t mr_port_icache_invalidate(const void *addr, mr_size_t size) {
    MR_IGNORE(addr);

    if (size == 0U) {
        return 0;
    }

    /* STM32H5 ICACHE only provides whole-cache invalidation */
    return icache_status(HAL_ICACHE_Invalidate());
}

mr_err_t mr_port_icache_invalidate_all(void) {
    return icache_status(HAL_ICACHE_Invalidate());
}
#endif /* defined(MR_USE_ICACHE) */

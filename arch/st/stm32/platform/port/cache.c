/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#include <mrX/port/mr_port_cache.h>

#if defined(MR_USE_DCACHE) || defined(MR_USE_ICACHE)
#if defined(MR_USE_DCACHE)
/* Data cache variant */
#if defined(HAL_DCACHE_MODULE_ENABLED) && defined(DCACHE1)
#define STM32_DCACHE_HAL
#if defined(DCACHE_CMDRSADDRR_CMDSTARTADDR_Pos)
#define STM32_DCACHE_LINE_SIZE                                                 \
    (1UL << DCACHE_CMDRSADDRR_CMDSTARTADDR_Pos)
#else
#define STM32_DCACHE_LINE_SIZE          (16U)
#endif /* defined(DCACHE_CMDRSADDRR_CMDSTARTADDR_Pos) */
#elif defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
#define STM32_DCACHE_SCB
#if defined(__SCB_DCACHE_LINE_SIZE)
#define STM32_DCACHE_LINE_SIZE          __SCB_DCACHE_LINE_SIZE
#else
#define STM32_DCACHE_LINE_SIZE          (32U)
#endif /* defined(__SCB_DCACHE_LINE_SIZE) */
#else
#error "MR_USE_DCACHE requires STM32 HAL DCACHE or CMSIS SCB D-Cache"
#endif /* STM32 D-Cache variant */
#endif /* defined(MR_USE_DCACHE) */

#if defined(MR_USE_ICACHE)
/* Instruction cache variant */
#if defined(HAL_ICACHE_MODULE_ENABLED)
#define STM32_ICACHE_HAL
#define STM32_ICACHE_LINE_SIZE          (16U)
#elif defined(__ICACHE_PRESENT) && (__ICACHE_PRESENT == 1U)
#define STM32_ICACHE_SCB
#if defined(__SCB_ICACHE_LINE_SIZE)
#define STM32_ICACHE_LINE_SIZE          __SCB_ICACHE_LINE_SIZE
#else
#define STM32_ICACHE_LINE_SIZE          (32U)
#endif /* defined(__SCB_ICACHE_LINE_SIZE) */
#else
#error "MR_USE_ICACHE requires STM32 HAL ICACHE or CMSIS SCB I-Cache"
#endif /* STM32 I-Cache variant */
#endif /* defined(MR_USE_ICACHE) */

#if defined(MR_USE_DCACHE) || defined(STM32_ICACHE_SCB)
static mr_err_t cache_range(const void *addr, mr_size_t size,
                            mr_size_t line_size, mr_size_t max_size,
                            mr_uintptr_t *aligned_addr,
                            mr_size_t *aligned_size) {
    const mr_uintptr_t max = ~(mr_uintptr_t)0U;
    mr_uintptr_t start, end, mask;
    mr_size_t span;

    /* Check cache line geometry */
    if ((line_size == 0U) || ((line_size & (line_size - 1U)) != 0U)) {
        return -MR_EINVAL;
    }

    /* Check address range */
    start = (mr_uintptr_t)addr;
    if ((mr_uint64_t)size > (mr_uint64_t)(max - start)) {
        return -MR_ERANGE;
    }

    /* Align range to cache lines */
    mask = (mr_uintptr_t)line_size - 1U;
    end = start + (mr_uintptr_t)size;
    if (end > (max - mask)) {
        return -MR_ERANGE;
    }
    start &= ~mask;
    end = (end + mask) & ~mask;

    /* Check backend size limit */
    span = (mr_size_t)(end - start);
    if (span > max_size) {
        return -MR_ERANGE;
    }

    /* Return aligned range */
    *aligned_addr = start;
    *aligned_size = span;
    return 0;
}
#endif /* defined(MR_USE_DCACHE) || defined(STM32_ICACHE_SCB) */

#if defined(MR_USE_DCACHE)
#if defined(STM32_DCACHE_HAL)
static DCACHE_HandleTypeDef __dcache = {
    .Instance = DCACHE1,
#if defined(DCACHE_READ_BURST_WRAP)
    .Init.ReadBurstType = DCACHE_READ_BURST_WRAP,
#endif /* defined(DCACHE_READ_BURST_WRAP) */
};
static mr_bool_t __dcache_inited;

static mr_err_t dcache_status(HAL_StatusTypeDef status) {
    mr_err_t ret;

    /* Convert HAL status */
    ret = mr_stm32_hal_status(status);
    if (ret != 0) {
        return ret;
    }

    /* Complete cache maintenance */
    __DSB();
    return 0;
}

static mr_err_t dcache_prepare(void) {
    mr_err_t ret;

    /* Skip initialized peripheral D-Cache */
    if (__dcache_inited) {
        return 0;
    }

#if defined(__HAL_RCC_DCACHE1_CLK_ENABLE)
    /* Enable peripheral D-Cache clock */
    __HAL_RCC_DCACHE1_CLK_ENABLE();
#endif /* defined(__HAL_RCC_DCACHE1_CLK_ENABLE) */

    /* Initialize peripheral D-Cache */
    ret = dcache_status(HAL_DCACHE_Init(&__dcache));
    if (ret != 0) {
        return ret;
    }
    __dcache_inited = MR_TRUE;
    return 0;
}
#endif /* defined(STM32_DCACHE_HAL) */

mr_err_t mr_port_dcache_enable(void) {
#if defined(STM32_DCACHE_HAL)
    mr_err_t ret;

    /* Prepare peripheral D-Cache */
    ret = dcache_prepare();
    if (ret != 0) {
        return ret;
    }

    /* Skip enabled peripheral D-Cache */
    if (HAL_DCACHE_IsEnabled(&__dcache) != 0U) {
        return 0;
    }

    /* Enable peripheral D-Cache */
    return dcache_status(HAL_DCACHE_Enable(&__dcache));
#else
    /* Enable core D-Cache */
    SCB_EnableDCache();
    __DSB();
    __ISB();
    return 0;
#endif /* defined(STM32_DCACHE_HAL) */
}

mr_err_t mr_port_dcache_disable(void) {
    mr_err_t ret;

#if defined(STM32_DCACHE_HAL)
    /* Skip uninitialized or disabled peripheral D-Cache */
    if (!__dcache_inited || (HAL_DCACHE_IsEnabled(&__dcache) == 0U)) {
        return 0;
    }
#endif /* defined(STM32_DCACHE_HAL) */

    /* Preserve dirty data */
    ret = mr_port_dcache_clean_all();
    if (ret != 0) {
        return ret;
    }

#if defined(STM32_DCACHE_HAL)
    /* Disable peripheral D-Cache */
    return dcache_status(HAL_DCACHE_Disable(&__dcache));
#else
    /* Disable core D-Cache */
    SCB_DisableDCache();
    __DSB();
    __ISB();
    return 0;
#endif /* defined(STM32_DCACHE_HAL) */
}

mr_size_t mr_port_dcache_line_size(void) {
    return STM32_DCACHE_LINE_SIZE;
}

mr_err_t mr_port_dcache_clean(const void *addr, mr_size_t size) {
    mr_uintptr_t aligned_addr;
    mr_size_t aligned_size;
    mr_err_t ret;

    /* Skip empty range */
    if (size == 0U) {
        return 0;
    }

#if defined(STM32_DCACHE_HAL)
    /* Prepare peripheral D-Cache */
    ret = dcache_prepare();
    if (ret != 0) {
        return ret;
    }

    /* Skip disabled peripheral D-Cache */
    if (HAL_DCACHE_IsEnabled(&__dcache) == 0U) {
        return 0;
    }
#endif /* defined(STM32_DCACHE_HAL) */

    /* Resolve aligned cache range */
#if defined(STM32_DCACHE_HAL)
    ret = cache_range(addr, size, STM32_DCACHE_LINE_SIZE, MR_UINT32_MAX,
                      &aligned_addr, &aligned_size);
#else
    ret = cache_range(addr, size, STM32_DCACHE_LINE_SIZE, MR_INT32_MAX,
                      &aligned_addr, &aligned_size);
#endif /* defined(STM32_DCACHE_HAL) */
    if (ret != 0) {
        return ret;
    }

    /* Clean cache range */
#if defined(STM32_DCACHE_HAL)
    return dcache_status(HAL_DCACHE_CleanByAddr(
        &__dcache, (const uint32_t *)aligned_addr, (uint32_t)aligned_size));
#else
    SCB_CleanDCache_by_Addr((void *)aligned_addr, (int32_t)aligned_size);
    return 0;
#endif /* defined(STM32_DCACHE_HAL) */
}

mr_err_t mr_port_dcache_invalidate(const void *addr, mr_size_t size) {
    mr_uintptr_t aligned_addr;
    mr_size_t aligned_size;
    mr_err_t ret;

    /* Skip empty range */
    if (size == 0U) {
        return 0;
    }

#if defined(STM32_DCACHE_HAL)
    /* Prepare peripheral D-Cache */
    ret = dcache_prepare();
    if (ret != 0) {
        return ret;
    }

    /* Skip disabled peripheral D-Cache */
    if (HAL_DCACHE_IsEnabled(&__dcache) == 0U) {
        return 0;
    }
#endif /* defined(STM32_DCACHE_HAL) */

    /* Resolve aligned cache range */
#if defined(STM32_DCACHE_HAL)
    ret = cache_range(addr, size, STM32_DCACHE_LINE_SIZE, MR_UINT32_MAX,
                      &aligned_addr, &aligned_size);
#else
    ret = cache_range(addr, size, STM32_DCACHE_LINE_SIZE, MR_INT32_MAX,
                      &aligned_addr, &aligned_size);
#endif /* defined(STM32_DCACHE_HAL) */
    if (ret != 0) {
        return ret;
    }

    /* Invalidate cache range */
#if defined(STM32_DCACHE_HAL)
    return dcache_status(HAL_DCACHE_InvalidateByAddr(
        &__dcache, (const uint32_t *)aligned_addr, (uint32_t)aligned_size));
#else
    SCB_InvalidateDCache_by_Addr((void *)aligned_addr, (int32_t)aligned_size);
    return 0;
#endif /* defined(STM32_DCACHE_HAL) */
}

mr_err_t mr_port_dcache_clean_invalidate(const void *addr, mr_size_t size) {
    mr_uintptr_t aligned_addr;
    mr_size_t aligned_size;
    mr_err_t ret;

    /* Skip empty range */
    if (size == 0U) {
        return 0;
    }

#if defined(STM32_DCACHE_HAL)
    /* Prepare peripheral D-Cache */
    ret = dcache_prepare();
    if (ret != 0) {
        return ret;
    }

    /* Skip disabled peripheral D-Cache */
    if (HAL_DCACHE_IsEnabled(&__dcache) == 0U) {
        return 0;
    }
#endif /* defined(STM32_DCACHE_HAL) */

    /* Resolve aligned cache range */
#if defined(STM32_DCACHE_HAL)
    ret = cache_range(addr, size, STM32_DCACHE_LINE_SIZE, MR_UINT32_MAX,
                      &aligned_addr, &aligned_size);
#else
    ret = cache_range(addr, size, STM32_DCACHE_LINE_SIZE, MR_INT32_MAX,
                      &aligned_addr, &aligned_size);
#endif /* defined(STM32_DCACHE_HAL) */
    if (ret != 0) {
        return ret;
    }

    /* Clean and invalidate cache range */
#if defined(STM32_DCACHE_HAL)
    return dcache_status(HAL_DCACHE_CleanInvalidByAddr(
        &__dcache, (const uint32_t *)aligned_addr, (uint32_t)aligned_size));
#else
    SCB_CleanInvalidateDCache_by_Addr((void *)aligned_addr,
                                      (int32_t)aligned_size);
    return 0;
#endif /* defined(STM32_DCACHE_HAL) */
}

mr_err_t mr_port_dcache_clean_all(void) {
#if defined(STM32_DCACHE_HAL)
    mr_err_t ret;

    /* Prepare peripheral D-Cache */
    ret = dcache_prepare();
    if (ret != 0) {
        return ret;
    }

    /* Skip disabled peripheral D-Cache */
    if (HAL_DCACHE_IsEnabled(&__dcache) == 0U) {
        return 0;
    }

    /* Clean entire peripheral D-Cache */
    return dcache_status(
        HAL_DCACHE_CleanByAddr(&__dcache, (const uint32_t *)0U, MR_UINT32_MAX));
#else
    /* Clean entire core D-Cache */
    SCB_CleanDCache();
    return 0;
#endif /* defined(STM32_DCACHE_HAL) */
}

mr_err_t mr_port_dcache_invalidate_all(void) {
#if defined(STM32_DCACHE_HAL)
    mr_err_t ret;

    /* Prepare peripheral D-Cache */
    ret = dcache_prepare();
    if (ret != 0) {
        return ret;
    }

    /* Skip disabled peripheral D-Cache */
    if (HAL_DCACHE_IsEnabled(&__dcache) == 0U) {
        return 0;
    }

    /* Invalidate entire peripheral D-Cache */
    return dcache_status(HAL_DCACHE_Invalidate(&__dcache));
#else
    /* Invalidate entire core D-Cache */
    SCB_InvalidateDCache();
    return 0;
#endif /* defined(STM32_DCACHE_HAL) */
}

mr_err_t mr_port_dcache_clean_invalidate_all(void) {
#if defined(STM32_DCACHE_HAL)
    mr_err_t ret;

    /* Prepare peripheral D-Cache */
    ret = dcache_prepare();
    if (ret != 0) {
        return ret;
    }

    /* Skip disabled peripheral D-Cache */
    if (HAL_DCACHE_IsEnabled(&__dcache) == 0U) {
        return 0;
    }

    /* Clean and invalidate entire peripheral D-Cache */
    return dcache_status(HAL_DCACHE_CleanInvalidByAddr(
        &__dcache, (const uint32_t *)0U, MR_UINT32_MAX));
#else
    /* Clean and invalidate entire core D-Cache */
    SCB_CleanInvalidateDCache();
    return 0;
#endif /* defined(STM32_DCACHE_HAL) */
}
#endif /* defined(MR_USE_DCACHE) */

#if defined(MR_USE_ICACHE)
#if defined(STM32_ICACHE_HAL)
static mr_err_t icache_status(HAL_StatusTypeDef status) {
    mr_err_t ret;

    /* Convert HAL status */
    ret = mr_stm32_hal_status(status);
    if (ret != 0) {
        return ret;
    }

    /* Complete cache maintenance */
    __DSB();
    __ISB();
    return 0;
}
#endif /* defined(STM32_ICACHE_HAL) */

mr_err_t mr_port_icache_enable(void) {
#if defined(STM32_ICACHE_HAL)
    /* Enable peripheral I-Cache */
    return icache_status(HAL_ICACHE_Enable());
#else
    /* Enable core I-Cache */
    SCB_EnableICache();
    __DSB();
    __ISB();
    return 0;
#endif /* defined(STM32_ICACHE_HAL) */
}

mr_err_t mr_port_icache_disable(void) {
#if defined(STM32_ICACHE_HAL)
    /* Disable peripheral I-Cache */
    return icache_status(HAL_ICACHE_Disable());
#else
    /* Disable core I-Cache */
    SCB_DisableICache();
    __DSB();
    __ISB();
    return 0;
#endif /* defined(STM32_ICACHE_HAL) */
}

mr_size_t mr_port_icache_line_size(void) {
    return STM32_ICACHE_LINE_SIZE;
}

mr_err_t mr_port_icache_invalidate(const void *addr, mr_size_t size) {
#if defined(STM32_ICACHE_SCB)
    mr_uintptr_t aligned_addr;
    mr_size_t aligned_size;
    mr_err_t ret;
#else
    MR_IGNORE(addr);
#endif /* defined(STM32_ICACHE_SCB) */

    /* Skip empty range */
    if (size == 0U) {
        return 0;
    }

#if defined(STM32_ICACHE_HAL)
    /* Peripheral I-Cache only supports whole-cache invalidation */
    return icache_status(HAL_ICACHE_Invalidate());
#else
    /* Resolve aligned cache range */
    ret = cache_range(addr, size, STM32_ICACHE_LINE_SIZE, MR_INT32_MAX,
                      &aligned_addr, &aligned_size);
    if (ret != 0) {
        return ret;
    }

    /* Invalidate core I-Cache range */
    SCB_InvalidateICache_by_Addr((void *)aligned_addr, (int32_t)aligned_size);
    return 0;
#endif /* defined(STM32_ICACHE_HAL) */
}

mr_err_t mr_port_icache_invalidate_all(void) {
#if defined(STM32_ICACHE_HAL)
    /* Invalidate entire peripheral I-Cache */
    return icache_status(HAL_ICACHE_Invalidate());
#else
    /* Invalidate entire core I-Cache */
    SCB_InvalidateICache();
    return 0;
#endif /* defined(STM32_ICACHE_HAL) */
}
#endif /* defined(MR_USE_ICACHE) */
#endif /* defined(MR_USE_DCACHE) || defined(MR_USE_ICACHE) */

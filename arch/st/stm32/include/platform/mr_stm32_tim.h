/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-11-25        MacRsh        Extract shared STM32 TIM clock helpers
 */

#ifndef __STM32_TIM_COMMON_H__
#define __STM32_TIM_COMMON_H__

#include <mrX/platform/mr_platform.h>

/* TIM time conversion definition */
#define STM32_TIM_NANOSECONDS           (1000000000ULL)

/* TIM prescaler definition */
#define STM32_TIM_PRESCALER_MAX         (65536ULL)
#define STM32_TIM_PSC_SEARCH_LIMIT      (64ULL)

#if defined(RCC_CLOCKTYPE_PCLK2)
MR_INLINE mr_bool_t stm32_tim_is_apb2(const TIM_TypeDef *instance) {
#if defined(TIM1)
    if (instance == TIM1) {
        return MR_TRUE;
    }
#endif /* defined(TIM1) */
#if defined(TIM8)
    if (instance == TIM8) {
        return MR_TRUE;
    }
#endif /* defined(TIM8) */
#if defined(TIM9)
    if (instance == TIM9) {
        return MR_TRUE;
    }
#endif /* defined(TIM9) */
#if defined(TIM10)
    if (instance == TIM10) {
        return MR_TRUE;
    }
#endif /* defined(TIM10) */
#if defined(TIM11)
    if (instance == TIM11) {
        return MR_TRUE;
    }
#endif /* defined(TIM11) */
#if defined(TIM15)
    if (instance == TIM15) {
        return MR_TRUE;
    }
#endif /* defined(TIM15) */
#if defined(TIM16)
    if (instance == TIM16) {
        return MR_TRUE;
    }
#endif /* defined(TIM16) */
#if defined(TIM17)
    if (instance == TIM17) {
        return MR_TRUE;
    }
#endif /* defined(TIM17) */
#if defined(TIM19)
    if (instance == TIM19) {
        return MR_TRUE;
    }
#endif /* defined(TIM19) */
#if defined(TIM20)
    if (instance == TIM20) {
        return MR_TRUE;
    }
#endif /* defined(TIM20) */
#if defined(TIM21)
    if (instance == TIM21) {
        return MR_TRUE;
    }
#endif /* defined(TIM21) */
#if defined(TIM22)
    if (instance == TIM22) {
        return MR_TRUE;
    }
#endif /* defined(TIM22) */
    return MR_FALSE;
}
#endif /* defined(RCC_CLOCKTYPE_PCLK2) */

MR_INLINE mr_uint32_t stm32_tim_apb_divider(mr_uint32_t divider,
                                            mr_bool_t is_apb2) {
#if defined(RCC_APB2_DIV1)
    if (is_apb2) {
        if (divider == RCC_APB2_DIV1) {
            return 1U;
        }
#if defined(RCC_APB2_DIV2)
        if (divider == RCC_APB2_DIV2) {
            return 2U;
        }
#endif /* defined(RCC_APB2_DIV2) */
#if defined(RCC_APB2_DIV4)
        if (divider == RCC_APB2_DIV4) {
            return 4U;
        }
#endif /* defined(RCC_APB2_DIV4) */
#if defined(RCC_APB2_DIV8)
        if (divider == RCC_APB2_DIV8) {
            return 8U;
        }
#endif /* defined(RCC_APB2_DIV8) */
#if defined(RCC_APB2_DIV16)
        if (divider == RCC_APB2_DIV16) {
            return 16U;
        }
#endif /* defined(RCC_APB2_DIV16) */
        return 0U;
    }
#else
    MR_IGNORE(is_apb2);
#endif /* defined(RCC_APB2_DIV1) */
#if defined(RCC_APB1_DIV1)
    if (divider == RCC_APB1_DIV1) {
        return 1U;
    }
#if defined(RCC_APB1_DIV2)
    if (divider == RCC_APB1_DIV2) {
        return 2U;
    }
#endif /* defined(RCC_APB1_DIV2) */
#if defined(RCC_APB1_DIV4)
    if (divider == RCC_APB1_DIV4) {
        return 4U;
    }
#endif /* defined(RCC_APB1_DIV4) */
#if defined(RCC_APB1_DIV8)
    if (divider == RCC_APB1_DIV8) {
        return 8U;
    }
#endif /* defined(RCC_APB1_DIV8) */
#if defined(RCC_APB1_DIV16)
    if (divider == RCC_APB1_DIV16) {
        return 16U;
    }
#endif /* defined(RCC_APB1_DIV16) */
    return 0U;
#else
#if defined(RCC_HCLK_DIV1)
    if (divider == RCC_HCLK_DIV1) {
        return 1U;
    }
#endif /* defined(RCC_HCLK_DIV1) */
#if defined(RCC_HCLK_DIV2)
    if (divider == RCC_HCLK_DIV2) {
        return 2U;
    }
#endif /* defined(RCC_HCLK_DIV2) */
#if defined(RCC_HCLK_DIV4)
    if (divider == RCC_HCLK_DIV4) {
        return 4U;
    }
#endif /* defined(RCC_HCLK_DIV4) */
#if defined(RCC_HCLK_DIV8)
    if (divider == RCC_HCLK_DIV8) {
        return 8U;
    }
#endif /* defined(RCC_HCLK_DIV8) */
#if defined(RCC_HCLK_DIV16)
    if (divider == RCC_HCLK_DIV16) {
        return 16U;
    }
#endif /* defined(RCC_HCLK_DIV16) */
    return 0U;
#endif /* defined(RCC_APB1_DIV1) */
}

MR_INLINE mr_bool_t stm32_tim_timpre_enabled(void) {
#if defined(RCC_CFGR1_TIMPRE)
    return ((RCC->CFGR1 & RCC_CFGR1_TIMPRE) != 0U);
#elif defined(RCC_DCKCFGR_TIMPRE)
    return ((RCC->DCKCFGR & RCC_DCKCFGR_TIMPRE) != 0U);
#elif defined(RCC_DCKCFGR1_TIMPRE)
    return ((RCC->DCKCFGR1 & RCC_DCKCFGR1_TIMPRE) != 0U);
#elif defined(RCC_CFGR_TIMPRE)
    return ((RCC->CFGR & RCC_CFGR_TIMPRE) != 0U);
#else
    return MR_FALSE;
#endif /* TIMPRE register layout */
}

MR_INLINE mr_uint64_t stm32_tim_apb_clock(const TIM_TypeDef *instance,
                                          mr_uint32_t *divider) {
    RCC_ClkInitTypeDef clock;
    uint32_t latency;

    /* Get clock configuration */
    HAL_RCC_GetClockConfig(&clock, &latency);

#if defined(RCC_CLOCKTYPE_PCLK2)
    /* Get APB2 clock */
    if (stm32_tim_is_apb2(instance)) {
        *divider = stm32_tim_apb_divider(clock.APB2CLKDivider, MR_TRUE);
        return HAL_RCC_GetPCLK2Freq();
    }
#else
    MR_IGNORE(instance);
#endif /* defined(RCC_CLOCKTYPE_PCLK2) */

    /* Get APB1 clock */
    *divider = stm32_tim_apb_divider(clock.APB1CLKDivider, MR_FALSE);
    return HAL_RCC_GetPCLK1Freq();
}

MR_INLINE mr_uint64_t stm32_tim_clock(const TIM_TypeDef *instance) {
    mr_uint64_t pclk;
    mr_uint32_t divider;

    /* Get APB clock */
    pclk = stm32_tim_apb_clock(instance, &divider);
    if ((pclk == 0ULL) || (divider == 0U)) {
        return 0ULL;
    }

    /* Check APB divider */
    if (divider == 1U) {
        return pclk;
    }

    /* Check TIMPRE */
    if (!stm32_tim_timpre_enabled()) {
        return pclk * 2ULL;
    }

    /* Calculate TIM clock */
    if (divider <= 4U) {
        return HAL_RCC_GetHCLKFreq();
    }
    return pclk * 4ULL;
}

MR_INLINE mr_err_t stm32_tim_period_ticks(mr_uint64_t clock, mr_uint64_t period,
                                          mr_uint64_t *ticks) {
    mr_uint64_t seconds, remainder, value;

    /* Split period */
    remainder = period % STM32_TIM_NANOSECONDS;
    seconds = period / STM32_TIM_NANOSECONDS;
    if (seconds > (MR_UINT64_MAX / clock)) {
        return -MR_ERANGE;
    }

    /* Calculate whole-second ticks */
    value = seconds * clock;

    /* Calculate sub-second ticks */
    remainder *= clock;
    remainder += STM32_TIM_NANOSECONDS / 2ULL;
    remainder /= STM32_TIM_NANOSECONDS;
    if (value > (MR_UINT64_MAX - remainder)) {
        return -MR_ERANGE;
    }

    /* Calculate target ticks */
    *ticks = value + remainder;
    if (*ticks == 0ULL) {
        return -MR_ERANGE;
    }
    return 0;
}

MR_INLINE mr_uint64_t stm32_tim_ticks_period(mr_uint64_t clock,
                                             mr_uint64_t ticks) {
    mr_uint64_t seconds, remainder, period;

    /* Split ticks */
    seconds = ticks / clock;
    remainder = ticks % clock;

    /* Calculate whole-second period */
    period = seconds * STM32_TIM_NANOSECONDS;

    /* Calculate sub-second period */
    remainder *= STM32_TIM_NANOSECONDS;
    remainder += clock / 2ULL;
    remainder /= clock;

    /* Calculate period */
    return period + remainder;
}

MR_INLINE mr_uint64_t stm32_tim_tick_error(mr_uint64_t target,
                                           mr_uint64_t actual) {
    /* Calculate tick error */
    if (actual >= target) {
        return actual - target;
    }
    return target - actual;
}

MR_INLINE mr_err_t stm32_tim_find_period(mr_uint64_t ticks,
                                         mr_uint64_t counter_max,
                                         mr_uint64_t factor,
                                         mr_uint64_t *prescaler,
                                         mr_uint64_t *counts) {
    mr_uint64_t best_error, search_end, psc_min;
    mr_uint64_t actual_ticks, divider, error;
    mr_uint64_t psc, value;

    /* Calculate prescaler range */
    divider = counter_max * factor;
    psc_min = (ticks + divider - 1ULL) / divider;
    if (psc_min == 0ULL) {
        psc_min = 1ULL;
    }
    search_end = psc_min + STM32_TIM_PSC_SEARCH_LIMIT - 1ULL;
    if (search_end > STM32_TIM_PRESCALER_MAX) {
        search_end = STM32_TIM_PRESCALER_MAX;
    }

    /* Find best period */
    *prescaler = 0ULL;
    *counts = 0ULL;
    best_error = MR_UINT64_MAX;
    for (psc = psc_min; psc <= search_end; psc++) {
        /* Calculate candidate counts */
        divider = psc * factor;
        value = (ticks + (divider / 2ULL)) / divider;
        if ((value == 0ULL) || (value > counter_max)) {
            continue;
        }

        /* Calculate candidate error */
        actual_ticks = value * divider;
        error = stm32_tim_tick_error(ticks, actual_ticks);
        if (error >= best_error) {
            continue;
        }

        /* Update best period */
        best_error = error;
        *prescaler = psc;
        *counts = value;
        if (error == 0ULL) {
            break;
        }
    }

    /* Check result */
    if (*counts == 0ULL) {
        return -MR_ERANGE;
    }
    return 0;
}

#endif /* __STM32_TIM_COMMON_H__ */

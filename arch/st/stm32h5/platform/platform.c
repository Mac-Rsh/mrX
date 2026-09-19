/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include <mrX/platform/mr_platform.h>

void mr_platform_init(void) {
    /* Initialize the STM32H5 clock port */
    mr_stm32h5_platform_clock_init(MR_CFG_TICK_PER_SECOND);
}

mr_err_t mr_stm32h5_gpio_clock_enable(GPIO_TypeDef *port) {
    switch ((mr_uintptr_t)port) {
#if defined(GPIOA) && defined(__HAL_RCC_GPIOA_CLK_ENABLE)
        case (mr_uintptr_t)GPIOA: {
            __HAL_RCC_GPIOA_CLK_ENABLE();
            break;
        }
#endif /* defined(GPIOA) && defined(__HAL_RCC_GPIOA_CLK_ENABLE) */
#if defined(GPIOB) && defined(__HAL_RCC_GPIOB_CLK_ENABLE)
        case (mr_uintptr_t)GPIOB: {
            __HAL_RCC_GPIOB_CLK_ENABLE();
            break;
        }
#endif /* defined(GPIOB) && defined(__HAL_RCC_GPIOB_CLK_ENABLE) */
#if defined(GPIOC) && defined(__HAL_RCC_GPIOC_CLK_ENABLE)
        case (mr_uintptr_t)GPIOC: {
            __HAL_RCC_GPIOC_CLK_ENABLE();
            break;
        }
#endif /* defined(GPIOC) && defined(__HAL_RCC_GPIOC_CLK_ENABLE) */
#if defined(GPIOD) && defined(__HAL_RCC_GPIOD_CLK_ENABLE)
        case (mr_uintptr_t)GPIOD: {
            __HAL_RCC_GPIOD_CLK_ENABLE();
            break;
        }
#endif /* defined(GPIOD) && defined(__HAL_RCC_GPIOD_CLK_ENABLE) */
#if defined(GPIOE) && defined(__HAL_RCC_GPIOE_CLK_ENABLE)
        case (mr_uintptr_t)GPIOE: {
            __HAL_RCC_GPIOE_CLK_ENABLE();
            break;
        }
#endif /* defined(GPIOE) && defined(__HAL_RCC_GPIOE_CLK_ENABLE) */
#if defined(GPIOF) && defined(__HAL_RCC_GPIOF_CLK_ENABLE)
        case (mr_uintptr_t)GPIOF: {
            __HAL_RCC_GPIOF_CLK_ENABLE();
            break;
        }
#endif /* defined(GPIOF) && defined(__HAL_RCC_GPIOF_CLK_ENABLE) */
#if defined(GPIOG) && defined(__HAL_RCC_GPIOG_CLK_ENABLE)
        case (mr_uintptr_t)GPIOG: {
            __HAL_RCC_GPIOG_CLK_ENABLE();
            break;
        }
#endif /* defined(GPIOG) && defined(__HAL_RCC_GPIOG_CLK_ENABLE) */
#if defined(GPIOH) && defined(__HAL_RCC_GPIOH_CLK_ENABLE)
        case (mr_uintptr_t)GPIOH: {
            __HAL_RCC_GPIOH_CLK_ENABLE();
            break;
        }
#endif /* defined(GPIOH) && defined(__HAL_RCC_GPIOH_CLK_ENABLE) */
#if defined(GPIOI) && defined(__HAL_RCC_GPIOI_CLK_ENABLE)
        case (mr_uintptr_t)GPIOI: {
            __HAL_RCC_GPIOI_CLK_ENABLE();
            break;
        }
#endif /* defined(GPIOI) && defined(__HAL_RCC_GPIOI_CLK_ENABLE) */
        default: {
            return -MR_EINVAL;
        }
    }
    return 0;
}

/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First generic STM32 version
 */

#ifndef __MR_PLATFORM_H__
#define __MR_PLATFORM_H__

#include <mrX/mr_config.h>
#include <mrX/libc/mr_compiler.h>
#include <mrX/libc/mr_types.h>
#include <mrX/libc/mr_errno.h>
#include MR_CFG_STM32_HAL_HEADER
#if defined(MR_USE_DRIVER_GPIO)
#include <mrX/drivers/gpio/mr_gpio.h>
#endif /* defined(MR_USE_DRIVER_GPIO) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Platform
 * @{
 */

/**
 * @brief This function initializes the STM32 clock port for mrX.
 *
 * @param per_second The number of clock ticks per second.
 *
 * @note CubeMX must initialize HAL and the system clock before this function
 * is called. The platform overrides HAL_IncTick to update both clocks;
 * the board SysTick handler must call HAL_IncTick exactly once per tick.
 */
extern void mr_stm32_platform_clock_init(mr_uint32_t per_second);

/**
 * @brief This function initializes the STM32 platform.
 */
extern void mr_platform_init(void);

/**
 * @brief This function converts a HAL status to an mrX error code.
 *
 * @param status The HAL operation status.
 * @return 0 on success, or the corresponding negative mrX error code.
 */
MR_INLINE mr_err_t mr_stm32_hal_status(HAL_StatusTypeDef status) {
    switch (status) {
        case HAL_OK: {
            return 0;
        }
        case HAL_BUSY: {
            return -MR_EBUSY;
        }
        case HAL_TIMEOUT: {
            return -MR_ETIMEDOUT;
        }
        default: {
            return -MR_EIO;
        }
    }
}

#if defined(MR_USE_DRIVER_GPIO)
/**
 * @brief This function muxes a GPIO specification to an alternate function.
 *
 * @param spec The GPIO specification owned by the STM32 GPIO backend.
 * @param af The STM32 alternate-function number.
 * @param pull The pull configuration.
 * @param otype The output type (push-pull or open-drain).
 * @return 0 on success, or a negative error code.
 *
 * @note GPIO generations with AFR registers use af directly. Legacy CRL/CRH
 * GPIO needs direction/remap information and currently returns -MR_ENOSYS.
 */
extern mr_err_t mr_stm32_gpio_pin_alternate(const mr_ht_gpio_spec_t *spec,
                                            mr_uint32_t af, mr_gpio_pull_t pull,
                                            mr_gpio_otype_t otype);

/**
 * @brief This function releases a GPIO specification from peripheral mux.
 *
 * @param spec The GPIO specification owned by the STM32 GPIO backend.
 * @return 0 on success, or a negative error code.
 */
extern mr_err_t mr_stm32_gpio_pin_release(const mr_ht_gpio_spec_t *spec);
#endif /* defined(MR_USE_DRIVER_GPIO) */

#if defined(MR_USE_TRACE) && defined(MR_USE_MODULE_XTRACE)
/**
 * @brief This function registers the STM32 RTT output transport for xtrace.
 *
 * @return 0 on success, or a negative error code.
 *
 * @note Uses one clock hook slot (mr_clock_hook_add) to drain the xtrace FIFO
 * into the SEGGER RTT up buffer in the background.
 */
extern mr_err_t mr_stm32_xtrace_rtt_register(void);

/**
 * @brief This function drains pending xtrace bytes into the RTT buffer.
 */
extern void mr_stm32_xtrace_rtt_poll(void);
#endif /* defined(MR_USE_TRACE) && defined(MR_USE_MODULE_XTRACE) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_PLATFORM_H__ */

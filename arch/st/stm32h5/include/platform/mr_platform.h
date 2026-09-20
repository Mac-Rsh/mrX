/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-20        MacRsh        GPIO owner helpers replace port clock switch
 */

#ifndef __MR_PLATFORM_H__
#define __MR_PLATFORM_H__

#include <mrX/mr_config.h>
#include <mrX/libc/mr_compiler.h>
#include <mrX/libc/mr_types.h>
#include <mrX/libc/mr_errno.h>
#include "stm32h5xx_hal.h"
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
 * @brief This function initializes the STM32H5 clock port for mrX.
 *
 * @param per_second The number of clock ticks per second.
 *
 * @note CubeMX must initialize HAL and the system clock before this function
 * is called. The platform overrides HAL_IncTick to update both clocks;
 * the board SysTick handler must call HAL_IncTick exactly once per tick.
 */
extern void mr_stm32h5_platform_clock_init(mr_uint32_t per_second);

/**
 * @brief This function initializes the STM32H5 platform.
 */
extern void mr_platform_init(void);

/**
 * @brief This function converts a HAL status to an mrX error code.
 *
 * @param status The HAL operation status.
 * @return 0 on success, or the corresponding negative mrX error code.
 */
MR_INLINE mr_err_t mr_stm32h5_hal_status(HAL_StatusTypeDef status) {
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
 * @param spec The GPIO specification owned by the STM32H5 GPIO backend.
 * @param af The STM32 alternate-function number (0..15).
 * @param pull The pull configuration.
 * @param otype The output type (push-pull or open-drain).
 * @return 0 on success, or a negative error code.
 *
 * @note The GPIO public config cannot carry an AF number. This helper is the
 * GPIO owner's AF path; consumers must not call HAL_GPIO_Init.
 */
extern mr_err_t mr_stm32h5_gpio_pin_alternate(const mr_ht_gpio_spec_t *spec,
                                              mr_uint32_t af,
                                              mr_gpio_pull_t pull,
                                              mr_gpio_otype_t otype);

/**
 * @brief This function releases a GPIO specification from peripheral mux.
 *
 * @param spec The GPIO specification owned by the STM32H5 GPIO backend.
 * @return 0 on success, or a negative error code.
 *
 * @note Use this when a consumer stops using a pin (for example UART drops
 * TX or RX). It does not return the pin to a GPIO API mode.
 */
extern mr_err_t mr_stm32h5_gpio_pin_release(const mr_ht_gpio_spec_t *spec);
#endif /* defined(MR_USE_DRIVER_GPIO) */

#if defined(MR_USE_DRIVER_TMR)
struct mr_driver;

/**
 * @brief This function stops an H5 timer at its next update boundary.
 *
 * @param drv The exclusively owned STM32H5 basic timer driver.
 * @param complete The timer IRQ callback, or MR_NULL to cancel a drain.
 * @param param The callback parameter.
 * @return 0 on success, a negative error code on failure.
 *
 * @note The timer must be configured. Its current counter is preserved.
 * A stopped timer is armed for one period on its next start. Cancel before
 * releasing the driver, including after a failed start. The caller must arm
 * this before the next update; the period must exceed DMA IRQ latency.
 */
extern mr_err_t mr_stm32h5_tmr_drain(struct mr_driver *drv,
                                     void (*complete)(void *), void *param);
#endif /* defined(MR_USE_DRIVER_TMR) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_PLATFORM_H__ */

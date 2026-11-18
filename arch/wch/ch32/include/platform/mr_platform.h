/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_PLATFORM_H__
#define __MR_PLATFORM_H__

#include <mrX/mr_config.h>
#include <mrX/libc/mr_compiler.h>
#include <mrX/libc/mr_types.h>
#include <mrX/libc/mr_errno.h>

#if (defined(MR_USE_CH32V00X) + defined(MR_USE_CH32VM00X)                      \
     + defined(MR_USE_CH32V10X) + defined(MR_USE_CH32V20X)                     \
     + defined(MR_USE_CH32V205) + defined(MR_USE_CH32V30X)                     \
     + defined(MR_USE_CH32V4X7)) != 1
#error "Select one CH32 EVT SDK family"
#endif /* CH32 EVT SDK family count */

#if defined(MR_USE_CH32V00X)
#include "ch32v00x.h"
#include "ch32v00x_rcc.h"
#elif defined(MR_USE_CH32VM00X)
#include "ch32v00X.h"
#include "ch32v00X_rcc.h"
#elif defined(MR_USE_CH32V10X)
#include "ch32v10x.h"
#include "ch32v10x_rcc.h"
#elif defined(MR_USE_CH32V20X)
#include "ch32v20x.h"
#include "ch32v20x_rcc.h"
#elif defined(MR_USE_CH32V205)
#include "ch32v205.h"
#include "ch32v205_rcc.h"
#elif defined(MR_USE_CH32V30X)
#include "ch32v30x.h"
#include "ch32v30x_rcc.h"
#elif defined(MR_USE_CH32V4X7)
#include "ch32v4x7.h"
#include "ch32v4x7_rcc.h"
#endif /* CH32 EVT SDK family */
#if defined(MR_USE_DRIVER_GPIO)
#include <mrX/drivers/gpio/mr_gpio.h>
#endif /* defined(MR_USE_DRIVER_GPIO) */
#if defined(MR_USE_DRIVER_TMR) || defined(MR_USE_DRIVER_PWM)
#include <mrX/drivers/mr_driver.h>
#if defined(MR_USE_CH32V20X) && (defined(CH32V20x_D8) || defined(CH32V20x_D8W))
#define MR_CH32_HAS_TIM5_32BIT
#endif /* CH32V20x device with 32-bit TIM5 */
#endif /* defined(MR_USE_DRIVER_TMR) || defined(MR_USE_DRIVER_PWM) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Platform
 * @{
 */

/* QingKe hardware-stacked interrupt attribute definition */
#define MR_CH32_INTERRUPT                                                      \
    __attribute__((interrupt("WCH-Interrupt-fast")))

/**
 * @brief This function checks whether an IRQ is in the PFIC peripheral range.
 *
 * @param irq The IRQ number, or a negative value when unused.
 * @return MR_TRUE when the IRQ number is in the peripheral range.
 */
MR_INLINE mr_bool_t mr_ch32_irq_valid(int irq) {
    return (irq >= 16) && (irq < 256);
}

/**
 * @brief This function initializes the SysTick clock port.
 *
 * @param per_second The configured kernel tick frequency in Hz.
 * @note Called once by mr_platform_init.
 */
extern void mr_ch32_platform_clock_init(mr_uint32_t per_second);

/**
 * @brief This function initializes the CH32 platform.
 *
 * @note mrX takes ownership of SysTick. Keep HCLK constant after
 * initialization.
 */
extern void mr_platform_init(void);

/**
 * @brief This function writes a diagnostic message through the board transport.
 *
 * @param buf The bytes to write.
 * @param size The byte count, greater than zero and representable as int.
 * @return The byte count written, or a negative error code.
 *
 * @note The board may override this weak hook. The default forwards to the
 * EVT debug transport with MR_USE_CH32_DEBUG_OUTPUT or returns -MR_ENOSYS.
 * The transport may block while polling hardware and must work with interrupts
 * masked for assertion output. Nested mrX writes return -MR_EBUSY.
 */
extern int mr_ch32_platform_output(const char *buf, mr_size_t size);

#if defined(MR_USE_DRIVER_GPIO)
/**
 * @brief This function muxes a GPIO specification to a CH32 peripheral.
 *
 * @param spec The GPIO specification owned by the CH32 GPIO backend.
 * @param output True for an alternate-function output or false for an input.
 * @param pull The input pull. Outputs require MR_GPIO_PULL_NONE.
 * @param otype The output driver type.
 * @return 0 on success, or a negative error code.
 *
 * @note The caller selects the peripheral remap separately.
 */
extern mr_err_t mr_ch32_gpio_pin_alternate(const mr_ht_gpio_spec_t *spec,
                                           mr_bool_t output,
                                           mr_gpio_pull_t pull,
                                           mr_gpio_otype_t otype);

/**
 * @brief This function releases a GPIO specification from peripheral mux.
 *
 * @param spec The GPIO specification owned by the CH32 GPIO backend.
 * @return 0 on success, or a negative error code.
 */
extern mr_err_t mr_ch32_gpio_pin_release(const mr_ht_gpio_spec_t *spec);
#endif /* defined(MR_USE_DRIVER_GPIO) */

#if defined(MR_USE_DRIVER_TMR) || defined(MR_USE_DRIVER_PWM)
/** @brief The quantized timer period and its input clock settings. */
typedef struct mr_ch32_timer_base {
    mr_uint64_t counts, period;
    mr_uint32_t hz, divider, clocks_per_count;
} mr_ch32_timer_base_t;

/**
 * @brief Quantizes a period using the smallest supported timer divider.
 *
 * @param hz The timer input clock in Hz.
 * @param period The requested period in nanoseconds.
 * @param max_counts The maximum hardware counts per traversal, at most 2^32.
 * @param count_factor The counter traversals per period, one or two.
 * @param base The resulting timer settings, valid on success.
 * @return 0 on success or -MR_ERANGE if the period is out of range.
 */
extern mr_err_t mr_ch32_timer_make_period(mr_uint32_t hz, mr_uint64_t period,
                                          mr_uint64_t max_counts,
                                          mr_uint32_t count_factor,
                                          mr_ch32_timer_base_t *base);

#if defined(MR_USE_DRIVER_PWM)
/**
 * @brief Quantizes a pulse using a previously calculated timer base.
 *
 * @param base The PWM timer settings from mr_ch32_timer_make_period(),
 * calculated with a count limit that fits the compare register.
 * @param pulse The requested pulse width in nanoseconds.
 * @param compare The compare value, valid on success.
 * @param actual The quantized pulse width in nanoseconds, valid on success.
 * @return 0 on success or -MR_EINVAL if the pulse exceeds the period.
 */
extern mr_err_t mr_ch32_timer_make_pulse(const mr_ch32_timer_base_t *base,
                                         mr_uint64_t pulse,
                                         mr_uint32_t *compare,
                                         mr_uint64_t *actual);
#endif /* defined(MR_USE_DRIVER_PWM) */

/**
 * @brief Writes the reload value using the timer's hardware register width.
 *
 * @param tim The timer instance, with its peripheral clock enabled.
 * @param reload The reload value within the instance's register range.
 */
extern void mr_ch32_timer_set_reload(TIM_TypeDef *tim, mr_uint32_t reload);

/**
 * @brief This function claims exclusive use of a CH32 hardware timer.
 *
 * @param tim The timer instance.
 * @param drv The claiming driver.
 * @return 0 on success, -MR_EBUSY if another driver owns the timer, or
 * -MR_ENOMEM if the platform owner table is full.
 */
extern mr_err_t mr_ch32_timer_claim(TIM_TypeDef *tim, const mr_driver_t *drv);
#endif /* defined(MR_USE_DRIVER_TMR) || defined(MR_USE_DRIVER_PWM) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_PLATFORM_H__ */

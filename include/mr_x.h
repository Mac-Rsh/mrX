/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_X_H__
#define __MR_X_H__

#include <mr_config.h>

/**
 * @addtogroup Kernel
 * @{
 */

#include <mr-X/kernel/mr_clock.h>
#include <mr-X/kernel/mr_fifo.h>
#include <mr-X/kernel/mr_hook.h>
#include <mr-X/kernel/mr_initcall.h>
#include <mr-X/kernel/mr_irq.h>
#include <mr-X/kernel/mr_list.h>
#include <mr-X/kernel/mr_listify.h>
#include <mr-X/kernel/mr_log.h>
#include <mr-X/kernel/mr_object.h>
#include <mr-X/kernel/mr_printf.h>
#include <mr-X/kernel/mr_ref.h>
#include <mr-X/kernel/mr_service.h>
#include <mr-X/kernel/mr_spinlock.h>
#include <mr-X/kernel/mr_timer.h>
#include <mr-X/kernel/mr_version.h>
#include <mr-X/kernel/mr_wait.h>
#include <mr-X/kernel/mr_workqueue.h>

/** @} */

/**
 * @addtogroup Libc
 * @{
 */

#include <mr-X/libc/mr_assert.h>
#include <mr-X/libc/mr_atomic.h>
#include <mr-X/libc/mr_compiler.h>
#include <mr-X/libc/mr_errno.h>
#include <mr-X/libc/mr_malloc.h>
#include <mr-X/libc/mr_sprintf.h>
#include <mr-X/libc/mr_sscanf.h>
#include <mr-X/libc/mr_string.h>
#include <mr-X/libc/mr_types.h>

/** @} */

/**
 * @addtogroup Port
 * @{
 */

#include <mr-X/port/mr_port_assert.h>
#include <mr-X/port/mr_port_irq.h>
#include <mr-X/port/mr_port_output.h>

/** @} */

/**
 * @addtogroup Drivers
 * @{
 */

#if defined(MR_USE_DRIVER)
#include <mr-X/drivers/mr_driver.h>
#if defined(MR_USE_DRIVER_GPIO)
#include <mr-X/drivers/gpio/mr_gpio.h>
#endif /* defined(MR_USE_DRIVER_GPIO) */
#if defined(MR_USE_DRIVER_UART)
#include <mr-X/drivers/uart/mr_uart.h>
#endif /* defined(MR_USE_DRIVER_UART) */
#endif /* defined(MR_USE_DRIVER) */

/** @} */

#endif /* __MR_X_H__ */

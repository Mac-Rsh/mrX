/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MRX_H__
#define __MRX_H__

#include <mrX/mr_config.h>

/**
 * @addtogroup Kernel
 * @{
 */

#include <mrX/kernel/mr_clock.h>
#include <mrX/kernel/mr_fifo.h>
#include <mrX/kernel/mr_hashmap.h>
#include <mrX/kernel/mr_hook.h>
#include <mrX/kernel/mr_initcall.h>
#include <mrX/kernel/mr_irq.h>
#include <mrX/kernel/mr_list.h>
#include <mrX/kernel/mr_listify.h>
#include <mrX/kernel/mr_log.h>
#include <mrX/kernel/mr_object.h>
#include <mrX/kernel/mr_printf.h>
#include <mrX/kernel/mr_ref.h>
#include <mrX/kernel/mr_service.h>
#include <mrX/kernel/mr_slab.h>
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/kernel/mr_timer.h>
#include <mrX/kernel/mr_version.h>
#include <mrX/kernel/mr_wait.h>
#include <mrX/kernel/mr_work.h>

/** @} */

/**
 * @addtogroup Libc
 * @{
 */

#include <mrX/libc/mr_assert.h>
#include <mrX/libc/mr_atomic.h>
#include <mrX/libc/mr_compiler.h>
#include <mrX/libc/mr_errno.h>
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_sprintf.h>
#include <mrX/libc/mr_sscanf.h>
#include <mrX/libc/mr_string.h>
#include <mrX/libc/mr_types.h>

/** @} */

/**
 * @addtogroup Drivers
 * @{
 */

#if defined(MR_USE_DRIVER)
#include <mrX/drivers/mr_driver.h>
#if defined(MR_USE_DRIVER_RAM)
#include <mrX/drivers/ram/mr_ram.h>
#endif /* defined(MR_USE_DRIVER_RAM) */
#if defined(MR_USE_DRIVER_GPIO)
#include <mrX/drivers/gpio/mr_gpio.h>
#endif /* defined(MR_USE_DRIVER_GPIO) */
#if defined(MR_USE_DRIVER_UART)
#include <mrX/drivers/uart/mr_uart.h>
#endif /* defined(MR_USE_DRIVER_UART) */
#if defined(MR_USE_DRIVER_I2C)
#include <mrX/drivers/i2c/mr_i2c.h>
#endif /* defined(MR_USE_DRIVER_I2C) */
#if defined(MR_USE_DRIVER_ADC)
#include <mrX/drivers/adc/mr_adc.h>
#endif /* defined(MR_USE_DRIVER_ADC) */
#if defined(MR_USE_DRIVER_DAC)
#include <mrX/drivers/dac/mr_dac.h>
#endif /* defined(MR_USE_DRIVER_DAC) */
#if defined(MR_USE_DRIVER_DMA)
#include <mrX/drivers/dma/mr_dma.h>
#endif /* defined(MR_USE_DRIVER_DMA) */
#if defined(MR_USE_DRIVER_TMR)
#include <mrX/drivers/tmr/mr_tmr.h>
#endif /* defined(MR_USE_DRIVER_TMR) */
#if defined(MR_USE_DRIVER_WDT)
#include <mrX/drivers/wdt/mr_wdt.h>
#endif /* defined(MR_USE_DRIVER_WDT) */
#if defined(MR_USE_DRIVER_FLASH)
#include <mrX/drivers/flash/mr_flash.h>
#endif /* defined(MR_USE_DRIVER_FLASH) */
#endif /* defined(MR_USE_DRIVER) */

/** @} */

/**
 * @addtogroup File-System
 * @{
 */

#if defined(MR_USE_FS)
#include <mrX/fs/mr_fs_system.h>
#include <mrX/fs/mr_fs_sblock.h>
#include <mrX/fs/mr_fs_dentry.h>
#include <mrX/fs/mr_fs_inode.h>
#include <mrX/fs/mr_fs_file.h>
#endif /* defined(MR_USE_FS) */

/**
 * @addtogroup Modules
 * @{
 */

#if defined(MR_USE_MODULE)
#if defined(MR_USE_MODULE_XVM)
#include <mrX/modules/xvm/mr_xvm.h>
#include <mrX/modules/xvm/mr_xvm_gdb.h>
#endif /* defined(MR_USE_MODULE_XVM) */
#if defined(MR_USE_MODULE_RPC)
#include <mrX/modules/rpc/mr_rpc.h>
#endif /* defined(MR_USE_MODULE_RPC) */
#endif /* defined(MR_USE_MODULE) */

/** @} */

#endif /* __MRX_H__ */

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

#include <mr-X/mr_clock.h>
#include <mr-X/mr_fifo.h>
#include <mr-X/mr_hook.h>
#include <mr-X/mr_initcall.h>
#include <mr-X/mr_irq.h>
#include <mr-X/mr_list.h>
#include <mr-X/mr_object.h>
#include <mr-X/mr_printf.h>
#include <mr-X/mr_ref.h>
#include <mr-X/mr_service.h>
#include <mr-X/mr_spinlock.h>
#include <mr-X/mr_timer.h>
#include <mr-X/mr_version.h>
#include <mr-X/mr_wait.h>
#include <mr-X/mr_workqueue.h>

/** @} */

/**
 * @addtogroup Libc
 * @{
 */

#include <libc/mr_assert.h>
#include <libc/mr_atomic.h>
#include <libc/mr_compiler.h>
#include <libc/mr_errno.h>
#include <libc/mr_malloc.h>
#include <libc/mr_sprintf.h>
#include <libc/mr_sscanf.h>
#include <libc/mr_string.h>
#include <libc/mr_types.h>

/** @} */

/**
 * @addtogroup Port
 * @{
 */

#include <port/mr_port_assert.h>
#include <port/mr_port_irq.h>
#include <port/mr_port_output.h>

/** @} */

#endif /* __MR_X_H__ */

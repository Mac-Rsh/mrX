/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_X_H__
#define __MR_X_H__

/**
 * @addtogroup Kernel
 * @{
 */

#include <kernel/mr_initcall.h>
#include <kernel/mr_clock.h>
#include <kernel/mr_fifo.h>
#include <kernel/mr_hook.h>
#include <kernel/mr_irq.h>
#include <kernel/mr_list.h>
#include <kernel/mr_printf.h>
#include <kernel/mr_ref.h>
#include <kernel/mr_service.h>
#include <kernel/mr_spinlock.h>
#include <kernel/mr_timer.h>
#include <kernel/mr_version.h>
#include <kernel/mr_workqueue.h>

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
#include <libc/mr_scanf.h>
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

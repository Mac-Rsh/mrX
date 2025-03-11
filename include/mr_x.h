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
#include <kernel/mr_kclock.h>
#include <kernel/mr_kfifo.h>
#include <kernel/mr_khook.h>
#include <kernel/mr_klist.h>
#include <kernel/mr_kobject.h>
#include <kernel/mr_kprintf.h>
#include <kernel/mr_kref.h>
#include <kernel/mr_kservice.h>
#include <kernel/mr_kspinlock.h>
#include <kernel/mr_ktimer.h>
#include <kernel/mr_kversion.h>
#include <kernel/mr_kworkqueue.h>

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
#include <libc/mr_printf.h>
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
#include <port/mr_port_thread.h>

/** @} */

#endif /* __MR_X_H__ */

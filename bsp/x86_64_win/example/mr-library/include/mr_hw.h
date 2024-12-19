/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_HW_H__
#define __MR_HW_H__

#include "mr-base/mr_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Hardware interface
 * @{
 */

/* CPU interface */
extern void mr_hw_cpu_relax(void);

/* Interrupts interface */
extern int mr_hw_irq_save(void);
extern void mr_hw_irq_restore(int mask);

/* Stack interface */
extern void *mr_hw_stack_init(void *entry, void *stack_addr);
extern void mr_hw_context_switch(void *from, void *to);
extern void mr_hw_context_switch_to(void *to);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_HW_H__ */

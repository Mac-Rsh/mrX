/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_HW_STACK_H__
#define __MR_HW_STACK_H__

#ifdef __cplusplus
"C" {
#endif /* __cplusplus */

/**
 * @addtogroup Stack hardware interface
 * @{
 */

/**
 * @brief This function initialize the hardware stack.
 * @param entry The entry point.
 * @param stack_addr The stack address(top).
 * @return The top of the stack.
 */
 void *mr_hw_stack_init(void *entry, void *stack_addr);

/**
 * @brief This function switch the hardware stack.
 *
 * @param from The old stack address(top).
 * @param to The new stack address(top).
 */
void mr_hw_context_switch(void *from, void *to);

/**
 * @brief This function switch the hardware stack.
 *
 * @param to The new stack address(top).
 */
void mr_hw_context_switch_to(void *to);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_HW_STACK_H__ */

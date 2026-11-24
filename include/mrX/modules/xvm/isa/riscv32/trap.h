/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __RISCV32_TRAP_H__
#define __RISCV32_TRAP_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_MODULE_XVM_RISCV32)
#include <mrX/modules/xvm/isa/riscv32/core.h>
#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup XVM-RISCV32
 * @{
 */

#if defined(MR_USE_MODULE_XVM_RISCV32)
/**
 * @brief This function writes back a pending syscall return value to a0.
 *
 * @param core The RISCV32 core.
 */
void xvm_riscv32_syscall_writeback(core_t *core);

/**
 * @brief This function populates an XVM syscall event from guest registers.
 *
 * @param core The RISCV32 core.
 * @param event The XVM event to fill.
 * @return MR_XVM_SYSCALL or MR_XVM_EXIT.
 */
mr_xvm_return_t xvm_riscv32_syscall_handle(core_t *core,
                                           mr_xvm_event_t *event);

/**
 * @brief This function refreshes machine interrupt pending bits.
 *
 * @param core The RISCV32 core.
 */
void xvm_riscv32_interrupt_update(core_t *core);

/**
 * @brief This function enters an enabled pending machine interrupt.
 *
 * @param core The RISCV32 core.
 * @return MR_TRUE if an interrupt trap was entered, MR_FALSE otherwise.
 */
mr_bool_t xvm_riscv32_interrupt_enter(core_t *core);

/**
 * @brief This function enters a synchronous exception trap handler.
 *
 * @param core The RISCV32 core.
 * @param except_nr The XVM exception number.
 * @param mtval The trap value recorded into MTVAL.
 * @return MR_TRUE if the trap was taken by MTVEC, MR_FALSE otherwise.
 */
mr_bool_t xvm_riscv32_trap_enter(core_t *core, mr_uint32_t except_nr,
                                 mr_uint32_t mtval);
#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __RISCV32_TRAP_H__ */

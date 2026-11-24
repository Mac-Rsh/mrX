/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __RISCV32_EXEC_H__
#define __RISCV32_EXEC_H__

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
 * @brief This function executes up to step instructions on a RISCV32 core.
 *
 * @param xvm The XVM instance.
 * @param core The RISCV32 core.
 * @param event The XVM event output.
 * @param step The maximum number of instructions to execute.
 * @return The XVM execution return status.
 */
mr_xvm_return_t xvm_riscv32_exec_run(mr_xvm_t *xvm, core_t *core,
                                     mr_xvm_event_t *event, mr_uint32_t step);
#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __RISCV32_EXEC_H__ */

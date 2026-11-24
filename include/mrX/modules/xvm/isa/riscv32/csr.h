/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __RISCV32_CSR_H__
#define __RISCV32_CSR_H__

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
 * @brief This function reads a RISCV32 CSR register.
 *
 * @param core The RISCV32 core.
 * @param addr The 12-bit CSR address.
 * @param val The output value pointer.
 * @return 0 on success, a negative exception number on failure.
 */
int xvm_riscv32_csr_load(core_t *core, mr_uint32_t addr, mr_uint32_t *val);

/**
 * @brief This function writes a RISCV32 CSR register.
 *
 * @param core The RISCV32 core.
 * @param addr The 12-bit CSR address.
 * @param val The value to write.
 * @return 0 on success, a negative exception number on failure.
 */
int xvm_riscv32_csr_store(core_t *core, mr_uint32_t addr, mr_uint32_t val);
#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __RISCV32_CSR_H__ */

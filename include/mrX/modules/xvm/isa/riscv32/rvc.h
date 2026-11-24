/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __RISCV32_RVC_H__
#define __RISCV32_RVC_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_MODULE_XVM_RISCV32_C)
#include <mrX/modules/xvm/isa/riscv32/core.h>
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_C) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup XVM-RISCV32
 * @{
 */

#if defined(MR_USE_MODULE_XVM_RISCV32_C)
/**
 * @brief This function expands a 16-bit RV32C compressed instruction into its
 * equivalent 32-bit RV32I instruction.
 *
 * @param half The 16-bit compressed instruction parcel.
 * @param out The output 32-bit expanded instruction pointer.
 * @return 0 on success, -MR_XVM_EXCEPT_NR_ILLEGAL on an illegal encoding.
 */
int xvm_riscv32_rvc_expand(mr_uint16_t half, mr_uint32_t *out);
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_C) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __RISCV32_RVC_H__ */

/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __RISCV32_INSTR_H__
#define __RISCV32_INSTR_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_MODULE_XVM_RISCV32)
#include <mrX/libc/mr_types.h>
#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup XVM-RISCV32
 * @{
 */

#if defined(MR_USE_MODULE_XVM_RISCV32)
/* RISCV32 instruction field extraction macros (endian/ABI-independent) */
#define INSTR_OPCODE(_raw)              ((_raw) & 0x7fU)
#define INSTR_RD(_raw)                  (((_raw) >> 7) & 0x1fU)
#define INSTR_FUNCT3(_raw)              (((_raw) >> 12) & 0x7U)
#define INSTR_RS1(_raw)                 (((_raw) >> 15) & 0x1fU)
#define INSTR_RS2(_raw)                 (((_raw) >> 20) & 0x1fU)
#define INSTR_FUNCT7(_raw)              (((_raw) >> 25) & 0x7fU)
#define INSTR_CSR(_raw)                 (((_raw) >> 20) & 0xfffU)

/* Branchless sign-extension helpers */
#define IMM_SEXT12(_imm)                (((_imm) ^ 0x800U) - 0x800U)
#define IMM_SEXT13(_imm)                (((_imm) ^ 0x1000U) - 0x1000U)
#define IMM_SEXT21(_imm)                (((_imm) ^ 0x100000U) - 0x100000U)

/* Immediate extraction macros */
#define INSTR_I_IMM(_raw)               IMM_SEXT12(((mr_uint32_t)(_raw)) >> 20)
#define INSTR_S_IMM(_raw)                                                      \
    IMM_SEXT12((((_raw) >> 20) & 0xfe0U) | (((_raw) >> 7) & 0x1fU))
#define INSTR_B_IMM(_raw)                                                      \
    IMM_SEXT13((((_raw) >> 19) & 0x1000U) | (((_raw) << 4) & 0x800U)           \
               | (((_raw) >> 20) & 0x7e0U) | (((_raw) >> 7) & 0x1eU))
#define INSTR_U_IMM(_raw)               ((_raw) & 0xfffff000U)
#define INSTR_J_IMM(_raw)                                                      \
    IMM_SEXT21((((_raw) >> 11) & 0x100000U) | ((_raw) & 0xff000U)              \
               | (((_raw) >> 9) & 0x800U) | (((_raw) >> 20) & 0x7feU))
#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __RISCV32_INSTR_H__ */

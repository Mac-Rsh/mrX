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
/* RISCV32 instruction type */
typedef union instr {
    struct instr_R_type {
        mr_uint32_t opcode: 7;
        mr_uint32_t rd: 5;
        mr_uint32_t funct3: 3;
        mr_uint32_t rs1: 5;
        mr_uint32_t rs2: 5;
        mr_uint32_t funct7: 7;
    } R_type;
    struct instr_I_type {
        mr_uint32_t opcode: 7;
        mr_uint32_t rd: 5;
        mr_uint32_t funct3: 3;
        mr_uint32_t rs1: 5;
        mr_uint32_t imm: 12;
    } I_type;
    struct instr_S_type {
        mr_uint32_t opcode: 7;
        mr_uint32_t imm1: 5;
        mr_uint32_t funct3: 3;
        mr_uint32_t rs1: 5;
        mr_uint32_t rs2: 5;
        mr_uint32_t imm2: 7;
    } S_type;
    struct instr_B_type {
        mr_uint32_t opcode: 7;
        mr_uint32_t imm1: 1;
        mr_uint32_t imm2: 4;
        mr_uint32_t funct3: 3;
        mr_uint32_t rs1: 5;
        mr_uint32_t rs2: 5;
        mr_uint32_t imm3: 6;
        mr_uint32_t imm4: 1;
    } B_type;
    struct instr_U_type {
        mr_uint32_t opcode: 7;
        mr_uint32_t rd: 5;
        mr_uint32_t imm: 20;
    } U_type;
    struct instr_J_type {
        mr_uint32_t opcode: 7;
        mr_uint32_t rd: 5;
        mr_uint32_t imm1: 8;
        mr_uint32_t imm2: 1;
        mr_uint32_t imm3: 10;
        mr_uint32_t imm4: 1;
    } J_type;
    struct instr_fmt_type {
        mr_uint32_t opcode: 7;
        mr_uint32_t raw: 25;
    } fmt;
    mr_uint32_t raw;
} instr_t;

#define IMM_SEXT12(_imm)         (((_imm) & 0x800) ? (0xfffff000 | (_imm)) : (_imm))
#define IMM_SEXT13(_imm)         (((_imm) & 0x1000) ? (0xffffe000 | (_imm)) : (_imm))
#define IMM_SEXT21(_imm)         (((_imm) & 0x100000) ? (0xffe00000 | (_imm)) : (_imm))
#define INSTR_I_TYPE_IMM(_instr) IMM_SEXT12((_instr).I_type.imm)
#define INSTR_S_TYPE_IMM(_instr)                                               \
    IMM_SEXT12(                                                                \
        (mr_uint32_t)(((_instr).S_type.imm2 << 5) | (_instr).S_type.imm1))
#define INSTR_B_TYPE_IMM(_instr)                                               \
    IMM_SEXT13((mr_uint32_t)(((_instr).B_type.imm4 << 12)                      \
                             | ((_instr).B_type.imm1 << 11)                    \
                             | ((_instr).B_type.imm3 << 5)                     \
                             | ((_instr).B_type.imm2 << 1)))
#define INSTR_J_TYPE_IMM(_instr)                                               \
    IMM_SEXT21((mr_uint32_t)(((_instr).J_type.imm4 << 20)                      \
                             | ((_instr).J_type.imm2 << 11)                    \
                             | ((_instr).J_type.imm1 << 12)                    \
                             | ((_instr).J_type.imm3 << 1)))
#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __RISCV32_INSTR_H__ */

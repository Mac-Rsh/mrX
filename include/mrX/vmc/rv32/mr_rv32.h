/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_VMC_RV32_H__
#define __MR_VMC_RV32_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_VMC_RV32)
#include <mrX/vmc/mr_vmc.h>
#endif /* defined(MR_USE_VMC_RV32) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup VMC-RV32
 * @{
 */

#if defined(MR_USE_VMC_RV32)
/* RV32 byte-code type */
typedef enum mr_rv32_bcode {
    MR_RV32_BCODE_UNKNOWN = 0,
    /* R-type(0b0110011) */
    MR_RV32_BCODE_ADD,
    MR_RV32_BCODE_SUB,
    MR_RV32_BCODE_XOR,
    MR_RV32_BCODE_OR,
    MR_RV32_BCODE_AND,
    MR_RV32_BCODE_SLL,
    MR_RV32_BCODE_SRL,
    MR_RV32_BCODE_SRA,
    MR_RV32_BCODE_SLT,
    MR_RV32_BCODE_SLTU,
    /* I-type(0b0010011) */
    MR_RV32_BCODE_ADDI,
    MR_RV32_BCODE_XORI,
    MR_RV32_BCODE_ORI,
    MR_RV32_BCODE_ANDI,
    MR_RV32_BCODE_SLLI,
    MR_RV32_BCODE_SRLI,
    MR_RV32_BCODE_SRAI,
    MR_RV32_BCODE_SLTI,
    MR_RV32_BCODE_SLTIU,
    /* I-type(0b0000011) */
    MR_RV32_BCODE_LB,
    MR_RV32_BCODE_LH,
    MR_RV32_BCODE_LW,
    MR_RV32_BCODE_LBU,
    MR_RV32_BCODE_LHU,
    /* S-type(0b0100011) */
    MR_RV32_BCODE_SB,
    MR_RV32_BCODE_SH,
    MR_RV32_BCODE_SW,
    /* B-type(0b1100011) */
    MR_RV32_BCODE_BEQ,
    MR_RV32_BCODE_BNE,
    MR_RV32_BCODE_BLT,
    MR_RV32_BCODE_BGE,
    MR_RV32_BCODE_BLTU,
    MR_RV32_BCODE_BGEU,
    /* J-type(0b1101111) */
    MR_RV32_BCODE_JAL,
    /* I-type(0b1100111) */
    MR_RV32_BCODE_JALR,
    /* U-type(0b0110111) */
    MR_RV32_BCODE_LUI,
    /* U-type(0b0010111) */
    MR_RV32_BCODE_AUIPC,
    /* I-type(0b1110011) */
    MR_RV32_BCODE_ECALL,
    MR_RV32_BCODE_EBREAK,
    MR_RV32_BCODE_CSRRW,
    MR_RV32_BCODE_CSRRS,
    MR_RV32_BCODE_CSRRC,
    MR_RV32_BCODE_CSRRWI,
    MR_RV32_BCODE_CSRRSI,
    MR_RV32_BCODE_CSRRCI,
    /* FENCE(0b0001111) */
    MR_RV32_BCODE_FENCE,
    /* R-type(0b0110011) */
    MR_RV32_BCODE_MUL,
    MR_RV32_BCODE_MULH,
    MR_RV32_BCODE_MULHSU,
    MR_RV32_BCODE_MULHU,
    MR_RV32_BCODE_DIV,
    MR_RV32_BCODE_DIVU,
    MR_RV32_BCODE_REM,
    MR_RV32_BCODE_REMU,
    /* R-type(0b0101111) */
    MR_RV32_BCODE_LRW,
    MR_RV32_BCODE_SCW,
    MR_RV32_BCODE_ADDW,
    MR_RV32_BCODE_XORW,
    MR_RV32_BCODE_ORW,
    MR_RV32_BCODE_ANDW,
    MR_RV32_BCODE_MINW,
    MR_RV32_BCODE_MAXW,
    MR_RV32_BCODE_MINUW,
    MR_RV32_BCODE_MAXUW,
    MR_RV32_BCODE_SWAPW,
} mr_rv32_bcode_t;

/* RV32 instruction type */
typedef union mr_rv32_instr {
    struct mr_rv32_instr_R_type {
        mr_uint32_t opcode: 7;
        mr_uint32_t rd: 5;
        mr_uint32_t funct3: 3;
        mr_uint32_t rs1: 5;
        mr_uint32_t rs2: 5;
        mr_uint32_t funct7: 7;
    } R_type;
    struct mr_rv32_instr_I_type {
        mr_uint32_t opcode: 7;
        mr_uint32_t rd: 5;
        mr_uint32_t funct3: 3;
        mr_uint32_t rs1: 5;
        mr_uint32_t imm: 12;
    } I_type;
    struct mr_rv32_instr_S_type {
        mr_uint32_t opcode: 7;
        mr_uint32_t imm1: 5;
        mr_uint32_t funct3: 3;
        mr_uint32_t rs1: 5;
        mr_uint32_t rs2: 5;
        mr_uint32_t imm2: 7;
    } S_type;
    struct mr_rv32_instr_B_type {
        mr_uint32_t opcode: 7;
        mr_uint32_t imm1: 1;
        mr_uint32_t imm2: 4;
        mr_uint32_t funct3: 3;
        mr_uint32_t rs1: 5;
        mr_uint32_t rs2: 5;
        mr_uint32_t imm3: 6;
        mr_uint32_t imm4: 1;
    } B_type;
    struct mr_rv32_instr_U_type {
        mr_uint32_t opcode: 7;
        mr_uint32_t rd: 5;
        mr_uint32_t imm: 20;
    } U_type;
    struct mr_rv32_instr_J_type {
        mr_uint32_t opcode: 7;
        mr_uint32_t rd: 5;
        mr_uint32_t imm1: 8;
        mr_uint32_t imm2: 1;
        mr_uint32_t imm3: 10;
        mr_uint32_t imm4: 1;
    } J_type;
    struct mr_rv32_instr_A_type {
        mr_uint32_t opcode: 7;
        mr_uint32_t rd: 5;
        mr_uint32_t funct3: 3;
        mr_uint32_t rs1: 5;
        mr_uint32_t rs2: 5;
        mr_uint32_t rl: 1;
        mr_uint32_t aq: 1;
        mr_uint32_t funct5: 5;
    } A_type;
    struct mr_rv32_instr_fmt_type {
        mr_uint32_t opcode: 7;
        mr_uint32_t raw: 25;
    } fmt;
    mr_uint32_t raw;
} mr_rv32_instr_t;

/* RV32 CSR type */
typedef struct mr_rv32_csr {
    mr_uint32_t mstatus;
    mr_uint32_t mtvec;
    mr_uint32_t mie;
    mr_uint32_t mip;
    mr_uint32_t mepc;
    mr_uint32_t mtval;
    mr_uint32_t mcause;
} mr_rv32_csr_t;

/* RV32 core type */
MR_VMC_CLAZZ_EXPORT(rv32);
typedef struct mr_rv32_core {
    /* Memory */
    const mr_uint8_t *image;
    mr_uint32_t image_size;
    mr_uint32_t memory_size;
    mr_uint32_t storage_addr;
    mr_uint32_t entry_addr;
    /* Registers */
    mr_uint32_t regs[32];
    mr_rv32_csr_t csr;
    mr_uint32_t pc;
    mr_uint8_t memory[];
} mr_rv32_core_t;
#endif /* defined(MR_USE_VMC_RV32) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_VMC_RV32_H__ */

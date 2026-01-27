/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_VMC_RISCV32_H__
#define __MR_VMC_RISCV32_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_VMC_RISCV32)
#include <mrX/vmc/mr_vmc.h>
#endif /* defined(MR_USE_VMC_RISCV32) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup VMC-RISCV32
 * @{
 */

#if defined(MR_USE_VMC_RISCV32)
/* RISCV32 byte-code type */
typedef enum mr_riscv32_bcode {
    MR_RISCV32_BCODE_UNKNOWN = 0,
    /* R-type(0b0110011) */
    MR_RISCV32_BCODE_ADD,
    MR_RISCV32_BCODE_SUB,
    MR_RISCV32_BCODE_XOR,
    MR_RISCV32_BCODE_OR,
    MR_RISCV32_BCODE_AND,
    MR_RISCV32_BCODE_SLL,
    MR_RISCV32_BCODE_SRL,
    MR_RISCV32_BCODE_SRA,
    MR_RISCV32_BCODE_SLT,
    MR_RISCV32_BCODE_SLTU,
    /* I-type(0b0010011) */
    MR_RISCV32_BCODE_ADDI,
    MR_RISCV32_BCODE_XORI,
    MR_RISCV32_BCODE_ORI,
    MR_RISCV32_BCODE_ANDI,
    MR_RISCV32_BCODE_SLLI,
    MR_RISCV32_BCODE_SRLI,
    MR_RISCV32_BCODE_SRAI,
    MR_RISCV32_BCODE_SLTI,
    MR_RISCV32_BCODE_SLTIU,
    /* I-type(0b0000011) */
    MR_RISCV32_BCODE_LB,
    MR_RISCV32_BCODE_LH,
    MR_RISCV32_BCODE_LW,
    MR_RISCV32_BCODE_LBU,
    MR_RISCV32_BCODE_LHU,
    /* S-type(0b0100011) */
    MR_RISCV32_BCODE_SB,
    MR_RISCV32_BCODE_SH,
    MR_RISCV32_BCODE_SW,
    /* B-type(0b1100011) */
    MR_RISCV32_BCODE_BEQ,
    MR_RISCV32_BCODE_BNE,
    MR_RISCV32_BCODE_BLT,
    MR_RISCV32_BCODE_BGE,
    MR_RISCV32_BCODE_BLTU,
    MR_RISCV32_BCODE_BGEU,
    /* J-type(0b1101111) */
    MR_RISCV32_BCODE_JAL,
    /* I-type(0b1100111) */
    MR_RISCV32_BCODE_JALR,
    /* U-type(0b0110111) */
    MR_RISCV32_BCODE_LUI,
    /* U-type(0b0010111) */
    MR_RISCV32_BCODE_AUIPC,
    /* I-type(0b1110011) */
    MR_RISCV32_BCODE_ECALL,
    MR_RISCV32_BCODE_EBREAK,
    MR_RISCV32_BCODE_CSRRW,
    MR_RISCV32_BCODE_CSRRS,
    MR_RISCV32_BCODE_CSRRC,
    MR_RISCV32_BCODE_CSRRWI,
    MR_RISCV32_BCODE_CSRRSI,
    MR_RISCV32_BCODE_CSRRCI,
    /* FENCE(0b0001111) */
    MR_RISCV32_BCODE_FENCE,
    /* R-type(0b0110011) */
    MR_RISCV32_BCODE_MUL,
    MR_RISCV32_BCODE_MULH,
    MR_RISCV32_BCODE_MULHSU,
    MR_RISCV32_BCODE_MULHU,
    MR_RISCV32_BCODE_DIV,
    MR_RISCV32_BCODE_DIVU,
    MR_RISCV32_BCODE_REM,
    MR_RISCV32_BCODE_REMU,
    /* R-type(0b0101111) */
    MR_RISCV32_BCODE_LRW,
    MR_RISCV32_BCODE_SCW,
    MR_RISCV32_BCODE_ADDW,
    MR_RISCV32_BCODE_XORW,
    MR_RISCV32_BCODE_ORW,
    MR_RISCV32_BCODE_ANDW,
    MR_RISCV32_BCODE_MINW,
    MR_RISCV32_BCODE_MAXW,
    MR_RISCV32_BCODE_MINUW,
    MR_RISCV32_BCODE_MAXUW,
    MR_RISCV32_BCODE_SWAPW,
} mr_riscv32_bcode_t;

/* RISCV32 instruction type */
typedef union mr_riscv32_instr {
    struct mr_riscv32_instr_R_type {
        mr_uint32_t opcode: 7;
        mr_uint32_t rd: 5;
        mr_uint32_t funct3: 3;
        mr_uint32_t rs1: 5;
        mr_uint32_t rs2: 5;
        mr_uint32_t funct7: 7;
    } R_type;
    struct mr_riscv32_instr_I_type {
        mr_uint32_t opcode: 7;
        mr_uint32_t rd: 5;
        mr_uint32_t funct3: 3;
        mr_uint32_t rs1: 5;
        mr_uint32_t imm: 12;
    } I_type;
    struct mr_riscv32_instr_S_type {
        mr_uint32_t opcode: 7;
        mr_uint32_t imm1: 5;
        mr_uint32_t funct3: 3;
        mr_uint32_t rs1: 5;
        mr_uint32_t rs2: 5;
        mr_uint32_t imm2: 7;
    } S_type;
    struct mr_riscv32_instr_B_type {
        mr_uint32_t opcode: 7;
        mr_uint32_t imm1: 1;
        mr_uint32_t imm2: 4;
        mr_uint32_t funct3: 3;
        mr_uint32_t rs1: 5;
        mr_uint32_t rs2: 5;
        mr_uint32_t imm3: 6;
        mr_uint32_t imm4: 1;
    } B_type;
    struct mr_riscv32_instr_U_type {
        mr_uint32_t opcode: 7;
        mr_uint32_t rd: 5;
        mr_uint32_t imm: 20;
    } U_type;
    struct mr_riscv32_instr_J_type {
        mr_uint32_t opcode: 7;
        mr_uint32_t rd: 5;
        mr_uint32_t imm1: 8;
        mr_uint32_t imm2: 1;
        mr_uint32_t imm3: 10;
        mr_uint32_t imm4: 1;
    } J_type;
    struct mr_riscv32_instr_A_type {
        mr_uint32_t opcode: 7;
        mr_uint32_t rd: 5;
        mr_uint32_t funct3: 3;
        mr_uint32_t rs1: 5;
        mr_uint32_t rs2: 5;
        mr_uint32_t rl: 1;
        mr_uint32_t aq: 1;
        mr_uint32_t funct5: 5;
    } A_type;
    struct mr_riscv32_instr_fmt_type {
        mr_uint32_t opcode: 7;
        mr_uint32_t raw: 25;
    } fmt;
    mr_uint32_t raw;
} mr_riscv32_instr_t;

/* RISCV32 register type */
typedef struct mr_riscv32_regs {
    /* CSR */
    mr_uint32_t mstatus;
    mr_uint32_t misa;
    mr_uint32_t mie;
    mr_uint32_t mtvec;
    mr_uint32_t mscratch;
    mr_uint32_t mepc;
    mr_uint32_t mcause;
    mr_uint32_t mtval;
    mr_uint32_t mip;
    mr_uint32_t mcycleh;
    mr_uint32_t mcyclel;
    mr_uint32_t minstreth;
    mr_uint32_t minstretl;
    /* GPR */
    mr_uint32_t x[32];
    /* PC */
    mr_uint32_t pc;
} mr_riscv32_regs_t;

/* RISCV32 core type */
MR_VMC_CLAZZ_EXPORT(riscv32);
typedef struct mr_riscv32_core {
    /* Registers */
    mr_riscv32_regs_t regs;
    /* Memory */
    const mr_uint8_t *image;
    mr_uint32_t image_size;
    mr_uint32_t memory_size;
    mr_uint32_t storage_addr;
    mr_uint32_t entry_addr;
    mr_uint8_t memory[];
} mr_riscv32_core_t;
#endif /* defined(MR_USE_VMC_RISCV32) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_VMC_RISCV32_H__ */

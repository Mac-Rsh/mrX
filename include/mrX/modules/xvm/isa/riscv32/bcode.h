/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __RISCV32_BCODE_H__
#define __RISCV32_BCODE_H__

#include <mrX/mr_config.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup XVM-RISCV32
 * @{
 */

#if defined(MR_USE_MODULE_XVM_RISCV32)
/* RISCV32 byte-code definition */
typedef enum bcode {
    BCODE_UNKNOWN = 0,
    /* OP (R-type) */
    BCODE_ADD,
    BCODE_SUB,
    BCODE_SLL,
    BCODE_SLT,
    BCODE_SLTU,
    BCODE_XOR,
    BCODE_SRL,
    BCODE_SRA,
    BCODE_OR,
    BCODE_AND,
    /* OP-IMM (I-type) */
    BCODE_ADDI,
    BCODE_SLTI,
    BCODE_SLTIU,
    BCODE_XORI,
    BCODE_ORI,
    BCODE_ANDI,
    BCODE_SLLI,
    BCODE_SRLI,
    BCODE_SRAI,
    /* LOAD (I-type) */
    BCODE_LB,
    BCODE_LH,
    BCODE_LW,
    BCODE_LBU,
    BCODE_LHU,
    /* STORE (S-type) */
    BCODE_SB,
    BCODE_SH,
    BCODE_SW,
    /* BRANCH (B-type) */
    BCODE_BEQ,
    BCODE_BNE,
    BCODE_BLT,
    BCODE_BGE,
    BCODE_BLTU,
    BCODE_BGEU,
    /* JUMP */
    BCODE_JAL,
    BCODE_JALR,
    /* Upper immediate */
    BCODE_LUI,
    BCODE_AUIPC,
    /* M extension */
    BCODE_MUL,
    BCODE_MULH,
    BCODE_MULHSU,
    BCODE_MULHU,
    BCODE_DIV,
    BCODE_DIVU,
    BCODE_REM,
    BCODE_REMU,
    /* System */
    BCODE_ECALL,
    BCODE_EBREAK,
    BCODE_MRET,
    BCODE_CSRRW,
    BCODE_CSRRS,
    BCODE_CSRRC,
    BCODE_CSRRWI,
    BCODE_CSRRSI,
    BCODE_CSRRCI,
    BCODE_FENCE,
} bcode_t;

/* RISCV32 byte-code decode macro function definition */
#define BCODE_DECODE_MAP(_code, _val)                                          \
    do {                                                                       \
        case (_code): {                                                        \
            return (_val);                                                     \
        }                                                                      \
    } while (0)
#define BCODE_DECODE_DEFAULT(_val)                                             \
    do {                                                                       \
        default: {                                                             \
            return (_val);                                                     \
        }                                                                      \
    } while (0)
#define BCODE_DECODE_FUNC(_code)        case (_code):
#define BCODE_DECODE_EXIT(_ret)         return (_ret)

/* RISCV32 byte-code execute macro function definition */
#define BCODE_EXECUTE_TO(_bcode)        switch (_bcode)
#define BCODE_EXECUTE_FUNC(_bcode)      case (_bcode):
#define BCODE_EXECUTE_EXIT(_ret)        return (_ret)
#define BCODE_EXECUTE_DEFAULT(_val)                                            \
    do {                                                                       \
        default: {                                                             \
            return (_val);                                                     \
        }                                                                      \
    } while (0)
#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __RISCV32_BCODE_H__ */

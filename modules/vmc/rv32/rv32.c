/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/vmc/rv32/mr_rv32.h>
#if defined(MR_USE_VMC_RV32)
#include <mrX/libc/mr_string.h>

/* RV32 VMC clazzes(static & dynamic) */
static mr_err_t rv32_vmc_load(mr_vmc_t *vmc, mr_vmc_env_t *env);
static mr_vmc_return_t rv32_vmc_exec(mr_vmc_t *vmc, mr_vmc_env_t *env,
                                     mr_vmc_event_t *event,
                                     mr_uint32_t instr_step);
static mr_err_t rv32_vmc_debug(mr_vmc_t *vmc, mr_vmc_env_t *env,
                               mr_uint32_t **reg_addr, mr_uint32_t *reg_size,
                               mr_uint32_t **memory_addr,
                               mr_uint32_t *memory_size);
MR_VMC_CLAZZ_DEFINE(rv32, rv32_vmc_load, rv32_vmc_exec, rv32_vmc_debug);

/* RV32 decode definition */
#define RV32_DECODE_MAP(_case, _val)                                           \
    do {                                                                       \
        case (_case): {                                                        \
            return (_val);                                                     \
        }                                                                      \
    } while (0)
#define RV32_DECODE_DEFAULT(_val)                                              \
    do {                                                                       \
        default: {                                                             \
            return (_val);                                                     \
        }                                                                      \
    } while (0)
#define RV32_DECODE_FUNC(_case)         case (_case):
#define RV32_DECODE_EXIT(_ret)          return (_ret)
/* RV32 execute definition */
#if defined(__GNUC__)
/* GCC computed goto has one less lookup table memory access than switch-case
 * and has better cache utilization (code vs data segment) */
#define RV32_EXEC_TO(_table, _bcode)    goto *((_table)[(_bcode)]);
#define RV32_EXEC_FUNC(_bcode, _label)  _label:
#define RV32_EXEC_DEFAULT(_val)
#else
#define RV32_EXEC_TO(_table, _bcode)    switch (_bcode)
#define RV32_EXEC_FUNC(_bcode, _label)  case (_bcode):
#define RV32_EXEC_DEFAULT(_val)                                                \
    do {                                                                       \
        default: {                                                             \
            return (_val);                                                     \
        }                                                                      \
    } while (0)
#endif /* defined(__GNUC__) */
#define RV32_EXEC_EXIT(_ret)            return (_ret)
/* RV32 core definition */
#define RV32_CORE_REG(_core, _reg)      ((_core)->regs[(_reg)])
#define RV32_CORE_PC(_core)             ((_core)->pc)
/* RV32 address may be unaligned, software VM handles it transparently */
#define RV32_CORE_IMGu1(_core, _off)                                           \
    ((mr_uint8_t *)((_core)->image + (_off)))
#define RV32_CORE_IMGu2(_core, _off)                                           \
    ((mr_uint16_t *)((_core)->image + (_off)))
#define RV32_CORE_IMGu4(_core, _off)                                           \
    ((mr_uint32_t *)((_core)->image + (_off)))
#define RV32_CORE_IMGi1(_core, _off)                                           \
    ((mr_int8_t *)((_core)->image + (_off)))
#define RV32_CORE_IMGi2(_core, _off)                                           \
    ((mr_int16_t *)((_core)->image + (_off)))
#define RV32_CORE_IMGi4(_core, _off)                                           \
    ((mr_int32_t *)((_core)->image + (_off)))
#define RV32_CORE_MEMu1(_core, _off)                                           \
    ((mr_uint8_t *)((_core)->memory + (_off)))
#define RV32_CORE_MEMu2(_core, _off)                                           \
    ((mr_uint16_t *)((_core)->memory + (_off)))
#define RV32_CORE_MEMu4(_core, _off)                                           \
    ((mr_uint32_t *)((_core)->memory + (_off)))
#define RV32_CORE_MEMi1(_core, _off)                                           \
    ((mr_int8_t *)((_core)->memory + (_off)))
#define RV32_CORE_MEMi2(_core, _off)                                           \
    ((mr_int16_t *)((_core)->memory + (_off)))
#define RV32_CORE_MEMi4(_core, _off)                                           \
    ((mr_int32_t *)((_core)->memory + (_off)))
#define RV32_CORE_IMG_OFF(_core, _off)  ((_off) - (_core)->entry_addr)
#define RV32_CORE_MEM_OFF(_core, _off)  ((_off) - (_core)->storage_addr)
/* RV32 MMIO definition */
#define RV32_MMIO_IN_IMG(_core, _off)   ((_off) < ((_core)->image_size))
#define RV32_MMIO_IN_MEM(_core, _off)   ((_off) < (_core)->memory_size)

static mr_err_t rv32_vmc_load(mr_vmc_t *vmc, mr_vmc_env_t *env) {
    mr_rv32_core_t *core;

    MR_IGNORE(vmc);

    /* Get RV32 core */
    core = (mr_rv32_core_t *)env->memory;

    /* Init RV32 core */
    mr_memset(core, 0, sizeof(mr_rv32_core_t));
    core->memory_size = env->memory_size - (mr_uint32_t)sizeof(mr_rv32_core_t);
    core->storage_addr = env->storage_addr;
    core->image_size = env->image_size;
    core->entry_addr = env->entry_addr;
    core->image = env->image;
    return 0;
}

MR_INLINE mr_uint32_t rv32_instr_decode(mr_rv32_instr_t instr) {
    switch (instr.fmt.opcode) {
        case 0b0110011: {
            if (instr.R_type.funct7 == 0x01) {
                switch (instr.R_type.funct3) {
                    RV32_DECODE_MAP(0x0, MR_RV32_BCODE_MUL);
                    RV32_DECODE_MAP(0x1, MR_RV32_BCODE_MULH);
                    RV32_DECODE_MAP(0x2, MR_RV32_BCODE_MULHSU);
                    RV32_DECODE_MAP(0x3, MR_RV32_BCODE_MULHU);
                    RV32_DECODE_MAP(0x4, MR_RV32_BCODE_DIV);
                    RV32_DECODE_MAP(0x5, MR_RV32_BCODE_DIVU);
                    RV32_DECODE_MAP(0x6, MR_RV32_BCODE_REM);
                    RV32_DECODE_MAP(0x7, MR_RV32_BCODE_REMU);
                    RV32_DECODE_DEFAULT(MR_RV32_BCODE_UNKNOWN);
                }
                RV32_DECODE_EXIT(MR_RV32_BCODE_UNKNOWN);
            }
            switch (instr.R_type.funct3) {
                RV32_DECODE_FUNC(0x0) {
                    switch (instr.R_type.funct7) {
                        RV32_DECODE_MAP(0x00, MR_RV32_BCODE_ADD);
                        RV32_DECODE_MAP(0x20, MR_RV32_BCODE_SUB);
                        RV32_DECODE_DEFAULT(MR_RV32_BCODE_UNKNOWN);
                    }
                }
                RV32_DECODE_MAP(0x1, MR_RV32_BCODE_SLL);
                RV32_DECODE_MAP(0x2, MR_RV32_BCODE_SLT);
                RV32_DECODE_MAP(0x3, MR_RV32_BCODE_SLTU);
                RV32_DECODE_MAP(0x4, MR_RV32_BCODE_XOR);
                RV32_DECODE_FUNC(0x5) {
                    switch (instr.R_type.funct7) {
                        RV32_DECODE_MAP(0x00, MR_RV32_BCODE_SRL);
                        RV32_DECODE_MAP(0x20, MR_RV32_BCODE_SRA);
                        RV32_DECODE_DEFAULT(MR_RV32_BCODE_UNKNOWN);
                    }
                }
                RV32_DECODE_MAP(0x6, MR_RV32_BCODE_OR);
                RV32_DECODE_MAP(0x7, MR_RV32_BCODE_AND);
                RV32_DECODE_DEFAULT(MR_RV32_BCODE_UNKNOWN);
            }
            RV32_DECODE_EXIT(MR_RV32_BCODE_UNKNOWN);
        }
        case 0b0010011: {
            switch (instr.I_type.funct3) {
                RV32_DECODE_MAP(0x0, MR_RV32_BCODE_ADDI);
                RV32_DECODE_MAP(0x1, MR_RV32_BCODE_SLLI);
                RV32_DECODE_MAP(0x2, MR_RV32_BCODE_SLTI);
                RV32_DECODE_MAP(0x3, MR_RV32_BCODE_SLTIU);
                RV32_DECODE_MAP(0x4, MR_RV32_BCODE_XORI);
                RV32_DECODE_FUNC(0x5) {
                    switch (instr.I_type.imm >> 5) {
                        RV32_DECODE_MAP(0x00, MR_RV32_BCODE_SRLI);
                        RV32_DECODE_MAP(0x20, MR_RV32_BCODE_SRAI);
                        RV32_DECODE_DEFAULT(MR_RV32_BCODE_UNKNOWN);
                    }
                }
                RV32_DECODE_MAP(0x6, MR_RV32_BCODE_ORI);
                RV32_DECODE_MAP(0x7, MR_RV32_BCODE_ANDI);
                RV32_DECODE_DEFAULT(MR_RV32_BCODE_UNKNOWN);
            }
            RV32_DECODE_EXIT(MR_RV32_BCODE_UNKNOWN);
        }
        case 0b0000011: {
            switch (instr.I_type.funct3) {
                RV32_DECODE_MAP(0x0, MR_RV32_BCODE_LB);
                RV32_DECODE_MAP(0x1, MR_RV32_BCODE_LH);
                RV32_DECODE_MAP(0x2, MR_RV32_BCODE_LW);
                RV32_DECODE_MAP(0x4, MR_RV32_BCODE_LBU);
                RV32_DECODE_MAP(0x5, MR_RV32_BCODE_LHU);
                RV32_DECODE_DEFAULT(MR_RV32_BCODE_UNKNOWN);
            }
            RV32_DECODE_EXIT(MR_RV32_BCODE_UNKNOWN);
        }
        case 0b0100011: {
            switch (instr.S_type.funct3) {
                RV32_DECODE_MAP(0x0, MR_RV32_BCODE_SB);
                RV32_DECODE_MAP(0x1, MR_RV32_BCODE_SH);
                RV32_DECODE_MAP(0x2, MR_RV32_BCODE_SW);
                RV32_DECODE_DEFAULT(MR_RV32_BCODE_UNKNOWN);
            }
            RV32_DECODE_EXIT(MR_RV32_BCODE_UNKNOWN);
        }
        case 0b1100011: {
            switch (instr.B_type.funct3) {
                RV32_DECODE_MAP(0x0, MR_RV32_BCODE_BEQ);
                RV32_DECODE_MAP(0x1, MR_RV32_BCODE_BNE);
                RV32_DECODE_MAP(0x4, MR_RV32_BCODE_BLT);
                RV32_DECODE_MAP(0x5, MR_RV32_BCODE_BGE);
                RV32_DECODE_MAP(0x6, MR_RV32_BCODE_BLTU);
                RV32_DECODE_MAP(0x7, MR_RV32_BCODE_BGEU);
                RV32_DECODE_DEFAULT(MR_RV32_BCODE_UNKNOWN);
            }
            RV32_DECODE_EXIT(MR_RV32_BCODE_UNKNOWN);
        }
        RV32_DECODE_MAP(0b1101111, MR_RV32_BCODE_JAL);
        RV32_DECODE_MAP(0b1100111, MR_RV32_BCODE_JALR);
        RV32_DECODE_MAP(0b0110111, MR_RV32_BCODE_LUI);
        RV32_DECODE_MAP(0b0010111, MR_RV32_BCODE_AUIPC);
        case 0b1110011: {
            switch (instr.I_type.funct3) {
                RV32_DECODE_FUNC(0x0) {
                    switch (instr.I_type.imm) {
                        RV32_DECODE_MAP(0x0, MR_RV32_BCODE_ECALL);
                        RV32_DECODE_MAP(0x1, MR_RV32_BCODE_EBREAK);
                        RV32_DECODE_DEFAULT(MR_RV32_BCODE_UNKNOWN);
                    }
                }
                RV32_DECODE_MAP(0x1, MR_RV32_BCODE_CSRRW);
                RV32_DECODE_MAP(0x2, MR_RV32_BCODE_CSRRS);
                RV32_DECODE_MAP(0x3, MR_RV32_BCODE_CSRRC);
                RV32_DECODE_MAP(0x5, MR_RV32_BCODE_CSRRWI);
                RV32_DECODE_MAP(0x6, MR_RV32_BCODE_CSRRSI);
                RV32_DECODE_MAP(0x7, MR_RV32_BCODE_CSRRCI);
                RV32_DECODE_DEFAULT(MR_RV32_BCODE_UNKNOWN);
            }
            RV32_DECODE_EXIT(MR_RV32_BCODE_UNKNOWN);
        }
        RV32_DECODE_MAP(0b0001111, MR_RV32_BCODE_FENCE);
        RV32_DECODE_DEFAULT(MR_RV32_BCODE_UNKNOWN);
    }
}

MR_INLINE int rv32_instr_execute(mr_rv32_core_t *core, mr_uint32_t bcode,
                                 mr_rv32_instr_t instr) {
#if defined(__GNUC__)
    static const void *bcode_label[] = {
        [MR_RV32_BCODE_UNKNOWN] = &&bcode_unknown,
        [MR_RV32_BCODE_ADD] = &&bcode_add,
        [MR_RV32_BCODE_SUB] = &&bcode_sub,
        [MR_RV32_BCODE_XOR] = &&bcode_xor,
        [MR_RV32_BCODE_OR] = &&bcode_or,
        [MR_RV32_BCODE_AND] = &&bcode_and,
        [MR_RV32_BCODE_SLL] = &&bcode_sll,
        [MR_RV32_BCODE_SRL] = &&bcode_srl,
        [MR_RV32_BCODE_SRA] = &&bcode_sra,
        [MR_RV32_BCODE_SLT] = &&bcode_slt,
        [MR_RV32_BCODE_SLTU] = &&bcode_sltu,
        [MR_RV32_BCODE_ADDI] = &&bcode_addi,
        [MR_RV32_BCODE_XORI] = &&bcode_xori,
        [MR_RV32_BCODE_ORI] = &&bcode_ori,
        [MR_RV32_BCODE_ANDI] = &&bcode_andi,
        [MR_RV32_BCODE_SLLI] = &&bcode_slli,
        [MR_RV32_BCODE_SRLI] = &&bcode_srli,
        [MR_RV32_BCODE_SRAI] = &&bcode_srai,
        [MR_RV32_BCODE_SLTI] = &&bcode_slti,
        [MR_RV32_BCODE_SLTIU] = &&bcode_sltiu,
        [MR_RV32_BCODE_LB] = &&bcode_lb,
        [MR_RV32_BCODE_LH] = &&bcode_lh,
        [MR_RV32_BCODE_LW] = &&bcode_lw,
        [MR_RV32_BCODE_LBU] = &&bcode_lbu,
        [MR_RV32_BCODE_LHU] = &&bcode_lhu,
        [MR_RV32_BCODE_SB] = &&bcode_sb,
        [MR_RV32_BCODE_SH] = &&bcode_sh,
        [MR_RV32_BCODE_SW] = &&bcode_sw,
        [MR_RV32_BCODE_BEQ] = &&bcode_beq,
        [MR_RV32_BCODE_BNE] = &&bcode_bne,
        [MR_RV32_BCODE_BLT] = &&bcode_blt,
        [MR_RV32_BCODE_BGE] = &&bcode_bge,
        [MR_RV32_BCODE_BLTU] = &&bcode_bltu,
        [MR_RV32_BCODE_BGEU] = &&bcode_bgeu,
        [MR_RV32_BCODE_JAL] = &&bcode_jal,
        [MR_RV32_BCODE_JALR] = &&bcode_jalr,
        [MR_RV32_BCODE_LUI] = &&bcode_lui,
        [MR_RV32_BCODE_AUIPC] = &&bcode_auipc,
        [MR_RV32_BCODE_ECALL] = &&bcode_ecall,
        [MR_RV32_BCODE_EBREAK] = &&bcode_ebreak,
        [MR_RV32_BCODE_FENCE] = &&bcode_fence,
        [MR_RV32_BCODE_MUL] = &&bcode_mul,
        [MR_RV32_BCODE_MULH] = &&bcode_mulh,
        [MR_RV32_BCODE_MULHSU] = &&bcode_mulhsu,
        [MR_RV32_BCODE_MULHU] = &&bcode_mulhu,
        [MR_RV32_BCODE_DIV] = &&bcode_div,
        [MR_RV32_BCODE_DIVU] = &&bcode_divu,
        [MR_RV32_BCODE_REM] = &&bcode_rem,
        [MR_RV32_BCODE_REMU] = &&bcode_remu,
    };
#endif /* defined(__GNUC__) */
    mr_uint32_t tmp0u4, tmp1u4;
    mr_uint64_t tmp0u8, tmp1u8;

    RV32_EXEC_TO(bcode_label, bcode) {
        RV32_EXEC_FUNC(MR_RV32_BCODE_UNKNOWN, bcode_unknown) {
            RV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_ILLEGAL);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_ADD, bcode_add) {
            RV32_CORE_REG(core, instr.R_type.rd)
                = RV32_CORE_REG(core, instr.R_type.rs1)
                + RV32_CORE_REG(core, instr.R_type.rs2);
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_SUB, bcode_sub) {
            RV32_CORE_REG(core, instr.R_type.rd)
                = RV32_CORE_REG(core, instr.R_type.rs1)
                - RV32_CORE_REG(core, instr.R_type.rs2);
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_XOR, bcode_xor) {
            RV32_CORE_REG(core, instr.R_type.rd)
                = RV32_CORE_REG(core, instr.R_type.rs1)
                ^ RV32_CORE_REG(core, instr.R_type.rs2);
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_OR, bcode_or) {
            RV32_CORE_REG(core, instr.R_type.rd)
                = RV32_CORE_REG(core, instr.R_type.rs1)
                | RV32_CORE_REG(core, instr.R_type.rs2);
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_AND, bcode_and) {
            RV32_CORE_REG(core, instr.R_type.rd)
                = RV32_CORE_REG(core, instr.R_type.rs1)
                & RV32_CORE_REG(core, instr.R_type.rs2);
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_SLL, bcode_sll) {
            RV32_CORE_REG(core, instr.R_type.rd)
                = RV32_CORE_REG(core, instr.R_type.rs1)
               << RV32_CORE_REG(core, instr.R_type.rs2);
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_SRL, bcode_srl) {
            RV32_CORE_REG(core, instr.R_type.rd)
                = RV32_CORE_REG(core, instr.R_type.rs1)
               >> RV32_CORE_REG(core, instr.R_type.rs2);
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_SRA, bcode_sra) {
            RV32_CORE_REG(core, instr.R_type.rd)
                = (mr_uint32_t)((mr_int32_t)RV32_CORE_REG(core,
                                                          instr.R_type.rs1)
                                >> RV32_CORE_REG(core, instr.R_type.rs2));
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_SLT, bcode_slt) {
            RV32_CORE_REG(core, instr.R_type.rd)
                = (mr_int32_t)RV32_CORE_REG(core, instr.R_type.rs1)
                < (mr_int32_t)RV32_CORE_REG(core, instr.R_type.rs2);
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_SLTU, bcode_sltu) {
            RV32_CORE_REG(core, instr.R_type.rd)
                = RV32_CORE_REG(core, instr.R_type.rs1)
                < RV32_CORE_REG(core, instr.R_type.rs2);
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_ADDI, bcode_addi) {
            /* tmp_u40 = imm */
            tmp0u4 = (instr.I_type.imm & 0x800)
                       ? (0xfffff000 | instr.I_type.imm)
                       : instr.I_type.imm;
            RV32_CORE_REG(core, instr.I_type.rd)
                = RV32_CORE_REG(core, instr.I_type.rs1) + tmp0u4;
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_XORI, bcode_xori) {
            /* tmp_u40 = imm */
            tmp0u4 = (instr.I_type.imm & 0x800)
                       ? (0xfffff000 | instr.I_type.imm)
                       : instr.I_type.imm;
            RV32_CORE_REG(core, instr.I_type.rd)
                = RV32_CORE_REG(core, instr.I_type.rs1) ^ tmp0u4;
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_ORI, bcode_ori) {
            /* tmp_u40 = imm */
            tmp0u4 = (instr.I_type.imm & 0x800)
                       ? (0xfffff000 | instr.I_type.imm)
                       : instr.I_type.imm;
            RV32_CORE_REG(core, instr.I_type.rd)
                = RV32_CORE_REG(core, instr.I_type.rs1) | tmp0u4;
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_ANDI, bcode_andi) {
            /* tmp_u40 = imm */
            tmp0u4 = (instr.I_type.imm & 0x800)
                       ? (0xfffff000 | instr.I_type.imm)
                       : instr.I_type.imm;
            RV32_CORE_REG(core, instr.I_type.rd)
                = RV32_CORE_REG(core, instr.I_type.rs1) & tmp0u4;
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_SLLI, bcode_slli) {
            RV32_CORE_REG(core, instr.I_type.rd)
                = RV32_CORE_REG(core, instr.I_type.rs1)
               << (instr.I_type.imm & 0x1f);
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_SRLI, bcode_srli) {
            RV32_CORE_REG(core, instr.I_type.rd)
                = RV32_CORE_REG(core, instr.I_type.rs1)
               >> (instr.I_type.imm & 0x1f);
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_SRAI, bcode_srai) {
            RV32_CORE_REG(core, instr.I_type.rd)
                = (mr_uint32_t)((mr_int32_t)RV32_CORE_REG(core,
                                                          instr.I_type.rs1)
                                >> (instr.I_type.imm & 0x1f));
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_SLTI, bcode_slti) {
            RV32_CORE_REG(core, instr.I_type.rd)
                = (mr_int32_t)RV32_CORE_REG(core, instr.I_type.rs1)
                < (mr_int32_t)instr.I_type.imm;
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_SLTIU, bcode_sltiu) {
            RV32_CORE_REG(core, instr.I_type.rd)
                = RV32_CORE_REG(core, instr.I_type.rs1) < instr.I_type.imm;
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        /* Software VM supports unaligned access transparently (no alignment
         * check),  which is different from hardware enforcement */
        RV32_EXEC_FUNC(MR_RV32_BCODE_LB, bcode_lb) {
            /* tmp_u40 = imm(img), tmp_u41 = imm(mem) */
            tmp0u4 = (instr.I_type.imm & 0x800)
                       ? (0xfffff000 | instr.I_type.imm)
                       : instr.I_type.imm;
            tmp0u4 = RV32_CORE_REG(core, instr.I_type.rs1) + tmp0u4;
            tmp1u4 = RV32_CORE_MEM_OFF(core, tmp0u4);
            tmp0u4 = RV32_CORE_IMG_OFF(core, tmp0u4);
            if (RV32_MMIO_IN_IMG(core, tmp0u4)) {
                RV32_CORE_REG(core, instr.I_type.rd)
                    = (mr_uint32_t)*RV32_CORE_IMGi1(core, tmp0u4);
            } else if (RV32_MMIO_IN_MEM(core, tmp1u4)) {
                RV32_CORE_REG(core, instr.I_type.rd)
                    = (mr_uint32_t)*RV32_CORE_MEMi1(core, tmp1u4);
            } else {
                RV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_ACCESS);
            }
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_LH, bcode_lh) {
            /* tmp_u40 = imm(img), tmp_u41 = imm(mem) */
            tmp0u4 = (instr.I_type.imm & 0x800)
                       ? (0xfffff000 | instr.I_type.imm)
                       : instr.I_type.imm;
            tmp0u4 = RV32_CORE_REG(core, instr.I_type.rs1) + tmp0u4;
            tmp1u4 = RV32_CORE_MEM_OFF(core, tmp0u4);
            tmp0u4 = RV32_CORE_IMG_OFF(core, tmp0u4);
            if (RV32_MMIO_IN_IMG(core, tmp0u4)) {
                RV32_CORE_REG(core, instr.I_type.rd)
                    = (mr_uint32_t)*RV32_CORE_IMGi2(core, tmp0u4);
            } else if (RV32_MMIO_IN_MEM(core, tmp1u4)) {
                RV32_CORE_REG(core, instr.I_type.rd)
                    = (mr_uint32_t)*RV32_CORE_MEMi2(core, tmp1u4);
            } else {
                RV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_ACCESS);
            }
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_LW, bcode_lw) {
            /* tmp_u40 = imm(img), tmp_u41 = imm(mem) */
            tmp0u4 = (instr.I_type.imm & 0x800)
                       ? (0xfffff000 | instr.I_type.imm)
                       : instr.I_type.imm;
            tmp0u4 = RV32_CORE_REG(core, instr.I_type.rs1) + tmp0u4;
            tmp1u4 = RV32_CORE_MEM_OFF(core, tmp0u4);
            tmp0u4 = RV32_CORE_IMG_OFF(core, tmp0u4);
            if (RV32_MMIO_IN_IMG(core, tmp0u4)) {
                RV32_CORE_REG(core, instr.I_type.rd)
                    = (mr_uint32_t)*RV32_CORE_IMGi4(core, tmp0u4);
            } else if (RV32_MMIO_IN_MEM(core, tmp1u4)) {
                RV32_CORE_REG(core, instr.I_type.rd)
                    = (mr_uint32_t)*RV32_CORE_MEMi4(core, tmp1u4);
            } else {
                RV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_ACCESS);
            }
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_LBU, bcode_lbu) {
            /* tmp_u40 = imm(img), tmp_u41 = imm(mem) */
            tmp0u4 = (instr.I_type.imm & 0x800)
                       ? (0xfffff000 | instr.I_type.imm)
                       : instr.I_type.imm;
            tmp0u4 = RV32_CORE_REG(core, instr.I_type.rs1) + tmp0u4;
            tmp1u4 = RV32_CORE_MEM_OFF(core, tmp0u4);
            tmp0u4 = RV32_CORE_IMG_OFF(core, tmp0u4);
            if (RV32_MMIO_IN_IMG(core, tmp0u4)) {
                RV32_CORE_REG(core, instr.I_type.rd)
                    = (mr_uint32_t)*RV32_CORE_IMGu1(core, tmp0u4);
            } else if (RV32_MMIO_IN_MEM(core, tmp1u4)) {
                RV32_CORE_REG(core, instr.I_type.rd)
                    = (mr_uint32_t)*RV32_CORE_MEMu1(core, tmp1u4);
            } else {
                RV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_ACCESS);
            }
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_LHU, bcode_lhu) {
            /* tmp_u40 = imm(img), tmp_u41 = imm(mem) */
            tmp0u4 = (instr.I_type.imm & 0x800)
                       ? (0xfffff000 | instr.I_type.imm)
                       : instr.I_type.imm;
            tmp0u4 = RV32_CORE_REG(core, instr.I_type.rs1) + tmp0u4;
            tmp1u4 = RV32_CORE_MEM_OFF(core, tmp0u4);
            tmp0u4 = RV32_CORE_IMG_OFF(core, tmp0u4);
            if (RV32_MMIO_IN_IMG(core, tmp0u4)) {
                RV32_CORE_REG(core, instr.I_type.rd)
                    = (mr_uint32_t)*RV32_CORE_IMGu2(core, tmp0u4);
            } else if (RV32_MMIO_IN_MEM(core, tmp1u4)) {
                RV32_CORE_REG(core, instr.I_type.rd)
                    = (mr_uint32_t)*RV32_CORE_MEMu2(core, tmp1u4);
            } else {
                RV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_ACCESS);
            }
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_SB, bcode_sb) {
            /* tmp_u40 = imm(img), tmp_u41 = imm(mem) */
            tmp0u4
                = (mr_uint32_t)((instr.S_type.imm2 << 5) | instr.S_type.imm1);
            tmp0u4 = (tmp0u4 & 0x800) ? (0xfffff000 | tmp0u4) : tmp0u4;
            tmp0u4 = RV32_CORE_REG(core, instr.S_type.rs1) + tmp0u4;
            tmp1u4 = RV32_CORE_MEM_OFF(core, tmp0u4);
            tmp0u4 = RV32_CORE_IMG_OFF(core, tmp0u4);
            if (RV32_MMIO_IN_IMG(core, tmp0u4)) {
                RV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_ACCESS);
            } else if (RV32_MMIO_IN_MEM(core, tmp1u4)) {
                *RV32_CORE_MEMi1(core, tmp1u4)
                    = (mr_int8_t)RV32_CORE_REG(core, instr.S_type.rs2);
            } else {
                RV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_OVERFLOW);
            }
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_SH, bcode_sh) {
            /* tmp_u40 = imm(img), tmp_u41 = imm(mem) */
            tmp0u4
                = (mr_uint32_t)((instr.S_type.imm2 << 5) | instr.S_type.imm1);
            tmp0u4 = (tmp0u4 & 0x800) ? (0xfffff000 | tmp0u4) : tmp0u4;
            tmp0u4 = RV32_CORE_REG(core, instr.S_type.rs1) + tmp0u4;
            tmp1u4 = RV32_CORE_MEM_OFF(core, tmp0u4);
            tmp0u4 = RV32_CORE_IMG_OFF(core, tmp0u4);
            if (RV32_MMIO_IN_IMG(core, tmp0u4)) {
                RV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_ACCESS);
            } else if (RV32_MMIO_IN_MEM(core, tmp1u4)) {
                *RV32_CORE_MEMi2(core, tmp1u4)
                    = (mr_int16_t)RV32_CORE_REG(core, instr.S_type.rs2);
            } else {
                RV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_OVERFLOW);
            }
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_SW, bcode_sw) {
            /* tmp_u40 = imm(img), tmp_u41 = imm(mem) */
            tmp0u4
                = (mr_uint32_t)((instr.S_type.imm2 << 5) | instr.S_type.imm1);
            tmp0u4 = (tmp0u4 & 0x800) ? (0xfffff000 | tmp0u4) : tmp0u4;
            tmp0u4 = RV32_CORE_REG(core, instr.S_type.rs1) + tmp0u4;
            tmp1u4 = RV32_CORE_MEM_OFF(core, tmp0u4);
            tmp0u4 = RV32_CORE_IMG_OFF(core, tmp0u4);
            if (RV32_MMIO_IN_IMG(core, tmp0u4)) {
                RV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_ACCESS);
            } else if (RV32_MMIO_IN_MEM(core, tmp1u4)) {
                *RV32_CORE_MEMi4(core, tmp1u4)
                    = (mr_int32_t)RV32_CORE_REG(core, instr.S_type.rs2);
            } else {
                RV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_OVERFLOW);
            }
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_BEQ, bcode_beq) {
            /* tmp_u40 = imm */
            tmp0u4 = (mr_uint32_t)((instr.B_type.imm4 << 12)
                                   | (instr.B_type.imm1 << 11)
                                   | (instr.B_type.imm3 << 5)
                                   | (instr.B_type.imm2 << 1));
            tmp0u4 = (tmp0u4 & 0x1000) ? (0xfffff000 | tmp0u4) : tmp0u4;
            if (RV32_CORE_REG(core, instr.B_type.rs1)
                == RV32_CORE_REG(core, instr.B_type.rs2)) {
                RV32_CORE_PC(core) += tmp0u4;
            }
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_BNE, bcode_bne) {
            /* tmp_u40 = imm */
            tmp0u4 = (mr_uint32_t)((instr.B_type.imm4 << 12)
                                   | (instr.B_type.imm1 << 11)
                                   | (instr.B_type.imm3 << 5)
                                   | (instr.B_type.imm2 << 1));
            tmp0u4 = (tmp0u4 & 0x1000) ? (0xfffff000 | tmp0u4) : tmp0u4;
            if (RV32_CORE_REG(core, instr.B_type.rs1)
                != RV32_CORE_REG(core, instr.B_type.rs2)) {
                RV32_CORE_PC(core) += tmp0u4;
            }
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_BLT, bcode_blt) {
            /* tmp_u40 = imm */
            tmp0u4 = (mr_uint32_t)((instr.B_type.imm4 << 12)
                                   | (instr.B_type.imm1 << 11)
                                   | (instr.B_type.imm3 << 5)
                                   | (instr.B_type.imm2 << 1));
            tmp0u4 = (tmp0u4 & 0x1000) ? (0xfffff000 | tmp0u4) : tmp0u4;
            if ((mr_int32_t)RV32_CORE_REG(core, instr.B_type.rs1)
                < (mr_int32_t)RV32_CORE_REG(core, instr.B_type.rs2)) {
                RV32_CORE_PC(core) += tmp0u4;
            }
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_BGE, bcode_bge) {
            /* tmp_u40 = imm */
            tmp0u4 = (mr_uint32_t)((instr.B_type.imm4 << 12)
                                   | (instr.B_type.imm1 << 11)
                                   | (instr.B_type.imm3 << 5)
                                   | (instr.B_type.imm2 << 1));
            tmp0u4 = (tmp0u4 & 0x1000) ? (0xfffff000 | tmp0u4) : tmp0u4;
            if ((mr_int32_t)RV32_CORE_REG(core, instr.B_type.rs1)
                >= (mr_int32_t)RV32_CORE_REG(core, instr.B_type.rs2)) {
                RV32_CORE_PC(core) += tmp0u4;
            }
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_BLTU, bcode_bltu) {
            /* tmp_u40 = imm */
            tmp0u4 = (mr_uint32_t)((instr.B_type.imm4 << 12)
                                   | (instr.B_type.imm1 << 11)
                                   | (instr.B_type.imm3 << 5)
                                   | (instr.B_type.imm2 << 1));
            tmp0u4 = (tmp0u4 & 0x1000) ? (0xfffff000 | tmp0u4) : tmp0u4;
            if (RV32_CORE_REG(core, instr.B_type.rs1)
                < RV32_CORE_REG(core, instr.B_type.rs2)) {
                RV32_CORE_PC(core) += tmp0u4;
            }
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_BGEU, bcode_bgeu) {
            /* tmp_u40 = imm */
            tmp0u4 = (mr_uint32_t)((instr.B_type.imm4 << 12)
                                   | (instr.B_type.imm1 << 11)
                                   | (instr.B_type.imm3 << 5)
                                   | (instr.B_type.imm2 << 1));
            tmp0u4 = (tmp0u4 & 0x1000) ? (0xfffff000 | tmp0u4) : tmp0u4;
            if (RV32_CORE_REG(core, instr.B_type.rs1)
                >= RV32_CORE_REG(core, instr.B_type.rs2)) {
                RV32_CORE_PC(core) += tmp0u4;
            }
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_JAL, bcode_jal) {
            /* tmp_u40 = imm */
            tmp0u4 = (mr_uint32_t)((instr.J_type.imm4 << 20)
                                   | (instr.J_type.imm2 << 11)
                                   | (instr.J_type.imm1 << 12)
                                   | (instr.J_type.imm3 << 1));
            tmp0u4 = (tmp0u4 & 0x100000) ? (0xffe00000 | tmp0u4) : tmp0u4;
            RV32_CORE_REG(core, instr.J_type.rd) = RV32_CORE_PC(core) + 4;
            RV32_CORE_PC(core) += tmp0u4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_JALR, bcode_jalr) {
            /* tmp_u40 = imm */
            tmp0u4 = (instr.I_type.imm & 0x800)
                       ? (0xfffff000 | instr.I_type.imm)
                       : instr.I_type.imm;
            RV32_CORE_REG(core, instr.I_type.rd) = RV32_CORE_PC(core) + 4;
            RV32_CORE_PC(core)
                = (RV32_CORE_REG(core, instr.I_type.rs1) + tmp0u4) & ~3u;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_LUI, bcode_lui) {
            RV32_CORE_REG(core, instr.U_type.rd)
                = (mr_uint32_t)(instr.U_type.imm << 12);
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_AUIPC, bcode_auipc) {
            RV32_CORE_REG(core, instr.U_type.rd)
                = RV32_CORE_PC(core) + (mr_uint32_t)(instr.U_type.imm << 12);
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_ECALL, bcode_ecall) {
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(-2);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_EBREAK, bcode_ebreak) {
            RV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_BREAK);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_FENCE, bcode_fence) {
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_MUL, bcode_mul) {
            RV32_CORE_REG(core, instr.R_type.rd)
                = RV32_CORE_REG(core, instr.R_type.rs1)
                * RV32_CORE_REG(core, instr.R_type.rs2);
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_MULH, bcode_mulh) {
            /* tmp_u80 = rs1, tmp_u81 = rs2 */
            tmp0u8 = (mr_uint64_t)(mr_int32_t)RV32_CORE_REG(core,
                                                            instr.R_type.rs1);
            tmp1u8 = (mr_uint64_t)(mr_int32_t)RV32_CORE_REG(core,
                                                            instr.R_type.rs2);
            RV32_CORE_REG(core, instr.R_type.rd)
                = (mr_uint32_t)((((mr_int64_t)tmp0u8) * ((mr_int64_t)tmp1u8))
                                >> 32);
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_MULHSU, bcode_mulhsu) {
            /* tmp_u80 = rs1, tmp_u81 = rs2 */
            tmp0u8 = (mr_uint64_t)(mr_int32_t)RV32_CORE_REG(core,
                                                            instr.R_type.rs1);
            tmp1u8 = (mr_uint32_t)RV32_CORE_REG(core, instr.R_type.rs2);
            RV32_CORE_REG(core, instr.R_type.rd)
                = (mr_uint32_t)((((mr_int64_t)tmp0u8) * ((mr_int64_t)tmp1u8))
                                >> 32);
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_MULHU, bcode_mulhu) {
            /* tmp_u80 = rs1, tmp_u41 = rs2 */
            tmp0u8 = (mr_uint64_t)RV32_CORE_REG(core, instr.R_type.rs1);
            tmp1u4 = RV32_CORE_REG(core, instr.R_type.rs2);
            RV32_CORE_REG(core, instr.R_type.rd)
                = (mr_uint32_t)((tmp0u8 * (mr_uint64_t)tmp1u4) >> 32);
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_DIV, bcode_div) {
            /* tmp_u40 = rs1, tmp_u41 = rs2 */
            tmp0u4 = RV32_CORE_REG(core, instr.R_type.rs1);
            tmp1u4 = RV32_CORE_REG(core, instr.R_type.rs2);
            if ((tmp0u4 == 0x80000000) && (((mr_int32_t)tmp1u4) == (-1))) {
                RV32_CORE_REG(core, instr.R_type.rd) = 0x80000000;
            } else if (tmp1u4 != 0) {
                RV32_CORE_REG(core, instr.R_type.rd)
                    = (mr_uint32_t)((mr_int32_t)tmp0u4 / (mr_int32_t)tmp1u4);
            } else {
                RV32_CORE_REG(core, instr.R_type.rd) = 0xFFFFFFFF;
            }
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_DIVU, bcode_divu) {
            /* tmp_u40 = rs1, tmp_u41 = rs2 */
            tmp0u4 = RV32_CORE_REG(core, instr.R_type.rs1);
            tmp1u4 = RV32_CORE_REG(core, instr.R_type.rs2);
            if (tmp1u4 != 0) {
                RV32_CORE_REG(core, instr.R_type.rd) = tmp0u4 / tmp1u4;
            } else {
                RV32_CORE_REG(core, instr.R_type.rd) = 0xFFFFFFFF;
            }
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_REM, bcode_rem) {
            /* tmp_u40 = rs1, tmp_u41 = rs2 */
            tmp0u4 = RV32_CORE_REG(core, instr.R_type.rs1);
            tmp1u4 = RV32_CORE_REG(core, instr.R_type.rs2);
            if ((tmp0u4 == 0x80000000) && (((mr_int32_t)tmp1u4) == (-1))) {
                RV32_CORE_REG(core, instr.R_type.rd) = 0;
            } else if (tmp1u4 != 0) {
                RV32_CORE_REG(core, instr.R_type.rd) = tmp0u4 % tmp1u4;
            } else {
                RV32_CORE_REG(core, instr.R_type.rd) = tmp0u4;
            }
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_FUNC(MR_RV32_BCODE_REMU, bcode_remu) {
            /* tmp_u40 = rs1, tmp_u41 = rs2 */
            tmp0u4 = RV32_CORE_REG(core, instr.R_type.rs1);
            tmp1u4 = RV32_CORE_REG(core, instr.R_type.rs2);
            if (tmp1u4 != 0) {
                RV32_CORE_REG(core, instr.R_type.rd) = tmp0u4 % tmp1u4;
            } else {
                RV32_CORE_REG(core, instr.R_type.rd) = tmp0u4;
            }
            RV32_CORE_PC(core) += 4;
            RV32_EXEC_EXIT(1);
        }
        RV32_EXEC_DEFAULT(-MR_VMC_EXCEPT_NR_ILLEGAL);
    }
}

static mr_vmc_return_t rv32_vmc_exec(mr_vmc_t *vmc, mr_vmc_env_t *env,
                                     mr_vmc_event_t *event,
                                     mr_uint32_t instr_step) {
    mr_uint32_t i, bcode, off;
    mr_rv32_instr_t instr;
    mr_rv32_core_t *core;
    int trap;

    MR_IGNORE(vmc);

    /* Get RV32 core */
    core = (mr_rv32_core_t *)env->memory;

    /* Decode and execute instruction */
    for (i = 0; i < instr_step; i++) {
        /* Fetch instruction */
        off = RV32_CORE_IMG_OFF(core, RV32_CORE_PC(core));
        if (RV32_MMIO_IN_IMG(core, off)) {
            instr.raw = *RV32_CORE_IMGu4(core, off);
        } else {
            /* Exception */
            event->except.nr = MR_VMC_EXCEPT_NR_ACCESS;
            return MR_VMC_EXCEPT;
        }

        /* Decode instruction to byte-code */
        bcode = rv32_instr_decode(instr);

        /* Execute instruction */
        trap = rv32_instr_execute(core, bcode, instr);
        if (trap <= 0) {
            /* Exception */
            event->except.nr = (mr_uint32_t)(-trap);
            return MR_VMC_EXCEPT;
        }
    }

    /* Record the number of steps executed */
    event->none.exec_step = i;
    return MR_VMC_NONE;
}

static mr_err_t rv32_vmc_debug(mr_vmc_t *vmc, mr_vmc_env_t *env,
                               mr_uint32_t **reg_addr, mr_uint32_t *reg_size,
                               mr_uint32_t **memory_addr,
                               mr_uint32_t *memory_size) {
    mr_rv32_core_t *core;

    MR_IGNORE(vmc);

    /* Get RV32 core */
    core = (mr_rv32_core_t *)env->memory;

    /* Get register and memory information */
    *reg_size = sizeof(core->regs) + sizeof(core->csr) + sizeof(core->pc);
    *reg_addr = core->regs;
    *memory_addr = env->memory + sizeof(mr_rv32_core_t);
    *memory_size = core->memory_size;
    return 0;
}
#endif /* defined(MR_USE_VMC_RV32) */

/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/vmc/riscv32/riscv32.h>
#if defined(MR_USE_VMC_RISCV32)
#include <mrX/libc/mr_string.h>

/* RISCV32 VMC clazzes(static & dynamic) */
static mr_err_t riscv32_vmc_load(mr_vmc_t *vmc, mr_vmc_prop_t *prop);
static mr_vmc_return_t riscv32_vmc_exec(mr_vmc_t *vmc, mr_vmc_prop_t *prop,
                                        mr_vmc_event_t *event,
                                        mr_uint32_t instr_step);
static mr_err_t riscv32_vmc_trace(struct mr_vmc *vmc, mr_vmc_prop_t *prop,
                                  void **reg_addr, mr_uint32_t *reg_size,
                                  void **memory_addr, mr_uint32_t *memory_size);
MR_VMC_CLAZZ_DEFINE(riscv32, riscv32_vmc_load, riscv32_vmc_exec,
                    riscv32_vmc_trace);

/* RISCV32 decode definition */
#define RISCV32_DECODE_MAP(_case, _val)                                        \
    do {                                                                       \
        case (_case): {                                                        \
            return (_val);                                                     \
        }                                                                      \
    } while (0)
#define RISCV32_DECODE_DEFAULT(_val)                                           \
    do {                                                                       \
        default: {                                                             \
            return (_val);                                                     \
        }                                                                      \
    } while (0)
#define RISCV32_DECODE_FUNC(_case)      case (_case):
#define RISCV32_DECODE_EXIT(_ret)       return (_ret)
/* RISCV32 execute definition */
#if defined(__GNUC__)
/* GCC computed goto has one less lookup table memory access than switch-case
 * and has better cache utilization (code vs data segment) */
#define RISCV32_EXEC_TO(_table, _bcode) goto *((_table)[(_bcode)]);
#define RISCV32_EXEC_FUNC(_bcode, _label)                                      \
    _label:
#define RISCV32_EXEC_DEFAULT(_val)
#else
#define RISCV32_EXEC_TO(_table, _bcode) switch (_bcode)
#define RISCV32_EXEC_FUNC(_bcode, _label)                                      \
    case (_bcode):
#define RISCV32_EXEC_DEFAULT(_val)                                             \
    do {                                                                       \
        default: {                                                             \
            return (_val);                                                     \
        }                                                                      \
    } while (0)
#endif /* defined(__GNUC__) */
#define RISCV32_EXEC_EXIT(_ret)         return (_ret)
/* RISCV32 core definition */
#define RISCV32_CORE_REGX(_core, _reg)  ((_core)->regs.x[(_reg)])
#define RISCV32_CORE_PC(_core)          ((_core)->regs.pc)
/* RISCV32 address may be unaligned, software VM handles it transparently */
#define RISCV32_CORE_IMGu1(_core, _off)                                        \
    ((mr_uint8_t *)((_core)->image + (_off)))
#define RISCV32_CORE_IMGu2(_core, _off)                                        \
    ((mr_uint16_t *)((_core)->image + (_off)))
#define RISCV32_CORE_IMGu4(_core, _off)                                        \
    ((mr_uint32_t *)((_core)->image + (_off)))
#define RISCV32_CORE_IMGi1(_core, _off)                                        \
    ((mr_int8_t *)((_core)->image + (_off)))
#define RISCV32_CORE_IMGi2(_core, _off)                                        \
    ((mr_int16_t *)((_core)->image + (_off)))
#define RISCV32_CORE_IMGi4(_core, _off)                                        \
    ((mr_int32_t *)((_core)->image + (_off)))
#define RISCV32_CORE_MEMu1(_core, _off)                                        \
    ((mr_uint8_t *)((_core)->memory + (_off)))
#define RISCV32_CORE_MEMu2(_core, _off)                                        \
    ((mr_uint16_t *)((_core)->memory + (_off)))
#define RISCV32_CORE_MEMu4(_core, _off)                                        \
    ((mr_uint32_t *)((_core)->memory + (_off)))
#define RISCV32_CORE_MEMi1(_core, _off)                                        \
    ((mr_int8_t *)((_core)->memory + (_off)))
#define RISCV32_CORE_MEMi2(_core, _off)                                        \
    ((mr_int16_t *)((_core)->memory + (_off)))
#define RISCV32_CORE_MEMi4(_core, _off)                                        \
    ((mr_int32_t *)((_core)->memory + (_off)))
#define RISCV32_CORE_IMG_OFF(_core, _off)                                      \
    ((_off) - (_core)->entry_addr)
#define RISCV32_CORE_MEM_OFF(_core, _off)                                      \
    ((_off) - (_core)->storage_addr)
/* RISCV32 MMIO definition */
#define RISCV32_MMIO_IN_IMG(_core, _off)                                       \
    ((_off) < ((_core)->image_size))
#define RISCV32_MMIO_IN_MEM(_core, _off)                                       \
    ((_off) < (_core)->memory_size)

static mr_err_t riscv32_vmc_load(mr_vmc_t *vmc, mr_vmc_prop_t *prop) {
    mr_riscv32_core_t *core;

    MR_IGNORE(vmc);

    /* Get RISCV32 core */
    core = (mr_riscv32_core_t *)prop->memory;

    /* Init RISCV32 core */
    mr_memset(core, 0, sizeof(mr_riscv32_core_t));
    core->memory_size
        = (mr_uint32_t)(prop->memory_size - sizeof(mr_riscv32_core_t));
    core->storage_addr = prop->storage_addr;
    core->image_size = prop->image_size;
    core->entry_addr = prop->entry_addr;
    core->image = prop->image;
    return 0;
}

MR_INLINE mr_uint32_t riscv32_instr_decode(mr_riscv32_instr_t instr) {
    switch (instr.fmt.opcode) {
        case 0b0110011: {
            if (instr.R_type.funct7 == 0x01) {
                switch (instr.R_type.funct3) {
                    RISCV32_DECODE_MAP(0x0, MR_RISCV32_BCODE_MUL);
                    RISCV32_DECODE_MAP(0x1, MR_RISCV32_BCODE_MULH);
                    RISCV32_DECODE_MAP(0x2, MR_RISCV32_BCODE_MULHSU);
                    RISCV32_DECODE_MAP(0x3, MR_RISCV32_BCODE_MULHU);
                    RISCV32_DECODE_MAP(0x4, MR_RISCV32_BCODE_DIV);
                    RISCV32_DECODE_MAP(0x5, MR_RISCV32_BCODE_DIVU);
                    RISCV32_DECODE_MAP(0x6, MR_RISCV32_BCODE_REM);
                    RISCV32_DECODE_MAP(0x7, MR_RISCV32_BCODE_REMU);
                    RISCV32_DECODE_DEFAULT(MR_RISCV32_BCODE_UNKNOWN);
                }
                RISCV32_DECODE_EXIT(MR_RISCV32_BCODE_UNKNOWN);
            }
            switch (instr.R_type.funct3) {
                RISCV32_DECODE_FUNC(0x0) {
                    switch (instr.R_type.funct7) {
                        RISCV32_DECODE_MAP(0x00, MR_RISCV32_BCODE_ADD);
                        RISCV32_DECODE_MAP(0x20, MR_RISCV32_BCODE_SUB);
                        RISCV32_DECODE_DEFAULT(MR_RISCV32_BCODE_UNKNOWN);
                    }
                }
                RISCV32_DECODE_MAP(0x1, MR_RISCV32_BCODE_SLL);
                RISCV32_DECODE_MAP(0x2, MR_RISCV32_BCODE_SLT);
                RISCV32_DECODE_MAP(0x3, MR_RISCV32_BCODE_SLTU);
                RISCV32_DECODE_MAP(0x4, MR_RISCV32_BCODE_XOR);
                RISCV32_DECODE_FUNC(0x5) {
                    switch (instr.R_type.funct7) {
                        RISCV32_DECODE_MAP(0x00, MR_RISCV32_BCODE_SRL);
                        RISCV32_DECODE_MAP(0x20, MR_RISCV32_BCODE_SRA);
                        RISCV32_DECODE_DEFAULT(MR_RISCV32_BCODE_UNKNOWN);
                    }
                }
                RISCV32_DECODE_MAP(0x6, MR_RISCV32_BCODE_OR);
                RISCV32_DECODE_MAP(0x7, MR_RISCV32_BCODE_AND);
                RISCV32_DECODE_DEFAULT(MR_RISCV32_BCODE_UNKNOWN);
            }
            RISCV32_DECODE_EXIT(MR_RISCV32_BCODE_UNKNOWN);
        }
        case 0b0010011: {
            switch (instr.I_type.funct3) {
                RISCV32_DECODE_MAP(0x0, MR_RISCV32_BCODE_ADDI);
                RISCV32_DECODE_MAP(0x1, MR_RISCV32_BCODE_SLLI);
                RISCV32_DECODE_MAP(0x2, MR_RISCV32_BCODE_SLTI);
                RISCV32_DECODE_MAP(0x3, MR_RISCV32_BCODE_SLTIU);
                RISCV32_DECODE_MAP(0x4, MR_RISCV32_BCODE_XORI);
                RISCV32_DECODE_FUNC(0x5) {
                    switch (instr.I_type.imm >> 5) {
                        RISCV32_DECODE_MAP(0x00, MR_RISCV32_BCODE_SRLI);
                        RISCV32_DECODE_MAP(0x20, MR_RISCV32_BCODE_SRAI);
                        RISCV32_DECODE_DEFAULT(MR_RISCV32_BCODE_UNKNOWN);
                    }
                }
                RISCV32_DECODE_MAP(0x6, MR_RISCV32_BCODE_ORI);
                RISCV32_DECODE_MAP(0x7, MR_RISCV32_BCODE_ANDI);
                RISCV32_DECODE_DEFAULT(MR_RISCV32_BCODE_UNKNOWN);
            }
            RISCV32_DECODE_EXIT(MR_RISCV32_BCODE_UNKNOWN);
        }
        case 0b0000011: {
            switch (instr.I_type.funct3) {
                RISCV32_DECODE_MAP(0x0, MR_RISCV32_BCODE_LB);
                RISCV32_DECODE_MAP(0x1, MR_RISCV32_BCODE_LH);
                RISCV32_DECODE_MAP(0x2, MR_RISCV32_BCODE_LW);
                RISCV32_DECODE_MAP(0x4, MR_RISCV32_BCODE_LBU);
                RISCV32_DECODE_MAP(0x5, MR_RISCV32_BCODE_LHU);
                RISCV32_DECODE_DEFAULT(MR_RISCV32_BCODE_UNKNOWN);
            }
            RISCV32_DECODE_EXIT(MR_RISCV32_BCODE_UNKNOWN);
        }
        case 0b0100011: {
            switch (instr.S_type.funct3) {
                RISCV32_DECODE_MAP(0x0, MR_RISCV32_BCODE_SB);
                RISCV32_DECODE_MAP(0x1, MR_RISCV32_BCODE_SH);
                RISCV32_DECODE_MAP(0x2, MR_RISCV32_BCODE_SW);
                RISCV32_DECODE_DEFAULT(MR_RISCV32_BCODE_UNKNOWN);
            }
            RISCV32_DECODE_EXIT(MR_RISCV32_BCODE_UNKNOWN);
        }
        case 0b1100011: {
            switch (instr.B_type.funct3) {
                RISCV32_DECODE_MAP(0x0, MR_RISCV32_BCODE_BEQ);
                RISCV32_DECODE_MAP(0x1, MR_RISCV32_BCODE_BNE);
                RISCV32_DECODE_MAP(0x4, MR_RISCV32_BCODE_BLT);
                RISCV32_DECODE_MAP(0x5, MR_RISCV32_BCODE_BGE);
                RISCV32_DECODE_MAP(0x6, MR_RISCV32_BCODE_BLTU);
                RISCV32_DECODE_MAP(0x7, MR_RISCV32_BCODE_BGEU);
                RISCV32_DECODE_DEFAULT(MR_RISCV32_BCODE_UNKNOWN);
            }
            RISCV32_DECODE_EXIT(MR_RISCV32_BCODE_UNKNOWN);
        }
        RISCV32_DECODE_MAP(0b1101111, MR_RISCV32_BCODE_JAL);
        RISCV32_DECODE_MAP(0b1100111, MR_RISCV32_BCODE_JALR);
        RISCV32_DECODE_MAP(0b0110111, MR_RISCV32_BCODE_LUI);
        RISCV32_DECODE_MAP(0b0010111, MR_RISCV32_BCODE_AUIPC);
        case 0b1110011: {
            switch (instr.I_type.funct3) {
                RISCV32_DECODE_FUNC(0x0) {
                    switch (instr.I_type.imm) {
                        RISCV32_DECODE_MAP(0x0, MR_RISCV32_BCODE_ECALL);
                        RISCV32_DECODE_MAP(0x1, MR_RISCV32_BCODE_EBREAK);
                        RISCV32_DECODE_DEFAULT(MR_RISCV32_BCODE_UNKNOWN);
                    }
                }
                RISCV32_DECODE_MAP(0x1, MR_RISCV32_BCODE_CSRRW);
                RISCV32_DECODE_MAP(0x2, MR_RISCV32_BCODE_CSRRS);
                RISCV32_DECODE_MAP(0x3, MR_RISCV32_BCODE_CSRRC);
                RISCV32_DECODE_MAP(0x5, MR_RISCV32_BCODE_CSRRWI);
                RISCV32_DECODE_MAP(0x6, MR_RISCV32_BCODE_CSRRSI);
                RISCV32_DECODE_MAP(0x7, MR_RISCV32_BCODE_CSRRCI);
                RISCV32_DECODE_DEFAULT(MR_RISCV32_BCODE_UNKNOWN);
            }
            RISCV32_DECODE_EXIT(MR_RISCV32_BCODE_UNKNOWN);
        }
        RISCV32_DECODE_MAP(0b0001111, MR_RISCV32_BCODE_FENCE);
        RISCV32_DECODE_DEFAULT(MR_RISCV32_BCODE_UNKNOWN);
    }
}

MR_INLINE int riscv32_instr_exec(mr_riscv32_core_t *core, mr_uint32_t bcode,
                                 mr_riscv32_instr_t instr) {
#if defined(__GNUC__)
    static const void *bcode_label[] = {
        [MR_RISCV32_BCODE_UNKNOWN] = &&bcode_unknown,
        [MR_RISCV32_BCODE_ADD] = &&bcode_add,
        [MR_RISCV32_BCODE_SUB] = &&bcode_sub,
        [MR_RISCV32_BCODE_XOR] = &&bcode_xor,
        [MR_RISCV32_BCODE_OR] = &&bcode_or,
        [MR_RISCV32_BCODE_AND] = &&bcode_and,
        [MR_RISCV32_BCODE_SLL] = &&bcode_sll,
        [MR_RISCV32_BCODE_SRL] = &&bcode_srl,
        [MR_RISCV32_BCODE_SRA] = &&bcode_sra,
        [MR_RISCV32_BCODE_SLT] = &&bcode_slt,
        [MR_RISCV32_BCODE_SLTU] = &&bcode_sltu,
        [MR_RISCV32_BCODE_ADDI] = &&bcode_addi,
        [MR_RISCV32_BCODE_XORI] = &&bcode_xori,
        [MR_RISCV32_BCODE_ORI] = &&bcode_ori,
        [MR_RISCV32_BCODE_ANDI] = &&bcode_andi,
        [MR_RISCV32_BCODE_SLLI] = &&bcode_slli,
        [MR_RISCV32_BCODE_SRLI] = &&bcode_srli,
        [MR_RISCV32_BCODE_SRAI] = &&bcode_srai,
        [MR_RISCV32_BCODE_SLTI] = &&bcode_slti,
        [MR_RISCV32_BCODE_SLTIU] = &&bcode_sltiu,
        [MR_RISCV32_BCODE_LB] = &&bcode_lb,
        [MR_RISCV32_BCODE_LH] = &&bcode_lh,
        [MR_RISCV32_BCODE_LW] = &&bcode_lw,
        [MR_RISCV32_BCODE_LBU] = &&bcode_lbu,
        [MR_RISCV32_BCODE_LHU] = &&bcode_lhu,
        [MR_RISCV32_BCODE_SB] = &&bcode_sb,
        [MR_RISCV32_BCODE_SH] = &&bcode_sh,
        [MR_RISCV32_BCODE_SW] = &&bcode_sw,
        [MR_RISCV32_BCODE_BEQ] = &&bcode_beq,
        [MR_RISCV32_BCODE_BNE] = &&bcode_bne,
        [MR_RISCV32_BCODE_BLT] = &&bcode_blt,
        [MR_RISCV32_BCODE_BGE] = &&bcode_bge,
        [MR_RISCV32_BCODE_BLTU] = &&bcode_bltu,
        [MR_RISCV32_BCODE_BGEU] = &&bcode_bgeu,
        [MR_RISCV32_BCODE_JAL] = &&bcode_jal,
        [MR_RISCV32_BCODE_JALR] = &&bcode_jalr,
        [MR_RISCV32_BCODE_LUI] = &&bcode_lui,
        [MR_RISCV32_BCODE_AUIPC] = &&bcode_auipc,
        [MR_RISCV32_BCODE_ECALL] = &&bcode_ecall,
        [MR_RISCV32_BCODE_EBREAK] = &&bcode_ebreak,
        [MR_RISCV32_BCODE_FENCE] = &&bcode_fence,
        [MR_RISCV32_BCODE_MUL] = &&bcode_mul,
        [MR_RISCV32_BCODE_MULH] = &&bcode_mulh,
        [MR_RISCV32_BCODE_MULHSU] = &&bcode_mulhsu,
        [MR_RISCV32_BCODE_MULHU] = &&bcode_mulhu,
        [MR_RISCV32_BCODE_DIV] = &&bcode_div,
        [MR_RISCV32_BCODE_DIVU] = &&bcode_divu,
        [MR_RISCV32_BCODE_REM] = &&bcode_rem,
        [MR_RISCV32_BCODE_REMU] = &&bcode_remu,
    };
#endif /* defined(__GNUC__) */
    mr_uint32_t tmp0u4, tmp1u4;
    mr_uint64_t tmp0u8, tmp1u8;

    RISCV32_EXEC_TO(bcode_label, bcode) {
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_UNKNOWN, bcode_unknown) {
            RISCV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_ILLEGAL);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_ADD, bcode_add) {
            RISCV32_CORE_REGX(core, instr.R_type.rd)
                = RISCV32_CORE_REGX(core, instr.R_type.rs1)
                + RISCV32_CORE_REGX(core, instr.R_type.rs2);
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_SUB, bcode_sub) {
            RISCV32_CORE_REGX(core, instr.R_type.rd)
                = RISCV32_CORE_REGX(core, instr.R_type.rs1)
                - RISCV32_CORE_REGX(core, instr.R_type.rs2);
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_XOR, bcode_xor) {
            RISCV32_CORE_REGX(core, instr.R_type.rd)
                = RISCV32_CORE_REGX(core, instr.R_type.rs1)
                ^ RISCV32_CORE_REGX(core, instr.R_type.rs2);
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_OR, bcode_or) {
            RISCV32_CORE_REGX(core, instr.R_type.rd)
                = RISCV32_CORE_REGX(core, instr.R_type.rs1)
                | RISCV32_CORE_REGX(core, instr.R_type.rs2);
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_AND, bcode_and) {
            RISCV32_CORE_REGX(core, instr.R_type.rd)
                = RISCV32_CORE_REGX(core, instr.R_type.rs1)
                & RISCV32_CORE_REGX(core, instr.R_type.rs2);
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_SLL, bcode_sll) {
            RISCV32_CORE_REGX(core, instr.R_type.rd)
                = RISCV32_CORE_REGX(core, instr.R_type.rs1)
               << RISCV32_CORE_REGX(core, instr.R_type.rs2);
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_SRL, bcode_srl) {
            RISCV32_CORE_REGX(core, instr.R_type.rd)
                = RISCV32_CORE_REGX(core, instr.R_type.rs1)
               >> RISCV32_CORE_REGX(core, instr.R_type.rs2);
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_SRA, bcode_sra) {
            RISCV32_CORE_REGX(core, instr.R_type.rd)
                = (mr_uint32_t)((mr_int32_t)RISCV32_CORE_REGX(core,
                                                              instr.R_type.rs1)
                                >> RISCV32_CORE_REGX(core, instr.R_type.rs2));
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_SLT, bcode_slt) {
            RISCV32_CORE_REGX(core, instr.R_type.rd)
                = (mr_int32_t)RISCV32_CORE_REGX(core, instr.R_type.rs1)
                < (mr_int32_t)RISCV32_CORE_REGX(core, instr.R_type.rs2);
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_SLTU, bcode_sltu) {
            RISCV32_CORE_REGX(core, instr.R_type.rd)
                = RISCV32_CORE_REGX(core, instr.R_type.rs1)
                < RISCV32_CORE_REGX(core, instr.R_type.rs2);
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_ADDI, bcode_addi) {
            /* tmp_u40 = imm */
            tmp0u4 = (instr.I_type.imm & 0x800)
                       ? (0xfffff000 | instr.I_type.imm)
                       : instr.I_type.imm;
            RISCV32_CORE_REGX(core, instr.I_type.rd)
                = RISCV32_CORE_REGX(core, instr.I_type.rs1) + tmp0u4;
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_XORI, bcode_xori) {
            /* tmp_u40 = imm */
            tmp0u4 = (instr.I_type.imm & 0x800)
                       ? (0xfffff000 | instr.I_type.imm)
                       : instr.I_type.imm;
            RISCV32_CORE_REGX(core, instr.I_type.rd)
                = RISCV32_CORE_REGX(core, instr.I_type.rs1) ^ tmp0u4;
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_ORI, bcode_ori) {
            /* tmp_u40 = imm */
            tmp0u4 = (instr.I_type.imm & 0x800)
                       ? (0xfffff000 | instr.I_type.imm)
                       : instr.I_type.imm;
            RISCV32_CORE_REGX(core, instr.I_type.rd)
                = RISCV32_CORE_REGX(core, instr.I_type.rs1) | tmp0u4;
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_ANDI, bcode_andi) {
            /* tmp_u40 = imm */
            tmp0u4 = (instr.I_type.imm & 0x800)
                       ? (0xfffff000 | instr.I_type.imm)
                       : instr.I_type.imm;
            RISCV32_CORE_REGX(core, instr.I_type.rd)
                = RISCV32_CORE_REGX(core, instr.I_type.rs1) & tmp0u4;
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_SLLI, bcode_slli) {
            RISCV32_CORE_REGX(core, instr.I_type.rd)
                = RISCV32_CORE_REGX(core, instr.I_type.rs1)
               << (instr.I_type.imm & 0x1f);
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_SRLI, bcode_srli) {
            RISCV32_CORE_REGX(core, instr.I_type.rd)
                = RISCV32_CORE_REGX(core, instr.I_type.rs1)
               >> (instr.I_type.imm & 0x1f);
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_SRAI, bcode_srai) {
            RISCV32_CORE_REGX(core, instr.I_type.rd)
                = (mr_uint32_t)((mr_int32_t)RISCV32_CORE_REGX(core,
                                                              instr.I_type.rs1)
                                >> (instr.I_type.imm & 0x1f));
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_SLTI, bcode_slti) {
            RISCV32_CORE_REGX(core, instr.I_type.rd)
                = (mr_int32_t)RISCV32_CORE_REGX(core, instr.I_type.rs1)
                < (mr_int32_t)instr.I_type.imm;
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_SLTIU, bcode_sltiu) {
            RISCV32_CORE_REGX(core, instr.I_type.rd)
                = RISCV32_CORE_REGX(core, instr.I_type.rs1) < instr.I_type.imm;
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        /* Software VM supports unaligned access transparently (no alignment
         * check),  which is different from hardware enforcement */
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_LB, bcode_lb) {
            /* tmp_u40 = imm(img), tmp_u41 = imm(mem) */
            tmp0u4 = (instr.I_type.imm & 0x800)
                       ? (0xfffff000 | instr.I_type.imm)
                       : instr.I_type.imm;
            tmp0u4 = RISCV32_CORE_REGX(core, instr.I_type.rs1) + tmp0u4;
            tmp1u4 = RISCV32_CORE_MEM_OFF(core, tmp0u4);
            tmp0u4 = RISCV32_CORE_IMG_OFF(core, tmp0u4);
            if (RISCV32_MMIO_IN_IMG(core, tmp0u4)) {
                RISCV32_CORE_REGX(core, instr.I_type.rd)
                    = (mr_uint32_t)*RISCV32_CORE_IMGi1(core, tmp0u4);
            } else if (RISCV32_MMIO_IN_MEM(core, tmp1u4)) {
                RISCV32_CORE_REGX(core, instr.I_type.rd)
                    = (mr_uint32_t)*RISCV32_CORE_MEMi1(core, tmp1u4);
            } else {
                RISCV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_ACCESS);
            }
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_LH, bcode_lh) {
            /* tmp_u40 = imm(img), tmp_u41 = imm(mem) */
            tmp0u4 = (instr.I_type.imm & 0x800)
                       ? (0xfffff000 | instr.I_type.imm)
                       : instr.I_type.imm;
            tmp0u4 = RISCV32_CORE_REGX(core, instr.I_type.rs1) + tmp0u4;
            tmp1u4 = RISCV32_CORE_MEM_OFF(core, tmp0u4);
            tmp0u4 = RISCV32_CORE_IMG_OFF(core, tmp0u4);
            if (RISCV32_MMIO_IN_IMG(core, tmp0u4)) {
                RISCV32_CORE_REGX(core, instr.I_type.rd)
                    = (mr_uint32_t)*RISCV32_CORE_IMGi2(core, tmp0u4);
            } else if (RISCV32_MMIO_IN_MEM(core, tmp1u4)) {
                RISCV32_CORE_REGX(core, instr.I_type.rd)
                    = (mr_uint32_t)*RISCV32_CORE_MEMi2(core, tmp1u4);
            } else {
                RISCV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_ACCESS);
            }
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_LW, bcode_lw) {
            /* tmp_u40 = imm(img), tmp_u41 = imm(mem) */
            tmp0u4 = (instr.I_type.imm & 0x800)
                       ? (0xfffff000 | instr.I_type.imm)
                       : instr.I_type.imm;
            tmp0u4 = RISCV32_CORE_REGX(core, instr.I_type.rs1) + tmp0u4;
            tmp1u4 = RISCV32_CORE_MEM_OFF(core, tmp0u4);
            tmp0u4 = RISCV32_CORE_IMG_OFF(core, tmp0u4);
            if (RISCV32_MMIO_IN_IMG(core, tmp0u4)) {
                RISCV32_CORE_REGX(core, instr.I_type.rd)
                    = (mr_uint32_t)*RISCV32_CORE_IMGi4(core, tmp0u4);
            } else if (RISCV32_MMIO_IN_MEM(core, tmp1u4)) {
                RISCV32_CORE_REGX(core, instr.I_type.rd)
                    = (mr_uint32_t)*RISCV32_CORE_MEMi4(core, tmp1u4);
            } else {
                RISCV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_ACCESS);
            }
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_LBU, bcode_lbu) {
            /* tmp_u40 = imm(img), tmp_u41 = imm(mem) */
            tmp0u4 = (instr.I_type.imm & 0x800)
                       ? (0xfffff000 | instr.I_type.imm)
                       : instr.I_type.imm;
            tmp0u4 = RISCV32_CORE_REGX(core, instr.I_type.rs1) + tmp0u4;
            tmp1u4 = RISCV32_CORE_MEM_OFF(core, tmp0u4);
            tmp0u4 = RISCV32_CORE_IMG_OFF(core, tmp0u4);
            if (RISCV32_MMIO_IN_IMG(core, tmp0u4)) {
                RISCV32_CORE_REGX(core, instr.I_type.rd)
                    = (mr_uint32_t)*RISCV32_CORE_IMGu1(core, tmp0u4);
            } else if (RISCV32_MMIO_IN_MEM(core, tmp1u4)) {
                RISCV32_CORE_REGX(core, instr.I_type.rd)
                    = (mr_uint32_t)*RISCV32_CORE_MEMu1(core, tmp1u4);
            } else {
                RISCV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_ACCESS);
            }
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_LHU, bcode_lhu) {
            /* tmp_u40 = imm(img), tmp_u41 = imm(mem) */
            tmp0u4 = (instr.I_type.imm & 0x800)
                       ? (0xfffff000 | instr.I_type.imm)
                       : instr.I_type.imm;
            tmp0u4 = RISCV32_CORE_REGX(core, instr.I_type.rs1) + tmp0u4;
            tmp1u4 = RISCV32_CORE_MEM_OFF(core, tmp0u4);
            tmp0u4 = RISCV32_CORE_IMG_OFF(core, tmp0u4);
            if (RISCV32_MMIO_IN_IMG(core, tmp0u4)) {
                RISCV32_CORE_REGX(core, instr.I_type.rd)
                    = (mr_uint32_t)*RISCV32_CORE_IMGu2(core, tmp0u4);
            } else if (RISCV32_MMIO_IN_MEM(core, tmp1u4)) {
                RISCV32_CORE_REGX(core, instr.I_type.rd)
                    = (mr_uint32_t)*RISCV32_CORE_MEMu2(core, tmp1u4);
            } else {
                RISCV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_ACCESS);
            }
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_SB, bcode_sb) {
            /* tmp_u40 = imm(img), tmp_u41 = imm(mem) */
            tmp0u4
                = (mr_uint32_t)((instr.S_type.imm2 << 5) | instr.S_type.imm1);
            tmp0u4 = (tmp0u4 & 0x800) ? (0xfffff000 | tmp0u4) : tmp0u4;
            tmp0u4 = RISCV32_CORE_REGX(core, instr.S_type.rs1) + tmp0u4;
            tmp1u4 = RISCV32_CORE_MEM_OFF(core, tmp0u4);
            tmp0u4 = RISCV32_CORE_IMG_OFF(core, tmp0u4);
            if (RISCV32_MMIO_IN_IMG(core, tmp0u4)) {
                RISCV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_ACCESS);
            } else if (RISCV32_MMIO_IN_MEM(core, tmp1u4)) {
                *RISCV32_CORE_MEMi1(core, tmp1u4)
                    = (mr_int8_t)RISCV32_CORE_REGX(core, instr.S_type.rs2);
            } else {
                RISCV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_OVERFLOW);
            }
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_SH, bcode_sh) {
            /* tmp_u40 = imm(img), tmp_u41 = imm(mem) */
            tmp0u4
                = (mr_uint32_t)((instr.S_type.imm2 << 5) | instr.S_type.imm1);
            tmp0u4 = (tmp0u4 & 0x800) ? (0xfffff000 | tmp0u4) : tmp0u4;
            tmp0u4 = RISCV32_CORE_REGX(core, instr.S_type.rs1) + tmp0u4;
            tmp1u4 = RISCV32_CORE_MEM_OFF(core, tmp0u4);
            tmp0u4 = RISCV32_CORE_IMG_OFF(core, tmp0u4);
            if (RISCV32_MMIO_IN_IMG(core, tmp0u4)) {
                RISCV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_ACCESS);
            } else if (RISCV32_MMIO_IN_MEM(core, tmp1u4)) {
                *RISCV32_CORE_MEMi2(core, tmp1u4)
                    = (mr_int16_t)RISCV32_CORE_REGX(core, instr.S_type.rs2);
            } else {
                RISCV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_OVERFLOW);
            }
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_SW, bcode_sw) {
            /* tmp_u40 = imm(img), tmp_u41 = imm(mem) */
            tmp0u4
                = (mr_uint32_t)((instr.S_type.imm2 << 5) | instr.S_type.imm1);
            tmp0u4 = (tmp0u4 & 0x800) ? (0xfffff000 | tmp0u4) : tmp0u4;
            tmp0u4 = RISCV32_CORE_REGX(core, instr.S_type.rs1) + tmp0u4;
            tmp1u4 = RISCV32_CORE_MEM_OFF(core, tmp0u4);
            tmp0u4 = RISCV32_CORE_IMG_OFF(core, tmp0u4);
            if (RISCV32_MMIO_IN_IMG(core, tmp0u4)) {
                RISCV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_ACCESS);
            } else if (RISCV32_MMIO_IN_MEM(core, tmp1u4)) {
                *RISCV32_CORE_MEMi4(core, tmp1u4)
                    = (mr_int32_t)RISCV32_CORE_REGX(core, instr.S_type.rs2);
            } else {
                RISCV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_OVERFLOW);
            }
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_BEQ, bcode_beq) {
            /* tmp_u40 = imm */
            tmp0u4 = (mr_uint32_t)((instr.B_type.imm4 << 12)
                                   | (instr.B_type.imm1 << 11)
                                   | (instr.B_type.imm3 << 5)
                                   | (instr.B_type.imm2 << 1));
            tmp0u4 = (tmp0u4 & 0x1000) ? (0xfffff000 | tmp0u4) : tmp0u4;
            if (RISCV32_CORE_REGX(core, instr.B_type.rs1)
                == RISCV32_CORE_REGX(core, instr.B_type.rs2)) {
                RISCV32_CORE_PC(core) += tmp0u4;
            } else {
                RISCV32_CORE_PC(core) += 4;
            }
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_BNE, bcode_bne) {
            /* tmp_u40 = imm */
            tmp0u4 = (mr_uint32_t)((instr.B_type.imm4 << 12)
                                   | (instr.B_type.imm1 << 11)
                                   | (instr.B_type.imm3 << 5)
                                   | (instr.B_type.imm2 << 1));
            tmp0u4 = (tmp0u4 & 0x1000) ? (0xfffff000 | tmp0u4) : tmp0u4;
            if (RISCV32_CORE_REGX(core, instr.B_type.rs1)
                != RISCV32_CORE_REGX(core, instr.B_type.rs2)) {
                RISCV32_CORE_PC(core) += tmp0u4;
            } else {
                RISCV32_CORE_PC(core) += 4;
            }
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_BLT, bcode_blt) {
            /* tmp_u40 = imm */
            tmp0u4 = (mr_uint32_t)((instr.B_type.imm4 << 12)
                                   | (instr.B_type.imm1 << 11)
                                   | (instr.B_type.imm3 << 5)
                                   | (instr.B_type.imm2 << 1));
            tmp0u4 = (tmp0u4 & 0x1000) ? (0xfffff000 | tmp0u4) : tmp0u4;
            if ((mr_int32_t)RISCV32_CORE_REGX(core, instr.B_type.rs1)
                < (mr_int32_t)RISCV32_CORE_REGX(core, instr.B_type.rs2)) {
                RISCV32_CORE_PC(core) += tmp0u4;
            } else {
                RISCV32_CORE_PC(core) += 4;
            }
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_BGE, bcode_bge) {
            /* tmp_u40 = imm */
            tmp0u4 = (mr_uint32_t)((instr.B_type.imm4 << 12)
                                   | (instr.B_type.imm1 << 11)
                                   | (instr.B_type.imm3 << 5)
                                   | (instr.B_type.imm2 << 1));
            tmp0u4 = (tmp0u4 & 0x1000) ? (0xffffe000 | tmp0u4) : tmp0u4;
            if ((mr_int32_t)RISCV32_CORE_REGX(core, instr.B_type.rs1)
                >= (mr_int32_t)RISCV32_CORE_REGX(core, instr.B_type.rs2)) {
                RISCV32_CORE_PC(core) += tmp0u4;
            } else {
                RISCV32_CORE_PC(core) += 4;
            }
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_BLTU, bcode_bltu) {
            /* tmp_u40 = imm */
            tmp0u4 = (mr_uint32_t)((instr.B_type.imm4 << 12)
                                   | (instr.B_type.imm1 << 11)
                                   | (instr.B_type.imm3 << 5)
                                   | (instr.B_type.imm2 << 1));
            tmp0u4 = (tmp0u4 & 0x1000) ? (0xffffe000 | tmp0u4) : tmp0u4;
            if (RISCV32_CORE_REGX(core, instr.B_type.rs1)
                < RISCV32_CORE_REGX(core, instr.B_type.rs2)) {
                RISCV32_CORE_PC(core) += tmp0u4;
            } else {
                RISCV32_CORE_PC(core) += 4;
            }
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_BGEU, bcode_bgeu) {
            /* tmp_u40 = imm */
            tmp0u4 = (mr_uint32_t)((instr.B_type.imm4 << 12)
                                   | (instr.B_type.imm1 << 11)
                                   | (instr.B_type.imm3 << 5)
                                   | (instr.B_type.imm2 << 1));
            tmp0u4 = (tmp0u4 & 0x1000) ? (0xffffe000 | tmp0u4) : tmp0u4;
            if (RISCV32_CORE_REGX(core, instr.B_type.rs1)
                >= RISCV32_CORE_REGX(core, instr.B_type.rs2)) {
                RISCV32_CORE_PC(core) += tmp0u4;
            } else {
                RISCV32_CORE_PC(core) += 4;
            }
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_JAL, bcode_jal) {
            /* tmp_u40 = imm */
            tmp0u4 = (mr_uint32_t)((instr.J_type.imm4 << 20)
                                   | (instr.J_type.imm2 << 11)
                                   | (instr.J_type.imm1 << 12)
                                   | (instr.J_type.imm3 << 1));
            tmp0u4 = (tmp0u4 & 0x100000) ? (0xffe00000 | tmp0u4) : tmp0u4;
            RISCV32_CORE_REGX(core, instr.J_type.rd)
                = RISCV32_CORE_PC(core) + 4;
            RISCV32_CORE_PC(core) += tmp0u4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_JALR, bcode_jalr) {
            /* tmp_u40 = imm */
            tmp0u4 = (instr.I_type.imm & 0x800)
                       ? (0xfffff000 | instr.I_type.imm)
                       : instr.I_type.imm;
            RISCV32_CORE_REGX(core, instr.I_type.rd)
                = RISCV32_CORE_PC(core) + 4;
            RISCV32_CORE_PC(core)
                = (RISCV32_CORE_REGX(core, instr.I_type.rs1) + tmp0u4) & ~3u;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_LUI, bcode_lui) {
            RISCV32_CORE_REGX(core, instr.U_type.rd)
                = (mr_uint32_t)(instr.U_type.imm << 12);
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_AUIPC, bcode_auipc) {
            RISCV32_CORE_REGX(core, instr.U_type.rd)
                = RISCV32_CORE_PC(core) + (mr_uint32_t)(instr.U_type.imm << 12);
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_ECALL, bcode_ecall) {
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(1);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_EBREAK, bcode_ebreak) {
            RISCV32_EXEC_EXIT(-MR_VMC_EXCEPT_NR_BREAK);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_FENCE, bcode_fence) {
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_MUL, bcode_mul) {
            RISCV32_CORE_REGX(core, instr.R_type.rd)
                = RISCV32_CORE_REGX(core, instr.R_type.rs1)
                * RISCV32_CORE_REGX(core, instr.R_type.rs2);
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_MULH, bcode_mulh) {
            /* tmp_u80 = rs1, tmp_u81 = rs2 */
            tmp0u8 = (mr_uint64_t)(mr_int32_t)RISCV32_CORE_REGX(
                core, instr.R_type.rs1);
            tmp1u8 = (mr_uint64_t)(mr_int32_t)RISCV32_CORE_REGX(
                core, instr.R_type.rs2);
            RISCV32_CORE_REGX(core, instr.R_type.rd)
                = (mr_uint32_t)((((mr_int64_t)tmp0u8) * ((mr_int64_t)tmp1u8))
                                >> 32);
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_MULHSU, bcode_mulhsu) {
            /* tmp_u80 = rs1, tmp_u81 = rs2 */
            tmp0u8 = (mr_uint64_t)(mr_int32_t)RISCV32_CORE_REGX(
                core, instr.R_type.rs1);
            tmp1u8 = (mr_uint32_t)RISCV32_CORE_REGX(core, instr.R_type.rs2);
            RISCV32_CORE_REGX(core, instr.R_type.rd)
                = (mr_uint32_t)((((mr_int64_t)tmp0u8) * ((mr_int64_t)tmp1u8))
                                >> 32);
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_MULHU, bcode_mulhu) {
            /* tmp_u80 = rs1, tmp_u41 = rs2 */
            tmp0u8 = (mr_uint64_t)RISCV32_CORE_REGX(core, instr.R_type.rs1);
            tmp1u4 = RISCV32_CORE_REGX(core, instr.R_type.rs2);
            RISCV32_CORE_REGX(core, instr.R_type.rd)
                = (mr_uint32_t)((tmp0u8 * (mr_uint64_t)tmp1u4) >> 32);
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_DIV, bcode_div) {
            /* tmp_u40 = rs1, tmp_u41 = rs2 */
            tmp0u4 = RISCV32_CORE_REGX(core, instr.R_type.rs1);
            tmp1u4 = RISCV32_CORE_REGX(core, instr.R_type.rs2);
            if ((tmp0u4 == 0x80000000) && (((mr_int32_t)tmp1u4) == (-1))) {
                RISCV32_CORE_REGX(core, instr.R_type.rd) = 0x80000000;
            } else if (tmp1u4 != 0) {
                RISCV32_CORE_REGX(core, instr.R_type.rd)
                    = (mr_uint32_t)((mr_int32_t)tmp0u4 / (mr_int32_t)tmp1u4);
            } else {
                RISCV32_CORE_REGX(core, instr.R_type.rd) = 0xFFFFFFFF;
            }
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_DIVU, bcode_divu) {
            /* tmp_u40 = rs1, tmp_u41 = rs2 */
            tmp0u4 = RISCV32_CORE_REGX(core, instr.R_type.rs1);
            tmp1u4 = RISCV32_CORE_REGX(core, instr.R_type.rs2);
            if (tmp1u4 != 0) {
                RISCV32_CORE_REGX(core, instr.R_type.rd) = tmp0u4 / tmp1u4;
            } else {
                RISCV32_CORE_REGX(core, instr.R_type.rd) = 0xFFFFFFFF;
            }
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_REM, bcode_rem) {
            /* tmp_u40 = rs1, tmp_u41 = rs2 */
            tmp0u4 = RISCV32_CORE_REGX(core, instr.R_type.rs1);
            tmp1u4 = RISCV32_CORE_REGX(core, instr.R_type.rs2);
            if ((tmp0u4 == 0x80000000) && (((mr_int32_t)tmp1u4) == (-1))) {
                RISCV32_CORE_REGX(core, instr.R_type.rd) = 0;
            } else if (tmp1u4 != 0) {
                RISCV32_CORE_REGX(core, instr.R_type.rd) = tmp0u4 % tmp1u4;
            } else {
                RISCV32_CORE_REGX(core, instr.R_type.rd) = tmp0u4;
            }
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_FUNC(MR_RISCV32_BCODE_REMU, bcode_remu) {
            /* tmp_u40 = rs1, tmp_u41 = rs2 */
            tmp0u4 = RISCV32_CORE_REGX(core, instr.R_type.rs1);
            tmp1u4 = RISCV32_CORE_REGX(core, instr.R_type.rs2);
            if (tmp1u4 != 0) {
                RISCV32_CORE_REGX(core, instr.R_type.rd) = tmp0u4 % tmp1u4;
            } else {
                RISCV32_CORE_REGX(core, instr.R_type.rd) = tmp0u4;
            }
            RISCV32_CORE_PC(core) += 4;
            RISCV32_EXEC_EXIT(0);
        }
        RISCV32_EXEC_DEFAULT(-MR_VMC_EXCEPT_NR_ILLEGAL);
    }
}

MR_INLINE int riscv32_bcode_exec(mr_riscv32_core_t *core, mr_uint32_t bcode,
                                 mr_riscv32_instr_t instr) {
    int trap;

    /* Execute instruction through raw handler */
    trap = riscv32_instr_exec(core, bcode, instr);

    /* Ensure reg[x0] always reads as 0 */
    RISCV32_CORE_REGX(core, 0) = 0;
    return trap;
}

static mr_vmc_return_t riscv32_vmc_exec(mr_vmc_t *vmc, mr_vmc_prop_t *prop,
                                        mr_vmc_event_t *event,
                                        mr_uint32_t instr_step) {
    mr_uint32_t i, bcode, off;
    mr_riscv32_instr_t instr;
    mr_riscv32_core_t *core;
    int trap;

    MR_IGNORE(vmc);

    /* Get RISCV32 core */
    core = (mr_riscv32_core_t *)prop->memory;

    /* Decode and execute instruction */
    for (i = 0; i < instr_step; i++) {
        /* Fetch instruction */
        off = RISCV32_CORE_IMG_OFF(core, RISCV32_CORE_PC(core));
        if (RISCV32_MMIO_IN_IMG(core, off)) {
            instr.raw = *RISCV32_CORE_IMGu4(core, off);
        } else {
            /* Exception */
            event->except.nr = MR_VMC_EXCEPT_NR_ACCESS;
            return MR_VMC_EXCEPT;
        }

        /* Decode instruction to byte-code */
        bcode = riscv32_instr_decode(instr);

        /* Execute instruction through byte-code handler */
        trap = riscv32_bcode_exec(core, bcode, instr);
        if (trap == 0) {
            continue;
        }

        /* Trap */
        if (trap > 0) {
            event->syscall.nr = (mr_uint32_t)trap;
            return MR_VMC_SYSCALL;
        } else {
            /* Exception */
            event->except.nr = (mr_uint32_t)(-trap);
            return MR_VMC_EXCEPT;
        }
    }

    /* Record the number of steps executed */
    event->none.exec_step = i;
    return MR_VMC_NONE;
}

static mr_err_t riscv32_vmc_trace(struct mr_vmc *vmc, mr_vmc_prop_t *prop,
                                  void **reg_addr, mr_uint32_t *reg_size,
                                  void **memory_addr,
                                  mr_uint32_t *memory_size) {
    mr_riscv32_core_t *core;

    MR_IGNORE(vmc);

    /* Get RISCV32 core */
    core = (mr_riscv32_core_t *)prop->memory;

    /* Get register and memory information */
    *reg_size = sizeof(mr_riscv32_regs_t);
    *reg_addr = &core->regs;
    *memory_addr = core->memory;
    *memory_size = core->memory_size;
    return 0;
}
#endif /* defined(MR_USE_VMC_RISCV32) */

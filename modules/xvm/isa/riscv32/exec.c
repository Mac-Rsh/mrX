/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/modules/xvm/isa/riscv32/exec.h>
#if defined(MR_USE_MODULE_XVM_RISCV32)
#include <mrX/modules/xvm/isa/riscv32/csr.h>
#include <mrX/modules/xvm/isa/riscv32/rvc.h>
#include <mrX/modules/xvm/isa/riscv32/trap.h>
#if defined(MR_USE_MODULE_XVM_DEBUG)
#include <mrX/modules/xvm/mr_xvm_debug.h>
#endif /* defined(MR_USE_MODULE_XVM_DEBUG) */

MR_INLINE int instr_fetch(core_t *core, mr_uint32_t addr, mr_uint32_t *val,
                          mr_uint32_t *instr_len) {
    mr_uint32_t bin_addr;
#if defined(MR_USE_MODULE_XVM_RISCV32_C)
    mr_uint32_t word;
    int ret;
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_C) */

    /* Check instruction address alignment */
    if (!CORE_ADDR_IS_ALIGN(addr, CORE_INSTR_ALIGN)) {
        core->trap_mtval = addr;
        return -MR_XVM_EXCEPT_NR_ALIGN;
    }

    /* Translate address to binary image offset */
    bin_addr = CORE_ADDR_BIN(core, addr);
#if defined(MR_USE_MODULE_XVM_RISCV32_C)
    /* Single 4-byte fetch covers both 32-bit and 16-bit instructions */
    if (CORE_ADDR_IS_IN_BIN(core, bin_addr, 4U)) {
        word = core_load(core->bin->image + bin_addr, 4U);
        if ((word & 0x3U) == 0x3U) {
            *val = word;
            *instr_len = 4U;
            return 0;
        }
        ret = xvm_riscv32_rvc_expand((mr_uint16_t)word, val);
        if (ret != 0) {
            core->trap_mtval = word & 0xFFFFU;
            return ret;
        }
        *instr_len = 2U;
        return 0;
    }

    /* Tail path: 16-bit compressed instruction at the last 2 bytes of image */
    if (CORE_ADDR_IS_IN_BIN(core, bin_addr, 2U)) {
        word = core_load(core->bin->image + bin_addr, 2U);
        if ((word & 0x3U) != 0x3U) {
            ret = xvm_riscv32_rvc_expand((mr_uint16_t)word, val);
            if (ret != 0) {
                core->trap_mtval = word;
                return ret;
            }
            *instr_len = 2U;
            return 0;
        }
    }
    core->trap_mtval = addr;
    return -MR_XVM_EXCEPT_NR_ACCESS;
#else
    /* Fetch 32-bit instruction */
    if (CORE_ADDR_IS_IN_BIN(core, bin_addr, 4U)) {
        *val = core_load(core->bin->image + bin_addr, 4U);
        *instr_len = 4U;
    } else {
        core->trap_mtval = addr;
        return -MR_XVM_EXCEPT_NR_ACCESS;
    }
    return 0;
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_C) */
}

MR_INLINE int instr_exec_op(core_t *core, mr_uint32_t raw,
                            mr_uint32_t instr_len) {
    mr_uint32_t funct7, funct3, rd, rs1, rs2, res;
    mr_int64_t prod;

    /* Decode R-type fields */
    funct7 = INSTR_FUNCT7(raw);
    funct3 = INSTR_FUNCT3(raw);
    rd = INSTR_RD(raw);
    rs1 = CORE_REG_X(core, INSTR_RS1(raw));
    rs2 = CORE_REG_X(core, INSTR_RS2(raw));

    if (funct7 == 0x00U) {
        switch (funct3) {
            /* ADD */
            case 0x0U: {
                res = rs1 + rs2;
                break;
            }
            /* SLL */
            case 0x1U: {
                res = rs1 << (rs2 & 0x1fU);
                break;
            }
            /* SLT */
            case 0x2U: {
                res = ((mr_int32_t)rs1 < (mr_int32_t)rs2) ? 1U : 0U;
                break;
            }
            /* SLTU */
            case 0x3U: {
                res = (rs1 < rs2) ? 1U : 0U;
                break;
            }
            /* XOR */
            case 0x4U: {
                res = rs1 ^ rs2;
                break;
            }
            /* SRL */
            case 0x5U: {
                res = rs1 >> (rs2 & 0x1fU);
                break;
            }
            /* OR */
            case 0x6U: {
                res = rs1 | rs2;
                break;
            }
            /* AND */
            case 0x7U: {
                res = rs1 & rs2;
                break;
            }
            default: {
                core->trap_mtval = raw;
                return -MR_XVM_EXCEPT_NR_ILLEGAL;
            }
        }
    } else if (funct7 == 0x20U) {
        /* SUB */
        if (funct3 == 0x0U) {
            res = rs1 - rs2;
        } else if (funct3 == 0x5U) {
            /* SRA */
            res = core_sra32(rs1, rs2);
        } else {
            core->trap_mtval = raw;
            return -MR_XVM_EXCEPT_NR_ILLEGAL;
        }
    } else if (funct7 == 0x01U) {
        /* M extension */
        switch (funct3) {
            /* MUL */
            case 0x0U: {
                res = rs1 * rs2;
                break;
            }
            /* MULH */
            case 0x1U: {
                prod
                    = (mr_int64_t)(mr_int32_t)rs1 * (mr_int64_t)(mr_int32_t)rs2;
                res = (mr_uint32_t)((mr_uint64_t)prod >> 32);
                break;
            }
            /* MULHSU */
            case 0x2U: {
                prod = (mr_int64_t)(mr_int32_t)rs1
                     * (mr_int64_t)(mr_uint64_t)rs2;
                res = (mr_uint32_t)((mr_uint64_t)prod >> 32);
                break;
            }
            /* MULHU */
            case 0x3U: {
                res = (mr_uint32_t)(((mr_uint64_t)rs1 * (mr_uint64_t)rs2)
                                    >> 32);
                break;
            }
            /* DIV */
            case 0x4U: {
                if ((rs1 == 0x80000000U) && ((mr_int32_t)rs2 == (-1))) {
                    res = 0x80000000U;
                } else if (rs2 != 0U) {
                    res = (mr_uint32_t)((mr_int32_t)rs1 / (mr_int32_t)rs2);
                } else {
                    res = 0xffffffffU;
                }
                break;
            }
            /* DIVU */
            case 0x5U: {
                res = (rs2 != 0U) ? (rs1 / rs2) : 0xffffffffU;
                break;
            }
            /* REM */
            case 0x6U: {
                if ((rs1 == 0x80000000U) && ((mr_int32_t)rs2 == (-1))) {
                    res = 0U;
                } else if (rs2 != 0U) {
                    res = (mr_uint32_t)((mr_int32_t)rs1 % (mr_int32_t)rs2);
                } else {
                    res = rs1;
                }
                break;
            }
            /* REMU */
            case 0x7U: {
                res = (rs2 != 0U) ? (rs1 % rs2) : rs1;
                break;
            }
            default: {
                core->trap_mtval = raw;
                return -MR_XVM_EXCEPT_NR_ILLEGAL;
            }
        }
    } else {
        core->trap_mtval = raw;
        return -MR_XVM_EXCEPT_NR_ILLEGAL;
    }
    CORE_REG_X(core, rd) = res;
    CORE_REG_PC(core) += instr_len;
    return TRAP_TYPE_NONE;
}

MR_INLINE int instr_exec_op_imm(core_t *core, mr_uint32_t raw,
                                mr_uint32_t instr_len) {
    mr_uint32_t funct3, funct7, rd, rs1, imm, res;

    /* Decode I-type fields */
    funct3 = INSTR_FUNCT3(raw);
    rd = INSTR_RD(raw);
    rs1 = CORE_REG_X(core, INSTR_RS1(raw));
    imm = INSTR_I_IMM(raw);

    switch (funct3) {
        /* ADDI */
        case 0x0U: {
            res = rs1 + imm;
            break;
        }
        /* SLLI */
        case 0x1U: {
            if (INSTR_FUNCT7(raw) != 0x00U) {
                core->trap_mtval = raw;
                return -MR_XVM_EXCEPT_NR_ILLEGAL;
            }
            res = rs1 << (imm & 0x1fU);
            break;
        }
        /* SLTI */
        case 0x2U: {
            res = ((mr_int32_t)rs1 < (mr_int32_t)imm) ? 1U : 0U;
            break;
        }
        /* SLTIU */
        case 0x3U: {
            res = (rs1 < imm) ? 1U : 0U;
            break;
        }
        /* XORI */
        case 0x4U: {
            res = rs1 ^ imm;
            break;
        }
        case 0x5U: {
            funct7 = INSTR_FUNCT7(raw);
            /* SRLI */
            if (funct7 == 0x00U) {
                res = rs1 >> (imm & 0x1fU);
            } else if (funct7 == 0x20U) {
                /* SRAI */
                res = core_sra32(rs1, imm);
            } else {
                core->trap_mtval = raw;
                return -MR_XVM_EXCEPT_NR_ILLEGAL;
            }
            break;
        }
        /* ORI */
        case 0x6U: {
            res = rs1 | imm;
            break;
        }
        /* ANDI */
        case 0x7U: {
            res = rs1 & imm;
            break;
        }
        default: {
            core->trap_mtval = raw;
            return -MR_XVM_EXCEPT_NR_ILLEGAL;
        }
    }
    CORE_REG_X(core, rd) = res;
    CORE_REG_PC(core) += instr_len;
    return TRAP_TYPE_NONE;
}

MR_INLINE int instr_exec_load(core_t *core, mr_uint32_t raw,
                              mr_uint32_t instr_len) {
    mr_uint32_t funct3, rd, addr, bin_addr, mem_addr, val;
    int ret;

    /* Decode LOAD fields and effective address */
    funct3 = INSTR_FUNCT3(raw);
    rd = INSTR_RD(raw);
    addr = CORE_REG_X(core, INSTR_RS1(raw)) + INSTR_I_IMM(raw);
    /* Translate address to binary image offset */
    bin_addr = CORE_ADDR_BIN(core, addr);
    mem_addr = CORE_ADDR_MEM(core, addr);

    switch (funct3) {
        /* LB */
        case 0x0U: {
            if (CORE_ADDR_IS_IN_BIN(core, bin_addr, 1U)) {
                val = (mr_uint32_t)(mr_int8_t)core_load(
                    core->bin->image + bin_addr, 1U);
            } else if (CORE_ADDR_IS_IN_MEM(core, mem_addr, 1U)) {
                val = (mr_uint32_t)(mr_int8_t)core_load(core->mem + mem_addr,
                                                        1U);
#if defined(MR_USE_MODULE_XVM_MMIO)
            } else if (CORE_ADDR_IS_IN_MMIO(core, addr, 1U)) {
                ret = xvm_riscv32_mmio_load(core, addr, 1U, &val);
                if (ret != 0) {
                    core->trap_mtval = addr;
                    return ret;
                }
                val = (mr_uint32_t)(mr_int8_t)val;
#endif /* defined(MR_USE_MODULE_XVM_MMIO) */
            } else {
                core->trap_mtval = addr;
                return -MR_XVM_EXCEPT_NR_ACCESS;
            }
            break;
        }
        /* LH */
        case 0x1U: {
            if (!CORE_ADDR_IS_ALIGN(addr, 2U)) {
                core->trap_mtval = addr;
                return -MR_XVM_EXCEPT_NR_ALIGN;
            }
            if (CORE_ADDR_IS_IN_BIN(core, bin_addr, 2U)) {
                val = (mr_uint32_t)(mr_int16_t)core_load(
                    core->bin->image + bin_addr, 2U);
            } else if (CORE_ADDR_IS_IN_MEM(core, mem_addr, 2U)) {
                val = (mr_uint32_t)(mr_int16_t)core_load(core->mem + mem_addr,
                                                         2U);
#if defined(MR_USE_MODULE_XVM_MMIO)
            } else if (CORE_ADDR_IS_IN_MMIO(core, addr, 2U)) {
                ret = xvm_riscv32_mmio_load(core, addr, 2U, &val);
                if (ret != 0) {
                    core->trap_mtval = addr;
                    return ret;
                }
                val = (mr_uint32_t)(mr_int16_t)val;
#endif /* defined(MR_USE_MODULE_XVM_MMIO) */
            } else {
                core->trap_mtval = addr;
                return -MR_XVM_EXCEPT_NR_ACCESS;
            }
            break;
        }
        /* LW */
        case 0x2U: {
            if (!CORE_ADDR_IS_ALIGN(addr, 4U)) {
                core->trap_mtval = addr;
                return -MR_XVM_EXCEPT_NR_ALIGN;
            }
            if (CORE_ADDR_IS_IN_BIN(core, bin_addr, 4U)) {
                val = core_load(core->bin->image + bin_addr, 4U);
            } else if (CORE_ADDR_IS_IN_MEM(core, mem_addr, 4U)) {
                val = core_load(core->mem + mem_addr, 4U);
            } else if (CORE_ADDR_IS_IN_MMIO(core, addr, 4U)) {
                ret = xvm_riscv32_mmio_load(core, addr, 4U, &val);
                if (ret != 0) {
                    core->trap_mtval = addr;
                    return ret;
                }
            } else {
                core->trap_mtval = addr;
                return -MR_XVM_EXCEPT_NR_ACCESS;
            }
            break;
        }
        /* LBU */
        case 0x4U: {
            if (CORE_ADDR_IS_IN_BIN(core, bin_addr, 1U)) {
                val = core_load(core->bin->image + bin_addr, 1U);
            } else if (CORE_ADDR_IS_IN_MEM(core, mem_addr, 1U)) {
                val = core_load(core->mem + mem_addr, 1U);
#if defined(MR_USE_MODULE_XVM_MMIO)
            } else if (CORE_ADDR_IS_IN_MMIO(core, addr, 1U)) {
                ret = xvm_riscv32_mmio_load(core, addr, 1U, &val);
                if (ret != 0) {
                    core->trap_mtval = addr;
                    return ret;
                }
                val &= 0xffU;
#endif /* defined(MR_USE_MODULE_XVM_MMIO) */
            } else {
                core->trap_mtval = addr;
                return -MR_XVM_EXCEPT_NR_ACCESS;
            }
            break;
        }
        /* LHU */
        case 0x5U: {
            if (!CORE_ADDR_IS_ALIGN(addr, 2U)) {
                core->trap_mtval = addr;
                return -MR_XVM_EXCEPT_NR_ALIGN;
            }
            if (CORE_ADDR_IS_IN_BIN(core, bin_addr, 2U)) {
                val = core_load(core->bin->image + bin_addr, 2U);
            } else if (CORE_ADDR_IS_IN_MEM(core, mem_addr, 2U)) {
                val = core_load(core->mem + mem_addr, 2U);
#if defined(MR_USE_MODULE_XVM_MMIO)
            } else if (CORE_ADDR_IS_IN_MMIO(core, addr, 2U)) {
                ret = xvm_riscv32_mmio_load(core, addr, 2U, &val);
                if (ret != 0) {
                    core->trap_mtval = addr;
                    return ret;
                }
                val &= 0xffffU;
#endif /* defined(MR_USE_MODULE_XVM_MMIO) */
            } else {
                core->trap_mtval = addr;
                return -MR_XVM_EXCEPT_NR_ACCESS;
            }
            break;
        }
        default: {
            core->trap_mtval = raw;
            return -MR_XVM_EXCEPT_NR_ILLEGAL;
        }
    }
    CORE_REG_X(core, rd) = val;
    CORE_REG_PC(core) += instr_len;
    return TRAP_TYPE_NONE;
}

MR_INLINE int instr_exec_store(core_t *core, mr_uint32_t raw,
                               mr_uint32_t instr_len) {
    mr_uint32_t funct3, addr, val, mem_addr;
    int ret;

    /* Decode STORE fields and effective address */
    funct3 = INSTR_FUNCT3(raw);
    addr = CORE_REG_X(core, INSTR_RS1(raw)) + INSTR_S_IMM(raw);
    val = CORE_REG_X(core, INSTR_RS2(raw));
    mem_addr = CORE_ADDR_MEM(core, addr);

    switch (funct3) {
        /* SB */
        case 0x0U: {
            if (CORE_ADDR_IS_IN_MEM(core, mem_addr, 1U)) {
                core_store(core->mem + mem_addr, val, 1U);
#if defined(MR_USE_MODULE_XVM_MMIO)
            } else if (CORE_ADDR_IS_IN_MMIO(core, addr, 1U)) {
                ret = xvm_riscv32_mmio_store(core, addr, 1U, val & 0xffU);
                if (ret != 0) {
                    core->trap_mtval = addr;
                    return ret;
                }
#endif /* defined(MR_USE_MODULE_XVM_MMIO) */
            } else {
                core->trap_mtval = addr;
                return -MR_XVM_EXCEPT_NR_ACCESS;
            }
            break;
        }
        /* SH */
        case 0x1U: {
            if (!CORE_ADDR_IS_ALIGN(addr, 2U)) {
                core->trap_mtval = addr;
                return -MR_XVM_EXCEPT_NR_ALIGN;
            }
            if (CORE_ADDR_IS_IN_MEM(core, mem_addr, 2U)) {
                core_store(core->mem + mem_addr, val, 2U);
#if defined(MR_USE_MODULE_XVM_MMIO)
            } else if (CORE_ADDR_IS_IN_MMIO(core, addr, 2U)) {
                ret = xvm_riscv32_mmio_store(core, addr, 2U, val & 0xffffU);
                if (ret != 0) {
                    core->trap_mtval = addr;
                    return ret;
                }
#endif /* defined(MR_USE_MODULE_XVM_MMIO) */
            } else {
                core->trap_mtval = addr;
                return -MR_XVM_EXCEPT_NR_ACCESS;
            }
            break;
        }
        /* SW */
        case 0x2U: {
            if (!CORE_ADDR_IS_ALIGN(addr, 4U)) {
                core->trap_mtval = addr;
                return -MR_XVM_EXCEPT_NR_ALIGN;
            }
            if (CORE_ADDR_IS_IN_MEM(core, mem_addr, 4U)) {
                core_store(core->mem + mem_addr, val, 4U);
            } else if (CORE_ADDR_IS_IN_MMIO(core, addr, 4U)) {
                ret = xvm_riscv32_mmio_store(core, addr, 4U, val);
                if (ret != 0) {
                    core->trap_mtval = addr;
                    return ret;
                }
            } else {
                core->trap_mtval = addr;
                return -MR_XVM_EXCEPT_NR_ACCESS;
            }
            break;
        }
        default: {
            core->trap_mtval = raw;
            return -MR_XVM_EXCEPT_NR_ILLEGAL;
        }
    }
    CORE_REG_PC(core) += instr_len;
    return TRAP_TYPE_NONE;
}

MR_INLINE int instr_exec_branch(core_t *core, mr_uint32_t raw,
                                mr_uint32_t instr_len) {
    mr_uint32_t funct3, rs1, rs2;
    mr_bool_t taken;

    /* Decode B-type fields */
    funct3 = INSTR_FUNCT3(raw);
    rs1 = CORE_REG_X(core, INSTR_RS1(raw));
    rs2 = CORE_REG_X(core, INSTR_RS2(raw));

    switch (funct3) {
        /* BEQ */
        case 0x0U: {
            taken = (rs1 == rs2);
            break;
        }
        /* BNE */
        case 0x1U: {
            taken = (rs1 != rs2);
            break;
        }
        /* BLT */
        case 0x4U: {
            taken = ((mr_int32_t)rs1 < (mr_int32_t)rs2);
            break;
        }
        /* BGE */
        case 0x5U: {
            taken = ((mr_int32_t)rs1 >= (mr_int32_t)rs2);
            break;
        }
        /* BLTU */
        case 0x6U: {
            taken = (rs1 < rs2);
            break;
        }
        /* BGEU */
        case 0x7U: {
            taken = (rs1 >= rs2);
            break;
        }
        default: {
            core->trap_mtval = raw;
            return -MR_XVM_EXCEPT_NR_ILLEGAL;
        }
    }
    CORE_REG_PC(core) += taken ? INSTR_B_IMM(raw) : instr_len;
    return TRAP_TYPE_NONE;
}

MR_INLINE int instr_exec_system(core_t *core, mr_uint32_t raw,
                                mr_uint32_t instr_len) {
    mr_uint32_t funct3, rd, rs1_idx, csr_addr;
    mr_uint32_t old_val, new_val, mstatus;
    int ret;

    /* Decode SYSTEM fields */
    funct3 = INSTR_FUNCT3(raw);
    rd = INSTR_RD(raw);
    rs1_idx = INSTR_RS1(raw);
    csr_addr = INSTR_CSR(raw);

    if (funct3 == 0x0U) {
        if ((rd != 0U) || (rs1_idx != 0U)) {
            core->trap_mtval = raw;
            return -MR_XVM_EXCEPT_NR_ILLEGAL;
        }
        switch (csr_addr) {
            /* ECALL */
            case 0x000U: {
                CORE_REG_PC(core) += instr_len;
                return TRAP_TYPE_SYSCALL;
            }
            /* EBREAK */
            case 0x001U: {
                return -MR_XVM_EXCEPT_NR_BREAK;
            }
            /* MRET */
            case 0x302U: {
                mstatus = CORE_REG_MSTATUS(core);
                if (mstatus & REG_MSTATUS_MPIE) {
                    mstatus |= REG_MSTATUS_MIE;
                } else {
                    mstatus &= ~REG_MSTATUS_MIE;
                }
                mstatus |= REG_MSTATUS_MPIE;
                mstatus = (mstatus & ~REG_MSTATUS_MPP_MASK) | REG_MSTATUS_MPP_M;
                CORE_REG_MSTATUS(core) = mstatus;
                CORE_REG_PC(core)
                    = CORE_REG_MEPC(core) & ~(CORE_INSTR_ALIGN - 1U);
                return TRAP_TYPE_NONE;
            }
            default: {
                core->trap_mtval = raw;
                return -MR_XVM_EXCEPT_NR_ILLEGAL;
            }
        }
    }

    old_val = 0U;
    switch (funct3) {
        /* CSRRW */
        case 0x1U:
        /* CSRRWI */
        case 0x5U: {
            if (rd != 0U) {
                ret = xvm_riscv32_csr_load(core, csr_addr, &old_val);
                if (ret != 0) {
                    core->trap_mtval = raw;
                    return ret;
                }
            }
            new_val = (funct3 == 0x1U) ? CORE_REG_X(core, rs1_idx) : rs1_idx;
            ret = xvm_riscv32_csr_store(core, csr_addr, new_val);
            if (ret != 0) {
                core->trap_mtval = raw;
                return ret;
            }
            break;
        }
        /* CSRRS */
        case 0x2U:
        /* CSRRSI */
        case 0x6U: {
            ret = xvm_riscv32_csr_load(core, csr_addr, &old_val);
            if (ret != 0) {
                core->trap_mtval = raw;
                return ret;
            }
            if (rs1_idx != 0U) {
                new_val
                    = (funct3 == 0x2U) ? CORE_REG_X(core, rs1_idx) : rs1_idx;
                ret = xvm_riscv32_csr_store(core, csr_addr, old_val | new_val);
                if (ret != 0) {
                    core->trap_mtval = raw;
                    return ret;
                }
            }
            break;
        }
        /* CSRRC */
        case 0x3U:
        /* CSRRCI */
        case 0x7U: {
            ret = xvm_riscv32_csr_load(core, csr_addr, &old_val);
            if (ret != 0) {
                core->trap_mtval = raw;
                return ret;
            }
            if (rs1_idx != 0U) {
                new_val
                    = (funct3 == 0x3U) ? CORE_REG_X(core, rs1_idx) : rs1_idx;
                ret = xvm_riscv32_csr_store(core, csr_addr, old_val & ~new_val);
                if (ret != 0) {
                    core->trap_mtval = raw;
                    return ret;
                }
            }
            break;
        }
        default: {
            core->trap_mtval = raw;
            return -MR_XVM_EXCEPT_NR_ILLEGAL;
        }
    }
    CORE_REG_X(core, rd) = old_val;
    CORE_REG_PC(core) += instr_len;
    return TRAP_TYPE_NONE;
}

MR_INLINE int instr_exec(core_t *core, mr_uint32_t raw, mr_uint32_t instr_len) {
    mr_uint32_t target;
    int trap;

    switch (INSTR_OPCODE(raw)) {
        /* OP (R-type): arithmetic and M extension */
        case 0b0110011U: {
            trap = instr_exec_op(core, raw, instr_len);
            break;
        }
        /* OP-IMM (I-type): immediate arithmetic */
        case 0b0010011U: {
            trap = instr_exec_op_imm(core, raw, instr_len);
            break;
        }
        /* LOAD (I-type): LB/LH/LW/LBU/LHU */
        case 0b0000011U: {
            trap = instr_exec_load(core, raw, instr_len);
            break;
        }
        /* STORE (S-type): SB/SH/SW */
        case 0b0100011U: {
            trap = instr_exec_store(core, raw, instr_len);
            break;
        }
        /* BRANCH (B-type): BEQ/BNE/BLT/BGE/BLTU/BGEU */
        case 0b1100011U: {
            trap = instr_exec_branch(core, raw, instr_len);
            break;
        }
        /* JAL (J-type) */
        case 0b1101111U: {
            CORE_REG_X(core, INSTR_RD(raw)) = CORE_REG_PC(core) + instr_len;
            CORE_REG_PC(core) += INSTR_J_IMM(raw);
            trap = TRAP_TYPE_NONE;
            break;
        }
        /* JALR (I-type) */
        case 0b1100111U: {
            if (INSTR_FUNCT3(raw) != 0x0U) {
                core->trap_mtval = raw;
                trap = -MR_XVM_EXCEPT_NR_ILLEGAL;
                break;
            }
            target
                = (CORE_REG_X(core, INSTR_RS1(raw)) + INSTR_I_IMM(raw)) & ~1U;
            CORE_REG_X(core, INSTR_RD(raw)) = CORE_REG_PC(core) + instr_len;
            CORE_REG_PC(core) = target;
            trap = TRAP_TYPE_NONE;
            break;
        }
        /* LUI (U-type) */
        case 0b0110111U: {
            CORE_REG_X(core, INSTR_RD(raw)) = INSTR_U_IMM(raw);
            CORE_REG_PC(core) += instr_len;
            trap = TRAP_TYPE_NONE;
            break;
        }
        /* AUIPC (U-type) */
        case 0b0010111U: {
            CORE_REG_X(core, INSTR_RD(raw))
                = CORE_REG_PC(core) + INSTR_U_IMM(raw);
            CORE_REG_PC(core) += instr_len;
            trap = TRAP_TYPE_NONE;
            break;
        }
        /* SYSTEM (I-type): ECALL/EBREAK/MRET/CSR */
        case 0b1110011U: {
            trap = instr_exec_system(core, raw, instr_len);
            break;
        }
        /* FENCE */
        case 0b0001111U: {
            CORE_REG_PC(core) += instr_len;
            trap = TRAP_TYPE_NONE;
            break;
        }
        default: {
            core->trap_mtval = raw;
            trap = -MR_XVM_EXCEPT_NR_ILLEGAL;
            break;
        }
    }

    /* Ensure x0 always reads as 0 */
    CORE_REG_X(core, 0) = 0U;

    /* Increment 64-bit counters on retired instruction */
    if (trap == 0) {
        CORE_REG_MCYCLE(core)++;
        CORE_REG_MINSTRET(core)++;
    }
    return trap;
}

mr_xvm_return_t xvm_riscv32_exec_run(mr_xvm_t *xvm, core_t *core,
                                     mr_xvm_event_t *event, mr_uint32_t step) {
    mr_uint32_t i, except_nr;
    mr_uint32_t raw, instr_len;
    int trap;

#if !defined(MR_USE_MODULE_XVM_DEBUG)
    (void)xvm;
#endif /* !defined(MR_USE_MODULE_XVM_DEBUG) */

    /* Writeback syscall result */
    xvm_riscv32_syscall_writeback(core);

    /* Fetch, decode and execute instructions */
    for (i = 0U; i < step; i++) {
        /* Reset trap context */
        core->trap_mtval = 0U;

        /* Check pending interrupts before fetching an instruction */
        if ((CORE_REG_MSTATUS(core) & REG_MSTATUS_MIE)
            && xvm_riscv32_interrupt_enter(core)) {
            continue;
        }

#if defined(MR_USE_MODULE_XVM_DEBUG)
        /* Fast-path check before calling cross-TU breakpoint matcher */
        if ((xvm->debug.breaktable.num != 0U)
            && mr_xvm_debug_break_hit(xvm, CORE_REG_PC(core))) {
            event->except.nr = MR_XVM_EXCEPT_NR_BREAK;
            return MR_XVM_EXCEPT;
        }
#endif /* defined(MR_USE_MODULE_XVM_DEBUG) */

        /* Fetch instruction */
        trap = instr_fetch(core, CORE_REG_PC(core), &raw, &instr_len);
        if (trap != 0) {
            except_nr = (mr_uint32_t)-trap;

            /* Enter trap */
            if (xvm_riscv32_trap_enter(core, except_nr, core->trap_mtval)) {
                continue;
            }
            event->except.nr = except_nr;
            return MR_XVM_EXCEPT;
        }

        /* Decode and execute instruction */
        trap = instr_exec(core, raw, instr_len);
        if (trap == 0) {
            continue;
        }

        /* Exception */
        if (trap < 0) {
            except_nr = (mr_uint32_t)-trap;

            /* Enter trap(BREAK is handled by the host debugger) */
            if (except_nr != MR_XVM_EXCEPT_NR_BREAK) {
                if (xvm_riscv32_trap_enter(core, except_nr, core->trap_mtval)) {
                    continue;
                }
            }

            /* Raise exception */
            event->except.nr = except_nr;
            return MR_XVM_EXCEPT;
        }

        /* Syscall */
        return xvm_riscv32_syscall_handle(core, event);
    }

    /* Record the number of steps executed */
    event->none.step = i;
    return MR_XVM_NONE;
}
#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */

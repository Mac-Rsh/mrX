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
#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_ACCEL)                              \
    && defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED)
#include <mrX/kernel/mr_spinlock.h>
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_ACCEL)                        \
    && defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED) */

#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_ACCEL)
#if defined(__GNUC__) || defined(__clang__) || defined(__ARMCC_VERSION)
#define LOOP_NOINLINE                   __attribute__((noinline))
#else
#define LOOP_NOINLINE
#endif /* defined(__GNUC__) || defined(__clang__) || defined(__ARMCC_VERSION) */

/* Minimum byte span to use bulk mr_memcpy/mr_memset in loop acceleration */
#define LOOP_BULK_MIN_BYTES             (16U)
#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED)
/* Shared-pool low-yield side-exit backoff threshold and cooldown iterations */
#define LOOP_LOW_YIELD_LIMIT            (8U)
#define LOOP_LOW_YIELD_COOLDOWN         (32U)
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED) */
#if defined(MR_USE_MODULE_XVM_RISCV32_C)
#define LOOP_MISS_SET(_pc)                                                     \
    ((((_pc) >> 1) ^ ((_pc) >> 3) ^ ((_pc) >> 6))                              \
     & ((CORE_LOOP_MISS_FILTER_SIZE >> 1) - 1U))
#else
#define LOOP_MISS_SET(_pc)                                                     \
    ((((_pc) >> 2) ^ ((_pc) >> 4) ^ ((_pc) >> 7))                              \
     & ((CORE_LOOP_MISS_FILTER_SIZE >> 1) - 1U))
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_C) */

#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED)
/* Shared fully-associative pool with open-addressing hash index */
#define LOOP_POOL_SLOTS                 (MR_CFG_XVM_RISCV32_LOOP_POOL_SIZE)
#if (LOOP_POOL_SLOTS <= 8U)
#define LOOP_POOL_HASH_SIZE             (16U)
#define LOOP_POOL_HASH_SHIFT            (28U)
#elif (LOOP_POOL_SLOTS <= 16U)
#define LOOP_POOL_HASH_SIZE             (32U)
#define LOOP_POOL_HASH_SHIFT            (27U)
#elif (LOOP_POOL_SLOTS <= 32U)
#define LOOP_POOL_HASH_SIZE             (64U)
#define LOOP_POOL_HASH_SHIFT            (26U)
#else
#define LOOP_POOL_HASH_SIZE             (128U)
#define LOOP_POOL_HASH_SHIFT            (25U)
#endif /* LOOP_POOL_SLOTS */
#define LOOP_POOL_HASH_MASK             (LOOP_POOL_HASH_SIZE - 1U)

/* Shared Clock loop pool entry */
typedef struct loop_pool_entry {
    const mr_uint8_t *image;
    mr_size_t image_size;
    mr_uint32_t image_hash;
    mr_uint16_t pin;
    mr_uint8_t used;
    mr_uint8_t valid;
    loop_desc_t desc;
} loop_pool_entry_t;

/* Shared negative miss cache entry (image == MR_NULL means empty) */
typedef struct loop_miss_entry {
    const mr_uint8_t *image;
    mr_size_t image_size;
    mr_uint32_t image_hash;
    mr_uint32_t branch_pc;
} loop_miss_entry_t;

/* Shared decoded loop pool with variable-length circular uop arena */
typedef struct loop_pool {
    mr_spinlock_t lock;
    mr_spinlock_t build_lock;
    mr_uint16_t arena_head;
    mr_uint8_t clock_hand;
    mr_uint8_t miss_idx;
    mr_uint8_t hash_idx[LOOP_POOL_HASH_SIZE];
    loop_pool_entry_t entries[LOOP_POOL_SLOTS];
    loop_miss_entry_t misses[MR_CFG_XVM_RISCV32_LOOP_MISS_SIZE];
    loop_uop_t arena_uops[MR_CFG_XVM_RISCV32_LOOP_ARENA_SIZE];
    mr_uint8_t arena_offs[MR_CFG_XVM_RISCV32_LOOP_ARENA_SIZE];
    loop_uop_t stage_uops[MR_CFG_XVM_RISCV32_LOOP_CACHE_SIZE - 1U];
    mr_uint8_t stage_offs[MR_CFG_XVM_RISCV32_LOOP_CACHE_SIZE - 1U];
    loop_desc_t stage_desc;
} loop_pool_t;

static loop_pool_t __loop_pool;
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED) */

/* R-type OP and M-extension funct3 to micro-op lookup tables */
static const mr_uint8_t __loop_op_map[8] = {
    UOP_ADD, UOP_SLL, UOP_SLT, UOP_SLTU, UOP_XOR, UOP_SRL, UOP_OR, UOP_AND,
};
static const mr_uint8_t __loop_mul_map[8] = {
    UOP_MUL, UOP_MULH, UOP_MULHSU, UOP_MULHU,
    UOP_DIV, UOP_DIVU, UOP_REM,    UOP_REMU,
};
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_ACCEL) */

MR_INLINE void exec_counter_flush(core_t *core, mr_uint32_t cur_i,
                                  mr_uint32_t *base_i) {
    mr_uint32_t delta;

    /* Flush retired instructions in [*base_i, cur_i) into 64-bit counters */
    delta = cur_i - *base_i;
    if (delta != 0U) {
        CORE_REG_MCYCLE(core) += delta;
        CORE_REG_MINSTRET(core) += delta;
        *base_i = cur_i;
    }
}

MR_INLINE mr_uint32_t exec_irq_enabled(const core_t *core) {
    /* Check global MIE and any enabled machine interrupt source */
    return ((CORE_REG_MSTATUS(core) & REG_MSTATUS_MIE)
            && (CORE_REG_MIE(core)
                & (REG_MIE_MEIE | REG_MIE_MSIE | REG_MIE_MTIE)))
             ? 1U
             : 0U;
}

MR_INLINE mr_uint32_t exec_timer_at(const core_t *core, mr_uint32_t base_i,
                                    mr_uint32_t step) {
    mr_uint64_t mcycle, mtimecmp, diff;
    mr_uint32_t rem;

    /* Skip timer scheduling when MTI is masked */
    if (!(CORE_REG_MSTATUS(core) & REG_MSTATUS_MIE)
        || !(CORE_REG_MIE(core) & REG_MIE_MTIE)) {
        return step;
    }

    /* Check whether CLINT timer has already expired */
    mcycle = CORE_REG_MCYCLE(core);
    mtimecmp = core->clint.mtimecmp;
    if (mcycle >= mtimecmp) {
        return base_i;
    }

    /* Clamp next timer expiry step to the current run budget */
    diff = mtimecmp - mcycle;
    rem = step - base_i;
    if (diff >= (mr_uint64_t)rem) {
        return step;
    }
    return base_i + (mr_uint32_t)diff;
}

MR_INLINE int instr_fetch(core_t *core, const mr_uint8_t *image,
                          mr_size_t image_size, mr_uint32_t addr,
                          mr_uint32_t *val, mr_uint32_t *instr_len) {
#if defined(MR_USE_MODULE_XVM_RISCV32_C)
    mr_uint16_t lo;
    int ret;
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_C) */

    /* Check instruction address alignment */
    if (!CORE_ADDR_IS_ALIGN(addr, CORE_INSTR_ALIGN)) {
        core->trap_mtval = addr;
        return -MR_XVM_EXCEPT_NR_ALIGN;
    }

#if defined(MR_USE_MODULE_XVM_RISCV32_C)
    /* Fetch first 16-bit parcel */
    if ((image_size >= 2U) && (addr <= (image_size - 2U))) {
        lo = core_load16(image + addr);
        if ((lo & 0x3U) != 0x3U) {
            ret = xvm_riscv32_rvc_expand(lo, val);
            if (ret != 0) {
                core->trap_mtval = (mr_uint32_t)lo;
                return ret;
            }
            *instr_len = 2U;
            return 0;
        }
        /* 32-bit instruction: fetch upper 16-bit parcel */
        if ((image_size >= 4U) && (addr <= (image_size - 4U))) {
            *val = (mr_uint32_t)lo
                 | ((mr_uint32_t)core_load16(image + addr + 2U) << 16);
            *instr_len = 4U;
            return 0;
        }
    }
    core->trap_mtval = addr;
    return -MR_XVM_EXCEPT_NR_ACCESS;
#else
    /* Fetch 32-bit instruction */
    if ((image_size >= 4U) && (addr <= (image_size - 4U))) {
        *val = core_load32(image + addr);
        *instr_len = 4U;
        return 0;
    }
    core->trap_mtval = addr;
    return -MR_XVM_EXCEPT_NR_ACCESS;
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_C) */
}

MR_INLINE int instr_exec_op(core_t *core, mr_uint32_t raw) {
    mr_uint32_t funct7, funct3, rd, rs1, rs2, res;
    mr_int64_t prod;

    /* Decode R-type fields */
    funct7 = INSTR_FUNCT7(raw);
    funct3 = INSTR_FUNCT3(raw);
    rd = INSTR_RD(raw);
    rs1 = CORE_REG_X(core, INSTR_RS1(raw));
    rs2 = CORE_REG_X(core, INSTR_RS2(raw));

    if (funct7 == 0x00U) {
        if (funct3 == 0x0U) {
            /* ADD */
            res = rs1 + rs2;
        } else {
            switch (funct3) {
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
    if (rd != 0U) {
        CORE_REG_X(core, rd) = res;
    }
    return TRAP_TYPE_NONE;
}

MR_INLINE int instr_exec_op_imm(core_t *core, mr_uint32_t raw) {
    mr_uint32_t funct3, funct7, rd, rs1, imm, res;

    /* Decode I-type fields */
    funct3 = INSTR_FUNCT3(raw);
    rd = INSTR_RD(raw);
    rs1 = CORE_REG_X(core, INSTR_RS1(raw));
    imm = INSTR_I_IMM(raw);

    if (funct3 == 0x0U) {
        /* ADDI */
        res = rs1 + imm;
    } else {
        switch (funct3) {
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
    }
    if (rd != 0U) {
        CORE_REG_X(core, rd) = res;
    }
    return TRAP_TYPE_NONE;
}

MR_INLINE int instr_exec_load(core_t *core, mr_uint32_t raw, mr_uint32_t cur_pc,
                              mr_uint32_t cur_i, mr_uint32_t *counter_base_i) {
    mr_uint32_t funct3, rd, addr, mem_addr, val;
    int ret;

    /* Decode LOAD fields and effective address */
    funct3 = INSTR_FUNCT3(raw);
    rd = INSTR_RD(raw);
    addr = CORE_REG_X(core, INSTR_RS1(raw)) + INSTR_I_IMM(raw);
    mem_addr = CORE_ADDR_MEM(core, addr);

    if (funct3 == 0x2U) {
        /* LW */
        if (!CORE_ADDR_IS_ALIGN(addr, 4U)) {
            core->trap_mtval = addr;
            return -MR_XVM_EXCEPT_NR_ALIGN;
        }
        if (CORE_ADDR_IS_IN_MEM(core, mem_addr, 4U)) {
            val = core_load32(core->mem + mem_addr);
        } else if (CORE_ADDR_IS_IN_BIN(core, addr, 4U)) {
            val = core_load32(core->bin->image + addr);
        } else if (CORE_ADDR_IS_IN_MMIO(core, addr, 4U)) {
            CORE_REG_PC(core) = cur_pc;
            exec_counter_flush(core, cur_i, counter_base_i);
            ret = xvm_riscv32_mmio_load(core, addr, 4U, &val);
            if (ret != 0) {
                core->trap_mtval = addr;
                return ret;
            }
        } else {
            core->trap_mtval = addr;
            return -MR_XVM_EXCEPT_NR_ACCESS;
        }
    } else {
        switch (funct3) {
            /* LB */
            case 0x0U: {
                if (CORE_ADDR_IS_IN_MEM(core, mem_addr, 1U)) {
                    val = (mr_uint32_t)(mr_int8_t)core_load8(core->mem
                                                             + mem_addr);
                } else if (CORE_ADDR_IS_IN_BIN(core, addr, 1U)) {
                    val = (mr_uint32_t)(mr_int8_t)core_load8(core->bin->image
                                                             + addr);
#if defined(MR_USE_MODULE_XVM_MMIO)
                } else if (CORE_ADDR_IS_IN_MMIO(core, addr, 1U)) {
                    CORE_REG_PC(core) = cur_pc;
                    exec_counter_flush(core, cur_i, counter_base_i);
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
                if (CORE_ADDR_IS_IN_MEM(core, mem_addr, 2U)) {
                    val = (mr_uint32_t)(mr_int16_t)core_load16(core->mem
                                                               + mem_addr);
                } else if (CORE_ADDR_IS_IN_BIN(core, addr, 2U)) {
                    val = (mr_uint32_t)(mr_int16_t)core_load16(core->bin->image
                                                               + addr);
#if defined(MR_USE_MODULE_XVM_MMIO)
                } else if (CORE_ADDR_IS_IN_MMIO(core, addr, 2U)) {
                    CORE_REG_PC(core) = cur_pc;
                    exec_counter_flush(core, cur_i, counter_base_i);
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
            /* LBU */
            case 0x4U: {
                if (CORE_ADDR_IS_IN_MEM(core, mem_addr, 1U)) {
                    val = (mr_uint32_t)core_load8(core->mem + mem_addr);
                } else if (CORE_ADDR_IS_IN_BIN(core, addr, 1U)) {
                    val = (mr_uint32_t)core_load8(core->bin->image + addr);
#if defined(MR_USE_MODULE_XVM_MMIO)
                } else if (CORE_ADDR_IS_IN_MMIO(core, addr, 1U)) {
                    CORE_REG_PC(core) = cur_pc;
                    exec_counter_flush(core, cur_i, counter_base_i);
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
                if (CORE_ADDR_IS_IN_MEM(core, mem_addr, 2U)) {
                    val = (mr_uint32_t)core_load16(core->mem + mem_addr);
                } else if (CORE_ADDR_IS_IN_BIN(core, addr, 2U)) {
                    val = (mr_uint32_t)core_load16(core->bin->image + addr);
#if defined(MR_USE_MODULE_XVM_MMIO)
                } else if (CORE_ADDR_IS_IN_MMIO(core, addr, 2U)) {
                    CORE_REG_PC(core) = cur_pc;
                    exec_counter_flush(core, cur_i, counter_base_i);
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
    }
    if (rd != 0U) {
        CORE_REG_X(core, rd) = val;
    }
    return TRAP_TYPE_NONE;
}

MR_INLINE int instr_exec_store(core_t *core, mr_uint32_t raw,
                               mr_uint32_t cur_pc, mr_uint32_t cur_i,
                               mr_uint32_t *counter_base_i,
                               mr_uint32_t *timer_at, mr_uint32_t step) {
    mr_uint32_t funct3, addr, val, mem_addr;
    int ret;

    /* Decode STORE fields and effective address */
    funct3 = INSTR_FUNCT3(raw);
    addr = CORE_REG_X(core, INSTR_RS1(raw)) + INSTR_S_IMM(raw);
    val = CORE_REG_X(core, INSTR_RS2(raw));
    mem_addr = CORE_ADDR_MEM(core, addr);

    if (funct3 == 0x2U) {
        /* SW */
        if (!CORE_ADDR_IS_ALIGN(addr, 4U)) {
            core->trap_mtval = addr;
            return -MR_XVM_EXCEPT_NR_ALIGN;
        }
        if (CORE_ADDR_IS_IN_MEM(core, mem_addr, 4U)) {
            core_store32(core->mem + mem_addr, val);
        } else if (CORE_ADDR_IS_IN_MMIO(core, addr, 4U)) {
            CORE_REG_PC(core) = cur_pc;
            exec_counter_flush(core, cur_i, counter_base_i);
            ret = xvm_riscv32_mmio_store(core, addr, 4U, val);
            if (ret != 0) {
                core->trap_mtval = addr;
                return ret;
            }
            core->irq_dirty = 1U;
            *timer_at = exec_timer_at(core, *counter_base_i, step);
        } else {
            core->trap_mtval = addr;
            return -MR_XVM_EXCEPT_NR_ACCESS;
        }
    } else if (funct3 == 0x0U) {
        /* SB */
        if (CORE_ADDR_IS_IN_MEM(core, mem_addr, 1U)) {
            core_store8(core->mem + mem_addr, val);
#if defined(MR_USE_MODULE_XVM_MMIO)
        } else if (CORE_ADDR_IS_IN_MMIO(core, addr, 1U)) {
            CORE_REG_PC(core) = cur_pc;
            exec_counter_flush(core, cur_i, counter_base_i);
            ret = xvm_riscv32_mmio_store(core, addr, 1U, val & 0xffU);
            if (ret != 0) {
                core->trap_mtval = addr;
                return ret;
            }
            core->irq_dirty = 1U;
            *timer_at = exec_timer_at(core, *counter_base_i, step);
#endif /* defined(MR_USE_MODULE_XVM_MMIO) */
        } else {
            core->trap_mtval = addr;
            return -MR_XVM_EXCEPT_NR_ACCESS;
        }
    } else if (funct3 == 0x1U) {
        /* SH */
        if (!CORE_ADDR_IS_ALIGN(addr, 2U)) {
            core->trap_mtval = addr;
            return -MR_XVM_EXCEPT_NR_ALIGN;
        }
        if (CORE_ADDR_IS_IN_MEM(core, mem_addr, 2U)) {
            core_store16(core->mem + mem_addr, val);
#if defined(MR_USE_MODULE_XVM_MMIO)
        } else if (CORE_ADDR_IS_IN_MMIO(core, addr, 2U)) {
            CORE_REG_PC(core) = cur_pc;
            exec_counter_flush(core, cur_i, counter_base_i);
            ret = xvm_riscv32_mmio_store(core, addr, 2U, val & 0xffffU);
            if (ret != 0) {
                core->trap_mtval = addr;
                return ret;
            }
            core->irq_dirty = 1U;
            *timer_at = exec_timer_at(core, *counter_base_i, step);
#endif /* defined(MR_USE_MODULE_XVM_MMIO) */
        } else {
            core->trap_mtval = addr;
            return -MR_XVM_EXCEPT_NR_ACCESS;
        }
    } else {
        core->trap_mtval = raw;
        return -MR_XVM_EXCEPT_NR_ILLEGAL;
    }
    return TRAP_TYPE_NONE;
}

#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_ACCEL)
MR_INLINE mr_bool_t loop_miss_filter_hit(const core_t *core,
                                         mr_uint32_t branch_pc) {
    mr_uint32_t base;

    /* Check both ways of the 2-way set (4 sets x 2 ways, zero 2-PC clash) */
    base = LOOP_MISS_SET(branch_pc) << 1;
    return ((core->miss_pc[base] == branch_pc)
            || (core->miss_pc[base + 1U] == branch_pc))
             ? MR_TRUE
             : MR_FALSE;
}

MR_INLINE void loop_miss_filter_add(core_t *core, mr_uint32_t branch_pc) {
    mr_uint32_t base;

    /* Shift way 0 -> way 1 and record most-recent rejected PC in way 0 */
    base = LOOP_MISS_SET(branch_pc) << 1;
    if (core->miss_pc[base] != branch_pc) {
        core->miss_pc[base + 1U] = core->miss_pc[base];
        core->miss_pc[base] = branch_pc;
    }
}

MR_INLINE mr_bool_t loop_is_addi_self(mr_uint32_t raw, mr_uint8_t *reg,
                                      mr_int32_t *imm) {
    mr_uint32_t rd;

    /* Match ADDI rd, rd, imm (rd != x0) */
    if ((INSTR_OPCODE(raw) != 0b0010011U) || (INSTR_FUNCT3(raw) != 0x0U)) {
        return MR_FALSE;
    }
    rd = INSTR_RD(raw);
    if ((rd == 0U) || (rd != INSTR_RS1(raw))) {
        return MR_FALSE;
    }
    *reg = (mr_uint8_t)rd;
    *imm = (mr_int32_t)INSTR_I_IMM(raw);
    return MR_TRUE;
}

MR_INLINE mr_bool_t loop_is_load(mr_uint32_t raw, mr_uint8_t *rd,
                                 mr_uint8_t *rs1, mr_int32_t *off,
                                 mr_uint8_t *funct3, mr_uint8_t *wshift) {
    mr_uint32_t f3, dst;

    /* Match LOAD rd, off(rs1) with non-zero rd */
    if (INSTR_OPCODE(raw) != 0b0000011U) {
        return MR_FALSE;
    }
    dst = INSTR_RD(raw);
    if (dst == 0U) {
        return MR_FALSE;
    }

    /* Decode access width shift from funct3 */
    f3 = INSTR_FUNCT3(raw);
    if ((f3 == 0x0U) || (f3 == 0x4U)) {
        *wshift = 0U;
    } else if ((f3 == 0x1U) || (f3 == 0x5U)) {
        *wshift = 1U;
    } else if (f3 == 0x2U) {
        *wshift = 2U;
    } else {
        return MR_FALSE;
    }
    *rd = (mr_uint8_t)dst;
    *rs1 = (mr_uint8_t)INSTR_RS1(raw);
    *off = (mr_int32_t)INSTR_I_IMM(raw);
    *funct3 = (mr_uint8_t)f3;
    return MR_TRUE;
}

MR_INLINE mr_bool_t loop_is_store(mr_uint32_t raw, mr_uint8_t *rs1,
                                  mr_uint8_t *rs2, mr_int32_t *off,
                                  mr_uint8_t *wshift) {
    mr_uint32_t f3;

    /* Match STORE rs2, off(rs1) (SB/SH/SW) */
    if (INSTR_OPCODE(raw) != 0b0100011U) {
        return MR_FALSE;
    }
    f3 = INSTR_FUNCT3(raw);
    if (f3 > 0x2U) {
        return MR_FALSE;
    }
    *rs1 = (mr_uint8_t)INSTR_RS1(raw);
    *rs2 = (mr_uint8_t)INSTR_RS2(raw);
    *off = (mr_int32_t)INSTR_S_IMM(raw);
    *wshift = (mr_uint8_t)f3;
    return MR_TRUE;
}

MR_INLINE mr_bool_t loop_is_add_acc(mr_uint32_t raw, mr_uint8_t val_reg,
                                    mr_uint8_t *acc_reg) {
    mr_uint32_t rd, rs1, rs2;

    /* Match ADD acc, acc, val (or ADD acc, val, acc) */
    if ((INSTR_OPCODE(raw) != 0b0110011U) || (INSTR_FUNCT3(raw) != 0x0U)
        || (INSTR_FUNCT7(raw) != 0x00U)) {
        return MR_FALSE;
    }
    rd = INSTR_RD(raw);
    rs1 = INSTR_RS1(raw);
    rs2 = INSTR_RS2(raw);
    if (rd == 0U) {
        return MR_FALSE;
    }
    if ((rs1 == rd) && (rs2 == val_reg)) {
        *acc_reg = (mr_uint8_t)rd;
        return MR_TRUE;
    }
    if ((rs2 == rd) && (rs1 == val_reg)) {
        *acc_reg = (mr_uint8_t)rd;
        return MR_TRUE;
    }
    return MR_FALSE;
}

MR_INLINE mr_uint32_t loop_load_elem(const mr_uint8_t *ptr, mr_uint8_t funct3) {
    /* Load element with matching sign/zero extension */
    switch (funct3) {
        /* LB */
        case 0x0U: {
            return (mr_uint32_t)(mr_int8_t)core_load8(ptr);
        }
        /* LH */
        case 0x1U: {
            return (mr_uint32_t)(mr_int16_t)core_load16(ptr);
        }
        /* LW */
        case 0x2U: {
            return core_load32(ptr);
        }
        /* LBU */
        case 0x4U: {
            return (mr_uint32_t)core_load8(ptr);
        }
        /* LHU */
        default: {
            return (mr_uint32_t)core_load16(ptr);
        }
    }
}

MR_INLINE mr_uint32_t loop_rem_iters_step(mr_uint32_t cur, mr_uint32_t end,
                                          mr_uint8_t b_f3, mr_uint8_t wshift) {
    mr_uint32_t diff;
    mr_uint32_t w;

    /* Compute remaining iterations for pointer-step loops */
    if (b_f3 == 0x1U) {
        /* BNE */
        diff = end - cur;
        w = 1U << wshift;
        if ((diff == 0U) || ((diff & (w - 1U)) != 0U)) {
            return 0U;
        }
        return diff >> wshift;
    }
    if (b_f3 == 0x6U) {
        /* BLTU */
        if (cur >= end) {
            return 0U;
        }
        return ((end - cur - 1U) >> wshift) + 1U;
    }
    return 0U;
}

MR_INLINE mr_bool_t loop_branch_taken(mr_uint8_t funct3, mr_uint32_t rs1,
                                      mr_uint32_t rs2) {
    /* Evaluate branch condition */
    switch (funct3) {
        /* BEQ */
        case 0x0U: {
            return (rs1 == rs2);
        }
        /* BNE */
        case 0x1U: {
            return (rs1 != rs2);
        }
        /* BLT */
        case 0x4U: {
            return ((mr_int32_t)rs1 < (mr_int32_t)rs2);
        }
        /* BGE */
        case 0x5U: {
            return ((mr_int32_t)rs1 >= (mr_int32_t)rs2);
        }
        /* BLTU */
        case 0x6U: {
            return (rs1 < rs2);
        }
        /* BGEU */
        default: {
            return (rs1 >= rs2);
        }
    }
}

static mr_bool_t loop_decode_uop(mr_uint32_t raw, mr_uint32_t pc,
                                 loop_uop_t *uop) {
    mr_uint32_t opcode, funct3, funct7, rd;

    /* Initialize decoded micro-op */
    opcode = INSTR_OPCODE(raw);
    funct3 = INSTR_FUNCT3(raw);
    rd = INSTR_RD(raw);
    uop->imm = 0U;
    uop->rd = (mr_uint8_t)rd;
    uop->rs1 = (mr_uint8_t)INSTR_RS1(raw);
    uop->rs2 = (mr_uint8_t)INSTR_RS2(raw);

    switch (opcode) {
        /* OP (R-type): RV32IM */
        case 0b0110011U: {
            funct7 = INSTR_FUNCT7(raw);
            if (funct7 == 0x00U) {
                uop->op = __loop_op_map[funct3];
            } else if (funct7 == 0x20U) {
                if (funct3 == 0x0U) {
                    uop->op = UOP_SUB;
                } else if (funct3 == 0x5U) {
                    uop->op = UOP_SRA;
                } else {
                    return MR_FALSE;
                }
            } else if (funct7 == 0x01U) {
                uop->op = __loop_mul_map[funct3];
            } else {
                return MR_FALSE;
            }
            if (rd == 0U) {
                uop->op = UOP_NOP;
            }
            return MR_TRUE;
        }
        /* OP-IMM (I-type) */
        case 0b0010011U: {
            uop->imm = INSTR_I_IMM(raw);
            switch (funct3) {
                case 0x0U: {
                    uop->op = UOP_ADDI;
                    break;
                }
                case 0x1U: {
                    if (INSTR_FUNCT7(raw) != 0x00U) {
                        return MR_FALSE;
                    }
                    uop->imm &= 0x1fU;
                    uop->op = UOP_SLLI;
                    break;
                }
                case 0x2U: {
                    uop->op = UOP_SLTI;
                    break;
                }
                case 0x3U: {
                    uop->op = UOP_SLTIU;
                    break;
                }
                case 0x4U: {
                    uop->op = UOP_XORI;
                    break;
                }
                case 0x5U: {
                    funct7 = INSTR_FUNCT7(raw);
                    uop->imm &= 0x1fU;
                    if (funct7 == 0x00U) {
                        uop->op = UOP_SRLI;
                    } else if (funct7 == 0x20U) {
                        uop->op = UOP_SRAI;
                    } else {
                        return MR_FALSE;
                    }
                    break;
                }
                case 0x6U: {
                    uop->op = UOP_ORI;
                    break;
                }
                default: {
                    uop->op = UOP_ANDI;
                    break;
                }
            }
            if (rd == 0U) {
                uop->op = UOP_NOP;
            }
            return MR_TRUE;
        }
        /* LOAD (I-type): fast LW or generic LOAD(funct3 in rs2) */
        case 0b0000011U: {
            uop->imm = INSTR_I_IMM(raw);
            if (funct3 == 0x2U) {
                uop->op = UOP_LW;
            } else if ((funct3 == 0x0U) || (funct3 == 0x1U) || (funct3 == 0x4U)
                       || (funct3 == 0x5U)) {
                uop->op = UOP_LOAD;
                uop->rs2 = (mr_uint8_t)funct3;
            } else {
                return MR_FALSE;
            }
            return MR_TRUE;
        }
        /* STORE (S-type): fast SW or generic STORE(funct3 in rd) */
        case 0b0100011U: {
            uop->imm = INSTR_S_IMM(raw);
            if (funct3 == 0x2U) {
                uop->op = UOP_SW;
            } else if (funct3 < 0x2U) {
                uop->op = UOP_STORE;
                uop->rd = (mr_uint8_t)funct3;
            } else {
                return MR_FALSE;
            }
            return MR_TRUE;
        }
        /* LUI (U-type) */
        case 0b0110111U: {
            uop->imm = INSTR_U_IMM(raw);
            uop->op = (rd == 0U) ? UOP_NOP : UOP_LUI;
            return MR_TRUE;
        }
        /* AUIPC (U-type): fold PC + U_IMM into LUI */
        case 0b0010111U: {
            uop->imm = pc + INSTR_U_IMM(raw);
            uop->op = (rd == 0U) ? UOP_NOP : UOP_LUI;
            return MR_TRUE;
        }
        /* BRANCH (B-type): internal forward skip (funct3 in rd) */
        case 0b1100011U: {
            if ((funct3 == 0x2U) || (funct3 == 0x3U)) {
                return MR_FALSE;
            }
            uop->op = UOP_BR;
            uop->rd = (mr_uint8_t)funct3;
            uop->imm = pc + INSTR_B_IMM(raw);
            return MR_TRUE;
        }
        /* FENCE */
        case 0b0001111U: {
            uop->op = UOP_NOP;
            return MR_TRUE;
        }
        default: {
            return MR_FALSE;
        }
    }
}

MR_INLINE mr_uint32_t loop_uop_pc(const loop_desc_t *desc,
                                  const mr_uint8_t *pc_offs, mr_uint32_t k) {
    /* Map micro-op index to caller or inlined callee PC */
    if ((k >= desc->u.dec.callee_start) && (k < desc->u.dec.callee_end)) {
        return desc->u.dec.callee_pc + pc_offs[k];
    }
    return desc->loop_start + pc_offs[k];
}

MR_INLINE mr_uint32_t loop_fault_retired(const loop_desc_t *desc, mr_uint32_t k,
                                         mr_uint32_t skipped) {
    mr_uint32_t elided;

    /* Add back elided dead AUIPC/ret instructions preceding micro-op k */
    elided = 0U;
    if (desc->u.dec.callee_end > 0U) {
        if (k >= desc->u.dec.callee_start) {
            elided += desc->u.dec.elided_call;
        }
        if (k >= desc->u.dec.callee_end) {
            elided += desc->u.dec.elided_ret;
        }
    }
    return k + elided - skipped;
}

#if !defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED)
static mr_bool_t loop_can_decode(core_t *core, const mr_uint8_t *image,
                                 mr_size_t image_size, mr_uint32_t loop_start,
                                 mr_uint32_t branch_pc,
                                 mr_uint32_t branch_raw) {
    mr_uint32_t pc, raw, len, count, max_body, b_f3, opcode, rd, rs1;
    mr_uint32_t const_reg, const_val, callee_pc, ret_pc, c_pc, c_raw, c_len;
    mr_uint32_t link_reg, target_pc;
    mr_bool_t has_callee, is_call;
    loop_uop_t tmp;

    /* Validate closing branch opcode (Bxx or unconditional JAL x0) */
    if (INSTR_OPCODE(branch_raw) == 0b1101111U) {
        if (INSTR_RD(branch_raw) != 0U) {
            return MR_FALSE;
        }
    } else {
        b_f3 = INSTR_FUNCT3(branch_raw);
        if ((b_f3 == 0x2U) || (b_f3 == 0x3U)) {
            return MR_FALSE;
        }
    }

    /* Read-only pre-scan: reject non-decodable loops without touching cache */
    max_body = MR_CFG_XVM_RISCV32_LOOP_CACHE_SIZE - 1U;
    pc = loop_start;
    count = 0U;
    const_reg = 0U;
    const_val = 0U;
    callee_pc = 0U;
    ret_pc = 0U;
    link_reg = 0U;
    has_callee = MR_FALSE;
    while (pc < branch_pc) {
        if ((count >= max_body) || ((pc - loop_start) > 255U)) {
            return MR_FALSE;
        }
        if (instr_fetch(core, image, image_size, pc, &raw, &len) != 0) {
            return MR_FALSE;
        }
        opcode = INSTR_OPCODE(raw);
        rd = INSTR_RD(raw);

        /* Accept unconditional JAL x0 as internal skip or out-of-loop exit */
        if ((opcode == 0b1101111U) && (rd == 0U)) {
            target_pc = pc + INSTR_J_IMM(raw);
            if ((target_pc >= loop_start) && (target_pc <= pc)) {
                return MR_FALSE;
            }
            count++;
            pc += len;
            continue;
        }

        /* Reject dynamic/load-fed JALR and second calls early */
        is_call = MR_FALSE;
        if (opcode == 0b1101111U) {
            if (has_callee) {
                return MR_FALSE;
            }
            callee_pc = pc + INSTR_J_IMM(raw);
            ret_pc = pc + len;
            link_reg = rd;
            is_call = MR_TRUE;
        } else if (opcode == 0b1100111U) {
            rs1 = INSTR_RS1(raw);
            if ((INSTR_FUNCT3(raw) != 0x0U) || (rd == 0U) || has_callee
                || (rs1 == 0U) || (rs1 != const_reg)) {
                return MR_FALSE;
            }
            callee_pc = (const_val + INSTR_I_IMM(raw)) & ~1U;
            ret_pc = pc + len;
            link_reg = rd;
            is_call = MR_TRUE;
        }

        /* Pre-check static leaf callee */
        if (is_call) {
            count++;
            c_pc = callee_pc;
            while (MR_TRUE) {
                if ((count >= max_body) || ((c_pc - callee_pc) > 255U)) {
                    return MR_FALSE;
                }
                if (instr_fetch(core, image, image_size, c_pc, &c_raw, &c_len)
                    != 0) {
                    return MR_FALSE;
                }
                if ((INSTR_OPCODE(c_raw) == 0b1100111U)
                    && (INSTR_FUNCT3(c_raw) == 0x0U) && (INSTR_RD(c_raw) == 0U)
                    && (INSTR_RS1(c_raw) == link_reg)
                    && (INSTR_I_IMM(c_raw) == 0U)) {
                    count++;
                    has_callee = MR_TRUE;
                    break;
                }
                if ((INSTR_OPCODE(c_raw) == 0b1101111U)
                    && (INSTR_RD(c_raw) == 0U)) {
                    target_pc = c_pc + INSTR_J_IMM(c_raw);
                    if (target_pc <= c_pc) {
                        return MR_FALSE;
                    }
                    count++;
                    c_pc += c_len;
                    continue;
                }
                if (!loop_decode_uop(c_raw, c_pc, &tmp)) {
                    return MR_FALSE;
                }
                if ((tmp.op != UOP_SW) && (tmp.op != UOP_STORE)
                    && (tmp.op != UOP_BR) && (tmp.op != UOP_NOP)
                    && (tmp.rd == link_reg)) {
                    return MR_FALSE;
                }
                if ((tmp.op == UOP_BR) && (tmp.imm <= c_pc)) {
                    return MR_FALSE;
                }
                count++;
                c_pc += c_len;
            }
            const_reg = 0U;
            pc = ret_pc;
            continue;
        }

        /* Pre-check regular caller instruction */
        if (!loop_decode_uop(raw, pc, &tmp)) {
            return MR_FALSE;
        }
        if (tmp.op == UOP_BR) {
            target_pc = tmp.imm;
            if ((target_pc >= loop_start) && (target_pc <= pc)) {
                return MR_FALSE;
            }
        } else if (tmp.op == UOP_LUI) {
            const_reg = tmp.rd;
            const_val = tmp.imm;
        } else if ((tmp.op != UOP_SW) && (tmp.op != UOP_STORE)
                   && (tmp.op != UOP_NOP) && (tmp.rd == const_reg)) {
            const_reg = 0U;
        }
        count++;
        pc += len;
    }
    return ((pc == branch_pc) && (count > 0U)) ? MR_TRUE : MR_FALSE;
}
#endif /* !defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED) */

static mr_bool_t loop_match_idiom(loop_desc_t *desc, const mr_uint32_t *body,
                                  mr_uint32_t count, mr_uint32_t b_f3,
                                  mr_uint32_t b_rs1, mr_uint32_t b_rs2) {
    mr_uint8_t rT, rS, rD, rA0, rA1, rE, cmp_reg;
    mr_uint8_t r0, r1, r2, r3, f3, w0, w1;
    mr_int32_t imm0, imm1, off0, off1;
    mr_bool_t matched;

    /* Pattern 1: DELAY (2 instrs): addi rC, rC, +-1; bne/blt/bltu rC, rE */
    if ((count == 1U) && ((b_f3 == 0x1U) || (b_f3 == 0x4U) || (b_f3 == 0x6U))
        && loop_is_addi_self(body[0], &r0, &imm0)
        && ((imm0 == 1) || (imm0 == -1))) {
        matched = MR_FALSE;
        if (b_f3 == 0x1U) {
            if ((b_rs1 == r0) && (b_rs2 != r0)) {
                r1 = (mr_uint8_t)b_rs2;
                matched = MR_TRUE;
            } else if ((b_rs2 == r0) && (b_rs1 != r0)) {
                r1 = (mr_uint8_t)b_rs1;
                matched = MR_TRUE;
            }
        } else if ((imm0 == 1) && (b_rs1 == r0) && (b_rs2 != r0)) {
            r1 = (mr_uint8_t)b_rs2;
            matched = MR_TRUE;
        }
        if (matched) {
            desc->kind = LOOP_KIND_DELAY;
            desc->instr_count = 2U;
            desc->branch_funct3 = (mr_uint8_t)b_f3;
            desc->r0 = r0;
            desc->r1 = r1;
            desc->u.idiom.off0 = imm0;
            return MR_TRUE;
        }
    }

    /* Pattern 2: FILL / memset (3 instrs): store + addi rP, +w; bne/bltu */
    if ((count == 2U) && ((b_f3 == 0x1U) || (b_f3 == 0x6U))) {
        matched = MR_FALSE;
        if (loop_is_store(body[0], &r0, &r1, &off0, &w0)
            && loop_is_addi_self(body[1], &r2, &imm0) && (r2 == r0)
            && (imm0 == (mr_int32_t)(1U << w0))) {
            matched = MR_TRUE;
        } else if (loop_is_addi_self(body[0], &r2, &imm0)
                   && loop_is_store(body[1], &r0, &r1, &off0, &w0) && (r2 == r0)
                   && (imm0 == (mr_int32_t)(1U << w0))) {
            off0 += imm0;
            matched = MR_TRUE;
        }
        if (matched && (r0 != 0U) && (r1 != r0)) {
            matched = MR_FALSE;
            if ((b_f3 == 0x1U) && (b_rs2 == r0) && (b_rs1 != r0)) {
                r3 = (mr_uint8_t)b_rs1;
                matched = MR_TRUE;
            } else if ((b_rs1 == r0) && (b_rs2 != r0)) {
                r3 = (mr_uint8_t)b_rs2;
                matched = MR_TRUE;
            }
            if (matched) {
                desc->kind = LOOP_KIND_FILL;
                desc->instr_count = 3U;
                desc->branch_funct3 = (mr_uint8_t)b_f3;
                desc->r0 = r0;
                desc->r1 = r1;
                desc->u.idiom.r2 = r3;
                desc->u.idiom.off0 = off0;
                desc->u.idiom.width_shift = w0;
                return MR_TRUE;
            }
        }
    }

    /* Pattern 3: SUM / reduction (4 instrs): load + add + addi; bne/bltu */
    if ((count == 3U) && ((b_f3 == 0x1U) || (b_f3 == 0x6U))) {
        matched = MR_FALSE;
        if (loop_is_load(body[0], &r1, &r0, &off0, &f3, &w0) && (r0 != 0U)
            && (r1 != r0)) {
            if (loop_is_add_acc(body[1], r1, &r2)
                && loop_is_addi_self(body[2], &r3, &imm0) && (r3 == r0)
                && (imm0 == (mr_int32_t)(1U << w0))) {
                matched = MR_TRUE;
            } else if (loop_is_addi_self(body[1], &r3, &imm0) && (r3 == r0)
                       && (imm0 == (mr_int32_t)(1U << w0))
                       && loop_is_add_acc(body[2], r1, &r2)) {
                matched = MR_TRUE;
            }
        }
        if (matched && (r2 != r0) && (r2 != r1)) {
            matched = MR_FALSE;
            if ((b_f3 == 0x1U) && (b_rs2 == r0) && (b_rs1 != r0)) {
                r3 = (mr_uint8_t)b_rs1;
                matched = MR_TRUE;
            } else if ((b_rs1 == r0) && (b_rs2 != r0)) {
                r3 = (mr_uint8_t)b_rs2;
                matched = MR_TRUE;
            }
            if (matched && (r3 != r1) && (r3 != r2)) {
                desc->kind = LOOP_KIND_SUM;
                desc->instr_count = 4U;
                desc->branch_funct3 = (mr_uint8_t)b_f3;
                desc->r0 = r0;
                desc->r1 = r1;
                desc->u.idiom.r2 = r2;
                desc->u.idiom.r3 = r3;
                desc->u.idiom.off0 = off0;
                desc->u.idiom.load_funct3 = f3;
                desc->u.idiom.width_shift = w0;
                return MR_TRUE;
            }
        }
    }

    /* Pattern 4: COPY / memcpy (5 instrs): load + store + 2*addi; bne/bltu */
    if ((count == 4U) && ((b_f3 == 0x1U) || (b_f3 == 0x6U))) {
        matched = MR_FALSE;
        if (loop_is_load(body[0], &rT, &rS, &off0, &f3, &w0)
            && loop_is_store(body[1], &rD, &r1, &off1, &w1) && (w0 == w1)
            && (r1 == rT) && (rS != 0U) && (rD != 0U) && (rS != rD)
            && (rT != rS) && (rT != rD)
            && loop_is_addi_self(body[2], &rA0, &imm0)
            && loop_is_addi_self(body[3], &rA1, &imm1)
            && (imm0 == (mr_int32_t)(1U << w0)) && (imm1 == imm0)
            && (((rA0 == rS) && (rA1 == rD)) || ((rA0 == rD) && (rA1 == rS)))) {
            if ((b_rs1 == rS) || (b_rs1 == rD)) {
                cmp_reg = (mr_uint8_t)b_rs1;
                rE = (mr_uint8_t)b_rs2;
                matched = MR_TRUE;
            } else if ((b_f3 == 0x1U) && ((b_rs2 == rS) || (b_rs2 == rD))) {
                cmp_reg = (mr_uint8_t)b_rs2;
                rE = (mr_uint8_t)b_rs1;
                matched = MR_TRUE;
            }
        }
        if (matched && (rE != rS) && (rE != rD) && (rE != rT)) {
            desc->kind = LOOP_KIND_COPY;
            desc->instr_count = 5U;
            desc->branch_funct3 = (mr_uint8_t)b_f3;
            desc->r0 = rS;
            desc->r1 = rD;
            desc->u.idiom.r2 = rT;
            desc->u.idiom.r3 = rE;
            desc->u.idiom.cmp_reg = cmp_reg;
            desc->u.idiom.off0 = off0;
            desc->u.idiom.off1 = off1;
            desc->u.idiom.load_funct3 = f3;
            desc->u.idiom.width_shift = w0;
            return MR_TRUE;
        }
    }
    return MR_FALSE;
}

static mr_bool_t loop_build_onepass(
    core_t *core, loop_desc_t *desc, loop_uop_t *uops, mr_uint8_t *pc_offs,
    const mr_uint8_t *image, mr_size_t image_size, mr_uint32_t branch_pc,
    mr_uint32_t branch_len, mr_uint32_t branch_raw, mr_uint32_t loop_start) {
    mr_uint32_t pc, raw, len, count, max_body, b_f3, b_rs1, b_rs2;
    mr_uint32_t opcode, rd, rs1, const_reg, const_val;
    mr_uint32_t callee_pc, ret_pc, c_pc, c_raw, c_len;
    mr_uint32_t k, t, target_pc, link_reg, end_idx, call_idx;
    mr_uint8_t elided_call, elided_ret, elided_alu;
    mr_bool_t has_callee, has_inner, has_mem, in_callee, found;
#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED)
    mr_bool_t has_exit;
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED) */
    mr_bool_t is_call, dead_auipc;
    mr_uint32_t body[4];
    loop_uop_t *uop;

    /* Initialize descriptor as unmatched */
    desc->branch_pc = branch_pc;
    desc->loop_start = loop_start;
    desc->branch_next = branch_pc + branch_len;
    desc->kind = LOOP_KIND_NONE;
    desc->u.dec.callee_pc = 0U;
    desc->u.dec.uop_start = 0U;
    desc->u.dec.callee_start = 0U;
    desc->u.dec.callee_end = 0U;
    desc->u.dec.elided_call = 0U;
    desc->u.dec.elided_ret = 0U;
#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED)
    desc->u.dec.has_exit = 0U;
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED) */

    /* Validate closing branch opcode (Bxx or unconditional JAL x0) */
    if (INSTR_OPCODE(branch_raw) == 0b1101111U) {
        if (INSTR_RD(branch_raw) != 0U) {
            return MR_FALSE;
        }
        b_f3 = 0x0U;
        b_rs1 = 0U;
        b_rs2 = 0U;
    } else {
        b_f3 = INSTR_FUNCT3(branch_raw);
        if ((b_f3 == 0x2U) || (b_f3 == 0x3U)) {
            return MR_FALSE;
        }
        b_rs1 = INSTR_RS1(branch_raw);
        b_rs2 = INSTR_RS2(branch_raw);
    }

    /* Single-pass fetch & decode for caller + at most one static leaf call */
    max_body = MR_CFG_XVM_RISCV32_LOOP_CACHE_SIZE - 1U;
    pc = loop_start;
    count = 0U;
    const_reg = 0U;
    const_val = 0U;
    callee_pc = 0U;
    ret_pc = 0U;
    link_reg = 0U;
    call_idx = 0U;
    has_callee = MR_FALSE;
    has_inner = MR_FALSE;
#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED)
    has_exit = MR_FALSE;
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED) */
    has_mem = MR_FALSE;
    dead_auipc = MR_FALSE;
    while (pc < branch_pc) {
        if ((count >= max_body) || ((pc - loop_start) > 255U)) {
            return MR_FALSE;
        }
        if (instr_fetch(core, image, image_size, pc, &raw, &len) != 0) {
            return MR_FALSE;
        }
        if (!has_callee && (count < 4U)) {
            body[count] = raw;
        }
        opcode = INSTR_OPCODE(raw);
        rd = INSTR_RD(raw);

        /* Encode unconditional JAL x0 as BEQ x0, x0 (internal skip or exit) */
        if ((opcode == 0b1101111U) && (rd == 0U)) {
            target_pc = pc + INSTR_J_IMM(raw);
            if ((target_pc >= loop_start) && (target_pc <= pc)) {
                return MR_FALSE;
            }
            uop = &uops[count];
            uop->op = UOP_BR;
            if ((target_pc < loop_start) || (target_pc > branch_pc)) {
                uop->rd = 0x80U;
#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED)
                has_exit = MR_TRUE;
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED) */
            } else {
                uop->rd = 0x0U;
            }
            uop->rs1 = 0U;
            uop->rs2 = 0U;
            uop->imm = target_pc;
            has_inner = MR_TRUE;
            pc_offs[count] = (mr_uint8_t)(pc - loop_start);
            count++;
            pc += len;
            continue;
        }

        /* Detect static leaf call: JAL or (AUIPC/LUI) + JALR */
        is_call = MR_FALSE;
        if (opcode == 0b1101111U) {
            if (has_callee) {
                return MR_FALSE;
            }
            callee_pc = pc + INSTR_J_IMM(raw);
            ret_pc = pc + len;
            link_reg = rd;
            is_call = MR_TRUE;
        } else if (opcode == 0b1100111U) {
            rs1 = INSTR_RS1(raw);
            if ((INSTR_FUNCT3(raw) != 0x0U) || (rd == 0U) || has_callee
                || (rs1 == 0U) || (rs1 != const_reg)) {
                return MR_FALSE;
            }
            callee_pc = (const_val + INSTR_I_IMM(raw)) & ~1U;
            ret_pc = pc + len;
            link_reg = rd;
            is_call = MR_TRUE;
            if ((count > 0U) && (rs1 == link_reg)
                && (uops[count - 1U].op == UOP_LUI)
                && (uops[count - 1U].rd == link_reg)) {
                dead_auipc = MR_TRUE;
            }
        }

        /* Inline leaf callee ending with JALR x0, 0(link_reg) */
        if (is_call) {
            call_idx = count;
            uop = &uops[count];
            uop->op = UOP_LUI;
            uop->rd = (mr_uint8_t)link_reg;
            uop->rs1 = 0U;
            uop->rs2 = 0U;
            uop->imm = ret_pc;
            pc_offs[count] = (mr_uint8_t)(pc - loop_start);
            count++;

            desc->u.dec.callee_pc = callee_pc;
            desc->u.dec.callee_start = (mr_uint8_t)count;
            c_pc = callee_pc;
            while (MR_TRUE) {
                if ((count >= max_body) || ((c_pc - callee_pc) > 255U)) {
                    return MR_FALSE;
                }
                if (instr_fetch(core, image, image_size, c_pc, &c_raw, &c_len)
                    != 0) {
                    return MR_FALSE;
                }
                if ((INSTR_OPCODE(c_raw) == 0b1100111U)
                    && (INSTR_FUNCT3(c_raw) == 0x0U) && (INSTR_RD(c_raw) == 0U)
                    && (INSTR_RS1(c_raw) == link_reg)
                    && (INSTR_I_IMM(c_raw) == 0U)) {
                    uop = &uops[count];
                    uop->op = UOP_NOP;
                    uop->rd = 0U;
                    uop->rs1 = 0U;
                    uop->rs2 = 0U;
                    uop->imm = 0U;
                    pc_offs[count] = (mr_uint8_t)(c_pc - callee_pc);
                    count++;
                    desc->u.dec.callee_end = (mr_uint8_t)count;
                    has_callee = MR_TRUE;
                    break;
                }
                if ((INSTR_OPCODE(c_raw) == 0b1101111U)
                    && (INSTR_RD(c_raw) == 0U)) {
                    target_pc = c_pc + INSTR_J_IMM(c_raw);
                    if (target_pc <= c_pc) {
                        return MR_FALSE;
                    }
                    uop = &uops[count];
                    uop->op = UOP_BR;
                    uop->rd = 0x0U;
                    uop->rs1 = 0U;
                    uop->rs2 = 0U;
                    uop->imm = target_pc;
                    has_inner = MR_TRUE;
                    pc_offs[count] = (mr_uint8_t)(c_pc - callee_pc);
                    count++;
                    c_pc += c_len;
                    continue;
                }
                uop = &uops[count];
                if (!loop_decode_uop(c_raw, c_pc, uop)) {
                    return MR_FALSE;
                }
                /* Reject callees that overwrite the link register */
                if ((uop->op != UOP_SW) && (uop->op != UOP_STORE)
                    && (uop->op != UOP_BR) && (uop->op != UOP_NOP)
                    && (uop->rd == link_reg)) {
                    return MR_FALSE;
                }
                if (uop->op == UOP_BR) {
                    if (uop->imm <= c_pc) {
                        return MR_FALSE;
                    }
                    has_inner = MR_TRUE;
                } else if ((uop->op == UOP_LW) || (uop->op == UOP_LOAD)
                           || (uop->op == UOP_SW) || (uop->op == UOP_STORE)) {
                    has_mem = MR_TRUE;
                }
                pc_offs[count] = (mr_uint8_t)(c_pc - callee_pc);
                count++;
                c_pc += c_len;
            }
            const_reg = 0U;
            pc = ret_pc;
            continue;
        }

        /* Decode regular caller instruction */
        uop = &uops[count];
        if (!loop_decode_uop(raw, pc, uop)) {
            return MR_FALSE;
        }
        if (uop->op == UOP_BR) {
            target_pc = uop->imm;
            if ((target_pc >= loop_start) && (target_pc <= pc)) {
                return MR_FALSE;
            }
            if ((target_pc < loop_start) || (target_pc > branch_pc)) {
                uop->rd |= 0x80U;
#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED)
                has_exit = MR_TRUE;
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED) */
            }
            has_inner = MR_TRUE;
        } else if (uop->op == UOP_LUI) {
            const_reg = uop->rd;
            const_val = uop->imm;
        } else {
            if ((uop->op == UOP_LW) || (uop->op == UOP_LOAD)
                || (uop->op == UOP_SW) || (uop->op == UOP_STORE)) {
                has_mem = MR_TRUE;
            }
            if ((uop->op != UOP_SW) && (uop->op != UOP_STORE)
                && (uop->op != UOP_NOP) && (uop->rd == const_reg)) {
                const_reg = 0U;
            }
        }
        pc_offs[count] = (mr_uint8_t)(pc - loop_start);
        count++;
        pc += len;
    }
    if ((pc != branch_pc) || (count == 0U)) {
        return MR_FALSE;
    }

    /* Try specialized idiom templates for short call-free loops (0 arena) */
    if (!has_callee && (count <= 4U)
        && loop_match_idiom(desc, body, count, b_f3, b_rs1, b_rs2)) {
        return MR_TRUE;
    }

    /* Resolve internal forward branch targets within caller or callee */
    if (has_inner) {
        for (k = 0U; k < count; k++) {
            if ((uops[k].op != UOP_BR) || ((uops[k].rd & 0x80U) != 0U)) {
                continue;
            }
            target_pc = uops[k].imm;
            in_callee = ((k >= desc->u.dec.callee_start)
                         && (k < desc->u.dec.callee_end));
            if (!in_callee && (target_pc == branch_pc)) {
                uops[k].imm = count;
                continue;
            }
            found = MR_FALSE;
            end_idx = in_callee ? (mr_uint32_t)desc->u.dec.callee_end : count;
            for (t = k + 1U; t < end_idx; t++) {
                if (in_callee) {
                    if ((desc->u.dec.callee_pc + pc_offs[t]) == target_pc) {
                        uops[k].imm = t;
                        found = MR_TRUE;
                        break;
                    }
                } else if ((t < desc->u.dec.callee_start)
                           || (t >= desc->u.dec.callee_end)) {
                    if ((desc->loop_start + pc_offs[t]) == target_pc) {
                        uops[k].imm = t;
                        found = MR_TRUE;
                        break;
                    }
                }
            }
            if (!found) {
                return MR_FALSE;
            }
        }
    }

    /* Zero-new-case static elisions for branch-free decoded loops */
    elided_call = 0U;
    elided_ret = 0U;
    elided_alu = 0U;
    if (!has_inner) {
        /* Elide dead AUIPC and tail ret NOP in inlined leaf calls */
        if (has_callee) {
            for (t = desc->u.dec.callee_end; t < count; t++) {
                uops[t - 1U] = uops[t];
                pc_offs[t - 1U] = pc_offs[t];
            }
            desc->u.dec.callee_end--;
            count--;
            elided_ret = 1U;

            if (dead_auipc) {
                for (t = call_idx; t < count; t++) {
                    uops[t - 1U] = uops[t];
                    pc_offs[t - 1U] = pc_offs[t];
                }
                desc->u.dec.callee_start--;
                desc->u.dec.callee_end--;
                count--;
                elided_call = 1U;
            }
        }

        /* Fold LUI + ADDI constant pairs in non-faulting ALU loops */
        if (!has_mem && (count >= 2U)) {
            k = 0U;
            while ((k + 1U) < count) {
                if ((uops[k].op == UOP_LUI) && (uops[k + 1U].op == UOP_ADDI)
                    && (uops[k + 1U].rd == uops[k].rd)
                    && (uops[k + 1U].rs1 == uops[k].rd)
                    && ((k + 1U) != desc->u.dec.callee_start)
                    && ((k + 1U) != desc->u.dec.callee_end)) {
                    uops[k].imm += uops[k + 1U].imm;
                    for (t = k + 2U; t < count; t++) {
                        uops[t - 1U] = uops[t];
                        pc_offs[t - 1U] = pc_offs[t];
                    }
                    if ((k + 1U) < desc->u.dec.callee_start) {
                        desc->u.dec.callee_start--;
                    }
                    if ((k + 1U) < desc->u.dec.callee_end) {
                        desc->u.dec.callee_end--;
                    }
                    count--;
                    elided_alu++;
                    continue;
                }
                k++;
            }
        }
    }

    /* Commit pre-decoded loop block */
    desc->kind = LOOP_KIND_DECODED;
    desc->u.dec.body_count = (mr_uint8_t)count;
    desc->instr_count
        = (mr_uint8_t)(count + 1U + elided_call + elided_ret + elided_alu);
    desc->u.dec.elided_call = elided_call;
    desc->u.dec.elided_ret = elided_ret;
#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED)
    desc->u.dec.has_exit = has_exit ? 1U : 0U;
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED) */
    desc->branch_funct3 = (mr_uint8_t)b_f3;
    desc->r0 = (mr_uint8_t)b_rs1;
    desc->r1 = (mr_uint8_t)b_rs2;
    return MR_TRUE;
}

LOOP_NOINLINE static mr_uint32_t exec_loop_decoded(
    core_t *core, const loop_desc_t *desc, const loop_uop_t *uops,
    const mr_uint8_t *pc_offs, mr_uint32_t budget, mr_uint32_t irq_en) {
    mr_uint32_t mem_base, retired, skipped, k;
    mr_uint32_t rs1, rs2, addr, mem_addr, val, w;
    const loop_uop_t *uop, *uop_end;
    mr_size_t mem_size;
    mr_uint32_t *regs;
    mr_uint8_t *mem;
    mr_int64_t prod;
    mr_uint8_t br_rd;

    /* Keep only inner-loop hot pointers and bounds in registers */
    regs = core->regs.x;
    mem = core->mem;
    mem_base = core->mem_base;
    mem_size = core->mem_size;
    uop_end = uops + desc->u.dec.body_count;
    retired = 0U;

    /* Execute pre-decoded iterations while budget covers a full iteration */
    while ((budget - retired) >= (mr_uint32_t)desc->instr_count) {
        uop = uops;
        skipped = 0U;
        while (uop < uop_end) {
            switch (uop->op) {
                case UOP_NOP: {
                    break;
                }
                case UOP_ADD: {
                    regs[uop->rd] = regs[uop->rs1] + regs[uop->rs2];
                    break;
                }
                case UOP_SUB: {
                    regs[uop->rd] = regs[uop->rs1] - regs[uop->rs2];
                    break;
                }
                case UOP_SLL: {
                    regs[uop->rd] = regs[uop->rs1] << (regs[uop->rs2] & 0x1fU);
                    break;
                }
                case UOP_SLT: {
                    regs[uop->rd] = ((mr_int32_t)regs[uop->rs1]
                                     < (mr_int32_t)regs[uop->rs2])
                                      ? 1U
                                      : 0U;
                    break;
                }
                case UOP_SLTU: {
                    regs[uop->rd] = (regs[uop->rs1] < regs[uop->rs2]) ? 1U : 0U;
                    break;
                }
                case UOP_XOR: {
                    regs[uop->rd] = regs[uop->rs1] ^ regs[uop->rs2];
                    break;
                }
                case UOP_SRL: {
                    regs[uop->rd] = regs[uop->rs1] >> (regs[uop->rs2] & 0x1fU);
                    break;
                }
                case UOP_SRA: {
                    regs[uop->rd] = core_sra32(regs[uop->rs1], regs[uop->rs2]);
                    break;
                }
                case UOP_OR: {
                    regs[uop->rd] = regs[uop->rs1] | regs[uop->rs2];
                    break;
                }
                case UOP_AND: {
                    regs[uop->rd] = regs[uop->rs1] & regs[uop->rs2];
                    break;
                }
                case UOP_MUL: {
                    regs[uop->rd] = regs[uop->rs1] * regs[uop->rs2];
                    break;
                }
                case UOP_MULH: {
                    prod = (mr_int64_t)(mr_int32_t)regs[uop->rs1]
                         * (mr_int64_t)(mr_int32_t)regs[uop->rs2];
                    regs[uop->rd] = (mr_uint32_t)((mr_uint64_t)prod >> 32);
                    break;
                }
                case UOP_MULHSU: {
                    prod = (mr_int64_t)(mr_int32_t)regs[uop->rs1]
                         * (mr_int64_t)(mr_uint64_t)regs[uop->rs2];
                    regs[uop->rd] = (mr_uint32_t)((mr_uint64_t)prod >> 32);
                    break;
                }
                case UOP_MULHU: {
                    regs[uop->rd]
                        = (mr_uint32_t)(((mr_uint64_t)regs[uop->rs1]
                                         * (mr_uint64_t)regs[uop->rs2])
                                        >> 32);
                    break;
                }
                case UOP_DIV: {
                    rs1 = regs[uop->rs1];
                    rs2 = regs[uop->rs2];
                    if ((rs1 == 0x80000000U) && ((mr_int32_t)rs2 == (-1))) {
                        regs[uop->rd] = 0x80000000U;
                    } else if (rs2 != 0U) {
                        regs[uop->rd]
                            = (mr_uint32_t)((mr_int32_t)rs1 / (mr_int32_t)rs2);
                    } else {
                        regs[uop->rd] = 0xffffffffU;
                    }
                    break;
                }
                case UOP_DIVU: {
                    rs2 = regs[uop->rs2];
                    regs[uop->rd]
                        = (rs2 != 0U) ? (regs[uop->rs1] / rs2) : 0xffffffffU;
                    break;
                }
                case UOP_REM: {
                    rs1 = regs[uop->rs1];
                    rs2 = regs[uop->rs2];
                    if ((rs1 == 0x80000000U) && ((mr_int32_t)rs2 == (-1))) {
                        regs[uop->rd] = 0U;
                    } else if (rs2 != 0U) {
                        regs[uop->rd]
                            = (mr_uint32_t)((mr_int32_t)rs1 % (mr_int32_t)rs2);
                    } else {
                        regs[uop->rd] = rs1;
                    }
                    break;
                }
                case UOP_REMU: {
                    rs1 = regs[uop->rs1];
                    rs2 = regs[uop->rs2];
                    regs[uop->rd] = (rs2 != 0U) ? (rs1 % rs2) : rs1;
                    break;
                }
                case UOP_ADDI: {
                    regs[uop->rd] = regs[uop->rs1] + uop->imm;
                    break;
                }
                case UOP_SLLI: {
                    regs[uop->rd] = regs[uop->rs1] << uop->imm;
                    break;
                }
                case UOP_SLTI: {
                    regs[uop->rd]
                        = ((mr_int32_t)regs[uop->rs1] < (mr_int32_t)uop->imm)
                            ? 1U
                            : 0U;
                    break;
                }
                case UOP_SLTIU: {
                    regs[uop->rd] = (regs[uop->rs1] < uop->imm) ? 1U : 0U;
                    break;
                }
                case UOP_XORI: {
                    regs[uop->rd] = regs[uop->rs1] ^ uop->imm;
                    break;
                }
                case UOP_SRLI: {
                    regs[uop->rd] = regs[uop->rs1] >> uop->imm;
                    break;
                }
                case UOP_SRAI: {
                    regs[uop->rd] = core_sra32(regs[uop->rs1], uop->imm);
                    break;
                }
                case UOP_ORI: {
                    regs[uop->rd] = regs[uop->rs1] | uop->imm;
                    break;
                }
                case UOP_ANDI: {
                    regs[uop->rd] = regs[uop->rs1] & uop->imm;
                    break;
                }
                case UOP_LW: {
                    addr = regs[uop->rs1] + uop->imm;
                    mem_addr = addr - mem_base;
                    if (!CORE_ADDR_IS_ALIGN(addr, 4U)) {
                        k = (mr_uint32_t)(uop - uops);
                        CORE_REG_PC(core) = loop_uop_pc(desc, pc_offs, k);
                        return retired + loop_fault_retired(desc, k, skipped);
                    }
                    if ((mem_size >= 4U) && (mem_addr <= (mem_size - 4U))) {
                        val = core_load32(mem + mem_addr);
                    } else if (CORE_ADDR_IS_IN_BIN(core, addr, 4U)) {
                        val = core_load32(core->bin->image + addr);
                    } else {
                        k = (mr_uint32_t)(uop - uops);
                        CORE_REG_PC(core) = loop_uop_pc(desc, pc_offs, k);
                        return retired + loop_fault_retired(desc, k, skipped);
                    }
                    if (uop->rd != 0U) {
                        regs[uop->rd] = val;
                    }
                    break;
                }
                case UOP_LOAD: {
                    addr = regs[uop->rs1] + uop->imm;
                    mem_addr = addr - mem_base;
                    w = 1U << (uop->rs2 & 0x3U);
                    if (!CORE_ADDR_IS_ALIGN(addr, w)) {
                        k = (mr_uint32_t)(uop - uops);
                        CORE_REG_PC(core) = loop_uop_pc(desc, pc_offs, k);
                        return retired + loop_fault_retired(desc, k, skipped);
                    }
                    if ((mem_size >= w) && (mem_addr <= (mem_size - w))) {
                        val = loop_load_elem(mem + mem_addr, uop->rs2);
                    } else if (CORE_ADDR_IS_IN_BIN(core, addr, w)) {
                        val = loop_load_elem(core->bin->image + addr, uop->rs2);
                    } else {
                        k = (mr_uint32_t)(uop - uops);
                        CORE_REG_PC(core) = loop_uop_pc(desc, pc_offs, k);
                        return retired + loop_fault_retired(desc, k, skipped);
                    }
                    if (uop->rd != 0U) {
                        regs[uop->rd] = val;
                    }
                    break;
                }
                case UOP_SW: {
                    addr = regs[uop->rs1] + uop->imm;
                    mem_addr = addr - mem_base;
                    if (CORE_ADDR_IS_ALIGN(addr, 4U) && (mem_size >= 4U)
                        && (mem_addr <= (mem_size - 4U))) {
                        core_store32(mem + mem_addr, regs[uop->rs2]);
                    } else {
                        k = (mr_uint32_t)(uop - uops);
                        CORE_REG_PC(core) = loop_uop_pc(desc, pc_offs, k);
                        return retired + loop_fault_retired(desc, k, skipped);
                    }
                    break;
                }
                case UOP_STORE: {
                    addr = regs[uop->rs1] + uop->imm;
                    mem_addr = addr - mem_base;
                    w = 1U << uop->rd;
                    if (CORE_ADDR_IS_ALIGN(addr, w) && (mem_size >= w)
                        && (mem_addr <= (mem_size - w))) {
                        if (w == 1U) {
                            core_store8(mem + mem_addr, regs[uop->rs2]);
                        } else {
                            core_store16(mem + mem_addr, regs[uop->rs2]);
                        }
                    } else {
                        k = (mr_uint32_t)(uop - uops);
                        CORE_REG_PC(core) = loop_uop_pc(desc, pc_offs, k);
                        return retired + loop_fault_retired(desc, k, skipped);
                    }
                    break;
                }
                case UOP_LUI: {
                    regs[uop->rd] = uop->imm;
                    break;
                }
                case UOP_BR: {
                    br_rd = uop->rd;
                    if (loop_branch_taken((mr_uint8_t)(br_rd & 0x7U),
                                          regs[uop->rs1], regs[uop->rs2])) {
                        k = (mr_uint32_t)(uop - uops);
                        if ((br_rd & 0x80U) != 0U) {
                            CORE_REG_PC(core) = uop->imm;
                            return retired + k + 1U - skipped;
                        }
                        skipped += (uop->imm - k - 1U);
                        uop = uops + uop->imm;
                        continue;
                    }
                    break;
                }
                default: {
                    k = (mr_uint32_t)(uop - uops);
                    CORE_REG_PC(core) = loop_uop_pc(desc, pc_offs, k);
                    return retired + loop_fault_retired(desc, k, skipped);
                }
            }
            uop++;
        }

        /* Evaluate closing backward branch and commit block-length steps */
        retired += (mr_uint32_t)desc->instr_count - skipped;
        if (!loop_branch_taken(desc->branch_funct3, regs[desc->r0],
                               regs[desc->r1])) {
            CORE_REG_PC(core) = desc->branch_next;
            return retired;
        }

        /* Yield back to outer loop if an external interrupt became pending */
        if ((irq_en != 0U) && (core->irq_dirty != 0U)) {
            break;
        }
    }
    return retired;
}

static mr_uint32_t exec_loop_cached(core_t *core, const loop_desc_t *desc,
                                    const loop_uop_t *uops,
                                    const mr_uint8_t *pc_offs,
                                    const mr_uint8_t *image,
                                    mr_size_t image_size, mr_uint32_t budget,
                                    mr_uint32_t irq_en) {
    mr_uint32_t max_iters, rem_iters, n, w, k, bytes, byte_off;
    mr_uint32_t cur, end, addr0, addr1, off0, off1, val, acc;
    const mr_uint8_t *src;
    mr_bool_t src_in_mem;
    mr_uint8_t *dst;

    /* Check whether budget can cover at least one full iteration */
    max_iters = budget / (mr_uint32_t)desc->instr_count;
    if (max_iters == 0U) {
        return 0U;
    }

    switch (desc->kind) {
        /* DELAY: advance counter register directly */
        case LOOP_KIND_DELAY: {
            cur = CORE_REG_X(core, desc->r0);
            end = CORE_REG_X(core, desc->r1);
            if (desc->branch_funct3 == 0x1U) {
                if (cur == end) {
                    return 0U;
                }
                rem_iters
                    = (desc->u.idiom.off0 < 0) ? (cur - end) : (end - cur);
            } else if (desc->branch_funct3 == 0x4U) {
                if ((mr_int32_t)cur >= (mr_int32_t)end) {
                    return 0U;
                }
                rem_iters = end - cur;
            } else {
                if (cur >= end) {
                    return 0U;
                }
                rem_iters = end - cur;
            }
            n = (rem_iters < max_iters) ? rem_iters : max_iters;
            CORE_REG_X(core, desc->r0)
                = (desc->u.idiom.off0 < 0) ? (cur - n) : (cur + n);
            if (n == rem_iters) {
                CORE_REG_PC(core) = desc->branch_next;
            }
            return n * (mr_uint32_t)desc->instr_count;
        }
        /* FILL: bulk write constant value into aligned RAM range */
        case LOOP_KIND_FILL: {
            cur = CORE_REG_X(core, desc->r0);
            end = CORE_REG_X(core, desc->u.idiom.r2);
            rem_iters = loop_rem_iters_step(cur, end, desc->branch_funct3,
                                            desc->u.idiom.width_shift);
            if (rem_iters == 0U) {
                return 0U;
            }
            n = (rem_iters < max_iters) ? rem_iters : max_iters;
            w = 1U << desc->u.idiom.width_shift;
            addr0 = cur + (mr_uint32_t)desc->u.idiom.off0;
            off0 = CORE_ADDR_MEM(core, addr0);

            /* Validate alignment and RAM bounds */
            if (!CORE_ADDR_IS_ALIGN(addr0, w) || (off0 > core->mem_size)
                || (n
                    > ((core->mem_size - off0) >> desc->u.idiom.width_shift))) {
                return 0U;
            }

            /* Perform bulk store */
            dst = core->mem + off0;
            val = CORE_REG_X(core, desc->r1);
            if ((w == 1U) && (n >= LOOP_BULK_MIN_BYTES)) {
                mr_memset(dst, (int)(val & 0xffU), (mr_size_t)n);
            } else if (w == 1U) {
                for (k = 0U; k < n; k++) {
                    core_store8(dst + k, val);
                }
            } else if (w == 2U) {
                for (k = 0U; k < n; k++) {
                    core_store16(dst + (k << 1), val);
                }
            } else {
                for (k = 0U; k < n; k++) {
                    core_store32(dst + (k << 2), val);
                }
            }

            /* Commit pointer and exit PC */
            CORE_REG_X(core, desc->r0) = cur + (n << desc->u.idiom.width_shift);
            if (n == rem_iters) {
                CORE_REG_PC(core) = desc->branch_next;
            }
            return n * (mr_uint32_t)desc->instr_count;
        }
        /* SUM: bulk load and accumulate aligned RAM/ROM range */
        case LOOP_KIND_SUM: {
            cur = CORE_REG_X(core, desc->r0);
            end = CORE_REG_X(core, desc->u.idiom.r3);
            rem_iters = loop_rem_iters_step(cur, end, desc->branch_funct3,
                                            desc->u.idiom.width_shift);
            if (rem_iters == 0U) {
                return 0U;
            }
            n = (rem_iters < max_iters) ? rem_iters : max_iters;
            w = 1U << desc->u.idiom.width_shift;
            addr0 = cur + (mr_uint32_t)desc->u.idiom.off0;

            /* Validate alignment and source region bounds */
            if (!CORE_ADDR_IS_ALIGN(addr0, w)) {
                return 0U;
            }
            off0 = CORE_ADDR_MEM(core, addr0);
            if ((off0 <= core->mem_size)
                && (n <= ((core->mem_size - off0)
                          >> desc->u.idiom.width_shift))) {
                src = core->mem + off0;
            } else if ((addr0 <= image_size)
                       && (n <= ((image_size - addr0)
                                 >> desc->u.idiom.width_shift))) {
                src = image + addr0;
            } else {
                return 0U;
            }

            /* Accumulate elements */
            acc = CORE_REG_X(core, desc->u.idiom.r2);
            val = 0U;
            for (k = 0U; k < n; k++) {
                val = loop_load_elem(src + (k << desc->u.idiom.width_shift),
                                     desc->u.idiom.load_funct3);
                acc += val;
            }

            /* Commit last loaded value, accumulator, pointer and exit PC */
            CORE_REG_X(core, desc->r1) = val;
            CORE_REG_X(core, desc->u.idiom.r2) = acc;
            CORE_REG_X(core, desc->r0) = cur + (n << desc->u.idiom.width_shift);
            if (n == rem_iters) {
                CORE_REG_PC(core) = desc->branch_next;
            }
            return n * (mr_uint32_t)desc->instr_count;
        }
        /* COPY: bulk copy aligned source range into RAM */
        case LOOP_KIND_COPY: {
            cur = CORE_REG_X(core, desc->u.idiom.cmp_reg);
            end = CORE_REG_X(core, desc->u.idiom.r3);
            rem_iters = loop_rem_iters_step(cur, end, desc->branch_funct3,
                                            desc->u.idiom.width_shift);
            if (rem_iters == 0U) {
                return 0U;
            }
            n = (rem_iters < max_iters) ? rem_iters : max_iters;
            w = 1U << desc->u.idiom.width_shift;
            addr0
                = CORE_REG_X(core, desc->r0) + (mr_uint32_t)desc->u.idiom.off0;
            addr1
                = CORE_REG_X(core, desc->r1) + (mr_uint32_t)desc->u.idiom.off1;

            /* Validate alignment and destination RAM bounds */
            if (!CORE_ADDR_IS_ALIGN(addr0, w)
                || !CORE_ADDR_IS_ALIGN(addr1, w)) {
                return 0U;
            }
            off1 = CORE_ADDR_MEM(core, addr1);
            if ((off1 > core->mem_size)
                || (n
                    > ((core->mem_size - off1) >> desc->u.idiom.width_shift))) {
                return 0U;
            }
            dst = core->mem + off1;

            /* Validate source RAM/ROM bounds */
            off0 = CORE_ADDR_MEM(core, addr0);
            if ((off0 <= core->mem_size)
                && (n <= ((core->mem_size - off0)
                          >> desc->u.idiom.width_shift))) {
                src = core->mem + off0;
                src_in_mem = MR_TRUE;
            } else if ((addr0 <= image_size)
                       && (n <= ((image_size - addr0)
                                 >> desc->u.idiom.width_shift))) {
                src = image + addr0;
                src_in_mem = MR_FALSE;
            } else {
                return 0U;
            }

            /* Use mr_memcpy for large disjoint ranges; inline loop otherwise */
            bytes = n << desc->u.idiom.width_shift;
            if ((bytes >= LOOP_BULK_MIN_BYTES)
                && (!src_in_mem
                    || (((off1 >= off0) ? (off1 - off0) : (off0 - off1))
                        >= bytes))) {
                mr_memcpy(dst, src, (mr_size_t)bytes);
            } else {
                for (k = 0U; k < n; k++) {
                    byte_off = k << desc->u.idiom.width_shift;
                    if (w == 1U) {
                        core_store8(dst + byte_off, core_load8(src + byte_off));
                    } else if (w == 2U) {
                        core_store16(dst + byte_off,
                                     core_load16(src + byte_off));
                    } else {
                        core_store32(dst + byte_off,
                                     core_load32(src + byte_off));
                    }
                }
            }

            /* Commit last transferred value, pointers and exit PC */
            val = loop_load_elem(dst + ((n - 1U) << desc->u.idiom.width_shift),
                                 desc->u.idiom.load_funct3);
            CORE_REG_X(core, desc->u.idiom.r2) = val;
            CORE_REG_X(core, desc->r0) += bytes;
            CORE_REG_X(core, desc->r1) += bytes;
            if (n == rem_iters) {
                CORE_REG_PC(core) = desc->branch_next;
            }
            return n * (mr_uint32_t)desc->instr_count;
        }
        /* DECODED: execute pre-decoded micro-op block */
        case LOOP_KIND_DECODED: {
            return exec_loop_decoded(core, desc, uops, pc_offs, budget, irq_en);
        }
        default: {
            return 0U;
        }
    }
}

#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED)
MR_INLINE mr_uint32_t loop_pool_hash(mr_uint32_t image_hash,
                                     mr_uint32_t branch_pc) {
    mr_uint32_t key;

    /* Multiplicative Fibonacci hash to spread correlated image PCs */
    key = (branch_pc >> 2) ^ image_hash ^ (image_hash >> 16);
    return (key * 0x9e3779b1U) >> LOOP_POOL_HASH_SHIFT;
}

MR_INLINE loop_pool_entry_t *loop_pool_find_locked(const mr_uint8_t *image,
                                                   mr_size_t image_size,
                                                   mr_uint32_t image_hash,
                                                   mr_uint32_t branch_pc) {
    mr_uint32_t slot, step;
    loop_pool_entry_t *entry;
    mr_uint8_t val;

    /* Probe open-addressing index table (0 = empty, val = entry_idx + 1) */
    slot = loop_pool_hash(image_hash, branch_pc);
    for (step = 0U; step < LOOP_POOL_HASH_SIZE; step++) {
        val = __loop_pool.hash_idx[slot];
        if (val == 0U) {
            return MR_NULL;
        }
        entry = &__loop_pool.entries[val - 1U];
        if ((entry->desc.branch_pc == branch_pc)
            && (entry->image_hash == image_hash)
            && (entry->image_size == image_size) && (entry->image == image)) {
            return entry;
        }
        slot = (slot + 1U) & LOOP_POOL_HASH_MASK;
    }
    return MR_NULL;
}

static void loop_pool_hash_insert_locked(mr_uint32_t entry_idx,
                                         mr_uint32_t image_hash,
                                         mr_uint32_t branch_pc) {
    mr_uint32_t slot, step;

    /* Insert entry_idx + 1 into the first empty linear-probe slot */
    slot = loop_pool_hash(image_hash, branch_pc);
    for (step = 0U; step < LOOP_POOL_HASH_SIZE; step++) {
        if (__loop_pool.hash_idx[slot] == 0U) {
            __loop_pool.hash_idx[slot] = (mr_uint8_t)(entry_idx + 1U);
            return;
        }
        slot = (slot + 1U) & LOOP_POOL_HASH_MASK;
    }
}

static void loop_pool_hash_remove_locked(mr_uint32_t entry_idx) {
    mr_uint32_t slot, step, hole, cur, home;
    loop_pool_entry_t *entry;
    mr_uint8_t target, val;

    /* Locate entry in linear-probe chain */
    entry = &__loop_pool.entries[entry_idx];
    target = (mr_uint8_t)(entry_idx + 1U);
    slot = loop_pool_hash(entry->image_hash, entry->desc.branch_pc);
    hole = LOOP_POOL_HASH_SIZE;
    for (step = 0U; step < LOOP_POOL_HASH_SIZE; step++) {
        val = __loop_pool.hash_idx[slot];
        if (val == 0U) {
            return;
        }
        if (val == target) {
            hole = slot;
            break;
        }
        slot = (slot + 1U) & LOOP_POOL_HASH_MASK;
    }
    if (hole == LOOP_POOL_HASH_SIZE) {
        return;
    }

    /* Backward-shift subsequent entries that belong at or before hole */
    cur = (hole + 1U) & LOOP_POOL_HASH_MASK;
    while (MR_TRUE) {
        val = __loop_pool.hash_idx[cur];
        if (val == 0U) {
            __loop_pool.hash_idx[hole] = 0U;
            return;
        }
        entry = &__loop_pool.entries[val - 1U];
        home = loop_pool_hash(entry->image_hash, entry->desc.branch_pc);
        if (((cur - home) & LOOP_POOL_HASH_MASK)
            >= ((cur - hole) & LOOP_POOL_HASH_MASK)) {
            __loop_pool.hash_idx[hole] = val;
            hole = cur;
        }
        cur = (cur + 1U) & LOOP_POOL_HASH_MASK;
    }
}

static void loop_pool_record_miss(const mr_uint8_t *image, mr_size_t image_size,
                                  mr_uint32_t image_hash,
                                  mr_uint32_t branch_pc) {
    loop_miss_entry_t *miss;

    /* Record failed backward branch in ring buffer */
    miss = &__loop_pool.misses[__loop_pool.miss_idx];
    miss->image = image;
    miss->image_size = image_size;
    miss->image_hash = image_hash;
    miss->branch_pc = branch_pc;
    __loop_pool.miss_idx = (mr_uint8_t)((__loop_pool.miss_idx + 1U)
                                        % MR_CFG_XVM_RISCV32_LOOP_MISS_SIZE);
}

static mr_bool_t loop_pool_alloc_arena(mr_uint32_t need,
                                       mr_uint16_t *out_start) {
    mr_uint32_t start, end, i, e_start, e_end;
    loop_pool_entry_t *entry;

    /* Reject requests larger than the circular arena capacity */
    if (need > MR_CFG_XVM_RISCV32_LOOP_ARENA_SIZE) {
        return MR_FALSE;
    }

    /* Advance circular arena cursor, wrapping when tail cannot fit */
    start = (mr_uint32_t)__loop_pool.arena_head;
    if ((start + need) > MR_CFG_XVM_RISCV32_LOOP_ARENA_SIZE) {
        start = 0U;
    }
    end = start + need;

    /* Abort allocation if any pinned entry overlaps [start, end) */
    for (i = 0U; i < LOOP_POOL_SLOTS; i++) {
        entry = &__loop_pool.entries[i];
        if (!entry->valid || (entry->desc.kind != LOOP_KIND_DECODED)
            || (entry->desc.u.dec.body_count == 0U)) {
            continue;
        }
        e_start = (mr_uint32_t)entry->desc.u.dec.uop_start;
        e_end = e_start + (mr_uint32_t)entry->desc.u.dec.body_count;
        if ((e_start < end) && (e_end > start) && (entry->pin != 0U)) {
            return MR_FALSE;
        }
    }

    /* Invalidate unpinned entries whose micro-ops overlap [start, end) */
    for (i = 0U; i < LOOP_POOL_SLOTS; i++) {
        entry = &__loop_pool.entries[i];
        if (!entry->valid || (entry->desc.kind != LOOP_KIND_DECODED)
            || (entry->desc.u.dec.body_count == 0U)) {
            continue;
        }
        e_start = (mr_uint32_t)entry->desc.u.dec.uop_start;
        e_end = e_start + (mr_uint32_t)entry->desc.u.dec.body_count;
        if ((e_start < end) && (e_end > start)) {
            loop_pool_hash_remove_locked(i);
            entry->valid = 0U;
        }
    }

    __loop_pool.arena_head = (mr_uint16_t)end;
    *out_start = (mr_uint16_t)start;
    return MR_TRUE;
}

static loop_pool_entry_t *loop_pool_acquire(
    core_t *core, const mr_uint8_t *image, mr_size_t image_size,
    mr_uint32_t branch_pc, mr_uint32_t branch_len, mr_uint32_t branch_raw,
    mr_uint32_t loop_start) {
    mr_uint32_t i, idx, victim_idx, need;
    loop_pool_entry_t *entry;
    loop_miss_entry_t *miss;
    mr_uint16_t arena_start;
    mr_bool_t built;
    int irq_mask;

    /* Lookup hash index and negative miss ring under IRQ-safe spinlock */
    irq_mask = mr_spinlock_lock_irqsave(&__loop_pool.lock);
    entry
        = loop_pool_find_locked(image, image_size, core->image_hash, branch_pc);
    if (entry != MR_NULL) {
        entry->used = 1U;
        entry->pin++;
        mr_spinlock_unlock_irqrestore(&__loop_pool.lock, irq_mask);
        return entry;
    }
    for (i = 0U; i < MR_CFG_XVM_RISCV32_LOOP_MISS_SIZE; i++) {
        miss = &__loop_pool.misses[i];
        if ((miss->branch_pc == branch_pc) && (miss->image == image)
            && (miss->image_hash == core->image_hash)
            && (miss->image_size == image_size)) {
            loop_miss_filter_add(core, branch_pc);
            mr_spinlock_unlock_irqrestore(&__loop_pool.lock, irq_mask);
            return MR_NULL;
        }
    }
    mr_spinlock_unlock_irqrestore(&__loop_pool.lock, irq_mask);

    /* Acquire non-blocking build lock with interrupts enabled */
    if (!mr_spinlock_try_lock(&__loop_pool.build_lock)) {
        return MR_NULL;
    }

    /* Decode directly into static staging buffer in a single pass */
    built = loop_build_onepass(core, &__loop_pool.stage_desc,
                               __loop_pool.stage_uops, __loop_pool.stage_offs,
                               image, image_size, branch_pc, branch_len,
                               branch_raw, loop_start);
    if (!built) {
        irq_mask = mr_spinlock_lock_irqsave(&__loop_pool.lock);
        loop_pool_record_miss(image, image_size, core->image_hash, branch_pc);
        mr_spinlock_unlock_irqrestore(&__loop_pool.lock, irq_mask);
        mr_spinlock_unlock(&__loop_pool.build_lock);
        loop_miss_filter_add(core, branch_pc);
        return MR_NULL;
    }

    /* Commit staged block into fully-associative Clock pool and arena */
    irq_mask = mr_spinlock_lock_irqsave(&__loop_pool.lock);
    entry
        = loop_pool_find_locked(image, image_size, core->image_hash, branch_pc);
    if (entry != MR_NULL) {
        entry->used = 1U;
        entry->pin++;
        mr_spinlock_unlock_irqrestore(&__loop_pool.lock, irq_mask);
        mr_spinlock_unlock(&__loop_pool.build_lock);
        return entry;
    }

    /* Select an unpinned Clock victim across the full pool */
    victim_idx = LOOP_POOL_SLOTS;
    for (i = 0U; i < (LOOP_POOL_SLOTS * 2U); i++) {
        idx = (mr_uint32_t)__loop_pool.clock_hand;
        __loop_pool.clock_hand = (mr_uint8_t)((idx + 1U) % LOOP_POOL_SLOTS);
        if (__loop_pool.entries[idx].pin != 0U) {
            continue;
        }
        if (!__loop_pool.entries[idx].valid
            || (__loop_pool.entries[idx].used == 0U)) {
            victim_idx = idx;
            break;
        }
        __loop_pool.entries[idx].used = 0U;
    }
    if (victim_idx == LOOP_POOL_SLOTS) {
        mr_spinlock_unlock_irqrestore(&__loop_pool.lock, irq_mask);
        mr_spinlock_unlock(&__loop_pool.build_lock);
        return MR_NULL;
    }

    /* Allocate contiguous micro-ops in circular arena if needed */
    need = (__loop_pool.stage_desc.kind == LOOP_KIND_DECODED)
             ? (mr_uint32_t)__loop_pool.stage_desc.u.dec.body_count
             : 0U;
    arena_start = 0U;
    if (need > 0U) {
        if (!loop_pool_alloc_arena(need, &arena_start)) {
            mr_spinlock_unlock_irqrestore(&__loop_pool.lock, irq_mask);
            mr_spinlock_unlock(&__loop_pool.build_lock);
            return MR_NULL;
        }
        mr_memcpy(&__loop_pool.arena_uops[arena_start], __loop_pool.stage_uops,
                  (mr_size_t)(need * sizeof(loop_uop_t)));
        mr_memcpy(&__loop_pool.arena_offs[arena_start], __loop_pool.stage_offs,
                  (mr_size_t)need);
    }

    /* Remove old victim key if still valid, then populate and index */
    entry = &__loop_pool.entries[victim_idx];
    if (entry->valid) {
        loop_pool_hash_remove_locked(victim_idx);
    }
    entry->desc = __loop_pool.stage_desc;
    if (entry->desc.kind == LOOP_KIND_DECODED) {
        entry->desc.u.dec.uop_start = arena_start;
    }
    entry->image = image;
    entry->image_size = image_size;
    entry->image_hash = core->image_hash;
    entry->used = 1U;
    entry->valid = 1U;
    entry->pin = 1U;
    loop_pool_hash_insert_locked(victim_idx, core->image_hash, branch_pc);
    mr_spinlock_unlock_irqrestore(&__loop_pool.lock, irq_mask);
    mr_spinlock_unlock(&__loop_pool.build_lock);
    return entry;
}

MR_INLINE void loop_pool_release(loop_pool_entry_t *entry) {
    int irq_mask;

    /* Unpin shared pool entry under IRQ-safe spinlock */
    irq_mask = mr_spinlock_lock_irqsave(&__loop_pool.lock);
    entry->pin--;
    mr_spinlock_unlock_irqrestore(&__loop_pool.lock, irq_mask);
}
#else
MR_INLINE mr_bool_t loop_private_is_miss(const core_t *core,
                                         mr_uint32_t branch_pc) {
    mr_uint32_t i;

    /* Check private negative miss ring */
    for (i = 0U; i < MR_CFG_XVM_RISCV32_LOOP_MISS_SIZE; i++) {
        if (core->loop_miss_pc[i] == branch_pc) {
            return MR_TRUE;
        }
    }
    return MR_FALSE;
}

MR_INLINE void loop_private_record_miss(core_t *core, mr_uint32_t branch_pc) {
    /* Record failed backward branch without touching valid hot block */
    loop_miss_filter_add(core, branch_pc);
    core->loop_miss_pc[core->loop_miss_idx] = branch_pc;
    core->loop_miss_idx = (mr_uint8_t)((core->loop_miss_idx + 1U)
                                       % MR_CFG_XVM_RISCV32_LOOP_MISS_SIZE);
}
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED) */

LOOP_NOINLINE static mr_uint32_t exec_loop_accelerate(core_t *core,
                                                      mr_uint32_t branch_pc,
                                                      mr_uint32_t branch_raw,
                                                      mr_uint32_t budget) {
    mr_uint32_t branch_len, irq_en, done;
    const mr_uint8_t *image;
    mr_size_t image_size;
#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED)
    mr_uint32_t instr_count, loop_start, branch_next;
    loop_pool_entry_t *entry;
    mr_uint16_t uop_start;
    mr_uint8_t has_exit;
#else
    loop_private_slot_t *slot;
#if (MR_CFG_XVM_RISCV32_LOOP_PRIVATE_SLOTS > 1U)
    mr_uint32_t s;
#endif /* (MR_CFG_XVM_RISCV32_LOOP_PRIVATE_SLOTS > 1U) */
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED) */

#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED)
    /* Temporary cooldown skip for low-yield blocks without permanent ban */
    if ((core->low_yield_skip != 0U) && (core->low_yield_pc == branch_pc)) {
        core->low_yield_skip--;
        return 0U;
    }
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED) */

    /* Skip acceleration if an enabled interrupt is pending */
    irq_en = exec_irq_enabled(core);
    if ((irq_en != 0U) && (core->irq_dirty != 0U)) {
        return 0U;
    }

    /* Recover image bounds and closing branch length on cold entry */
    image = core->bin->image;
    image_size = core->bin->size;
#if defined(MR_USE_MODULE_XVM_RISCV32_C)
    branch_len = ((core_load16(image + branch_pc) & 0x3U) != 0x3U) ? 2U : 4U;
#else
    branch_len = 4U;
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_C) */

#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED)
    /* Acquire shared decoded loop entry */
    entry = loop_pool_acquire(core, image, image_size, branch_pc, branch_len,
                              branch_raw, CORE_REG_PC(core));
    if (!entry) {
        return 0U;
    }
    uop_start = (entry->desc.kind == LOOP_KIND_DECODED)
                  ? entry->desc.u.dec.uop_start
                  : 0U;
    has_exit = (entry->desc.kind == LOOP_KIND_DECODED)
                 ? entry->desc.u.dec.has_exit
                 : 0U;
    instr_count = (mr_uint32_t)entry->desc.instr_count;
    loop_start = entry->desc.loop_start;
    branch_next = entry->desc.branch_next;
    done = exec_loop_cached(
        core, &entry->desc, &__loop_pool.arena_uops[uop_start],
        &__loop_pool.arena_offs[uop_start], image, image_size, budget, irq_en);
    loop_pool_release(entry);

    /* Apply temporary cooldown to blocks that chronically side-exit early */
    if (has_exit != 0U) {
        if (done >= instr_count) {
            if (core->low_yield_pc == branch_pc) {
                core->low_yield_cnt = 0U;
                core->low_yield_skip = 0U;
            }
        } else if ((budget >= instr_count) && (CORE_REG_PC(core) != loop_start)
                   && (CORE_REG_PC(core) != branch_next)) {
            if (core->low_yield_pc == branch_pc) {
                if (core->low_yield_cnt >= (LOOP_LOW_YIELD_LIMIT - 1U)) {
                    core->low_yield_skip = LOOP_LOW_YIELD_COOLDOWN;
                } else {
                    core->low_yield_cnt++;
                }
            } else {
                core->low_yield_pc = branch_pc;
                core->low_yield_cnt = 1U;
                core->low_yield_skip = 0U;
            }
        }
    }
    return done;
#else
    /* Lookup private cache slot(s) and negative miss ring */
    slot = MR_NULL;
#if (MR_CFG_XVM_RISCV32_LOOP_PRIVATE_SLOTS > 1U)
    for (s = 0U; s < MR_CFG_XVM_RISCV32_LOOP_PRIVATE_SLOTS; s++) {
        if ((core->loop_slots[s].desc.branch_pc == branch_pc)
            && (core->loop_slots[s].desc.kind != LOOP_KIND_NONE)) {
            slot = &core->loop_slots[s];
            core->loop_slot_next = (mr_uint8_t)(s ^ 1U);
            break;
        }
    }
#else
    if ((core->loop_slots[0].desc.branch_pc == branch_pc)
        && (core->loop_slots[0].desc.kind != LOOP_KIND_NONE)) {
        slot = &core->loop_slots[0];
    }
#endif /* (MR_CFG_XVM_RISCV32_LOOP_PRIVATE_SLOTS > 1U) */
    if (!slot) {
        if (loop_private_is_miss(core, branch_pc)) {
            loop_miss_filter_add(core, branch_pc);
            return 0U;
        }
        if (!loop_can_decode(core, image, image_size, CORE_REG_PC(core),
                             branch_pc, branch_raw)) {
            loop_private_record_miss(core, branch_pc);
            return 0U;
        }
#if (MR_CFG_XVM_RISCV32_LOOP_PRIVATE_SLOTS > 1U)
        slot = &core->loop_slots[core->loop_slot_next];
#else
        slot = &core->loop_slots[0];
#endif /* (MR_CFG_XVM_RISCV32_LOOP_PRIVATE_SLOTS > 1U) */
        if (!loop_build_onepass(core, &slot->desc, slot->uops, slot->offs,
                                image, image_size, branch_pc, branch_len,
                                branch_raw, CORE_REG_PC(core))) {
            loop_private_record_miss(core, branch_pc);
            return 0U;
        }
#if (MR_CFG_XVM_RISCV32_LOOP_PRIVATE_SLOTS > 1U)
        core->loop_slot_next ^= 1U;
#endif /* (MR_CFG_XVM_RISCV32_LOOP_PRIVATE_SLOTS > 1U) */
    }
    done = exec_loop_cached(core, &slot->desc, slot->uops, slot->offs, image,
                            image_size, budget, irq_en);
    return done;
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_SHARED) */
}
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_ACCEL) */

MR_INLINE int instr_exec_branch(core_t *core, mr_uint32_t raw,
                                mr_uint32_t instr_len, mr_uint32_t *pc,
                                mr_uint32_t *cur_i, mr_uint32_t timer_at,
                                mr_bool_t debug_active) {
    mr_uint32_t funct3, rs1, rs2;
#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_ACCEL)
    mr_uint32_t imm, branch_pc;
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_ACCEL) */
    mr_bool_t taken;

#if !defined(MR_USE_MODULE_XVM_RISCV32_LOOP_ACCEL)
    (void)cur_i;
    (void)timer_at;
    (void)debug_active;
#endif /* !defined(MR_USE_MODULE_XVM_RISCV32_LOOP_ACCEL) */

    /* Decode B-type fields */
    funct3 = INSTR_FUNCT3(raw);
    rs1 = CORE_REG_X(core, INSTR_RS1(raw));
    rs2 = CORE_REG_X(core, INSTR_RS2(raw));

    /* Frequency-ordered branch evaluation without jump table */
    if (funct3 == 0x1U) {
        /* BNE */
        taken = (rs1 != rs2);
    } else if (funct3 == 0x0U) {
        /* BEQ */
        taken = (rs1 == rs2);
    } else if (funct3 == 0x4U) {
        /* BLT */
        taken = ((mr_int32_t)rs1 < (mr_int32_t)rs2);
    } else if (funct3 == 0x5U) {
        /* BGE */
        taken = ((mr_int32_t)rs1 >= (mr_int32_t)rs2);
    } else if (funct3 == 0x6U) {
        /* BLTU */
        taken = (rs1 < rs2);
    } else if (funct3 == 0x7U) {
        /* BGEU */
        taken = (rs1 >= rs2);
    } else {
        core->trap_mtval = raw;
        return -MR_XVM_EXCEPT_NR_ILLEGAL;
    }

    /* Update PC and try tight-loop acceleration on taken backward branches */
    if (taken) {
#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_ACCEL)
        imm = INSTR_B_IMM(raw);
        branch_pc = *pc;
        *pc = branch_pc + imm;
        if (((mr_int32_t)imm < 0) && !loop_miss_filter_hit(core, branch_pc)
            && !debug_active && (timer_at > (*cur_i + 1U))) {
            CORE_REG_PC(core) = *pc;
            *cur_i += exec_loop_accelerate(core, branch_pc, raw,
                                           timer_at - *cur_i - 1U);
            *pc = CORE_REG_PC(core);
        }
#else
        *pc += INSTR_B_IMM(raw);
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_ACCEL) */
    } else {
        *pc += instr_len;
    }
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
    if (rd != 0U) {
        CORE_REG_X(core, rd) = old_val;
    }
    CORE_REG_PC(core) += instr_len;
    return TRAP_TYPE_NONE;
}

MR_INLINE int instr_exec(core_t *core, mr_uint32_t raw, mr_uint32_t instr_len,
                         mr_uint32_t *pc, mr_uint32_t *cur_i,
                         mr_uint32_t *counter_base_i, mr_uint32_t *irq_en,
                         mr_uint32_t *timer_at, mr_uint32_t step,
                         mr_bool_t debug_active) {
    mr_uint32_t opcode, rd, target;
#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_ACCEL)
    mr_uint32_t branch_pc;
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_ACCEL) */
    int trap;

    /* Frequency-ordered fast dispatch for the 5 hottest RV32IM opcodes */
    opcode = INSTR_OPCODE(raw);
    if (opcode == 0b0010011U) {
        /* OP-IMM (I-type): immediate arithmetic */
        trap = instr_exec_op_imm(core, raw);
        if (trap == TRAP_TYPE_NONE) {
            *pc += instr_len;
        }
        return trap;
    }
    if (opcode == 0b0110011U) {
        /* OP (R-type): arithmetic and M extension */
        trap = instr_exec_op(core, raw);
        if (trap == TRAP_TYPE_NONE) {
            *pc += instr_len;
        }
        return trap;
    }
    if (opcode == 0b0000011U) {
        /* LOAD (I-type): LB/LH/LW/LBU/LHU */
        trap = instr_exec_load(core, raw, *pc, *cur_i, counter_base_i);
        if (trap == TRAP_TYPE_NONE) {
            *pc += instr_len;
        }
        return trap;
    }
    if (opcode == 0b0100011U) {
        /* STORE (S-type): SB/SH/SW */
        trap = instr_exec_store(core, raw, *pc, *cur_i, counter_base_i,
                                timer_at, step);
        if (trap == TRAP_TYPE_NONE) {
            *pc += instr_len;
        }
        return trap;
    }
    if (opcode == 0b1100011U) {
        /* BRANCH (B-type): BEQ/BNE/BLT/BGE/BLTU/BGEU */
        return instr_exec_branch(core, raw, instr_len, pc, cur_i, *timer_at,
                                 debug_active);
    }

    switch (opcode) {
        /* JAL (J-type) */
        case 0b1101111U: {
            rd = INSTR_RD(raw);
            if (rd != 0U) {
                CORE_REG_X(core, rd) = *pc + instr_len;
                *pc += INSTR_J_IMM(raw);
            } else {
#if defined(MR_USE_MODULE_XVM_RISCV32_LOOP_ACCEL)
                target = INSTR_J_IMM(raw);
                branch_pc = *pc;
                *pc = branch_pc + target;
                if (((mr_int32_t)target < 0)
                    && !loop_miss_filter_hit(core, branch_pc) && !debug_active
                    && (*timer_at > (*cur_i + 1U))) {
                    CORE_REG_PC(core) = *pc;
                    *cur_i += exec_loop_accelerate(core, branch_pc, raw,
                                                   *timer_at - *cur_i - 1U);
                    *pc = CORE_REG_PC(core);
                }
#else
                *pc += INSTR_J_IMM(raw);
#endif /* defined(MR_USE_MODULE_XVM_RISCV32_LOOP_ACCEL) */
            }
            return TRAP_TYPE_NONE;
        }
        /* JALR (I-type) */
        case 0b1100111U: {
            if (INSTR_FUNCT3(raw) != 0x0U) {
                core->trap_mtval = raw;
                return -MR_XVM_EXCEPT_NR_ILLEGAL;
            }
            target
                = (CORE_REG_X(core, INSTR_RS1(raw)) + INSTR_I_IMM(raw)) & ~1U;
            rd = INSTR_RD(raw);
            if (rd != 0U) {
                CORE_REG_X(core, rd) = *pc + instr_len;
            }
            *pc = target;
            return TRAP_TYPE_NONE;
        }
        /* LUI (U-type) */
        case 0b0110111U: {
            rd = INSTR_RD(raw);
            if (rd != 0U) {
                CORE_REG_X(core, rd) = INSTR_U_IMM(raw);
            }
            *pc += instr_len;
            return TRAP_TYPE_NONE;
        }
        /* AUIPC (U-type) */
        case 0b0010111U: {
            rd = INSTR_RD(raw);
            if (rd != 0U) {
                CORE_REG_X(core, rd) = *pc + INSTR_U_IMM(raw);
            }
            *pc += instr_len;
            return TRAP_TYPE_NONE;
        }
        /* SYSTEM (I-type): ECALL/EBREAK/MRET/CSR */
        case 0b1110011U: {
            CORE_REG_PC(core) = *pc;
            exec_counter_flush(core, *cur_i, counter_base_i);
            trap = instr_exec_system(core, raw, instr_len);
            *pc = CORE_REG_PC(core);
            if (trap == TRAP_TYPE_NONE) {
                *irq_en = exec_irq_enabled(core);
                *timer_at = exec_timer_at(core, *counter_base_i, step);
                if (*irq_en != 0U) {
                    core->irq_dirty = 1U;
                }
            }
            return trap;
        }
        /* FENCE */
        case 0b0001111U: {
            *pc += instr_len;
            return TRAP_TYPE_NONE;
        }
        default: {
            core->trap_mtval = raw;
            return -MR_XVM_EXCEPT_NR_ILLEGAL;
        }
    }
}

mr_xvm_return_t xvm_riscv32_exec_run(mr_xvm_t *xvm, core_t *core,
                                     mr_xvm_event_t *event, mr_uint32_t step) {
    mr_uint32_t i, counter_base_i, irq_en, timer_at, except_nr;
    mr_uint32_t pc, raw, instr_len;
    const mr_uint8_t *image;
    mr_bool_t debug_active;
    mr_size_t image_size;
    int trap;

#if defined(MR_USE_MODULE_XVM_DEBUG)
    debug_active = (xvm->debug.breaktable.num != 0U);
#else
    (void)xvm;
    debug_active = MR_FALSE;
#endif /* defined(MR_USE_MODULE_XVM_DEBUG) */

    /* Writeback syscall result */
    xvm_riscv32_syscall_writeback(core);

    /* Cache immutable image bounds, PC and interrupt schedule in locals */
    image = core->bin->image;
    image_size = core->bin->size;
    pc = CORE_REG_PC(core);
    counter_base_i = 0U;
    irq_en = exec_irq_enabled(core);
    timer_at = exec_timer_at(core, 0U, step);

    /* Fetch, decode and execute instructions */
    for (i = 0U; i < step; i++) {
        /* Check pending interrupts only when timer expires or state changed */
        if ((i >= timer_at) || ((irq_en != 0U) && (core->irq_dirty != 0U))) {
            CORE_REG_PC(core) = pc;
            exec_counter_flush(core, i, &counter_base_i);
            core->irq_dirty = 0U;
            if (xvm_riscv32_interrupt_enter(core)) {
                pc = CORE_REG_PC(core);
                counter_base_i = i + 1U;
                irq_en = 0U;
                timer_at = step;
                continue;
            }
            timer_at = exec_timer_at(core, counter_base_i, step);
        }

#if defined(MR_USE_MODULE_XVM_DEBUG)
        /* Fast-path check before calling cross-TU breakpoint matcher */
        if (debug_active && mr_xvm_debug_break_hit(xvm, pc)) {
            CORE_REG_PC(core) = pc;
            exec_counter_flush(core, i, &counter_base_i);
            event->except.nr = MR_XVM_EXCEPT_NR_BREAK;
            return MR_XVM_EXCEPT;
        }
#endif /* defined(MR_USE_MODULE_XVM_DEBUG) */

        /* Fetch instruction */
        trap = instr_fetch(core, image, image_size, pc, &raw, &instr_len);
        if (trap != 0) {
            CORE_REG_PC(core) = pc;
            exec_counter_flush(core, i, &counter_base_i);
            except_nr = (mr_uint32_t)-trap;

            /* Enter trap */
            if (xvm_riscv32_trap_enter(core, except_nr, core->trap_mtval)) {
                pc = CORE_REG_PC(core);
                counter_base_i = i + 1U;
                irq_en = 0U;
                timer_at = step;
                continue;
            }
            event->except.nr = except_nr;
            return MR_XVM_EXCEPT;
        }

        /* Decode and execute instruction */
        trap = instr_exec(core, raw, instr_len, &pc, &i, &counter_base_i,
                          &irq_en, &timer_at, step, debug_active);
        if (trap == 0) {
            continue;
        }

        /* Flush PC and retired counters before trap or syscall */
        CORE_REG_PC(core) = pc;
        exec_counter_flush(core, i, &counter_base_i);

        /* Exception */
        if (trap < 0) {
            except_nr = (mr_uint32_t)-trap;

            /* Enter trap(BREAK is handled by the host debugger) */
            if (except_nr != MR_XVM_EXCEPT_NR_BREAK) {
                if (xvm_riscv32_trap_enter(core, except_nr, core->trap_mtval)) {
                    pc = CORE_REG_PC(core);
                    counter_base_i = i + 1U;
                    irq_en = 0U;
                    timer_at = step;
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

    /* Flush PC, remaining retired instructions and record steps executed */
    CORE_REG_PC(core) = pc;
    exec_counter_flush(core, i, &counter_base_i);
    event->none.step = i;
    return MR_XVM_NONE;
}
#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */

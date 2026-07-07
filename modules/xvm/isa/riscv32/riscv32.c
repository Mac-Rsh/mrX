/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/modules/xvm/mr_xvm.h>
#if defined(MR_USE_MODULE_XVM_RISCV32)
#include <mrX/modules/xvm/isa/riscv32/core.h>
#include <mrX/libc/mr_string.h>

/* XVM RISCV32 operations */
static mr_err_t xvm_riscv32_load(mr_xvm_t *xvm);
static mr_xvm_return_t xvm_riscv32_execute(mr_xvm_t *xvm, mr_xvm_event_t *event,
                                           mr_uint32_t step);
static mr_ssize_t xvm_riscv32_mem_map(mr_xvm_t *xvm, mr_uint64_t addr,
                                      void **memory, mr_bool_t *writable);
static mr_err_t xvm_riscv32_reg_load(mr_xvm_t *xvm, const char *name,
                                     mr_uint64_t *val);
static mr_err_t xvm_riscv32_reg_store(mr_xvm_t *xvm, const char *name,
                                      mr_uint64_t val);
static mr_err_t xvm_riscv32_interrupt(mr_xvm_t *xvm, mr_uint32_t irq,
                                      mr_bool_t active);
static const mr_xvm_ops_t __xvm_riscv32_ops = {
    .load = xvm_riscv32_load,
    .execute = xvm_riscv32_execute,
    .mem_map = xvm_riscv32_mem_map,
    .reg_load = xvm_riscv32_reg_load,
    .reg_store = xvm_riscv32_reg_store,
    .interrupt = xvm_riscv32_interrupt,
};
static mr_xvm_isa_t __xvm_riscv32_isa
    = MR_XVM_ISA_INIT("riscv32", &__xvm_riscv32_ops);

/* RISCV32 core CSR map table */
static const name_addr_map_t csr_map_table[] = {
    {  "mstatus",   REG_CSR_MSTATUS},
    {     "misa",      REG_CSR_MISA},
    {      "mie",       REG_CSR_MIE},
    {    "mtvec",     REG_CSR_MTVEC},
    { "mscratch",  REG_CSR_MSCRATCH},
    {     "mepc",      REG_CSR_MEPC},
    {   "mcause",    REG_CSR_MCAUSE},
    {    "mtval",     REG_CSR_MTVAL},
    {      "mip",       REG_CSR_MIP},
    {   "mcycle",    REG_CSR_MCYCLE},
    {  "mcycleh",   REG_CSR_MCYCLEH},
    { "minstret",  REG_CSR_MINSTRET},
    {"minstreth", REG_CSR_MINSTRETH},
    {"mvendorid", REG_CSR_MVENDORID},
    {  "marchid",   REG_CSR_MARCHID},
    {   "mimpid",    REG_CSR_MIMPID},
    {  "mhartid",   REG_CSR_MHARTID},
};

static mr_err_t xvm_riscv32_load(mr_xvm_t *xvm) {
    mr_xvm_datamem_t *datamem;
    core_t *core;

    /* Get XVM data memory */
    datamem = MR_XVM_DATAMEM(xvm);
    core = (core_t *)datamem->data;

    /* Check parameter */
    if (datamem->size < sizeof(core_t)) {
        return -MR_ENOMEM;
    }

    /* Load RISCV32 core */
    mr_memset(core, 0, sizeof(*core));
    core->mem_size = datamem->size - sizeof(*core);
    mr_atomic_init(&core->intr.active, 0);
    core->regs.misa = 0x40001101;
    core->bin = MR_XVM_BIN(xvm);
    core->trap_mtval = 0;
    return 0;
}

MR_INLINE void syscall_writeback(core_t *core) {
    /* Check if syscall is pending */
    if (!core->syscall.pending) {
        return;
    }

    /* A0 = ret */
    CORE_REG_X(core, 10) = (mr_uint32_t)core->syscall.ret;
    core->syscall.pending = MR_FALSE;
}

MR_INLINE mr_bool_t trap_enter_cause(core_t *core, mr_uint32_t cause,
                                     mr_uint32_t mtval) {
    mr_uint32_t mtvec, mstatus;

    /* Get trap vector */
    mtvec = CORE_REG_MTVEC(core);
    if ((mtvec & (~0x3U)) == 0) {
        /* Must align to 4 bytes */
        return MR_FALSE;
    }

    /* Save trap context */
    CORE_REG_MEPC(core) = CORE_REG_PC(core) & (~0x3U);
    CORE_REG_MCAUSE(core) = cause;
    CORE_REG_MTVAL(core) = mtval;

    /* Save global interrupt status */
    mstatus = CORE_REG_MSTATUS(core);
    if (mstatus & REG_MSTATUS_MIE) {
        mstatus |= REG_MSTATUS_MPIE;
    } else {
        mstatus &= ~REG_MSTATUS_MPIE;
    }
    mstatus &= ~REG_MSTATUS_MPP_MASK;
    mstatus |= REG_MSTATUS_MPP_M;
    mstatus &= ~REG_MSTATUS_MIE;
    CORE_REG_MSTATUS(core) = mstatus;

    /* Clear LR */
    CORE_REG_LR_VALID(core) = 0;

    /* Switch to trap handler */
    if ((cause & 0x80000000u) && ((mtvec & 0x3u) == 1)) {
        CORE_REG_PC(core) = (mtvec & ~0x3u) + ((cause & 0x7fffffffu) << 2);
    } else {
        CORE_REG_PC(core) = mtvec & ~0x3u;
    }
    return MR_TRUE;
}

MR_INLINE void interrupt_update(core_t *core) {
    /* Update interrupt status */
    if ((mr_atomic_load(&core->intr.active) & core->intr.enable) != 0) {
        CORE_REG_MIP(core) |= REG_MIP_MEIP;
    } else {
        CORE_REG_MIP(core) &= ~REG_MIP_MEIP;
    }
}

MR_INLINE mr_bool_t interrupt_enter(core_t *core) {
    /* Check if global interrupt status is enabled */
    if (!(CORE_REG_MSTATUS(core) & REG_MSTATUS_MIE)) {
        return MR_FALSE;
    }

    /* Check if machine external interrupt is enabled */
    if ((!(CORE_REG_MIE(core) & REG_MIE_MEIE)) || (core->intr.enable == 0)) {
        return MR_FALSE;
    }

    /* Refresh pending interrupt only when it can be taken. */
    interrupt_update(core);

    /* Check if machine external interrupt is pending */
    if (!(CORE_REG_MIP(core) & REG_MIP_MEIP)) {
        return MR_FALSE;
    }

    /* Enter interrupt */
    return trap_enter_cause(core, REG_MCAUSE_MEI_INTR, 0);
}

MR_INLINE int instr_fetch(core_t *core, mr_uint32_t addr, mr_uint32_t *val) {
    /* Address misaligned */
    if (!CORE_ADDR_IS_ALIGN(addr, 4)) {
        core->trap_mtval = addr;
        return -MR_XVM_EXCEPT_NR_ALIGN;
    }

    /* Access instruction exception */
    addr = CORE_ADDR_BIN(core, addr);
    if (!CORE_ADDR_IS_IN_BIN(core, addr, 4)) {
        core->trap_mtval = addr;
        return -MR_XVM_EXCEPT_NR_ACCESS;
    }

    /* Fetch instruction */
    *val = *CORE_BIN_u4(core, addr);
    return 0;
}

MR_INLINE bcode_t instr_decode(instr_t instr) {
    switch (instr.fmt.opcode) {
        /* OP (R-type): arithmetic and M extension */
        case 0b0110011: {
            if (instr.R_type.funct7 == 0x01) {
                switch (instr.R_type.funct3) {
                    BCODE_DECODE_MAP(0x0, BCODE_MUL);
                    BCODE_DECODE_MAP(0x1, BCODE_MULH);
                    BCODE_DECODE_MAP(0x2, BCODE_MULHSU);
                    BCODE_DECODE_MAP(0x3, BCODE_MULHU);
                    BCODE_DECODE_MAP(0x4, BCODE_DIV);
                    BCODE_DECODE_MAP(0x5, BCODE_DIVU);
                    BCODE_DECODE_MAP(0x6, BCODE_REM);
                    BCODE_DECODE_MAP(0x7, BCODE_REMU);
                    BCODE_DECODE_DEFAULT(BCODE_UNKNOWN);
                }
            }
            switch (instr.R_type.funct3) {
                BCODE_DECODE_FUNC(0x0) {
                    switch (instr.R_type.funct7) {
                        BCODE_DECODE_MAP(0x00, BCODE_ADD);
                        BCODE_DECODE_MAP(0x20, BCODE_SUB);
                        BCODE_DECODE_DEFAULT(BCODE_UNKNOWN);
                    }
                }
                BCODE_DECODE_MAP(0x1, BCODE_SLL);
                BCODE_DECODE_MAP(0x2, BCODE_SLT);
                BCODE_DECODE_MAP(0x3, BCODE_SLTU);
                BCODE_DECODE_MAP(0x4, BCODE_XOR);
                BCODE_DECODE_FUNC(0x5) {
                    switch (instr.R_type.funct7) {
                        BCODE_DECODE_MAP(0x00, BCODE_SRL);
                        BCODE_DECODE_MAP(0x20, BCODE_SRA);
                        BCODE_DECODE_DEFAULT(BCODE_UNKNOWN);
                    }
                }
                BCODE_DECODE_MAP(0x6, BCODE_OR);
                BCODE_DECODE_MAP(0x7, BCODE_AND);
                BCODE_DECODE_DEFAULT(BCODE_UNKNOWN);
            }
        }
        /* OP-IMM (I-type): immediate arithmetic */
        case 0b0010011: {
            switch (instr.I_type.funct3) {
                BCODE_DECODE_MAP(0x0, BCODE_ADDI);
                BCODE_DECODE_FUNC(0x1) {
                    switch (instr.I_type.imm >> 5) {
                        BCODE_DECODE_MAP(0x00, BCODE_SLLI);
                        BCODE_DECODE_DEFAULT(BCODE_UNKNOWN);
                    }
                }
                BCODE_DECODE_MAP(0x2, BCODE_SLTI);
                BCODE_DECODE_MAP(0x3, BCODE_SLTIU);
                BCODE_DECODE_MAP(0x4, BCODE_XORI);
                BCODE_DECODE_FUNC(0x5) {
                    switch (instr.I_type.imm >> 5) {
                        BCODE_DECODE_MAP(0x00, BCODE_SRLI);
                        BCODE_DECODE_MAP(0x20, BCODE_SRAI);
                        BCODE_DECODE_DEFAULT(BCODE_UNKNOWN);
                    }
                }
                BCODE_DECODE_MAP(0x6, BCODE_ORI);
                BCODE_DECODE_MAP(0x7, BCODE_ANDI);
                BCODE_DECODE_DEFAULT(BCODE_UNKNOWN);
            }
        }
        /* LOAD (I-type) */
        case 0b0000011: {
            switch (instr.I_type.funct3) {
                BCODE_DECODE_MAP(0x0, BCODE_LB);
                BCODE_DECODE_MAP(0x1, BCODE_LH);
                BCODE_DECODE_MAP(0x2, BCODE_LW);
                BCODE_DECODE_MAP(0x4, BCODE_LBU);
                BCODE_DECODE_MAP(0x5, BCODE_LHU);
                BCODE_DECODE_DEFAULT(BCODE_UNKNOWN);
            }
        }
        /* STORE (S-type) */
        case 0b0100011: {
            switch (instr.S_type.funct3) {
                BCODE_DECODE_MAP(0x0, BCODE_SB);
                BCODE_DECODE_MAP(0x1, BCODE_SH);
                BCODE_DECODE_MAP(0x2, BCODE_SW);
                BCODE_DECODE_DEFAULT(BCODE_UNKNOWN);
            }
        }
        /* BRANCH (B-type) */
        case 0b1100011: {
            switch (instr.B_type.funct3) {
                BCODE_DECODE_MAP(0x0, BCODE_BEQ);
                BCODE_DECODE_MAP(0x1, BCODE_BNE);
                BCODE_DECODE_MAP(0x4, BCODE_BLT);
                BCODE_DECODE_MAP(0x5, BCODE_BGE);
                BCODE_DECODE_MAP(0x6, BCODE_BLTU);
                BCODE_DECODE_MAP(0x7, BCODE_BGEU);
                BCODE_DECODE_DEFAULT(BCODE_UNKNOWN);
            }
        }
        /* JAL (J-type) */
        BCODE_DECODE_MAP(0b1101111, BCODE_JAL);
        /* JALR (I-type) */
        BCODE_DECODE_MAP(0b1100111, BCODE_JALR);
        /* LUI (U-type) */
        BCODE_DECODE_MAP(0b0110111, BCODE_LUI);
        /* AUIPC (U-type) */
        BCODE_DECODE_MAP(0b0010111, BCODE_AUIPC);
        /* AMO (R-type): A extension */
        case 0b0101111: {
            if (instr.R_type.funct3 != 0x2) {
                return BCODE_UNKNOWN;
            }
            switch (instr.R_type.funct7 >> 2) {
                BCODE_DECODE_FUNC(0x02) {
                    if (instr.R_type.rs2 == 0) {
                        return BCODE_LR_W;
                    }
                    return BCODE_UNKNOWN;
                }
                BCODE_DECODE_MAP(0x03, BCODE_SC_W);
                BCODE_DECODE_MAP(0x01, BCODE_AMOSWAP_W);
                BCODE_DECODE_MAP(0x00, BCODE_AMOADD_W);
                BCODE_DECODE_MAP(0x04, BCODE_AMOXOR_W);
                BCODE_DECODE_MAP(0x0c, BCODE_AMOAND_W);
                BCODE_DECODE_MAP(0x08, BCODE_AMOOR_W);
                BCODE_DECODE_MAP(0x10, BCODE_AMOMIN_W);
                BCODE_DECODE_MAP(0x14, BCODE_AMOMAX_W);
                BCODE_DECODE_MAP(0x18, BCODE_AMOMINU_W);
                BCODE_DECODE_MAP(0x1c, BCODE_AMOMAXU_W);
                BCODE_DECODE_DEFAULT(BCODE_UNKNOWN);
            }
        }
        /* SYSTEM (I-type): ECALL, EBREAK, MRET, CSR */
        case 0b1110011: {
            switch (instr.I_type.funct3) {
                BCODE_DECODE_FUNC(0x0) {
                    switch (instr.I_type.imm) {
                        BCODE_DECODE_MAP(0x0, BCODE_ECALL);
                        BCODE_DECODE_MAP(0x1, BCODE_EBREAK);
                        BCODE_DECODE_MAP(0x302, BCODE_MRET);
                        BCODE_DECODE_DEFAULT(BCODE_UNKNOWN);
                    }
                }
                BCODE_DECODE_MAP(0x1, BCODE_CSRRW);
                BCODE_DECODE_MAP(0x2, BCODE_CSRRS);
                BCODE_DECODE_MAP(0x3, BCODE_CSRRC);
                BCODE_DECODE_MAP(0x5, BCODE_CSRRWI);
                BCODE_DECODE_MAP(0x6, BCODE_CSRRSI);
                BCODE_DECODE_MAP(0x7, BCODE_CSRRCI);
                BCODE_DECODE_DEFAULT(BCODE_UNKNOWN);
            }
        }
        /* FENCE */
        BCODE_DECODE_MAP(0b0001111, BCODE_FENCE);
        BCODE_DECODE_DEFAULT(BCODE_UNKNOWN);
    }
}

static mr_uint32_t trap_except_cause(mr_uint32_t except_nr) {
    switch (except_nr) {
        case MR_XVM_EXCEPT_NR_ALIGN: {
            return 0;
        }
        case MR_XVM_EXCEPT_NR_OVERFLOW: {
            return 1;
        }
        case MR_XVM_EXCEPT_NR_ILLEGAL: {
            return 2;
        }
        case MR_XVM_EXCEPT_NR_BREAK: {
            return 3;
        }
        case MR_XVM_EXCEPT_NR_ACCESS: {
            return 7;
        }
        default: {
            return 2;
        }
    }
}

MR_INLINE mr_bool_t trap_enter(core_t *core, mr_uint32_t except_nr,
                            mr_uint32_t mtval) {
    /* Enter trap handler */
    return trap_enter_cause(core, trap_except_cause(except_nr), mtval);
}

static int mmio_intr_load(core_t *core, mr_uint32_t addr, mr_uint32_t *val) {
    switch (addr) {
        case CORE_MMIO_INTR_PENDING: {
            /* Only read enabled interrupts */
            *val = (mr_uint32_t)mr_atomic_load(&core->intr.active)
                 & core->intr.enable;
            break;
        }
        case CORE_MMIO_INTR_ENABLE: {
            *val = core->intr.enable;
            break;
        }
        default: {
            return -MR_XVM_EXCEPT_NR_ACCESS;
        }
    }
    return 0;
}

static int mmio_load(core_t *core, mr_uint32_t addr, mr_uint32_t *val) {
    /* Load MMIO */
    if ((addr >= CORE_MMIO_INTR_BASE) && (addr < CORE_MMIO_INTR_END)) {
        return mmio_intr_load(core, addr, val);
    } else {
        return -MR_XVM_EXCEPT_NR_ACCESS;
    }
}

static int mmio_intr_store(core_t *core, mr_uint32_t addr, mr_uint32_t val) {
    switch (addr) {
        case CORE_MMIO_INTR_PENDING: {
            /* Clear set interrupts */
            mr_atomic_fetch_and(&core->intr.active, ~val);
            break;
        }
        case CORE_MMIO_INTR_ENABLE: {
            /* Set/unset interrupts */
            core->intr.enable = val;
            break;
        }
        default: {
            return -MR_XVM_EXCEPT_NR_ACCESS;
        }
    }
    return 0;
}

static int mmio_store(core_t *core, mr_uint32_t addr, mr_uint32_t val) {
    /* Store MMIO */
    if ((addr >= CORE_MMIO_INTR_BASE) && (addr < CORE_MMIO_INTR_END)) {
        return mmio_intr_store(core, addr, val);
    } else {
        return -MR_XVM_EXCEPT_NR_ACCESS;
    }
}

static int csr_load(core_t *core, mr_uint32_t addr, mr_uint32_t *val) {
    switch (addr) {
        case REG_CSR_MSTATUS: {
            *val = CORE_REG_MSTATUS(core);
            break;
        }
        case REG_CSR_MISA: {
            *val = CORE_REG_MISA(core);
            break;
        }
        case REG_CSR_MIE: {
            *val = CORE_REG_MIE(core);
            break;
        }
        case REG_CSR_MTVEC: {
            *val = CORE_REG_MTVEC(core);
            break;
        }
        case REG_CSR_MSCRATCH: {
            *val = CORE_REG_MSCRATCH(core);
            break;
        }
        case REG_CSR_MEPC: {
            *val = CORE_REG_MEPC(core);
            break;
        }
        case REG_CSR_MCAUSE: {
            *val = CORE_REG_MCAUSE(core);
            break;
        }
        case REG_CSR_MTVAL: {
            *val = CORE_REG_MTVAL(core);
            break;
        }
        case REG_CSR_MIP: {
            interrupt_update(core);
            *val = CORE_REG_MIP(core);
            break;
        }
        case REG_CSR_MCYCLE:
        case REG_CSR_CYCLE: {
            *val = CORE_REG_MCYCLE(core);
            break;
        }
        case REG_CSR_MCYCLEH:
        case REG_CSR_CYCLEH: {
            *val = CORE_REG_MCYCLEH(core);
            break;
        }
        case REG_CSR_MINSTRET:
        case REG_CSR_INSTRET: {
            *val = CORE_REG_MINSTRET(core);
            break;
        }
        case REG_CSR_MINSTRETH:
        case REG_CSR_INSTRETH: {
            *val = CORE_REG_MINSTRETH(core);
            break;
        }
        case REG_CSR_MVENDORID:
        case REG_CSR_MARCHID:
        case REG_CSR_MIMPID:
        case REG_CSR_MHARTID: {
            *val = 0;
            break;
        }
        default: {
            return -MR_XVM_EXCEPT_NR_ILLEGAL;
        }
    }
    return 0;
}

static int csr_store(core_t *core, mr_uint32_t addr, mr_uint32_t val) {
    switch (addr) {
        case REG_CSR_MSTATUS: {
            CORE_REG_MSTATUS(core) = val;
            break;
        }
        case REG_CSR_MISA: {
            CORE_REG_MISA(core) = val;
            break;
        }
        case REG_CSR_MIE: {
            CORE_REG_MIE(core) = val;
            break;
        }
        case REG_CSR_MTVEC: {
            CORE_REG_MTVEC(core) = val;
            break;
        }
        case REG_CSR_MSCRATCH: {
            CORE_REG_MSCRATCH(core) = val;
            break;
        }
        case REG_CSR_MEPC: {
            CORE_REG_MEPC(core) = val;
            break;
        }
        case REG_CSR_MCAUSE: {
            CORE_REG_MCAUSE(core) = val;
            break;
        }
        case REG_CSR_MTVAL: {
            CORE_REG_MTVAL(core) = val;
            break;
        }
        case REG_CSR_MIP: {
            CORE_REG_MIP(core) = val;
            interrupt_update(core);
            break;
        }
        case REG_CSR_MCYCLE:
        case REG_CSR_CYCLE: {
            CORE_REG_MCYCLE(core) = val;
            break;
        }
        case REG_CSR_MCYCLEH:
        case REG_CSR_CYCLEH: {
            CORE_REG_MCYCLEH(core) = val;
            break;
        }
        case REG_CSR_MINSTRET:
        case REG_CSR_INSTRET: {
            CORE_REG_MINSTRET(core) = val;
            break;
        }
        case REG_CSR_MINSTRETH:
        case REG_CSR_INSTRETH: {
            CORE_REG_MINSTRETH(core) = val;
            break;
        }
        case REG_CSR_MVENDORID:
        case REG_CSR_MARCHID:
        case REG_CSR_MIMPID:
        case REG_CSR_MHARTID: {
            break;
        }
        default: {
            return -MR_XVM_EXCEPT_NR_ILLEGAL;
        }
    }
    return 0;
}

MR_INLINE int bcode_instr_exec(core_t *core, bcode_t bcode, instr_t instr) {
    mr_uint32_t tmp0u4, tmp1u4, tmp2u4;
    mr_uint64_t tmp0u8, tmp1u8;
    int ret;

    BCODE_EXECUTE_TO(bcode) {
        BCODE_EXECUTE_FUNC(BCODE_UNKNOWN) {
            BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ILLEGAL);
        }
        /* ---- OP (R-type) ---- */
        BCODE_EXECUTE_FUNC(BCODE_ADD) {
            CORE_REG_X(core, instr.R_type.rd)
                = CORE_REG_X(core, instr.R_type.rs1)
                + CORE_REG_X(core, instr.R_type.rs2);
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_SUB) {
            CORE_REG_X(core, instr.R_type.rd)
                = CORE_REG_X(core, instr.R_type.rs1)
                - CORE_REG_X(core, instr.R_type.rs2);
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_XOR) {
            CORE_REG_X(core, instr.R_type.rd)
                = CORE_REG_X(core, instr.R_type.rs1)
                ^ CORE_REG_X(core, instr.R_type.rs2);
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_OR) {
            CORE_REG_X(core, instr.R_type.rd)
                = CORE_REG_X(core, instr.R_type.rs1)
                | CORE_REG_X(core, instr.R_type.rs2);
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_AND) {
            CORE_REG_X(core, instr.R_type.rd)
                = CORE_REG_X(core, instr.R_type.rs1)
                & CORE_REG_X(core, instr.R_type.rs2);
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_SLL) {
            CORE_REG_X(core, instr.R_type.rd)
                = CORE_REG_X(core, instr.R_type.rs1)
               << (CORE_REG_X(core, instr.R_type.rs2) & 0x1f);
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_SRL) {
            CORE_REG_X(core, instr.R_type.rd)
                = CORE_REG_X(core, instr.R_type.rs1)
               >> (CORE_REG_X(core, instr.R_type.rs2) & 0x1f);
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_SRA) {
            CORE_REG_X(core, instr.R_type.rd)
                = (mr_uint32_t)((mr_int32_t)CORE_REG_X(core, instr.R_type.rs1)
                                >> (CORE_REG_X(core, instr.R_type.rs2) & 0x1f));
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_SLT) {
            CORE_REG_X(core, instr.R_type.rd)
                = (mr_int32_t)CORE_REG_X(core, instr.R_type.rs1)
                < (mr_int32_t)CORE_REG_X(core, instr.R_type.rs2);
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_SLTU) {
            CORE_REG_X(core, instr.R_type.rd)
                = CORE_REG_X(core, instr.R_type.rs1)
                < CORE_REG_X(core, instr.R_type.rs2);
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        /* ---- OP-IMM (I-type) ---- */
        BCODE_EXECUTE_FUNC(BCODE_ADDI) {
            tmp0u4 = INSTR_I_TYPE_IMM(instr);
            CORE_REG_X(core, instr.I_type.rd)
                = CORE_REG_X(core, instr.I_type.rs1) + tmp0u4;
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_XORI) {
            tmp0u4 = INSTR_I_TYPE_IMM(instr);
            CORE_REG_X(core, instr.I_type.rd)
                = CORE_REG_X(core, instr.I_type.rs1) ^ tmp0u4;
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_ORI) {
            tmp0u4 = INSTR_I_TYPE_IMM(instr);
            CORE_REG_X(core, instr.I_type.rd)
                = CORE_REG_X(core, instr.I_type.rs1) | tmp0u4;
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_ANDI) {
            tmp0u4 = INSTR_I_TYPE_IMM(instr);
            CORE_REG_X(core, instr.I_type.rd)
                = CORE_REG_X(core, instr.I_type.rs1) & tmp0u4;
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_SLLI) {
            CORE_REG_X(core, instr.I_type.rd)
                = CORE_REG_X(core, instr.I_type.rs1)
               << (instr.I_type.imm & 0x1f);
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_SRLI) {
            CORE_REG_X(core, instr.I_type.rd)
                = CORE_REG_X(core, instr.I_type.rs1)
               >> (instr.I_type.imm & 0x1f);
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_SRAI) {
            CORE_REG_X(core, instr.I_type.rd)
                = (mr_uint32_t)((mr_int32_t)CORE_REG_X(core, instr.I_type.rs1)
                                >> (instr.I_type.imm & 0x1f));
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_SLTI) {
            tmp0u4 = INSTR_I_TYPE_IMM(instr);
            CORE_REG_X(core, instr.I_type.rd)
                = (mr_int32_t)CORE_REG_X(core, instr.I_type.rs1)
                < (mr_int32_t)tmp0u4;
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_SLTIU) {
            tmp0u4 = INSTR_I_TYPE_IMM(instr);
            CORE_REG_X(core, instr.I_type.rd)
                = CORE_REG_X(core, instr.I_type.rs1) < tmp0u4;
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        /* ---- LOAD ---- */
        BCODE_EXECUTE_FUNC(BCODE_LB) {
            tmp0u4 = INSTR_I_TYPE_IMM(instr);
            tmp0u4 = CORE_REG_X(core, instr.I_type.rs1) + tmp0u4;
            tmp1u4 = CORE_ADDR_BIN(core, tmp0u4);
            tmp2u4 = CORE_ADDR_MEM(core, tmp0u4);
            if (CORE_ADDR_IS_IN_BIN(core, tmp1u4, 1)) {
                CORE_REG_X(core, instr.I_type.rd)
                    = (mr_uint32_t)*CORE_BIN_i1(core, tmp1u4);
            } else if (CORE_ADDR_IS_IN_MEM(core, tmp2u4, 1)) {
                CORE_REG_X(core, instr.I_type.rd)
                    = (mr_uint32_t)*CORE_MEM_i1(core, tmp2u4);
            } else {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ACCESS);
            }
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_LH) {
            tmp0u4 = INSTR_I_TYPE_IMM(instr);
            tmp0u4 = CORE_REG_X(core, instr.I_type.rs1) + tmp0u4;
            if (!CORE_ADDR_IS_ALIGN(tmp0u4, 2)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ALIGN);
            }
            tmp1u4 = CORE_ADDR_BIN(core, tmp0u4);
            tmp2u4 = CORE_ADDR_MEM(core, tmp0u4);
            if (CORE_ADDR_IS_IN_BIN(core, tmp1u4, 2)) {
                CORE_REG_X(core, instr.I_type.rd)
                    = (mr_uint32_t)*CORE_BIN_i2(core, tmp1u4);
            } else if (CORE_ADDR_IS_IN_MEM(core, tmp2u4, 2)) {
                CORE_REG_X(core, instr.I_type.rd)
                    = (mr_uint32_t)*CORE_MEM_i2(core, tmp2u4);
            } else {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ACCESS);
            }
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_LW) {
            tmp0u4 = INSTR_I_TYPE_IMM(instr);
            tmp0u4 = CORE_REG_X(core, instr.I_type.rs1) + tmp0u4;
            if (!CORE_ADDR_IS_ALIGN(tmp0u4, 4)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ALIGN);
            }
            tmp1u4 = CORE_ADDR_BIN(core, tmp0u4);
            tmp2u4 = CORE_ADDR_MEM(core, tmp0u4);
            if (CORE_ADDR_IS_IN_BIN(core, tmp1u4, 4)) {
                CORE_REG_X(core, instr.I_type.rd)
                    = (mr_uint32_t)*CORE_BIN_i4(core, tmp1u4);
            } else if (CORE_ADDR_IS_IN_MEM(core, tmp2u4, 4)) {
                CORE_REG_X(core, instr.I_type.rd)
                    = (mr_uint32_t)*CORE_MEM_i4(core, tmp2u4);
            } else if (CORE_ADDR_IS_IN_MMIO(core, tmp0u4, 4)) {
                ret = mmio_load(core, tmp0u4, &tmp1u4);
                if (ret != 0) {
                    core->trap_mtval = tmp0u4;
                    BCODE_EXECUTE_EXIT(ret);
                }
                CORE_REG_X(core, instr.I_type.rd) = tmp1u4;
            } else {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ACCESS);
            }
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_LBU) {
            tmp0u4 = INSTR_I_TYPE_IMM(instr);
            tmp0u4 = CORE_REG_X(core, instr.I_type.rs1) + tmp0u4;
            tmp1u4 = CORE_ADDR_BIN(core, tmp0u4);
            tmp2u4 = CORE_ADDR_MEM(core, tmp0u4);
            if (CORE_ADDR_IS_IN_BIN(core, tmp1u4, 1)) {
                CORE_REG_X(core, instr.I_type.rd)
                    = (mr_uint32_t)*CORE_BIN_u1(core, tmp1u4);
            } else if (CORE_ADDR_IS_IN_MEM(core, tmp2u4, 1)) {
                CORE_REG_X(core, instr.I_type.rd)
                    = (mr_uint32_t)*CORE_MEM_u1(core, tmp2u4);
            } else {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ACCESS);
            }
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_LHU) {
            tmp0u4 = INSTR_I_TYPE_IMM(instr);
            tmp0u4 = CORE_REG_X(core, instr.I_type.rs1) + tmp0u4;
            if (!CORE_ADDR_IS_ALIGN(tmp0u4, 2)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ALIGN);
            }
            tmp1u4 = CORE_ADDR_BIN(core, tmp0u4);
            tmp2u4 = CORE_ADDR_MEM(core, tmp0u4);
            if (CORE_ADDR_IS_IN_BIN(core, tmp1u4, 2)) {
                CORE_REG_X(core, instr.I_type.rd)
                    = (mr_uint32_t)*CORE_BIN_u2(core, tmp1u4);
            } else if (CORE_ADDR_IS_IN_MEM(core, tmp2u4, 2)) {
                CORE_REG_X(core, instr.I_type.rd)
                    = (mr_uint32_t)*CORE_MEM_u2(core, tmp2u4);
            } else {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ACCESS);
            }
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        /* ---- STORE ---- */
        BCODE_EXECUTE_FUNC(BCODE_SB) {
            tmp0u4 = INSTR_S_TYPE_IMM(instr);
            tmp0u4 = CORE_REG_X(core, instr.S_type.rs1) + tmp0u4;
            tmp1u4 = CORE_ADDR_MEM(core, tmp0u4);
            if (CORE_ADDR_IS_IN_MEM(core, tmp1u4, 1)) {
                *CORE_MEM_i1(core, tmp1u4)
                    = (mr_int8_t)CORE_REG_X(core, instr.S_type.rs2);
            } else {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ACCESS);
            }
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_SH) {
            tmp0u4 = INSTR_S_TYPE_IMM(instr);
            tmp0u4 = CORE_REG_X(core, instr.S_type.rs1) + tmp0u4;
            if (!CORE_ADDR_IS_ALIGN(tmp0u4, 2)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ALIGN);
            }
            tmp1u4 = CORE_ADDR_MEM(core, tmp0u4);
            if (CORE_ADDR_IS_IN_MEM(core, tmp1u4, 2)) {
                *CORE_MEM_i2(core, tmp1u4)
                    = (mr_int16_t)CORE_REG_X(core, instr.S_type.rs2);
            } else {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ACCESS);
            }
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_SW) {
            tmp0u4 = INSTR_S_TYPE_IMM(instr);
            tmp0u4 = CORE_REG_X(core, instr.S_type.rs1) + tmp0u4;
            if (!CORE_ADDR_IS_ALIGN(tmp0u4, 4)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ALIGN);
            }
            tmp1u4 = CORE_ADDR_MEM(core, tmp0u4);
            if (CORE_ADDR_IS_IN_MEM(core, tmp1u4, 4)) {
                *CORE_MEM_i4(core, tmp1u4)
                    = (mr_int32_t)CORE_REG_X(core, instr.S_type.rs2);
            } else if (CORE_ADDR_IS_IN_MMIO(core, tmp0u4, 4)) {
                ret = mmio_store(core, tmp0u4,
                                 CORE_REG_X(core, instr.S_type.rs2));
                if (ret != 0) {
                    core->trap_mtval = tmp0u4;
                    BCODE_EXECUTE_EXIT(ret);
                }
            } else {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ACCESS);
            }
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        /* ---- BRANCH ---- */
        BCODE_EXECUTE_FUNC(BCODE_BEQ) {
            tmp0u4 = INSTR_B_TYPE_IMM(instr);
            if (CORE_REG_X(core, instr.B_type.rs1)
                == CORE_REG_X(core, instr.B_type.rs2)) {
                CORE_REG_PC(core) += tmp0u4;
            } else {
                CORE_REG_PC(core) += 4;
            }
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_BNE) {
            tmp0u4 = INSTR_B_TYPE_IMM(instr);
            if (CORE_REG_X(core, instr.B_type.rs1)
                != CORE_REG_X(core, instr.B_type.rs2)) {
                CORE_REG_PC(core) += tmp0u4;
            } else {
                CORE_REG_PC(core) += 4;
            }
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_BLT) {
            tmp0u4 = INSTR_B_TYPE_IMM(instr);
            if ((mr_int32_t)CORE_REG_X(core, instr.B_type.rs1)
                < (mr_int32_t)CORE_REG_X(core, instr.B_type.rs2)) {
                CORE_REG_PC(core) += tmp0u4;
            } else {
                CORE_REG_PC(core) += 4;
            }
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_BGE) {
            tmp0u4 = INSTR_B_TYPE_IMM(instr);
            if ((mr_int32_t)CORE_REG_X(core, instr.B_type.rs1)
                >= (mr_int32_t)CORE_REG_X(core, instr.B_type.rs2)) {
                CORE_REG_PC(core) += tmp0u4;
            } else {
                CORE_REG_PC(core) += 4;
            }
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_BLTU) {
            tmp0u4 = INSTR_B_TYPE_IMM(instr);
            if (CORE_REG_X(core, instr.B_type.rs1)
                < CORE_REG_X(core, instr.B_type.rs2)) {
                CORE_REG_PC(core) += tmp0u4;
            } else {
                CORE_REG_PC(core) += 4;
            }
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_BGEU) {
            tmp0u4 = INSTR_B_TYPE_IMM(instr);
            if (CORE_REG_X(core, instr.B_type.rs1)
                >= CORE_REG_X(core, instr.B_type.rs2)) {
                CORE_REG_PC(core) += tmp0u4;
            } else {
                CORE_REG_PC(core) += 4;
            }
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        /* ---- JUMP ---- */
        BCODE_EXECUTE_FUNC(BCODE_JAL) {
            tmp0u4 = INSTR_J_TYPE_IMM(instr);
            CORE_REG_X(core, instr.J_type.rd) = CORE_REG_PC(core) + 4;
            CORE_REG_PC(core) += tmp0u4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_JALR) {
            tmp0u4 = INSTR_I_TYPE_IMM(instr);
            tmp1u4 = (CORE_REG_X(core, instr.I_type.rs1) + tmp0u4) & ~1u;
            CORE_REG_X(core, instr.I_type.rd) = CORE_REG_PC(core) + 4;
            CORE_REG_PC(core) = tmp1u4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        /* ---- Upper immediate ---- */
        BCODE_EXECUTE_FUNC(BCODE_LUI) {
            CORE_REG_X(core, instr.U_type.rd)
                = (mr_uint32_t)(instr.U_type.imm << 12);
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_AUIPC) {
            CORE_REG_X(core, instr.U_type.rd)
                = CORE_REG_PC(core) + (mr_uint32_t)(instr.U_type.imm << 12);
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        /* ---- M extension ---- */
        BCODE_EXECUTE_FUNC(BCODE_MUL) {
            CORE_REG_X(core, instr.R_type.rd)
                = CORE_REG_X(core, instr.R_type.rs1)
                * CORE_REG_X(core, instr.R_type.rs2);
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_MULH) {
            tmp0u8
                = (mr_uint64_t)(mr_int32_t)CORE_REG_X(core, instr.R_type.rs1);
            tmp1u8
                = (mr_uint64_t)(mr_int32_t)CORE_REG_X(core, instr.R_type.rs2);
            CORE_REG_X(core, instr.R_type.rd)
                = (mr_uint32_t)((((mr_int64_t)tmp0u8) * ((mr_int64_t)tmp1u8))
                                >> 32);
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_MULHSU) {
            tmp0u8
                = (mr_uint64_t)(mr_int32_t)CORE_REG_X(core, instr.R_type.rs1);
            tmp1u8 = (mr_uint64_t)CORE_REG_X(core, instr.R_type.rs2);
            CORE_REG_X(core, instr.R_type.rd)
                = (mr_uint32_t)((((mr_int64_t)tmp0u8) * ((mr_int64_t)tmp1u8))
                                >> 32);
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_MULHU) {
            tmp0u8 = (mr_uint64_t)CORE_REG_X(core, instr.R_type.rs1);
            tmp1u4 = CORE_REG_X(core, instr.R_type.rs2);
            CORE_REG_X(core, instr.R_type.rd)
                = (mr_uint32_t)((tmp0u8 * (mr_uint64_t)tmp1u4) >> 32);
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_DIV) {
            tmp0u4 = CORE_REG_X(core, instr.R_type.rs1);
            tmp1u4 = CORE_REG_X(core, instr.R_type.rs2);
            if ((tmp0u4 == 0x80000000) && (((mr_int32_t)tmp1u4) == (-1))) {
                CORE_REG_X(core, instr.R_type.rd) = 0x80000000;
            } else if (tmp1u4 != 0) {
                CORE_REG_X(core, instr.R_type.rd)
                    = (mr_uint32_t)((mr_int32_t)tmp0u4 / (mr_int32_t)tmp1u4);
            } else {
                CORE_REG_X(core, instr.R_type.rd) = 0xFFFFFFFF;
            }
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_DIVU) {
            tmp0u4 = CORE_REG_X(core, instr.R_type.rs1);
            tmp1u4 = CORE_REG_X(core, instr.R_type.rs2);
            if (tmp1u4 != 0) {
                CORE_REG_X(core, instr.R_type.rd) = tmp0u4 / tmp1u4;
            } else {
                CORE_REG_X(core, instr.R_type.rd) = 0xFFFFFFFF;
            }
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_REM) {
            tmp0u4 = CORE_REG_X(core, instr.R_type.rs1);
            tmp1u4 = CORE_REG_X(core, instr.R_type.rs2);
            if ((tmp0u4 == 0x80000000) && (((mr_int32_t)tmp1u4) == (-1))) {
                CORE_REG_X(core, instr.R_type.rd) = 0;
            } else if (tmp1u4 != 0) {
                CORE_REG_X(core, instr.R_type.rd)
                    = (mr_uint32_t)((mr_int32_t)tmp0u4 % (mr_int32_t)tmp1u4);
            } else {
                CORE_REG_X(core, instr.R_type.rd) = tmp0u4;
            }
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_REMU) {
            tmp0u4 = CORE_REG_X(core, instr.R_type.rs1);
            tmp1u4 = CORE_REG_X(core, instr.R_type.rs2);
            if (tmp1u4 != 0) {
                CORE_REG_X(core, instr.R_type.rd) = tmp0u4 % tmp1u4;
            } else {
                CORE_REG_X(core, instr.R_type.rd) = tmp0u4;
            }
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        /* ---- A extension ---- */
        BCODE_EXECUTE_FUNC(BCODE_LR_W) {
            tmp0u4 = CORE_REG_X(core, instr.R_type.rs1);
            if (!CORE_ADDR_IS_ALIGN(tmp0u4, 4)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ALIGN);
            }
            tmp1u4 = CORE_ADDR_MEM(core, tmp0u4);
            if (CORE_ADDR_IS_IN_BIN(core, tmp0u4, 4)) {
                tmp2u4 = *CORE_BIN_u4(core, tmp0u4);
            } else if (CORE_ADDR_IS_IN_MEM(core, tmp1u4, 4)) {
                tmp2u4 = *CORE_MEM_u4(core, tmp1u4);
            } else {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ACCESS);
            }
            CORE_REG_X(core, instr.R_type.rd) = tmp2u4;
            CORE_REG_LR_ADDR(core) = tmp0u4;
            CORE_REG_LR_VALID(core) = 1;
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_SC_W) {
            tmp0u4 = CORE_REG_X(core, instr.R_type.rs1);
            if (!CORE_ADDR_IS_ALIGN(tmp0u4, 4)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ALIGN);
            }
            tmp1u4 = CORE_ADDR_MEM(core, tmp0u4);
            if (!CORE_ADDR_IS_IN_MEM(core, tmp1u4, 4)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ACCESS);
            }
            if (CORE_REG_LR_VALID(core) && (CORE_REG_LR_ADDR(core) == tmp0u4)) {
                *CORE_MEM_u4(core, tmp1u4) = CORE_REG_X(core, instr.R_type.rs2);
                CORE_REG_X(core, instr.R_type.rd) = 0;
            } else {
                CORE_REG_X(core, instr.R_type.rd) = 1;
            }
            CORE_REG_LR_VALID(core) = 0;
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_AMOSWAP_W) {
            tmp0u4 = CORE_REG_X(core, instr.R_type.rs1);
            if (!CORE_ADDR_IS_ALIGN(tmp0u4, 4)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ALIGN);
            }
            tmp1u4 = CORE_ADDR_MEM(core, tmp0u4);
            if (!CORE_ADDR_IS_IN_MEM(core, tmp1u4, 4)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ACCESS);
            }
            tmp2u4 = *CORE_MEM_u4(core, tmp1u4);
            *CORE_MEM_u4(core, tmp1u4) = CORE_REG_X(core, instr.R_type.rs2);
            CORE_REG_X(core, instr.R_type.rd) = tmp2u4;
            CORE_REG_LR_VALID(core) = 0;
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_AMOADD_W) {
            tmp0u4 = CORE_REG_X(core, instr.R_type.rs1);
            if (!CORE_ADDR_IS_ALIGN(tmp0u4, 4)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ALIGN);
            }
            tmp1u4 = CORE_ADDR_MEM(core, tmp0u4);
            if (!CORE_ADDR_IS_IN_MEM(core, tmp1u4, 4)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ACCESS);
            }
            tmp2u4 = *CORE_MEM_u4(core, tmp1u4);
            *CORE_MEM_u4(core, tmp1u4)
                = tmp2u4 + CORE_REG_X(core, instr.R_type.rs2);
            CORE_REG_X(core, instr.R_type.rd) = tmp2u4;
            CORE_REG_LR_VALID(core) = 0;
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_AMOXOR_W) {
            tmp0u4 = CORE_REG_X(core, instr.R_type.rs1);
            if (!CORE_ADDR_IS_ALIGN(tmp0u4, 4)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ALIGN);
            }
            tmp1u4 = CORE_ADDR_MEM(core, tmp0u4);
            if (!CORE_ADDR_IS_IN_MEM(core, tmp1u4, 4)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ACCESS);
            }
            tmp2u4 = *CORE_MEM_u4(core, tmp1u4);
            *CORE_MEM_u4(core, tmp1u4)
                = tmp2u4 ^ CORE_REG_X(core, instr.R_type.rs2);
            CORE_REG_X(core, instr.R_type.rd) = tmp2u4;
            CORE_REG_LR_VALID(core) = 0;
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_AMOAND_W) {
            tmp0u4 = CORE_REG_X(core, instr.R_type.rs1);
            if (!CORE_ADDR_IS_ALIGN(tmp0u4, 4)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ALIGN);
            }
            tmp1u4 = CORE_ADDR_MEM(core, tmp0u4);
            if (!CORE_ADDR_IS_IN_MEM(core, tmp1u4, 4)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ACCESS);
            }
            tmp2u4 = *CORE_MEM_u4(core, tmp1u4);
            *CORE_MEM_u4(core, tmp1u4)
                = tmp2u4 & CORE_REG_X(core, instr.R_type.rs2);
            CORE_REG_X(core, instr.R_type.rd) = tmp2u4;
            CORE_REG_LR_VALID(core) = 0;
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_AMOOR_W) {
            tmp0u4 = CORE_REG_X(core, instr.R_type.rs1);
            if (!CORE_ADDR_IS_ALIGN(tmp0u4, 4)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ALIGN);
            }
            tmp1u4 = CORE_ADDR_MEM(core, tmp0u4);
            if (!CORE_ADDR_IS_IN_MEM(core, tmp1u4, 4)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ACCESS);
            }
            tmp2u4 = *CORE_MEM_u4(core, tmp1u4);
            *CORE_MEM_u4(core, tmp1u4)
                = tmp2u4 | CORE_REG_X(core, instr.R_type.rs2);
            CORE_REG_X(core, instr.R_type.rd) = tmp2u4;
            CORE_REG_LR_VALID(core) = 0;
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_AMOMIN_W) {
            tmp0u4 = CORE_REG_X(core, instr.R_type.rs1);
            if (!CORE_ADDR_IS_ALIGN(tmp0u4, 4)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ALIGN);
            }
            tmp1u4 = CORE_ADDR_MEM(core, tmp0u4);
            if (!CORE_ADDR_IS_IN_MEM(core, tmp1u4, 4)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ACCESS);
            }
            tmp2u4 = *CORE_MEM_u4(core, tmp1u4);
            *CORE_MEM_u4(core, tmp1u4)
                = ((mr_int32_t)tmp2u4
                   < (mr_int32_t)CORE_REG_X(core, instr.R_type.rs2))
                    ? tmp2u4
                    : CORE_REG_X(core, instr.R_type.rs2);
            CORE_REG_X(core, instr.R_type.rd) = tmp2u4;
            CORE_REG_LR_VALID(core) = 0;
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_AMOMAX_W) {
            tmp0u4 = CORE_REG_X(core, instr.R_type.rs1);
            if (!CORE_ADDR_IS_ALIGN(tmp0u4, 4)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ALIGN);
            }
            tmp1u4 = CORE_ADDR_MEM(core, tmp0u4);
            if (!CORE_ADDR_IS_IN_MEM(core, tmp1u4, 4)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ACCESS);
            }
            tmp2u4 = *CORE_MEM_u4(core, tmp1u4);
            *CORE_MEM_u4(core, tmp1u4)
                = ((mr_int32_t)tmp2u4
                   > (mr_int32_t)CORE_REG_X(core, instr.R_type.rs2))
                    ? tmp2u4
                    : CORE_REG_X(core, instr.R_type.rs2);
            CORE_REG_X(core, instr.R_type.rd) = tmp2u4;
            CORE_REG_LR_VALID(core) = 0;
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_AMOMINU_W) {
            tmp0u4 = CORE_REG_X(core, instr.R_type.rs1);
            if (!CORE_ADDR_IS_ALIGN(tmp0u4, 4)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ALIGN);
            }
            tmp1u4 = CORE_ADDR_MEM(core, tmp0u4);
            if (!CORE_ADDR_IS_IN_MEM(core, tmp1u4, 4)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ACCESS);
            }
            tmp2u4 = *CORE_MEM_u4(core, tmp1u4);
            *CORE_MEM_u4(core, tmp1u4)
                = (tmp2u4 < CORE_REG_X(core, instr.R_type.rs2))
                    ? tmp2u4
                    : CORE_REG_X(core, instr.R_type.rs2);
            CORE_REG_X(core, instr.R_type.rd) = tmp2u4;
            CORE_REG_LR_VALID(core) = 0;
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_AMOMAXU_W) {
            tmp0u4 = CORE_REG_X(core, instr.R_type.rs1);
            if (!CORE_ADDR_IS_ALIGN(tmp0u4, 4)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ALIGN);
            }
            tmp1u4 = CORE_ADDR_MEM(core, tmp0u4);
            if (!CORE_ADDR_IS_IN_MEM(core, tmp1u4, 4)) {
                core->trap_mtval = tmp0u4;
                BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_ACCESS);
            }
            tmp2u4 = *CORE_MEM_u4(core, tmp1u4);
            *CORE_MEM_u4(core, tmp1u4)
                = (tmp2u4 > CORE_REG_X(core, instr.R_type.rs2))
                    ? tmp2u4
                    : CORE_REG_X(core, instr.R_type.rs2);
            CORE_REG_X(core, instr.R_type.rd) = tmp2u4;
            CORE_REG_LR_VALID(core) = 0;
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        /* ---- System ---- */
        BCODE_EXECUTE_FUNC(BCODE_ECALL) {
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_SYSCALL);
        }
        BCODE_EXECUTE_FUNC(BCODE_EBREAK) {
            BCODE_EXECUTE_EXIT(-MR_XVM_EXCEPT_NR_BREAK);
        }
        BCODE_EXECUTE_FUNC(BCODE_MRET) {
            tmp0u4 = CORE_REG_MSTATUS(core);
            if (tmp0u4 & REG_MSTATUS_MPIE) {
                tmp0u4 |= REG_MSTATUS_MIE;
            } else {
                tmp0u4 &= ~REG_MSTATUS_MIE;
            }
            tmp0u4 |= REG_MSTATUS_MPIE;
            tmp0u4 &= ~REG_MSTATUS_MPP_MASK;
            CORE_REG_MSTATUS(core) = tmp0u4;
            CORE_REG_PC(core) = CORE_REG_MEPC(core);
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_CSRRW) {
            tmp0u4 = instr.I_type.imm;
            tmp1u4 = 0;
            if (instr.I_type.rd != 0) {
                ret = csr_load(core, tmp0u4, &tmp1u4);
                if (ret != 0) {
                    BCODE_EXECUTE_EXIT(ret);
                }
            }
            ret = csr_store(core, tmp0u4, CORE_REG_X(core, instr.I_type.rs1));
            if (ret != 0) {
                BCODE_EXECUTE_EXIT(ret);
            }
            CORE_REG_X(core, instr.I_type.rd) = tmp1u4;
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_CSRRS) {
            tmp0u4 = instr.I_type.imm;
            ret = csr_load(core, tmp0u4, &tmp1u4);
            if (ret != 0) {
                BCODE_EXECUTE_EXIT(ret);
            }
            if (instr.I_type.rs1 != 0) {
                ret = csr_store(core, tmp0u4,
                                tmp1u4 | CORE_REG_X(core, instr.I_type.rs1));
                if (ret != 0) {
                    BCODE_EXECUTE_EXIT(ret);
                }
            }
            CORE_REG_X(core, instr.I_type.rd) = tmp1u4;
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_CSRRC) {
            tmp0u4 = instr.I_type.imm;
            ret = csr_load(core, tmp0u4, &tmp1u4);
            if (ret != 0) {
                BCODE_EXECUTE_EXIT(ret);
            }
            if (instr.I_type.rs1 != 0) {
                ret = csr_store(core, tmp0u4,
                                tmp1u4 & ~CORE_REG_X(core, instr.I_type.rs1));
                if (ret != 0) {
                    BCODE_EXECUTE_EXIT(ret);
                }
            }
            CORE_REG_X(core, instr.I_type.rd) = tmp1u4;
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_CSRRWI) {
            tmp0u4 = instr.I_type.imm;
            tmp1u4 = 0;
            if (instr.I_type.rd != 0) {
                ret = csr_load(core, tmp0u4, &tmp1u4);
                if (ret != 0) {
                    BCODE_EXECUTE_EXIT(ret);
                }
            }
            ret = csr_store(core, tmp0u4, instr.I_type.rs1);
            if (ret != 0) {
                BCODE_EXECUTE_EXIT(ret);
            }
            CORE_REG_X(core, instr.I_type.rd) = tmp1u4;
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_CSRRSI) {
            tmp0u4 = instr.I_type.imm;
            ret = csr_load(core, tmp0u4, &tmp1u4);
            if (ret != 0) {
                BCODE_EXECUTE_EXIT(ret);
            }
            if (instr.I_type.rs1 != 0) {
                ret = csr_store(core, tmp0u4, tmp1u4 | instr.I_type.rs1);
                if (ret != 0) {
                    BCODE_EXECUTE_EXIT(ret);
                }
            }
            CORE_REG_X(core, instr.I_type.rd) = tmp1u4;
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_CSRRCI) {
            tmp0u4 = instr.I_type.imm;
            ret = csr_load(core, tmp0u4, &tmp1u4);
            if (ret != 0) {
                BCODE_EXECUTE_EXIT(ret);
            }
            if (instr.I_type.rs1 != 0) {
                ret = csr_store(core, tmp0u4,
                                tmp1u4 & (mr_uint32_t)~instr.I_type.rs1);
                if (ret != 0) {
                    BCODE_EXECUTE_EXIT(ret);
                }
            }
            CORE_REG_X(core, instr.I_type.rd) = tmp1u4;
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_FUNC(BCODE_FENCE) {
            CORE_REG_PC(core) += 4;
            BCODE_EXECUTE_EXIT(TRAP_TYPE_NONE);
        }
        BCODE_EXECUTE_DEFAULT(-MR_XVM_EXCEPT_NR_ILLEGAL);
    }
}

MR_INLINE void counter_increase(core_t *core) {
    /* Increment cycle counter */
    CORE_REG_MCYCLE(core)++;
    if (CORE_REG_MCYCLE(core) == 0) {
        /* Carry cycle counter */
        CORE_REG_MCYCLEH(core)++;
    }

    /* Increment instruction counter */
    CORE_REG_MINSTRET(core)++;
    if (CORE_REG_MINSTRET(core) == 0) {
        /* Carry instruction counter */
        CORE_REG_MINSTRETH(core)++;
    }
}

MR_INLINE int bcode_exec(core_t *core, bcode_t bcode, instr_t instr) {
    int trap;

    /* Execute instruction */
    trap = bcode_instr_exec(core, bcode, instr);

    /* Ensure x0 always reads as 0 */
    CORE_REG_X(core, 0) = 0;

    /* Increment counters on success */
    if (trap == 0) {
        counter_increase(core);
    }
    return trap;
}

MR_INLINE mr_xvm_return_t syscall_handle(core_t *core, mr_xvm_event_t *event) {
    mr_uint32_t i;

    /* A7 = syscall number */
    event->syscall.nr = CORE_REG_X(core, 17);
    if (event->syscall.nr == 0) {
        return MR_XVM_EXIT;
    }

    /* A0 = ret */
    event->syscall.ret = &core->syscall.ret;

    /* A0~A6 = param[0]~param[n] */
    for (i = 0; i < MR_CFG_XVM_SYSCALL_PARAM_NUM; i++) {
        if (i < 7) {
            core->syscall.param[i] = CORE_REG_X(core, 10 + i);
        } else {
            /* Invalid padding data */
            core->syscall.param[i] = 0;
        }
        event->syscall.param[i] = &core->syscall.param[i];
    }
    core->syscall.pending = MR_TRUE;
    return MR_XVM_SYSCALL;
}

static mr_xvm_return_t xvm_riscv32_execute(mr_xvm_t *xvm, mr_xvm_event_t *event,
                                           mr_uint32_t step) {
    mr_xvm_datamem_t *datamem;
    mr_uint32_t i, except_nr;
    instr_t instr;
    bcode_t bcode;
    core_t *core;
    int trap;

    /* Get XVM data memory and RISCV32 core */
    datamem = MR_XVM_DATAMEM(xvm);
    core = (core_t *)datamem->data;

    /* Writeback syscall result */
    syscall_writeback(core);

    /* Decode and execute instructions */
    for (i = 0; i < step; i++) {
        /* Reset trap context */
        core->trap_mtval = 0;

        /* Enter interrupt */
        if (interrupt_enter(core)) {
            continue;
        }

        /* Fetch instruction */
        trap = instr_fetch(core, CORE_REG_PC(core), &instr.raw);
        if (trap != 0) {
            except_nr = (mr_uint32_t)-trap;

            /* Enter trap */
            if (trap_enter(core, except_nr, core->trap_mtval)) {
                continue;
            }
            event->except.nr = except_nr;
            return MR_XVM_EXCEPT;
        }

        /* Decode instruction */
        bcode = instr_decode(instr);

        /* Execute instruction */
        trap = bcode_exec(core, bcode, instr);
        if (trap == 0) {
            continue;
        }

        /* Exception */
        if (trap < 0) {
            except_nr = (mr_uint32_t)-trap;

            /* Unknown instruction */
            if (bcode == BCODE_UNKNOWN) {
                core->trap_mtval = instr.raw;
            }

            /* Enter trap(BREAK is handled by the host debugger) */
            if (except_nr != MR_XVM_EXCEPT_NR_BREAK) {
                if (trap_enter(core, except_nr, core->trap_mtval)) {
                    continue;
                }
            }

            /* Raise exception */
            event->except.nr = except_nr;
            return MR_XVM_EXCEPT;
        }

        /* Syscall */
        return syscall_handle(core, event);
    }

    /* Record the number of steps executed */
    event->none.step = i;
    return MR_XVM_NONE;
}

static mr_ssize_t xvm_riscv32_mem_map(mr_xvm_t *xvm, mr_uint64_t addr,
                                      void **memory, mr_bool_t *writable) {
    mr_xvm_datamem_t *datamem;
    mr_uint32_t mem_addr;
    mr_uint32_t addr32;
    core_t *core;

    /* Get XVM data memory and RISCV32 core */
    datamem = MR_XVM_DATAMEM(xvm);
    core = (core_t *)datamem->data;

    /* Convert address to 32-bit */
    if (addr <= MR_UINT32_MAX) {
        addr32 = (mr_uint32_t)addr;
    } else {
        return -MR_EINVAL;
    }

    /* Map binary image region(read-only) */
    if (CORE_ADDR_IS_IN_BIN(core, addr32, 1)) {
        *memory = CORE_BIN_u1(core, addr32);
        *writable = MR_FALSE;
        return (mr_ssize_t)(core->bin->size - addr32);
    }

    /* Map data memory region(read-write) */
    mem_addr = CORE_ADDR_MEM(core, addr32);
    if (CORE_ADDR_IS_IN_MEM(core, mem_addr, 1)) {
        *memory = CORE_MEM_u1(core, mem_addr);
        *writable = MR_TRUE;
        return (mr_ssize_t)(core->mem_size - mem_addr);
    }
    return -MR_EINVAL;
}

static mr_bool_t reg_find(const char *name, reg_type_t *type, mr_uint32_t *id) {
    mr_uint32_t idx;
    mr_size_t i;

    /* General purpose register: x0..x31 */
    if (name[0] == 'x') {
        /* Get register index */
        for (idx = 0, i = 1; (name[i] >= '0') && (name[i] <= '9'); i++) {
            idx = idx * 10 + (mr_uint32_t)(name[i] - '0');
            if (idx > 31) {
                break;
            }
        }
        if ((i > 1) && (name[i] == '\0') && (idx <= 31)) {
            *type = REG_TYPE_X;
            *id = idx;
            return MR_TRUE;
        }
    }

    /* Program counter */
    if (mr_strcmp(name, "pc") == 0) {
        *type = REG_TYPE_PC;
        return MR_TRUE;
    }

    /* CSR */
    for (i = 0; i < MR_ARRAY_SIZE(csr_map_table); i++) {
        if (mr_strcmp(name, csr_map_table[i].name) == 0) {
            *id = csr_map_table[i].addr;
            *type = REG_TYPE_CSR;
            return MR_TRUE;
        }
    }
    return MR_FALSE;
}

static mr_err_t xvm_riscv32_reg_load(mr_xvm_t *xvm, const char *name,
                                     mr_uint64_t *val) {
    mr_xvm_datamem_t *datamem;
    mr_uint32_t id, reg_val;
    reg_type_t type;
    core_t *core;

    /* Get XVM data memory and RISCV32 core */
    datamem = MR_XVM_DATAMEM(xvm);
    core = (core_t *)datamem->data;

    /* Find register */
    if (!reg_find(name, &type, &id)) {
        return -MR_EINVAL;
    }

    /* Load register */
    if (type == REG_TYPE_X) {
        /* 'x0' is always 0 */
        reg_val = (id == 0) ? 0 : CORE_REG_X(core, id);
    } else if (type == REG_TYPE_PC) {
        /* Program counter */
        reg_val = CORE_REG_PC(core);
    } else {
        /* CSR */
        if (csr_load(core, id, &reg_val) != 0) {
            return -MR_EINVAL;
        }
    }
    *val = (mr_uint64_t)reg_val;
    return 0;
}

static mr_err_t xvm_riscv32_reg_store(mr_xvm_t *xvm, const char *name,
                                      mr_uint64_t val) {
    mr_xvm_datamem_t *datamem;
    reg_type_t type;
    mr_uint32_t id;
    core_t *core;

    /* Get XVM data memory and RISCV32 core */
    datamem = MR_XVM_DATAMEM(xvm);
    core = (core_t *)datamem->data;

    /* Find register */
    if (!reg_find(name, &type, &id)) {
        return -MR_EINVAL;
    }

    /* Store register */
    if (type == REG_TYPE_X) {
        /* 'x0' is always 0 */
        if (id != 0) {
            CORE_REG_X(core, id) = (mr_uint32_t)val;
        }
    } else if (type == REG_TYPE_PC) {
        /* Program counter */
        CORE_REG_PC(core) = (mr_uint32_t)val;
    } else {
        /* CSR */
        if (csr_store(core, id, (mr_uint32_t)val) != 0) {
            return -MR_EINVAL;
        }
    }
    return 0;
}

static mr_err_t xvm_riscv32_interrupt(mr_xvm_t *xvm, mr_uint32_t irq,
                                      mr_bool_t active) {
    mr_xvm_datamem_t *datamem;
    core_t *core;

    /* Check parameter */
    if (irq >= 32) {
        return -MR_EINVAL;
    }

    /* Get XVM data memory and RISCV32 core */
    datamem = MR_XVM_DATAMEM(xvm);
    core = (core_t *)datamem->data;

    /* Set/unset interrupt */
    if (active) {
        mr_atomic_fetch_or(&core->intr.active, (1U << irq));
    } else {
        mr_atomic_fetch_and(&core->intr.active, ~(1U << irq));
    }
    return 0;
}

static void mr_xvm_isa_riscv32_init(void) {
    mr_xvm_isa_register(&__xvm_riscv32_isa);
}
MR_INIT_EXPORT(mr_xvm_isa_riscv32_init, MR_INIT_LEVEL_MODULE);
#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */

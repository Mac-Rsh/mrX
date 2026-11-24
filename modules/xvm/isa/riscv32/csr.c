/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/modules/xvm/isa/riscv32/csr.h>
#if defined(MR_USE_MODULE_XVM_RISCV32)
#include <mrX/modules/xvm/isa/riscv32/trap.h>

int xvm_riscv32_csr_load(core_t *core, mr_uint32_t addr, mr_uint32_t *val) {
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
            /* Refresh interrupt sources before reading MIP */
            xvm_riscv32_interrupt_update(core);
            *val = CORE_REG_MIP(core);
            break;
        }
        case REG_CSR_MCYCLE:
        case REG_CSR_CYCLE: {
            *val = (mr_uint32_t)CORE_REG_MCYCLE(core);
            break;
        }
        case REG_CSR_MCYCLEH:
        case REG_CSR_CYCLEH: {
            *val = (mr_uint32_t)(CORE_REG_MCYCLE(core) >> 32);
            break;
        }
        case REG_CSR_MINSTRET:
        case REG_CSR_INSTRET: {
            *val = (mr_uint32_t)CORE_REG_MINSTRET(core);
            break;
        }
        case REG_CSR_MINSTRETH:
        case REG_CSR_INSTRETH: {
            *val = (mr_uint32_t)(CORE_REG_MINSTRET(core) >> 32);
            break;
        }
        case REG_CSR_MVENDORID:
        case REG_CSR_MARCHID:
        case REG_CSR_MIMPID:
        case REG_CSR_MHARTID: {
            *val = 0U;
            break;
        }
        default: {
            return -MR_XVM_EXCEPT_NR_ILLEGAL;
        }
    }
    return 0;
}

int xvm_riscv32_csr_store(core_t *core, mr_uint32_t addr, mr_uint32_t val) {
    /* RISC-V CSRs with bits [11:10] == 0b11 are read-only */
    if ((addr & 0xC00U) == 0xC00U) {
        return -MR_XVM_EXCEPT_NR_ILLEGAL;
    }

    switch (addr) {
        case REG_CSR_MSTATUS: {
            CORE_REG_MSTATUS(core) = val;
            break;
        }
        case REG_CSR_MISA: {
            /* MISA is WARL; keep fixed ISA reporting */
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

            /* Restore interrupt source bits */
            xvm_riscv32_interrupt_update(core);
            break;
        }
        case REG_CSR_MCYCLE: {
            CORE_REG_MCYCLE(core)
                = (CORE_REG_MCYCLE(core) & 0xffffffff00000000ULL)
                | (mr_uint64_t)val;
            break;
        }
        case REG_CSR_MCYCLEH: {
            CORE_REG_MCYCLE(core)
                = (CORE_REG_MCYCLE(core) & 0x00000000ffffffffULL)
                | ((mr_uint64_t)val << 32);
            break;
        }
        case REG_CSR_MINSTRET: {
            CORE_REG_MINSTRET(core)
                = (CORE_REG_MINSTRET(core) & 0xffffffff00000000ULL)
                | (mr_uint64_t)val;
            break;
        }
        case REG_CSR_MINSTRETH: {
            CORE_REG_MINSTRET(core)
                = (CORE_REG_MINSTRET(core) & 0x00000000ffffffffULL)
                | ((mr_uint64_t)val << 32);
            break;
        }
        default: {
            return -MR_XVM_EXCEPT_NR_ILLEGAL;
        }
    }
    return 0;
}
#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */

/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/modules/xvm/isa/riscv32/trap.h>
#if defined(MR_USE_MODULE_XVM_RISCV32)

void xvm_riscv32_syscall_writeback(core_t *core) {
    /* Check whether syscall is pending */
    if (!core->syscall.pending) {
        return;
    }

    /* A0 = ret */
    CORE_REG_X(core, 10) = (mr_uint32_t)core->syscall.ret;
    core->syscall.pending = MR_FALSE;
}

mr_xvm_return_t xvm_riscv32_syscall_handle(core_t *core,
                                           mr_xvm_event_t *event) {
    mr_uint32_t i;

    /* A7 = syscall number */
    event->syscall.nr = CORE_REG_X(core, 17);
    if (event->syscall.nr == 0U) {
        return MR_XVM_EXIT;
    }

    /* A0 = ret */
    event->syscall.ret = &core->syscall.ret;

    /* A0~A6 = param[0]~param[n] */
    for (i = 0U; i < MR_CFG_XVM_SYSCALL_PARAM_NUM; i++) {
        if (i < 7U) {
            core->syscall.param[i] = CORE_REG_X(core, 10U + i);
        } else {
            /* Invalid padding data */
            core->syscall.param[i] = 0U;
        }
        event->syscall.param[i] = &core->syscall.param[i];
    }
    core->syscall.pending = MR_TRUE;
    return MR_XVM_SYSCALL;
}

static mr_bool_t trap_enter_cause(core_t *core, mr_uint32_t mcause,
                                  mr_uint32_t mtval) {
    mr_uint32_t mstatus, mtvec;

    /* Check whether trap vector is configured */
    mtvec = CORE_REG_MTVEC(core);
    if ((mtvec & ~0x3U) == 0U) {
        return MR_FALSE;
    }

    /* Save trap state */
    CORE_REG_MEPC(core) = CORE_REG_PC(core);
    CORE_REG_MCAUSE(core) = mcause;
    CORE_REG_MTVAL(core) = mtval;

    /* Save MIE to MPIE, disable MIE, set MPP = M */
    mstatus = CORE_REG_MSTATUS(core);
    if (mstatus & REG_MSTATUS_MIE) {
        mstatus |= REG_MSTATUS_MPIE;
    } else {
        mstatus &= ~REG_MSTATUS_MPIE;
    }
    mstatus &= ~REG_MSTATUS_MIE;
    mstatus = (mstatus & ~REG_MSTATUS_MPP_MASK) | REG_MSTATUS_MPP_M;
    CORE_REG_MSTATUS(core) = mstatus;

    /* Jump to trap vector */
    if (((mtvec & 0x3U) == 1U) && (mcause & REG_MCAUSE_INTR)) {
        CORE_REG_PC(core)
            = (mtvec & ~0x3U) + ((mcause & ~REG_MCAUSE_INTR) * 4U);
    } else {
        CORE_REG_PC(core) = mtvec & ~0x3U;
    }
    return MR_TRUE;
}

void xvm_riscv32_interrupt_update(core_t *core) {
    mr_uint32_t mask, irq;
    mr_bool_t meip;

    /* Filter pending, enabled and unclaimed IRQs */
    meip = MR_FALSE;
    mask = (mr_uint32_t)mr_atomic_load(&core->plic.pending) & core->plic.enable
         & ~core->plic.claimed & ~1U;

    /* Check whether any IRQ exceeds the priority threshold */
    if (mask != 0U) {
        for (irq = 1U; irq < CORE_MMIO_PLIC_IRQ_NUM; irq++) {
            if (((mask & (1U << irq)) != 0U)
                && ((mr_uint32_t)core->plic.priority[irq]
                    > core->plic.threshold)) {
                meip = MR_TRUE;
                break;
            }
        }
    }

    /* Update external interrupt pending bit */
    if (meip) {
        CORE_REG_MIP(core) |= REG_MIP_MEIP;
    } else {
        CORE_REG_MIP(core) &= ~REG_MIP_MEIP;
    }

    /* Update CLINT software interrupt pending bit (MSIP) */
    if ((core->clint.msip & 1U) != 0U) {
        CORE_REG_MIP(core) |= REG_MIP_MSIP;
    } else {
        CORE_REG_MIP(core) &= ~REG_MIP_MSIP;
    }

    /* Update CLINT timer interrupt pending bit (MTIP) */
    if (CORE_REG_MCYCLE(core) >= core->clint.mtimecmp) {
        CORE_REG_MIP(core) |= REG_MIP_MTIP;
    } else {
        CORE_REG_MIP(core) &= ~REG_MIP_MTIP;
    }
}

mr_bool_t xvm_riscv32_interrupt_enter(core_t *core) {
    mr_uint32_t pending;

    /* Check whether global interrupt status is enabled */
    if (!(CORE_REG_MSTATUS(core) & REG_MSTATUS_MIE)) {
        return MR_FALSE;
    }

    /* Check whether any machine interrupt source is enabled */
    if ((CORE_REG_MIE(core) & (REG_MIE_MEIE | REG_MIE_MSIE | REG_MIE_MTIE))
        == 0U) {
        return MR_FALSE;
    }

    /* Refresh pending interrupt bits */
    xvm_riscv32_interrupt_update(core);

    /* Select the highest-priority pending interrupt: MEI > MSI > MTI */
    pending = CORE_REG_MIP(core) & CORE_REG_MIE(core);
    if ((pending & REG_MIP_MEIP) != 0U) {
        return trap_enter_cause(core, REG_MCAUSE_MEI_INTR, 0U);
    }
    if ((pending & REG_MIP_MSIP) != 0U) {
        return trap_enter_cause(core, REG_MCAUSE_MSI_INTR, 0U);
    }
    if ((pending & REG_MIP_MTIP) != 0U) {
        return trap_enter_cause(core, REG_MCAUSE_MTI_INTR, 0U);
    }
    return MR_FALSE;
}

static mr_uint32_t trap_except_cause(mr_uint32_t except_nr) {
    switch (except_nr) {
        case MR_XVM_EXCEPT_NR_ALIGN: {
            return 0U;
        }
        case MR_XVM_EXCEPT_NR_OVERFLOW: {
            return 1U;
        }
        case MR_XVM_EXCEPT_NR_ILLEGAL: {
            return 2U;
        }
        case MR_XVM_EXCEPT_NR_BREAK: {
            return 3U;
        }
        case MR_XVM_EXCEPT_NR_ACCESS: {
            return 7U;
        }
        default: {
            return 2U;
        }
    }
}

mr_bool_t xvm_riscv32_trap_enter(core_t *core, mr_uint32_t except_nr,
                                 mr_uint32_t mtval) {
    /* Enter trap handler */
    return trap_enter_cause(core, trap_except_cause(except_nr), mtval);
}
#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */

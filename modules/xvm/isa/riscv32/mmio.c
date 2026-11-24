/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/modules/xvm/isa/riscv32/core.h>
#if defined(MR_USE_MODULE_XVM_RISCV32)
#include <mrX/modules/xvm/isa/riscv32/trap.h>

static mr_uint32_t plic_claim(core_t *core) {
    mr_uint32_t mask, best_irq, best_prio, irq, prio;

    /* Filter pending, enabled and unclaimed IRQs */
    mask = (mr_uint32_t)mr_atomic_load(&core->plic.pending) & core->plic.enable
         & ~core->plic.claimed & ~1U;
    if (mask == 0U) {
        return 0U;
    }

    /* Select highest priority, preferring lower IRQs on ties */
    best_irq = 0U;
    best_prio = core->plic.threshold;
    for (irq = 1U; irq < CORE_MMIO_PLIC_IRQ_NUM; irq++) {
        if ((mask & (1U << irq)) == 0U) {
            continue;
        }
        prio = (mr_uint32_t)core->plic.priority[irq];
        if (prio > best_prio) {
            best_prio = prio;
            best_irq = irq;
        }
    }

    /* Claim the IRQ and refresh the external interrupt */
    if (best_irq != 0U) {
        mr_atomic_fetch_and(&core->plic.pending, ~(1U << best_irq));
        core->plic.claimed |= (1U << best_irq);
        xvm_riscv32_interrupt_update(core);
    }
    return best_irq;
}

static void plic_complete(core_t *core, mr_uint32_t irq) {
    mr_uint32_t mask;

    /* Check IRQ number */
    if ((irq == 0U) || (irq >= CORE_MMIO_PLIC_IRQ_NUM)) {
        return;
    }

    /* Release the claim and re-pend an active source */
    mask = (1U << irq);
    core->plic.claimed &= ~mask;
    if (((mr_uint32_t)mr_atomic_load(&core->plic.level) & mask) != 0U) {
        mr_atomic_fetch_or(&core->plic.pending, mask);
    }

    /* Refresh the external interrupt */
    xvm_riscv32_interrupt_update(core);
}

static int mmio_plic_load(core_t *core, mr_uint32_t addr, mr_size_t size,
                          mr_uint32_t *val) {
    mr_uint32_t irq;

    /* Check access width and alignment */
    if ((size != 4U) || ((addr & 0x3U) != 0U)) {
        return -MR_XVM_EXCEPT_NR_ACCESS;
    }

    /* Priority registers: IRQ 0 is reserved and reads as 0 */
    if ((addr >= CORE_MMIO_PLIC_PRIORITY_BASE)
        && (addr < CORE_MMIO_PLIC_PRIORITY_END)) {
        irq = (addr - CORE_MMIO_PLIC_PRIORITY_BASE) >> 2;
        *val = (irq == 0U) ? 0U : (mr_uint32_t)core->plic.priority[irq];
        return 0;
    }

    /* Access PLIC registers */
    switch (addr) {
        case CORE_MMIO_PLIC_PENDING: {
            *val = (mr_uint32_t)mr_atomic_load(&core->plic.pending) & ~1U;
            break;
        }
        case CORE_MMIO_PLIC_ENABLE: {
            *val = core->plic.enable & ~1U;
            break;
        }
        case CORE_MMIO_PLIC_THRESHOLD: {
            *val = core->plic.threshold;
            break;
        }
        case CORE_MMIO_PLIC_CLAIM: {
            *val = plic_claim(core);
            break;
        }
        default: {
            return -MR_XVM_EXCEPT_NR_ACCESS;
        }
    }
    return 0;
}

static int mmio_clint_load(core_t *core, mr_uint32_t addr, mr_size_t size,
                           mr_uint32_t *val) {
    /* Check access width and alignment */
    if ((size != 4U) || ((addr & 0x3U) != 0U)) {
        return -MR_XVM_EXCEPT_NR_ACCESS;
    }

    /* Access CLINT registers */
    switch (addr) {
        case CORE_MMIO_CLINT_MSIP: {
            *val = core->clint.msip & 1U;
            break;
        }
        case CORE_MMIO_CLINT_MTIMECMP_LO: {
            *val = (mr_uint32_t)core->clint.mtimecmp;
            break;
        }
        case CORE_MMIO_CLINT_MTIMECMP_HI: {
            *val = (mr_uint32_t)(core->clint.mtimecmp >> 32);
            break;
        }
        case CORE_MMIO_CLINT_MTIME_LO: {
            *val = (mr_uint32_t)CORE_REG_MCYCLE(core);
            break;
        }
        case CORE_MMIO_CLINT_MTIME_HI: {
            *val = (mr_uint32_t)(CORE_REG_MCYCLE(core) >> 32);
            break;
        }
        default: {
            return -MR_XVM_EXCEPT_NR_ACCESS;
        }
    }
    return 0;
}

int xvm_riscv32_mmio_load(core_t *core, mr_uint32_t addr, mr_size_t size,
                          mr_uint32_t *val) {
#if defined(MR_USE_MODULE_XVM_MMIO)
    mr_uint64_t val64;
    mr_err_t ret;
#endif /* defined(MR_USE_MODULE_XVM_MMIO) */

    /* Built-in PLIC */
    if ((addr >= CORE_MMIO_PLIC_BASE) && (addr < CORE_MMIO_PLIC_END)) {
        return mmio_plic_load(core, addr, size, val);
    }

    /* Built-in CLINT */
    if ((addr >= CORE_MMIO_CLINT_BASE) && (addr < CORE_MMIO_CLINT_END)) {
        return mmio_clint_load(core, addr, size, val);
    }

#if defined(MR_USE_MODULE_XVM_MMIO)
    /* Forward to registered user MMIO regions */
    if (core->xvm != MR_NULL) {
        val64 = 0U;
        ret = mr_xvm_mmio_load(core->xvm, (mr_uint64_t)addr, size, &val64);
        if (ret == 0) {
            *val = (mr_uint32_t)val64;
            return 0;
        }
    }
#endif /* defined(MR_USE_MODULE_XVM_MMIO) */
    return -MR_XVM_EXCEPT_NR_ACCESS;
}

static int mmio_plic_store(core_t *core, mr_uint32_t addr, mr_size_t size,
                           mr_uint32_t val) {
    mr_uint32_t irq;

    /* Check access width and alignment */
    if ((size != 4U) || ((addr & 0x3U) != 0U)) {
        return -MR_XVM_EXCEPT_NR_ACCESS;
    }

    /* Priority registers: IRQ 0 is reserved (writes ignored) */
    if ((addr >= CORE_MMIO_PLIC_PRIORITY_BASE)
        && (addr < CORE_MMIO_PLIC_PRIORITY_END)) {
        irq = (addr - CORE_MMIO_PLIC_PRIORITY_BASE) >> 2;
        if (irq != 0U) {
            core->plic.priority[irq] =
                (mr_uint8_t)(val & CORE_MMIO_PLIC_MAX_PRIO);
            xvm_riscv32_interrupt_update(core);
        }
        return 0;
    }

    /* Access PLIC registers */
    switch (addr) {
        case CORE_MMIO_PLIC_PENDING: {
            /* Pending bits are read-only per PLIC spec; ignore writes */
            break;
        }
        case CORE_MMIO_PLIC_ENABLE: {
            core->plic.enable = val & ~1U;
            xvm_riscv32_interrupt_update(core);
            break;
        }
        case CORE_MMIO_PLIC_THRESHOLD: {
            core->plic.threshold = val & CORE_MMIO_PLIC_MAX_PRIO;
            xvm_riscv32_interrupt_update(core);
            break;
        }
        case CORE_MMIO_PLIC_CLAIM: {
            plic_complete(core, val);
            break;
        }
        default: {
            return -MR_XVM_EXCEPT_NR_ACCESS;
        }
    }
    return 0;
}

static int mmio_clint_store(core_t *core, mr_uint32_t addr, mr_size_t size,
                            mr_uint32_t val) {
    /* Check access width and alignment */
    if ((size != 4U) || ((addr & 0x3U) != 0U)) {
        return -MR_XVM_EXCEPT_NR_ACCESS;
    }

    /* Access CLINT registers */
    switch (addr) {
        case CORE_MMIO_CLINT_MSIP: {
            core->clint.msip = val & 1U;
            xvm_riscv32_interrupt_update(core);
            break;
        }
        case CORE_MMIO_CLINT_MTIMECMP_LO: {
            core->clint.mtimecmp =
                (core->clint.mtimecmp & 0xFFFFFFFF00000000ULL)
                | (mr_uint64_t)val;
            xvm_riscv32_interrupt_update(core);
            break;
        }
        case CORE_MMIO_CLINT_MTIMECMP_HI: {
            core->clint.mtimecmp =
                (core->clint.mtimecmp & 0x00000000FFFFFFFFULL)
                | ((mr_uint64_t)val << 32);
            xvm_riscv32_interrupt_update(core);
            break;
        }
        case CORE_MMIO_CLINT_MTIME_LO:
        case CORE_MMIO_CLINT_MTIME_HI: {
            /* MTIME is read-only counter backed by MCYCLE */
            break;
        }
        default: {
            return -MR_XVM_EXCEPT_NR_ACCESS;
        }
    }
    return 0;
}

int xvm_riscv32_mmio_store(core_t *core, mr_uint32_t addr, mr_size_t size,
                           mr_uint32_t val) {
#if defined(MR_USE_MODULE_XVM_MMIO)
    mr_err_t ret;
#endif /* defined(MR_USE_MODULE_XVM_MMIO) */

    /* Built-in PLIC */
    if ((addr >= CORE_MMIO_PLIC_BASE) && (addr < CORE_MMIO_PLIC_END)) {
        return mmio_plic_store(core, addr, size, val);
    }

    /* Built-in CLINT */
    if ((addr >= CORE_MMIO_CLINT_BASE) && (addr < CORE_MMIO_CLINT_END)) {
        return mmio_clint_store(core, addr, size, val);
    }

#if defined(MR_USE_MODULE_XVM_MMIO)
    /* Forward to registered user MMIO regions */
    if (core->xvm != MR_NULL) {
        ret = mr_xvm_mmio_store(core->xvm, (mr_uint64_t)addr, size,
                                (mr_uint64_t)val);
        if (ret == 0) {
            return 0;
        }
    }
#endif /* defined(MR_USE_MODULE_XVM_MMIO) */
    return -MR_XVM_EXCEPT_NR_ACCESS;
}
#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */

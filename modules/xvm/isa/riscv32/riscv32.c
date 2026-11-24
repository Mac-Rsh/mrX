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
#include <mrX/modules/xvm/isa/riscv32/trap.h>
#include <mrX/modules/xvm/isa/riscv32/exec.h>

/* XVM RISCV32 operations */
static mr_err_t xvm_riscv32_load(mr_xvm_t *xvm);
static mr_xvm_return_t xvm_riscv32_execute(mr_xvm_t *xvm, mr_xvm_event_t *event,
                                           mr_uint32_t step);
static mr_ssize_t xvm_riscv32_mem_map(mr_xvm_t *xvm, mr_uint64_t addr,
                                      void **memory, mr_bool_t *writable);
static mr_err_t xvm_riscv32_reg_load(mr_xvm_t *xvm, mr_uint32_t nr,
                                     mr_uint64_t *val);
static mr_err_t xvm_riscv32_reg_store(mr_xvm_t *xvm, mr_uint32_t nr,
                                      mr_uint64_t val);
static mr_err_t xvm_riscv32_interrupt(mr_xvm_t *xvm, mr_uint32_t irq,
                                      mr_bool_t active);
static const mr_xvm_isa_ops_t __xvm_riscv32_ops = {
    .load = xvm_riscv32_load,
    .execute = xvm_riscv32_execute,
    .mem_map = xvm_riscv32_mem_map,
    .reg_load = xvm_riscv32_reg_load,
    .reg_store = xvm_riscv32_reg_store,
    .interrupt = xvm_riscv32_interrupt,
};
#if defined(MR_USE_MODULE_XVM_DEBUG)
/* XVM RISCV32 target */
static mr_xvm_isa_regdesc_t __xvm_riscv32_regs[] = {
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32,  .name = "x0"},
    {.type = MR_XVM_ISA_REG_CODE, .bit_size = 32,  .name = "x1"},
    {.type = MR_XVM_ISA_REG_DATA, .bit_size = 32,  .name = "x2"},
    {.type = MR_XVM_ISA_REG_DATA, .bit_size = 32,  .name = "x3"},
    {.type = MR_XVM_ISA_REG_DATA, .bit_size = 32,  .name = "x4"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32,  .name = "x5"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32,  .name = "x6"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32,  .name = "x7"},
    {.type = MR_XVM_ISA_REG_DATA, .bit_size = 32,  .name = "x8"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32,  .name = "x9"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32, .name = "x10"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32, .name = "x11"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32, .name = "x12"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32, .name = "x13"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32, .name = "x14"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32, .name = "x15"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32, .name = "x16"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32, .name = "x17"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32, .name = "x18"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32, .name = "x19"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32, .name = "x20"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32, .name = "x21"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32, .name = "x22"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32, .name = "x23"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32, .name = "x24"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32, .name = "x25"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32, .name = "x26"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32, .name = "x27"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32, .name = "x28"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32, .name = "x29"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32, .name = "x30"},
    {.type = MR_XVM_ISA_REG_INT, .bit_size = 32, .name = "x31"},
    {.type = MR_XVM_ISA_REG_CODE, .bit_size = 32,  .name = "pc"},
};
static mr_xvm_isa_target_t __xvm_riscv32_target = {
    .feature = "org.gnu.gdb.riscv.cpu",
    .arch = "riscv:rv32",
    .regs = __xvm_riscv32_regs,
    .regs_num = sizeof(__xvm_riscv32_regs) / sizeof(__xvm_riscv32_regs[0]),
};
#define XVM_RISCV32_TARGET              (&__xvm_riscv32_target)
#else
#define XVM_RISCV32_TARGET              (MR_NULL)
#endif /* defined(MR_USE_MODULE_XVM_DEBUG) */
static mr_xvm_isa_t __xvm_riscv32_isa
    = MR_XVM_ISA_INIT("riscv32", &__xvm_riscv32_ops, XVM_RISCV32_TARGET);

/* Core storage alignment definition */
#define CORE_ALIGN                      (sizeof(mr_uint64_t))

MR_INLINE core_t *core_from_datamem(const mr_xvm_datamem_t *datamem) {
    const mr_uintptr_t mask = (mr_uintptr_t)CORE_ALIGN - 1U;
    mr_uintptr_t base;

    /* Align data memory up to the core boundary */
    base = (mr_uintptr_t)datamem->data;
    return (core_t *)((base + mask) & ~mask);
}

static mr_err_t xvm_riscv32_load(mr_xvm_t *xvm) {
    const mr_xvm_binary_t *bin;
    mr_xvm_datamem_t *datamem;
    mr_size_t align_pad;
    mr_size_t mem_size;
    core_t *core;

    /* Get XVM data memory and aligned RISCV32 core */
    datamem = MR_XVM_DATAMEM(xvm);
    bin = MR_XVM_BIN(xvm);
    core = core_from_datamem(datamem);
    align_pad = (mr_size_t)((mr_uintptr_t)core - (mr_uintptr_t)datamem->data);

    /* Check data memory capacity */
    if ((datamem->size >= align_pad)
        && ((datamem->size - align_pad) >= sizeof(core_t))) {
        mem_size = datamem->size - align_pad - sizeof(*core);
    } else {
        return -MR_ENOMEM;
    }

    /* Validate 32-bit RAM address range and non-overlap with MMIO */
    if ((bin->entry > MR_UINT32_MAX)
        || ((mr_uint64_t)mem_size > (0x100000000ULL - bin->entry))) {
        return -MR_EINVAL;
    }
    if ((mem_size > 0U) && (bin->entry < (mr_uint64_t)CORE_MMIO_END)
        && ((bin->entry + (mr_uint64_t)mem_size)
            > (mr_uint64_t)CORE_MMIO_BASE)) {
        return -MR_EINVAL;
    }

    /* Load RISCV32 core */
#if defined(MR_USE_MODULE_XVM_MMIO)
    core->xvm = xvm;
#endif /* defined(MR_USE_MODULE_XVM_MMIO) */
    core->mem_size = mem_size;

    /* Init PLIC latches and timer compare */
    mr_atomic_init(&core->plic.level, 0);
    mr_atomic_init(&core->plic.pending, 0);
    core->clint.mtimecmp = MR_UINT64_MAX;

    /* Init core registers and binary */
    core->regs.misa = CORE_MISA_DEFAULT;
    core->bin = bin;
    core->trap_mtval = 0U;
    return 0;
}

static mr_xvm_return_t xvm_riscv32_execute(mr_xvm_t *xvm, mr_xvm_event_t *event,
                                           mr_uint32_t step) {
    mr_xvm_datamem_t *datamem;
    core_t *core;

    /* Get XVM data memory and aligned RISCV32 core */
    datamem = MR_XVM_DATAMEM(xvm);
    core = core_from_datamem(datamem);

    return xvm_riscv32_exec_run(xvm, core, event, step);
}

static mr_ssize_t xvm_riscv32_mem_map(mr_xvm_t *xvm, mr_uint64_t addr,
                                      void **memory, mr_bool_t *writable) {
    mr_xvm_datamem_t *datamem;
    mr_uint32_t mem_addr;
    mr_uint32_t addr32;
    core_t *core;

    /* Get XVM data memory and aligned RISCV32 core */
    datamem = MR_XVM_DATAMEM(xvm);
    core = core_from_datamem(datamem);
    if ((!core->bin) || (!core->bin->image)) {
        return -MR_EPERM;
    }

    /* Convert address to 32-bit */
    if (addr <= MR_UINT32_MAX) {
        addr32 = (mr_uint32_t)addr;
    } else {
        return -MR_EINVAL;
    }

    /* Map binary image region(read-only) */
    if (CORE_ADDR_IS_IN_BIN(core, addr32, 1U)) {
        *memory = (void *)(core->bin->image + addr32);
        *writable = MR_FALSE;
        return (mr_ssize_t)(core->bin->size - addr32);
    }

    /* Map data memory region(read-write) */
    mem_addr = CORE_ADDR_MEM(core, addr32);
    if (CORE_ADDR_IS_IN_MEM(core, mem_addr, 1U)) {
        *memory = core->mem + mem_addr;
        *writable = MR_TRUE;
        return (mr_ssize_t)(core->mem_size - mem_addr);
    }
    return -MR_EINVAL;
}

static mr_err_t xvm_riscv32_reg_load(mr_xvm_t *xvm, mr_uint32_t nr,
                                     mr_uint64_t *val) {
    mr_xvm_datamem_t *datamem;
    core_t *core;

    /* Get XVM data memory and aligned RISCV32 core */
    datamem = MR_XVM_DATAMEM(xvm);
    core = core_from_datamem(datamem);

    /* Load general purpose register */
    if (nr <= 31U) {
        /* 'x0' is always 0 */
        *val = (nr == 0U) ? 0U : (mr_uint64_t)CORE_REG_X(core, nr);
        return 0;
    }

    /* Load program counter */
    if (nr == 32U) {
        *val = (mr_uint64_t)CORE_REG_PC(core);
        return 0;
    }
    return -MR_EINVAL;
}

static mr_err_t xvm_riscv32_reg_store(mr_xvm_t *xvm, mr_uint32_t nr,
                                      mr_uint64_t val) {
    mr_xvm_datamem_t *datamem;
    core_t *core;

    /* Get XVM data memory and aligned RISCV32 core */
    datamem = MR_XVM_DATAMEM(xvm);
    core = core_from_datamem(datamem);

    /* Store general purpose register */
    if (nr <= 31U) {
        /* 'x0' is always 0 */
        if (nr != 0U) {
            CORE_REG_X(core, nr) = (mr_uint32_t)val;
        }
        return 0;
    }

    /* Store program counter */
    if (nr == 32U) {
        CORE_REG_PC(core) = (mr_uint32_t)val;
        return 0;
    }
    return -MR_EINVAL;
}

static mr_err_t xvm_riscv32_interrupt(mr_xvm_t *xvm, mr_uint32_t irq,
                                      mr_bool_t active) {
    mr_xvm_datamem_t *datamem;
    mr_uint32_t mask;
    core_t *core;

    /* Check parameter (PLIC IRQ 0 is reserved as "no interrupt") */
    if ((irq == 0U) || (irq >= CORE_MMIO_PLIC_IRQ_NUM)) {
        return -MR_EINVAL;
    }

    /* Get XVM data memory and aligned RISCV32 core */
    datamem = MR_XVM_DATAMEM(xvm);
    core = core_from_datamem(datamem);

    /* Update level state and pending latch */
    mask = (1U << irq);
    if (active) {
        mr_atomic_fetch_or(&core->plic.level, mask);

        /* Latch the IRQ unless it is already claimed */
        if ((core->plic.claimed & mask) == 0U) {
            mr_atomic_fetch_or(&core->plic.pending, mask);
        }
    } else {
        mr_atomic_fetch_and(&core->plic.level, ~mask);
        mr_atomic_fetch_and(&core->plic.pending, ~mask);
    }
    return 0;
}

static void mr_xvm_isa_riscv32_init(void) {
    mr_xvm_isa_register(&__xvm_riscv32_isa);
}
MR_INIT_EXPORT(mr_xvm_isa_riscv32_init, MR_INIT_LEVEL_MODULE);
#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */

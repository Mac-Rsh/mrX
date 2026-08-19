/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __RISCV32_CORE_H__
#define __RISCV32_CORE_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_MODULE_XVM_RISCV32)
#include <mrX/modules/xvm/isa/riscv32/bcode.h>
#include <mrX/modules/xvm/isa/riscv32/instr.h>
#include <mrX/modules/xvm/isa/riscv32/mmio.h>
#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup XVM-RISCV32
 * @{
 */

#if defined(MR_USE_MODULE_XVM_RISCV32)
/* RISCV32 CSR definition */
#define REG_CSR_MSTATUS                 (0x300)
#define REG_CSR_MISA                    (0x301)
#define REG_CSR_MIE                     (0x304)
#define REG_CSR_MTVEC                   (0x305)
#define REG_CSR_MSCRATCH                (0x340)
#define REG_CSR_MEPC                    (0x341)
#define REG_CSR_MCAUSE                  (0x342)
#define REG_CSR_MTVAL                   (0x343)
#define REG_CSR_MIP                     (0x344)
#define REG_CSR_MCYCLE                  (0xb00)
#define REG_CSR_MINSTRET                (0xb02)
#define REG_CSR_MCYCLEH                 (0xb80)
#define REG_CSR_MINSTRETH               (0xb82)
#define REG_CSR_CYCLE                   (0xc00)
#define REG_CSR_INSTRET                 (0xc02)
#define REG_CSR_CYCLEH                  (0xc80)
#define REG_CSR_INSTRETH                (0xc82)
#define REG_CSR_MVENDORID               (0xf11)
#define REG_CSR_MARCHID                 (0xf12)
#define REG_CSR_MIMPID                  (0xf13)
#define REG_CSR_MHARTID                 (0xf14)

/* RISCV32 MSTATUS definition */
#define REG_MSTATUS_MIE                 (1u << 3)
#define REG_MSTATUS_MPIE                (1u << 7)
#define REG_MSTATUS_MPP_MASK            (3u << 11)
#define REG_MSTATUS_MPP_M               (3u << 11)

/* RISCV32 MIP definition */
#define REG_MIP_MEIP                    (1u << 11)
#define REG_MIE_MEIE                    (1u << 11)

/* RISCV32 MCAUSE definition */
#define REG_MCAUSE_INTR                 (1u << 31)
#define REG_MCAUSE_MEI                  (11u)
#define REG_MCAUSE_MEI_INTR             (REG_MCAUSE_INTR | REG_MCAUSE_MEI)

/* RISCV32 trap type */
typedef enum trap_type {
    TRAP_TYPE_NONE,
    TRAP_TYPE_SYSCALL,
} trap_type_t;

/* RISCV32 core registers type */
typedef struct core_regs {
    mr_uint32_t x[32];
    mr_uint32_t pc;
    mr_uint32_t mstatus;
    mr_uint32_t misa;
    mr_uint32_t mie;
    mr_uint32_t mtvec;
    mr_uint32_t mscratch;
    mr_uint32_t mepc;
    mr_uint32_t mcause;
    mr_uint32_t mtval;
    mr_uint32_t mip;
    mr_uint32_t mcycle;
    mr_uint32_t mcycleh;
    mr_uint32_t minstret;
    mr_uint32_t minstreth;
} core_regs_t;

/* RISCV32 syscall shadow type */
typedef struct syscall_shadow {
    mr_uint64_t param[MR_CFG_XVM_SYSCALL_PARAM_NUM];
    mr_uint64_t ret;
    mr_bool_t pending;
} syscall_shadow_t;

/* RISCV32 interrupt controller type */
typedef struct intr_control {
    mr_atomic_t active;
    mr_uint32_t enable;
} intr_control_t;

/* RISCV32 core type */
typedef struct core {
    const mr_xvm_binary_t *bin;
    syscall_shadow_t syscall;
    mr_uint32_t trap_mtval;
    intr_control_t intr;
    core_regs_t regs;
    mr_size_t mem_size;
    mr_uint8_t mem[];
} core_t;

/* RISCV32 core registers macro function definition */
#define CORE_REG(_core)                 ((_core)->regs)
#define CORE_REG_X(_core, _reg)         (CORE_REG(_core).x[(_reg)])
#define CORE_REG_PC(_core)              (CORE_REG(_core).pc)
#define CORE_REG_MSTATUS(_core)         (CORE_REG(_core).mstatus)
#define CORE_REG_MISA(_core)            (CORE_REG(_core).misa)
#define CORE_REG_MIE(_core)             (CORE_REG(_core).mie)
#define CORE_REG_MTVEC(_core)           (CORE_REG(_core).mtvec)
#define CORE_REG_MSCRATCH(_core)        (CORE_REG(_core).mscratch)
#define CORE_REG_MEPC(_core)            (CORE_REG(_core).mepc)
#define CORE_REG_MCAUSE(_core)          (CORE_REG(_core).mcause)
#define CORE_REG_MTVAL(_core)           (CORE_REG(_core).mtval)
#define CORE_REG_MIP(_core)             (CORE_REG(_core).mip)
#define CORE_REG_MCYCLE(_core)          (CORE_REG(_core).mcycle)
#define CORE_REG_MCYCLEH(_core)         (CORE_REG(_core).mcycleh)
#define CORE_REG_MINSTRET(_core)        (CORE_REG(_core).minstret)
#define CORE_REG_MINSTRETH(_core)       (CORE_REG(_core).minstreth)

/* RISCV32 core address macro function definition */
#define CORE_ADDR_IS_ALIGN(_addr, _size)                                       \
    (((_addr) & ((_size) - 1)) == 0)
#define CORE_ADDR_BIN(_core, _addr)     (_addr)
#define CORE_ADDR_MEM(_core, _addr)                                            \
    ((_addr) - (mr_uint32_t)(_core)->bin->entry)
#define CORE_ADDR_IS_IN_BIN(_core, _addr, _size)                               \
    (((_core)->bin->size >= (_size))                                           \
     && ((_addr) <= ((_core)->bin->size - (_size))))
#define CORE_ADDR_IS_IN_MEM(_core, _addr, _size)                               \
    (((_core)->mem_size >= (_size))                                            \
     && ((_addr) <= ((_core)->mem_size - (_size))))
#define CORE_ADDR_IS_IN_MMIO(_core, _addr, _size)                              \
    ((CORE_MMIO_SIZE >= (_size)) && ((_addr) >= CORE_MMIO_BASE)                \
     && ((_addr) <= (CORE_MMIO_END - (_size))))
#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __RISCV32_CORE_H__ */

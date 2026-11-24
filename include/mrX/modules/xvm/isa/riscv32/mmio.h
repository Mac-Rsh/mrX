/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __RISCV32_MMIO_H__
#define __RISCV32_MMIO_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_MODULE_XVM_RISCV32)
#include <mrX/modules/xvm/mr_xvm_isa.h>
#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup XVM-RISCV32
 * @{
 */

#if defined(MR_USE_MODULE_XVM_RISCV32)
struct core;

/* RISCV32 MMIO definition */
#define CORE_MMIO_BASE                  (0x40000000U)
#define CORE_MMIO_SIZE                  (0x10000000U)
#define CORE_MMIO_END                   (CORE_MMIO_BASE + CORE_MMIO_SIZE)

/* RISCV32 MMIO peripheral definition */
#define CORE_MMIO_PLIC_BASE             (CORE_MMIO_BASE + 0x00000000U)
#define CORE_MMIO_PLIC_SIZE             (0x00400000U)
#define CORE_MMIO_PLIC_END              (CORE_MMIO_PLIC_BASE + CORE_MMIO_PLIC_SIZE)
#define CORE_MMIO_CLINT_BASE            (CORE_MMIO_BASE + 0x01000000U)
#define CORE_MMIO_CLINT_SIZE            (0x00010000U)
#define CORE_MMIO_CLINT_END                                                    \
    (CORE_MMIO_CLINT_BASE + CORE_MMIO_CLINT_SIZE)

/* RISCV32 PLIC register definition (M-mode context 0, IRQ 1..31) */
#define CORE_MMIO_PLIC_IRQ_NUM          (32U)
#define CORE_MMIO_PLIC_MAX_PRIO         (7U)
#define CORE_MMIO_PLIC_PRIORITY_BASE    (CORE_MMIO_PLIC_BASE + 0x000000U)
#define CORE_MMIO_PLIC_PRIORITY_END                                            \
    (CORE_MMIO_PLIC_PRIORITY_BASE + (CORE_MMIO_PLIC_IRQ_NUM * 4U))
#define CORE_MMIO_PLIC_PENDING          (CORE_MMIO_PLIC_BASE + 0x001000U)
#define CORE_MMIO_PLIC_ENABLE           (CORE_MMIO_PLIC_BASE + 0x002000U)
#define CORE_MMIO_PLIC_THRESHOLD        (CORE_MMIO_PLIC_BASE + 0x200000U)
#define CORE_MMIO_PLIC_CLAIM            (CORE_MMIO_PLIC_BASE + 0x200004U)

/* RISCV32 CLINT register definition (M-mode) */
#define CORE_MMIO_CLINT_MSIP            (CORE_MMIO_CLINT_BASE + 0x0000U)
#define CORE_MMIO_CLINT_MTIMECMP_LO     (CORE_MMIO_CLINT_BASE + 0x4000U)
#define CORE_MMIO_CLINT_MTIMECMP_HI     (CORE_MMIO_CLINT_BASE + 0x4004U)
#define CORE_MMIO_CLINT_MTIME_LO        (CORE_MMIO_CLINT_BASE + 0xBFF8U)
#define CORE_MMIO_CLINT_MTIME_HI        (CORE_MMIO_CLINT_BASE + 0xBFFCU)

/**
 * @brief This function reads a value from the RISCV32 MMIO address space.
 *
 * @param core The RISCV32 core.
 * @param addr The target MMIO address.
 * @param size The access width in bytes(1, 2, or 4).
 * @param val The output value pointer.
 * @return 0 on success, a negative exception number on failure.
 */
int xvm_riscv32_mmio_load(struct core *core, mr_uint32_t addr, mr_size_t size,
                          mr_uint32_t *val);

/**
 * @brief This function writes a value to the RISCV32 MMIO address space.
 *
 * @param core The RISCV32 core.
 * @param addr The target MMIO address.
 * @param size The access width in bytes(1, 2, or 4).
 * @param val The value to write.
 * @return 0 on success, a negative exception number on failure.
 */
int xvm_riscv32_mmio_store(struct core *core, mr_uint32_t addr, mr_size_t size,
                           mr_uint32_t val);
#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __RISCV32_MMIO_H__ */

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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup XVM-RISCV32
 * @{
 */

#if defined(MR_USE_MODULE_XVM_RISCV32)
/* RISCV32 MMIO definition */
#define CORE_MMIO_BASE                  (0x40000000U)
#define CORE_MMIO_SIZE                  (0x10000000U)
#define CORE_MMIO_END                   (CORE_MMIO_BASE + CORE_MMIO_SIZE)

/* RISCV32 MMIO peripheral definition */
#define CORE_MMIO_INTR_BASE             (CORE_MMIO_BASE + 0x0000U)
#define CORE_MMIO_INTR_END              (CORE_MMIO_BASE + 0x0400U)

/* RISCV32 MMIO interrupt controller definition */
#define CORE_MMIO_INTR_PENDING          (CORE_MMIO_INTR_BASE + 0x0000U)
#define CORE_MMIO_INTR_ENABLE           (CORE_MMIO_INTR_BASE + 0x0004U)
#endif /* defined(MR_USE_MODULE_XVM_RISCV32) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __RISCV32_MMIO_H__ */

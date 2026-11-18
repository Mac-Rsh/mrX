/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#include <mrX/port/mr_port_irq.h>

/* QingKe global interrupt CSR definition */
#if defined(MR_USE_CH32V00X) || defined(MR_USE_CH32VM00X)                      \
    || defined(MR_USE_CH32V10X)
#define CH32_IRQ_CSR                    "mstatus"
#else
#define CH32_IRQ_CSR                    "0x800"
#endif /* CH32 EVT SDK family */

/* QingKe global interrupt mask definition */
#define CH32_IRQ_MASK                   (0x88U)

int mr_port_irq_disable(void) {
    mr_uint32_t status;

    /* Disable irq and save the previous state */
    __asm__ volatile ("csrrc %0, " CH32_IRQ_CSR ", %1\n\tfence.i"
                      : "=r"(status) : "r"(CH32_IRQ_MASK) : "memory");
    return (int)(status & CH32_IRQ_MASK);
}

void mr_port_irq_enable(int mask) {
    /* Restore irq state */
    __asm__ volatile ("csrc " CH32_IRQ_CSR ", %0\n\t"
                      "csrs " CH32_IRQ_CSR ", %1"
                      : : "r"(CH32_IRQ_MASK),
                          "r"((mr_uint32_t)mask & CH32_IRQ_MASK) : "memory");
}

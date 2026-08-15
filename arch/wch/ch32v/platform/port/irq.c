/**
 * @copyright (c) 2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2025-11-11        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>

/* Register definition */
#define MSTATUS_MIE                     (1U << 3)

int mr_port_irq_disable(void) {
    int mask;

    /* Save mstatus and clear MIE */
    __asm__ volatile("csrrc %0, mstatus, %1"
                     : "=r"(mask)
                     : "r"(MSTATUS_MIE)
                     : "memory");
    return mask;
}

void mr_port_irq_enable(int mask) {
    /* Restore saved mstatus */
    __asm__ volatile("csrw mstatus, %0" : : "r"(mask) : "memory");
}

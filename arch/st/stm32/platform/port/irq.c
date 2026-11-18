/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#include <mrX/port/mr_port_irq.h>

int mr_port_irq_disable(void) {
    int mask;

    /* Save irq state */
    mask = (int)__get_PRIMASK();

    /* Disable irq */
    __disable_irq();
    return mask;
}

void mr_port_irq_enable(int mask) {
    /* Restore irq state */
    __set_PRIMASK((uint32_t)mask);
}

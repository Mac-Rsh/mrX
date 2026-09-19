/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include <mrX/platform/mr_platform.h>
#include <mrX/port/mr_port_irq.h>

int mr_port_irq_disable(void) {
    int mask = (int)__get_PRIMASK();

    __disable_irq();
    return mask;
}

void mr_port_irq_enable(int mask) {
    __set_PRIMASK((uint32_t)mask);
}

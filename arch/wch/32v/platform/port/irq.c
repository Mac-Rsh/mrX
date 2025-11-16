/**
* @copyright (c) 2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2025-11-11        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#include <mrX/libc/mr_compiler.h>

/* Irq nest counter */
static volatile int __irq_nest = 0;

int mr_port_irq_disable(void) {
    /* Disable interrupt */
    __disable_irq();

    /* Increment irq nest */
    return (++__irq_nest);
}

void mr_port_irq_enable(int mask) {
    MR_UNUSED(mask);

    /* Decrement irq nest */
    if ((--__irq_nest) > 0) {
        return;
    }

    /* Enable interrupt */
    __enable_irq();
}

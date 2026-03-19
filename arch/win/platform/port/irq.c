/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-01-08        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#include <mrX/libc/mr_compiler.h>

/* Global critical section */
static CRITICAL_SECTION __irq_lock;
/* IRQ nest count */
static int __irq_nest = 0;

void mr_win_platform_irq_init(void) {
    /* Init critical section */
    InitializeCriticalSection(&__irq_lock);
}

int mr_port_irq_disable(void) {
    int mask;

    /* Enter critical section */
    EnterCriticalSection(&__irq_lock);

    /* Increase nest count */
    mask = __irq_nest++;
    return mask;
}

void mr_port_irq_enable(int mask) {
    MR_IGNORE(mask);

    /* Decrease nest count */
    __irq_nest--;

    /* Leave critical section */
    LeaveCriticalSection(&__irq_lock);
}

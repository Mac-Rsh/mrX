/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-01-08        MacRsh        First version
 * @date 2026-09-07        MacRsh        Restore nest mask on enable
 */

#include <mrX/platform/mr_platform.h>

/* IRQ lock spin count */
#define IRQ_LOCK_SPIN_COUNT             (4000)

/* Global critical section */
static CRITICAL_SECTION __irq_lock;
/* IRQ nest count */
static int __irq_nest = 0;

void mr_win_platform_irq_init(void) {
    /* Init critical section */
    if (!InitializeCriticalSectionAndSpinCount(&__irq_lock,
                                               IRQ_LOCK_SPIN_COUNT)) {
        InitializeCriticalSection(&__irq_lock);
    }
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
    /* Restore nest count */
    __irq_nest = mask;

    /* Leave critical section */
    LeaveCriticalSection(&__irq_lock);
}

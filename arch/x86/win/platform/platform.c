/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-01-08        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#include <mrX/libc/mr_types.h>

void mr_platform_init(void) {
    /* Init platform irq */
    extern void x86_win_platform_irq_init(void);
    x86_win_platform_irq_init();

    /* Init platform clock */
    extern void x86_win_platform_clock_init(mr_uint32_t per_second);
    x86_win_platform_clock_init(MR_CFG_TICK_PER_SECOND);
}

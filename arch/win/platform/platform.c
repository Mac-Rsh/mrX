/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-01-08        MacRsh        First version
 * @date 2026-09-07        MacRsh        Init irq defer
 */

#include <mrX/platform/mr_platform.h>
#include <mrX/libc/mr_types.h>

void mr_platform_init(void) {
    /* Init platform irq */
    extern void mr_win_platform_irq_init(void);
    mr_win_platform_irq_init();

    /* Init platform clock */
    extern void mr_win_platform_clock_init(mr_uint32_t per_second);
    mr_win_platform_clock_init(MR_CFG_TICK_PER_SECOND);

    /* Init platform irq defer */
    extern void mr_win_platform_irq_defer_init(void);
    mr_win_platform_irq_defer_init();
}

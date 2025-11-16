/**
 * @copyright (c) 2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2025-11-11        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#include <mrX/libc/mr_types.h>

void mr_platform_init(void) {
    /* Init platform clock */
    extern void mr_platform_clock_init(mr_uint32_t per_second);
    mr_platform_clock_init(MR_CFG_TICK_PER_SECOND);

    /* Init platform irq defer */
    extern void mr_platform_irq_defer_init(void);
    mr_platform_irq_defer_init();
}

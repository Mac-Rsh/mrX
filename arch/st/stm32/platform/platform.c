/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>

void mr_platform_init(void) {
    /* Init STM32 clock */
    mr_stm32_platform_clock_init(MR_CFG_TICK_PER_SECOND);
}

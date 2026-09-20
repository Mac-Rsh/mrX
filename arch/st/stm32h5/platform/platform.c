/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-09-20        MacRsh        Drop parallel GPIO clock switch
 */

#include <mrX/platform/mr_platform.h>

void mr_platform_init(void) {
    /* Initialize the STM32H5 clock port */
    mr_stm32h5_platform_clock_init(MR_CFG_TICK_PER_SECOND);
}

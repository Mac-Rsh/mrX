/**
 * @copyright (c) 2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2025-11-11        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#include <mrX.h>

void SW_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void SW_Handler(void) {
#if defined(MR_USE_IRQ_DEFER)
    /* Execute irq defer */
    mr_irq_defer_execute();
#endif /* defined(MR_USE_IRQ_DEFER) */
}

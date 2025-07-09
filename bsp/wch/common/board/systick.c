/**
 * @copyright (c) 2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2025-06-11        MacRsh        First version
 */

#include <board/mr_board.h>
#include <mr_x.h>

extern uint32_t SystemCoreClock;

void SysTick_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void SysTick_Handler(void) {
    /* Clear the interrupt */
    SysTick->SR &= ~(1 << 0);

    /* Increase clock */
    mr_clock_increase(1);
}

static void systick_init(mr_uint32_t tick) {
    /* Init systick */
    SysTick->CTLR = 0;
    SysTick->SR = 0;
    SysTick->CNT = 0;
    SysTick->CMP = (SystemCoreClock / tick) - 1;
    SysTick->CTLR = 0xf;

    /* Init NVIC */
    NVIC_SetPriority(SysTicK_IRQn, MR_CFG_WCH_SYSTICK_IRQ_PRIORITY);
    NVIC_EnableIRQ(SysTicK_IRQn);
}

void mr_board_systick_init(void) {
    /* Init systick */
    systick_init(MR_CFG_TICK_PER_SECOND);
}

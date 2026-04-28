/**
 * @copyright (c) 2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2025-11-11        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_IRQ_DEFER_HOOK)
#include <mrX/kernel/mr_irq.h>

void SW_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void SW_Handler(void) {
#if defined(MR_USE_IRQ_DEFER)
    /* Execute irq defer */
    mr_irq_defer_execute();
#endif /* defined(MR_USE_IRQ_DEFER) */
}

static void irq_defer_hook_suspend(void *priv) {
    MR_IGNORE(priv);

    /* Clear soft irq */
    SysTick->CTLR &= ~(1U << 31);
}

static void irq_defer_hook_wakeup(void *priv) {
    MR_IGNORE(priv);

    /* Trigger soft irq */
    SysTick->CTLR |= (1U << 31);
}
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */

void mr_ch32v_platform_irq_defer_init(void) {
#if defined(MR_USE_IRQ_DEFER_HOOK)
    static const mr_irq_defer_hook_t __hook
        = {.suspend = irq_defer_hook_suspend, .wakeup = irq_defer_hook_wakeup};

    /* Init irq defer hook */
    mr_irq_defer_hook_set(&__hook);

    /* Init NVIC */
#if !defined(MR_CFG_WCH_DEFER_IRQ_PRIORITY)
#define MR_CFG_WCH_DEFER_IRQ_PRIORITY   (2)
#endif /* !defined(MR_CFG_WCH_DEFER_IRQ_PRIORITY) */
    NVIC_SetPriority(Software_IRQn, MR_CFG_WCH_DEFER_IRQ_PRIORITY);
    NVIC_EnableIRQ(Software_IRQn);
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */
}

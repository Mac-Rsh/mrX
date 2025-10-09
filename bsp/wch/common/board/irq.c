/**
 * @copyright (c) 2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2025-06-11        MacRsh        First version
 */

#include <board/mr_board.h>
#include <mr_x.h>

#if defined(MR_USE_IRQ)
static void irq_priority_set(mr_uint32_t irq, mr_uint8_t priority, void *priv);
static void irq_type_set(mr_uint32_t irq, mr_uint8_t type, void *priv);
static void irq_enable(mr_uint32_t irq, void *priv);
static void irq_unmask(mr_uint32_t irq, void *priv);
static void irq_disable(mr_uint32_t irq, void *priv);
static void irq_mask(mr_uint32_t irq, void *priv);
static mr_irq_ops_t __ops = {.priority_set = irq_priority_set,
                             .type_set = irq_type_set,
                             .enable = irq_enable,
                             .unmask = irq_unmask,
                             .disable = irq_disable,
                             .mask = irq_mask};
static mr_irq_t __irq;
#if defined(MR_USE_IRQ_DEFER_HOOK)
static void irq_defer_hook_suspend(void *param);
static void irq_defer_hook_wakeup(void *param);
static const mr_irq_defer_hook_t __hook
    = {.suspend = irq_defer_hook_suspend, .wakeup = irq_defer_hook_wakeup};
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */
static mr_uint8_t __irqn[] = {
    WWDG_IRQn,
};

static void irq_priority_set(mr_uint32_t irq, mr_uint8_t priority,
                             void *priv) {
    MR_UNUSED(priv);

    /* Set irq priority */
    NVIC_SetPriority((IRQn_Type)__irqn[irq], priority);
}

static void irq_type_set(mr_uint32_t irq, mr_uint8_t type, void *priv) {
    MR_UNUSED(irq);
    MR_UNUSED(type);
    MR_UNUSED(priv);
}

static void irq_enable(mr_uint32_t irq, void *priv) {
    MR_UNUSED(priv);

    /* Enable irq */
    NVIC_EnableIRQ((IRQn_Type)__irqn[irq]);
}

static void irq_unmask(mr_uint32_t irq, void *priv) {
    MR_UNUSED(irq);
    MR_UNUSED(priv);
}

static void irq_disable(mr_uint32_t irq, void *priv) {
    MR_UNUSED(priv);

    /* Disable irq */
    NVIC_DisableIRQ((IRQn_Type)__irqn[irq]);
}

static void irq_mask(mr_uint32_t irq, void *priv) {
    MR_UNUSED(irq);
    MR_UNUSED(priv);
}

#if defined(MR_USE_IRQ_DEFER_HOOK)
static void irq_defer_hook_suspend(void *priv) {
    MR_UNUSED(priv);

    /* Clear soft irq */
    SysTick->CTLR &= ~(1U << 31);
}

static void irq_defer_hook_wakeup(void *priv) {
    MR_UNUSED(priv);

    /* Trigger soft irq */
    SysTick->CTLR |= (1U << 31);
}

void SW_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void SW_Handler(void) {
    mr_irq_defer_execute();
}
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */

void mr_board_irq_init(void) {
    mr_err_t ret;

    /* Init irq */
    ret = mr_irq_init(&__irq, 0,
                      MR_MIN(MR_ARRAY_SIZE(__irqn), MR_CFG_IRQ_TABLE_SIZE - 1),
                      &__ops, MR_NULL);
    if (ret != 0) {
        MR_ASSERT(ret == 0);
        return;
    }

#if defined(MR_USE_IRQ_DEFER_HOOK)
    /* Init irq defer hook */
    mr_irq_defer_hook_set(&__hook);

    /* Init NVIC */
    NVIC_SetPriority(Software_IRQn, MR_CFG_WCH_IRQ_DEFER_PRIORITY);
    NVIC_EnableIRQ(Software_IRQn);
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */
}
MR_INIT_EXPORT(mr_board_irq_init, MR_INIT_LEVEL_IRQ);
#endif /* defined(MR_USE_IRQ) */

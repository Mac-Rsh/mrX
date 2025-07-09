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
static void irq_priority_set(mr_uint32_t irq, mr_uint8_t priority, void *param);
static void irq_type_set(mr_uint32_t irq, mr_uint8_t type, void *param);
static void irq_enable(mr_uint32_t irq, void *param);
static void irq_unmask(mr_uint32_t irq, void *param);
static void irq_disable(mr_uint32_t irq, void *param);
static void irq_mask(mr_uint32_t irq, void *param);
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
static mr_irq_defer_hook_t __hook
    = {.suspend = irq_defer_hook_suspend, .wakeup = irq_defer_hook_wakeup};
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */

#if defined(MR_USE_IRQ)
static void irq_priority_set(mr_uint32_t irq, mr_uint8_t priority,
                             void *param) {
    MR_UNUSED(param);

    /* Set irq priority */
    NVIC_SetPriority((IRQn_Type)irq, priority);
}

static void irq_type_set(mr_uint32_t irq, mr_uint8_t type, void *param) {
    MR_UNUSED(irq);
    MR_UNUSED(type);
    MR_UNUSED(param);
}

static void irq_enable(mr_uint32_t irq, void *param) {
    MR_UNUSED(param);

    /* Enable irq */
    NVIC_EnableIRQ((IRQn_Type)irq);
}

static void irq_unmask(mr_uint32_t irq, void *param) {
    MR_UNUSED(irq);
    MR_UNUSED(param);
}

static void irq_disable(mr_uint32_t irq, void *param) {
    MR_UNUSED(param);

    /* Disable irq */
    NVIC_DisableIRQ((IRQn_Type)irq);
}

static void irq_mask(mr_uint32_t irq, void *param) {
    MR_UNUSED(irq);
    MR_UNUSED(param);
}
#endif /* defined(MR_USE_IRQ) */

#if defined(MR_USE_IRQ_DEFER_HOOK)
static void irq_defer_hook_suspend(void *param) {
    MR_UNUSED(param);

    /* Clear soft irq */
    SysTick->CTLR &= ~(1 << 31);
}

static void irq_defer_hook_wakeup(void *param) {
    MR_UNUSED(param);

    /* Trigger soft irq */
    SysTick->CTLR |= (1 << 31);
}

void SW_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void SW_Handler(void) {
    mr_irq_defer_execute();
}
#endif /* defined(MR_USE_IRQ_DEFER_HOOK) */

void mr_board_irq_init(void) {
    mr_err_t ret;

#if (MR_CFG_WCH_IRQ_END == MR_CFG_IRQ_TABLE_SIZE)
#error "IRQ size exceed IRQ table, adjust 'MR_CFG_WCH_IRQ_END'"
#endif /* (MR_CFG_WCH_IRQ_END == MR_CFG_IRQ_TABLE_SIZE) */
    /* Init irq */
    ret = mr_irq_init(&__irq, MR_CFG_WCH_IRQ_BEGIN, MR_CFG_WCH_IRQ_END, &__ops,
                      MR_NULL);
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

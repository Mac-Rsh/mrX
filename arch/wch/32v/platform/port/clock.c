/**
 * @copyright (c) 2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2025-11-11        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_CLOCK_HIRES)
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/kernel/mr_clock.h>

/* High resolution clock lock */
static mr_spinlock_t __lock = MR_SPINLOCK_INIT();
/* High resolution clock coarse tick */
static mr_uint64_t __hires_coarse_tick = 0;

/* High resolution clock definition */
#define HIRES_COARSE_INTERVAL                                                  \
    (MR_CFG_HIRES_TICK_PER_SECOND / MR_CFG_TICK_PER_SECOND)
#endif /* defined(MR_USE_CLOCK_HIRES) */

void SysTick_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void SysTick_Handler(void) {
    int mask;

    /* Clear the interrupt */
    SysTick->SR &= ~(1 << 0);

#if defined(MR_USE_CLOCK_HIRES)
    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__lock);

    /* Increase hires coarse tick */
    __hires_coarse_tick += HIRES_COARSE_INTERVAL;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__lock, mask);
#else
    MR_UNUSED(mask);
#endif /* defined(MR_USE_CLOCK_HIRES) */

    /* Increase clock */
    mr_clock_increase(1);
}

void mr_platform_clock_init(mr_uint32_t per_second) {
    /* Init systick */
    SysTick->CTLR = 0;
    SysTick->CMP = (SystemCoreClock / per_second) - 1;
    SysTick->CNT = 0;
    SysTick->SR = 0;
    SysTick->CTLR = 0xf;

    /* Init NVIC */
#if !defined(MR_CFG_WCH_CLOCK_IRQ_PRIORITY)
#define MR_CFG_WCH_CLOCK_IRQ_PRIORITY   (0)
#endif /* !defined(MR_CFG_WCH_CLOCK_IRQ_PRIORITY) */
    NVIC_SetPriority(SysTicK_IRQn, MR_CFG_WCH_CLOCK_IRQ_PRIORITY);
    NVIC_EnableIRQ(SysTicK_IRQn);
}

#if defined(MR_USE_CLOCK_HIRES)
mr_uint64_t mr_port_clock_hires_tick(void) {
    mr_uint64_t thin_tick, hires_tick;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__lock);

    /* Get high resolution clock thin tick */
    thin_tick = SysTick->CNT * HIRES_COARSE_INTERVAL;
    thin_tick /= SysTick->CMP;

    /* Get high resolution clock tick */
    hires_tick = __hires_coarse_tick + thin_tick;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__lock, mask);
    return hires_tick;
}

void mr_port_clock_hires_tick_set(mr_uint64_t tick) {
    mr_uint64_t thin_tick, coarse_tick;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__lock);

    /* Set high resolution clock thin & coarse tick */
    thin_tick = tick % HIRES_COARSE_INTERVAL;
    coarse_tick = tick - thin_tick;
    thin_tick *= SysTick->CMP;
    thin_tick += (HIRES_COARSE_INTERVAL / 2);
    thin_tick /= HIRES_COARSE_INTERVAL;
    SysTick->CNT = thin_tick;

    /* Set hires coarse tick */
    __hires_coarse_tick = coarse_tick;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__lock, mask);
}
#endif /* defined(MR_USE_CLOCK_HIRES) */

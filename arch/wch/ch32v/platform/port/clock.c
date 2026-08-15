/**
 * @copyright (c) 2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2025-11-11        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_CLOCK_HIRES)
#include <mrX/kernel/mr_clock.h>

/* High resolution clock coarse tick */
static mr_uint64_t __hires_coarse_tick = 0;

/* High resolution clock definition */
#define HIRES_COARSE_INTERVAL                                                  \
    (MR_CFG_HIRES_TICK_PER_SECOND / MR_CFG_TICK_PER_SECOND)
#endif /* defined(MR_USE_CLOCK_HIRES) */

void SysTick_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void SysTick_Handler(void) {
    /* Clear the interrupt */
    SysTick->SR &= ~(1 << 0);

#if defined(MR_USE_CLOCK_HIRES)
    /* Increase coarse tick */
    __hires_coarse_tick += HIRES_COARSE_INTERVAL;
#endif /* defined(MR_USE_CLOCK_HIRES) */

    /* Increase clock */
    mr_clock_increase(1);
}

void mr_ch32v_platform_clock_init(mr_uint32_t per_second) {
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
    mr_uint64_t thin, coarse, coarse2, hires;
    mr_uint32_t retry;

    /* Get current coarse tick */
    coarse = __hires_coarse_tick;

    /* Dual sampling */
    for (retry = 5; retry > 0; retry--) {
        /* Get current thin tick */
        thin = SysTick->CNT * HIRES_COARSE_INTERVAL;
        thin /= SysTick->CMP;

        /* Calculate high resolution tick */
        hires = coarse + thin;

        /* Check if coarse tick is updated */
        coarse2 = __hires_coarse_tick;
        if (coarse2 != coarse) {
            /* Retry */
            coarse = coarse2;
            continue;
        }

        /* No updated */
        break;
    }
    return hires;
}

void mr_port_clock_hires_tick_set(mr_uint64_t tick) {
    mr_uint64_t thin, coarse;

    /* Stop systick */
    SysTick->CTLR = 0;

    /* Set thin tick */
    thin = tick % HIRES_COARSE_INTERVAL;
    coarse = tick - thin;
    thin = (thin * SysTick->CMP) + (HIRES_COARSE_INTERVAL / 2);
    thin /= HIRES_COARSE_INTERVAL;
    SysTick->CNT = thin;

    /* Set coarse tick */
    __hires_coarse_tick = coarse;

    /* Start systick */
    SysTick->CTLR = 0xf;
}
#endif /* defined(MR_USE_CLOCK_HIRES) */

void mr_port_clock_busy_wait(mr_uint32_t us) {
    mr_uint64_t period, target, last, elapsed;

    /* Init start time */
    target = ((mr_uint64_t)us * SystemCoreClock + 999999ULL) / 1000000ULL;
    period = SysTick->CMP + 1;
    last = SysTick->CNT;
    elapsed = 0;

    /* Wait */
    while (elapsed < target) {
        cur = SysTick->CNT;
        if (cur < last) {
            elapsed += period - last + cur;
        } else {
            elapsed += cur - last;
        }
        last = cur;
    }
}

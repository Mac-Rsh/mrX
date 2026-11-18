/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#include <mrX/port/mr_port_clock.h>
#include <mrX/kernel/mr_clock.h>
#include <mrX/port/mr_port_irq.h>

#if defined(MR_USE_CLOCK_HIRES)
#if (!defined(DWT) || !defined(DWT_CTRL_CYCCNTENA_Msk)                         \
     || !defined(CoreDebug_DEMCR_TRCENA_Msk))
#error "MR_USE_CLOCK_HIRES requires DWT CYCCNT support"
#endif /* (!defined(DWT) || !defined(DWT_CTRL_CYCCNTENA_Msk)                   \
     || !defined(CoreDebug_DEMCR_TRCENA_Msk)) */
#endif /* defined(MR_USE_CLOCK_HIRES) */

/* Low resolution clock state */
static volatile mr_bool_t __clock_ready;
static mr_uint32_t __clock_fraction;
static mr_uint32_t __clock_rate;

#if defined(MR_USE_CLOCK_HIRES)
/* High resolution clock state */
static volatile mr_uint32_t __clock_hires_cycle;
static volatile mr_uint64_t __clock_hires_tick;
static volatile mr_bool_t __clock_hires_ready;
static mr_uint32_t __clock_hires_fraction;

static void clock_dwt_init(void) {
    /* Init DWT cycle counter */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();
    __clock_hires_ready = MR_TRUE;
}
#endif /* defined(MR_USE_CLOCK_HIRES) */

static void clock_systick_busy_wait(mr_uint32_t us) {
    mr_uint64_t denominator, target, elapsed;
    mr_uint32_t current, reload, last;

    /* Check SysTick */
    if ((SysTick->CTRL & SysTick_CTRL_ENABLE_Msk) == 0U) {
        return;
    }

    /* Get SysTick period */
    reload = SysTick->LOAD + 1U;

    /* Check clock parameter */
    if ((reload == 0U) || (uwTickFreq == 0U)) {
        return;
    }

    /* Calculate target SysTick cycles */
    denominator = (mr_uint64_t)uwTickFreq * 1000ULL;
    target = ((mr_uint64_t)us * reload + denominator - 1ULL) / denominator;

    /* Wait for target cycles */
    last = SysTick->VAL;
    elapsed = 0U;
    while (elapsed < target) {
        current = SysTick->VAL;
        if (current > last) {
            elapsed += last + reload - current;
        } else {
            elapsed += last - current;
        }
        last = current;
    }
}

void mr_stm32_platform_clock_init(mr_uint32_t per_second) {
    int mask;

    /* Check parameter */
    if (per_second == 0U) {
        return;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Init low resolution clock */
    __clock_rate = per_second;
    __clock_ready = MR_FALSE;
    __clock_fraction = 0U;

    /* Set SysTick irq priority */
#if !defined(MR_CFG_STM32_CLOCK_IRQ_PRIORITY)
#define MR_CFG_STM32_CLOCK_IRQ_PRIORITY (15U)
#endif /* !defined(MR_CFG_STM32_CLOCK_IRQ_PRIORITY) */
    HAL_NVIC_SetPriority(SysTick_IRQn, MR_CFG_STM32_CLOCK_IRQ_PRIORITY, 0U);

#if defined(MR_USE_CLOCK_HIRES)
    /* Init high resolution clock */
    clock_dwt_init();
    mr_port_clock_hires_tick_set(0U);
#endif /* defined(MR_USE_CLOCK_HIRES) */

    /* Mark clock ready */
    __clock_ready = MR_TRUE;

    /* Unlock */
    mr_port_irq_enable(mask);
}

void HAL_IncTick(void) {
    mr_uint64_t scaled;
    mr_tick_t ticks;

    /* Increase HAL tick */
    uwTick += (uint32_t)uwTickFreq;

    /* Check clock state */
    if (!__clock_ready) {
        return;
    }

#if defined(MR_USE_CLOCK_HIRES)
    /* Extend high resolution clock */
    mr_port_clock_hires_tick();
#endif /* defined(MR_USE_CLOCK_HIRES) */

    /* Calculate low resolution ticks */
    scaled = __clock_fraction + (mr_uint64_t)__clock_rate * uwTickFreq;
    __clock_fraction = (mr_uint32_t)(scaled % 1000U);
    ticks = (mr_tick_t)(scaled / 1000U);

    /* Increase low resolution clock */
    if (ticks != 0U) {
        mr_clock_increase(ticks);
    }
}

#if defined(MR_USE_CLOCK_HIRES)
mr_uint64_t mr_port_clock_hires_tick(void) {
    mr_uint32_t cycle, elapsed;
    mr_uint64_t scaled, tick;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check clock state */
    if (!__clock_hires_ready) {
        mr_port_irq_enable(mask);
        return 0U;
    }

    /* Calculate elapsed cycles */
    cycle = DWT->CYCCNT;
    elapsed = cycle - __clock_hires_cycle;

    /* Convert elapsed cycles */
    scaled = (mr_uint64_t)elapsed * MR_CFG_HIRES_TICK_PER_SECOND
           + __clock_hires_fraction;

    /* Update high resolution clock */
    __clock_hires_tick += scaled / SystemCoreClock;
    __clock_hires_fraction = (mr_uint32_t)(scaled % SystemCoreClock);
    __clock_hires_cycle = cycle;
    tick = __clock_hires_tick;

    /* Unlock */
    mr_port_irq_enable(mask);
    return tick;
}

void mr_port_clock_hires_tick_set(mr_uint64_t tick) {
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Set high resolution clock */
    __clock_hires_tick = tick;
    __clock_hires_cycle = DWT->CYCCNT;
    __clock_hires_fraction = 0U;

    /* Unlock */
    mr_port_irq_enable(mask);
}
#endif /* defined(MR_USE_CLOCK_HIRES) */

void mr_port_clock_busy_wait(mr_uint32_t us) {
    /* Check parameter */
    if (us == 0U) {
        return;
    }

    /* Wait */
    clock_systick_busy_wait(us);
}

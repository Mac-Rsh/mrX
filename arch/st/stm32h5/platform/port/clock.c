/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include <mrX/platform/mr_platform.h>
#include <mrX/port/mr_port_clock.h>
#include <mrX/kernel/mr_clock.h>
#include <mrX/port/mr_port_irq.h>

#if !defined(MR_CFG_STM32H5_CLOCK_IRQ_PRIORITY)
#define MR_CFG_STM32H5_CLOCK_IRQ_PRIORITY (15U)
#endif /* !defined(MR_CFG_STM32H5_CLOCK_IRQ_PRIORITY) */

static mr_uint32_t __clock_rate;
static mr_uint32_t __clock_fraction;
static volatile mr_bool_t __clock_ready;

#if defined(MR_USE_CLOCK_HIRES)
/* DWT is a 32-bit free-running cycle counter on Cortex-M33. */
static volatile mr_uint64_t __clock_hires_tick;
static volatile mr_uint32_t __clock_hires_cycle;
static mr_uint32_t __clock_hires_fraction;
static volatile mr_bool_t __clock_hires_ready;

static void clock_dwt_init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();
    __clock_hires_ready = MR_TRUE;
}
#endif /* defined(MR_USE_CLOCK_HIRES) */

void mr_stm32h5_platform_clock_init(mr_uint32_t per_second) {
    int mask;

    if (per_second == 0U) {
        return;
    }

    mask = mr_port_irq_disable();
    __clock_ready = MR_FALSE;
    /* HAL owns the millisecond time base; scale mrX ticks in HAL_IncTick */
    __clock_rate = per_second;
    __clock_fraction = 0U;

    HAL_NVIC_SetPriority(SysTick_IRQn, MR_CFG_STM32H5_CLOCK_IRQ_PRIORITY, 0U);

#if defined(MR_USE_CLOCK_HIRES)
    clock_dwt_init();
    mr_port_clock_hires_tick_set(0U);
#endif /* defined(MR_USE_CLOCK_HIRES) */
    __clock_ready = MR_TRUE;
    mr_port_irq_enable(mask);
}

void HAL_IncTick(void) {
    mr_uint64_t scaled;
    mr_tick_t ticks;

    uwTick += (uint32_t)uwTickFreq;
    if (!__clock_ready) {
        return;
    }
#if defined(MR_USE_CLOCK_HIRES)
    /* Extend CYCCNT on every HAL tick, even without application reads */
    mr_port_clock_hires_tick();
#endif /* defined(MR_USE_CLOCK_HIRES) */
    scaled = __clock_fraction + (mr_uint64_t)__clock_rate * uwTickFreq;
    ticks = (mr_tick_t)(scaled / 1000U);
    __clock_fraction = (mr_uint32_t)(scaled % 1000U);
    if (ticks != 0U) {
        mr_clock_increase(ticks);
    }
}

#if defined(MR_USE_CLOCK_HIRES)
mr_uint64_t mr_port_clock_hires_tick(void) {
    mr_uint64_t scaled;
    mr_uint64_t tick;
    mr_uint32_t cycle;
    mr_uint32_t elapsed;
    int mask;

    mask = mr_port_irq_disable();
    if (!__clock_hires_ready) {
        mr_port_irq_enable(mask);
        return 0U;
    }

    cycle = DWT->CYCCNT;
    elapsed = cycle - __clock_hires_cycle;
    scaled = (mr_uint64_t)elapsed * MR_CFG_HIRES_TICK_PER_SECOND
           + __clock_hires_fraction;
    __clock_hires_tick += scaled / SystemCoreClock;
    __clock_hires_fraction = (mr_uint32_t)(scaled % SystemCoreClock);
    __clock_hires_cycle = cycle;
    tick = __clock_hires_tick;
    mr_port_irq_enable(mask);
    return tick;
}

void mr_port_clock_hires_tick_set(mr_uint64_t tick) {
    int mask;

    mask = mr_port_irq_disable();
    __clock_hires_tick = tick;
    __clock_hires_cycle = DWT->CYCCNT;
    __clock_hires_fraction = 0U;
    mr_port_irq_enable(mask);
}
#endif /* defined(MR_USE_CLOCK_HIRES) */

void mr_port_clock_busy_wait(mr_uint32_t us) {
    mr_uint64_t target;
    mr_uint64_t elapsed;
    mr_uint32_t current;
    mr_uint32_t last;

#if defined(MR_USE_CLOCK_HIRES)
    if (!__clock_hires_ready) {
        return;
    }
#else
    /* DWT is enabled together with the platform clock port. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
#endif /* defined(MR_USE_CLOCK_HIRES) */

    target = ((mr_uint64_t)us * (mr_uint64_t)SystemCoreClock + 999999ULL)
           / 1000000ULL;
    last = DWT->CYCCNT;
    elapsed = 0U;
    while (elapsed < target) {
        current = DWT->CYCCNT;
        elapsed += (mr_uint32_t)(current - last);
        last = current;
    }
}

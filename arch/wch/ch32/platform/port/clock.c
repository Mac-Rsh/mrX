/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#include <mrX/port/mr_port_clock.h>
#include <mrX/port/mr_port_assert.h>
#include <mrX/port/mr_port_irq.h>
#include <mrX/kernel/mr_clock.h>

/* SysTick control definition */
#define CH32_STE                        (1U << 0)
#define CH32_STIE                       (1U << 1)
#define CH32_STCLK                      (1U << 2)

#if defined(MR_USE_CH32V10X)
/* QingKe V3A SysTick clock divider definition */
#define CH32_SYSTICK_DIVIDER            (8U)
#else
#define CH32_SYSTICK_DIVIDER            (1U)
#endif /* defined(MR_USE_CH32V10X) */

/* SysTick interrupt rate limit definition */
#define CH32_SYSTICK_MAX_HZ             (10000U)

/* EVT SysTick interrupt number definition */
#if defined(MR_USE_CH32V205) || defined(MR_USE_CH32V4X7)
#define CH32_SYSTICK_IRQ                SysTick_IRQn
#else
#define CH32_SYSTICK_IRQ                SysTicK_IRQn
#endif /* CH32 EVT SDK family */

/* SysTick count type */
#if defined(MR_USE_CH32V10X) || defined(MR_USE_CH32V20X)                       \
    || defined(MR_USE_CH32V30X)
#define CH32_SYSTICK_64BIT
#endif /* CH32 EVT SDK family */

#if defined(CH32_SYSTICK_64BIT)
typedef mr_uint64_t systick_count_t;
typedef uint32_t systick_word_t __attribute__((may_alias));
#else
typedef mr_uint32_t systick_count_t;
#endif /* defined(CH32_SYSTICK_64BIT) */

/* Low resolution clock state */
static mr_uint32_t __clock_frequency;
static mr_uint32_t __clock_rate;
static mr_uint32_t __clock_period;
static mr_uint32_t __clock_fraction;
static systick_count_t __clock_sample;
static systick_count_t __clock_compare;

#if defined(MR_USE_CLOCK_HIRES)
/* High resolution clock state */
static systick_count_t __hires_sample;
static mr_uint64_t __hires_tick;
static mr_uint32_t __hires_fraction;
#endif /* defined(MR_USE_CLOCK_HIRES) */

static systick_count_t clock_count(void) {
#if defined(CH32_SYSTICK_64BIT)
    volatile const systick_word_t *cnt_high, *cnt_low;
    mr_uint32_t high, low;

    /* Locate the counter words in the EVT layout */
#if defined(MR_USE_CH32V10X)
    cnt_low = (volatile const systick_word_t *)&SysTick->CNTL0;
    cnt_high = (volatile const systick_word_t *)&SysTick->CNTH0;
#else
    cnt_low = (volatile const systick_word_t *)&SysTick->CNT;
    cnt_high = cnt_low + 1;
#endif /* defined(MR_USE_CH32V10X) */

    /* Read both halves without tearing if the low word rolls over */
    do {
        high = *cnt_high;
        low = *cnt_low;
    } while (high != *cnt_high);
    return ((mr_uint64_t)high << 32) | low;
#else
    return SysTick->CNT;
#endif /* defined(CH32_SYSTICK_64BIT) */
}

#if defined(MR_USE_CH32V10X)
static void clock_write_word(mr_uintptr_t address, mr_uint32_t value) {
    mr_uint32_t shift;

    /* V3A allows only byte writes to the counter and compare registers */
    for (shift = 0U; shift < 32U; shift += 8U) {
        *(volatile uint8_t *)address = (uint8_t)(value >> shift);
        address++;
    }
}
#endif /* defined(MR_USE_CH32V10X) */

static void clock_compare(systick_count_t compare) {
#if defined(MR_USE_CH32V10X)
    /* Move the compare point out of range while updating the low word */
    clock_write_word((mr_uintptr_t)&SysTick->CMPHR0, MR_UINT32_MAX);
    clock_write_word((mr_uintptr_t)&SysTick->CMPLR0, (mr_uint32_t)compare);
    clock_write_word((mr_uintptr_t)&SysTick->CMPHR0,
                     (mr_uint32_t)(compare >> 32));
#elif defined(CH32_SYSTICK_64BIT)
    volatile systick_word_t *cmp;

    /* Prevent a spurious match while the compare value is written in halves */
    cmp = (volatile systick_word_t *)&SysTick->CMP;
    cmp[1] = MR_UINT32_MAX;
    cmp[0] = (mr_uint32_t)compare;
    cmp[1] = (mr_uint32_t)(compare >> 32);
#else
    SysTick->CMP = compare;
#endif /* CH32 EVT SDK family */
}

static mr_uint64_t clock_scale(systick_count_t elapsed, mr_uint32_t rate,
                               mr_uint32_t *fraction) {
    mr_uint64_t scaled, whole;

    /* Keep the multiplication bounded even when servicing a delayed tick */
    whole = elapsed / __clock_frequency;
    scaled = (mr_uint64_t)(elapsed % __clock_frequency) * rate + *fraction;
    *fraction = (mr_uint32_t)(scaled % __clock_frequency);
    return whole * rate + scaled / __clock_frequency;
}

void mr_ch32_platform_clock_init(mr_uint32_t per_second) {
    mr_uint32_t interrupt_rate, frequency;
    int mask;

    /* Check parameter */
    frequency = SystemCoreClock / CH32_SYSTICK_DIVIDER;
    if ((frequency == 0U) || (per_second == 0U)) {
        mr_port_assert(__FILE__, __LINE__, "Invalid SysTick frequency");
        return;
    }

    /* Lock */
    mask = mr_port_irq_disable();

    /* Reset SysTick */
    SysTick->CTLR = 0U;
#if defined(MR_USE_CH32V10X)
    clock_write_word((mr_uintptr_t)&SysTick->CNTL0, 0U);
    clock_write_word((mr_uintptr_t)&SysTick->CNTH0, 0U);
#else
    SysTick->SR = 0U;
    SysTick->CNT = 0U;
#endif /* defined(MR_USE_CH32V10X) */
    NVIC_ClearPendingIRQ(CH32_SYSTICK_IRQ);

    /* Init low resolution clock */
    __clock_frequency = frequency;
    __clock_rate = per_second;
    __clock_fraction = 0U;
    __clock_sample = 0U;
#if defined(MR_USE_CLOCK_HIRES)
    /* Reset high resolution clock */
    __hires_sample = 0U;
    __hires_tick = 0U;
    __hires_fraction = 0U;
#endif /* defined(MR_USE_CLOCK_HIRES) */

    /* Batch kernel ticks if the configured rate exceeds the IRQ rate limit */
    interrupt_rate = per_second;
    if (interrupt_rate > CH32_SYSTICK_MAX_HZ) {
        interrupt_rate = CH32_SYSTICK_MAX_HZ;
    }
    __clock_period = (__clock_frequency - 1U) / interrupt_rate + 1U;
    __clock_compare = __clock_period;
    clock_compare(__clock_compare);
    NVIC_SetPriority(CH32_SYSTICK_IRQ, MR_UINT8_MAX);
    NVIC_EnableIRQ(CH32_SYSTICK_IRQ);
#if defined(MR_USE_CH32V10X)
    SysTick->CTLR = CH32_STE;
#else
    SysTick->CTLR = CH32_STCLK | CH32_STIE | CH32_STE;
#endif /* defined(MR_USE_CH32V10X) */

    /* Unlock */
    mr_port_irq_enable(mask);
}

#if defined(MR_USE_CLOCK_HIRES)
mr_uint64_t mr_port_clock_hires_tick(void) {
    systick_count_t now;
    mr_uint64_t tick;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Check clock state */
    if (__clock_frequency == 0U) {
        tick = __hires_tick;
    } else {
        /* Extend high resolution clock */
        now = clock_count();
        __hires_tick += clock_scale(now - __hires_sample,
                                    MR_CFG_HIRES_TICK_PER_SECOND,
                                    &__hires_fraction);
        __hires_sample = now;
        tick = __hires_tick;
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return tick;
}

void mr_port_clock_hires_tick_set(mr_uint64_t tick) {
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();

    /* Set high resolution clock */
    __hires_tick = tick;
    __hires_sample = clock_count();
    __hires_fraction = 0U;

    /* Unlock */
    mr_port_irq_enable(mask);
}
#endif /* defined(MR_USE_CLOCK_HIRES) */

void mr_port_clock_busy_wait(mr_uint32_t us) {
    systick_count_t now, previous;
    mr_uint64_t elapsed, target;

    /* Check clock state */
    if (__clock_frequency == 0U) {
        return;
    }

    /* Calculate target cycles */
    target = ((mr_uint64_t)us * __clock_frequency + 999999ULL) / 1000000ULL;

    /* Wait for target cycles */
    previous = clock_count();
    elapsed = 0U;
    while (elapsed < target) {
        now = clock_count();
        elapsed += (systick_count_t)(now - previous);
        previous = now;
    }
}

void SysTick_Handler(void) MR_CH32_INTERRUPT;

void SysTick_Handler(void) {
    systick_count_t now, elapsed;
    mr_uint64_t ticks;
    mr_uint32_t lead;

#if !defined(MR_USE_CH32V10X)
    /* Acknowledge the compare flag where SysTick has a status register */
    SysTick->SR = 0U;
#endif /* !defined(MR_USE_CH32V10X) */

    /* Check clock state */
    if (__clock_frequency == 0U) {
        return;
    }

    /* Check elapsed count */
    now = clock_count();
    elapsed = now - __clock_sample;
    if (elapsed < (systick_count_t)(__clock_compare - __clock_sample)) {
        return;
    }

    /* Preserve the IRQ cadence and skip compare points already passed */
    lead = __clock_period
         - (mr_uint32_t)((now - __clock_compare) % __clock_period);
    __clock_compare = now + lead;
    clock_compare(__clock_compare);
    if ((systick_count_t)(clock_count() - now) >= lead) {
        /* A late compare write may miss the equality interrupt */
        NVIC_SetPendingIRQ(CH32_SYSTICK_IRQ);
    }

    /* Increase low resolution clock */
    __clock_sample = now;
    ticks = clock_scale(elapsed, __clock_rate, &__clock_fraction);
#if defined(MR_USE_CLOCK_HIRES)
    /* Extend the hardware count even when the application is idle */
    mr_port_clock_hires_tick();
#endif /* defined(MR_USE_CLOCK_HIRES) */
    while (ticks > MR_TICK_MAX) {
        mr_clock_increase(MR_TICK_MAX);
        ticks -= MR_TICK_MAX;
    }
    if (ticks != 0U) {
        mr_clock_increase((mr_tick_t)ticks);
    }
}

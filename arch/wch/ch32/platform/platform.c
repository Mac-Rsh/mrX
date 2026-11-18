/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#if defined(MR_USE_DRIVER_TMR) || defined(MR_USE_DRIVER_PWM)
#include <mrX/port/mr_port_irq.h>

/* Timer period conversion definition */
#define TIMER_NANOSECONDS               (1000000000ULL)
#define TIMER_DIVIDER_MAX               (65536U)

/* Timer owner type */
typedef struct timer_owner {
    const mr_driver_t *drv;
    TIM_TypeDef *tim;
} timer_owner_t;

/* Timer instance limit definition */
#define TIMER_MAX_INSTANCES             (10U)

/* Timer owner table */
static timer_owner_t __timer_owners[TIMER_MAX_INSTANCES];

static mr_uint64_t timer_ticks_to_ns(mr_uint64_t ticks, mr_uint32_t hz) {
    mr_uint64_t seconds, remainder;

    /* Scale the quotient and remainder separately for 32-bit timers */
    seconds = ticks / hz;
    remainder = ((ticks % hz) * TIMER_NANOSECONDS + hz / 2U) / hz;
    if (seconds > (MR_UINT64_MAX - remainder) / TIMER_NANOSECONDS) {
        return MR_UINT64_MAX;
    }
    return seconds * TIMER_NANOSECONDS + remainder;
}

static mr_uint64_t timer_ns_to_counts(mr_uint64_t ns, mr_uint32_t hz,
                                     mr_uint32_t divider) {
    mr_uint64_t ticks, fraction, unit;

    /* Keep fractional ticks until rounding against the prescaled clock */
    fraction = (ns % TIMER_NANOSECONDS) * hz;
    ticks = (ns / TIMER_NANOSECONDS) * hz + fraction / TIMER_NANOSECONDS;
    unit = TIMER_NANOSECONDS * divider;
    fraction = (ticks % divider) * TIMER_NANOSECONDS
               + fraction % TIMER_NANOSECONDS;
    return ticks / divider + (fraction + unit / 2U) / unit;
}

mr_err_t mr_ch32_timer_make_period(mr_uint32_t hz, mr_uint64_t period,
                                   mr_uint64_t max_counts,
                                   mr_uint32_t count_factor,
                                   mr_ch32_timer_base_t *base) {
    mr_uint64_t max_ticks, ticks, counts;
    mr_uint32_t divider;

    /* Check the input and the hardware range before multiplying by Hz */
    if ((hz == 0U) || (period == 0U) || (max_counts == 0U)
        || (max_counts > (mr_uint64_t)MR_UINT32_MAX + 1U)
        || ((count_factor != 1U) && (count_factor != 2U))) {
        return -MR_ERANGE;
    }
    max_ticks = max_counts * TIMER_DIVIDER_MAX;
    if (period > timer_ticks_to_ns(max_ticks * count_factor, hz)) {
        return -MR_ERANGE;
    }
    ticks = timer_ns_to_counts(period, hz, count_factor);
    if ((ticks == 0U) || (ticks > max_ticks)) {
        return -MR_ERANGE;
    }

    /* Preserve timer resolution with the smallest suitable divider */
    divider = (mr_uint32_t)((ticks + max_counts - 1U) / max_counts);
    counts = (ticks + divider / 2U) / divider;
    base->counts = counts;
    base->clocks_per_count = divider * count_factor;
    base->period = timer_ticks_to_ns(counts * base->clocks_per_count, hz);
    base->hz = hz;
    base->divider = divider;
    return 0;
}

#if defined(MR_USE_DRIVER_PWM)
mr_err_t mr_ch32_timer_make_pulse(const mr_ch32_timer_base_t *base,
                                  mr_uint64_t pulse, mr_uint32_t *compare,
                                  mr_uint64_t *actual) {
    mr_uint64_t counts;

    /* Check pulse width */
    if (pulse > base->period) {
        return -MR_EINVAL;
    }

    /* Keep exact full duty even after rounding the reported period */
    counts = (pulse == base->period)
        ? base->counts
        : timer_ns_to_counts(pulse, base->hz, base->clocks_per_count);
    *compare = (mr_uint32_t)counts;
    *actual = timer_ticks_to_ns(counts * base->clocks_per_count, base->hz);
    return 0;
}
#endif /* defined(MR_USE_DRIVER_PWM) */

void mr_ch32_timer_set_reload(TIM_TypeDef *tim, mr_uint32_t reload) {
#if defined(MR_CH32_HAS_TIM5_32BIT)
    /* TIM5 requires a full-width write even when the high half is zero */
    if (tim == TIM5) {
        tim->ATRLR_R32 = reload;
        return;
    }
#elif defined(MR_USE_CH32V205)
    /* TIM4 has a 32-bit reload register in the V205 EVT union */
    if (tim == TIM4) {
        tim->ATRLR_TIM4 = reload;
        return;
    }
#endif /* CH32 timer with 32-bit reload */
    tim->ATRLR = (mr_uint16_t)reload;
}

mr_err_t mr_ch32_timer_claim(TIM_TypeDef *tim, const mr_driver_t *drv) {
    mr_uint32_t index;
    mr_err_t ret;
    int mask;

    /* Lock */
    mask = mr_port_irq_disable();
    ret = -MR_ENOMEM;
    for (index = 0U; index < MR_ARRAY_SIZE(__timer_owners); index++) {
        if (__timer_owners[index].tim == tim) {
            ret = (__timer_owners[index].drv == drv) ? 0 : -MR_EBUSY;
            break;
        }
        if (__timer_owners[index].tim == MR_NULL) {
            __timer_owners[index].tim = tim;
            __timer_owners[index].drv = drv;
            ret = 0;
            break;
        }
    }

    /* Unlock */
    mr_port_irq_enable(mask);
    return ret;
}
#endif /* defined(MR_USE_DRIVER_TMR) || defined(MR_USE_DRIVER_PWM) */

void mr_platform_init(void) {
    /* Init CH32 clock */
    SystemCoreClockUpdate();
    mr_ch32_platform_clock_init(MR_CFG_TICK_PER_SECOND);
}

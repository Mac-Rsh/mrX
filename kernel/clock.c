/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/port/mr_port_clock.h>
#include <mrX/port/mr_port_irq.h>
#include <mrX/kernel/mr_service.h>
#include <mrX/kernel/mr_clock.h>
#include <mrX/kernel/mr_hook.h>
#include <mrX/libc/mr_assert.h>
#include <mrX/libc/mr_atomic.h>
#if defined(MR_USE_CLOCK_TRACE)
#define MR_TRACE_TAG                    "clock"
#endif /* defined(MR_USE_CLOCK_TRACE) */
#include <mrX/kernel/mr_trace.h>

/* Clock hook entry */
#if !defined(MR_CFG_CLOCK_HOOK_SIZE)
#define MR_CFG_CLOCK_HOOK_SIZE          (2)
#endif /* !defined(MR_CFG_CLOCK_HOOK_SIZE) */
#if (MR_CFG_CLOCK_HOOK_SIZE < 2)
#error "MR_CFG_CLOCK_HOOK_SIZE must >= 2"
#endif /* (MR_CFG_CLOCK_HOOK_SIZE < 2) */
static mr_ptr_t __clock_entry[MR_CFG_CLOCK_HOOK_SIZE];
/* Clock hook */
static mr_hook_t __clock_hook
    = MR_HOOK_INIT(__clock_entry, MR_CFG_CLOCK_HOOK_SIZE);
/* Clock uptime(coarse) */
static volatile mr_tick_t __clock_uptime = 0;
/* Clock tick */
static volatile mr_tick_t __clock_tick = 0;

static mr_tick_t clock_hook(void) {
    mr_tick_t tickless, less;
    mr_clock_hook_t *entry;
    mr_uint32_t i;

    /* Init tickless */
    tickless = MR_TICK_MAX;

    /* Each clock hooks */
    MR_HOOK_EACH(i, entry, mr_clock_hook_t, &__clock_hook) {
        /* Call clock hook entry */
        less = entry();

        /* Update tickless */
        if ((less > 0) && (less < tickless)) {
            tickless = less;
        }
    }
    return tickless;
}

void mr_clock_increase(mr_tick_t tick) {
    mr_tick_t last, next;
    int mask;

    /* Disable interrupt */
    mask = mr_port_irq_disable();

    /* Update tick and uptime atomically */
    last = __clock_tick;
    next = last + tick;
    __clock_tick = next;
    if (next < last) {
        __clock_uptime++;
    }

    /* Enable interrupt */
    mr_port_irq_enable(mask);

    /* Call clock hooks */
    clock_hook();
}

mr_tick_t mr_clock_tick(void) {
    mr_tick_t tick;
    int mask;

    /* Disable interrupt */
    mask = mr_port_irq_disable();

    /* Get clock tick */
    tick = __clock_tick;

    /* Enable interrupt */
    mr_port_irq_enable(mask);
    return tick;
}

mr_tick_t mr_clock_tickless(void) {
    mr_tick_t tickless;

    /* Get clock tickless */
    tickless = clock_hook();

    /* Trace clock tickless */
    MR_TRACE_COUNTER("tickless", tickless);
    return tickless;
}

mr_err_t mr_clock_hook_add(mr_clock_hook_t *entry) {
    /* Check parameter */
    MR_ASSERT(entry != MR_NULL);

    /* Add hook to clock */
    return mr_hook_add(&__clock_hook, entry);
}

static mr_uint64_t clock_scale_floor(mr_uint64_t tick, mr_uint32_t scale,
                                     mr_uint32_t frequency) {
    mr_uint64_t fraction, quotient;

    /* Split before multiplication */
    fraction = ((tick % frequency) * scale) / frequency;
    quotient = tick / frequency;

    /* Saturate the result */
    if (quotient > ((MR_UINT64_MAX - fraction) / scale)) {
        return MR_UINT64_MAX;
    }
    return quotient * scale + fraction;
}

#if defined(MR_USE_CLOCK_HIRES)
void mr_clock_hires_tick_set(mr_hires_tick_t tick) {
    /* Set clock high resolution tick */
    mr_port_clock_hires_tick_set((mr_uint64_t)tick);
}

mr_hires_tick_t mr_clock_hires_tick(void) {
    /* Get clock high resolution tick */
    return (mr_hires_tick_t)mr_port_clock_hires_tick();
}

mr_hires_timestamp_t mr_hires_tick2timestamp(mr_hires_tick_t tick,
                                             int time_zone) {
    const mr_uint8_t day_of_month_comm[]
        = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const mr_uint8_t day_of_month_leap[]
        = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    mr_int64_t zone_offset, tick_zone;
    mr_hires_timestamp_t timestamp;
    const mr_uint8_t *day_of_month;
    mr_uint16_t day_of_year;
    mr_bool_t is_leap;

    /* Limit time zone */
    time_zone = time_zone <= 12 ? time_zone : 12;
    time_zone = time_zone >= -12 ? time_zone : -12;

    /* Convert high resolution ticks to microseconds */
    tick = clock_scale_floor(tick, 1000000U, MR_CFG_HIRES_TICK_PER_SECOND);

    /* Time zone(UTC + n) */
    zone_offset = (mr_int64_t)time_zone * 3600000000LL;
    tick_zone = (mr_int64_t)tick + zone_offset;
    if (tick_zone < 0) {
        tick_zone = 0;
    }

    /* Microsecond ~ hour */
    timestamp.microsecond = (mr_uint16_t)(tick_zone % 1000);
    tick_zone /= 1000;
    timestamp.millisecond = (mr_uint16_t)(tick_zone % 1000);
    tick_zone /= 1000;
    timestamp.second = (mr_uint8_t)(tick_zone % 60);
    tick_zone /= 60;
    timestamp.minute = (mr_uint8_t)(tick_zone % 60);
    tick_zone /= 60;
    timestamp.hour = (mr_uint8_t)(tick_zone % 24);
    tick_zone /= 24;

    /* Year */
    timestamp.year = 1970;
    while (1) {
        /* Check whether year is leap year */
        is_leap = ((timestamp.year % 4 == 0) && (timestamp.year % 100 != 0))
               || (timestamp.year % 400 == 0);
        day_of_year = is_leap ? 366 : 365;
        if (tick_zone < day_of_year) {
            break;
        }
        tick_zone -= day_of_year;
        timestamp.year++;
    }

    /* Month */
    day_of_month = is_leap ? day_of_month_leap : day_of_month_comm;
    for (timestamp.month = 1;
         tick_zone >= (mr_int64_t)day_of_month[timestamp.month - 1];
         timestamp.month++) {
        tick_zone -= (mr_int64_t)day_of_month[timestamp.month - 1];
    }

    /* Day */
    timestamp.day = (mr_uint8_t)(tick_zone + 1);
    return timestamp;
}
#endif /* defined(MR_USE_CLOCK_HIRES) */

mr_uptime_t mr_clock_uptime(void) {
    mr_tick_t coarse, thin;
    mr_uint64_t tick;
    int mask;

    /* Disable interrupt */
    mask = mr_port_irq_disable();

    /* Read a consistent clock snapshot */
    coarse = __clock_uptime;
    thin = __clock_tick;

    /* Enable interrupt */
    mr_port_irq_enable(mask);

    /* Combine 32-bit coarse and thin ticks */
    tick = ((mr_uint64_t)coarse * ((mr_uint64_t)MR_TICK_MAX + 1ULL)) + thin;
    /* Convert elapsed ticks to milliseconds */
    return (mr_uptime_t)clock_scale_floor(tick, 1000U, MR_CFG_TICK_PER_SECOND);
}

void mr_clock_busy_wait(mr_uint32_t us) {
    /* Don't give up the CPU */
    MR_TRACE_BEGIN("busy_wait", us);
    mr_port_clock_busy_wait(us);
    MR_TRACE_END("busy_wait", us);
}

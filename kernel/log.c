/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/kernel/mr_log.h>
#if defined(MR_USE_LOG)
#include <mrX/kernel/mr_printf.h>
#include <mrX/kernel/mr_clock.h>

/* Log level mask */
static mr_uint32_t __log_level = 0xffffffffU;

#if defined(MR_USE_CLOCK_HIRES)
static void log_timestamp_print(void) {
    mr_hires_timestamp_t ts;
    mr_hires_tick_t tick;

    /* Get high resolution tick */
    tick = mr_clock_hires_tick();

    /* Get high resolution timestamp */
#if !defined(MR_CFG_LOG_TIME_ZONE)
#define MR_CFG_LOG_TIME_ZONE            (8)
#endif /* !defined(MR_CFG_LOG_TIME_ZONE) */
    ts = mr_hires_tick2timestamp(tick, MR_CFG_LOG_TIME_ZONE);

    /* Print timestamp */
    mr_printf("[%04d-%02d-%02d-%02d:%02d:%02d:%03d:%03d]", ts.year, ts.month,
              ts.day, ts.hour, ts.minute, ts.second, ts.millisecond,
              ts.microsecond);
}
#else
static void log_timestamp_print(void) {
    mr_uint32_t millisecond;
    mr_uptime_t uptime;
    mr_uint64_t second;

    /* Get clock uptime */
    uptime = mr_clock_uptime();

    /* Microsecond ~ second */
    millisecond = (mr_uint32_t)(uptime % 1000);
    second = (mr_uint64_t)uptime / 1000;

    /* Print timestamp */
    mr_printf("[%9llu.%03u]", second, millisecond);
}
#endif /* defined(MR_USE_CLOCK_HIRES) */

void mr_log(mr_uint32_t level, const char *fmt, ...) {
    mr_va_list args;

    /* Check parameter */
    if ((level >= 32U) || (!fmt) || (fmt[0] == '\0')) {
        return;
    }

    /* Check whether log level is enabled */
    if (!((1U << level) & __log_level)) {
        /* Output is disabled */
        return;
    }

    /* Print timestamp */
    log_timestamp_print();

    /* Printf */
    mr_va_start(args, fmt);
    mr_vprintf(fmt, args);
    mr_va_end(args);
}

void mr_log_mask_set(mr_uint32_t mask) {
    /* Set log level mask */
    __log_level = mask;
}

mr_uint32_t mr_log_mask(void) {
    /* Get log level mask */
    return __log_level;
}
#endif /* defined(MR_USE_LOG) */

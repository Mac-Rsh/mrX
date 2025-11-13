/**
 * @copyright (c) 2024-2025, MacRsh
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
static mr_uint32_t __level = 0xffffffffU;

#if defined(MR_USE_CLOCK_HIRES)
static void log_timestamp_print(void) {
    mr_hires_timestamp_t timestamp;

    /* Get high resolution timestamp */
#if !defined(MR_CFG_LOG_TIME_ZONE)
#define MR_CFG_LOG_TIME_ZONE            (8)
#endif /* !defined(MR_CFG_LOG_TIME_ZONE) */
    timestamp = mr_clock_hires_tick2timestamp(mr_clock_hires_tick(),
                                              MR_CFG_LOG_TIME_ZONE);

    /* Print timestamp */
    mr_printf("[%04d-%02d-%02d-%02d:%02d:%02d:%03d:%03d]", timestamp.year,
              timestamp.month, timestamp.day, timestamp.hour, timestamp.minute,
              timestamp.second, timestamp.millisecond, timestamp.microsecond);
}
#else
static void log_timestamp_print(void) {
    mr_tick_t tick;

    /* Get relative timestamp */
    tick = mr_clock_tick();

    /* Print timestamp */
    mr_printf("[%10u]", tick);
}
#endif /* defined(MR_USE_CLOCK_HIRES) */

void mr_log(mr_uint32_t level, const char *fmt, ...) {
    mr_va_list args;

    /* Check parameter */
    if ((level >= 32) || (!fmt) || (fmt[0] == '\0')) {
        return;
    }

    /* Check if log level is enabled */
    if (!((1U << level) & __level)) {
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
    __level = mask;
}

mr_uint32_t mr_log_mask(void) {
    /* Get log level mask */
    return __level;
}
#endif /* defined(MR_USE_LOG) */

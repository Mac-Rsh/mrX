/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_CLOCK_H__
#define __MR_CLOCK_H__

#include <mrX/libc/mr_errno.h>
#include <mrX/libc/mr_types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Clock
 * @{
 */

#if !defined(MR_CFG_TICK_PER_SECOND)
/* Clock tick per-second definition */
#define MR_CFG_TICK_PER_SECOND          (1000U)
#endif /* !defined(MR_CFG_TICK_PER_SECOND) */

/* Clock tick type */
typedef mr_uint32_t                     mr_tick_t;
#define MR_TICK_MAX                     MR_UINT32_MAX

#if defined(MR_USE_CLOCK_HIRES)
#if !defined(MR_CFG_HIRES_TICK_PER_SECOND)
/* Clock high resolution tick per-second definition */
#define MR_CFG_HIRES_TICK_PER_SECOND    (1000000ULL)
#endif /* !defined(MR_CFG_HIRES_TICK_PER_SECOND) */
#if (MR_CFG_HIRES_TICK_PER_SECOND > 1000000ULL)
#error "MR_CFG_HIRES_TICK_PER_SECOND must <= 1000000"
#endif /* (MR_CFG_HIRES_TICK_PER_SECOND > 1000000) */

/* Clock high resolution type */
typedef mr_uint64_t                     mr_hires_tick_t;
#define MR_HIRES_TICK_MAX               MR_UINT64_MAX

/* Clock high resolution timestamp type */
typedef struct mr_hires_timestamp {
    mr_uint16_t year;
    mr_uint8_t month;
    mr_uint8_t day;
    mr_uint8_t hour;
    mr_uint8_t minute;
    mr_uint8_t second;
    mr_uint16_t millisecond;
    mr_uint16_t microsecond;
} mr_hires_timestamp_t;
#endif /* defined(MR_USE_CLOCK_HIRES) */

/* Clock uptime type */
typedef mr_uint64_t                     mr_uptime_t;
#define MR_UPTIME_MAX                   MR_UINT64_MAX

/* Clock hook type */
typedef mr_tick_t(mr_clock_hook_t)(void);

/**
 * @brief This macro converts milliseconds to clock ticks.
 *
 * @param _ms The milliseconds.
 * @return The clock ticks.
 */
#define MR_TICK_MSEC(_ms)                                                      \
    ((mr_tick_t)(((mr_uint64_t)(_ms) * MR_CFG_TICK_PER_SECOND) / 1000ULL))

/**
 * @brief This macro converts microseconds to clock ticks.
 *
 * @param _us The microseconds.
 * @return The clock ticks.
 */
#define MR_TICK_USEC(_us)                                                      \
    ((mr_tick_t)(((mr_uint64_t)(_us) * MR_CFG_TICK_PER_SECOND) / 1000000ULL))

/**
 * @brief This macro converts clock ticks to milliseconds.
 *
 * @param _tick The clock ticks.
 * @return The milliseconds.
 */
#define MR_MSEC_TICK(_tick)                                                    \
    ((mr_uint64_t)(((mr_uint64_t)(_tick) * 1000ULL) / MR_CFG_TICK_PER_SECOND))

/**
 * @brief This macro converts clock ticks to microseconds.
 *
 * @param _tick The clock ticks.
 * @return The microseconds.
 */
#define MR_USEC_TICK(_tick)                                                    \
    ((mr_uint64_t)(((mr_uint64_t)(_tick) * 1000000ULL)                         \
                   / MR_CFG_TICK_PER_SECOND))

/**
 * @brief This function increases the clock tick.
 *
 * @param tick The increased tick.
 */
void mr_clock_increase(mr_tick_t tick);

/**
 * @brief This function gets the clock tick.
 *
 * @return The current tick.
 */
mr_tick_t mr_clock_tick(void);

/**
 * @brief This function gets the clock tick less.
 *
 * @return The current tick less.
 */
mr_tick_t mr_clock_tickless(void);

/**
 * @brief This function adds a hook to the clock.
 *
 * @param entry The hook entry.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_clock_hook_add(mr_clock_hook_t *entry);

#if defined(MR_USE_CLOCK_HIRES)
/**
 * @brief This function sets the clock high resolution tick.
 *
 * @param tick The high resolution tick.
 */
void mr_clock_hires_tick_set(mr_hires_tick_t tick);

/**
 * @brief This function gets the clock high resolution tick.
 *
 * @return The current high resolution tick.
 */
mr_hires_tick_t mr_clock_hires_tick(void);

/**
 * @brief This function converts the high resolution tick to timestamp.
 *
 * @param tick The high resolution tick.
 * @param time_zone The time zone(UTC + n).
 * @return The timestamp.
 */
mr_hires_timestamp_t mr_hires_tick2timestamp(mr_hires_tick_t tick,
                                             int time_zone);
#endif /* defined(MR_USE_CLOCK_HIRES) */

/**
 * @brief This function gets the clock uptime.
 *
 * @return The current uptime.
 */
mr_uptime_t mr_clock_uptime(void);

/**
 * @brief This function busy waits for a specified number of microseconds.
 *
 * @param us The number of microseconds to wait.
 *
 * @note It won't give up the CPU.
 */
void mr_clock_busy_wait(mr_uint32_t us);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_CLOCK_H__ */

/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-01-08        MacRsh        First version
 */

#include <mrX/platform/mr_platform.h>
#include <mrX/port/mr_port_clock.h>
#include <mrX/libc/mr_compiler.h>
#include <mrX/kernel/mr_clock.h>
#include <time.h>

/* High resolution timer */
static LARGE_INTEGER __hires_freq, __hires_start;
static mr_int64_t __hires_offset = 0;

static DWORD WINAPI tick_thread_entry(LPVOID param) {
    LARGE_INTEGER start, end;
    mr_uint64_t target;

    MR_IGNORE(param);

    /* Set thread priority */
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    while (1) {
        target = (mr_uint64_t)__hires_freq.QuadPart / MR_CFG_TICK_PER_SECOND;
        QueryPerformanceCounter(&start);

        /* Spin wait */
        do {
            QueryPerformanceCounter(&end);
        } while ((mr_uint64_t)(end.QuadPart - start.QuadPart) < target);

        /* Increase clock tick */
        mr_clock_increase(1);
    }
    return 0;
}

void mr_win_platform_clock_init(mr_uint32_t per_second) {
    ULARGE_INTEGER unix_uli;
    FILETIME unix_ft;
    HANDLE thread;

    MR_IGNORE(per_second);

    /* Init high resolution timer */
    QueryPerformanceFrequency(&__hires_freq);
    QueryPerformanceCounter(&__hires_start);

    /* Init high resolution timer to unix time */
    GetSystemTimeAsFileTime(&unix_ft);
    unix_uli.HighPart = unix_ft.dwHighDateTime;
    unix_uli.LowPart = unix_ft.dwLowDateTime;
    unix_uli.QuadPart -= 116444736000000000ULL;
    unix_uli.QuadPart /= 10ULL;
    mr_port_clock_hires_tick_set(unix_uli.QuadPart);

    /* Start tick thread */
    thread = CreateThread(NULL, 0, tick_thread_entry, NULL, 0, NULL);
    MR_IGNORE(thread);
}

#if defined(MR_USE_CLOCK_HIRES)
mr_uint64_t mr_port_clock_hires_tick(void) {
    LARGE_INTEGER count;
    mr_uint64_t thin;

    /* Get current high resolution counter */
    QueryPerformanceCounter(&count);

    /* Calculate hires tick */
    thin = (mr_uint64_t)(count.QuadPart - __hires_start.QuadPart);
    thin *= MR_CFG_HIRES_TICK_PER_SECOND;
    thin /= (mr_uint64_t)__hires_freq.QuadPart;
    if ((mr_int64_t)thin <= (-__hires_offset)) {
        return 0;
    }
    return thin + (mr_uint64_t)__hires_offset;
}

void mr_port_clock_hires_tick_set(mr_uint64_t tick) {
    /* Set hires offset */
    __hires_offset = (mr_int64_t)tick;
}
#endif /* defined(MR_USE_CLOCK_HIRES) */

void mr_port_clock_busy_wait(mr_uint32_t us) {
    LARGE_INTEGER start, end, freq;
    mr_uint64_t wait_ticks;

    /* Get frequency */
    QueryPerformanceFrequency(&freq);

    /* Calculate wait ticks */
    wait_ticks = (mr_uint64_t)us * (mr_uint64_t)freq.QuadPart / 1000000ULL;

    /* Get start time */
    QueryPerformanceCounter(&start);

    /* Busy wait */
    do {
        QueryPerformanceCounter(&end);
    } while ((mr_uint64_t)(end.QuadPart - start.QuadPart) < wait_ticks);
}

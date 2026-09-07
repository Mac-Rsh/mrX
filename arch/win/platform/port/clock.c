/**
 * @copyright (c) 2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2026-01-08        MacRsh        First version
 * @date 2026-09-07        MacRsh        Waitable timer tick source
 */

#include <mrX/platform/mr_platform.h>
#include <mrX/port/mr_port_clock.h>
#include <mrX/libc/mr_compiler.h>
#include <mrX/kernel/mr_clock.h>

/* FILETIME 100-ns units per second */
#define CLOCK_100NS_PER_SECOND          (10000000ULL)
/* FILETIME epoch to Unix epoch */
#define CLOCK_UNIX_EPOCH_100NS          (116444736000000000ULL)

/* High resolution timer */
static LARGE_INTEGER __hires_freq, __hires_start;
static mr_int64_t __hires_offset = 0;
/* Tick thread waitable timer */
static HANDLE __tick_timer = NULL;
/* Tick rate used by the tick thread */
static mr_uint32_t __tick_per_second = MR_CFG_TICK_PER_SECOND;

static HANDLE tick_timer_create(void) {
    HANDLE timer;

#if defined(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)
    /* Create high resolution waitable timer */
    timer = CreateWaitableTimerEx(NULL, NULL,
                                  CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                  TIMER_ALL_ACCESS);
    if (timer) {
        return timer;
    }
#endif /* defined(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION) */

    /* Create waitable timer */
    return CreateWaitableTimer(NULL, FALSE, NULL);
}

static void tick_timer_wait(mr_uint64_t wait_100ns) {
    LARGE_INTEGER due;
    DWORD ms;

    due.QuadPart = -(LONGLONG)wait_100ns;
    if (__tick_timer) {
        if (SetWaitableTimer(__tick_timer, &due, 0, NULL, NULL, FALSE)) {
            WaitForSingleObject(__tick_timer, INFINITE);
            return;
        }
    }

    /* Fall back to Sleep */
    ms = (DWORD)(wait_100ns / 10000ULL);
    if (ms == 0) {
        ms = 1;
    }
    Sleep(ms);
}

static DWORD WINAPI tick_thread_entry(LPVOID param) {
    LARGE_INTEGER now;
    mr_uint64_t origin, elapsed, issued, target, next, remain;
    mr_uint64_t freq, tps, wait_100ns;
    mr_tick_t ticks;

    MR_IGNORE(param);

    /* Set thread priority */
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    freq = (mr_uint64_t)__hires_freq.QuadPart;
    tps = (mr_uint64_t)__tick_per_second;
    if ((freq == 0ULL) || (tps == 0ULL)) {
        return 0;
    }

    QueryPerformanceCounter(&now);
    origin = (mr_uint64_t)now.QuadPart;
    issued = 0ULL;

    while (1) {
        QueryPerformanceCounter(&now);
        elapsed = (mr_uint64_t)now.QuadPart - origin;
        target = (elapsed / freq) * tps + ((elapsed % freq) * tps) / freq;
        if (target > issued) {
            ticks = (mr_tick_t)(target - issued);
            issued = target;
            mr_clock_increase(ticks);
            continue;
        }

        next = ((issued + 1ULL) / tps) * freq
               + (((issued + 1ULL) % tps) * freq) / tps;
        if (next <= elapsed) {
            continue;
        }

        remain = next - elapsed;
        wait_100ns = remain * CLOCK_100NS_PER_SECOND / freq;
        if (wait_100ns == 0ULL) {
            continue;
        }
        tick_timer_wait(wait_100ns);
    }
    return 0;
}

void mr_win_platform_clock_init(mr_uint32_t per_second) {
    ULARGE_INTEGER unix_uli;
    FILETIME unix_ft;
    HANDLE thread;

    if (per_second == 0U) {
        per_second = MR_CFG_TICK_PER_SECOND;
    }
    __tick_per_second = per_second;

    /* Init high resolution timer */
    QueryPerformanceFrequency(&__hires_freq);
    QueryPerformanceCounter(&__hires_start);

    /* Init high resolution timer to unix time */
    GetSystemTimeAsFileTime(&unix_ft);
    unix_uli.HighPart = unix_ft.dwHighDateTime;
    unix_uli.LowPart = unix_ft.dwLowDateTime;
    unix_uli.QuadPart -= CLOCK_UNIX_EPOCH_100NS;
    unix_uli.QuadPart /= 10ULL;
    mr_port_clock_hires_tick_set(unix_uli.QuadPart);

    /* Create tick waitable timer */
    __tick_timer = tick_timer_create();

    /* Start tick thread */
    thread = CreateThread(NULL, 0, tick_thread_entry, NULL, 0, NULL);
    if (thread) {
        CloseHandle(thread);
    }
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
    LARGE_INTEGER start, end;
    mr_uint64_t wait_ticks;
    mr_uint64_t freq;

    /* Get frequency */
    freq = (mr_uint64_t)__hires_freq.QuadPart;
    if (freq == 0ULL) {
        QueryPerformanceFrequency(&__hires_freq);
        freq = (mr_uint64_t)__hires_freq.QuadPart;
        if (freq == 0ULL) {
            return;
        }
    }

    /* Calculate wait ticks */
    wait_ticks = (mr_uint64_t)us * freq / 1000000ULL;

    /* Get start time */
    QueryPerformanceCounter(&start);

    /* Busy wait */
    do {
        QueryPerformanceCounter(&end);
    } while ((mr_uint64_t)(end.QuadPart - start.QuadPart) < wait_ticks);
}

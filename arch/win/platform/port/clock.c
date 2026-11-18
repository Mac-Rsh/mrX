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
#include <mrX/kernel/mr_clock.h>

/* Clock time unit definition */
#define CLOCK_100NS_PER_SECOND          (10000000ULL)
#define CLOCK_100NS_PER_MILLISECOND     (10000ULL)
#define CLOCK_US_PER_SECOND             (1000000ULL)

#if defined(MR_USE_CLOCK_HIRES)
/* FILETIME Unix epoch definition */
#define CLOCK_UNIX_EPOCH_100NS          (116444736000000000ULL)

/* High resolution clock baseline */
static SRWLOCK __hires_lock = SRWLOCK_INIT;
static LARGE_INTEGER __hires_start;
static mr_uint64_t __hires_base;
#endif /* defined(MR_USE_CLOCK_HIRES) */

/* Performance counter frequency */
static mr_uint64_t __clock_freq;

static HANDLE tick_timer_create(void) {
#if defined(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)
    HANDLE timer;

    /* Create high resolution waitable timer */
    timer = CreateWaitableTimerEx(MR_NULL, MR_NULL,
                                  CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                  TIMER_MODIFY_STATE | SYNCHRONIZE);
    if (timer) {
        return timer;
    }
#endif /* defined(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION) */

    /* Create waitable timer */
    return CreateWaitableTimer(MR_NULL, FALSE, MR_NULL);
}

static void tick_timer_wait(HANDLE timer, mr_uint64_t wait_100ns) {
    LARGE_INTEGER due;

    /* A negative due time is a relative wait in 100-ns units */
    due.QuadPart = -(LONGLONG)wait_100ns;
    if (timer && SetWaitableTimer(timer, &due, 0, MR_NULL, MR_NULL, FALSE)
        && (WaitForSingleObject(timer, INFINITE) == WAIT_OBJECT_0)) {
        return;
    }

    /* Round the fallback wait up to a whole millisecond */
    Sleep((DWORD)((wait_100ns + CLOCK_100NS_PER_MILLISECOND - 1ULL)
                  / CLOCK_100NS_PER_MILLISECOND));
}

static DWORD WINAPI tick_thread_entry(LPVOID param) {
    const mr_uint64_t tps = MR_CFG_TICK_PER_SECOND;
    mr_uint64_t elapsed, issued, target, next;
    mr_uint64_t freq, remaining, wait_100ns;
    LARGE_INTEGER origin, now;
    mr_tick_t ticks;

    /* Set thread priority */
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    /* Track deadlines from a fixed origin to avoid accumulating wakeup delay */
    freq = __clock_freq;
    QueryPerformanceCounter(&origin);
    issued = 0ULL;

    while (1) {
        QueryPerformanceCounter(&now);
        elapsed = (mr_uint64_t)(now.QuadPart - origin.QuadPart);
        target = (elapsed / freq) * tps + ((elapsed % freq) * tps) / freq;
        if (target > issued) {
            /* Keep each advance within the timer's half-range comparison */
            if ((target - issued) >= (MR_TICK_MAX / 2U)) {
                target = issued + (MR_TICK_MAX / 2U - 1U);
            }
            ticks = (mr_tick_t)(target - issued);
            issued = target;
            mr_clock_increase(ticks);
            continue;
        }

        /* Round the deadline and relative wait up to avoid early polling */
        next = issued + 1ULL;
        next = (next / tps) * freq
               + ((next % tps) * freq + tps - 1ULL) / tps;
        remaining = next - elapsed;
        wait_100ns = (remaining * CLOCK_100NS_PER_SECOND + freq - 1ULL) / freq;
        tick_timer_wait(param, wait_100ns);
    }
    return 0;
}

#if defined(MR_USE_CLOCK_HIRES)
static void clock_hires_init(mr_uint64_t freq) {
    mr_uint64_t elapsed, tick;
    ULARGE_INTEGER time;
    FILETIME file_time;

    /* Convert Unix time to the configured hires tick unit */
    GetSystemTimeAsFileTime(&file_time);
    time.HighPart = file_time.dwHighDateTime;
    time.LowPart = file_time.dwLowDateTime;
    elapsed = 0;
    if (time.QuadPart > CLOCK_UNIX_EPOCH_100NS) {
        elapsed = time.QuadPart - CLOCK_UNIX_EPOCH_100NS;
    }
    tick = (elapsed / CLOCK_100NS_PER_SECOND) * MR_CFG_HIRES_TICK_PER_SECOND
           + ((elapsed % CLOCK_100NS_PER_SECOND) * MR_CFG_HIRES_TICK_PER_SECOND)
                 / CLOCK_100NS_PER_SECOND;

    /* Publish the counter frequency and the initial baseline together */
    AcquireSRWLockExclusive(&__hires_lock);
    __clock_freq = freq;
    QueryPerformanceCounter(&__hires_start);
    __hires_base = tick;
    ReleaseSRWLockExclusive(&__hires_lock);
}
#endif /* defined(MR_USE_CLOCK_HIRES) */

void mr_win_platform_clock_init(void) {
    HANDLE timer, thread;
    LARGE_INTEGER freq;

    /* Init the performance counter frequency before starting the tick thread */
    if (!QueryPerformanceFrequency(&freq) || (freq.QuadPart <= 0)) {
        return;
    }

#if defined(MR_USE_CLOCK_HIRES)
    /* Init high resolution clock */
    clock_hires_init((mr_uint64_t)freq.QuadPart);
#else
    __clock_freq = (mr_uint64_t)freq.QuadPart;
#endif /* defined(MR_USE_CLOCK_HIRES) */

    /* Pass the waitable timer to the tick thread once it starts */
    timer = tick_timer_create();
    thread = CreateThread(MR_NULL, 0, tick_thread_entry, timer, 0, MR_NULL);
    if (thread) {
        CloseHandle(thread);
    } else if (timer) {
        CloseHandle(timer);
    }
}

#if defined(MR_USE_CLOCK_HIRES)
mr_uint64_t mr_port_clock_hires_tick(void) {
    mr_uint64_t freq, elapsed, tick;
    LARGE_INTEGER count, start;

    /* Lock while sampling the counter against a consistent baseline */
    AcquireSRWLockShared(&__hires_lock);
    QueryPerformanceCounter(&count);
    start = __hires_start;
    tick = __hires_base;
    freq = __clock_freq;
    ReleaseSRWLockShared(&__hires_lock);

    /* Ignore a counter sample that precedes a baseline set on another CPU */
    if ((freq == 0ULL) || (count.QuadPart <= start.QuadPart)) {
        return tick;
    }

    /* Split before multiplication to avoid overflow on long runs */
    elapsed = (mr_uint64_t)(count.QuadPart - start.QuadPart);
    return tick + (elapsed / freq) * MR_CFG_HIRES_TICK_PER_SECOND
           + ((elapsed % freq) * MR_CFG_HIRES_TICK_PER_SECOND) / freq;
}

void mr_port_clock_hires_tick_set(mr_uint64_t tick) {
    /* Lock while replacing the counter reference and its logical tick */
    AcquireSRWLockExclusive(&__hires_lock);
    QueryPerformanceCounter(&__hires_start);
    __hires_base = tick;
    ReleaseSRWLockExclusive(&__hires_lock);
}
#endif /* defined(MR_USE_CLOCK_HIRES) */

void mr_port_clock_busy_wait(mr_uint32_t us) {
    LARGE_INTEGER frequency, start, end;
    mr_uint64_t freq, wait_ticks;

    /* Use a local frequency so this also works before platform initialization */
    if ((us == 0U) || !QueryPerformanceFrequency(&frequency)
        || (frequency.QuadPart <= 0)) {
        return;
    }

    /* Round up so a fractional counter tick does not shorten the delay */
    freq = (mr_uint64_t)frequency.QuadPart;
    wait_ticks = (us / CLOCK_US_PER_SECOND) * freq
                 + ((us % CLOCK_US_PER_SECOND) * freq + CLOCK_US_PER_SECOND
                    - 1ULL) / CLOCK_US_PER_SECOND;

    /* Get start time */
    QueryPerformanceCounter(&start);

    /* Busy wait */
    do {
        QueryPerformanceCounter(&end);
    } while ((mr_uint64_t)(end.QuadPart - start.QuadPart) < wait_ticks);
}

[**中文**](kernel_clock.md) | [**English**](kernel_clock_en.md)

---

# Clock Management

The clock management module maintains the system tick, tickless query hooks, system uptime, and an optional
high-resolution clock. The normal tick uses `mr_tick_t` and is advanced by `mr_clock_increase()`; the high-resolution
tick is provided by the platform layer interface; busy-wait delays are also delegated to the platform layer.

This module does not create hardware timers. Hardware or platform timer interrupts should call `mr_clock_increase()` at
the appropriate point to advance the kernel clock and trigger clock hooks.

## Core Data Structures and Macros

### Tick Configuration

```c
#if !defined(MR_CFG_TICK_PER_SECOND)
#define MR_CFG_TICK_PER_SECOND (1000U)
#endif

typedef mr_uint32_t mr_tick_t;
#define MR_TICK_MAX MR_UINT32_MAX
```

`MR_CFG_TICK_PER_SECOND` defines the number of ticks per second for the normal clock, defaulting to 1000. `mr_tick_t` is
a 32-bit unsigned type; overflow is handled internally by the module using coarse-grained counters.

### Tick Conversion Macros

```c
#define MR_TICK_MSEC(_ms) \
    ((mr_tick_t)(((mr_uint64_t)(_ms) * MR_CFG_TICK_PER_SECOND) / 1000ULL))

#define MR_TICK_USEC(_us) \
    ((mr_tick_t)(((mr_uint64_t)(_us) * MR_CFG_TICK_PER_SECOND) / 1000000ULL))
```

### High-Resolution Clock

```c
#if defined(MR_USE_CLOCK_HIRES)
typedef mr_uint64_t mr_hires_tick_t;
#define MR_HIRES_TICK_MAX MR_UINT64_MAX

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
#endif
```

### Clock Hook

```c
typedef mr_tick_t(mr_clock_hook_t)(void);
```

## Main Interface

### Advance and Read Normal Tick

```c
void mr_clock_increase(mr_tick_t tick);
mr_tick_t mr_clock_tick(void);
```

### Tickless Query and Hook Registration

```c
mr_tick_t mr_clock_tickless(void);
mr_err_t mr_clock_hook_add(mr_clock_hook_t *entry);
```

### High-Resolution Tick and Timestamp

```c
void mr_clock_hires_tick_set(mr_hires_tick_t tick);
mr_hires_tick_t mr_clock_hires_tick(void);
mr_hires_timestamp_t mr_hires_tick2timestamp(mr_hires_tick_t tick, int time_zone);
```

### Uptime and Busy Wait

```c
mr_uptime_t mr_clock_uptime(void);
void mr_clock_busy_wait(mr_uint32_t us);
```

## Usage Example

```c
static mr_tick_t timer_tickless(void) {
    return MR_TICK_MSEC(10);
}

void systick_isr(void) {
    mr_clock_increase(1);
}

void example(void) {
    mr_clock_hook_add(timer_tickless);

    if (mr_clock_tickless() <= MR_TICK_MSEC(10)) {
        /* can configure next platform timer based on tickless result */
    }

    mr_clock_busy_wait(5);
}
```

## Notes

- `mr_clock_increase()` executes all clock hooks; hooks should be kept short and must not perform blocking operations.
- `mr_clock_tick()` returns a 32-bit tick; callers must handle wraparound themselves. Use `mr_clock_uptime()` when a
  long-running duration is needed.
- `mr_clock_uptime()` currently returns time in milliseconds; precision is affected by `MR_CFG_TICK_PER_SECOND` and the
  rounding factor.
- `mr_clock_busy_wait()` is a busy wait and does not yield the CPU.
- The actual monotonicity and precision of the high-resolution tick depend on the platform layer's `mr_port_clock_*`
  implementation.

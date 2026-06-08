[**中文**](kernel_log.md) | [**English**](kernel_log_en.md)

---

# Kernel Log

The kernel log module provides log output with timestamps, levels, and tags. The module outputs text based on
`mr_printf`, controls the runtime output range via a global level mask, and controls whether each log macro expands
through compile-time configuration and `MR_LOG_TAG`.

This document corresponds to `include/mrX/kernel/mr_log.h`, `kernel/log.c`, and `test/kernel/log.c`.

## Module Purpose

The `mr_log` module is used to output structured debug information in the kernel and components. When calling a log
macro, the macro generates a format string containing the level and module tag; `mr_log()` prints a timestamp before
actual output and decides whether to output the log based on the level mask.

The module depends on `MR_USE_LOG`. In Kconfig, `MR_USE_LOG` selects `MR_USE_PRINTF`, so log output always builds on the
printf module.

## Core Macros and Configuration

### Log Levels

```c
#define MR_LOG_LEVEL_ERR                (0)
#define MR_LOG_LEVEL_WRN                (1)
#define MR_LOG_LEVEL_INF                (2)
#define MR_LOG_LEVEL_DBG                (3)
#define MR_LOG_LEVEL_TRC                (4)
```

Log levels are represented as integers from 0 to 4. Higher values indicate more detailed information. `mr_log()` itself
accepts level values from 0 to 31, but the standard macros provided by the header only define the five levels above.

### Level Mask

```c
#define MR_LOG_MASK(_level)             (1U << (_level))
```

The level mask is used to filter logs at runtime. `mr_log()` only outputs when `(1U << level) & mask` is non-zero. The
default mask in the source is `0xffffffffU`, meaning all levels 0 through 31 are enabled by default.

### Default Compile-Time Level

```c
#if !defined(MR_CFG_LOG_LEVEL)
#define MR_CFG_LOG_LEVEL                MR_LOG_LEVEL_TRC
#endif
```

`MR_CFG_LOG_LEVEL` controls the compile-time expansion upper limit of log macros. In Kconfig, this configuration depends
on `MR_USE_LOG`, defaults to 4, and ranges from 0 to 4. If `MR_LOG_TAG` is not defined where a log macro is included,
the corresponding log macro expands to a no-op.

### Log Format Macro

```c
#define __MR_LOG_FORMAT(_level, _fmt)                                          \
    (" [" _level "/" MR_LOG_TAG "] " _fmt "\r\n")
```

Log macros concatenate the level, `MR_LOG_TAG`, and the user format string into the final format string. Timestamps are
not generated in this macro but are output first inside `mr_log()`.

### Log Output Macros

```c
#define MR_LOG_ERR(_fmt, ...) mr_log(MR_LOG_LEVEL_ERR, ...)
#define MR_LOG_WRN(_fmt, ...) mr_log(MR_LOG_LEVEL_WRN, ...)
#define MR_LOG_INF(_fmt, ...) mr_log(MR_LOG_LEVEL_INF, ...)
#define MR_LOG_DBG(_fmt, ...) mr_log(MR_LOG_LEVEL_DBG, ...)
#define MR_LOG_TRC(_fmt, ...) mr_log(MR_LOG_LEVEL_TRC, ...)
```

Each macro expands to a `mr_log()` call only when `MR_LOG_TAG` is defined and `MR_CFG_LOG_LEVEL` is greater than or
equal to the corresponding level; otherwise, it expands to `((void)0)`.

## Module Interfaces

### Output Log

```c
void mr_log(mr_uint32_t level, const char *fmt, ...);
```

`mr_log()` filters logs based on the level mask. When the level is valid and enabled, it first outputs a timestamp, then
passes `fmt` and the variadic arguments to `mr_vprintf()`.

No return value. The following cases return directly without output:

| Condition                         | Behavior                                           |
|-----------------------------------|----------------------------------------------------|
| `level >= 32U`                    | Level exceeds 32-bit mask range, ignored directly. |
| `fmt == MR_NULL`                  | Invalid format string, ignored directly.           |
| `fmt[0] == '\0'`                  | Empty format string, ignored directly.             |
| Current level not enabled by mask | Output filtered.                                   |

The timestamp format depends on clock configuration:

| Configuration                    | Timestamp Source and Format                                                                          |
|----------------------------------|------------------------------------------------------------------------------------------------------|
| `MR_USE_CLOCK_HIRES`             | Uses `mr_clock_hires_tick()` and `mr_hires_tick2timestamp()`, outputs `[YYYY-MM-DD-HH:MM:SS:ms:us]`. |
| `MR_USE_CLOCK_HIRES` not enabled | Uses `mr_clock_uptime()`, outputs `[seconds.millisecond]`.                                           |

### Setting Log Mask

```c
void mr_log_mask_set(mr_uint32_t mask);
```

`mr_log_mask_set()` sets the global log level mask to `mask`. For example, `MR_LOG_MASK(MR_LOG_LEVEL_ERR)` only enables
error logs, and `0U` disables all level output.

### Getting Log Mask

```c
mr_uint32_t mr_log_mask(void);
```

`mr_log_mask()` returns the current global log level mask. Test cases cover default mask, setting and reading mask,
invalid levels, invalid format strings, and mask filtering behavior.

## Usage Examples

```c
#define MR_LOG_TAG "demo"
#include <mrX/kernel/mr_log.h>

void example_log(void) {
    mr_log_mask_set(MR_LOG_MASK(MR_LOG_LEVEL_ERR) |
                    MR_LOG_MASK(MR_LOG_LEVEL_INF));

    MR_LOG_ERR("open failed: %d", -1);
    MR_LOG_INF("device ready: %s", "uart0");
    MR_LOG_DBG("this message depends on mask and compile level");
}
```

Alternatively, `mr_log()` can be called directly:

```c
mr_log(MR_LOG_LEVEL_WRN, " [WRN/manual] value=%u\r\n", value);
```

When calling directly, the caller must organize the level and tag text themselves; the log macros already incorporate
this format.

## Notes

- Log macros depend on `MR_LOG_TAG`; without a defined tag, the macros produce no output.
- `MR_CFG_LOG_LEVEL` is a compile-time filter; macros with levels higher than this value will not generate `mr_log()`
  calls.
- `mr_log_mask_set()` is a runtime filter and cannot restore log macros already filtered out at compile time.
- The global mask `__log_level` is not individually locked in the current implementation; callers must handle concurrent
  reads and writes to the mask themselves.
- `mr_log()` uses `mr_printf()` and `mr_vprintf()` for output; the buffer size, safe mode, and platform output
  limitations of the printf module also apply to the log module.
- When high-resolution clock is enabled, the timestamp uses `MR_CFG_LOG_TIME_ZONE`; when not configured, the source
  defaults to 8.

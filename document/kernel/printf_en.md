[**中文**](kernel_printf.md) | [**English**](kernel_printf_en.md)

---

# Kernel Output

The kernel output module provides lightweight formatted output capability. The module formats output into an internal
buffer via `mr_vsnprintf()`, then calls the platform layer `mr_port_output()` to output the byte stream. This module is
controlled by the `MR_USE_PRINTF` configuration and is commonly used for kernel debugging, log output, and test output.

This document corresponds to `include/mrX/kernel/mr_printf.h`, `kernel/printf.c`, and `test/kernel/printf.c`.

## Module Purpose

The `mr_printf` module wraps two output interfaces: `mr_printf()` handles variadic arguments, and `mr_vprintf()` handles
an already-expanded `mr_va_list`. Formatting capability comes from `mr_vsnprintf()` in the mrX libc, while actual
character output is completed by the port layer `mr_port_output()`, so the module itself is not bound to any specific
UART, console, or host output device.

When `MR_USE_PRINTF_SAFE` is not enabled, `mr_vprintf()` uses a static buffer; when enabled, each call allocates a
temporary buffer via `mr_malloc()` and frees it after output. The buffer size is controlled by `MR_CFG_PRINTF_BUF_SIZE`,
defaulting to 64 bytes when not defined in the source.

## Core Macros and Configuration

### Output Switch

```c
#if defined(MR_USE_PRINTF)
```

Only when `MR_USE_PRINTF` is enabled do the color macros and output interfaces take effect. In
`include/mrX/kernel/Kconfig`, this configuration is enabled by default.

### Buffer Size

```c
#define MR_CFG_PRINTF_BUF_SIZE          (64)
```

`MR_CFG_PRINTF_BUF_SIZE` is the format buffer size. In Kconfig, this configuration depends on `MR_USE_PRINTF`, defaults
to 64, and ranges from 8 to 65535. The source also uses 64 as the default when the configuration is missing.

### Safe Output Mode

```c
#if !defined(MR_USE_PRINTF_SAFE)
    static char buf[MR_CFG_PRINTF_BUF_SIZE];
#else
    char *buf;
#endif
```

When `MR_USE_PRINTF_SAFE` is not enabled, the module uses a static buffer inside the function, which is shared when
multiple execution contexts call it concurrently. When enabled, the module allocates an independent buffer for each
call, avoiding shared buffer contention but introducing heap allocation overhead and potentially returning `-MR_ENOMEM`.

### Color Macros

```c
#define MR_COLOR_RESET                  "\033[0m"
#define MR_COLOR_BLACK                  "\033[30m"
#define MR_COLOR_RED                    "\033[31m"
#define MR_COLOR_GREEN                  "\033[32m"
#define MR_COLOR_YELLOW                 "\033[33m"
#define MR_COLOR_BLUE                   "\033[34m"
#define MR_COLOR(_color, _str)          _color _str MR_COLOR_RESET
```

When `MR_USE_PRINTF_COLOR` is enabled, the color macros expand to ANSI escape sequences; when not enabled, the color
constants are empty, and `MR_COLOR(_color, _str)` directly returns `_str`. When using colored output, the actual
terminal or output device must support ANSI escape sequences.

## Module Interfaces

### Formatted Output with Variadic Argument List

```c
int mr_vprintf(const char *fmt, mr_va_list args);
```

`mr_vprintf()` receives a format string and `mr_va_list`, writes the formatted result to an internal buffer, then calls
`mr_port_output(buf, outsz)` for output.

Return values:

| Return Value | Meaning                                                                                |
|--------------|----------------------------------------------------------------------------------------|
| `>= 0`       | Return value of `mr_port_output()`, indicating the platform layer output result.       |
| `-MR_EINVAL` | `fmt` is null, `fmt` is an empty string, or the formatting function returned an error. |
| `-MR_ENOMEM` | Temporary buffer allocation failed when `MR_USE_PRINTF_SAFE` is enabled.               |

Test cases cover null format string, empty format string, plain string, integer, hexadecimal, character, and longer
format string output.

### Formatted Output

```c
int mr_printf(const char *fmt, ...);
```

`mr_printf()` is the commonly used output interface. It checks `fmt`, constructs the argument list via `mr_va_start()`,
and then calls `mr_vprintf()` to complete output.

Return values are the same as `mr_vprintf()`. When `fmt` is null or empty, it directly returns `-MR_EINVAL`.

## Usage Examples

```c
#include <mrX/kernel/mr_printf.h>

void example_printf(void) {
    int ret;

    ret = mr_printf("boot step: %u\n", 1U);
    if (ret < 0) {
        return;
    }

    mr_printf(MR_COLOR(MR_COLOR_GREEN, "init ok\n"));
}
```

To forward an existing variadic argument list, it can be wrapped as a `mr_vprintf()` call:

```c
static int output_with_prefix(const char *fmt, ...) {
    mr_va_list args;
    int ret;

    mr_va_start(args, fmt);
    ret = mr_vprintf(fmt, args);
    mr_va_end(args);
    return ret;
}
```

## Notes

- `mr_printf()` and `mr_vprintf()` do not accept empty format strings; passing `MR_NULL` or `""` returns `-MR_EINVAL`.
- When `MR_USE_PRINTF_SAFE` is not enabled, the internal static buffer is unsuitable for concurrent output of the same
  formatted data from multiple threads or interrupt contexts.
- When `MR_USE_PRINTF_SAFE` is enabled, the call path depends on heap allocation and is unsuitable for contexts that
  cannot allocate memory.
- Output length is limited by `MR_CFG_PRINTF_BUF_SIZE`; behavior when exceeding the buffer is determined by
  `mr_vsnprintf()`.
- This module is only responsible for formatting and calling the port layer for output; output device initialization,
  blocking behavior, and concurrency protection are determined by the platform implementation of `mr_port_output()`.

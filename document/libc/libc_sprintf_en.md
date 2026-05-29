[**中文**](libc_sprintf.md) | [**English**](libc_sprintf_en.md)

---

# libc Formatted Output

`mr_sprintf.h` and `libc/sprintf.c` provide formatted output interfaces for in-memory buffers. When `MR_USE_SPRINTF_STD`
is enabled, the interfaces map to the standard library; otherwise, mrX provides its own implementation handling
integers, strings, characters, pointers, and other common formats.

## Core Interface

```c
int mr_vsnprintf(char *buf, mr_size_t size, const char *fmt, mr_va_list args);
int mr_vsprintf(char *buf, const char *fmt, mr_va_list args);
int mr_snprintf(char *buf, mr_size_t size, const char *fmt, ...);
int mr_sprintf(char *buf, const char *fmt, ...);
```

`mr_vsnprintf()` is the core implementation. `mr_snprintf()` and `mr_sprintf()` create an `mr_va_list` and delegate.
`mr_vsprintf()` calls `mr_vsnprintf()` by passing a nearly unlimited size.

The return value is the number of characters that would have been written, not including the terminating `\0`. When
`size > 0`, the output buffer is null-terminated; if the content is truncated, the return value still represents the
full formatted length.

## Supported Format Capabilities

### Flags, Width, and Precision

The self-implementation supports the following format modifiers:

| Category  | Supported Items                                                                        |
|-----------|----------------------------------------------------------------------------------------|
| Flags     | `-`, `+`, space, `0`, `#`                                                              |
| Width     | Decimal digit width, `*` dynamic width; negative dynamic width enables left alignment. |
| Precision | `.digit`, `.*` dynamic precision; negative dynamic precision is treated as 0.          |
| Length    | `l`, `ll`, `z`, `h`, `hh`                                                              |

### Conversion Specifiers

| Specifier  | Meaning                                                                                           |
|------------|---------------------------------------------------------------------------------------------------|
| `%c`       | Character.                                                                                        |
| `%s`       | String, null pointer outputs `"(null)"`.                                                          |
| `%p`       | Pointer; default width is 2 times the pointer byte count plus the `0x` prefix, with zero padding. |
| `%d`, `%i` | Signed decimal.                                                                                   |
| `%u`       | Unsigned decimal.                                                                                 |
| `%x`, `%X` | Hexadecimal, lowercase or uppercase.                                                              |
| `%o`       | Octal.                                                                                            |
| `%b`       | Binary, mrX extension.                                                                            |

The `#` flag adds `0b`, `0`, `0x`, or `0X` prefixes for binary, octal, and hexadecimal output.

## Standard Library Mapping

When `MR_USE_SPRINTF_STD` is enabled:

```c
mr_vsnprintf -> vsnprintf
mr_vsprintf  -> vsprintf
mr_snprintf  -> snprintf
mr_sprintf   -> sprintf
```

## Usage Example

```c
#include <mrX/libc/mr_sprintf.h>

void sample_sprintf_usage(void)
{
    char buf[32];

    mr_snprintf(buf, sizeof(buf), "id=%04d hex=%#x", 7, 0x2a);
}
```

Variadic argument wrapper example:

```c
static int sample_vformat(char *buf, mr_size_t size, const char *fmt, ...)
{
    mr_va_list args;
    int ret;

    mr_va_start(args, fmt);
    ret = mr_vsnprintf(buf, size, fmt, args);
    mr_va_end(args);
    return ret;
}
```

## Notes

- The self-implementation is only compiled when the `MR_USE_SPRINTF` compile switch is enabled.
- The self-implementation does not support floating-point formatting. `%f`, `%e`, `%g`, and their uppercase variants are
  treated as unsupported formats, outputting `%` followed by the original specifier.
- In the self-implementation, returns 0 when `fmt == MR_NULL`.
- The self-implementation allows `buf == MR_NULL` and `size == 0`, which can be used to calculate the formatted length.
- The self-implementation's behavior for `%%` differs from the standard library: it outputs two percent characters via
  the unsupported format path.
- `mr_vsprintf()` does not know the real buffer size; the caller must ensure sufficient destination space.
- Tests cover integers, strings, width, precision, length modifiers, pointers, binary, prefixes, truncation, empty
  strings, empty format, and null string argument scenarios.

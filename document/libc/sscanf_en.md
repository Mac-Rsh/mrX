[**中文**](libc_sscanf.md) | [**English**](libc_sscanf_en.md)

---

# libc Formatted Input

`mr_sscanf.h` and `libc/sscanf.c` provide interfaces for reading formatted data from strings. When `MR_USE_SSCANF_STD`
is enabled, they map directly to the standard library `sscanf()`; otherwise, the current mrX implementation performs
null pointer checks on arguments before calling the standard library `vsscanf()`.

## Core Interface

```c
int mr_sscanf(const char *buf, const char *fmt, ...);
```

`mr_sscanf()` parses data from `buf` according to the description in `fmt` and writes the results to the objects pointed
to by the variadic arguments. The return value is the number of fields successfully assigned; on match failure, it
returns the return value of the standard library `vsscanf()`.

## Implementation

Under non-`MR_USE_SSCANF_STD` configuration, the source implementation behaves as follows:

1. If `buf == MR_NULL` or `fmt == MR_NULL`, returns `-1`.
2. Initializes the argument list with `va_start()`.
3. Calls `vsscanf(buf, fmt, args)` to perform the actual parsing.
4. Ends the argument list with `va_end()` and returns the parsing result.

## Standard Library Mapping

When `MR_USE_SSCANF_STD` is enabled:

```c
#define mr_sscanf(_b, _fmt, ...) sscanf(_b, _fmt, ##__VA_ARGS__)
```

In this case, null pointer behavior depends entirely on the platform standard library and does not go through mrX's null
pointer check.

## Usage Example

```c
#include <mrX/libc/mr_sscanf.h>

void sample_sscanf_usage(void)
{
    int id;
    char name[8];

    if (mr_sscanf("42 mrX", "%d %7s", &id, name) == 2) {
        /* id == 42, name == "mrX" */
    }
}
```

## Notes

- The current non-standard configuration still depends on `<stdio.h>` and `vsscanf()`, and is not a fully independent
  scanf implementation.
- Return value semantics are primarily determined by the platform's `vsscanf()`. In tests, null input with `"%d"`
  returns `-1`, and format mismatch returns `0`.
- The caller must provide a sufficiently large destination buffer for conversions such as `%s`; it is recommended to use
  width limits in the format string, e.g., `%7s`.
- When `fmt` is an empty string, the behavior covered by tests returns `0`.

[**中文**](libc_string.md) | [**English**](libc_string_en.md)

---

# libc String and Memory Utilities

`mr_string.h` and `libc/string.c` provide memory operations, C string operations, error code strings, and bit scan
utilities. When `MR_USE_STRING_STD` is enabled, most string interfaces map to the standard library; otherwise, mrX
provides its own implementations. Bit scan functions are always provided as inline functions.

## Core Interface

### Memory Operations

```c
void *mr_memccpy(void *dst, const void *src, int c, mr_size_t size);
int mr_memcmp(const void *m1, const void *m2, mr_size_t size);
void *mr_memcpy(void *dst, const void *src, mr_size_t size);
void *mr_memset(void *dst, int c, mr_size_t size);
void *mr_memchr(const void *s, int c, mr_size_t size);
```

`mr_memccpy()` copies from `src` to `dst`, stopping after encountering byte `c` and returning a pointer to the byte
after `c` in the destination buffer; if not found, it returns `MR_NULL`. `mr_memcpy()` copies by machine word when the
address is aligned to `mr_uintptr_t`, otherwise copies by byte. `mr_memset()` constructs a machine word fill value and
writes as aligned as possible.

### String Operations

```c
int mr_strcmp(const char *s1, const char *s2);
char *mr_strcpy(char *dst, const char *src);
char *mr_strerror(int errnum);
mr_size_t mr_strlen(const char *s);
mr_size_t mr_strnlen(const char *s, mr_size_t size);
int mr_strncmp(const char *s1, const char *s2, mr_size_t size);
char *mr_strncpy(char *dst, const char *src, mr_size_t size);
char *mr_strchr(const char *s, int c);
```

`mr_strerror()` returns the English description string for built-in error codes, returning `"Unknown error"` for unknown
errors. `mr_strncpy()` is implemented based on `mr_memccpy()`; if the source string ends within the limit, the remaining
space is filled with `\0`.

### Bit Scan

```c
int mr_ffs32(mr_uint32_t x);
int mr_ffs64(mr_uint64_t x);
int mr_fls32(mr_uint32_t x);
int mr_fls64(mr_uint64_t x);
```

`mr_ffs32()` and `mr_ffs64()` return the 1-based index of the least significant set bit; returns 0 when input is 0.
`mr_fls32()` and `mr_fls64()` return the 1-based index of the most significant set bit; returns 0 when input is 0.

## Standard Library Mapping

When `MR_USE_STRING_STD` is enabled, `mr_memcmp()`, `mr_memcpy()`, `mr_memset()`, `mr_memchr()`, `mr_strcmp()`,
`mr_strcpy()`, `mr_strerror()`, `mr_strlen()`, `mr_strnlen()`, `mr_strncmp()`, `mr_strncpy()`, and `mr_strchr()` map
directly to standard library functions of the same name. `mr_memccpy()` maps to `memccpy()`.

## Usage Example

```c
#include <mrX/libc/mr_string.h>

void sample_string_usage(void)
{
    char buf[16];

    mr_memset(buf, 0, sizeof(buf));
    mr_strncpy(buf, "abc", sizeof(buf));

    if (mr_strchr(buf, 'b') != MR_NULL) {
        (void)mr_strlen(buf);
    }
}
```

```c
int bit_index(mr_uint32_t flags)
{
    return mr_ffs32(flags);
}
```

## Notes

- Self-implemented versions are only compiled when the `MR_USE_STRING` compile switch is enabled.
- When `MR_USE_STRING_SAFE` is enabled, some interfaces return safe values for null pointers, such as `MR_NULL`, `0`, or
  comparison result `0`; when not enabled, passing null pointers is undefined behavior.
- `mr_memcpy()` does not handle overlapping source and destination regions and cannot replace `memmove()`.
- `mr_strcpy()` does not check the destination buffer size; the caller must ensure sufficient space.
- `mr_strchr()` limits the search range via `mr_strlen(s) + 1`, so null pointer behavior depends on the safety
  configuration of `mr_strlen()` and `mr_memchr()`.
- Tests cover memory copy/fill/compare/search, string copy/compare/length, error strings, and 32/64-bit ffs/fls basic
  boundaries.

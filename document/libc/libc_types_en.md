[**中文**](libc_types.md) | [**English**](libc_types_en.md)

---

# libc Base Types

`mr_types.h` provides unified base types, boundary value macros, booleans, null pointers, and variadic argument wrappers
for mrX. This header reduces type differences across different compilation environments, standard library
configurations, and 32/64-bit architectures.

## Core Types and Macros

### Integer Types

When `MR_USE_TYPES_STD` is enabled, integer types come from `<stdint.h>`; otherwise, C base types are used to define
fixed-width integers.

| Type                                                      | Meaning                           |
|-----------------------------------------------------------|-----------------------------------|
| `mr_int8_t`, `mr_int16_t`, `mr_int32_t`, `mr_int64_t`     | 8/16/32/64-bit signed integers.   |
| `mr_uint8_t`, `mr_uint16_t`, `mr_uint32_t`, `mr_uint64_t` | 8/16/32/64-bit unsigned integers. |
| `MR_INT8_MIN` to `MR_INT64_MAX`                           | Signed integer boundary values.   |
| `MR_UINT8_MAX` to `MR_UINT64_MAX`                         | Unsigned integer maximum values.  |

### Size and Pointer Types

```c
typedef size_t mr_size_t;      /* MR_USE_TYPES_STD */
typedef mr_int64_t mr_ssize_t; /* MR_USE_ARCH_64B */
typedef void *mr_ptr_t;
```

`mr_size_t` maps to `size_t` in standard type mode, otherwise selects `mr_uint64_t` or `mr_uint32_t` based on
`MR_USE_ARCH_64B`. `mr_ssize_t` always selects a signed type based on the architecture bit width.

| Macro                          | Meaning                                                                 |
|--------------------------------|-------------------------------------------------------------------------|
| `MR_SIZE_MAX`                  | Maximum value of `mr_size_t`.                                           |
| `MR_SSIZE_MIN`, `MR_SSIZE_MAX` | Boundary values of `mr_ssize_t`.                                        |
| `mr_intptr_t`, `mr_uintptr_t`  | Signed/unsigned integer types that can hold pointer values.             |
| `MR_NULL`                      | Null pointer. `NULL` under standard type mode, otherwise `((void *)0)`. |

### Boolean and Floating-point Types

`mr_bool_t` maps to `bool` under `MR_USE_TYPES_STD`, otherwise to `int`. Corresponding boolean constants are `MR_FALSE`
and `MR_TRUE`.

`mr_f32_t` and `mr_f64_t` are fixed to `float` and `double` respectively.

### Variadic Argument Wrappers

```c
typedef va_list mr_va_list;
#define mr_va_start(_a, _l) va_start(_a, _l)
#define mr_va_arg(_a, _t)   va_arg(_a, _t)
#define mr_va_end(_a)       va_end(_a)
```

Variadic arguments directly use `<stdarg.h>`, exposed via `mr_` prefixed macros to modules such as formatted output.

## Main Interface

This module does not provide runtime functions; it mainly provides base ABI types for other modules via typedefs and
macros.

## Usage Example

```c
#include <mrX/libc/mr_types.h>

mr_size_t copy_limit(mr_size_t requested)
{
    if (requested > 1024U) {
        return 1024U;
    }
    return requested;
}

mr_bool_t is_empty(const void *ptr)
{
    return (ptr == MR_NULL) ? MR_TRUE : MR_FALSE;
}
```

## Notes

- `MR_USE_TYPES_STD` determines whether to depend on `<stdbool.h>`, `<stddef.h>`, and `<stdint.h>`.
- When standard type mode is not enabled, fixed-width types depend on the current compiler's width conventions for
  `char`, `short`, `int`, and `long long`.
- `mr_size_t` and `mr_ssize_t` are related to `MR_USE_ARCH_64B`; do not assume they are fixed at 32 bits when storing
  binary data across architectures.
- `mr_va_list` is still the standard `va_list`; the same argument list cannot be consumed repeatedly across functions
  unless the caller copies or reinitializes it according to C standard rules.

[**中文**](libc_assert.md) | [**English**](libc_assert_en.md)

---

# libc Assertions

`mr_assert.h` provides runtime assertions, panics, and compile-time static assertions. This module selects standard
library assertions, third-party assertions, mrX port layer assertions, or no-op assertions based on configuration.

## Core Macros and Interfaces

### Runtime Assertions

```c
#define MR_ASSERT(_expr) ...
#define MR_PANIC()       ...
```

| Configuration       | `MR_ASSERT(_expr)` Behavior                                                                                 | `MR_PANIC()` Behavior                                                              |
|---------------------|-------------------------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------|
| `MR_USE_ASSERT_STD` | Calls `assert(_expr)`.                                                                                      | Calls `assert(0)`.                                                                 |
| `MR_USE_ASSERT_3RD` | Provided by `mrX/3party/libc/mr_assert.h`.                                                                  | Provided by the third-party header.                                                |
| `MR_USE_ASSERT`     | When expression is false, calls `mr_port_assert(__FILE__, __LINE__, #_expr)`, then enters an infinite loop. | Calls `mr_port_assert(__FILE__, __LINE__, "panic")`, then enters an infinite loop. |
| None of the above   | The expression is not evaluated; the macro is a no-op.                                                      | Enters an infinite loop directly.                                                  |

### Port Layer Assertion Callback

```c
extern void mr_port_assert(const char *file, int line, const char *expr);
```

When `MR_USE_ASSERT` is enabled, assertion failure calls the port layer implementation. `file` is the source file,
`line` is the line number, and `expr` is the failed expression string or `"panic"`.

### Static Assertion

```c
#define MR_STATIC_ASSERT(_name, _expr) \
    typedef char __mr_static_assert_##_name[(_expr) ? 1 : -1]
```

`MR_STATIC_ASSERT()` checks an expression at compile time. When the expression is false, the array length is `-1`,
causing compilation to fail.

## Usage Example

```c
#include <mrX/libc/mr_assert.h>
#include <mrX/libc/mr_types.h>

MR_STATIC_ASSERT(size_is_supported, sizeof(void *) <= sizeof(mr_uint64_t));

void sample_require_pointer(void *ptr)
{
    MR_ASSERT(ptr != MR_NULL);
}
```

The port layer can implement the following callback to output the assertion location or trigger a platform debug
mechanism:

```c
#include <mrX/port/mr_port_assert.h>

void mr_port_assert(const char *file, int line, const char *expr)
{
    (void)file;
    (void)line;
    (void)expr;
}
```

## Notes

- When assertions are disabled, `MR_ASSERT(_expr)` expands to `((void)0)`, and the expression is not evaluated;
  therefore, do not place code with side effects inside assertion expressions.
- `MR_PANIC()` enters an infinite loop in all configurations except standard and third-party modes.
- In `MR_USE_ASSERT` mode, after `mr_port_assert()` returns, it still enters an infinite loop.
- `MR_STATIC_ASSERT()` generates a typedef name; `_name` must not be duplicated within the same scope.

[**中文**](libc_errno.md) | [**English**](libc_errno_en.md)

---

# libc Error Codes

`mr_errno.h` defines the error code type and error code macros used internally by mrX. This module uniformly abstracts
differences between the standard library, third-party libc, and built-in error codes, allowing kernel objects, memory
management, and other components to return negative error codes or error reasons.

## Core Type and Macros

### Error Type

```c
typedef int mr_err_t;
```

`mr_err_t` is used to represent interface return status. The common convention in mrX source code is to return `0` on
success and a negative error code on failure, e.g., `-MR_EINVAL`.

### Error Code Source

| Configuration      | Behavior                                                                             |
|--------------------|--------------------------------------------------------------------------------------|
| `MR_USE_ERRNO_STD` | Includes `<errno.h>`, macros such as `MR_EPERM` map to standard errno.               |
| `MR_USE_ERRNO_3RD` | Includes `mrX/3party/libc/mr_errno.h`, error codes provided by a third-party header. |
| Neither enabled    | Uses built-in values from `mr_errno.h`.                                              |

### Built-in Error Codes

| Macro          | Built-in Value | Meaning                           |
|----------------|---------------:|-----------------------------------|
| `MR_EPERM`     |              1 | Operation not permitted.          |
| `MR_ENOENT`    |              2 | No such entry.                    |
| `MR_EIO`       |              5 | I/O error.                        |
| `MR_EAGAIN`    |             11 | Resource temporarily unavailable. |
| `MR_ENOMEM`    |             12 | Not enough memory.                |
| `MR_EBUSY`     |             16 | Resource busy.                    |
| `MR_EEXIST`    |             17 | Target already exists.            |
| `MR_EINVAL`    |             22 | Invalid argument.                 |
| `MR_ENOSYS`    |             40 | Function not implemented.         |
| `MR_ETIMEDOUT` |            116 | Operation timed out.              |

## Main Interface

This module does not provide function interfaces, only `mr_err_t` and error code macros. Error code to string conversion
is provided by `mr_strerror()`, see `libc_string.md`.

## Usage Example

```c
#include <mrX/libc/mr_errno.h>
#include <mrX/libc/mr_types.h>

mr_err_t sample_check(void *ptr)
{
    if (ptr == MR_NULL) {
        return -MR_EINVAL;
    }
    return 0;
}
```

## Notes

- The built-in values in the documentation only apply when neither `MR_USE_ERRNO_STD` nor `MR_USE_ERRNO_3RD` is enabled.
- When using standard errno, the specific values are determined by the platform C library.
- mrX code typically returns negative error codes; the error code macros themselves are positive.
- This header only declares error codes and does not maintain a thread-local `errno` variable.

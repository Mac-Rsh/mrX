[**中文**](libc_compiler.md) | [**English**](libc_compiler_en.md)

---

# libc Compiler Adaptation

`mr_compiler.h` encapsulates compiler attributes and common compile-time helper macros. This module is used to unify
conventions for alignment, section placement, weak symbols, inlining, and unused markings.

## Core Macros

The current implementation supports `__GNUC__`, `__clang__`, or `__ARMCC_VERSION` compiler environments; otherwise, it
triggers a compilation error.

| Macro                  | Expansion                                | Purpose                                                     |
|------------------------|------------------------------------------|-------------------------------------------------------------|
| `MR_ALIGN(_x)`         | `__attribute__((aligned(_x)))`           | Specifies object alignment.                                 |
| `MR_SECTION(_x)`       | `__attribute__((section(_x)))`           | Specifies the section for an object or function.            |
| `MR_UNUSED`            | `__attribute__((unused))`                | Marks potentially unused symbols.                           |
| `MR_USED`              | `__attribute__((used))`                  | Instructs the compiler to retain a symbol.                  |
| `MR_WEAK`              | `__attribute__((weak))`                  | Defines a weak symbol.                                      |
| `MR_INLINE`            | `static __inline`                        | Defines an inline function in a header.                     |
| `MR_ALIGN_SIZE`        | `sizeof(void *)`                         | mrX default machine word alignment size.                    |
| `MR_SECTION_ALIGN(_x)` | `MR_ALIGN(MR_ALIGN_SIZE) MR_SECTION(_x)` | Places into a specified section with default alignment.     |
| `MR_IGNORE(_x)`        | `(void)(_x)`                             | Explicitly ignores a variable, suppressing unused warnings. |

## Main Interface

This module does not provide runtime functions; all capabilities take effect at compile time through macros.

## Usage Example

```c
#include <mrX/libc/mr_compiler.h>

static int MR_ALIGN(MR_ALIGN_SIZE) aligned_value;

MR_WEAK void board_hook(void)
{
}

static void sample(int unused_arg)
{
    MR_IGNORE(unused_arg);
}
```

## Notes

- `MR_ALIGN_SIZE` is the pointer width, not necessarily equal to the cache line size.
- The section name for `MR_SECTION(_x)` must conform to linker script conventions; otherwise, the object may not be
  placed or retained correctly.
- `MR_USED` can only instruct the compiler to retain a symbol; whether it is preserved at link time is also affected by
  the linker script and garbage collection options.
- Non-GCC/Clang/ARMCC compatible compilers are currently not supported.

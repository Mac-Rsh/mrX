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

### Aligning Objects

`MR_ALIGN()` can be used to require an object to be aligned to a specified number of bytes. Common use cases include DMA
buffers, hardware descriptors, stack storage, or data that must be machine-word aligned.

```c
#include <mrX/libc/mr_compiler.h>

static unsigned char MR_ALIGN(32) dma_buffer[128];
static int MR_ALIGN(MR_ALIGN_SIZE) aligned_value;
```

### Placing Symbols into Sections

`MR_SECTION()` can place an object or function into a section defined by the linker script. The section name must match
the project linker script.

```c
#include <mrX/libc/mr_compiler.h>

static const char MR_SECTION(".rodata.board") board_name[] = "demo-board";

void MR_SECTION(".fast_text") fast_irq_handler(void)
{
    /* Code that should be placed in a fast execution region. */
}
```

### Placing Symbols with Default Alignment

`MR_SECTION_ALIGN()` is equivalent to `MR_ALIGN(MR_ALIGN_SIZE) MR_SECTION(_x)`. It is useful for placing table entries,
descriptors, or auto-registration objects into a section while keeping mrX default machine-word alignment.

```c
#include <mrX/libc/mr_compiler.h>

struct command_entry {
    const char *name;
    void (*handler)(void);
};

static void reboot_handler(void)
{
}

static const struct command_entry MR_USED MR_SECTION_ALIGN(".mr_cmd") reboot_cmd = {
    "reboot",
    reboot_handler,
};
```

### Defining Overridable Weak Hooks

`MR_WEAK` can define a default implementation that may be overridden by a strong symbol with the same name from board
or application code.

```c
#include <mrX/libc/mr_compiler.h>

MR_WEAK void board_hook(void)
{
    /* Default no-op implementation. */
}
```

### Retaining Static Symbols

`MR_USED` tells the compiler not to remove a symbol just because it is not directly referenced by C code. It is commonly
used together with `MR_SECTION()` or `MR_SECTION_ALIGN()`.

```c
#include <mrX/libc/mr_compiler.h>

static const char MR_USED MR_SECTION(".mr_version") build_version[] = "1.0.0";
```

### Defining Header Inline Functions

`MR_INLINE` is used to define small `static inline` style functions in headers, avoiding duplicate external definitions
when the same header is included by multiple source files.

```c
#include <mrX/libc/mr_compiler.h>

MR_INLINE int mr_max_int(int a, int b)
{
    return (a > b) ? a : b;
}
```

### Marking or Ignoring Unused Items

`MR_IGNORE()` explicitly ignores parameters or local variables inside a function body. `MR_UNUSED` can directly mark
functions, variables, or parameters that may not be referenced.

```c
#include <mrX/libc/mr_compiler.h>

static void sample(int unused_arg, void *context MR_UNUSED)
{
    MR_IGNORE(unused_arg);
}

static void debug_dump(void) MR_UNUSED;

static void debug_dump(void)
{
}
```

## Notes

- `MR_ALIGN_SIZE` is the pointer width, not necessarily equal to the cache line size.
- The section name for `MR_SECTION(_x)` must conform to linker script conventions; otherwise, the object may not be
  placed or retained correctly.
- `MR_USED` can only instruct the compiler to retain a symbol; whether it is preserved at link time is also affected by
  the linker script and garbage collection options.
- Non-GCC/Clang/ARMCC compatible compilers are currently not supported.

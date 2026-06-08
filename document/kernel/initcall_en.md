[**中文**](kernel_initcall.md) | [**English**](kernel_initcall_en.md)

---

# Initialization Calls

The initialization call module is used to place initialization functions into designated link sections and execute them
in order sorted by link section name during user initialization. The module also provides the kernel initialization
entry point, responsible for bootstrapping basic components such as the platform, memory allocator, timer, and
interrupts.

Entries exported by `MR_INIT_EXPORT()` rely on the `initcall` section rules in `sections.toml` to be retained and
sorted. The linker script or build system must provide the `__mr_initcall_start` and `__mr_initcall_end` symbols;
`mr_user_init()` traverses the initialization table using these two symbols.

## Core Data Structures and Macros

### Initialization Level

```c
#define MR_INIT_LEVEL_IRQ          "1.0"
#define MR_INIT_LEVEL_DRIVER       "1.1"
#define MR_INIT_LEVEL_USER(_level) "8."_level
```

The initialization level is part of the link section name. Currently predefined levels include the interrupt level,
driver level, and user level. The user level forms `8.<level>` via string concatenation.

`kernel/initcall.c` internally also exports two boundary entries: `start` uses level `"0.end"` and `end` uses level
`"9.end"`. These ensure start and end entries exist within the user initialization traversal range.

### Initialization Function and Entry

```c
typedef void(mr_initcall_entry_t)(void);

typedef struct mr_initcall {
    const char *name;
    mr_initcall_entry_t *entry;
} mr_initcall_t;
```

`name` holds the initialization function name string, `entry` holds the initialization function pointer. Initialization
functions take no parameters and return no error code.

### Export Macro

```c
#define MR_INIT_EXPORT(_entry, _level)                                         \
    MR_USED const mr_initcall_t MR_CAT(__mr_initcall_, _entry)                 \
        MR_SECTION_ALIGN("mr_initcall." _level)                                \
        = {.name = MR_STR(_entry), .entry = (_entry)}
```

`MR_INIT_EXPORT()` defines a `const mr_initcall_t` object with the variable name `__mr_initcall_<entry>`, the section
name `mr_initcall.<level>`, and uses `MR_USED` to prevent the compiler from removing it.

### Section Configuration

The relevant initialization call configuration in `sections.toml` is as follows:

```toml
[[section]]
name = "initcall"
input = "mr_initcall.*"
output = ".text"
keep = true
sort = true
comment = "Initcall"
```

This configuration requires retaining all `mr_initcall.*` input sections, sorting them by section name, and outputting
them to `.text`. Therefore, the initialization level string directly affects the execution order.

## Main Interfaces

### Export Initialization Function

```c
MR_INIT_EXPORT(entry, level);
```

### Kernel Initialization

```c
void mr_kernel_init(void);
```

`mr_kernel_init()` uses an internal static state `__initcall_kstatus` to guarantee single execution. Depending on
configuration options, it calls in order:

1. `mr_platform_init()`, when `MR_USE_PLATFORM` is enabled.
2. `mr_libc_malloc_init()`, when `MR_USE_MALLOC` is enabled.
3. `mr_kernel_timer_init()`, when `MR_USE_TIMER` is enabled.
4. `mr_kernel_irq_init()`, when `MR_USE_IRQ` is enabled.

### User Initialization

```c
void mr_user_init(void);
```

`mr_user_init()` first asserts that `__mr_initcall_start < __mr_initcall_end`, then calls `mr_kernel_init()`. The
function then traverses from `__mr_initcall_start` to `__mr_initcall_end`, invoking `call->entry()` for each
`mr_initcall_t`.

## Usage Example

```c
#include <mrX/kernel/mr_initcall.h>

static void driver_init(void) {
    /* initialize driver resources */
}
MR_INIT_EXPORT(driver_init, MR_INIT_LEVEL_DRIVER);

static void app_init(void) {
    /* initialize user module */
}
MR_INIT_EXPORT(app_init, MR_INIT_LEVEL_USER("0"));

void boot(void) {
    mr_user_init();
}
```

## Notes

- Initialization functions have no return value; failure handling must be done within the function itself.
- Initialization order depends on the string sorting of section names, not the compilation order of C files.
- `MR_INIT_LEVEL_USER(_level)` concatenates strings directly; the passed argument should form a valid string literal,
  e.g., `"0"`.
- Both `mr_kernel_init()` and `mr_user_init()` execute only once; repeated calls will return directly or skip user
  entries.
- The build system must retain and sort `mr_initcall.*` sections according to `sections.toml`, and provide the
  `__mr_initcall_start` and `__mr_initcall_end` symbols.
- The initialization state uses `volatile mr_bool_t`, which is not a general concurrency synchronization primitive; this
  entry should be called within a controlled boot flow.

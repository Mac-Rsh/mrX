[**中文**](kernel_entry.md) | [**English**](kernel_entry_en.md)

---

# Kernel Entry

The kernel entry module provides an optional system startup entry wrapper. When `MR_USE_ENTRY` is enabled, the entry
code calls `mr_kernel_init()` before entering the user's `main()`, to execute the mrX kernel initialization sequence.

This document corresponds to `kernel/entry.c`, `include/mrX.h`, and the entry-related configuration in Kconfig.

## Module Purpose

The purpose of the entry module is to unify kernel initialization before the application's main function executes. The
private `entry()` function in `kernel/entry.c` performs a single operation: calling `mr_kernel_init()`. Execution then
proceeds to the real user `main()`.

Different toolchains use different entry mechanisms. ARMCC uses `$Sub$$main()` to wrap `$Super$$main()`; other
toolchains provide `mr_entry()`, which can be called by startup code or the platform project.

## Entry Configuration

### MR_USE_ENTRY

```kconfig
config MR_USE_ENTRY
    bool "Use entry"
    default n
```

`MR_USE_ENTRY` controls whether `kernel/entry.c` compiles the entry wrapper. Disabled by default. When enabled, the
entry module includes `mr_initcall.h` and calls `mr_kernel_init()`.

## Main Implementation

### Internal Entry Function

```c
static void entry(void) {
    mr_kernel_init();
}
```

`entry()` is an internal function in `kernel/entry.c` and is not declared externally. It triggers kernel initialization.

### ARMCC main Wrapper

```c
extern int $Super$$main(void);

int $Sub$$main(void) {
    entry();
    $Super$$main();
    return 0;
}
```

### Generic Entry Function

```c
extern int main(void);

int mr_entry(void) {
    entry();
    main();
    return 0;
}
```

For non-ARMCC toolchains, the module defines `mr_entry()`. This function first performs kernel initialization, then
calls the user's `main()`.

## Public Header Relationship

`include/mrX.h` is the aggregate header for mrX. It includes common kernel headers, such as `mr_initcall.h`,
`mr_hook.h`, `mr_log.h`, `mr_printf.h`, `mr_version.h`, and others.

## Usage Example

```c
#include <mrX.h>

int main(void) {
    /* mr_kernel_init() has been called before main when entry is wired in. */
    return 0;
}
```

For non-ARMCC projects that wish to use the mrX entry, the startup code must enter `mr_entry()`:

```c
extern int mr_entry(void);

void platform_start(void) {
    (void)mr_entry();
}
```

## Notes

- `MR_USE_ENTRY` is disabled by default; `kernel/entry.c` does not provide the entry wrapper when not enabled.
- The ARMCC path relies on the `$Sub$$main` and `$Super$$main` linker mechanism.
- The non-ARMCC path only defines `mr_entry()` and does not automatically replace `main()`; the startup code must call
  it explicitly.
- Both `mr_entry()` and `$Sub$$main()` ignore the return value of the user's `main()` and return 0.
- The current public headers do not contain a declaration for `mr_entry()`; external startup code must declare it
  themselves.

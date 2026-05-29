[**中文**](kernel_version.md) | [**English**](kernel_version_en.md)

---

# Kernel Version

The kernel version header provides mrX version number constants, version encoding macros, and version matching macros.
This module has no corresponding C source file; all capabilities are provided by preprocessor macros in
`include/mrX/kernel/mr_version.h`.

This document corresponds to `include/mrX/kernel/mr_version.h`.

## Module Purpose

The version module is used to identify the current mrX version at compile time or in runtime code. Callers can read the
major, minor, and patch versions directly, or use the unified version encoding for comparison.

The current header defines the version as `1.0.0`.

## Core Macros

### Version Number Constants

```c
#define MR_VERSION_MAJOR                (1U)
#define MR_VERSION_MINOR                (0U)
#define MR_VERSION_PATCH                (0U)
```

These three macros represent the major, minor, and patch versions respectively. They are all unsigned integer constants.

### Version Encoding

```c
#define MR_VERSION_CODE(_major, _minor, _patch)                                \
    (((_major) << 16) | ((_minor) << 8) | (_patch))
```

`MR_VERSION_CODE()` encodes the three version numbers into a single integer: the major version occupies bits 16 to 31,
the minor version occupies bits 8 to 15, and the patch version occupies bits 0 to 7.

### Current Version Encoding

```c
#define MR_VERSION                                                             \
    MR_VERSION_CODE(MR_VERSION_MAJOR, MR_VERSION_MINOR, MR_VERSION_PATCH)
```

`MR_VERSION` is the encoded value of the current mrX version. For `1.0.0`, the encoded result is `0x00010000`.

### Version Matching

```c
#define MR_VERSION_IS(_major, _minor, _patch)                                  \
    (MR_VERSION == MR_VERSION_CODE(_major, _minor, _patch))
```

`MR_VERSION_IS()` checks whether the current version equals the specified version. This macro performs an exact match,
not a greater-than, less-than, or compatibility check.

## Usage Example

```c
#include <mrX/kernel/mr_version.h>

#if MR_VERSION_IS(1, 0, 0)
/* Code for mrX 1.0.0 */
#endif

unsigned int version_code(void) {
    return MR_VERSION;
}
```

The target version encoding can also be constructed directly:

```c
if (MR_VERSION == MR_VERSION_CODE(1U, 0U, 0U)) {
    /* The current version is 1.0.0. */
}
```

## Notes

- `MR_VERSION_CODE()` does not mask the input range; callers should ensure that the minor and patch versions fit within
  8 bits to avoid field overlap.
- `MR_VERSION_IS()` performs an exact version comparison and does not imply API compatibility.
- The version header is included in the kernel public header set via `include/mrX.h`; these macros are available after
  including `<mrX.h>`.

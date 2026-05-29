[**中文**](kernel_service.md) | [**English**](kernel_service_en.md)

---

# Service Macros and Basic Tools

The Service module provides common macros and small inline utility functions in the kernel, including container address
calculation, array length computation, min/max and alignment, variadic macro helpers, temporary compound literal
construction, and parity calculation. This module maintains no runtime state and has no corresponding `.c`
implementation file; all interfaces are provided in `mr_service.h` as macros or `MR_INLINE` functions.

## Core Macros and Functions

### Container and Array Tools

```c
#define MR_CONTAINER_OF(_ptr, _type, _member) \
    ((void *)((char *)(_ptr) - (char *)((void *)&(((_type *)0)->_member))))

#define MR_ARRAY_SIZE(_array) (sizeof(_array) / sizeof((_array)[0]))
```

`MR_CONTAINER_OF()` calculates the address of the outer structure from a member pointer. Test cases use the address of
structure member `b` to recover the containing structure object.

`MR_ARRAY_SIZE()` returns the number of elements in a C array. This macro should only be used on real arrays, not on
function parameters that have decayed to pointers.

### Min/Max and Alignment

```c
#define MR_MIN(_a, _b) (((_a) < (_b)) ? (_a) : (_b))
#define MR_MAX(_a, _b) (((_a) > (_b)) ? (_a) : (_b))
#define MR_ALIGN_UP(_x, _ali)   (((_x) + (_ali) - 1) & ~((_ali) - 1))
#define MR_ALIGN_DOWN(_x, _ali) ((_x) & ~((_ali) - 1))
```

`MR_MIN()` and `MR_MAX()` return the smaller or larger of two expressions. `MR_ALIGN_UP()` rounds up, `MR_ALIGN_DOWN()`
rounds down.

The current alignment macros use bitmask calculation, requiring `_ali` to be suitable as a binary alignment value,
typically a power of two.

### Round Division

```c
#define MR_ROUND_DIV(_a, _b) \
    (((_a) + ((((_a) ^ (_b)) >= 0) ? (_b) : -(_b)) / 2) / (_b))
```

`MR_ROUND_DIV()` performs integer division and compensates by half the divisor according to the sign of the divisor,
used to obtain a rounded quotient.

### Temporary Object Construction

```c
#define MR_MAKE_LOCAL(_type, ...) (&((_type){__VA_ARGS__}))
```

`MR_MAKE_LOCAL()` uses a compound literal to create a temporary object in the current scope and returns the object
address. Test cases cover struct and integer temporary objects.

### Variadic and Concatenation Tools

```c
#define MR_VA_ARGS_NUM(...) /* returns the number of arguments, range 0-63 */
#define MR_STR(_s)          /* converts argument to string */
#define MR_CAT(...)         /* concatenates multiple tokens */
#define MR_CAT_SEP(_s, ...) /* concatenates multiple tokens with a separator prefix */
#define MR_DE_BRACKET(...)  __VA_ARGS__
#define MR_MACRO_IF(_c, _if, _else) /* conditional expansion */
#define MR_MACRO_IS_USED(_m)        /* checks if a macro is defined as 1 by convention */
```

These macros primarily serve conditional expansion, name generation, and configuration checks in other kernel headers.
`MR_MACRO_IF()` requires `_if` and `_else` arguments to be passed with extra parentheses to avoid commas in expressions
breaking the macro argument list; internally it uses `MR_DE_BRACKET` to remove the protective parentheses.

### Parity

```c
MR_INLINE mr_bool_t mr_parity_even(const void *src, mr_size_t size);
MR_INLINE mr_bool_t mr_parity_odd(const void *src, mr_size_t size);
```

`mr_parity_even()` counts the number of set bits in the `size` bytes of data pointed to by `src`, returning `MR_TRUE`
when the set bit count is even. `mr_parity_odd()` returns the inverse of the even parity result, returning `MR_TRUE`
when the set bit count is odd.

Test cases calculate bit parity for multiple values within a 16-bit integer range and compare them against the return
values of these two interfaces.

## Usage Examples

### Getting Container via Member Pointer

```c
#include <mrX/kernel/mr_service.h>

typedef struct sample_node {
    int id;
    char state;
} sample_node_t;

void sample_container_usage(void)
{
    sample_node_t node = {1, 'r'};
    char *state = &node.state;
    sample_node_t *owner;

    owner = MR_CONTAINER_OF(state, sample_node_t, state);
    (void)owner;
}
```

### Alignment and Parity

```c
#include <mrX/kernel/mr_service.h>

void sample_service_usage(void)
{
    mr_uint32_t value = 0x55U;
    mr_uint32_t aligned;
    mr_bool_t even;

    aligned = MR_ALIGN_UP(13U, 8U);
    even = mr_parity_even(&value, sizeof(value));

    (void)aligned;
    (void)even;
}
```

## Notes

- Macros such as `MR_MIN()` and `MR_MAX()` may evaluate their arguments multiple times; do not pass expressions with
  side effects, e.g., `i++`.
- `MR_ARRAY_SIZE()` can only be used on array objects visible at compile time, not on pointers.
- The `_ali` parameter of `MR_ALIGN_UP()` and `MR_ALIGN_DOWN()` should typically be a power of two; otherwise the
  bitmask alignment result will not have generic division-based alignment semantics.
- `MR_MAKE_LOCAL()` returns the address of a compound literal in the current scope; it should not be saved or used
  asynchronously after the object's lifetime ends.
- `MR_CONTAINER_OF()` depends on the member actually belonging to the specified container type; passing a non-matching
  pointer yields an invalid address.

[**中文**](kernel_ref.md) | [**English**](kernel_ref_en.md)

---

# Reference Counting

The reference counting module provides lightweight lifecycle marking and atomic reference counting. `mr_ref_t` stores a
fixed magic value and a count value in the same atomic variable; after initialization, the reference count is 1. Callers
increase the reference via `mr_ref_get()`, release it via `mr_ref_put()`, and a release callback is triggered when the
last reference is released.

This module only manages the reference count itself, not the memory layout of the referenced object. The `mr_object_t`
in the object management module embeds `mr_ref_t` to reuse this capability.

## Core Data Structures and Macros

### Reference Count Type

```c
typedef volatile mr_atomic_t mr_ref_t;
typedef void(mr_ref_release_t)(mr_ref_t *ref);
```

`mr_ref_t` is an atomic variable type used to store both magic and reference count. `mr_ref_release_t` is the callback
type invoked when the last reference is released, with the callback parameter being the current reference count object.

### Magic and Count Bits

```c
#if defined(MR_USE_REF_LONG_COUNT)
#define MR_REF_MAGIC      (0x6d720000U)
#define MR_REF_MAGIC_MASK (0xffff0000U)
#define MR_REF_OVERFLOW   (0x6d730000U)
#else
#define MR_REF_MAGIC      (0x6d725800U)
#define MR_REF_MAGIC_MASK (0xffffff00U)
#define MR_REF_OVERFLOW   (0x6d725900U)
#endif
```

When `MR_USE_REF_LONG_COUNT` is not defined, the reference count uses the lower 8 bits and the magic uses the upper 24
bits; when `MR_USE_REF_LONG_COUNT` is defined, the reference count uses the lower 16 bits and the magic uses the upper
16 bits. `MR_REF_OVERFLOW` is used to trigger an assertion in `mr_ref_get()` when an overflow marker is detected.

### Initialization and Read Macros

```c
#define MR_REF_INIT() MR_ATOMIC_INIT(MR_REF_MAGIC | 0x0001U)
#define MR_REF_IS_INITED(_ref) (((_ref) & MR_REF_MAGIC_MASK) == MR_REF_MAGIC)
#define MR_REF_COUNT(_ref) ((_ref) & (~MR_REF_MAGIC))
```

`MR_REF_INIT()` is used for static initialization, with an initial count of 1. `MR_REF_IS_INITED()` checks whether the
magic is valid. `MR_REF_COUNT()` reads the lower count bits.

## Main Interfaces

### Initializing Reference Count

```c
void mr_ref_init(mr_ref_t *ref);
```

`mr_ref_init()` sets `ref` to `MR_REF_MAGIC | 1`, granting one valid lifecycle. This function does not check the old
value; repeated initialization will overwrite the current count state.

### Acquiring Reference

```c
mr_bool_t mr_ref_get(mr_ref_t *ref);
```

`mr_ref_get()` first reads the current value. If the magic is invalid or the count is 0, it returns `MR_FALSE`,
indicating the object's lifecycle has ended or has not been initialized. Otherwise, it increments the count by 1 via
`mr_atomic_compare_exchange()` and returns `MR_TRUE` on success.

When the current value equals `MR_REF_OVERFLOW`, an assertion in the function fails. This behavior is designed to expose
count overflow rather than returning a normal error code.

### Releasing Reference

```c
mr_bool_t mr_ref_put(mr_ref_t *ref, mr_ref_release_t *release);
```

`mr_ref_put()` decrements the count by 1 using `mr_atomic_fetch_sub()`. If before decrementing, the magic is valid, the
count is 1, and `release` is non-null, it first clears the magic marker, then calls `release(ref)`, and returns
`MR_TRUE`. In all other cases, it returns `MR_FALSE`.

Note that the current implementation only executes the "last reference released" branch when `release != MR_NULL`; if
the last `put` passes a null release function, the function will not clear the magic nor return `MR_TRUE`.

## Usage Example

```c
static void my_release(mr_ref_t *ref) {
    /* Release resources of the object containing ref, or mark the object as released. */
}

void example(void) {
    mr_ref_t ref;

    mr_ref_init(&ref);

    if (mr_ref_get(&ref)) {
        /* Use the object protected by ref. */
        mr_ref_put(&ref, my_release);
    }

    if (mr_ref_put(&ref, my_release)) {
        /* The last reference has been released, my_release has been called. */
    }
}
```

Tests in `test/kernel/ref.c` cover static initialization, dynamic initialization, consecutive reference acquires,
callback invocation on last release, and `mr_ref_get()` returning failure after lifecycle ends.

## Notes

- `mr_ref_t` only guarantees atomic read-modify-write for reference count operations; it does not automatically protect
  other fields of the object.
- `mr_ref_put()` does not check for count underflow; continuing to `put` an already released or uninitialized reference
  will continue to modify the atomic value.
- The `release` callback is called after the magic has been cleared; within the callback, `MR_REF_IS_INITED()` should no
  longer be assumed to be true.
- The bit width of the reference count is determined by `MR_USE_REF_LONG_COUNT`; the default lower 8-bit count is
  suitable for objects with a small reference count.
- Static initialization uses `MR_REF_INIT()`, runtime initialization uses `mr_ref_init()`, both produce an initial count
  of 1.

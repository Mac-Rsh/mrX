[**中文**](libc_atomic.md) | [**English**](libc_atomic_en.md)

---

# libc Atomic Operations

`mr_atomic.h` provides integer atomic operations used internally by mrX. The default implementation protects
read-modify-write critical sections by disabling interrupts; when `MR_USE_ATOMIC_3RD` is enabled, the specific types and
interfaces are provided by a third-party header.

## Core Type and Macro

```c
typedef mr_intptr_t mr_atomic_t;
#define MR_ATOMIC_INIT(_val) (_val)
```

In the default implementation, `mr_atomic_t` is a signed type that can hold a pointer-width integer. Static
initialization can use `MR_ATOMIC_INIT()`.

All default atomic functions are defined as inline functions and, during access, call:

```c
int mask = mr_port_irq_disable();
...
mr_port_irq_enable(mask);
```

## Main Interface

### Initialization, Load, and Store

```c
void mr_atomic_init(volatile mr_atomic_t *atomic, mr_atomic_t val);
mr_atomic_t mr_atomic_load(volatile mr_atomic_t *atomic);
void mr_atomic_store(volatile mr_atomic_t *atomic, mr_atomic_t val);
```

`mr_atomic_init()` directly writes the initial value. `mr_atomic_load()` and `mr_atomic_store()` read or write the
atomic variable within an interrupt-disabled critical section.

### Exchange

```c
mr_atomic_t mr_atomic_exchange(volatile mr_atomic_t *atomic, mr_atomic_t val);
```

This interface updates `atomic` to `val` and returns the old value before the update.

### Compare-and-Exchange

```c
mr_bool_t mr_atomic_compare_exchange(volatile mr_atomic_t *atomic,
                                     mr_atomic_t *val,
                                     mr_atomic_t des);
```

When `*atomic == *val`, writes `des` and returns `MR_TRUE`. Otherwise, writes the current `*atomic` back to `*val` and
returns `MR_FALSE`.

### Fetch Operations

```c
mr_atomic_t mr_atomic_fetch_add(volatile mr_atomic_t *atomic, mr_atomic_t val);
mr_atomic_t mr_atomic_fetch_sub(volatile mr_atomic_t *atomic, mr_atomic_t val);
mr_atomic_t mr_atomic_fetch_and(volatile mr_atomic_t *atomic, mr_atomic_t val);
mr_atomic_t mr_atomic_fetch_or(volatile mr_atomic_t *atomic, mr_atomic_t val);
mr_atomic_t mr_atomic_fetch_xor(volatile mr_atomic_t *atomic, mr_atomic_t val);
```

These interfaces return the old value before modification, then perform addition, subtraction, bitwise AND, bitwise OR,
and bitwise XOR respectively.

## Usage Example

```c
#include <mrX/libc/mr_atomic.h>

static volatile mr_atomic_t counter = MR_ATOMIC_INIT(0);

void sample_atomic_usage(void)
{
    mr_atomic_t expect;

    mr_atomic_fetch_add(&counter, 1);

    expect = 1;
    if (mr_atomic_compare_exchange(&counter, &expect, 10) == MR_FALSE) {
        /* expect has been updated to the current value of counter. */
    }
}
```

Test cases cover static initialization, load/store, exchange, compare-exchange success and failure paths, and the
old-value return semantics of fetch-add/sub/and/or/xor.

## Notes

- The default implementation depends on `mr_port_irq_disable()` and `mr_port_irq_enable()`, suitable for single-core or
  specific port layer critical sections protected by disabling interrupts.
- `mr_atomic_init()` does not disable interrupts itself; it should typically be called before the object is published.
- The default implementation has no explicit memory order parameters and is not equivalent to the full cross-core memory
  model of C11 atomics.
- The parameter pointer must be valid; the default implementation does not perform null pointer checks.

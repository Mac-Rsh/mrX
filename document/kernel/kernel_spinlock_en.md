[**中文**](kernel_spinlock.md) | [**English**](kernel_spinlock_en.md)

---

# Spinlock

The Spinlock module provides busy-wait mutual exclusion primitives based on atomic variables, along with lock operations
that save and restore interrupt state. This module is used to protect short critical sections, especially paths such as
kernel global tables and object state where sleeping inside the critical section is not allowed.

The spinlock itself only holds the lock state. The interrupt-aware versions call platform-layer interfaces via
`mr_port_irq_disable()` and `mr_port_irq_enable()`, so the platform must provide nestable interrupt masking semantics.

## Core Data Structures and Macros

### Lock Type

```c
typedef mr_atomic_t mr_spinlock_t;
```

`mr_spinlock_t` is an atomic variable. A value of 0 indicates unlocked, and non-zero indicates locked. The current
implementation writes 1 when locking and writes back 0 when unlocking.

### Static Initialization

```c
#define MR_SPINLOCK_INIT() MR_ATOMIC_INIT(0)
```

`MR_SPINLOCK_INIT()` is used to statically define a spinlock in the unlocked state.

## Main Interface

### Initializing a Spinlock

```c
void mr_spinlock_init(mr_spinlock_t *lock);
```

`mr_spinlock_init()` sets the lock state to 0. This function does not check whether the lock is currently held; the
caller must ensure the lock is not re-initialized while held.

### Lock and Unlock

```c
void mr_spinlock_lock(mr_spinlock_t *lock);
void mr_spinlock_unlock(mr_spinlock_t *lock);
```

`mr_spinlock_lock()` loops attempting to change the lock state from 0 to 1 until the compare-and-swap succeeds. This
function does not yield the CPU and has no timeout logic.

`mr_spinlock_unlock()` writes the lock state back to 0. The current implementation does not check whether the caller
holds the lock.

### Lock with Interrupt Disable and Unlock with Interrupt Restore

```c
int mr_spinlock_lock_irqsave(mr_spinlock_t *lock);
void mr_spinlock_unlock_irqrestore(mr_spinlock_t *lock, int mask);
```

`mr_spinlock_lock_irqsave()` first calls `mr_port_irq_disable()` to obtain the current interrupt mask or nesting level,
then acquires the spinlock, and returns this mask.

`mr_spinlock_unlock_irqrestore()` first releases the spinlock, then calls `mr_port_irq_enable(mask)` to restore the
interrupt state. `mask` must come from a paired `mr_spinlock_lock_irqsave()` call.

### Query Lock State

```c
mr_bool_t mr_spinlock_is_locked(mr_spinlock_t *lock);
```

`mr_spinlock_is_locked()` reads the atomic lock value; returns `MR_TRUE` if non-zero, otherwise returns `MR_FALSE`. This
interface is only for observing transient state and cannot be used as a lock-free synchronization condition.

## Usage Example

```c
static mr_spinlock_t list_lock = MR_SPINLOCK_INIT();

void update_shared_state(void) {
    int mask;

    mask = mr_spinlock_lock_irqsave(&list_lock);
    /* Modify shared state; do not perform operations that may sleep inside the critical section. */
    mr_spinlock_unlock_irqrestore(&list_lock, mask);
}
```

The test in `test/kernel/spinlock.c` covers static initialization, basic lock/unlock, lock/unlock with interrupt save
and restore, and the state changes of `mr_spinlock_is_locked()`.

## Notes

- `mr_spinlock_lock()` is a busy-wait loop; critical sections should be kept very short.
- The current implementation is non-reentrant; locking the same lock again from the same execution context will spin
  waiting for itself to release.
- The interrupt-aware versions depend on the platform implementation of `mr_port_irq_disable()` and
  `mr_port_irq_enable()`, and require the platform to support nested restoration.
- The unlock interface does not verify ownership; an incorrect unlock will directly set the lock state to 0.
- The return value of `mr_spinlock_is_locked()` may become invalid immediately after reading; it cannot replace actual
  locking.

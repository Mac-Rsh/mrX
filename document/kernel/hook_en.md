[**中文**](kernel_hook.md) | [**English**](kernel_hook_en.md)

---

# Hook Management

The hook module provides fixed-capacity callback entry table management capabilities. The module is responsible for
initializing the hook table, appending entries to the table, and providing a traversal macro for callers to execute
callbacks in registration order. It is suitable for scenarios such as system ticks, work queues, and driver events that
need to maintain a set of callback functions.

This document corresponds to `include/mrX/kernel/mr_hook.h`, `kernel/hook.c`, and `test/kernel/hook.c`.

## Module Purpose

The `mr_hook` module only manages an array of entry pointers. It does not define the specific signature of callback
functions, nor does it actively invoke callbacks. The caller is responsible for preparing the storage array, specifying
the capacity, converting function or object entries to `mr_ptr_t` for registration, and converting them back to the
actual type during traversal.

`mr_hook_add()` uses an internal global spinlock to protect the append operation, preventing concurrent modification of
`next` and the entry array by multiple execution contexts. The traversal macro itself does not lock; whether
registration and traversal can occur concurrently is determined by the specific consuming module.

## Core Data Structures and Macros

### Hook Control Block

```c
typedef struct mr_hook {
    mr_ptr_t *entry;
    mr_uint16_t size;
    mr_uint16_t next;
} mr_hook_t;
```

Field meanings are as follows:

| Field   | Meaning                                                                  |
|---------|--------------------------------------------------------------------------|
| `entry` | Starting address of the entry array, array element type is `mr_ptr_t`.   |
| `size`  | Capacity of the entry array, in number of entries.                       |
| `next`  | Next writable position, also indicates the number of registered entries. |

### Static Initialization Macro

```c
#define MR_HOOK_INIT(_entry, _size)                                            \
    {.entry = (_entry), .size = (_size), .next = 0}
```

### Traversal Macro

```c
#define MR_HOOK_EACH(_i, _entry, _type, _hook)                                 \
    for ((_i) = 0, (_entry) = (_type *)(_hook)->entry[(_i)];                   \
         (_i) < (_hook)->next;                                                 \
         (_i)++, (_entry) = (_type *)(_hook)->entry[(_i)])
```

## Module Interfaces

### Initialize Hook

```c
MR_INLINE void mr_hook_init(mr_hook_t *hook, mr_ptr_t *entry,
                            mr_uint16_t size);
```

### Add Entry

```c
mr_err_t mr_hook_add(mr_hook_t *hook, mr_ptr_t entry);
```

`mr_hook_add()` appends the entry to the end of the hook table. The function acquires the global spinlock and saves the
interrupt state internally, writes the entry, then releases the lock and restores the interrupt state.

Return values are as follows:

| Return Value | Meaning                                      |
|--------------|----------------------------------------------|
| `0`          | Addition succeeded, `next` incremented by 1. |
| `-MR_ENOMEM` | `next >= size`, the entry table is full.     |

## Usage Example

```c
#include <mrX/kernel/mr_hook.h>

typedef void (*event_hook_t)(int event);

static mr_ptr_t event_entries[4];
static mr_hook_t event_hook = MR_HOOK_INIT(event_entries, 4);

static void on_event(int event) {
    (void)event;
}

void example_hook_register(void) {
    mr_hook_add(&event_hook, (mr_ptr_t)on_event);
}

void example_hook_emit(int event) {
    mr_uint16_t i;
    event_hook_t cb;

    MR_HOOK_EACH(i, cb, event_hook_t, &event_hook) {
        cb(event);
    }
}
```

## Notes

- The hook table capacity is fixed; the module does not automatically expand. When capacity is insufficient,
  `mr_hook_add()` returns `-MR_ENOMEM`.
- `mr_hook_init()` does not clear the contents of the entry array, only sets `next` to 0; old array contents will not be
  traversed.
- `mr_hook_add()` only protects the append process; `MR_HOOK_EACH()` does not hold a lock during traversal.
- `MR_HOOK_EACH()` reads `entry[0]` during the loop initialization phase, so even when `next == 0`, `entry` must point
  to a valid array, unless the caller avoids executing the traversal macro on an empty hook.
- The module stores `mr_ptr_t`; function pointer and data pointer conversion must be consistent with the target platform
  ABI.

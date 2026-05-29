[**中文**](kernel_wait.md) | [**English**](kernel_wait_en.md)

---

# Wait Event

The Wait module connects the event state of "waitable objects" with the callbacks of "waiters". `mr_waitable_t`
represents an object capable of producing events, and `mr_waiter_t` represents an object that waits for a specified
event and executes a callback when triggered. The module supports three event types: input, output, and error, along
with two waiting modes: edge-triggered and level-triggered.

Wait objects manage their lifecycle via `mr_object_t`, and a global spinlock protects the insertion, removal, and
traversal of the wait lists.

## Core Data Structures and Macros

### Event Flags

```c
#define MR_WAIT_IN   (1ULL << 60)
#define MR_WAIT_OUT  (1ULL << 61)
#define MR_WAIT_ERR  (1ULL << 62)
#define MR_WAIT_EDGE (1ULL << 63)
#define MR_WAIT_EVENT_MASK (~(MR_WAIT_IN | MR_WAIT_OUT | MR_WAIT_ERR | MR_WAIT_EDGE))
```

`MR_WAIT_IN`, `MR_WAIT_OUT`, and `MR_WAIT_ERR` indicate event types. `MR_WAIT_EDGE` indicates the waiter uses
edge-triggered mode; when not set, level-triggered mode is used. The lower bits can carry module-specific event data,
and `MR_WAIT_EVENT_MASK` can be used to extract this data.

### Waitable Object

```c
typedef struct mr_waitable {
    mr_object_t parent;
    mr_uint64_t last;
    mr_atomic_t gen;
    mr_list_t list;
    mr_ptr_t check;
    mr_ptr_t param;
} mr_waitable_t;
```

Field descriptions:

| Field    | Description                                                                            |
|----------|----------------------------------------------------------------------------------------|
| `parent` | Object control block.                                                                  |
| `last`   | The last processed event, used for computing edge events.                              |
| `gen`    | Check generation counter, used to discard stale results from concurrent nested checks. |
| `list`   | Head of the `mr_waiter_t` list waiting on this object.                                 |
| `check`  | Event check function, type `mr_waitable_check_t *`.                                    |
| `param`  | Parameter passed to the check function.                                                |

```c
typedef mr_uint64_t(mr_waitable_check_t)(mr_waitable_t *waitable, void *param);
```

The check function returns the current event set. If the check function is null, the internal check result is 0.

### Waiter

```c
typedef struct mr_waiter {
    mr_object_t parent;
    mr_uint64_t event;
    mr_list_t list;
    mr_ptr_t entry;
    mr_ptr_t param;
} mr_waiter_t;
```

Field descriptions:

| Field    | Description                                                    |
|----------|----------------------------------------------------------------|
| `parent` | Object control block.                                          |
| `event`  | The event type and trigger mode being waited on.               |
| `list`   | Wait list node; an empty list indicates not currently waiting. |
| `entry`  | Trigger callback, type `mr_waiter_entry_t *`.                  |
| `param`  | Parameter passed to the trigger callback.                      |

```c
typedef void(mr_waiter_entry_t)(mr_waiter_t *waiter, void *param,
                                mr_uint64_t event);
```

## Module Interface

### Waitable Object Initialization and Creation

```c
mr_err_t mr_waitable_init(mr_waitable_t *able,
                          mr_waitable_check_t *check, void *param);
mr_waitable_t *mr_waitable_create(mr_waitable_check_t *check, void *param);
```

`mr_waitable_init()` initializes a caller-provided static object. `mr_waitable_create()` creates a dynamic object from
the heap. `check` can be `MR_NULL`, but in that case `mr_waitable_complete()` will not produce events.

### Waitable Object Deletion and Reference Management

```c
mr_err_t mr_waitable_del(mr_waitable_t *able);
mr_waitable_t *mr_waitable_get(mr_waitable_t *able);
mr_bool_t mr_waitable_put(mr_waitable_t *able);
```

`mr_waitable_del()` cancels all waiters on this object, then releases the object reference. `mr_waitable_get()` and
`mr_waitable_put()` are reference-counting wrappers.

### Complete, Raise, and Cancel All

```c
mr_err_t mr_waitable_complete(mr_waitable_t *able);
mr_err_t mr_waitable_raise(mr_waitable_t *able, mr_uint64_t event);
mr_err_t mr_waitable_cancel_all(mr_waitable_t *able);
```

`mr_waitable_complete()` calls `check` to get the current events when waiters exist, and triggers matching waiters. If
the waiter list is empty, the function returns directly without calling `check`.

`mr_waitable_raise()` does not call `check`; instead it directly triggers waiters with the given `event`.

`mr_waitable_cancel_all()` removes all waiters from the wait list without invoking their callbacks.

### Waiter Initialization and Creation

```c
mr_err_t mr_waiter_init(mr_waiter_t *wait, mr_waiter_entry_t *entry,
                        void *param, mr_uint64_t event);
mr_waiter_t *mr_waiter_create(mr_waiter_entry_t *entry, void *param,
                              mr_uint64_t event);
```

`mr_waiter_init()` initializes a static waiter. `mr_waiter_create()` creates a dynamic waiter. Both require
`entry != MR_NULL`.

### Wait, Cancel, and Event Configuration

```c
mr_err_t mr_waiter_wait(mr_waiter_t *wait, mr_waitable_t *able);
mr_err_t mr_waiter_cancel(mr_waiter_t *wait);
mr_err_t mr_waiter_event_set(mr_waiter_t *wait, mr_uint64_t event);
mr_uint64_t mr_waiter_event(mr_waiter_t *wait);
#define MR_WAITER_IS_WAITING(_waiter) (!mr_list_is_empty(&(_waiter)->list))
```

`mr_waiter_wait()` adds the waiter to the waitable object's wait list. If the waiter is already waiting, returns
`-MR_EBUSY`.

A level-triggered waiter immediately performs a `check` after being added to the list; if the current event matches, the
waiter callback is called immediately. An edge-triggered waiter does not check historical state when added; it only
waits for subsequent event changes or explicit triggers.

`mr_waiter_cancel()` removes the waiter from the wait list. `mr_waiter_event_set()` first cancels the current wait, then
updates the wait event. `mr_waiter_event()` returns the current wait event.

## Usage Example

```c
#include <mrX/kernel/mr_wait.h>

static mr_uint64_t sample_check(mr_waitable_t *able, void *param)
{
    (void)able;
    return (*(int *)param != 0) ? MR_WAIT_IN : 0;
}

static void sample_waiter_entry(mr_waiter_t *waiter, void *param,
                                mr_uint64_t event)
{
    (void)waiter;
    (void)param;
    (void)event;
}

void sample_wait_usage(void)
{
    int ready = 0;
    mr_waitable_t able;
    mr_waiter_t waiter;

    mr_waitable_init(&able, sample_check, &ready);
    mr_waiter_init(&waiter, sample_waiter_entry, MR_NULL, MR_WAIT_IN);

    mr_waiter_wait(&waiter, &able);
    ready = 1;
    mr_waitable_complete(&able);

    mr_waiter_del(&waiter);
    mr_waitable_del(&able);
}
```

In this example, the waiter waits for `MR_WAIT_IN`. After `ready` is set to non-zero, `mr_waitable_complete()` retrieves
the event via `sample_check()` and triggers the callback.

## Notes

- The Wait module is only available when `MR_USE_WAIT` is enabled.
- Waiter callbacks execute after the internal wait lock is released, but modifying wait relationships within the
  callback still requires consideration of object lifecycles.
- Edge events are computed via `event & ~able->last`; level events use the current event directly.
- `mr_waitable_complete()` does not call the check function when there are no waiters.
- `mr_waiter_event_set()` cancels the current wait relationship; the caller needs to wait again.
- Deleting a waitable object does not delete the waiter objects themselves; it only removes the waiters from the wait
  list.

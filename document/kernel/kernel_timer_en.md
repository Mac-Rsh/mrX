[**中文**](kernel_timer.md) | [**English**](kernel_timer_en.md)

---

# Timer

The Timer module is used to execute a callback after the kernel clock tick reaches a specified time. Timer objects
manage their lifecycle via an embedded `mr_object_t`, are added to the global timer list as list nodes, and are
registered with the clock hook via `mr_kernel_timer_init()` for timeout checking.

Timers support both one-shot and periodic triggering. A one-shot timer does not automatically re-insert itself into the
timer list after firing; a periodic timer restarts itself according to the configured tick after the callback executes.

## Core Data Structures and Macros

### Timer Control Block

```c
typedef struct mr_timer {
    mr_object_t parent;
    mr_tick_t itick;
    mr_tick_t ttick;
    mr_list_t list;
    mr_ptr_t entry;
    mr_ptr_t param;
} mr_timer_t;
```

Field descriptions:

| Field    | Description                                                                            |
|----------|----------------------------------------------------------------------------------------|
| `parent` | Object control block, used for reference counting and static/dynamic release.          |
| `itick`  | Initial timer duration. The highest bit may carry `MR_TIMER_PERIODIC`.                 |
| `ttick`  | Absolute timeout tick, computed from the current tick at start time plus the duration. |
| `list`   | Node in the global timer list; an empty list indicates the timer is not running.       |
| `entry`  | Timeout callback, type `mr_timer_entry_t *`.                                           |
| `param`  | User parameter passed to the timeout callback.                                         |

### Periodic Flag

```c
#define MR_TIMER_PERIODIC (1U << 31)
```

`MR_TIMER_PERIODIC` marks a periodic timer. When computing the timeout, the module uses `itick & ~MR_TIMER_PERIODIC` to
obtain the actual tick count.

### Initialization Macro and Callback Type

```c
typedef void(mr_timer_entry_t)(mr_timer_t *timer, void *param);

#define MR_TIMER_INIT(_timer, _entry, _param, _tick) \
    MR_TIMER_DERIVE_INIT((_timer), MR_CLAZZES_STATIK(timer), (_entry), \
                         (_param), (_tick))
```

`MR_TIMER_DERIVE_INIT()` can be used for deriving timer subclasses, while `MR_TIMER_INIT()` initializes a normal timer
using the timer static class.

## Module Interface

### Initialization and Creation

```c
mr_err_t mr_timer_init(mr_timer_t *timer, mr_timer_entry_t *entry,
                       void *param, mr_tick_t tick);
mr_timer_t *mr_timer_create(mr_timer_entry_t *entry, void *param,
                            mr_tick_t tick);
```

`mr_timer_init()` initializes a caller-provided static timer object. `mr_timer_create()` allocates an `mr_timer_t` from
the heap and initializes it as a dynamic object. Both require `entry != MR_NULL` and `tick > 0`.

After successful initialization, the timer object has a reference count of 1, an empty list node, and has not yet
started timing.

### Deletion and Reference Management

```c
mr_err_t mr_timer_del(mr_timer_t *timer);
mr_timer_t *mr_timer_get(mr_timer_t *timer);
mr_bool_t mr_timer_put(mr_timer_t *timer);
```

`mr_timer_del()` first removes the timer from the timer list, then releases the object reference. A dynamically created
timer frees its memory when the last reference is released; a statically initialized timer only ends its object
lifecycle.

`mr_timer_get()` and `mr_timer_put()` are timer wrappers for the object reference-counting interface.

### Start, Stop, and State Check

```c
mr_err_t mr_timer_start(mr_timer_t *timer);
mr_err_t mr_timer_stop(mr_timer_t *timer);
#define MR_TIMER_IS_TIMING(_timer) (!mr_list_is_empty(&(_timer)->list))
```

`mr_timer_start()` computes the timeout based on the current clock tick and `itick`, then inserts the timer into the
global timer list. If the timer is already in the list, the start operation repositions it via list move semantics,
effectively restarting it.

`mr_timer_stop()` removes the timer from the timer list. `MR_TIMER_IS_TIMING()` checks whether the timer is currently
running by testing if the list node is empty.

### Setting and Reading Tick

```c
mr_err_t mr_timer_tick_set(mr_timer_t *timer, mr_tick_t tick);
mr_tick_t mr_timer_tick(mr_timer_t *timer);
```

`mr_timer_tick_set()` first removes the timer from the timer list, then updates `itick`. If the timer is currently
running, calling this function stops the current timing; the caller must call `mr_timer_start()` again.

`mr_timer_tick()` returns the current `itick`, including any `MR_TIMER_PERIODIC` flag that may be set.

### Kernel Timer Initialization

```c
void mr_kernel_timer_init(void);
```

`mr_kernel_timer_init()` registers the internal timeout check function with the clock hook. After the clock advances,
the timeout check retrieves expired timers, calls their callbacks after releasing the timer lock, and re-inserts
periodic timers into the timer list after the callback completes.

## Usage Example

```c
#include <mrX/kernel/mr_timer.h>

static void sample_timer_entry(mr_timer_t *timer, void *param)
{
    (void)timer;
    (void)param;
}

void sample_timer_usage(void)
{
    mr_timer_t timer;

    if (mr_timer_init(&timer, sample_timer_entry, MR_NULL,
                      10 | MR_TIMER_PERIODIC) != 0) {
        return;
    }

    mr_timer_start(&timer);
    mr_timer_stop(&timer);
    mr_timer_del(&timer);
}
```

This example creates a static periodic timer. After starting, the timer fires a callback every 10 ticks; it is stopped
and then the object is deleted.

## Notes

- The Timer module is only available when `MR_USE_TIMER` is enabled.
- `tick` must be greater than 0; the periodic flag occupies the highest bit, so the actual timer duration is obtained
  from `tick & ~MR_TIMER_PERIODIC`.
- Timeout callbacks execute after the internal timer lock is released; callbacks may use the timer API, but must still
  avoid object lifecycle races.
- `mr_timer_tick_set()` stops a running timer.
- Deleting a timer first stops it, then releases the object reference; after a dynamic timer is deleted, the original
  pointer must not be accessed.

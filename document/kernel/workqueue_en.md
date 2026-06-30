[**中文**](kernel_workqueue.md) | [**English**](kernel_workqueue_en.md)

---

# Work Queue

The Workqueue module is used to submit work items to a queue and execute work callbacks when the caller explicitly
drains the queue. The module supports immediate work and delayed work: immediate work is added to the run list, while
delayed work is first placed in a timer list sorted by timeout tick, and moved to the run list by an internal timer upon
expiration.

Both work queues and work items manage their lifecycle via an embedded `mr_object_t`. Internally, the queue uses a
spinlock to protect the linked lists, an atomic counter to track the pending execution depth, and optionally (when
`MR_USE_WORKQUEUE_HOOK` is enabled) a hook to notify the external execution context to wake up or suspend.

## Core Data Structures and Macros

### Work Queue Control Block

```c
typedef struct mr_workqueue {
    mr_object_t parent;
    mr_atomic_t depth;
    mr_atomic_t dying;
    mr_timer_t timer;
    mr_list_t tlist;
    mr_list_t list;
#if defined(MR_USE_WORKQUEUE_HOOK)
    const mr_workqueue_hook_t *hook;
#endif
} mr_workqueue_t;
```

Field descriptions:

| Field    | Description                                                                     |
|----------|---------------------------------------------------------------------------------|
| `parent` | Object control block.                                                           |
| `depth`  | Queue pending execution depth, used to control wakeup/suspend hook invocations. |
| `dying`  | Deletion flag; when set, new work submissions are rejected.                     |
| `timer`  | Internal timer, used to move delayed work to the run list.                      |
| `tlist`  | Delayed work linked list, sorted by expiration tick.                            |
| `list`   | Pending execution work linked list.                                             |
| `hook`   | Optional wakeup/suspend callback.                                               |

### Work Item Control Block

```c
typedef struct mr_work {
    mr_object_t parent;
    mr_list_t list;
    mr_ptr_t queue;
    mr_tick_t tick;
    mr_ptr_t entry;
    mr_ptr_t param;
} mr_work_t;
```

Field descriptions:

| Field    | Description                                                                        |
|----------|------------------------------------------------------------------------------------|
| `parent` | Object control block.                                                              |
| `list`   | Work linked list node; non-empty indicates the work is in the queue or delay list. |
| `queue`  | The work queue to which it is bound or was most recently submitted.                |
| `tick`   | Absolute tick for delayed work expiration.                                         |
| `entry`  | Work callback, type `mr_work_entry_t *`.                                           |
| `param`  | Parameter passed to the work callback.                                             |

```c
typedef void(mr_work_entry_t)(struct mr_work *work, void *param);
#define MR_WORK_IS_RUNNING(_work) (!mr_list_is_empty(&(_work)->list))
```

`MR_WORK_IS_RUNNING()` checks whether the work item is already in a queue list. It covers both the immediate pending and
delayed waiting states.

### Hook Type

```c
typedef struct mr_workqueue_hook {
    void (*suspend)(struct mr_workqueue *queue, void *param);
    void (*wakeup)(struct mr_workqueue *queue, void *param);
    void *param;
} mr_workqueue_hook_t;
```

The hook exists only when `MR_USE_WORKQUEUE_HOOK` is enabled. `wakeup` is called when work is first submitted to an
empty queue; `suspend` is called when the queue is drained and depth drops back.

## Module Interface

### Work Queue Initialization, Creation, and Deletion

```c
mr_err_t mr_workqueue_init(mr_workqueue_t *queue);
mr_workqueue_t *mr_workqueue_create(void);
mr_err_t mr_workqueue_del(mr_workqueue_t *queue);
```

`mr_workqueue_init()` initializes a static queue and its internal timer. `mr_workqueue_create()` allocates a dynamic
queue from the heap. `mr_workqueue_del()` marks the queue as dying, releases the object reference; the release path
deletes the internal timer and clears both the immediate and delayed work lists.

### Draining the Queue

```c
mr_err_t mr_workqueue_execute(mr_workqueue_t *queue);
```

`mr_workqueue_execute()` swaps the current pending list into a local operation list, then removes each work item one by
one and calls the work callback after releasing the internal lock. After execution, it invokes the internal suspend
logic.

This function does not automatically loop waiting for new work. An external scheduler typically calls it after receiving
a hook wakeup.

### Submitting Work to a Specific Queue

```c
mr_err_t mr_workqueue_work(mr_workqueue_t *queue, mr_work_t *work);
mr_err_t mr_workqueue_delayed_work(mr_workqueue_t *queue, mr_work_t *work,
                                   mr_tick_t tick);
```

`mr_workqueue_work()` adds the work to the queue's run list and wakes up the queue. Returns `-MR_ENOENT` if the queue is
being deleted; returns `-MR_EBUSY` if the work is already in a list.

`mr_workqueue_delayed_work()` adds the work to the delayed list. When `tick == 0`, it degrades to an immediate
submission. A non-zero delay requires `tick < MR_TICK_MAX / 2`. If the new work is the earliest to expire, the internal
timer is set to the optimal tick and started.

### Setting Hook and Reference Management

```c
mr_err_t mr_workqueue_hook_set(mr_workqueue_t *queue,
                               const mr_workqueue_hook_t *hook);
mr_workqueue_t *mr_workqueue_get(mr_workqueue_t *queue);
mr_bool_t mr_workqueue_put(mr_workqueue_t *queue);
```

`mr_workqueue_hook_set()` saves the hook pointer; the hook must be non-null. The caller must ensure the hook object
remains valid while the queue is in use.

`mr_workqueue_get()` and `mr_workqueue_put()` are reference-counting wrappers for work queues.

### Work Item Initialization, Creation, and Deletion

```c
mr_err_t mr_work_init(mr_work_t *work, mr_work_entry_t *entry, void *param);
mr_work_t *mr_work_create(mr_work_entry_t *entry, void *param);
mr_err_t mr_work_del(mr_work_t *work);
```

`mr_work_init()` initializes a static work item. `mr_work_create()` creates a dynamic work item. Both require
`entry != MR_NULL`. `mr_work_del()` first cancels the work, then releases the object reference.

### Binding, Scheduling, and Canceling Work

```c
mr_err_t mr_work_bind(mr_work_t *work, mr_workqueue_t *queue);
mr_err_t mr_work_schedule(mr_work_t *work);
mr_err_t mr_work_delayed_schedule(mr_work_t *work, mr_tick_t tick);
mr_err_t mr_work_cancel(mr_work_t *work);
mr_work_t *mr_work_get(mr_work_t *work);
mr_bool_t mr_work_put(mr_work_t *work);
```

`mr_work_bind()` binds a work item to a work queue. `mr_work_schedule()` and `mr_work_delayed_schedule()` submit work
using the queue pointed to by `work->queue`; returns `-MR_ENOENT` if the work is not yet bound or if the queue's
lifecycle has ended.

`mr_work_cancel()` removes the work item from its current list. `mr_work_get()` and `mr_work_put()` are
reference-counting wrappers for work items.

## Usage Example

```c
#include <mrX/kernel/mr_work.h>

static void sample_work_entry(struct mr_work *work, void *param)
{
    (void)work;
    (void)param;
}

void sample_workqueue_usage(void)
{
    mr_workqueue_t queue;
    mr_work_t work;

    mr_workqueue_init(&queue);
    mr_work_init(&work, sample_work_entry, MR_NULL);
    mr_work_bind(&work, &queue);

    mr_work_schedule(&work);
    mr_workqueue_execute(&queue);

    mr_work_del(&work);
    mr_workqueue_del(&queue);
}
```

This example creates a static work queue and a static work item. After binding the work item to the queue, it is
scheduled and then the callback is executed via `mr_workqueue_execute()`.

## Notes

- The Workqueue module is only available when `MR_USE_WORK` is enabled, and depends on the Timer module for
  handling delayed work.
- `mr_work_schedule()` and `mr_work_delayed_schedule()` require that the work item has a valid queue set via
  `mr_work_bind()` or a prior submission.
- The same work item cannot be resubmitted while `MR_WORK_IS_RUNNING()` returns true.
- Deleting a queue clears the pending and delayed lists, but does not delete the work item objects themselves.
- Work callbacks execute after the internal lock is released; they may resubmit work, but must avoid object deletion and
  concurrent access to the same object.
- `mr_workqueue_hook_set()` does not copy the hook content; the hook pointer must remain valid.

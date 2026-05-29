[**中文**](kernel_irq.md) | [**English**](kernel_irq_en.md)

---

# Interrupt Management

The interrupt management module is compiled when `MR_USE_IRQ` is enabled. It is responsible for registering interrupt
controller objects, maintaining the interrupt descriptor table, binding interrupt handler functions, and dispatching
handlers when interrupts trigger. The module abstracts interrupt control operations provided by the platform or driver
through `mr_irq_ops_t`, and protects the descriptor table with a global spinlock.

This module supports shared interrupts and optional deferred interrupt handling. Deferred handling capability is
controlled by `MR_USE_IRQ_DEFER`, and the deferred handling wakeup/suspend callback is controlled by
`MR_USE_IRQ_DEFER_HOOK`.

## Core Data Structures and Macros

### Interrupt Trigger Flags

```c
#define MR_IRQ_EDGE_RISING  (0x01U)
#define MR_IRQ_EDGE_FALLING (0x02U)
#define MR_IRQ_EDGE_BOTH    (0x03U)
#define MR_IRQ_LEVEL_HIGH   (0x04U)
#define MR_IRQ_LEVEL_LOW    (0x08U)
#define MR_IRQ_SHARED       (0x10U)
```

The lower 4 bits represent the trigger type, extracted internally using `IRQ_FLAG_TYPE_MASK`; `MR_IRQ_SHARED` indicates
that multiple actions are allowed to share the same IRQ. Shared interrupts require that subsequently registered flags
exactly match the existing flags.

### Interrupt Return Values

```c
typedef enum mr_irq_return {
    MR_IRQ_NONE,
    MR_IRQ_HANDLED,
    MR_IRQ_DEFERRED,
} mr_irq_return_t;
```

`MR_IRQ_NONE` indicates the current handler did not process the interrupt, and the dispatch logic continues to try
subsequent actions. `MR_IRQ_HANDLED` indicates the interrupt has been processed and dispatch stops. `MR_IRQ_DEFERRED`
indicates deferred processing is needed; when deferred processing is not enabled or the action has no deferred entry
configured, the current implementation treats it as handled.

### Interrupt Control Operations

```c
typedef struct mr_irq_ops {
    void (*priority_set)(mr_uint32_t irq, mr_uint8_t priority, void *priv);
    void (*type_set)(mr_uint32_t irq, mr_uint8_t type, void *priv);
    void (*enable)(mr_uint32_t irq, void *priv);
    void (*unmask)(mr_uint32_t irq, void *priv);
    void (*disable)(mr_uint32_t irq, void *priv);
    void (*mask)(mr_uint32_t irq, void *priv);
} mr_irq_ops_t;
```

`mr_irq_ops_t` is implemented by the specific platform or interrupt controller driver. The module sets the trigger type
and default priority on the first request, and calls enable, disable, mask, and unmask in the enable/disable and handle
paths.

### Interrupt Object

```c
typedef struct mr_irq {
    mr_object_t parent;
    mr_atomic_t dying;
    mr_uint32_t istart;
    mr_uint32_t iend;
    const mr_irq_ops_t *ops;
    mr_ptr_t priv;
} mr_irq_t;
```

`parent` provides object lifecycle management. `dying` prevents a being-deleted object from being used by request, free,
enable, disable, priority_set, or handle. `istart` and `iend` describe the IRQ number range managed by this interrupt
controller. `ops` and `priv` hold the underlying controller operations and private data.

### Action and Descriptor

```c
typedef struct mr_irq_action {
    struct mr_irq_action *next;
    mr_uint32_t pending: 1;
    mr_uint32_t irq: 31;
    mr_ptr_t entry;
    mr_ptr_t owner;
#if defined(MR_USE_IRQ_DEFER)
    mr_work_t defer;
    mr_ptr_t defer_entry;
#endif
} mr_irq_action_t;

typedef struct mr_irq_desc {
    mr_irq_action_t *action;
    mr_uint16_t priority;
    mr_uint16_t flags;
    mr_atomic_t depth;
    mr_irq_t *irq;
} mr_irq_desc_t;
```

Each IRQ number corresponds to one descriptor. `action` is the handler function linked list, `priority` and `flags`
store the current configuration, `depth` is the disable depth, and `irq` points to the parent interrupt object.
`pending` indicates the action is being processed; if `mr_irq_free()` is called at this time, the implementation first
clears `owner`, and the action is freed after the handler path returns.

The global descriptor table size is controlled by `MR_CFG_IRQ_TABLE_SIZE`, with a default value of 32.

### Platform Interrupt Interface

```c
extern int mr_port_irq_disable(void);
extern void mr_port_irq_enable(int mask);
```

The platform layer interface is located in `include/mrX/port/mr_port_irq.h`, used by the irqsave/irqrestore paths of
spinlocks.

## Main Interfaces

### Initialize and Create Interrupt Object

```c
mr_err_t mr_irq_init(mr_irq_t *irq, mr_uint32_t irq_start, mr_uint32_t irq_end,
                     const mr_irq_ops_t *ops, void *priv);
mr_irq_t *mr_irq_create(mr_uint32_t irq_start, mr_uint32_t irq_end,
                        const mr_irq_ops_t *ops, void *priv);
```

`mr_irq_init()` initializes a static interrupt object, `mr_irq_create()` allocates and initializes a dynamic interrupt
object. Both insert the `[irq_start, irq_end]` range into the global descriptor table, initialize each descriptor's
disable depth to 1, and save `ops` and `priv`.

### Delete and Reference Management

```c
mr_err_t mr_irq_del(mr_irq_t *irq);
mr_irq_t *mr_irq_get(mr_irq_t *irq);
mr_bool_t mr_irq_put(mr_irq_t *irq);
```

`mr_irq_del()` marks the object as dying and releases the object reference. When the last reference is released, the
module removes the range from the descriptor table, frees all actions, and disables and masks the corresponding IRQs.

### Request and Free Interrupt

```c
mr_err_t mr_irq_request(mr_uint32_t irq, mr_irq_entry_t *entry,
                        void *owner, mr_uint16_t flags);
mr_err_t mr_irq_free(mr_uint32_t irq, void *owner);
```

`mr_irq_request()` adds an action for the specified IRQ. The first action sets the trigger type, default priority 0, and
descriptor flags, and decreases the disable depth from 1 to 0, thereby calling `unmask()` and `enable()`. Subsequent
actions are only allowed if the existing flags contain `MR_IRQ_SHARED` and the new flags exactly match the old flags.

`mr_irq_free()` deletes the action by owner. After deleting the last action, the disable depth is increased, and when
the depth transitions from 0 to 1, `disable()` and `mask()` are called. If the action is currently being processed, the
function clears the owner and returns, with the actual release completed by the handle path.

### Enable, Disable, and Query Configuration

```c
mr_err_t mr_irq_enable(mr_uint32_t irq);
mr_err_t mr_irq_disable(mr_uint32_t irq);
int mr_irq_type(mr_uint32_t irq);
mr_err_t mr_irq_priority_set(mr_uint32_t irq, mr_uint8_t priority);
int mr_irq_priority(mr_uint32_t irq);
```

### Dispatch Interrupt

```c
mr_irq_return_t mr_irq_handle(mr_uint32_t irq);
```

`mr_irq_handle()` looks up the descriptor under global lock protection, obtains the interrupt object reference, and
first calls the underlying `mask()`. It then invokes handler functions in action linked list order; the global lock is
released before calling each handler function and re-acquired after the handler returns.

### Deferred Interrupt Handling

```c
mr_err_t mr_irq_defer_request(mr_uint32_t irq, mr_irq_entry_t *entry,
                              void *owner, mr_uint16_t flags,
                              mr_irq_entry_t *defer_entry);
void mr_irq_defer_execute(void);
mr_err_t mr_irq_defer_hook_set(const mr_irq_defer_hook_t *hook);
```

## Usage Example

```c
static void hw_priority_set(mr_uint32_t irq, mr_uint8_t priority, void *priv) {}
static void hw_type_set(mr_uint32_t irq, mr_uint8_t type, void *priv) {}
static void hw_enable(mr_uint32_t irq, void *priv) {}
static void hw_unmask(mr_uint32_t irq, void *priv) {}
static void hw_disable(mr_uint32_t irq, void *priv) {}
static void hw_mask(mr_uint32_t irq, void *priv) {}

static const mr_irq_ops_t ops = {
    .priority_set = hw_priority_set,
    .type_set = hw_type_set,
    .enable = hw_enable,
    .unmask = hw_unmask,
    .disable = hw_disable,
    .mask = hw_mask,
};

static mr_irq_return_t button_irq(mr_uint32_t irq, void *owner) {
    return MR_IRQ_HANDLED;
}

void example(void) {
    static mr_irq_t irqctl;

    mr_irq_init(&irqctl, 0, 3, &ops, MR_NULL);
    mr_irq_request(0, button_irq, &irqctl, MR_IRQ_EDGE_RISING);

    /* platform interrupt entry calls this */
    mr_irq_handle(0);

    mr_irq_free(0, &irqctl);
    mr_irq_del(&irqctl);
}
```

## Notes

- This module is only available when `MR_USE_IRQ` is defined.
- `mr_irq_init()` and `mr_irq_create()` require that IRQ ranges do not overlap, and the range must be less than
  `MR_CFG_IRQ_TABLE_SIZE`.
- When requesting, `owner` is used both for release matching and for duplicate registration detection; the same owner
  cannot register twice for the same IRQ.
- Shared interrupts must set `MR_IRQ_SHARED` starting from the first action, otherwise subsequent shared registrations
  will return busy.
- Interrupt handler functions execute after the global lock is released, but the action is marked as pending; calling
  `mr_irq_free()` inside a handler function is a tested scenario.
- Deleting an interrupt object cleans up all actions within its IRQ range; deferred actions pending execution are
  skipped because their owner is cleared.
- All function pointers in the underlying `mr_irq_ops_t` are called directly in the current implementation; drivers must
  provide a complete implementation.

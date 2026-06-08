[**中文**](drivers_driver.md) | [**English**](drivers_driver_en.md)

---

# Driver Framework

The Driver module provides the common driver object model in mrX. A driver manages its lifecycle through an embedded `mr_object_t`, uses `mr_driver_clazz_t` to describe setup and shutdown methods, and stores concrete driver data, private context, and low-level operations in `data`, `priv`, and `ops`.

Drivers can be statically initialized, dynamically created, or installed into linker sections for automatic setup during system startup. The module is only available when `MR_USE_DRIVER` is enabled.

## Core Data Structures and Macros

### Driver Control Block

```c
typedef struct mr_driver {
    mr_object_t parent;
    const mr_driver_clazz_t *clazz;
    mr_atomic_t flags;
    mr_uint16_t type;
    mr_int16_t state;
    mr_ptr_t data;
    mr_ptr_t priv;
    mr_ptr_t ops;
} mr_driver_t;
```

Field descriptions:

| Field | Description |
|-------|-------------|
| `parent` | Object control block for reference counting and static/dynamic release. |
| `clazz` | Driver setup and shutdown method table. |
| `flags` | Driver state flags. |
| `type` | Driver major type. |
| `state` | Status code from the most recent setup or shutdown. |
| `data` | Driver data, usually pointing to a module-specific data structure. |
| `priv` | Platform or device private data. |
| `ops` | Low-level operation table. |

### Driver Class

```c
typedef mr_err_t(mr_driver_method_t)(const struct mr_driver *drv);

typedef struct mr_driver_clazz {
    const char *name;
    mr_driver_method_t *init;
    mr_driver_method_t *deinit;
} mr_driver_clazz_t;
```

`init` is called by `mr_driver_setup()`, and `deinit` is called by `mr_driver_shutdown()` or during driver release. If a method is `MR_NULL`, the framework treats that step as successful.

### Flags, Types, and Init Levels

```c
#define MR_DRIVER_FLAG_SETUP  (1U << 0)
#define MR_DRIVER_FLAG_LOCK   (1U << 1)
#define MR_DRIVER_FLAG_DYING  (1U << 2)

#define MR_DRIVER_LEVEL_EARLY   "1"
#define MR_DRIVER_LEVEL_PRIO(p) "2." p
#define MR_DRIVER_LEVEL_NORMAL  "3"
#define MR_DRIVER_LEVEL_LAST    "4"
#define MR_DRIVER_LEVEL_DEFER   "9"
```

`MR_DRIVER_FLAG_SETUP` means the driver has been set up. `MR_DRIVER_FLAG_LOCK` prevents setup/shutdown reentry. `MR_DRIVER_FLAG_DYING` means the driver is being deleted, and later setup/shutdown calls fail.

Driver types are represented by `mr_driver_type_t`. Current major types include RAM, GPIO, UART, I2C, ADC, DAC, and DMA. The major type mask is `MR_DRIVER_TYPE_MASK`.

## Module Interface

### Initialization, Creation, and Deletion

```c
mr_err_t mr_driver_init(mr_driver_t *drv, const mr_driver_clazz_t *dclazz,
                        mr_uint16_t type, void *data, void *priv, void *ops);
mr_driver_t *mr_driver_create(const mr_driver_clazz_t *dclazz,
                              mr_uint16_t type, void *data, void *priv,
                              void *ops);
mr_err_t mr_driver_del(mr_driver_t *drv);
```

`mr_driver_init()` initializes a caller-provided static driver object. `mr_driver_create()` allocates a driver object from the heap and initializes it as a dynamic object. A dynamic object frees the `mr_driver_t` storage when the last reference is released.

`mr_driver_del()` marks the driver as dying and releases the object reference. If the driver is still set up when the object is released, the framework calls `deinit`.

### Setup, Shutdown, and State Query

```c
mr_err_t mr_driver_setup(mr_driver_t *drv);
mr_err_t mr_driver_shutdown(mr_driver_t *drv);
mr_bool_t mr_driver_is_ready(mr_driver_t *drv);
mr_int16_t mr_driver_state(mr_driver_t *drv);
```

`mr_driver_setup()` calls the driver's `init` method. It is idempotent: calling it on an already set-up driver returns 0 without calling `init` again. `mr_driver_shutdown()` calls `deinit`; calling it on a driver that is not set up returns 0.

`mr_driver_is_ready()` returns `MR_TRUE` only when the driver object is valid, set up, and `state == 0`.

### Lookup and Reference Management

```c
mr_driver_t *mr_driver_find(const char *name);
mr_driver_t *mr_driver_get(mr_driver_t *drv);
mr_bool_t mr_driver_put(mr_driver_t *drv);
```

`mr_driver_find()` only finds drivers exported to the driver linker section through `MR_DRIVER_INSTALL()`. `mr_driver_get()` and `mr_driver_put()` are driver wrappers for object reference counting.

### Access Macros

```c
MR_DRIVER_TYPE(drv)
MR_DRIVER_IS_TYPE(drv, type)
MR_DRIVER_DATA(drv)
MR_DRIVER_PRIV(drv)
MR_DRIVER_OPS(drv)
MR_DRIVER_IS_SETUP(drv)
MR_DRIVER_ASSERT(drv, major_type)
```

`MR_DRIVER_ASSERT()` checks that the driver is non-null, initialized as an object, belongs to the driver class, has been set up, and matches the requested major type. Therefore, concrete driver APIs usually require the driver to be set up before being called.

## Driver Installation and Startup Flow

```c
#define MR_DRIVER_INSTALL(_name, _dclazz, _type, _data, _priv, _ops, _level)
```

`MR_DRIVER_INSTALL()` defines a driver object and a driver entry, then places the entry in the `mr_driver.<level>` linker section. During system startup, `mr_drivers_init()` walks entries between `start` and `defer` and calls `mr_driver_setup()` for each driver.

Drivers installed at `MR_DRIVER_LEVEL_DEFER` are placed in the deferred region and are not set up automatically during startup. Callers must set them up on demand.

## HardTree Support

`mr_hardtree.h` provides compile-time hardware tree access macros based on `mr_hardgen.h`. Common driver-level macros are:

```c
MR_HT_HAS_DRIVER()
MR_HT_DRIVER_NUM()
MR_HT_DRIVER(id)
MR_HT_DRIVER_FOREACH(fn, ...)
MR_HT_DRIVER_INSTALL(id, dclazz, type, data, priv, ops)
MR_HT_DRIVER_FIND(node)
```

`MR_HT_DRIVER_INSTALL()` reads the `INIT_LEVEL` property from the hardware tree node. If the property does not exist, it uses `MR_DRIVER_LEVEL_DEFER` by default.

## Usage Example

```c
static mr_err_t sample_driver_init(const mr_driver_t *drv)
{
    (void)drv;
    return 0;
}

static mr_err_t sample_driver_deinit(const mr_driver_t *drv)
{
    (void)drv;
    return 0;
}

static const mr_driver_clazz_t sample_driver_clazz =
    MR_DRIVER_CLAZZ_INIT("sample", sample_driver_init, sample_driver_deinit);

void sample_driver_usage(void)
{
    mr_driver_t *drv;

    drv = mr_driver_create(&sample_driver_clazz, MR_DRIVER_TYPE_RAM,
                           MR_NULL, MR_NULL, MR_NULL);
    mr_driver_setup(drv);

    if (mr_driver_is_ready(drv)) {
        /* Use driver data, private data, or operations here. */
    }

    mr_driver_shutdown(drv);
    mr_driver_del(drv);
}
```

This example dynamically creates a driver object, sets it up, uses it, shuts it down, and deletes it.

## Notes

- `mr_driver_init()` is for static objects; the object must not already be initialized.
- `mr_driver_create()` returns a dynamic object. The original pointer must not be accessed after deletion.
- `mr_driver_setup()` and `mr_driver_shutdown()` use an internal lock to prevent reentry; reentry returns `-MR_EBUSY`.
- After a driver enters the dying state, setup/shutdown returns `-MR_ENOENT`.
- `mr_driver_find()` depends on the linker script keeping and sorting the `mr_driver.*` sections.
- A HardTree driver without an explicit init level is not set up automatically during startup.

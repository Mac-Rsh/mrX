[**中文**](drivers_ram.md) | [**English**](drivers_ram_en.md)

---

# RAM Driver

The RAM driver provides a memory device abstraction for internal or external RAM such as SRAM, PSRAM, and SDRAM. It supports multiple memory regions, mount/unmount lifecycle operations, and optional read/write operations.

The module is only available when `MR_USE_DRIVER_RAM` is enabled. The RAM heaps extension is controlled by `MR_USE_DRIVER_RAM_HEAPS` and depends on `MR_USE_MALLOC`.

## Core Data Structures and Macros

### RAM Region and Data

```c
typedef struct mr_ram_region {
    void *address;
    mr_uint32_t size;
} mr_ram_region_t;

typedef struct mr_ram_data {
    mr_ram_region_t *regions;
    mr_uint32_t regions_num;
} mr_ram_data_t;
```

`regions` points to the region array, and `regions_num` is the number of regions. Each region describes a RAM address and size.

### RAM Operations

```c
typedef struct mr_ram_ops {
    mr_err_t (*mount)(const mr_driver_t *drv);
    mr_err_t (*unmount)(const mr_driver_t *drv);
    mr_ssize_t (*write)(const mr_driver_t *drv, mr_ssize_t offset,
                        const mr_uint8_t *buf, mr_size_t size);
    mr_ssize_t (*read)(const mr_driver_t *drv, mr_ssize_t offset,
                       mr_uint8_t *buf, mr_size_t size);
} mr_ram_ops_t;
```

`mount` and `unmount` are lifecycle operations. `write` and `read` are optional I/O operations. If a read/write operation is not implemented, the generic interface returns `-MR_ENOSYS`.

### Definition and Installation Macros

```c
#define MR_RAM_DATA_INIT(_regions, _regions_num)
#define MR_RAM_DEFINE(_name, _data, _priv, _ops)
#define MR_RAM_INSTALL(_name, _data, _priv, _ops, _level)
#define MR_HT_RAM_INSTALL(_id, _priv, _ops)
```

`MR_RAM_DEFINE()` defines a static RAM driver. `MR_RAM_INSTALL()` installs a RAM driver at the specified init level. `MR_HT_RAM_INSTALL()` generates RAM driver and region data from a HardTree node.

## Module Interface

### Lifecycle

```c
mr_err_t mr_ram_init(mr_driver_t *drv, mr_ram_data_t *data,
                     void *priv, mr_ram_ops_t *ops);
mr_driver_t *mr_ram_create(mr_ram_data_t *data, void *priv,
                           mr_ram_ops_t *ops);
mr_err_t mr_ram_del(mr_driver_t *drv);
mr_driver_t *mr_ram_get(mr_driver_t *drv);
mr_bool_t mr_ram_put(mr_driver_t *drv);
```

`mr_ram_init()` and `mr_ram_create()` wrap the generic driver lifecycle for RAM. During RAM setup, the framework calls low-level `mount` if it exists. During RAM shutdown or release, the framework calls `unmount` if it exists.

Dynamic RAM deletion frees the data pointed to by `MR_DRIVER_DATA(drv)`.

### Read and Write Interface

```c
mr_ssize_t mr_ram_write(mr_driver_t *drv, mr_ssize_t offset,
                        const void *buf, mr_size_t size);
mr_ssize_t mr_ram_read(mr_driver_t *drv, mr_ssize_t offset,
                       void *buf, mr_size_t size);
```

`mr_ram_write()` and `mr_ram_read()` directly call low-level `ops->write` and `ops->read`. If the corresponding operation is not implemented, they return `-MR_ENOSYS`.

## HardTree RAM Region

RAM region property cells use this format:

```text
[address, size]
```

`MR_HT_RAM_INSTALL()` generates an `mr_ram_region_t` array and an `mr_ram_data_t` from node properties, then installs the RAM driver through `MR_HT_DRIVER_INSTALL()`.

## RAM Heaps Extension

The RAM heaps extension registers RAM regions into the system heap. Its `mount` operation walks `mr_ram_data_t.regions` and adds each region to the heap through `mr_heap_region_add()`.

This extension does not provide RAM read/write operations. It only provides mount/unmount lifecycle operations. Its current unmount returns `-MR_EIO`, meaning RAM that has been added to the heap cannot be normally unmounted.

## Usage Example

```c
static mr_ram_region_t sample_regions[] = {
    { .address = (void *)0x20000000, .size = 4096 },
};

static mr_ram_data_t sample_ram_data =
    MR_RAM_DATA_INIT(sample_regions, MR_ARRAY_SIZE(sample_regions));

void sample_ram_usage(mr_driver_t *ram_drv)
{
    mr_uint8_t buf[16];

    mr_ram_write(ram_drv, 0, buf, sizeof(buf));
    mr_ram_read(ram_drv, 0, buf, sizeof(buf));
}
```

This example defines a RAM region data object and shows how to call RAM read/write APIs.

## Notes

- The generic layer does not check whether `offset` and `size` exceed region boundaries. The low-level driver must handle this.
- `mr_ram_write()` and `mr_ram_read()` depend on low-level ops. If not implemented, they return `-MR_ENOSYS`.
- `MR_USE_DRIVER_RAM_HEAPS` depends on `MR_USE_DRIVER_RAM` and `MR_USE_MALLOC`.
- The RAM heaps extension only adds RAM to the system heap and does not provide read/write operations.
- The RAM heaps extension returns `-MR_EIO` from unmount and cannot normally unmount RAM that has been added to the heap.

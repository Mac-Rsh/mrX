[**中文**](drivers_ram.md) | [**English**](drivers_ram_en.md)

---

# RAM 驱动

RAM 驱动提供内存设备抽象，用于描述 SRAM、PSRAM、SDRAM 等内部或外部 RAM。模块支持多个内存 region、mount/unmount 生命周期，以及可选的 read/write 操作。

模块只有在 `MR_USE_DRIVER_RAM` 启用时可用。RAM heaps 扩展由 `MR_USE_DRIVER_RAM_HEAPS` 控制，并依赖 `MR_USE_MALLOC`。

## 核心数据结构和宏

### RAM region 和数据

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

`regions` 指向 region 数组，`regions_num` 表示 region 数量。每个 region 描述一段可用 RAM 地址和长度。

### RAM 操作集

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

`mount` 和 `unmount` 是生命周期操作，`write` 和 `read` 是可选读写操作。未实现读写操作时，通用接口返回 `-MR_ENOSYS`。

### 定义和安装宏

```c
#define MR_RAM_DATA_INIT(_regions, _regions_num)
#define MR_RAM_DEFINE(_name, _data, _priv, _ops)
#define MR_RAM_INSTALL(_name, _data, _priv, _ops, _level)
#define MR_HT_RAM_INSTALL(_id, _priv, _ops)
```

`MR_RAM_DEFINE()` 定义静态 RAM driver；`MR_RAM_INSTALL()` 将 RAM driver 安装到指定初始化级别；`MR_HT_RAM_INSTALL()` 从 HardTree 节点生成 RAM driver 和 region 数据。

## 模块接口

### 生命周期

```c
mr_err_t mr_ram_init(mr_driver_t *drv, mr_ram_data_t *data,
                     void *priv, mr_ram_ops_t *ops);
mr_driver_t *mr_ram_create(mr_ram_data_t *data, void *priv,
                           mr_ram_ops_t *ops);
mr_err_t mr_ram_del(mr_driver_t *drv);
mr_driver_t *mr_ram_get(mr_driver_t *drv);
mr_bool_t mr_ram_put(mr_driver_t *drv);
```

`mr_ram_init()` 和 `mr_ram_create()` 是通用 driver 生命周期接口的 RAM 封装。RAM setup 时，如果底层提供 `mount`，框架会调用它。RAM shutdown 或释放时，如果底层提供 `unmount`，框架会调用它。

动态 RAM 删除时会释放 `MR_DRIVER_DATA(drv)` 指向的数据。

### 读写接口

```c
mr_ssize_t mr_ram_write(mr_driver_t *drv, mr_ssize_t offset,
                        const void *buf, mr_size_t size);
mr_ssize_t mr_ram_read(mr_driver_t *drv, mr_ssize_t offset,
                       void *buf, mr_size_t size);
```

`mr_ram_write()` 和 `mr_ram_read()` 直接调用底层 `ops->write` 和 `ops->read`。若对应操作未实现，返回 `-MR_ENOSYS`。

## HardTree RAM region

RAM region 属性单元格式为：

```text
[address, size]
```

`MR_HT_RAM_INSTALL()` 会根据节点属性生成 `mr_ram_region_t` 数组和 `mr_ram_data_t`，再通过 `MR_HT_DRIVER_INSTALL()` 安装 RAM driver。

## RAM heaps 扩展

RAM heaps 扩展将 RAM region 注册到系统 heap。它的 `mount` 会遍历 `mr_ram_data_t.regions`，并通过 `mr_heap_region_add()` 将每段 region 加入堆。

该扩展不提供 RAM read/write，只提供 mount/unmount 生命周期操作。当前 unmount 返回 `-MR_EIO`，表示已加入 heap 的 RAM 不能正常卸载。

## 使用示例

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

该示例定义一个 RAM region 数据对象，并展示 RAM 读写接口的调用方式。

## 注意事项

- 通用层不检查 `offset` 和 `size` 是否越过 region 边界，底层驱动需要自行处理。
- `mr_ram_write()` 和 `mr_ram_read()` 依赖底层 ops 实现，未实现时返回 `-MR_ENOSYS`。
- `MR_USE_DRIVER_RAM_HEAPS` 依赖 `MR_USE_DRIVER_RAM` 和 `MR_USE_MALLOC`。
- RAM heaps 扩展只用于把 RAM 加入系统 heap，不提供读写接口。
- RAM heaps 扩展的 unmount 返回 `-MR_EIO`，不能正常卸载已加入 heap 的 RAM。

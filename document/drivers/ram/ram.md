# RAM 设备（RAM）

> RAM 驱动统一描述 CPU 可直接映射的 SRAM/SDRAM/PSRAM，以及只能通过控制器
> 间接访问的 RAM。设备几何在 setup 阶段通过 `probe` 获取；直接映射区域通过
> `region` 查询，间接 RAM 可以没有任何直接映射区域。

| 项       | 说明                                                                                 |
|----------|--------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_DRIVER_RAM`（默认关闭）                                                      |
| 头文件   | `<mrX/drivers/ram/mr_ram.h>`                                                         |
| 依赖模块 | [驱动框架 Driver](../driver.md)；[动态内存 Malloc](../../libc/malloc.md)（动态创建） |
| 参考测试 | `mrX/test/drivers/ram/ram.c`                                                         |

---

## 用户指南

### 1. 模块概述

RAM 驱动把“RAM 是什么”与“RAM 怎么连接”分开。公共层只描述容量、CPU 可直接 寻址区域以及按偏移读写能力；具体设备可以是片内
SRAM、FMC/EMC SDRAM、memory- mapped PSRAM，也可以是 SPI/QSPI 等控制器后面的间接访问 RAM。

两类设备都使用同一个模型：

```text
CPU direct mapped RAM             Indirect RAM
SRAM / SDRAM / PSRAM              SPI/QSPI PSRAM
        |                                |
        +---- probe / info --------------+
        |                                |
      region                           no region
        |                                |
  CPU direct access                 read / write
```

公共层的主要规则：

- `probe` 为必需回调，setup 时发现并缓存 `mr_ram_info_t`。
- `info.size` 必须非 0。
- `regions_num == 0` 表示没有 CPU 可直接寻址区域，是合法的间接 RAM。
- `regions_num > 0` 时 `region` 回调必须存在。
- `read`/`write` 均为可选能力，未实现时返回 `-MR_ENOSYS`。
- 读写 offset 使用 `mr_uint64_t`；公共层根据 `info.size` 做溢出安全的范围检查。
- RAM 没有 erase、掉电保持或块设备语义；这些属于 Flash/存储层。

### 2. 几何信息

```c
mr_ram_info_t info;
mr_ram_region_t region;

mr_ram_info(ram, &info);
mr_ram_region(ram, 0U, &region);
```

数据结构：

```c
typedef struct mr_ram_info {
    mr_uint64_t size;
    mr_uint32_t regions_num;
} mr_ram_info_t;

typedef struct mr_ram_region {
    void *address;
    mr_size_t size;
} mr_ram_region_t;
```

`info.size` 是整个 RAM 设备的逻辑容量。`region` 只描述 CPU 可以直接解引用的 memory-mapped 区域，因此它不是读写接口的分片表，也不要求所有
RAM 都存在。

典型情况：

| 设备                | `size`       | `regions_num` | `region`                      |
|---------------------|--------------|---------------|-------------------------------|
| MCU SRAM            | 总 SRAM 容量 | 1 或多个 bank | 每个 bank 的地址与大小        |
| FMC SDRAM           | SDRAM 总容量 | 通常 1        | SDRAM 映射地址与大小          |
| memory-mapped PSRAM | PSRAM 总容量 | 1 或多个      | 映射窗口                      |
| SPI PSRAM           | PSRAM 总容量 | 0             | 不支持，访问走 `read`/`write` |

### 3. 生命周期与 probe

RAM setup 时公共层调用：

```c
ops->probe(drv, &info);
```

`probe` 可以只返回固定几何，也可以完成外部 RAM 控制器初始化、读取器件 ID、确定 容量等工作。成功后公共层校验 `size` 和 region
能力，并把信息缓存到
`mr_ram_data_t.info`。

shutdown 时只清空公共层缓存；第一版没有 teardown/remove 回调。再次 setup 会重新 执行 `probe`。

### 4. 读写

```c
mr_ram_read(ram, offset, buf, size);
mr_ram_write(ram, offset, buf, size);
```

公共层先检查：

```text
offset <= info.size
size   <= info.size - offset
```

因此不会通过 `offset + size` 产生整数溢出。`offset == info.size && size == 0` 合法； 任何真正越过设备末尾的访问返回
`-MR_EINVAL`，不会进入 backend。

具体的数据搬运由 backend 完成。memory-mapped RAM 可以直接 memcpy；SPI/QSPI PSRAM 可以在 `read`/`write` 中发送控制器命令。

### 5. 后端接口

```c
typedef struct mr_ram_ops {
    mr_err_t (*probe)(const mr_driver_t *drv, mr_ram_info_t *info);
    mr_err_t (*region)(const mr_driver_t *drv, mr_uint32_t index,
                       mr_ram_region_t *region);
    mr_ssize_t (*read)(const mr_driver_t *drv, mr_uint64_t offset,
                       mr_uint8_t *buf, mr_size_t size);
    mr_ssize_t (*write)(const mr_driver_t *drv, mr_uint64_t offset,
                        const mr_uint8_t *buf, mr_size_t size);
} mr_ram_ops_t;
```

`probe` 必需；其余回调按能力实现。若 `probe` 报告 `regions_num > 0`，则 `region`
也必须实现，否则 setup 返回 `-MR_EINVAL`。

### 6. 示例：间接访问 RAM

```c
static mr_uint8_t backing[4096];

static mr_err_t ram0_probe(const mr_driver_t *drv, mr_ram_info_t *info) {
    MR_IGNORE(drv);
    info->size = sizeof(backing);
    info->regions_num = 0U;       /* 无 CPU 直接映射 */
    return 0;
}

static mr_ssize_t ram0_read(const mr_driver_t *drv, mr_uint64_t offset,
                            mr_uint8_t *buf, mr_size_t size) {
    MR_IGNORE(drv);
    mr_memcpy(buf, &backing[(mr_size_t)offset], size);
    return (mr_ssize_t)size;
}

static mr_ssize_t ram0_write(const mr_driver_t *drv, mr_uint64_t offset,
                             const mr_uint8_t *buf, mr_size_t size) {
    MR_IGNORE(drv);
    mr_memcpy(&backing[(mr_size_t)offset], buf, size);
    return (mr_ssize_t)size;
}

static mr_ram_data_t ram0_data;
static const mr_ram_ops_t ram0_ops = {
    .probe = ram0_probe,
    .read = ram0_read,
    .write = ram0_write,
};

MR_RAM_INSTALL(ram0, &ram0_data, MR_NULL, &ram0_ops,
               MR_DRIVER_LEVEL_NORMAL);
```

由于公共层已经按 `info.size` 做范围检查，backend 不需要重复实现同一层的总容量 边界检查；backend 仍负责控制器自身的事务限制、对齐、缓存等约束。

### 7. HardTree

通用安装宏：

```c
MR_HT_RAM_INSTALL(id, priv, ops)
```

只创建零初始化的 `mr_ram_data_t` 并注册实例，不再假定几何一定在 HardTree 中静态 可知。具体 backend 自己决定从寄存器、器件
ID、固定参数或 HardTree 属性得到容量和 region。

### 8. API 参考

| API                                | 说明                                  |
|------------------------------------|---------------------------------------|
| `mr_ram_init` / `mr_ram_create`    | 初始化或动态创建 RAM 驱动             |
| `mr_ram_del`                       | 删除 RAM 驱动                         |
| `mr_ram_info`                      | 获取 setup 时缓存的容量和 region 数量 |
| `mr_ram_region`                    | 获取一个 CPU 可直接寻址区域           |
| `mr_ram_read`                      | 按逻辑 offset 读取                    |
| `mr_ram_write`                     | 按逻辑 offset 写入                    |
| `mr_ram_get` / `mr_ram_put`        | 驱动引用计数                          |
| `MR_RAM_DEFINE` / `MR_RAM_INSTALL` | 静态定义/安装                         |
| `MR_HT_RAM_INSTALL`                | HardTree 实例安装                     |

### 9. 设计边界

当前 RAM 公共层故意不加入以下内容：

- cache policy / MPU memory attribute；
- ECC 状态与错误事件；
- DMA 专用接口；
- heap allocation/free；
- erase、sector、block 等非易失存储语义；
- teardown/remove。

这些能力有明确需求时应按各自层次增加，而不是把 `mr_ram` 重新变成一个混合抽象。

---

## 开发者指南

### 10. 核心流程

```text
                 mr_driver_setup
                        |
                     probe()
                        |
                 cache ram_data.info
                        |
        +---------------+----------------+
        |                                |
   mr_ram_region                    mr_ram_read/write
        |                                |
   ops->region                     range check
                                         |
                                  ops->read/write
```

`ram.c` 的 init 要求 `probe` 非空。probe 成功后：

- `info.size == 0` -> `-MR_EINVAL`；
- `regions_num > 0 && ops->region == NULL` -> `-MR_EINVAL`；
- 其余信息缓存到 `mr_ram_data_t.info`。

`ram_deinit` 只清空缓存信息。

### 11. 与 FLASH 的关系

RAM 与 FLASH 现在使用同一代几何发现模型：

| RAM      | FLASH     |
|----------|-----------|
| `probe`  | `probe`   |
| `info`   | `info`    |
| `region` | `region`  |
| `read`   | `read`    |
| `write`  | `program` |
| 无       | `erase`   |

二者区别在于 region 的意义：RAM region 表示 CPU 直接映射区；FLASH region 表示擦除 几何区域。

平台 RAM backend 的 probe/region、memory-map、cache 与 HardTree 接入见 [RAM 驱动移植指南](porting.md)。

### 12. 源码导读

| 位置                                   | 职责                                      |
|----------------------------------------|-------------------------------------------|
| `mrX/include/mrX/drivers/ram/mr_ram.h` | info/region、ops、公共 API、HardTree 安装 |
| `mrX/drivers/ram/ram.c`                | setup probe、信息缓存与 driver 生命周期   |
| `mrX/test/drivers/ram/ram.c`           | probe、region、范围检查、可选 IO 测试     |

### 13. 错误语义

- `-MR_EINVAL`：probe 几何非法、region index 越界或读写范围越界。
- `-MR_ENOSYS`：可选的 `region`/`read`/`write` 回调未实现。
- 其他 backend 错误码原样透传。

空指针和 `buf == MR_NULL && size != 0` 等编程错误由 `MR_ASSERT` 保护。

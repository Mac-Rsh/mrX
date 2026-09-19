# Flash Memory (FLASH)

> 为片内 Flash 与片外 NOR Flash 提供统一的探测、几何查询、读取、编程与擦除接口。

| 项目     | 说明 |
|----------|------|
| 功能宏   | `MR_USE_DRIVER_FLASH` |
| 头文件   | `<mrX/drivers/flash/mr_flash.h>` |
| 依赖模块 | [驱动框架](../driver.md) |
| 参考测试 | `mrX/test/drivers/flash/flash.c` |

---

## 用户指南

### 1. 模块概述

FLASH 驱动把具体 Flash 的连接方式与上层存储语义分离。设备可以是 MCU 片内 Flash，也可以是 SPI/QSPI/OSPI 等片外 NOR Flash。公共层不要求容量、页大小和擦除粒度必须在编译期已知；驱动在 setup 阶段调用后端 `probe`，把运行时发现的信息缓存到 `mr_flash_data_t.info`。

第一版公共模型面向可按地址读取、编程和擦除的 NOR/片内 Flash。NAND 的 OOB、ECC、坏块等语义暂不放入本接口。

### 2. 几何信息

```c
mr_flash_info_t info;
mr_flash_region_t region;

mr_flash_info(flash, &info);
mr_flash_region(flash, 0, &region);
```

`mr_flash_info_t` 字段：

| 字段 | 说明 |
|------|------|
| `size` | Flash 总容量，单位字节 |
| `program_size` | 最小编程单位，单位字节 |
| `page_size` | 编程页大小；无页边界概念时为 0 |
| `regions_num` | 擦除 region 数量 |
| `erased_value` | 擦除后的字节值，NOR 通常为 `0xff` |

`mr_flash_region_t` 描述一个擦除几何区域：`offset`、`size` 与该区域的最小 `erase_size`。不同 region 可以具有不同擦除粒度，因此能够覆盖部分 MCU 的非均匀 sector 布局。
`mr_flash_region` 先检查 `region` callback 是否存在，再检查 `index < info.regions_num`；因此 `regions_num > 0` 并不强制 backend 必须实现 region 查询，缺少 callback 时优先返回 `-MR_ENOSYS`。

### 3. 生命周期与 probe

驱动 setup 时公共层调用：

```c
ops->probe(drv, &info);
```

片外 Flash 可以在这里读取 JEDEC ID、SFDP 或芯片寄存器；片内 Flash 后端也可以读取芯片容量寄存器，或者直接填入固定几何。公共层只关心最终得到的 `mr_flash_info_t`。

`probe` 成功后要求：`size != 0`、`program_size != 0`、`regions_num != 0`。若 `page_size != 0`，其值不能小于 `program_size`。

公共层不会进一步验证 `erased_value`、region 覆盖关系、region 的 `erase_size` 或 `program_size/page_size` 是否为 2 的幂；这些都属于 backend 提供 geometry 的正确性责任。

FLASH clazz 没有 deinit 回调，因此 shutdown 本身不会清空 `flash_data->info`。再次 setup 时会重新执行 `probe` 并用新的 `info` 覆盖缓存；未 setup 状态下不应通过类型化 FLASH 操作接口使用这份旧缓存。

### 4. 读、编程与擦除

```c
mr_flash_read(flash, offset, buf, size);
mr_flash_erase(flash, offset, erase_size);
mr_flash_program(flash, offset, data, size);
```

`mr_flash_program` 只表示 Flash program 操作，**不会隐式 erase**。需要从 0 恢复为擦除态的位必须由调用者先执行 `mr_flash_erase`。

公共层只检查 `[offset, offset + size)` 是否落在缓存的 `info.size` 范围内，并用减法形式避免加法溢出。它不会按 `program_size`、`page_size` 或 region 的 `erase_size` 检查对齐，也不会自动拆分跨页 program；这些限制与策略全部由 backend 处理。零长度 read/program/erase 在范围合法时也会继续调用对应 backend。

### 5. 后端接口

```c
typedef struct mr_flash_ops {
    mr_err_t (*probe)(const mr_driver_t *drv, mr_flash_info_t *info);
    mr_err_t (*region)(const mr_driver_t *drv, mr_uint32_t index,
                       mr_flash_region_t *region);
    mr_ssize_t (*read)(const mr_driver_t *drv, mr_uint64_t offset,
                       mr_uint8_t *buf, mr_size_t size);
    mr_ssize_t (*program)(const mr_driver_t *drv, mr_uint64_t offset,
                          const mr_uint8_t *buf, mr_size_t size);
    mr_err_t (*erase)(const mr_driver_t *drv, mr_uint64_t offset,
                      mr_uint64_t size);
} mr_flash_ops_t;
```

`probe` 是 setup 时直接 `MR_ASSERT` 的必需 callback。`region`、`read`、`program`、`erase` 都是可选 callback；对应公共 API 在 callback 为空时返回 `-MR_ENOSYS`。`region` 可以返回静态表项，也可以动态计算或从硬件信息中回读；公共层只缓存 `mr_flash_info_t`，不会保存完整 region 数组。

### 6. HardTree

`MR_HT_FLASH_INSTALL` 只创建 Flash 公共数据并注册实例：

```c
MR_HT_FLASH_INSTALL(id, priv, ops)
```

HardTree 不强制描述 Flash 的容量、页大小或擦除 geometry；这些信息统一由 setup 阶段的 `probe` 获得，避免把片外器件运行时才能确认的信息固化到静态硬件树中。

### 7. 源码导读

| 位置 | 职责 |
|------|------|
| `mrX/include/mrX/drivers/flash/mr_flash.h` | 几何结构、ops、生命周期包装和读/编程/擦除 API |
| `mrX/drivers/flash/flash.c` | setup probe、几何缓存与 driver 生命周期 |
| `mrX/include/mrX/drivers/flash/Kconfig` | FLASH 功能开关 |

当前仓库没有 FLASH 平台 backend；实现新 backend 时见 [FLASH 驱动移植指南](porting.md)，公共层契约的行为级测试见 `mrX/test/drivers/flash/flash.c`。

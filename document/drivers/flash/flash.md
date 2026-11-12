# 闪存驱动（FLASH）

> 为片内 Flash 与片外 NOR Flash 提供统一的探测、几何查询、读取、编程与擦除接口。

| 项目     | 说明                             |
|----------|----------------------------------|
| 功能宏   | `MR_USE_DRIVER_FLASH`            |
| 头文件   | `<mrX/drivers/flash/mr_flash.h>` |
| 依赖模块 | [驱动框架](../driver.md)         |
| 参考测试 | `mrX/test/drivers/flash/flash.c` |

---

## 用户指南

### 1. 模块概述

FLASH 驱动把具体 Flash 的连接方式与上层存储语义分离。设备可以是 MCU 片内
Flash，也可以是 SPI/QSPI/OSPI 等片外 NOR
Flash。公共层不要求容量、页大小和擦除粒度必须在编译期已知；驱动在 setup 阶段调用后端
`probe`，把运行时发现的信息缓存到
`mr_flash_data_t.info`。

第一版公共模型面向可按地址读取、编程和擦除的 NOR/片内 Flash。NAND 的
OOB、ECC、坏块等语义暂不放入本接口。

### 2. 几何信息

```c
mr_flash_info_t info;
mr_flash_region_t region;

mr_flash_info(flash, &info);
mr_flash_region(flash, 0, &region);
```

`mr_flash_info_t` 字段：

| 字段           | 说明                              |
|----------------|-----------------------------------|
| `size`         | Flash 总容量，单位字节            |
| `program_size` | 最小编程单位，单位字节            |
| `page_size`    | 编程页大小；无页边界概念时为 0    |
| `regions_num`  | 擦除 region 数量                  |
| `erased_value` | 擦除后的字节值，NOR 通常为 `0xff` |

`mr_flash_region_t` 描述一个擦除几何区域：`offset`、`size` 与该区域的最小
`erase_size`。不同 region 可以具有不同擦除粒度，因此能够覆盖部分
MCU 的非均匀 sector 布局。
`mr_flash_region` 先检查 `region` callback 是否存在，再检查
`index < info.regions_num`；因此 `regions_num > 0` 并不强制
backend 必须实现 region 查询，缺少 callback 时优先返回 `-MR_ENOSYS`。

### 3. 生命周期与 probe

驱动 setup 时公共层调用：

```c
ops->probe(drv, &info);
```

片外 Flash 可以在这里读取 JEDEC ID、SFDP 或芯片寄存器；片内 Flash
后端也可以读取芯片容量寄存器，或者直接填入固定几何。公共层只关心最终得到的
`mr_flash_info_t`。

`probe` 成功后要求：`size != 0`、`program_size != 0`、`regions_num != 0`。若
`page_size != 0`，其值不能小于 `program_size`。

公共层不会进一步验证 `erased_value`、region 覆盖关系、region 的 `erase_size` 或
`program_size/page_size` 是否为 2 的幂；这些都属于
backend 提供 geometry 的正确性责任。

FLASH clazz 没有 deinit 回调，因此 shutdown 本身不会清空 `flash_data->info`。再次
setup 时会重新执行 `probe` 并用新的
`info` 覆盖缓存；未 setup 状态下不应通过类型化 FLASH 操作接口使用这份旧缓存。

### 4. 读、编程与擦除

```c
mr_flash_read(flash, offset, buf, size);
mr_flash_erase(flash, offset, erase_size);
mr_flash_program(flash, offset, data, size);
```

`mr_flash_program` 只表示 Flash program 操作， **不会隐式 erase**。需要从 0
恢复为擦除态的位必须由调用者先执行
`mr_flash_erase`。

公共层只检查 `[offset, offset + size)` 是否落在缓存的 `info.size`
范围内，并用减法形式避免加法溢出。它不会按
`program_size`、`page_size` 或 region 的 `erase_size` 检查对齐，也不会自动拆分跨页
program；这些限制与策略全部由 backend
处理。零长度 read/program/erase 在范围合法时也会继续调用对应 backend。

### 5. API 参考

| API                                 | 说明                               |
|-------------------------------------|------------------------------------|
| `mr_flash_init` / `mr_flash_create` | 初始化静态实例 / 创建动态实例      |
| `mr_flash_del`                      | 删除 Flash 驱动                    |
| `mr_flash_info`                     | 读取 setup 阶段缓存的总体几何信息  |
| `mr_flash_region`                   | 查询指定擦除 region                |
| `mr_flash_read`                     | 按字节偏移读取，成功返回实际字节数 |
| `mr_flash_program`                  | 按字节偏移编程，成功返回实际字节数 |
| `mr_flash_erase`                    | 擦除指定字节区间                   |
| `mr_flash_get` / `mr_flash_put`     | 增加 / 归还对象引用                |

```c
mr_err_t mr_flash_info(mr_driver_t *drv, mr_flash_info_t *info);
mr_err_t mr_flash_region(mr_driver_t *drv, mr_uint32_t index,
                         mr_flash_region_t *region);
mr_ssize_t mr_flash_read(mr_driver_t *drv, mr_uint64_t offset,
                         void *buf, mr_size_t size);
mr_ssize_t mr_flash_program(mr_driver_t *drv, mr_uint64_t offset,
                            const void *buf, mr_size_t size);
mr_err_t mr_flash_erase(mr_driver_t *drv, mr_uint64_t offset,
                        mr_uint64_t size);
```

`read/program` 成功时返回 backend 报告的实际字节数，失败返回负错误码；其他操作成功返回
0。区间越界或 region 索引越界返回
`-MR_EINVAL`，可选 callback 缺失返回 `-MR_ENOSYS`，backend 其他错误原样透传。公共层不要求
`read/program` 成功时必须返回完整
`size`，调用方需检查短读/短写。

静态实例使用 `MR_FLASH_DEFINE` / `MR_FLASH_INSTALL` 或 `mr_flash_init`；动态实例使用
`mr_flash_create`。动态创建时传入的
`mr_flash_data_t` 必须来自动态分配，驱动最终释放时会回收它。类型化 Flash 操作应在
`mr_driver_setup()` 成功后使用。

### 6. 后端接口

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

`probe` 是 setup 时直接 `MR_ASSERT` 的必需 callback。`region`、`read`、`program`、
`erase` 都是可选 callback；对应公共 API 在
callback 为空时返回 `-MR_ENOSYS`。`region` 可以返回静态表项，也可以动态计算或从硬件信息中回读；公共层只缓存
`mr_flash_info_t`，不会保存完整 region 数组。

### 7. HardTree

`MR_HT_FLASH_INSTALL` 只创建 Flash 公共数据并注册实例：

```c
MR_HT_FLASH_INSTALL(id, priv, ops)
```

HardTree 不强制描述 Flash 的容量、页大小或擦除 geometry；这些信息统一由 setup 阶段的
`probe`
获得，避免把片外器件运行时才能确认的信息固化到静态硬件树中。

### 8. 常见问题（FAQ）

**为什么 `program` 不自动擦除？**

擦除粒度、耗时和寿命代价都显著高于编程，隐式擦除会使数据破坏范围和延迟不可见。调用方必须按
geometry 显式安排擦除。

**为什么 `mr_flash_region` 返回 `-MR_ENOSYS`？**

该 backend 没有实现 region 查询。`regions_num` 不为 0 只表示 probe
报告了几何区域数，公共层不强制保存 region 表。

**跨 page 的 program 会自动拆分吗？**

不会。公共层只做容量边界检查；对齐、跨页、擦除粒度和位转换规则由 backend 或上层存储逻辑处理。

---

## 开发者指南

### 9. 架构总览

```text
上层存储 / 升级逻辑
          |
          v
mr_flash_* 公共 API
          |
          +-- 缓存 mr_flash_info_t
          |
          v
mr_flash_ops_t backend
          |
          v
片内 Flash / SPI、QSPI、OSPI NOR
```

驱动 setup 通过 `probe` 建立几何信息；之后的 info 查询直接读取缓存，region
和数据操作则分发到 backend。

### 10. 数据结构与核心流程

`mr_flash_data_t` 只保存 `mr_flash_info_t info`。`flash_init` 在 setup 阶段调用必需的
`probe`，成功后检查 `size`、
`program_size`、`regions_num` 非零，并在 `page_size` 非零时要求其不小于
`program_size`。检查失败返回 `-MR_EINVAL`，不更新缓存。

`mr_flash_read/program/erase` 以 `offset > info.size` 或
`size > info.size - offset` 判定越界，避免 `offset + size`
的无符号溢出。零长度操作在偏移合法时仍会分发给 backend。

### 11. 并发与中断安全

- 公共层不对读、编程、擦除或 geometry 查询加锁；同一设备的并发策略由 backend 或上层管理。
- Flash 编程/擦除可能禁止取指、中断或 cache，能否在 ISR 或多线程中调用取决于具体
  backend。
- 删除驱动前，调用方应保证没有未完成的异步硬件操作。当前 FLASH clazz 没有 deinit
  回调，不会代替 backend 等待硬件完成。

### 12. 扩展与派生

实现 backend 时，`probe` 必须填充稳定的总体 geometry；非均匀擦除设备再通过
`region` 按索引返回分区。backend
还应明确对齐、跨页、位转换、cache/指令取指与锁定策略，详见 [FLASH 驱动移植指南](porting.md)。

### 13. 源码导读

| 位置                                                 | 职责                                          |
|------------------------------------------------------|-----------------------------------------------|
| `mrX/include/mrX/drivers/flash/mr_flash.h`           | 几何结构、ops、生命周期包装和读/编程/擦除 API |
| `mrX/drivers/flash/flash.c`                          | setup probe、几何缓存与 driver 生命周期       |
| `mrX/include/mrX/drivers/flash/Kconfig`              | FLASH 功能开关                                |
| `mrX/test/drivers/flash/flash.c`                     | 公共边界、可选 callback 与 probe 约束         |

实现新 backend 时见 [FLASH 驱动移植指南](porting.md)。

### 14. 附录

| 常量 / 宏                              | 说明                          |
|----------------------------------------|-------------------------------|
| `MR_DRIVER_TYPE_FLASH`                 | Flash 驱动类型值              |
| `MR_FLASH_DEFINE` / `MR_FLASH_INSTALL` | 定义 / 注册静态驱动           |
| `MR_HT_FLASH_INSTALL`                  | 通过 HardTree 注册 Flash 实例 |

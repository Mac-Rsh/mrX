# RAM 设备（RAM）

> RAM 驱动在驱动框架之上提供统一的内存设备抽象：以"区域（region）"描述一段
> 可用内存，通过挂载/卸载纳入系统、按偏移读写数据；框架自带的 ram_heaps 扩展
> 把硬件模板描述的内存区域挂入动态内存堆，用于扩展系统可用内存。

| 项       | 说明                                                                                           |
|----------|------------------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_DRIVER_RAM`（默认关闭）                                                                |
| 头文件   | `<mrX/drivers/ram/mr_ram.h>`                                                                   |
| 依赖模块 | [驱动框架 Driver](driver.md)；[动态内存 Malloc](../libc/malloc.md)（ram_heaps 扩展与动态创建） |
| 参考测试 | `mrX/test/drivers/ram/ram.c`                                                                   |

---

## 用户指南

### 1. 模块概述

RAM 驱动解决的问题是： **把片内 SRAM、片外 PSRAM/SDRAM 等内存映射设备纳入 统一的驱动模型**，用同一套接口描述区域、挂载卸载并按偏移读写。

特性：

- 区域描述：`mr_ram_region_t`（地址 + 大小）数组描述一块或多块可用内存， 挂在模型数据 `mr_ram_data_t` 中供 `ops` 实现使用。
- 生命周期与挂载绑定：setup 时执行 `mount`，shutdown 时执行 `unmount`， 两个回调均可选。
- 按偏移读写：`mr_ram_write`/`mr_ram_read` 直接透传 `ops`，返回实际字节数； 四个 `ops` 回调全部可选，未实现的读写回调返回
  `-MR_ENOSYS`。
- ram_heaps 内置扩展：把硬件模板（compat `ram_heaps`）描述的区域逐个挂入 动态内存堆（`mr_heap_region_add`
  ），实现"外扩内存即堆内存"。
- hardtree 规格宏：`MR_HT_RAM_INSTALL` 从硬件模板属性自动展开区域数组。

与文件系统的关系：RAM 驱动与 [RamFS](../fs/ramfs.md) 之间没有直接的调用 关系——RamFS 的目录与文件数据由 VFS 与动态内存堆支撑；RAM
驱动（经 ram_heaps 扩展）把内存区域加入同一个堆，间接扩大 RamFS 及其他堆使用方可 分配的内存总量。需要纯内存文件系统时直接使用
RamFS 即可。

不适用于：需要掉电保存的存储、需要擦除/块语义的 Flash 类设备（`ops` 中无 擦除接口，读写为纯内存语义）。

### 2. 配置与依赖

| 配置项              | 类型 | 默认值 | 说明                                                                           |
|---------------------|------|--------|--------------------------------------------------------------------------------|
| `MR_USE_DRIVER_RAM` | bool | n      | 启用 RAM 驱动（`mrX/include/mrX/drivers/ram/Kconfig:6`），依赖 `MR_USE_DRIVER` |

启用后重新生成 `mr_config.h`，包含头文件即可使用：

```c
#include <mrX/drivers/ram/mr_ram.h>
```

RAM 驱动没有流、事件等子功能开关，单一宏控制全部代码；`ram_heaps` 扩展随 本宏一并参与编译，但仅在硬件模板中存在 `ram_heaps`
兼容节点时实例化。

### 3. 快速上手

定义一个以静态数组为后端的 RAM 设备并安装：

```c
#include <mrX/drivers/ram/mr_ram.h>
#include <mrX/libc/mr_string.h>

static mr_uint8_t ram_backing[4096];

static mr_ssize_t ram0_write(const mr_driver_t *drv, mr_ssize_t offset,
                             const mr_uint8_t *buf, mr_size_t size) {
    if ((offset < 0) || ((mr_size_t)offset + size > sizeof(ram_backing))) {
        return -MR_EINVAL;
    }
    mr_memcpy(&ram_backing[offset], buf, size);
    return (mr_ssize_t)size;
}

static mr_ssize_t ram0_read(const mr_driver_t *drv, mr_ssize_t offset,
                            mr_uint8_t *buf, mr_size_t size) {
    if ((offset < 0) || ((mr_size_t)offset + size > sizeof(ram_backing))) {
        return -MR_EINVAL;
    }
    mr_memcpy(buf, &ram_backing[offset], size);
    return (mr_ssize_t)size;
}

static const mr_ram_ops_t ram0_ops = {.write = ram0_write, .read = ram0_read};
static mr_ram_region_t ram0_regions[] = {{ram_backing, sizeof(ram_backing)}};

/* Install: registered and auto-setup at boot */
MR_RAM_INSTALL(ram0, MR_RAM_DATA_INIT(ram0_regions, 1), MR_NULL, &ram0_ops,
               MR_DRIVER_LEVEL_NORMAL);
```

其他代码中按名查找并读写：

```c
mr_driver_t *drv = mr_driver_find("ram0");
if (drv != MR_NULL) {
    mr_ram_write(drv, 0, "hello", 5);
    mr_driver_put(drv);     /* Release the reference from find */
}
```

### 4. 使用说明

**区域描述**：`mr_ram_data_t` 的 `regions`/`regions_num` 是对设备内存的 描述信息，框架的读写路径不使用它们（可为 `MR_NULL`
/0）；`ops` 实现与 ram_heaps 扩展按需读取。

**挂载语义**：`mount`/`unmount` 与驱动 setup/shutdown 绑定：

| 操作                   | 行为                                         |
|------------------------|----------------------------------------------|
| `ops` 未提供 `mount`   | setup 直接成功，视为无需初始化               |
| `mount` 返回非 0       | setup 失败，错误码透传，驱动保持未 setup     |
| `ops` 未提供 `unmount` | shutdown 直接成功                            |
| `unmount` 返回非 0     | shutdown 失败，驱动仍视为已 setup，可重试    |
| `ops` 四个回调全空     | setup/shutdown 直接成功，仅可作占位驱动      |
| 删除时仍有其他引用     | `unmount` 与资源释放推迟到最后一个引用归还时 |

**读写语义**：`mr_ram_write(drv, offset, buf, size)` /
`mr_ram_read(drv, offset, buf, size)` 的 `offset` 为带符号偏移（负偏移的 含义由 `ops` 自行解释，框架不校验范围），返回实际读写的字节数或负数错误码；
`buf` 为 `MR_NULL` 时 `size` 必须为 0（断言保护），一致性校验之外的边界 检查由 `ops` 负责。

**生命周期**：RAM 驱动对象遵循[驱动框架](driver.md)的生命周期。静态方式用
`MR_RAM_DEFINE` / `MR_RAM_INSTALL` 或运行时 `mr_ram_init`；动态方式
`mr_ram_create` 在堆上创建，`mr_ram_create` 传入的 `mr_ram_data_t` 必须用
`mr_malloc` 分配——驱动删除时框架会释放它。

### 5. API 参考

定义与安装：

| API                 | 说明                                                    |
|---------------------|---------------------------------------------------------|
| `MR_RAM_DATA_INIT`  | 初始化 `mr_ram_data_t`（区域数组/数量）                 |
| `MR_RAM_DEFINE`     | 编译期定义 RAM 驱动                                     |
| `MR_RAM_INSTALL`    | 编译期定义并注册 RAM 驱动（可被 `mr_driver_find` 查找） |
| `MR_HT_RAM_INSTALL` | 经硬件模板安装 RAM 实例                                 |

生命周期：

| API             | 说明                          |
|-----------------|-------------------------------|
| `mr_ram_init`   | 运行时初始化一个静态 RAM 驱动 |
| `mr_ram_create` | 在堆上动态创建 RAM 驱动       |
| `mr_ram_del`    | 删除 RAM 驱动                 |

读写：

| API            | 说明                                       |
|----------------|--------------------------------------------|
| `mr_ram_write` | 按偏移写入数据，返回实际字节数或负数错误码 |
| `mr_ram_read`  | 按偏移读取数据，返回实际字节数或负数错误码 |

引用：

| API                         | 说明                |
|-----------------------------|---------------------|
| `mr_ram_get` / `mr_ram_put` | 引用计数加一 / 减一 |

**`mr_ram_init`**

```c
mr_err_t mr_ram_init(mr_driver_t *drv, mr_ram_data_t *data, void *priv,
                     mr_ram_ops_t *ops);
```

- `drv`：驱动对象，由调用者分配存储。
- `data`：RAM 模型数据，不可为 `MR_NULL`。
- `priv`：私有数据（如控制器寄存器实例），可为 `MR_NULL`。
- `ops`：操作表，不可为 `MR_NULL`。
- 返回：成功 `0`，失败负数错误码。

**`mr_ram_create`**

```c
mr_driver_t *mr_ram_create(mr_ram_data_t *data, void *priv, mr_ram_ops_t *ops);
```

参数含义同 `mr_ram_init`。成功返回驱动指针，内存不足返回 `MR_NULL`。

**`mr_ram_write` / `mr_ram_read`**

```c
mr_ssize_t mr_ram_write(mr_driver_t *drv, mr_ssize_t offset, const void *buf,
                        mr_size_t size);
mr_ssize_t mr_ram_read(mr_driver_t *drv, mr_ssize_t offset, void *buf,
                       mr_size_t size);
```

`ops` 未实现对应回调时返回 `-MR_ENOSYS`；其余错误码由 `ops` 透传。

**`mr_ram_del`**

```c
mr_err_t mr_ram_del(mr_driver_t *drv);
```

注销驱动：静态对象反初始化（可再次 `init` 复用），动态对象释放内存；若 仍有其他持有者，`unmount` 与释放推迟到引用归零。

### 6. 完整示例

示例 1：动态创建 RAM 驱动并走一遍挂载/卸载生命周期（改编自
`mrX/test/drivers/ram/ram.c` 的 `lifecycle`）：

```c
static mr_err_t my_mount(const mr_driver_t *drv)   { return 0; }
static mr_err_t my_unmount(const mr_driver_t *drv) { return 0; }

static const mr_ram_ops_t my_ops = {.mount = my_mount, .unmount = my_unmount};

void ram_lifecycle_example(void) {
    mr_ram_data_t *data = mr_malloc(sizeof(*data));
    mr_driver_t *drv;

    if (data == MR_NULL) {
        return;
    }
    data->regions = MR_NULL;            /* Description is optional */
    data->regions_num = 0;
    drv = mr_ram_create(data, MR_NULL, (mr_ram_ops_t *)&my_ops);
    if (drv == MR_NULL) {
        return;
    }
    mr_driver_setup(drv);               /* mount runs once */
    mr_driver_setup(drv);               /* idempotent, mount not re-run */
    mr_driver_shutdown(drv);            /* unmount runs */
    mr_ram_del(drv);                    /* Frees data */
}
```

示例 2：引用计数推迟卸载（改编自测试 `reference_delays_release`）：

```c
void ram_reference_example(mr_driver_t *drv) {
    mr_driver_t *held = mr_ram_get(drv);    /* Second holder keeps it alive */

    mr_ram_del(drv);                        /* Mark dying; unmount deferred */
    /* ... last put triggers unmount ... */
    mr_ram_put(held);
}
```

更多行为（mount/unmount 失败路径、可选生命周期、读写与 `-MR_ENOSYS`）见
`mrX/test/drivers/ram/ram.c`。

### 7. 常见问题（FAQ）

**Q：`mr_driver_setup` 返回非 0？**

`mount` 回调失败，错误码原样透传（如 `-MR_EIO`）。驱动保持未 setup，修复 挂载条件后可直接再次 setup。

**Q：`mr_driver_shutdown` 返回非 0 且驱动仍显示已 setup？**

`unmount` 回调失败时 setup 标记保持不变（见[驱动框架](driver.md)的 shutdown 语义），可再次尝试 shutdown；对象删除时框架仍会补一次
`deinit` 兜底。

**Q：`mr_ram_write`/`mr_ram_read` 返回 `-MR_ENOSYS`？**

`ops` 未实现对应回调。RAM 驱动的四个回调全部可选，只读或只写的半双工设备 可只实现一侧。

**Q：ram_heaps 扩展的 `unmount` 为什么总是失败？**

内存区域一旦通过 `mr_heap_region_add` 并入堆，无法在不破坏堆完整性的前提下 安全摘除，因此扩展的 `unmount` 固定返回
`-MR_EIO`（`ram_heaps.c:42`）。

**Q：`regions` 什么时候必须填？**

框架读写路径不使用它；使用 ram_heaps 扩展或 `ops` 实现需要按区域遍历内存时 才需要填写。`MR_HT_RAM_INSTALL` 会从硬件模板属性自动生成。

**Q：它和 RamFS 是什么关系？**

没有直接耦合。RamFS 的文件数据由堆缓冲区支撑（见
[RamFS](../fs/ramfs.md)）；RAM 驱动经 ram_heaps 把区域挂入同一个堆，间接 扩大 RamFS 可用的分配空间。

---

## 开发者指南

### 8. 架构总览

RAM 驱动是[驱动框架](driver.md)的派生模块，自身极薄：框架层只做断言校验、 可选能力门控与生命周期挂钩，实际内存操作全部在
`ops` 中：

```
        +------------------------------------------+
        |  应用（mr_ram_write / mr_ram_read）        |
        +-------------------+----------------------+
                            | 断言 / ENOSYS 门控
        +-------------------v----------------------+
        |  RAM 框架层（ram.c + mr_ram.h）            |
        |  mount/unmount 随 setup/shutdown 启闭      |
        +-------------------+----------------------+
                            | mr_ram_ops_t
        +-------------------v----------------------+
        |  ops 实现：自定义读写 / ram_heaps 扩展      |
        |  （ram_heaps 将区域挂入 malloc 堆）         |
        +------------------------------------------+
             依赖：驱动框架；malloc（ram_heaps 与动态创建）
```

### 9. 数据结构

区域与模型数据（`mrX/include/mrX/drivers/ram/mr_ram.h:28`、`mr_ram.h:35`）：

```c
typedef struct mr_ram_region {
    void *address;          /* Region start address */
    mr_uint32_t size;       /* Region size in bytes */
} mr_ram_region_t;

typedef struct mr_ram_data {
    mr_ram_region_t *regions;       /* Region array (description only) */
    mr_uint32_t regions_num;        /* Region count */
} mr_ram_data_t;
```

操作表（`mr_ram.h:41`）：`mount`/`unmount`/`write`/`read` 四个回调 **全部 可选**，框架层对未实现者分别处理（见第 10 节）。回调签名均为
`const mr_driver_t *drv` 入参，读写在 `ops` 内部完成边界检查。

### 10. 核心流程

**clazz**：RAM 定义了完整的 init/deinit（`MR_DRIVER_CLAZZES_DEFINE(ram,
ram_init, ram_deinit, ram_release)`，`mrX/drivers/ram/ram.c:17`）。

**setup（挂载）**：`ram_init`（`mrX/drivers/ram/ram.c:19`）在框架 setup 阶段被调用：`ops->mount` 存在则执行，返回非 0 时 setup
失败；不存在则直接 成功。 **shutdown（卸载）**：`ram_deinit`（`ram.c:39`）对称处理
`ops->unmount`。`ram_release`（`ram.c:59`）在引用归零时 `mr_free` 模型 数据。

**读写路径**：`mr_ram_write`（`mr_ram.h:153`）与 `mr_ram_read`
（`mr_ram.h:184`）做断言校验（驱动类型、ops 非空、buf/size 一致性）后， 对未实现的回调返回 `-MR_ENOSYS`，否则原样透传 `ops`
，框架不拷贝数据。

**ram_heaps 扩展**（`mrX/drivers/ram/ram_heaps.c`）：

1. `MR_HT_COMPAT` 定义为 `ram_heaps`（`ram_heaps.c:14`），仅在硬件模板存在 该兼容组时编译实例（`MR_HT_HAS_DRIVER()`）。
2. 扩展的 `ops` 在 `mount`（`ram_heaps.c:23`）中把区域并入堆：遍历 `data->regions`， 把每个区域组装为 `{address, size}` +
   `MR_NULL` 结尾的
   `mr_heap_region_t` 数组，逐个调用 `mr_heap_region_add`
   （`mrX/libc/malloc.c:274`）并入堆。
3. `unmount`（`ram_heaps.c:38`）固定返回 `-MR_EIO`：并入堆的区域无法安全 摘除。
4. `MR_HT_DRIVER_FOREACH`（`ram_heaps.c:48`）对该兼容组的所有实例批量 安装驱动。

### 11. 并发与中断安全

- 框架层 API 无内部互斥；对同一 RAM 设备的并发读写由 `ops` 实现或应用保证。
- `mr_ram_get`/`mr_ram_put` 的引用计数与 `mr_ram_del` 的延迟释放语义来自
  [驱动框架](driver.md)；`create`/`del` 涉及堆操作，应在线程上下文调用。
- ram_heaps 的 `mr_heap_region_add` 在 setup 上下文执行（系统初始化或首次
  `mr_driver_setup`），与堆分配器的并发安全由 malloc 模块保证。
- 挂载/卸载回调在调用 setup/shutdown 的调用者上下文中执行，回调内不应阻塞。

### 12. 扩展与派生

- **hardtree 实例化**：`MR_HT_RAM_INSTALL`（`mr_ram.h:236`）从硬件模板的
  `region` 属性（属性单元 `[address, size]`，`mr_ram.h:245`）自动生成区域 数组与 `mr_ram_data_t` 并安装驱动；ram_heaps
  扩展即基于它实现，是编写
  "带 `ops` 的 RAM 派生" 的现成示例。
- **自定义 ops**：外扩 SRAM/SDRAM 控制器可提供自己的 `mount`（初始化控制 器/时序）与读写回调，通过 `MR_RAM_INSTALL` 注册；
  `regions` 描述信息供 回调自行消费。
- 平台侧目前没有 RAM 控制器实现，接口与 [ADC](adc.md) 的平台移植路径同构， 可参照其安装方式。

### 13. 源码导读

| 位置                                       | 职责                                   |
|--------------------------------------------|----------------------------------------|
| `mrX/include/mrX/drivers/ram/mr_ram.h:28`  | 区域结构 `mr_ram_region_t`             |
| `mrX/include/mrX/drivers/ram/mr_ram.h:35`  | 模型数据 `mr_ram_data_t`               |
| `mrX/include/mrX/drivers/ram/mr_ram.h:41`  | 操作表（四个回调全部可选）             |
| `mrX/include/mrX/drivers/ram/mr_ram.h:56`  | `MR_RAM_DATA_INIT` 初始化宏            |
| `mrX/include/mrX/drivers/ram/mr_ram.h:153` | 写路径（断言 + ENOSYS + 透传）         |
| `mrX/include/mrX/drivers/ram/mr_ram.h:184` | 读路径                                 |
| `mrX/include/mrX/drivers/ram/mr_ram.h:236` | hardtree 实例安装宏                    |
| `mrX/drivers/ram/ram.c:17`                 | RAM clazz（init/deinit/release）       |
| `mrX/drivers/ram/ram.c:19`                 | setup：可选 mount                      |
| `mrX/drivers/ram/ram.c:39`                 | shutdown：可选 unmount                 |
| `mrX/drivers/ram/ram_heaps.c:23`           | 扩展挂载：区域并入 malloc 堆           |
| `mrX/drivers/ram/ram_heaps.c:48`           | 兼容组实例批量安装                     |
| `mrX/libc/malloc.c:274`                    | `mr_heap_region_add`（堆区域注册入口） |
| `mrX/test/drivers/ram/ram.c`               | 行为级测试                             |

### 14. 附录：配置宏与错误码

**配置宏**

| 宏                  | 说明                                   |
|---------------------|----------------------------------------|
| `MR_USE_DRIVER_RAM` | 模块开关，默认 n，依赖 `MR_USE_DRIVER` |

**错误码**：`-MR_ENOSYS`（`write`/`read` 回调未实现）、`-MR_EIO`
（`mount`/`unmount` 失败透传，ram_heaps 卸载固定返回）、其余为 `ops`
透传错误码，定义见 `mrX/include/mrX/libc/mr_errno.h`；空指针、buf/size 不一致等编程错误在断言开启时由 `MR_ASSERT` 拦截。

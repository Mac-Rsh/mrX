# 驱动框架（Driver）

> 驱动框架为所有外设驱动提供统一的对象模型：驱动的定义与注册、按名查找、
> 生命周期管理（setup/shutdown）与引用计数，是 GPIO、ADC、DAC、RAM 等具体
> 设备驱动的公共基座。

| 项       | 说明                                                                                                                       |
|----------|----------------------------------------------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_DRIVER`（默认关闭，选中后自动开启 `MR_USE_WAIT`）                                                                  |
| 头文件   | `<mrX/drivers/mr_driver.h>`                                                                                                |
| 依赖模块 | [对象 Object](../kernel/object.md)、[等待队列 Wait](../kernel/wait.md)、[动态内存 Malloc](../libc/malloc.md)（仅动态创建） |
| 参考测试 | `mrX/test/drivers/driver.c`                                                                                                |

---

## 用户指南

### 1. 模块概述

驱动框架解决的问题是： **用同一种方式描述、注册、查找和启停一个硬件设备驱动**， 让上层应用不必关心驱动代码放在哪个文件、何时初始化。

特性：

- 每个驱动是一个对象（`mr_driver_t`），带类型标签与引用计数，支持静态初始化与 动态创建两种生命周期方式。
- 通过 `MR_DRIVER_INSTALL` 宏把驱动"安装"到链接段，系统初始化时自动按优先级 依次启动，无需手工调用初始化函数。
- `mr_driver_find` 支持按名称查找任意已安装的驱动，找到即持有引用。
- 驱动生命周期被明确划分为"注册（init）→ 上电（setup）→ 下电（shutdown）→ 注销（del）"四步，setup/shutdown 可反复循环。
- 驱动按大类型分类（GPIO/ADC/DAC/RAM/UART/I2C/DMA/TMR/WDT/FLASH 等），API 入口会校验类型， 把 A 类驱动指针传给 B 类 API 会被立即拦截。

不适用于：需要复杂总线枚举或热插拔的场景（框架只提供单层驱动模型，无总线/ 设备子树）。

### 2. 配置与依赖

| 配置项          | 类型 | 默认值 | 说明                                                                         |
|-----------------|------|--------|------------------------------------------------------------------------------|
| `MR_USE_DRIVER` | bool | n      | 启用驱动框架（`mrX/include/mrX/drivers/Kconfig`），自动 `select MR_USE_WAIT` |

启用后重新生成 `mr_config.h`，包含头文件即可使用：

```c
#include <mrX/drivers/mr_driver.h>
```

具体外设驱动（如 `MR_USE_DRIVER_GPIO`）均 `depends on MR_USE_DRIVER`，因此必须先启用驱动框架，之后具体外设驱动选项才可用。

### 3. 快速上手

定义一个最简单的驱动并让系统自动启动它：

```c
#include <mrX/drivers/mr_driver.h>

static mr_err_t my_init(const mr_driver_t *drv) {
    /* Power on / configure hardware here */
    return 0;
}

static mr_err_t my_deinit(const mr_driver_t *drv) {
    /* Power off here */
    return 0;
}

static const mr_driver_clazz_t my_clazz = MR_DRIVER_CLAZZ_INIT(
    "my_driver", my_init, my_deinit, MR_NULL);

/* Install: registered and auto-setup at boot (level "3" = normal) */
MR_DRIVER_INSTALL(my_dev, &my_clazz, MR_DRIVER_TYPE_RAM,
                  MR_NULL, MR_NULL, MR_NULL, MR_DRIVER_LEVEL_NORMAL);
```

要点：

- `MR_DRIVER_INSTALL(name, ...)` 生成驱动对象与注册条目；`name` 即之后
  `mr_driver_find` 使用的名字。
- 第 3 个参数是驱动大类型，必须非 `MR_DRIVER_TYPE_NONE`；自定义为具体外设 驱动时应使用该外设模块的类型（如
  `MR_DRIVER_TYPE_GPIO`）。
- 最后一个参数是初始化级别，决定启动顺序（见第 4 节）。

其他代码中按名查找并使用：

```c
mr_driver_t *drv = mr_driver_find("my_dev");
if (drv != MR_NULL) {
    /* ... use drv ... */
    mr_driver_put(drv);     /* Release the reference from find */
}
```

### 4. 使用说明

**生命周期**

- 静态方式：定义 `mr_driver_t` 后调用 `mr_driver_init`，或直接用
  `MR_DRIVER_DEFINE` / `MR_DRIVER_INSTALL` 在编译期定义；用完 `mr_driver_del`
  反初始化（不释放内存，可重新 `init` 复用）。
- 动态方式：`mr_driver_create` 在堆上创建，`mr_driver_del` 销毁并释放内存。
- `mr_driver_setup` 执行 clazz 的 `init` 回调（上电）；`mr_driver_shutdown`
  执行 `deinit` 回调（下电）。两者可反复交替调用。
- `mr_driver_del` 注销驱动：若当前处于 setup 状态会先执行 `deinit`；若有其他 持有者尚未 `put`，实际销毁推迟到最后一个引用归还时。

**运行期行为约定**

| 操作                                                | 行为                                                                                            |
|-----------------------------------------------------|-------------------------------------------------------------------------------------------------|
| 对已 setup 的驱动再次 `mr_driver_setup`             | 幂等，直接返回 0，不重复执行 `init`                                                             |
| 对未 setup 的驱动 `mr_driver_shutdown`              | 安全，返回 0，不执行 `deinit`                                                                   |
| 并发同时对同一驱动 `setup`/`shutdown`               | 后到者返回 `-MR_EBUSY`                                                                          |
| 驱动正在被删除（`del` 已调用）时 `setup`/`shutdown` | 返回 `-MR_EPERM`                                                                                |
| clazz 未提供 `init`/`deinit` 回调                   | `setup`/`shutdown` 直接成功，视为无需硬件操作                                                   |
| `init` 回调返回非 0                                 | `setup` 返回该错误码，驱动保持未 setup；错误码同时记录在驱动状态中，可用 `mr_driver_state` 读取 |
| `mr_driver_find` 查找不存在的名字                   | 返回 `MR_NULL`                                                                                  |
| `mr_driver_get` / `mr_driver_put`                   | 引用计数加一 / 减一；`find` 内部已 `get` 一次，用完必须 `put`                                   |

**初始化级别**

`MR_DRIVER_INSTALL` 的最后一个参数决定自动启动顺序，由链接脚本按级别名排序：
`MR_DRIVER_LEVEL_EARLY`（"1"）→ `MR_DRIVER_LEVEL_PRIO("x")`（"2.x"）→
`MR_DRIVER_LEVEL_NORMAL`（"3"）→ `MR_DRIVER_LEVEL_LAST`（"4"）。同级别的
`MR_DRIVER_LEVEL_PRIO` 子级别字符串越小越先。`MR_DRIVER_LEVEL_DEFER`（"9"） 的驱动 **不会**被自动启动，需查找后手动 `setup`。

**回调上下文**：`init`/`deinit` 回调由 `mr_driver_setup`/`mr_driver_shutdown` 同步调用，因此执行上下文就是调用者当前上下文；系统自动启动阶段则运行在内核初始化流程中。框架本身不额外切换上下文，回调是否允许阻塞由具体调用场景和后端实现决定。

### 5. API 参考

生命周期：

| API                  | 说明                                           |
|----------------------|------------------------------------------------|
| `MR_DRIVER_INIT`     | 静态初始化宏（编译期初始化驱动对象）           |
| `mr_driver_init`     | 运行时初始化一个静态驱动                       |
| `mr_driver_create`   | 在堆上动态创建一个驱动                         |
| `mr_driver_del`      | 删除驱动（静态对象反初始化，动态对象释放内存） |
| `mr_driver_setup`    | 启动驱动（执行 clazz 的 `init`）               |
| `mr_driver_shutdown` | 停止驱动（执行 clazz 的 `deinit`）             |

查找与引用：

| API                                     | 说明                                           |
|-----------------------------------------|------------------------------------------------|
| `mr_driver_find`                        | 按名称查找驱动，返回已持有引用的驱动指针       |
| `mr_driver_get` / `mr_driver_put`       | 引用计数加一 / 减一                            |
| `MR_DRIVER_FIND`                        | 宏：按编译期名字直接取驱动对象地址（不做查找） |
| `MR_DRIVER_EXPORT` / `MR_DRIVER_IMPORT` | 跨文件导出 / 导入驱动对象                      |
| `mr_driver_is_ready`                    | 查询驱动是否已 setup 且状态正常                |

参数与查询：

| API                                                   | 说明                                |
|-------------------------------------------------------|-------------------------------------|
| `mr_driver_type`                                      | 读取完整驱动类型值                  |
| `MR_DRIVER_TYPE` / `MR_DRIVER_IS_TYPE`                | 宏：读取 / 精确比较完整类型值       |
| `mr_driver_state` / `mr_driver_state_set`             | 读取 / 设置驱动状态（0 或负错误码） |
| `MR_DRIVER_IS_SETUP`                                  | 宏：查询驱动是否已 setup            |
| `MR_DRIVER_IS_DYING`                                  | 宏：查询驱动是否正在删除            |
| `MR_DRIVER_ASSERT_TYPE`                               | 宏：按 `MR_DRIVER_TYPE_MASK` 校验 major type，不要求 setup |
| `MR_DRIVER_DATA` / `MR_DRIVER_PRIV` / `MR_DRIVER_OPS` | 宏：取驱动数据 / 私有数据 / 操作表  |

clazz 定义：

| API                                                      | 说明                                                 |
|----------------------------------------------------------|------------------------------------------------------|
| `MR_DRIVER_CLAZZ_INIT`                                   | 静态初始化一个驱动 clazz（name/init/deinit/release） |
| `mr_driver_clazz_init`                                   | 运行时初始化一个驱动 clazz（`MR_INLINE`）            |
| `MR_DRIVER_CLAZZES_DEFINE`                               | 定义静态/动态双通道 clazz（动态通道带 release）      |
| `MR_DRIVER_CLAZZES_STATIK` / `MR_DRIVER_CLAZZES_DYNAMIC` | 取静态 / 动态 clazz                                  |

**`mr_driver_init`**

```c
mr_err_t mr_driver_init(mr_driver_t *drv, const mr_driver_clazz_t *dclazz,
                        mr_uint16_t type, void *data, void *priv, void *ops);
```

- `drv`：驱动对象，由调用者分配存储。
- `dclazz`：驱动 clazz，提供 `init`/`deinit`/`release` 回调，不可为 `MR_NULL`。
- `type`：驱动大类型，不可为 `MR_DRIVER_TYPE_NONE`。
- `data` / `priv` / `ops`：驱动数据、私有数据与操作表，语义由具体外设驱动 约定，可为 `MR_NULL`。
- 返回：成功 `0`，失败负数错误码。

**`mr_driver_create`**

```c
mr_driver_t *mr_driver_create(const mr_driver_clazz_t *dclazz, mr_uint16_t type,
                              void *data, void *priv, void *ops);
```

参数含义同 `mr_driver_init`。成功返回驱动指针，内存不足返回 `MR_NULL`。

**`mr_driver_setup` / `mr_driver_shutdown`**

```c
mr_err_t mr_driver_setup(mr_driver_t *drv);
mr_err_t mr_driver_shutdown(mr_driver_t *drv);
```

`setup` 首次调用执行 clazz 的 `init` 并标记已 setup；重复调用幂等。
`shutdown` 执行 `deinit` 并清除 setup 标记；未 setup 时安全返回 0。若 `init`
返回非 0，setup 失败且驱动保持未 setup 状态。

**`mr_driver_del`**

```c
mr_err_t mr_driver_del(mr_driver_t *drv);
```

注销驱动。若驱动仍被其他持有者引用，`deinit` 与 `release` 推迟到引用归零时 执行。成功返回 `0`。

**`mr_driver_find`**

```c
mr_driver_t *mr_driver_find(const char *name);
```

- `name`：安装时的驱动名，不可为 `MR_NULL` 或空串。
- 返回：成功返回已 `get` 引用的驱动指针（用完需 `mr_driver_put`），未找到返回
  `MR_NULL`。
- 注意：只能找到经 `MR_DRIVER_INSTALL` 安装的驱动（`MR_DRIVER_DEFINE` 仅定义 对象、不生成注册条目），且 DEFER 级别的内部占位名不可被找到。

**`mr_driver_is_ready`**

```c
mr_bool_t mr_driver_is_ready(mr_driver_t *drv);
```

驱动非空、已初始化、已 setup、状态为 0 且未被标记删除（DYING）时返回
`MR_TRUE`，其余返回 `MR_FALSE`。对 `MR_NULL` 安全。

**`mr_driver_state_set` / `mr_driver_state`**

```c
void mr_driver_state_set(mr_driver_t *drv, mr_err_t errcode);
mr_err_t mr_driver_state(mr_driver_t *drv);
```

驱动状态只能为 0 或负数错误码（断言保护）。`setup`/`shutdown` 的返回值会自动 写入状态。

### 6. 完整示例

示例 1：动态创建驱动并完整走一遍生命周期（改编自 `mrX/test/drivers/driver.c`
的 `setup_shutdown_cycle`）：

```c
static mr_err_t test_init(const mr_driver_t *drv)   { return 0; }
static mr_err_t test_deinit(const mr_driver_t *drv) { return 0; }

static const mr_driver_clazz_t test_clazz = MR_DRIVER_CLAZZ_INIT(
    "test_driver", test_init, test_deinit, MR_NULL);

void lifecycle_example(void) {
    mr_driver_t *drv = mr_driver_create(&test_clazz, MR_DRIVER_TYPE_RAM,
                                        MR_NULL, MR_NULL, MR_NULL);
    if (drv == MR_NULL) {
        return;
    }

    mr_driver_setup(drv);       /* init runs once */
    mr_driver_setup(drv);       /* idempotent, still 0 */
    mr_driver_shutdown(drv);    /* deinit runs */
    mr_driver_setup(drv);       /* setup again is fine */

    mr_driver_del(drv);
}
```

示例 2：跨模块共享驱动指针，用引用计数保护（改编自测试
`delete_with_extra_reference`）：

```c
void reference_example(void) {
    mr_driver_t *drv = mr_driver_find("my_dev");
    mr_driver_t *held;

    if (drv == MR_NULL) {
        return;
    }
    held = mr_driver_get(drv);  /* Second holder keeps it alive */

    mr_driver_del(drv);         /* Mark dying; actual destroy deferred */
    mr_driver_put(drv);         /* Release find's reference */

    /* ... still safe to inspect held here ... */
    mr_driver_put(held);        /* Last reference: driver released now */
}
```

更多行为（setup 幂等、shutdown 失败保持 setup、无回调 clazz、状态读写）见
`mrX/test/drivers/driver.c`。

### 7. 常见问题（FAQ）

**Q：`mr_driver_setup` 返回 `-MR_EBUSY`？**

另一个执行流正在对该驱动做 `setup`/`shutdown`。框架用驱动内的锁标志保证同一 驱动的启停互斥，稍后重试即可；不要在 `init`
回调里再对同一驱动调用
`setup`/`shutdown`。

**Q：`mr_driver_find` 返回 `MR_NULL`，驱动明明写了？**

排查：驱动是否用 `MR_DRIVER_INSTALL` 安装（`MR_DRIVER_DEFINE` 定义的对象不在 查找段中）；名字拼写是否一致；GNU ld 风格链接脚本是否保留了
`mr_driver.*` 段（MDK 下注册条目带 `used` 属性，armlink 默认保留，见
[MDK 工程维护](../scripts/mdk/mdk.md) 的"注册段与链接器"一节）；该驱动 是否为 DEFER 级别的内部占位。

**Q：setup 失败后驱动处于什么状态？**

保持"未 setup"，`mr_driver_is_ready` 返回 `MR_FALSE`；失败错误码记录在驱动 状态中，可用 `mr_driver_state` 读取。修复硬件问题后可直接再次
`setup`。

**Q：`shutdown` 返回非 0 怎么办？**

`deinit` 失败时 setup 标记保持不变（驱动仍视为已 setup），失败码写入
`mr_driver_state`，因此 `mr_driver_is_ready` 为假；可再次尝试 `shutdown`。 若硬件实际仍可用，调用一次幂等 `setup` 会把状态清
0 并恢复 ready，不会重复 执行 `init`。对象删除时框架仍会再执行一次 `deinit` 兜底。

**Q：静态和动态方式怎么选？**

板上固定外设用 `MR_DRIVER_INSTALL`（注册 + 自动启动）；运行期才确定存在的 设备用 `mr_driver_create` / `mr_driver_del`。

---

## 开发者指南

### 8. 架构总览

驱动框架位于内核服务层之下、具体外设驱动之下层，是所有 `mrX/drivers/` 模块 的公共基类：

```
        +---------------------------------------------+
        |  应用 / 文件系统 / 上层模块                    |
        +----------------------+----------------------+
                               | mr_driver_find() / 类型化 API
        +----------------------v----------------------+
        |  外设驱动层：gpio / adc / dac / ram ...       |----> 各自 ops 分发
        +----------------------+----------------------+
                               | mr_driver_setup / shutdown / del
        +----------------------v----------------------+
        |  驱动框架 Driver（本模块）                     |
        |  对象引用计数 + 生命周期 + 注册段查找           |
        +----------------------+----------------------+
             依赖：对象系统（clazz/引用计数）、等待队列（Kconfig 自动选中）、
                   malloc（仅动态方式）、链接段（mr_driver.*）
```

关键接线：

- 内核初始化序列在 `MR_INIT_LEVEL_DRIVER`（"1.2"）调用
  `mr_drivers_driver_init`（`mrX/drivers/driver.c:299`，注册于
  `driver.c:319`），遍历链接段 `mr_driver.*` 中 start 到 defer 之间的条目， 逐个 `mr_driver_setup`。
- 链接脚本以 `KEEP(*(SORT(mr_driver.*)))` 收集注册条目并按级别名排序 （`user/linker.lds:69`
  ），这是"初始化级别决定启动顺序"的实现基础。
- 具体外设驱动通过 `MR_DRIVER_CLAZZES_DEFINE(gpio, ...)` 等宏在 driver clazz 之上叠加自己的
  clazz，形成"框架管生命周期、外设管硬件操作"的分工。

### 9. 数据结构

驱动对象（`mrX/include/mrX/drivers/mr_driver.h:79`）：

```c
typedef struct mr_driver {
    mr_object_t parent;             /* Object base: clazz + reference count */
    const mr_driver_clazz_t *clazz; /* Lifecycle callbacks */
    mr_atomic_t flags;              /* SETUP / LOCK / DYING bits */
    mr_uint16_t type;               /* Major type tag */
    mr_int16_t  state;              /* 0 or negative error code */
    mr_ptr_t    data;               /* Driver data (per-device model data) */
    mr_ptr_t    priv;               /* Private data (hardware instance) */
    mr_ptr_t    ops;                /* Operation table */
} mr_driver_t;
```

| 字段                    | 说明                                                                                                                  |
|-------------------------|-----------------------------------------------------------------------------------------------------------------------|
| `parent`                | 对象基类，提供类型检查与引用计数                                                                                      |
| `clazz`                 | 生命周期回调集，`init`/`deinit` 为 `mr_driver_method_t`，`release` 为 `mr_driver_release_t`                           |
| `flags`                 | 原子标志：bit0 `MR_DRIVER_FLAG_SETUP`、bit1 `MR_DRIVER_FLAG_LOCK`、bit2 `MR_DRIVER_FLAG_DYING`（`mr_driver.h:32-34`） |
| `type`                  | 大类型，高 8 位为类别（`MR_DRIVER_TYPE_MASK` = 0xff00），见第 14 节                                                   |
| `state`                 | 最近一次 setup/shutdown 的结果，0 或负错误码                                                                          |
| `data` / `priv` / `ops` | 三个通用指针槽位：外设驱动通常用 `data` 挂模型数据（框架级 release 负责释放）、`priv` 挂寄存器实例、`ops` 挂操作表    |

注册条目（`mr_driver.h:91`）：

```c
typedef struct mr_driver_entry {
    const char *name;
    mr_driver_t *drv;
} mr_driver_entry_t;
```

`MR_DRIVER_INSTALL`（`mr_driver.h:276`）同时生成驱动对象与该条目，并把条目 放入 `MR_USED const` +
`MR_SECTION_ALIGN("mr_driver." _level)` 修饰的段； 框架自身在 `mrX/drivers/driver.c:21-23` 安装了三个占位条目
start（"0.end"）、 defer（"8.end"）、end（"9.end"），用于在段内圈出"自动启动区"。

### 10. 核心流程

**自动启动**：`mr_drivers_driver_init`（`mrX/drivers/driver.c:299`）从
`start + 1` 遍历到 `defer`（不含），对每个条目断言 `drv` 非空后调用
`mr_driver_setup`，并以断言拦截非 0 返回。DEFER 级别（位于 defer 与 end 之间）的驱动不在此列。

**setup（`mrX/drivers/driver.c:118`）**：

1. `driver_lock`：原子获取 LOCK 标志（见第 11 节），失败返回 `-MR_EBUSY` 或
   `-MR_EPERM`。
2. 已有 SETUP 标志则将 state 置 0 后直接成功（幂等）。
3. clazz 或 `init` 为空则视为无需操作，直接标记 SETUP。
4. 调用 `drv->clazz->init(drv)`；返回 0 才置 SETUP 标志。
5. 未走幂等早退时，以返回值调用 `mr_driver_state_set` 写入状态，最后解锁。

**shutdown（`mrX/drivers/driver.c:181`）**：与 setup 对称——未 setup 直接 成功； clazz 无 `deinit` 直接清除 SETUP；`deinit` 返回
0 才清除 SETUP 标志， 失败则 SETUP 保持（`MR_DRIVER_IS_SETUP` 仍为真，见测试
`shutdown_fail_keeps_setup`），状态记录错误码；之后再调用一次 `setup` 会把 state 清 0 并恢复 `mr_driver_is_ready`，不重复
`init`。

**删除（`mrX/drivers/driver.c:77`）**：`mr_driver_del` 先原子置
`MR_DRIVER_FLAG_DYING`，再走对象删除通道 `mr_object_del`。对象引用计数未归零 时不立即释放；归零时触发 clazz 的 release 通道（
`driver.c:268`）：
`driver_obj_release` 先对仍处 SETUP 状态的驱动补一次 `deinit`（测试
`delete_with_extra_reference` 验证了 `deinit` 推迟到引用归零），再调用
`clazz->release` 归还外设驱动资源；动态 clazz 的 `driver_release` 额外
`mr_free` 驱动对象本身。

**查找（`mrX/drivers/driver.c:222`）**：线性扫描注册段，`mr_strcmp` 命中后 检查条目是否为内部 defer 占位（命中占位返回
`MR_NULL`），否则
`mr_driver_get` 返回带引用的驱动。段首 start 占位被 `start + 1` 跳过，end 占位在区间外，因此外部名字无法命中它们。

### 11. 并发与中断安全

- 并发控制不使用传统互斥锁，而是 `flags` 上的原子操作：`driver_lock`
  （`mrX/drivers/driver.c:90`）在 CAS 循环中检查 DYING/LOCK 后尝试置 LOCK，失败则重读重试；`driver_unlock`（`driver.c:113`）用
  `fetch_and` 清除。它保证同一驱动的 `setup`/`shutdown` 互斥。
- `init`/`deinit` 回调在持有 LOCK 标志期间同步执行：回调内不要再次对同一驱动调用
  `setup`/`shutdown`（会得到 `-MR_EBUSY`），也不要在 `del` 后继续使用该对象。
- 驱动框架不统一承诺各 API 都可在中断上下文调用。特别是动态创建、最后一次 `put`、删除后的最终释放都可能进入 `deinit`/`release`，动态对象还可能执行 `mr_free`；具体上下文约束应由调用者结合对象生命周期和后端实现判断。
- 操作类 API 入口通常以 `MR_DRIVER_ASSERT` 校验非空、对象已初始化、clazz 为 driver、已 setup
  且 major type 匹配；类型化 `*_del` 使用 `MR_DRIVER_ASSERT_TYPE`，不要求已 setup。断言关闭时这些编程错误不再由框架拦截，调用者必须保证指针来源正确。

### 12. 扩展与派生

外设驱动子模块的标准派生方式（以 [GPIO](gpio/gpio.md) 为例）：

1. 在子模块源文件中 `MR_DRIVER_CLAZZES_DEFINE(gpio, gpio_init, gpio_deinit,
   gpio_release)` 定义自己的静态/动态 clazz。子模块可在 `init`/`deinit` 中维护模型资源，例如 UART、WDT、TMR、I2C 都会初始化/销毁自己的 `mr_waitable_t`；动态 clazz 的 `release` 通常负责释放 `data`。
2. 提供类型化包装（`mr_gpio_init`/`mr_gpio_create` 等 `MR_INLINE` 函数），内部固定传入 `MR_DRIVER_TYPE_GPIO` 与子模块 clazz，让上层通过类型化入口操作驱动。
3. 操作表 `ops` 定义该外设的能力集。必需能力通常在包装层以 `MR_ASSERT` 要求非空；真正可选的能力允许置 `MR_NULL`，对应 API 再返回 `-MR_ENOSYS`。

硬件描述侧通过 hardtree（`mrX/include/mrX/drivers/mr_hardtree.h`）派生：
`.hts` 硬件模板经 `python ./scripts/cli.py htc` 生成 `mr_hardgen.h` 宏树， 驱动源文件定义 `MR_HT_COMPAT` 后用
`MR_HT_DRIVER_INSTALL`（`mr_driver.h:679`， 默认级别取节点的 `INIT_LEVEL` 属性，缺省 DEFER）按实例批量安装驱动；
`mr_driver.h:692` 对 DRIVER 兼容组的所有实例统一导出，免去了跨文件 import/export。具体外设 backend 的 HardTree、ops、IRQ/DMA 与平台资源接线不在驱动框架文档展开，请阅读对应驱动目录下的 `porting.md`，例如 [GPIO 移植](gpio/porting.md)、[ADC 移植](adc/porting.md) 和 [UART 移植](uart/porting.md)。

### 13. 源码导读

| 位置                                      | 职责                                        |
|-------------------------------------------|---------------------------------------------|
| `mrX/include/mrX/drivers/mr_driver.h:32`  | SETUP/LOCK/DYING 标志位定义                 |
| `mrX/include/mrX/drivers/mr_driver.h:44`  | 驱动大类型枚举                              |
| `mrX/include/mrX/drivers/mr_driver.h:79`  | `mr_driver_t` 结构体                        |
| `mrX/include/mrX/drivers/mr_driver.h:215` | 静态初始化宏 `MR_DRIVER_INIT`               |
| `mrX/include/mrX/drivers/mr_driver.h:276` | 注册安装宏 `MR_DRIVER_INSTALL`              |
| `mrX/include/mrX/drivers/mr_driver.h:505` | HardTree 驱动安装/查找宏                    |
| `mrX/drivers/driver.c:21`                 | start/defer/end 三个段内占位条目            |
| `mrX/drivers/driver.c:90`                 | 原子锁获取（setup/shutdown 互斥的基础）     |
| `mrX/drivers/driver.c:118`                | setup 流程（幂等、状态记录）                |
| `mrX/drivers/driver.c:181`                | shutdown 流程                               |
| `mrX/drivers/driver.c:222`                | 按名查找                                    |
| `mrX/drivers/driver.c:268`                | 引用归零后的释放通道（补 deinit + release） |
| `mrX/drivers/driver.c:299`                | 自动启动（`MR_INIT_LEVEL_DRIVER`）          |
| `user/linker.lds:69`                      | `mr_driver.*` 段收集与排序                  |
| `mrX/test/drivers/driver.c`               | 生命周期/引用计数/查找的行为级测试          |

### 14. 附录：配置宏与错误码

**配置宏**

| 宏                                             | 说明                                             |
|------------------------------------------------|--------------------------------------------------|
| `MR_USE_DRIVER`                                | 模块开关，默认 n，自动选中 `MR_USE_WAIT`         |
| `MR_DRIVER_FLAG_SETUP` / `_LOCK` / `_DYING`    | 驱动标志位                                       |
| `MR_DRIVER_LEVEL_EARLY/PRIO/NORMAL/LAST/DEFER` | 初始化级别字符串（"1"/"2.x"/"3"/"4"/"9"）        |
| `MR_DRIVER_TYPE_RAM/GPIO/UART/I2C/ADC/DAC/DMA/TMR/WDT/FLASH` | 大类型（0x0100 起，步进 0x0100）          |
| `MR_DRIVER_TYPE_MASK`                          | 类型掩码 0xff00，`MR_DRIVER_ASSERT` / `MR_DRIVER_ASSERT_TYPE` 用其比较类别 |

**错误码**：驱动框架核心的生命周期控制主要返回 `-MR_EBUSY`（驱动正被 setup/shutdown）与 `-MR_EPERM`（驱动正在删除）；具体外设包装层还可能返回 `-MR_EINVAL`、`-MR_ENOSYS` 等错误。错误码定义见 `mrX/include/mrX/libc/mr_errno.h`。空指针、类型不符等编程错误在断言开启时由 `MR_ASSERT` 拦截，而不是统一转换成返回值。

---

## 本目录其它驱动

驱动框架只提供生命周期、引用计数和硬件树实例化。具体外设的配置项、API 与事件语义 在各自的文档中：

| 驱动           | 文档                           | 对应外设类型                       |
|----------------|--------------------------------|------------------------------------|
| GPIO           | [gpio.md](gpio/gpio.md)             | `MR_DRIVER_TYPE_GPIO`              |
| UART           | [uart.md](uart/uart.md)             | `MR_DRIVER_TYPE_UART`              |
| I2C            | [i2c.md](i2c/i2c.md)               | `MR_DRIVER_TYPE_I2C`               |
| ADC            | [adc.md](adc/adc.md)               | `MR_DRIVER_TYPE_ADC`               |
| DAC            | [dac.md](dac/dac.md)               | `MR_DRIVER_TYPE_DAC`               |
| DMA            | [dma.md](dma/dma.md)               | `MR_DRIVER_TYPE_DMA`               |
| TMR        | [tmr.md](tmr/tmr.md)       | `MR_DRIVER_TYPE_TMR`           |
| WDT            | [wdt.md](wdt/wdt.md)               | `MR_DRIVER_TYPE_WDT`               |
| FLASH          | [flash.md](flash/flash.md)           | `MR_DRIVER_TYPE_FLASH`             |
| RAM 设备       | [ram.md](ram/ram.md)               | `MR_DRIVER_TYPE_RAM`（内存块设备） |
| HC165 输入扩展 | [gpio_hc165.md](gpio/gpio_hc165.md) | GPIO 端口（输入扩展芯片）          |
| HC595 输出扩展 | [gpio_hc595.md](gpio/gpio_hc595.md) | GPIO 端口（输出扩展芯片）          |

新增外设驱动时，先按本文的开发者指南实现 `MR_DRIVER_CLAZZES_DEFINE` 与 ops， 再参考类型最接近的上述文档补齐用户指南。

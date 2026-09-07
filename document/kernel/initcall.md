# 初始化调用（Initcall）

> 初始化调用提供基于链接器段收集的自动初始化机制：组件用 `MR_INIT_EXPORT` 把初始化
> 函数按优先级放入专用段，系统启动时按级别顺序自动执行，无需在任何地方手工罗列
> 初始化调用。

| 项       | 说明                                                                                                             |
|----------|------------------------------------------------------------------------------------------------------------------|
| 功能宏   | 无（始终编译；内核接线按 `MR_USE_*` 条件展开）                                                                   |
| 头文件   | `<mrX/kernel/mr_initcall.h>`                                                                                     |
| 依赖模块 | [平台 Platform](../platform/platform.md)、[定时器](timer.md)、[中断 Irq](irq.md)（按配置在内核初始化序列中接线） |
| 参考测试 | 无独立测试文件（各内核子模块的初始化由其模块测试间接覆盖）                                                       |

---

## 用户指南

### 1. 模块概述

初始化调用解决的问题是： **新增一个组件/驱动/应用模块时，不想去修改启动代码**。 模块在自己的源文件里用一行 `MR_INIT_EXPORT`
声明初始化函数与级别，链接器把所有 声明收集进同一个段并按级别排序，启动流程统一遍历执行。

特性：

- 声明式注册：一行宏完成初始化函数的导出，散布在各自源文件中，无中心注册表。
- 分级排序：内核服务（中断、文件系统、驱动、模块）与应用初始化按级别先后执行。
- 幂等保护：`mr_kernel_init` / `mr_user_init` 各自有状态标记，重复调用安全。
- 两段式启动：内核初始化（平台、堆、定时器、中断等硬编码序列）与用户初始化 （段收集的初始化函数）分离，顺序由框架保证。

不适用于：需要携带参数或返回错误中断启动流程的初始化（initcall 函数原型固定为
`void (*)(void)`）；对执行顺序有精细要求的场合只能通过级别字符串表达。

### 2. 配置与依赖

本模块自身无 Kconfig 选项，始终编译。`mrX/kernel/initcall.c` 中的内核初始化序列 按以下 `MR_USE_*` 条件接线（详见第 8 节）：

| 配置项            | 说明                                                             |
|-------------------|------------------------------------------------------------------|
| `MR_USE_PLATFORM` | 内核初始化时调用 `mr_platform_init`                              |
| `MR_USE_MALLOC`   | 内核初始化时调用 `mr_libc_malloc_init`                           |
| `MR_USE_TIMER`    | 内核初始化时调用 `mr_kernel_timer_init`（见 [定时器](timer.md)） |
| `MR_USE_IRQ`      | 内核初始化时调用 `mr_kernel_irq_init`                            |
| `MR_USE_FS`       | 内核扩展初始化时调用 `mr_fs_file_init`                           |

包含头文件即可使用：

```c
#include <mrX/kernel/mr_initcall.h>
```

### 3. 快速上手

让一个应用初始化函数在系统启动时自动执行：

```c
#include <mrX/kernel/mr_initcall.h>

static void app_init(void) {
    /* Runs automatically during mr_user_init() */
}

MR_INIT_EXPORT(app_init, MR_INIT_LEVEL_USER("0"));
```

系统启动走到 `mr_user_init()`（由入口模块自动调用，见 [入口 Entry](entry.md)， 或由 `main` 手工调用）时，`app_init`
会在所有内核服务与驱动就绪之后被执行。

### 4. 使用说明

**级别**：初始化函数按级别字符串排序执行，数值越小越先执行。框架预定义级别：

| 级别宏                       | 值           | 执行阶段                              |
|------------------------------|--------------|---------------------------------------|
| `MR_INIT_LEVEL_IRQ`          | `"1.0"`      | 中断管理初始化                        |
| `MR_INIT_LEVEL_FS`           | `"1.1"`      | 文件系统初始化                        |
| `MR_INIT_LEVEL_DRIVER`       | `"1.2"`      | 驱动框架初始化                        |
| `MR_INIT_LEVEL_MODULE`       | `"1.3"`      | 上层组件（RPC 等）初始化              |
| `MR_INIT_LEVEL_USER(_level)` | `"8."_level` | 用户应用初始化，`_level` 为自定义子级 |

约定：内核组件用预定义级别；应用代码用 `MR_INIT_LEVEL_USER("0")`、
`MR_INIT_LEVEL_USER("1")` 等表达相对顺序。

**生命周期与行为约定**

| 操作                                   | 行为                                                         |
|----------------------------------------|--------------------------------------------------------------|
| 重复调用 `mr_user_init`                | 安全，第二次起直接返回                                       |
| 重复调用 `mr_kernel_init`              | 安全，第二次起直接返回                                       |
| 只调用 `mr_kernel_init`                | 只执行内核初始化序列，不执行任何 `MR_INIT_EXPORT` 导出的函数 |
| initcall 函数中再调用 `mr_kernel_init` | 安全（幂等）                                                 |
| initcall 函数原型                      | 固定 `void (*)(void)`，无参数、无返回值                      |
| initcall 函数中的失败                  | 无错误传播通道，需自行处理（如置错误标志）                   |

**执行时机**：所有 initcall 在 `mr_user_init` 内串行执行，此时内核服务（堆、定时器、 中断等）已就绪；initcall 中可以使用堆与其他内核
API。

### 5. API 参考

初始化驱动：

| API               | 说明                                               |
|-------------------|----------------------------------------------------|
| `MR_INIT_EXPORT`  | 宏：导出一个初始化函数并指定级别                   |
| `MR_INIT_LEVEL_*` | 宏：预定义级别常量，见第 4 节表格                  |
| `mr_kernel_init`  | 执行内核初始化序列（用户无需调用，由入口自动完成） |
| `mr_user_init`    | 执行内核初始化与全部 initcall（启动入口调用）      |

**`MR_INIT_EXPORT`**

```c
#define MR_INIT_EXPORT(_entry, _level)                                         \
    MR_USED const mr_initcall_t MR_CAT(__mr_initcall_, _entry)                 \
        MR_SECTION_ALIGN("mr_initcall." _level)                                \
        = {.name = MR_STR(_entry), .entry = (_entry)}
```

- `_entry`：初始化函数名，原型 `void (*)(void)`，同一函数不可重复导出（符号冲突）。
- `_level`：级别字符串，使用 `MR_INIT_LEVEL_*` 宏。
- 效果：定义一个 `mr_initcall_t` 常量放入 `mr_initcall.<级别>` 段，链接时按段名排序 收集。

**`mr_kernel_init`**

```c
void mr_kernel_init(void);
```

执行内核硬编码初始化序列（平台 → 堆 → 定时器 → 中断 → 内核扩展），幂等。 Doxygen 注释明确"用户不需要调用此函数"。

**`mr_user_init`**

```c
void mr_user_init(void);
```

先完成 `mr_kernel_init`，再按级别顺序遍历执行段内全部 initcall，幂等。 若内核尚未初始化，会在此时被顺带启动。

### 6. 完整示例

示例 1：应用模块自动初始化（等同 `main` 之前完成）：

```c
#include <mrX/kernel/mr_initcall.h>

static int app_state;

static void app_init(void) {
    app_state = 1;
}

MR_INIT_EXPORT(app_init, MR_INIT_LEVEL_USER("0"));

void app_check(void) {
    /* Called after mr_user_init: app_state is 1 */
}
```

示例 2：手动分层的两个用户阶段（改编自框架内真实用法的级别形式， 如 `mrX/drivers/driver.c:315`、`mrX/modules/rpc/rpc.c:1383`
对内核级别的使用）：

```c
static void app_early_init(void) {
    /* Runs before app_late_init */
}
MR_INIT_EXPORT(app_early_init, MR_INIT_LEVEL_USER("0"));

static void app_late_init(void) {
    /* Can rely on things prepared by app_early_init */
}
MR_INIT_EXPORT(app_late_init, MR_INIT_LEVEL_USER("1"));
```

框架内的真实用例：`mr_drivers_driver_init`（`MR_INIT_LEVEL_DRIVER`，
`mrX/drivers/driver.c:315`）、`mr_fs_ramfs_init`（`MR_INIT_LEVEL_FS`，
`mrX/fs/ramfs/ramfs.c:447`）、`rpc_module_init`（`MR_INIT_LEVEL_MODULE`，
`mrX/modules/rpc/rpc.c:1383`）。

### 7. 常见问题（FAQ）

**Q：我的 initcall 没有被执行？**

按顺序排查：确认没有遗漏 `MR_INIT_EXPORT`（只定义函数不导出不会执行）；确认执行 路径上有人调用了 `mr_user_init`
（由入口模块自动完成，见 [入口 Entry](entry.md)， 或 `main` 中手工调用）；确认级别字符串合法（应使用 `MR_INIT_LEVEL_*` 宏）。

**Q：两个 initcall 的执行顺序不对？**

执行顺序由级别字符串的字典序决定。使用 `MR_INIT_LEVEL_USER("0")` ~
`MR_INIT_LEVEL_USER("9")` 这类单字符子级即可按预期排序。

**Q：initcall 里能调用内核 API 吗？**

能。initcall 执行前内核初始化序列已完成（堆、定时器、中断等已就绪）， initcall 中可以正常使用内核服务。

**Q：initcall 返回的错误怎么上报？**

无法上报：原型为 `void (*)(void)`，没有错误传播通道。初始化失败请自行记录 状态或直接触发断言。

**Q：`mr_kernel_init` 和 `mr_user_init` 该调用哪个？**

应用启动入口只需关心 `mr_user_init`（它内部会先执行 `mr_kernel_init`）。
`mr_kernel_init` 供只需要内核服务、不需要跑应用初始化的场合使用。

---

## 开发者指南

### 8. 架构总览

初始化调用是内核的启动骨架：它不做具体服务，而是规定"各服务在何时被点亮"。

```
        启动入口（entry / main）
                |
        mr_user_init()  ----------------------+  幂等（__initcall_ustatus）
                |
        mr_kernel_init()  ---+  幂等（__initcall_kstatus）
                |            |
                |   1. mr_platform_init()      (MR_USE_PLATFORM)
                |   2. mr_libc_malloc_init()   (MR_USE_MALLOC)
                |   3. mr_kernel_timer_init()  (MR_USE_TIMER)  -> [定时器](timer.md)
                |   4. mr_kernel_irq_init()    (MR_USE_IRQ)
                |   5. mr_fs_file_init()       (MR_USE_FS, 内核扩展)
                |
        遍历 mr_initcall.* 段（按级别字典序）
             IRQ -> FS -> DRIVER -> MODULE -> USER("x") -> ...
```

**内核子模块接线**：`mrX/kernel/initcall.c:35-69` 的 `mr_kernel_init` 是内核服务的 点亮顺序表。其中定时器的接线（
`initcall.c:56-60` 调用 `mr_kernel_timer_init`， 实现在 `mrX/kernel/timer.c:264`
）把到期检查函数注册为时钟钩子，是 [定时器](timer.md)
和 [时钟](clock.md) 之间的桥梁；中断、平台、堆同理。新增需要启动接线的内核模块时， 在此序列中按 `MR_USE_*` 守卫追加调用。

**段收集机制**：`MR_INIT_EXPORT` 把 `mr_initcall_t` 常量放入 `mr_initcall.<级别>`
段；链接脚本以 `KEEP(*(SORT(mr_initcall.*)))` 收集（宿主构建见
`user/linker.lds:64-65`，目标平台由移植层链接脚本提供同等规则，规则模板见
`mrX/sections.toml`）。`SORT` 保证 `mr_initcall.0.end` < `mr_initcall.1.0` < ... <
`mr_initcall.9.end` 的字典序执行。`mr_user_init` 通过链接器提供的边界符号
`__mr_initcall_start` / `__mr_initcall_end` 定界遍历（`initcall.c:72-73`）， 由平台链接脚本与段收集规则配套提供。

`mr_initcall.c` 内部还导出两个空函数作为段边界哨兵：`start`（`"0.end"`，
`initcall.c:20`）与 `end`（`"9.end"`，`initcall.c:25`），保证段首尾至少各有一个 条目、边界符号之间始终包含完整级别区间。

### 9. 数据结构

```c
typedef void(mr_initcall_entry_t)(void);        /* mr_initcall.h:31 */

typedef struct mr_initcall {                    /* mr_initcall.h:34 */
    const char *name;                           /* Entry name (for debug) */
    mr_initcall_entry_t *entry;                 /* Init function */
} mr_initcall_t;
```

| 字段    | 说明                                             |
|---------|--------------------------------------------------|
| `name`  | 函数名字符串，由 `MR_STR(_entry)` 生成，仅供诊断 |
| `entry` | 初始化函数指针，段内条目的有效载荷               |

段内条目常量由 `MR_INIT_EXPORT`（`mr_initcall.h:45`）以
`MR_USED`（防链接器丢弃）+ `MR_SECTION_ALIGN("mr_initcall."<级别>)`（指定段并对齐到 指针宽度，见
`mrX/include/mrX/libc/mr_compiler.h:33-34`）修饰。

**状态标记**：`__initcall_kstatus` / `__initcall_ustatus`（`initcall.c:14-15`）为
`volatile mr_bool_t`，不使用原子类型——注释说明其只在原子操作尚未就绪的极早期 执行，依赖单线程启动前提。

### 10. 核心流程

**`mr_kernel_init`**（`initcall.c:35`）：

1. 检查 `__initcall_kstatus`，已初始化直接返回；否则置位（先置位再执行， 保证初始化函数中重入安全）。
2. 按 `MR_USE_*` 依次调用平台、堆、定时器、中断的 `*_init`。
3. 调用 `kernel_extensions_init`（`initcall.c:27`），按 `MR_USE_FS` 执行文件系统 初始化。放在最后，因为文件系统依赖驱动与堆。

**`mr_user_init`**（`initcall.c:71`）：

1. `MR_ASSERT(start < end)` 校验链接器边界符号有效（`initcall.c:77`）。
2. 先调用 `mr_kernel_init`（`initcall.c:80`）：即使用户只调用了 `mr_user_init`，
   内核服务也会先就绪；这是"若内核尚未启动，将在此处启动"的实现。
3. 检查 `__initcall_ustatus` 幂等后，`for (call = start; call <= end; call++)`
   闭区间遍历段内条目并逐个执行 `call->entry()`（`initcall.c:91-96`）。

**为什么用级别字符串而不是整数**：级别编码在段名中，排序完全由链接器 `SORT` 在 链接期完成，运行期遍历零比较开销；代价是排序遵循字典序，因此级别宏的取值都设计为
固定位数（`"1.0"`、`"8.x"`、`"0.end"`）。

### 11. 并发与中断安全

- 整个初始化流程设计为单线程启动前提：状态标记是普通 `volatile` 布尔而非原子量， initcall 段常量位于只读段。不应在多线程环境中并发调用
  `mr_kernel_init` /
  `mr_user_init`。
- initcall 函数运行在线程上下文（启动调用者的上下文），非中断上下文；其中可以使用 阻塞之外的大多数内核
  API（此时调度设施是否可用取决于各服务的初始化完成情况， 内核服务在 initcall 之前已就绪）。
- initcall 之间串行执行，无需自行加锁保护"只在本模块初始化中触碰"的数据。

### 12. 源码导读

| 位置                                      | 职责                                     |
|-------------------------------------------|------------------------------------------|
| `mrX/include/mrX/kernel/mr_initcall.h:24` | 级别宏定义（IRQ/FS/DRIVER/MODULE/USER）  |
| `mrX/include/mrX/kernel/mr_initcall.h:34` | `mr_initcall_t` 段条目结构               |
| `mrX/include/mrX/kernel/mr_initcall.h:45` | `MR_INIT_EXPORT` 导出宏                  |
| `mrX/kernel/initcall.c:14`                | 内核/用户初始化状态标记                  |
| `mrX/kernel/initcall.c:17-25`             | 段首尾哨兵 `start` / `end`               |
| `mrX/kernel/initcall.c:35`                | `mr_kernel_init`：内核服务点亮序列       |
| `mrX/kernel/initcall.c:56`                | 定时器接线（`mr_kernel_timer_init`）     |
| `mrX/kernel/initcall.c:71`                | `mr_user_init`：幂等 + 段遍历            |
| `user/linker.lds:65`                      | 宿主链接脚本的段收集规则                 |
| `mrX/sections.toml`                       | 段规则模板（`mr_initcall.*` -> `.text`） |

### 13. 附录

**配置宏**

| 宏                                                                                         | 说明                                       |
|--------------------------------------------------------------------------------------------|--------------------------------------------|
| `MR_INIT_LEVEL_IRQ` / `MR_INIT_LEVEL_FS` / `MR_INIT_LEVEL_DRIVER` / `MR_INIT_LEVEL_MODULE` | 内核预定义级别（`"1.0"` ~ `"1.3"`）        |
| `MR_INIT_LEVEL_USER(_level)`                                                               | 用户级别，`"8."<子级>`                     |
| `MR_USE_PLATFORM` / `MR_USE_MALLOC` / `MR_USE_TIMER` / `MR_USE_IRQ` / `MR_USE_FS`          | 决定 `mr_kernel_init` 中接线的内核模块开关 |

**错误码**：本模块 API 无返回值、无失败路径；链接期错误（边界符号缺失、段未收集） 表现为链接失败或 `mr_user_init` 中的
`MR_ASSERT(start < end)` 断言失败。

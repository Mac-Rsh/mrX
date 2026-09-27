# 初始化调用（Initcall）

> 初始化调用提供基于链接器段收集的自动初始化机制：组件用 `MR_INIT_EXPORT` 把初始化
> 函数按优先级放入专用段，系统启动时按级别顺序自动执行，无需在任何地方手工罗列
> 初始化调用。

| 项       | 说明                                                                                                             |
|----------|------------------------------------------------------------------------------------------------------------------|
| 功能宏   | 无（始终编译；内核接线按 `MR_USE_*` 条件展开）                                                                   |
| 头文件   | `<mrX/kernel/mr_initcall.h>`                                                                                     |
| 依赖模块 | [平台 Platform](../platform/platform.md)、[定时器](timer.md)、[中断 Irq](irq.md)（按配置在内核初始化序列中接线） |
| 参考测试 | `mrX/test/kernel/initcall.c`                                                                                     |

---

## 用户指南

### 1. 模块概述

初始化调用解决的问题是： **新增一个组件/驱动/应用模块时，不想去修改启动代码**。 模块在自己的源文件里用一行 `MR_INIT_EXPORT`
声明初始化函数与级别，链接器把所有 声明收集进同一个段并按级别排序，启动流程统一遍历执行。

特性：

- 声明式注册：一行宏完成初始化函数的导出，散布在各自源文件中，无中心注册表。
- 分级排序：所有 `MR_INIT_EXPORT` 条目按段名排序；IRQ/FS/DRIVER/MODULE/USER 只是这些导出条目的排序区间，不等同于
  `mr_kernel_init` 中的硬编码初始化函数。
- 启动期幂等：`mr_kernel_init` / `mr_user_init` 各有普通 `volatile` 状态标记，在单执行流启动假设下重复调用会直接返回。
- 两段式启动：`mr_kernel_init` 先运行平台/堆/定时器/中断/文件系统核心等硬编码接线；`mr_user_init` 再遍历全部导出的
  initcall。

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

只有执行到 `mr_user_init()` 时，`app_init` 才会运行。当前 [入口 Entry](entry.md) 只自动调用 `mr_kernel_init()`，不会自动调用
`mr_user_init()`；应用需要在 `main` 或自己的启动接线中显式执行它。用户级 `8.x` 条目会排在预定义的 `1.x`
IRQ/FS/DRIVER/MODULE 导出条目之后。

### 4. 使用说明

**级别**：初始化函数按级别字符串排序执行，数值越小越先执行。框架预定义级别：

| 级别宏                       | 值           | 执行阶段                             |
|------------------------------|--------------|--------------------------------------|
| `MR_INIT_LEVEL_IRQ`          | `"1.0"`      | initcall 排序区间：IRQ 类导出条目    |
| `MR_INIT_LEVEL_FS`           | `"1.1"`      | initcall 排序区间：FS 类导出条目     |
| `MR_INIT_LEVEL_DRIVER`       | `"1.2"`      | initcall 排序区间：Driver 类导出条目 |
| `MR_INIT_LEVEL_MODULE`       | `"1.3"`      | initcall 排序区间：Module 类导出条目 |
| `MR_INIT_LEVEL_USER(_level)` | `"8."_level` | 用户导出条目，`_level` 为自定义子级  |

约定：内核组件用预定义级别；应用代码用 `MR_INIT_LEVEL_USER("0")`、
`MR_INIT_LEVEL_USER("1")` 等表达相对顺序。

**生命周期与行为约定**

| 操作                                   | 行为                                                         |
|----------------------------------------|--------------------------------------------------------------|
| 启动期串行重复调用 `mr_user_init`      | 第二次起不再遍历 initcall                                    |
| 启动期串行重复调用 `mr_kernel_init`    | 第二次起直接返回                                             |
| 只调用 `mr_kernel_init`                | 只执行内核初始化序列，不执行任何 `MR_INIT_EXPORT` 导出的函数 |
| initcall 函数中再调用 `mr_kernel_init` | 安全（幂等）                                                 |
| initcall 函数原型                      | 固定 `void (*)(void)`，无参数、无返回值                      |
| initcall 函数中的失败                  | 无错误传播通道，需自行处理（如置错误标志）                   |

**执行时机**：所有导出的 initcall（包括 `MR_INIT_LEVEL_IRQ` 等 `1.x` 条目）都在 `mr_user_init` 内按链接顺序串行执行，并且
`mr_user_init` 会先调用 `mr_kernel_init`。因此相应 `MR_USE_*` 已启用的硬编码内核服务会先完成初始化；未启用的服务不会凭
initcall 自动出现。

### 5. API 参考

初始化驱动：

| API               | 说明                                                            |
|-------------------|-----------------------------------------------------------------|
| `MR_INIT_EXPORT`  | 宏：导出一个初始化函数并指定级别                                |
| `MR_INIT_LEVEL_*` | 宏：预定义级别常量，见第 4 节表格                               |
| `mr_kernel_init`  | 执行硬编码内核初始化序列；Entry 可自动调用                      |
| `mr_user_init`    | 确保 kernel init 后执行全部段内 initcall；需由应用/启动接线调用 |

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

执行硬编码初始化序列：按配置依次调用 platform、malloc、timer、irq，最后进入 `kernel_extensions_init`（当前在 `MR_USE_FS` 时调用
`mr_fs_file_init`）。状态位在执行这些步骤之前先置真，因此同一启动调用链里的重入会直接返回。

**`mr_user_init`**

```c
void mr_user_init(void);
```

先完成 `mr_kernel_init`，再按级别顺序遍历执行段内全部 initcall，幂等。 若内核尚未初始化，会在此时被顺带启动。

### 6. 完整示例

示例 1：应用模块在 `mr_user_init()` 遍历时初始化：

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

示例 2：手动分层的两个用户阶段（改编自框架内真实用法的级别形式， 如 `mrX/drivers/driver.c:319`、`mrX/modules/rpc/rpc.c:1385`
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

框架内各组件也使用 `MR_INIT_LEVEL_FS` / `DRIVER` / `MODULE` 等排序标签导出自己的初始化入口。它们不是 `mr_kernel_init`
的硬编码步骤，而是在后续 `mr_user_init` 段遍历中执行。

### 7. 常见问题（FAQ）

**Q：我的 initcall 没有被执行？**

按顺序排查：确认没有遗漏 `MR_INIT_EXPORT`（只定义函数不导出不会执行）；确认执行 路径上有人调用了 `mr_user_init`
（当前 Entry 不会代调，见 [入口 Entry](entry.md)）；确认级别字符串落在 start/end 哨兵之间，通常直接使用 `MR_INIT_LEVEL_*` 宏。

**Q：两个 initcall 的执行顺序不对？**

执行顺序由级别字符串的字典序决定。使用 `MR_INIT_LEVEL_USER("0")` ~
`MR_INIT_LEVEL_USER("9")` 这类单字符子级即可按预期排序。

**Q：initcall 里能调用内核 API 吗？**

可以使用已经被配置并由 `mr_kernel_init` 启动的服务。`mr_user_init` 确实先完成 kernel init，但 malloc/timer/irq/platform
等各项是否存在仍取决于对应 `MR_USE_*` 配置；initcall 同步运行在 `mr_user_init` 的当前调用上下文。

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
        Entry 可调用 mr_kernel_init()；main/启动代码按需调用 mr_user_init()
                |
        mr_user_init()  ----------------------+  启动期状态短路（__initcall_ustatus）
                |
        mr_kernel_init()  ---+  启动期状态短路（__initcall_kstatus）
                |            |
                |   1. mr_platform_init()      (MR_USE_PLATFORM)
                |   2. mr_libc_malloc_init()   (MR_USE_MALLOC)
                |   3. mr_kernel_timer_init()  (MR_USE_TIMER)  -> [定时器](timer.md)
                |   4. mr_kernel_irq_init()    (MR_USE_IRQ)
                |   5. mr_fs_file_init()       (MR_USE_FS, 内核扩展)
                |
        遍历 mr_initcall.* 段（按级别字典序）
         start(0.end) -> 1.0 -> 1.1 -> 1.2 -> 1.3 -> 8.x -> end(9.end)
```

**内核子模块接线**：`mrX/kernel/initcall.c:35-69` 的 `mr_kernel_init` 是内核服务的 点亮顺序表。其中定时器的接线（
`initcall.c:56-60` 调用 `mr_kernel_timer_init`， 实现在 `mrX/kernel/timer.c:264`
）把到期检查函数注册为时钟钩子，是 [定时器](timer.md)
和 [时钟](clock.md) 之间的桥梁；中断、平台、堆同理。新增需要启动接线的内核模块时， 在此序列中按 `MR_USE_*` 守卫追加调用。

**段收集机制**：`MR_INIT_EXPORT` 把 `mr_initcall_t` 常量放入 `mr_initcall.<级别>`
段；链接脚本以 `KEEP(*(SORT(mr_initcall.*)))` 收集（宿主构建见
`user/linker.lds:64-65`，目标平台由移植层链接脚本提供同等规则，规则模板见
`mrX/sections.toml`）。`SORT` 保证 `mr_initcall.0.end` < `mr_initcall.1.0` < ... <
`mr_initcall.9.end` 的字典序执行。`mr_user_init` 通过
`__mr_initcall_start` / `__mr_initcall_end` 定界遍历（`initcall.c:72-73`）。这两个符号不是链接器生成的， 而是本文件哨兵条目
`start` / `end` 的变量名本身（`MR_CAT(__mr_initcall_, start/end)`），链接脚本只负责收集与排序，
MDK（armlink）依赖其默认的按名连续放置即可，无需额外配置。

`mrX/kernel/initcall.c` 内部还导出两个空函数作为段边界哨兵：`start`（`"0.end"`，
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
3. 调用 `kernel_extensions_init`（当前仅在 `MR_USE_FS` 时调用 `mr_fs_file_init`）。这是硬编码 kernel extension 步骤，与后续段内
   `MR_INIT_LEVEL_FS` 条目是两套机制。

**`mr_user_init`**（`initcall.c:71`）：

1. `MR_ASSERT(start < end)` 校验链接器边界符号有效（`initcall.c:78`）。
2. 先调用 `mr_kernel_init`（`initcall.c:81`），确保启用的硬编码内核服务先执行。
3. 检查 `__initcall_ustatus`，首次调用先置真；随后按 `sizeof(mr_initcall_t)` 从 `start` 地址到 `end`
   地址闭区间遍历，连两个空哨兵也会执行，并逐个调用 `call->entry()`。

**级别字符串边界**：排序由链接器按段名字典序完成，运行时不比较 level。`mr_user_init` 只遍历 `start`（`0.end`）到 `end`（
`9.end`）之间的连续条目，因此自定义段名必须在字典序上落在这两个哨兵之间；使用预定义 `1.x` / `8.x` 宏最稳妥。不要使用会排到
`0.end` 之前或 `9.end` 之后的自定义级别。

### 11. 并发与中断安全

- 整个初始化流程设计为单线程启动前提：状态标记是普通 `volatile` 布尔而非原子量， initcall 段常量位于只读段。不应在多线程环境中并发调用
  `mr_kernel_init` /
  `mr_user_init`。
- initcall 函数同步运行在 `mr_user_init` 的调用者上下文；模块本身不会强制切换到“线程上下文”。实际设计应在正常启动路径中执行，而不要并发或从不合适的中断上下文触发整条用户初始化序列。
- initcall 之间串行执行，无需自行加锁保护"只在本模块初始化中触碰"的数据。

### 12. 扩展与派生

本模块没有对象 clazz 与派生机制；它的"扩展"是 **向初始化序列注入条目**，注入面是编译期与链接期而非运行期继承：

- 组件侧：一行 `MR_INIT_EXPORT(_entry, _level)`（`mrX/include/mrX/kernel/mr_initcall.h:45`）即把自己挂进 `mr_initcall.<级别>`
  段，用户区段可用 `MR_INIT_LEVEL_USER("0")`、`"1"` 表达子级与相对顺序；测试也验证了 1.x 先于 8.x、8.0 先于 8.1。若确实自定义
  level，必须同时满足链接脚本收集规则，并保证字典序位于 `0.end` 与 `9.end` 哨兵之间；模块没有运行期级别注册表。
- 内核侧：新增需要启动接线的内核模块，是在 `kernel_extensions_init` 中按 `MR_USE_*` 守卫追加调用（
  `mrX/kernel/initcall.c:27-33`、`:67`），而不是派生 `mr_initcall_t`；派生类扩展无从谈起——`mr_initcall_t` 只有 `name`/
  `entry` 两个字段，无虚表、无钩子、无注销接口（`mr_initcall.h:34`）。

### 13. 源码导读

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

### 14. 附录

**配置宏**

| 宏                                                                                         | 说明                                       |
|--------------------------------------------------------------------------------------------|--------------------------------------------|
| `MR_INIT_LEVEL_IRQ` / `MR_INIT_LEVEL_FS` / `MR_INIT_LEVEL_DRIVER` / `MR_INIT_LEVEL_MODULE` | 内核预定义级别（`"1.0"` ~ `"1.3"`）        |
| `MR_INIT_LEVEL_USER(_level)`                                                               | 用户级别，`"8."<子级>`                     |
| `MR_USE_PLATFORM` / `MR_USE_MALLOC` / `MR_USE_TIMER` / `MR_USE_IRQ` / `MR_USE_FS`          | 决定 `mr_kernel_init` 中接线的内核模块开关 |

**错误码**：本模块 API 为 `void`，没有错误码传播通道。链接/段布局错误可能表现为链接失败、`MR_ASSERT(start < end)` 或遍历到无效
`entry` 时的断言；某个 initcall 自身失败也不会由框架自动中止并上报。

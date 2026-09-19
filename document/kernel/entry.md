# 入口（Entry）

> 入口模块把工具链/板级入口接到 `mr_kernel_init()`：在用户 `main` 之前完成内核级初始化。
> 用户 `MR_INIT_EXPORT` 项仍由 `mr_user_init()` 执行，不属于 Entry 自动阶段。

| 项       | 说明                                                                                         |
|----------|----------------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_ENTRY`（默认关闭，`mrX/include/mrX/kernel/Kconfig:6`）                               |
| 头文件   | 无独立头文件；`mr_kernel_init` / `mr_user_init` 声明于 `<mrX/kernel/mr_initcall.h>`，`mr_entry` 当前没有公共声明 |
| 依赖模块 | [初始化调用 Initcall](initcall.md)（启动序列的执行者）                                       |
| 参考测试 | 无独立测试文件（宿主应用入口见工作区根 `user/main.c`，调用点 `user/main.c:1251`）             |

---

## 用户指南

### 1. 模块概述

入口模块解决的问题是：**把 `mr_kernel_init()` 接到用户 `main` 之前**。启用 `MR_USE_ENTRY` 后，Entry 先执行内核初始化，再进入用户 `main`；它不会执行 `mr_user_init()` 的用户 initcall 遍历。未启用时，应用或启动代码需要自行决定何时调用 `mr_kernel_init()` / `mr_user_init()`。

特性：

- 对 ARMCC（`__ARMCC_VERSION`）通过 `$Sub$$main` / `$Super$$main` 自动包裹用户
  `main`，应用零改动。
- 对其他工具链提供 `mr_entry()` 函数，由板级启动代码在跳转 `main` 前调用。
- 手工入口场景可以在真实 `main` 中调用 `mr_user_init()`；该函数会先补做 `mr_kernel_init()`，再执行用户 initcall。

适用场景：目标板固件（希望内核服务先于应用就绪）、需要统一启动流程的产品工程。 不适用于：完全不用内核服务的纯应用（保持
`MR_USE_ENTRY` 关闭即可，不引入任何 启动开销）。

### 2. 配置与依赖

| 配置项         | 类型 | 默认值 | 说明                                                                                |
|----------------|------|--------|-------------------------------------------------------------------------------------|
| `MR_USE_ENTRY` | bool | n      | 启用入口点支持，提供标准化初始化与启动流程（`mrX/include/mrX/kernel/Kconfig:6-11`） |

包含头文件：

```c
#include <mrX/kernel/mr_initcall.h>
```

启用 `MR_USE_ENTRY` 并重新生成 `mr_config.h` 后，`mrX/kernel/entry.c` 参与编译并 提供启动接线；未启用时该文件整体编译为空，仅剩手工初始化路径。

### 3. 快速上手

目标板场景（启用 `MR_USE_ENTRY`，应用代码无需任何启动调用）：

```c
/* Board toolchain is ARMCC: $Sub$$main wraps user main automatically.
 * For other toolchains, call mr_entry() from the board startup code
 * before jumping to main. */
int main(void) {
    /* mr_kernel_init() has run; user MR_INIT_EXPORT entries have not. */
    return 0;
}
```

宿主模拟场景（`user/main.c` 的写法，未依赖工具链入口包裹）：

```c
#include <mrX/kernel/mr_initcall.h>

int main(void) {
    mr_user_init();     /* Kernel init + all initcalls */
    /* Application body */
    return 0;
}
```

### 4. 使用说明

**两种启动路径**

| 场景                              | 启动方式                                             |
|-----------------------------------|------------------------------------------------------|
| ARMCC 目标板 + `MR_USE_ENTRY`     | 自动：`$Sub$$main` 先执行内核启动，再进入用户 `main` |
| 其他工具链目标板 + `MR_USE_ENTRY` | 半自动：板级启动代码在 `main` 前调用 `mr_entry()`    |
| 宿主模拟 / 手工控制               | 手工：在 `main` 开头调用 `mr_user_init()`            |

**行为约定**

| 操作                                            | 行为                                                                   |
|-------------------------------------------------|------------------------------------------------------------------------|
| `mr_entry()` 重复调用                           | 每次都会再次调用 `main()`；只有其中的 `mr_kernel_init()` 有状态短路 |
| 未启用 `MR_USE_ENTRY` 且不手工初始化            | 不会自动执行 `mr_kernel_init()`；依赖显式初始化的服务不会被启动       |
| `main` 中再调 `mr_kernel_init`                  | 已初始化时直接返回（启动期单线程假设）                                |
| `main` 中调用 `mr_user_init`                    | 先确保 kernel init，再且仅一次遍历用户 initcall                       |
| `main` 返回                                     | `$Sub$$main` / `mr_entry` 都忽略真实 `main` 返回值并返回 0             |

**与初始化调用的分工**：入口模块只负责"在正确时机点火"；启动内容（内核服务顺序、
`MR_INIT_EXPORT` 导出的应用初始化）由 [初始化调用 Initcall](initcall.md) 定义。

### 5. API 参考

启动接口（定义于 `mrX/kernel/entry.c`，声明随 [初始化调用 Initcall](initcall.md)）：

| API              | 说明                                                                               |
|------------------|------------------------------------------------------------------------------------|
| `mr_entry`       | 非 ARMCC 分支定义的入口符号：执行 kernel init 后调用 `main`；当前无公共头文件声明 |
| `mr_kernel_init` | 执行内核初始化序列（被 Entry 调用）                                               |
| `mr_user_init`   | 先执行/确认 kernel init，再遍历链接区中的全部 initcall                            |

**`mr_entry`**

```c
int mr_entry(void);
```

- 无参数。
- 行为：调用 `mr_kernel_init()`，随后调用用户 `main()`，忽略 `main` 的返回值并固定返回 0。
- 注意：仅在启用 `MR_USE_ENTRY` 且工具链非 ARMCC 时定义；当前公共头文件没有声明该符号，属于启动接线接口而不是普通应用 API。ARMCC 分支改用 `$Sub$$main`。

**`mr_kernel_init` / `mr_user_init`**

原型与行为见 [初始化调用 Initcall](initcall.md) 的 API 参考。入口模块是它们的 自动调用方，接口本身由初始化调用模块提供。

### 6. 完整示例

示例 1：宿主模拟环境的完整启动（取自工作区根 `user/main.c` 的实际结构，`user/main.c:1248-1267`）：

```c
#include <stdlib.h>
#include <mrX.h>

int main(void) {
    int ret;

    mr_user_init();     /* Kernel init + all MR_INIT_EXPORT initcalls */
    ret = 0;
#if defined(MR_USE_DRIVER_DMA)
    ret = demo_dma_run();
#endif /* defined(MR_USE_DRIVER_DMA) */
#if defined(MR_USE_MODULE_RPC)
    if (ret == 0) {
        ret = demo_rpc_run();
    }
#else
#if !defined(MR_USE_DRIVER_DMA)
    mr_printf("RPC is disabled
");
    ret = 1;
#endif /* !defined(MR_USE_DRIVER_DMA) */
#endif /* defined(MR_USE_MODULE_RPC) */
    exit(ret);
}
```

宿主入口是完整应用入口，而不是测试专用入口：`mr_user_init()` 之后由应用自己决定做什么， 这份示例工程按配置运行
DMA 与 RPC 演示。启用测试框架时，测试用例通过 `MR_INIT_EXPORT` 或应用 自己调用测试运行入口执行，与入口模块无关。

示例 2：Entry 自动完成 kernel init 后，在 `main` 中显式执行用户 initcall：

```c
#include <mrX/kernel/mr_initcall.h>

static void app_init(void) {
    /* Runs after kernel services are ready, before main's body */
}

MR_INIT_EXPORT(app_init, MR_INIT_LEVEL_USER("0"));

int main(void) {
    /* Entry has already called mr_kernel_init(). */
    mr_user_init();     /* Executes app_init and other exported initcalls once. */

    for (;;) {
        /* Main loop */
    }
    return 0;
}
```

### 7. 常见问题（FAQ）

**Q：启用 `MR_USE_ENTRY` 后 `main` 里还要调 `mr_user_init` 吗？**

如果应用需要执行 `MR_INIT_EXPORT` 的用户 initcall，仍然需要某处调用 `mr_user_init()`。`$Sub$$main` / `mr_entry` 只调用 `mr_kernel_init()`；它们并不会替你运行用户 initcall。

**Q：不启用 `MR_USE_ENTRY` 会缺什么？**

`mrX/kernel/entry.c` 不参与编译，内核不会自动启动；只要应用在合适时机调用
`mr_kernel_init`（只要内核服务）或 `mr_user_init`（内核服务 + 全部 initcall）， 功能完全等价。

**Q：`mr_entry` 和 `mr_user_init` 是什么关系？**

`mr_entry` 面向非 ARMCC 的启动接线：先 kernel init，再直接调用 `main`。`mr_user_init` 则先确保 kernel init，然后遍历 initcall 链接区。二者共享 `mr_kernel_init()`，但只有后者执行用户 initcall。

**Q：`main` 之前的启动代码里能调用内核 API 吗？**

只有启动序列完成之后才保证可用。ARMCC 路径下进入 `main` 即已完成；手工路径下 在调用 `mr_kernel_init` 返回之前不可依赖内核服务。

---

## 开发者指南

### 8. 架构总览

入口模块是启动链条的最前端，把工具链入口与内核初始化序列衔接起来：

```
        工具链入口
   +---------+----------------------+
   | ARMCC   | 其他工具链 / 宿主      |
   +----+----+----------+-----------+
        |               |
  $Sub$$main()     mr_entry()        (mrX/kernel/entry.c)
   自动包裹          板级启动代码调用
        |               |
        +-------+-------+
                |
          entry()                       (entry.c:13)
                |
        mr_kernel_init()                (mrX/kernel/initcall.c:35)
          平台 -> 堆 -> 定时器 -> 中断 -> 内核扩展
                |
        （手工路径：mr_user_init() 再执行全部 initcall，
          见 [初始化调用 Initcall](initcall.md)）
                |
             用户 main
```

- ARMCC 分支（`entry.c:19-28`）：`$Sub$$main` 先执行 `entry()` 再 `$Super$$main`
  转入用户 `main`，对应用透明。
- 非 ARMCC 分支（`entry.c:30-38`）：导出 `mr_entry()`，由板级启动代码在跳转
  `main` 前调用；`entry()` 只执行 `mr_kernel_init()`，随后由 `main` 决定是否继续
  `mr_user_init` 跑应用 initcall。
- 宿主模拟（本仓库）不使用工具链包裹，`main` 开头调用 `mr_user_init`
  （`user/main.c:1251`），等价于"入口 + 用户初始化"一步完成。

### 9. 数据结构

本模块无数据结构：`entry.c` 仅含一个静态函数 `entry`（`entry.c:13`）与两个入口
分支，全部状态（初始化幂等标记）归属 [初始化调用 Initcall](initcall.md) 的
`__initcall_kstatus` / `__initcall_ustatus`。

### 10. 核心流程

**编译开关**：`entry.c` 首行包含 `mrX/mr_config.h` 并以
`#if defined(MR_USE_ENTRY)` 包裹全部实现（`entry.c:9-10`），未启用时编译单元为空， 不产生任何符号与代码。

**ARMCC 包裹机制**：`$Sub$$main` 是 ARM 链接器的符号重定向特性——对符号 `main` 的 所有引用被改向 `$Sub$$main`，函数内通过
`$Super$$main` 调用原始 `main`。入口模块 利用它把内核启动插入 `main` 之前（`entry.c:19-28`），用户代码无需任何改动。

**非 ARMCC 路径**：C 语言没有标准的前置钩子，因此内核只提供 `mr_entry()` 函数， 把"何时调用"交给板级启动代码（复位向量 →
启动汇编 → `mr_entry` → `main` 的 约定），保持内核对具体启动文件的零侵入。

**启动内容的复用**：`entry()` 只调用 `mr_kernel_init`（`entry.c:15`），不执行
`mr_user_init` 的 initcall 遍历——目标板上 `main` 之后的初始化策略（是否、何时跑 应用 initcall）留给应用/启动代码决定；宿主环境需要完整启动，因此
`main` 直接调用
`mr_user_init`（内部已含 `mr_kernel_init`，幂等保证不会重复初始化）。

### 11. 并发与中断安全

- Entry 设计基于启动阶段的单执行流假设。`mr_kernel_init` 的状态只是 `volatile mr_bool_t`，不是并发一次性初始化原语。
- `mr_entry` / `$Sub$$main` 都应只作为一次启动入口使用。重复调用 `mr_entry` 虽然不会重复 kernel init，但会再次进入用户 `main()`，不能描述为“无副作用”。
- 启动完成后的并发安全性由被调用的内核模块自身保证，与本模块无关。

### 12. 扩展与派生

入口模块没有 clazz、操作集或回调注册点：`entry.c` 里既无对象也无注册表，只有静态 函数 `entry()`（`mrX/kernel/entry.c:13`
）与两个入口分支，无运行期可替换接口。 工具链接入靠 **编译期分支**扩展：新增工具链须在 `__ARMCC_VERSION` 判定 （
`entry.c:19-39`）中加一支——ARMCC 用 `$Sub$$main` / `$Super$$main` 包裹 （`entry.c:20-28`），其余工具链导出 `mr_entry()`
由板级启动代码调用（`entry.c:30-38`）。

启动内容不在此处扩展：`entry()` 只点火 `mr_kernel_init()`（`entry.c:13-16`），
内核服务顺序固定归 [初始化调用 Initcall](initcall.md)（`initcall.c:35-69`）；应用侧 启动内容用
`MR_INIT_EXPORT(_entry, _level)`（`mr_initcall.h:45`）挂接、由
`mr_user_init` 遍历（`initcall.c:92-98`），级别常量见 `mr_initcall.h:24-28`（用例
`mrX/test/kernel/initcall.c:34`）。宿主/手工路径即在 `main` 中直接调用
`mr_user_init`（`user/main.c:1251`）。

### 13. 源码导读

| 位置                               | 职责                                     |
|------------------------------------|------------------------------------------|
| `mrX/include/mrX/kernel/Kconfig:6` | `MR_USE_ENTRY` 配置定义（默认 n）        |
| `mrX/kernel/entry.c:9-10`          | 编译开关与配置头引入                     |
| `mrX/kernel/entry.c:13`            | `entry()`：内核启动的统一点火点          |
| `mrX/kernel/entry.c:19-28`         | ARMCC `$Sub$$main` 自动包裹分支          |
| `mrX/kernel/entry.c:30-38`         | 非 ARMCC `mr_entry()` 分支               |
| `mrX/kernel/initcall.c:35`         | `mr_kernel_init`：实际启动序列           |
| `user/main.c:1251`                  | 宿主模拟的手工启动调用点（工作区根目录） |

### 14. 附录

**配置宏**

| 宏                | 说明                                                         |
|-------------------|--------------------------------------------------------------|
| `MR_USE_ENTRY`    | 模块开关，默认 n；启用后 `entry.c` 提供自动启动接线          |
| `__ARMCC_VERSION` | 工具链探测宏（编译器预定义），选择包裹分支或 `mr_entry` 分支 |

**返回值**：`mr_entry` 与 `$Sub$$main` 都固定返回 0，并不会传播用户 `main()` 的返回值。`mr_kernel_init` / `mr_user_init` 为 `void`，内核初始化子步骤也没有通过 Entry 聚合错误码。

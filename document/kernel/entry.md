# 入口（Entry）

> 入口模块为内核提供标准化的启动接线：在用户 `main` 之前自动完成内核初始化，
> 使"上电即有内核服务"成为默认行为，应用代码无需关心启动顺序。

| 项       | 说明                                                                                         |
|----------|----------------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_ENTRY`（默认关闭，`mrX/include/mrX/kernel/Kconfig:6`）                               |
| 头文件   | 无独立头文件（对外接口为 `<mrX/kernel/mr_initcall.h>` 的 `mr_kernel_init` / `mr_user_init`） |
| 依赖模块 | [初始化调用 Initcall](initcall.md)（启动序列的执行者）                                       |
| 参考测试 | 无独立测试文件（宿主入口见 `user/main.c`）                                                   |

---

## 用户指南

### 1. 模块概述

入口模块解决的问题是： **内核服务的初始化应该在应用代码运行前自动完成**。启用
`MR_USE_ENTRY` 后，内核在工具链的入口机制上"抢先"执行启动序列，再交还控制权给 用户 `main`；未启用时，应用需在 `main`
中自行调用初始化接口。

特性：

- 对 ARMCC（`__ARMCC_VERSION`）通过 `$Sub$$main` / `$Super$$main` 自动包裹用户
  `main`，应用零改动。
- 对其他工具链提供 `mr_entry()` 函数，由板级启动代码在跳转 `main` 前调用。
- 宿主模拟环境（如本仓库的 x64 主机框架）`main` 即真实入口，由它在 `main` 开头 调用 `mr_user_init` 达到相同效果。

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
    /* Kernel services (heap, timer, irq, ...) are ready here */
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
| `mr_entry()` 重复调用                           | 内核初始化幂等（由 [初始化调用 Initcall](initcall.md) 保证），无副作用 |
| 未启用 `MR_USE_ENTRY` 且不手工初始化            | 内核服务不可用（堆、定时器等未就绪）                                   |
| `main` 中再调 `mr_user_init` / `mr_kernel_init` | 安全，幂等                                                             |
| 启用 `MR_USE_ENTRY` 后 `main` 返回              | 内核不接管 `main` 返回后的行为，与无内核时一致                         |

**与初始化调用的分工**：入口模块只负责"在正确时机点火"；启动内容（内核服务顺序、
`MR_INIT_EXPORT` 导出的应用初始化）由 [初始化调用 Initcall](initcall.md) 定义。

### 5. API 参考

启动接口（定义于 `mrX/kernel/entry.c`，声明随 [初始化调用 Initcall](initcall.md)）：

| API              | 说明                                                                               |
|------------------|------------------------------------------------------------------------------------|
| `mr_entry`       | 内核入口函数：执行内核启动后调用用户 `main`（非 ARMCC 工具链，由板级启动代码调用） |
| `mr_kernel_init` | 执行内核初始化序列（被 `mr_entry` 调用）                                           |
| `mr_user_init`   | 执行内核初始化与全部 initcall（宿主/手工路径使用）                                 |

**`mr_entry`**

```c
int mr_entry(void);
```

- 无参数。
- 行为：执行内核启动（`mr_kernel_init`），随后调用用户 `main` 并返回其结果语境下的
  `0`（`mrX/kernel/entry.c:31-38`）。
- 注意：仅在启用 `MR_USE_ENTRY` 且工具链非 ARMCC 时存在；ARMCC 工具链改用
  `$Sub$$main` 包裹，无需（也不应）调用 `mr_entry`。

**`mr_kernel_init` / `mr_user_init`**

原型与行为见 [初始化调用 Initcall](initcall.md) 的 API 参考。入口模块是它们的 自动调用方，接口本身由初始化调用模块提供。

### 6. 完整示例

示例 1：宿主模拟环境的完整启动（取自 `user/main.c` 的结构）：

```c
#include <mrX/test/mr_test.h>
#include <stdlib.h>
#include <mrX.h>

int main(void) {
    int ret;

    mr_user_init();     /* Kernel init + all MR_INIT_EXPORT initcalls */

    ret = (mr_test_run() == MR_TRUE) ? 0 : 1;
    exit(ret);
}
```

示例 2：配合自动初始化的目标板应用（启用 `MR_USE_ENTRY`，应用只写业务）：

```c
#include <mrX/kernel/mr_initcall.h>

static void app_init(void) {
    /* Runs after kernel services are ready, before main's body */
}

MR_INIT_EXPORT(app_init, MR_INIT_LEVEL_USER("0"));

int main(void) {
    /* Kernel and app_init are already done here (ARMCC path) */
    for (;;) {
        /* Main loop */
    }
    return 0;
}
```

### 7. 常见问题（FAQ）

**Q：启用 `MR_USE_ENTRY` 后 `main` 里还要调 `mr_user_init` 吗？**

ARMCC 目标板不需要（`$Sub$$main` 已在 `main` 前完成启动）；其他工具链取决于板级 启动代码是否调用了 `mr_entry`；宿主模拟环境的
`main` 即真实入口，需要按
`user/main.c` 的方式手工调用。

**Q：不启用 `MR_USE_ENTRY` 会缺什么？**

`mrX/kernel/entry.c` 不参与编译，内核不会自动启动；只要应用在合适时机调用
`mr_kernel_init`（只要内核服务）或 `mr_user_init`（内核服务 + 全部 initcall）， 功能完全等价。

**Q：`mr_entry` 和 `mr_user_init` 是什么关系？**

`mr_entry` 面向工具链入口机制（启动代码/入口包裹调用），内部执行内核启动后转入
`main`；`mr_user_init` 面向已进入 `main` 之后的场景。二者都最终触发同一条幂等的 内核初始化序列。

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
- 宿主模拟（本仓库）不使用工具链包裹，`main` 第一行调用 `mr_user_init`
  （`user/main.c:8`），等价于"入口 + 用户初始化"一步完成。

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

- 入口函数设计为单线程启动前提下的最早执行点，只做一次幂等的内核初始化调用， 与中断无交互。
- `mr_entry` / `$Sub$$main` 均不可重入语义：应在启动上下文调用且仅调用一次 （重复调用因初始化幂等而无害，但没有意义）。
- 启动完成后的并发安全性由被调用的内核模块自身保证，与本模块无关。

### 12. 源码导读

| 位置                               | 职责                              |
|------------------------------------|-----------------------------------|
| `mrX/include/mrX/kernel/Kconfig:6` | `MR_USE_ENTRY` 配置定义（默认 n） |
| `mrX/kernel/entry.c:9-10`          | 编译开关与配置头引入              |
| `mrX/kernel/entry.c:13`            | `entry()`：内核启动的统一点火点   |
| `mrX/kernel/entry.c:19-28`         | ARMCC `$Sub$$main` 自动包裹分支   |
| `mrX/kernel/entry.c:30-38`         | 非 ARMCC `mr_entry()` 分支        |
| `mrX/kernel/initcall.c:35`         | `mr_kernel_init`：实际启动序列    |
| `user/main.c:8`                    | 宿主模拟的手工启动调用点          |

### 13. 附录

**配置宏**

| 宏                | 说明                                                         |
|-------------------|--------------------------------------------------------------|
| `MR_USE_ENTRY`    | 模块开关，默认 n；启用后 `entry.c` 提供自动启动接线          |
| `__ARMCC_VERSION` | 工具链探测宏（编译器预定义），选择包裹分支或 `mr_entry` 分支 |

**错误码**：本模块无返回值语义（`mr_entry` 恒以 `0` 收尾进入/转出 `main`）， 启动失败路径表现为被调用模块的错误码或断言。

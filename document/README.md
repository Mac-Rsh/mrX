# mrX 文档规范

本文档定义 mrX 项目文档的目录组织、标准骨架与写作约定。所有模块文档（用户篇 + 开发者篇）
均按此规范编写；第一篇示例文档见 [kernel/timer.md](kernel/timer.md)。

---

## 1. 文档组织

文档目录与源码目录一一对应，每篇文档对应一个功能模块：

```
document/
├── README.md            # 本文件：文档规范与总索引
├── kernel/              # 内核模块
├── libc/                # C 库
├── drivers/             # 设备驱动框架
├── modules/             # 上层组件
├── fs/                  # 文件系统
├── platform/            # 平台移植层
├── test/                # 测试框架
└── scripts/             # 主机 Python CLI
```

命名约定：

- 文档文件名与对应源文件同名（如 `kernel/timer.md` 对应 `kernel/timer.c`）。
- 一篇文档对应一个 `MR_USE_*` 功能宏，文档开头必须标注该宏；无功能宏的模块 标注"无（始终编译）"。
- `README.md` 维护总索引，新增文档后同步更新索引表。

### 总索引

| 目录      | 文档                                             | 对应源码                        |
|-----------|--------------------------------------------------|---------------------------------|
| kernel/   | [时钟 Clock](kernel/clock.md)                    | `mrX/kernel/clock.c`            |
| kernel/   | [入口 Entry](kernel/entry.md)                    | `mrX/kernel/entry.c`            |
| kernel/   | [FIFO](kernel/fifo.md)                           | `mrX/kernel/fifo.c`             |
| kernel/   | [哈希表 Hashmap](kernel/hashmap.md)              | `mrX/kernel/hashmap.c`          |
| kernel/   | [钩子 Hook](kernel/hook.md)                      | `mrX/kernel/hook.c`             |
| kernel/   | [初始化调用 Initcall](kernel/initcall.md)        | `mrX/kernel/initcall.c`         |
| kernel/   | [中断 Irq](kernel/irq.md)                        | `mrX/kernel/irq.c`              |
| kernel/   | [日志 Log](kernel/log.md)                        | `mrX/kernel/log.c`              |
| kernel/   | [对象系统 Object](kernel/object.md)              | `mrX/kernel/object.c`           |
| kernel/   | [格式化输出 Printf](kernel/printf.md)            | `mrX/kernel/printf.c`           |
| kernel/   | [内存块分配器 Slab](kernel/slab.md)              | `mrX/kernel/slab.c`             |
| kernel/   | [软定时器 Timer](kernel/timer.md)                | `mrX/kernel/timer.c`            |
| kernel/   | [等待队列 Wait](kernel/wait.md)                  | `mrX/kernel/wait.c`             |
| kernel/   | [工作队列 Work](kernel/work.md)                  | `mrX/kernel/work.c`             |
| libc/     | [内存分配 Malloc](libc/malloc.md)                | `mrX/libc/malloc.c`             |
| libc/     | [格式化 Sprintf](libc/sprintf.md)                | `mrX/libc/sprintf.c`            |
| libc/     | [格式化解析 Sscanf](libc/sscanf.md)              | `mrX/libc/sscanf.c`             |
| libc/     | [字符串 String](libc/string.md)                  | `mrX/libc/string.c`             |
| drivers/  | [ADC 驱动](drivers/adc.md)                       | `mrX/drivers/adc/adc.c`         |
| drivers/  | [DAC 驱动](drivers/dac.md)                       | `mrX/drivers/dac/dac.c`         |
| drivers/  | [驱动框架 Driver](drivers/driver.md)             | `mrX/drivers/driver.c`          |
| drivers/  | [GPIO 驱动](drivers/gpio.md)                     | `mrX/drivers/gpio/gpio.c`       |
| drivers/  | [HC165 并行输入扩展 GPIO](drivers/gpio_hc165.md) | `mrX/drivers/gpio/gpio_hc165.c` |
| drivers/  | [HC595 并行输出扩展 GPIO](drivers/gpio_hc595.md) | `mrX/drivers/gpio/gpio_hc595.c` |
| drivers/  | [RAM 设备](drivers/ram.md)                       | `mrX/drivers/ram/ram.c`         |
| fs/       | [文件系统 VFS](fs/fs.md)                         | `mrX/fs/file.c` 等              |
| fs/       | [RAM 文件系统 RamFS](fs/ramfs.md)                | `mrX/fs/ramfs/ramfs.c`          |
| fs/       | [根文件系统 RootFS](fs/rootfs.md)                | `mrX/fs/rootfs/rootfs.c`        |
| modules/  | [调试器 Dbg](modules/dbg.md)                     | `mrX/modules/xvm/dbg.c`         |
| modules/  | [GDB 桩 Gdb](modules/gdb.md)                     | `mrX/modules/xvm/gdb.c`         |
| modules/  | [远程过程调用 RPC](modules/rpc.md)               | `mrX/modules/rpc/rpc.c` 等      |
| modules/  | [虚拟机 XVM](modules/xvm.md)                     | `mrX/modules/xvm/xvm.c` 等      |
| platform/ | [平台移植层 Platform](platform/platform.md)      | `mrX/platform/`                 |
| test/     | [测试框架 Test](test/test.md)                    | `mrX/test/test.c`               |
| scripts/  | [命令行 CLI](scripts/cli.md)                     | `mrX/scripts/cli.py`            |

---

## 2. 标准骨架

每篇模块文档固定分为两大部分： **前半部分面向用户（讲使用），后半部分面向开发者（讲原理）**。 两部分之间以水平分割线与醒目标题隔开，读者可按需停读。

````markdown
# 模块名（英文标识符）

> 一句话概述：本模块是什么、解决什么问题。

| 项       | 说明                                         |
|----------|----------------------------------------------|
| 功能宏   | `MR_USE_XXX`                                 |
| 头文件   | `<mrX/xxx/mr_xxx.h>`                         |
| 依赖模块 | [模块 A](../xxx/a.md)、[模块 B](../xxx/b.md) |

---

## 用户指南

### 1. 模块概述

特性列表、适用场景、与相邻模块的分工边界。

### 2. 配置与依赖

Kconfig 选项、默认值、相关 `MR_CFG_*` 参数及含义。

### 3. 快速上手

一段可直接编译运行的最小示例，覆盖最典型用法。

### 4. 使用说明

面向使用者的行为约定：工作模式、生命周期、回调上下文、可/不可在回调中做的事。

### 5. API 参考

按功能分组的 API 表格 + 逐个 API 的原型、参数、返回值、注意事项。 描述必须与头文件 Doxygen 注释一致。

### 6. 完整示例

2~3 个典型场景示例（静态 / 动态、常见组合用法），取材或改编自 `mrX/test/`。

### 7. 常见问题（FAQ）

以问答形式列出易错点与排查清单。

---

## 开发者指南

### 8. 架构总览

模块在内核中的位置、上下依赖关系、对外提供的内部接口。

### 9. 数据结构

核心结构体逐字段说明（可引用头文件行号），关键宏的编码方式。

### 10. 核心流程

初始化 / 启动 / 到期处理等主要流程的实现，配文字时序或简易图。
关键实现点（如回绕安全比较、锁粒度）单独小节解释"为什么这样写"。

### 11. 并发与中断安全

锁的保护范围、回调的执行上下文、哪些调用点可重入。

### 12. 扩展与派生

如何基于本模块派生子类/复用机制（若适用）。

### 13. 源码导读

`文件:行号 -> 职责` 对照表，方便直接跳转阅读。

### 14. 附录

配置宏一览、错误码。
````

---

## 3. 写作约定

### 3.1 两部分的措辞边界

- **用户篇**只讲"做什么、怎么调、行为是什么"，禁止暴露内部实现（不出现链表、锁、 源码行号）。用户不需要知道原理也能正确使用。
- **开发者篇**讲"怎么实现、为什么这样实现"，允许且鼓励引用 `文件:行号`，方便跳转。
- 行为约定只写一次：若某行为对两类读者都重要（如回调上下文限制），用户篇写"是什么"， 开发者篇解释"为什么"，开发者篇不重复使用说明。

### 3.2 语言与术语

- 正文使用中文，代码、API 名、宏名、配置项保持英文原样，不翻译。
- API 首次出现给出完整原型，此后使用短名。
- 术语全项目统一：`tick`（时钟节拍）、`回调`（entry/callback）、`静态初始化`
  （栈上/全局对象 + `*_INIT` 宏）、`动态创建`（`*_create` 堆分配）。 新术语先查本表，不在表中则在此处补充。

### 3.3 代码示例

- 示例必须来自 `mrX/test/` 或可按现有 API 直接编译，禁止虚构 API。
- 最小示例只保留说明主题所需的代码，错误处理可省略但需注明。
- 示例中涉及的 `MR_USE_*` 宏必须在"配置与依赖"一节可查。

### 3.4 API 表格

- 按"初始化与销毁 / 控制 / 参数与查询 / 引用管理"等功能分组，每组一个表格。
- 表格列固定为：API | 说明；细节（参数、返回值、注意事项）放表格下方的逐个小节。
- 返回值统一写法：`0` 表示成功；负数错误码表示失败（并给出 `mr_errno` 名称）； 指针返回 `MR_NULL` 表示失败。

### 3.5 交叉引用

- 引用其他模块文档使用相对路径：`[时钟](clock.md)`。
- 引用源码使用 `` `mrX/kernel/timer.c:98` `` 格式（文件:行号）。
- 行号随代码演进会失效，开发者篇引用时以函数名为锚、行号仅为辅助。

### 3.6 维护

- 修改模块 API 或行为时，必须同步更新对应文档（视为功能修改的一部分）。
- 行为未定的实现细节不写入文档；文档中不得出现"大概、可能、应该"等含糊表述描述 已实现行为。

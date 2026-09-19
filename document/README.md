# mrX 文档入口

> 这是整套文档的起点，也是唯一的目录页。它只回答三个问题：mrX 是什么、你该从哪篇开始读、其他文档放在哪。
> 第一次接触 mrX，顺着往下看十分钟就能走通；已经熟悉项目、只想查某个模块，直接跳到下面的索引表即可。

## 1. 三分钟认识 mrX

mrX 不是那种“下载下来就能跑出一个程序”的工程，而是一套可以直接嵌进你自己工程的内核与组件集合。

- 你的应用工程还是你的，业务代码留在你自己的目录里。
- mrX 以一个依赖的形式被接进去，提供内核服务、C 库、设备驱动、文件系统、RPC 等能力。
- 它需要一份运行环境——tick、中断、输出、断言这些底层动作由 **平台适配层**提供。
- 接入前你会生成两个头文件：`mr_config.h` 记录你启用了哪些功能，`mr_hardgen.h` 描述你的板级硬件。
- 之后在自己的入口调用一次 `mr_user_init()`，框架就起来了；剩下的按需查模块文档。

把这条线记住就够了： **选平台 → 配置功能 → 描述硬件 → 链接注册段 → 调用 `mr_user_init()`**。

## 2. 选一条阅读路线

### 路线 A：第一次把 mrX 接进应用（最常见）

按顺序读，不要跳：

1. [框架使用指南 Usage](usage.md) —— 完整跑一遍接入流程，这是主菜。
2. [平台移植层 Platform](platform/platform.md) 的“选择并安装已有平台”一节 —— 挑一个和目标环境匹配的适配。
3. 回到 [Usage](usage.md)，按 Kconfig → HTC → sections → 启动的顺序做完。
4. 接入成功之后，再按需要翻对应模块的用户指南。

### 路线 B：目标芯片还没有现成平台

1. 先读 [Platform](platform/platform.md) 的新平台移植部分，把 `mr_port_*` 接口实现出来。
2. 再走路线 A，用你自己刚做好的平台完成接入。

### 路线 C：给 mrX 加功能或改实现

1. 先看该模块文档的 **用户指南**，确认你理解的是对外约定的行为。
2. 再进同一篇文档的 **开发者指南**，看架构、数据结构和扩展方式。
3. 要扩展命令行工具时，读 [CLI 总览](scripts/cli/cli.md)；插件必须在 `scripts/plugins/catalog.py` 里显式登记。

### 路线 D：只是想查一个 API

直接从第 5 节的索引跳进对应模块文档，用页面内搜索找函数名即可，不必从 Usage 读起。

## 3. 按目标找文档

不知道该看哪篇时，从左边一列找你的目标：

| 你的目标                          | 看这篇                                                                 |
|-----------------------------------|------------------------------------------------------------------------|
| 第一次把 mrX 接入应用             | [框架使用指南](usage.md)                                               |
| 选择、安装已有平台                | [平台移植层：使用已有平台](platform/platform.md#11-选择并安装已有平台) |
| 为新芯片开发平台适配              | [平台移植层：开发新平台](platform/platform.md#13-新平台的移植范围)     |
| 为平台实现某个外设 backend         | [设备驱动移植入口](#设备驱动)                                         |
| 验收自研平台适配                  | [平台移植层：适配验收清单](platform/platform.md#121-适配验收清单)      |
| 配置功能、生成 `mr_config.h`      | [框架使用指南：Kconfig](usage.md#3-使用-kconfig-配置功能)              |
| 描述板级硬件、生成 `mr_hardgen.h` | [框架使用指南：HTC](usage.md#4-htc-硬件描述编译)                       |
| 处理 initcall、驱动或 RPC 注册段  | [框架使用指南：注册段](usage.md#5-注册段与-sections)                   |
| 查一个模块的 API                  | 从下面的功能索引进入对应模块文档                                       |
| 阅读或修改框架实现                | 进入对应模块文档的“开发者指南”                                         |
| 使用主机端 CLI 工具               | [主机脚本教程](scripts/README.md)                                      |
| 了解测试框架                      | [测试框架 Test](test/test.md)                                          |

## 4. 文档放在哪

模块文档按源码职责分目录，通常与源码目录保持对应——`kernel/timer.md` 讲的就是 `kernel/timer.c`；RPC 和 XVM 文档集中在 `modules/` 下的对应目录：

```text
mrX/document/
├── README.md         # 你现在看的这篇：入口与索引
├── usage.md          # 接入与快速开始（主流程都在这）
├── documentation.md  # 文档编写与维护规范（只给维护者看）
├── kernel/           # 内核服务：时钟、中断、日志、定时器、对象系统……（有目录入口）
├── libc/             # C 库：内存分配、字符串、格式化输入输出
├── drivers/          # 设备驱动：驱动框架、GPIO、UART、ADC/DAC、扩展芯片
├── fs/               # 文件系统：VFS、RamFS、RootFS
├── modules/          # 上层组件：RPC、XVM（含调试器、GDB 桩）
├── platform/         # 平台移植层：安装已有平台、移植新平台
├── scripts/          # 主机端 Python 工具（kconfig、htc、sections……）（有目录入口）
└── test/             # 测试框架
```

部分目录带一个入口文件，从它进目录比直接点某个模块文件更省事：
[kernel/README.md](kernel/README.md)（内核服务总览）、[scripts/README.md](scripts/README.md)、
[platform/platform.md](platform/platform.md)、[test/test.md](test/test.md)。

`libc/`、`drivers/`、`fs/`、`modules/` 没有独立入口文件，直接看第 5 节索引即可； 其中 `drivers/driver.md`
末尾列有本目录全部驱动，可作为驱动的起点。

## 5. 完整文档索引

### 框架接入与工具

| 文档                             | 解决的问题                                                                               |
|----------------------------------|------------------------------------------------------------------------------------------|
| [Usage](usage.md)                | 框架接入的最短流程、Kconfig UI、HTC、sections、初始化和排查                              |
| [Platform](platform/platform.md) | 已有平台安装、`install.toml`、新芯片移植和 `mr_port_*` 契约                              |
| [脚本教程](scripts/README.md)    | CLI 入口及 `kconfig`、`htc`、`platform`、`sections`、`rpc-idl`、`mdk`、`echo` 的详细用法 |
| [Test](test/test.md)             | 测试框架、测试用例注册和运行方式                                                         |

### 内核服务

| 模块              | 文档                          | 模块                | 文档                              |
|-------------------|-------------------------------|---------------------|-----------------------------------|
| 时钟 Clock        | [clock.md](kernel/clock.md)   | 入口 Entry          | [entry.md](kernel/entry.md)       |
| FIFO              | [fifo.md](kernel/fifo.md)     | 哈希表 Hashmap      | [hashmap.md](kernel/hashmap.md)   |
| 钩子 Hook         | [hook.md](kernel/hook.md)     | 初始化调用 Initcall | [initcall.md](kernel/initcall.md) |
| 中断 Irq          | [irq.md](kernel/irq.md)       | 日志 Log            | [log.md](kernel/log.md)           |
| 对象系统 Object   | [object.md](kernel/object.md) | 格式化输出 Printf   | [printf.md](kernel/printf.md)     |
| 内存块分配器 Slab | [slab.md](kernel/slab.md)     | 软定时器 Timer      | [timer.md](kernel/timer.md)       |
| 等待队列 Wait     | [wait.md](kernel/wait.md)     | 工作队列 Work       | [work.md](kernel/work.md)         |

### C 库

| 模块              | 文档                        | 模块           | 文档                          |
|-------------------|-----------------------------|----------------|-------------------------------|
| 内存分配 Malloc   | [malloc.md](libc/malloc.md) | 格式化 Sprintf | [sprintf.md](libc/sprintf.md) |
| 格式化解析 Sscanf | [sscanf.md](libc/sscanf.md) | 字符串 String  | [string.md](libc/string.md)   |

### 设备驱动

| 模块            | 文档                                   | 模块           | 文档                                   |
|-----------------|----------------------------------------|----------------|----------------------------------------|
| 驱动框架 Driver | [driver.md](drivers/driver.md)         | GPIO           | [gpio.md](drivers/gpio/gpio.md)             |
| UART            | [uart.md](drivers/uart/uart.md)       | I2C            | [i2c.md](drivers/i2c/i2c.md)                |
| ADC             | [adc.md](drivers/adc/adc.md)         | DAC            | [dac.md](drivers/dac/dac.md)                |
| RAM 设备        | [ram.md](drivers/ram/ram.md)         | DMA            | [dma.md](drivers/dma/dma.md)                |
| TMR             | [tmr.md](drivers/tmr/tmr.md)         | WDT            | [wdt.md](drivers/wdt/wdt.md)                |
| FLASH           | [flash.md](drivers/flash/flash.md)   |                |                                              |
| HC165 输入扩展  | [gpio_hc165.md](drivers/gpio/gpio_hc165.md) | HC595 输出扩展 | [gpio_hc595.md](drivers/gpio/gpio_hc595.md) |

平台 backend 开发请阅读对应驱动目录下的 `porting.md`：
[GPIO](drivers/gpio/porting.md)、[UART](drivers/uart/porting.md)、[I2C](drivers/i2c/porting.md)、[ADC](drivers/adc/porting.md)、[DAC](drivers/dac/porting.md)、[DMA](drivers/dma/porting.md)、[TMR](drivers/tmr/porting.md)、[WDT](drivers/wdt/porting.md)、[RAM](drivers/ram/porting.md)、[FLASH](drivers/flash/porting.md)。

### 文件系统

| 模块              | 文档                      | 模块               | 文档                    |
|-------------------|---------------------------|--------------------|-------------------------|
| 文件系统 VFS      | [fs.md](fs/fs.md)         | RAM 文件系统 RamFS | [ramfs.md](fs/ramfs.md) |
| 根文件系统 RootFS | [rootfs.md](fs/rootfs.md) |                    |                         |

### 上层模块

| 模块             | 文档                     | 模块       | 文档                     |
|------------------|--------------------------|------------|--------------------------|
| 远程过程调用 RPC | [rpc.md](modules/rpc/rpc.md) | 虚拟机 XVM | [xvm.md](modules/xvm/xvm.md) |
| 调试器 Dbg       | [dbg.md](modules/xvm/dbg.md) | GDB 桩 Gdb | [gdb.md](modules/xvm/gdb.md) |

## 6. 阅读方法：每篇模块文档怎么用

模块文档统一按 **用户指南 → 开发者指南**组织，所以拿到任何一篇都可以这样读：

驱动目录下的 `porting.md` 是例外：它只面向平台 backend 开发者，直接按移植步骤组织，不再重复用户指南。

- **前半部分（用户指南）**回答“怎么用”：模块做什么、依赖什么、怎么配、API 长什么样、完整例子、常见问题。 只写应用的话，读到这里就够了。
- **后半部分（开发者指南）**回答“为什么这样做、怎么扩展”：架构、数据结构、核心流程、并发与中断安全、源码导读。
  只在排查问题或者要改框架时才需要往下读。
- 文档里出现的 `MR_USE_*` 是功能开关，`MR_CFG_*` 是资源参数，都在 Kconfig 里配置； 每篇文档开头的表格会告诉你它依赖哪个功能宏和哪个头文件。
- 示例都取自 `mrX/test/` 或能直接按公开 API 编译，看到例子可以直接抄。

## 7. 卡住了怎么办

- **接入阶段报错**：先看 [Usage 的常见问题排查](usage.md#7-常见问题排查)，它按平台、配置、生成、初始化四个阶段给了排查顺序。
- **某个模块行为不对**：进对应模块文档的用户指南，先看 FAQ 一节。
- **命令行工具用不起来**：看 [脚本教程](scripts/README.md)，里面区分了“用户只需执行 `cli.py`”和“库代码不要直接运行”。
- **文档本身有问题**：修改规则见 [文档编写与维护规范](documentation.md)。

## 8. 文档维护

文档编写规则、模块文档骨架、示例约束和维护要求单独放在
[文档编写与维护规范](documentation.md)。它是维护者参考，不是 mrX 使用者的入门材料。

新增、删除或重命名文档时，请同步更新本文第 4 节的目录说明和第 5 节的索引。

# mrX 文档入口

> 这是整套文档的起点，也是唯一的目录页：mrX 是什么、每类任务该读哪篇、文档放在哪。
> 不知道从哪开始就顺着往下看；只想查某个模块，直接跳到第 3 节的索引。

mrX 是一套可以直接嵌进你自己工程的内核与组件集合：应用工程还是你的，业务代码留在你自己的目录里，mrX
以依赖的形式接进来，提供内核服务、C 库、设备驱动、文件系统、RPC 等能力。tick、中断、输出、断言这些底层动作由 **平台适配层**
提供。接入时先生成记录启用功能的 `mr_config.h`，使用硬件树时再生成描述板级硬件的
`mr_hardgen.h`，最后在应用入口调用一次 `mr_user_init()`，框架就起来了。完整接入流程见
[框架使用指南](usage.md)。

## 1. 按目标找文档

从左边一列找你的目标：

| 你的目标                          | 看这篇                                                                           |
|-----------------------------------|----------------------------------------------------------------------------------|
| 第一次把 mrX 接入应用             | [框架使用指南](usage.md)                                                         |
| 选择、安装已有平台                | [平台移植层：使用已有平台](platform/platform.md#11-选择并安装已有平台)           |
| 为新芯片开发平台适配              | [平台移植层：新平台的移植范围](platform/platform.md#13-新平台的移植范围)         |
| 为平台实现某个外设 backend        | 对应驱动目录下的 `porting.md`，索引见 [设备驱动](#设备驱动)                      |
| 验收自研平台适配                  | [平台移植层：适配验收清单](platform/platform.md#121-适配验收清单)                |
| 配置功能、生成 `mr_config.h`      | [框架使用指南：Kconfig](usage.md#3-使用-kconfig-配置功能)                        |
| 描述板级硬件、生成 `mr_hardgen.h` | [框架使用指南：HTC](usage.md#4-htc-硬件描述编译)                                 |
| 处理 initcall、驱动或 RPC 注册段  | [框架使用指南：注册段](usage.md#6-注册段与-sections)                             |
| 把 mrX 源码纳入应用构建           | [框架使用指南：纳入构建](usage.md#5-把-mrx-纳入你的构建)                         |
| 查一个模块的 API                  | 从第 3 节的索引进入对应模块文档，用页面内搜索找函数名                            |
| 阅读或修改框架实现                | 进入对应模块文档的“开发者指南”                                                   |
| 给 CLI 扩展插件                   | [CLI 总览](scripts/cli/cli.md)；插件需在 `scripts/plugins/catalog.py` 里显式登记 |
| 使用主机端 CLI 工具               | [主机脚本教程](scripts/README.md)                                                |
| 了解测试框架                      | [测试框架 Test](test/test.md)                                                    |

## 2. 文档放在哪

模块文档按源码职责分目录，通常与源码目录保持对应——`kernel/timer.md` 讲的就是 `kernel/timer.c`；Boot、RPC 和 XVM 文档集中在
`modules/` 下的对应目录：

```text
mrX/document/
├── README.md         # 你现在看的这篇：入口与索引
├── usage.md          # 接入与快速开始
├── documentation.md  # 文档编写与维护规范（只给维护者看）
├── kernel/           # 内核服务：时钟、中断、日志、定时器、对象系统……（有目录入口）
├── libc/             # C 库：内存分配、字符串、格式化输入输出
├── drivers/          # 设备驱动：驱动框架、GPIO、UART、ADC/DAC、扩展芯片
├── fs/               # 文件系统：VFS、RamFS、RootFS
├── modules/          # 上层组件：Boot、RPC、XVM、Crypto
├── platform/         # 平台移植层：安装已有平台、移植新平台
├── arch/             # 芯片厂商适配规范：当前包含 ST / STM32H5
│                     # 芯片运行期语义在源码侧 mrX/arch/<vendor>/<target>/README.md
├── scripts/          # 主机端 Python 工具（kconfig、htc、sections……）（有目录入口）
└── test/             # 测试框架
```

部分目录带一个入口文件，从它进目录比直接点某个模块文件更省事：
[kernel/README.md](kernel/README.md)（内核服务总览）、[scripts/README.md](scripts/README.md)、
[platform/platform.md](platform/platform.md)、[ST 芯片适配](arch/st/st.md)、[test/test.md](test/test.md)。

`libc/`、`drivers/`、`fs/`、`modules/` 没有独立入口文件，直接看第 3 节索引即可； 其中 `drivers/driver.md`
末尾列有本目录全部驱动，可作为驱动的起点。

适配还会用到两类不在第 3 节索引里的文档：

- 芯片的运行期语义（单位、中断顺序、能力限制）在源码侧
  `mrX/arch/<vendor>/<target>/README.md`，例如
  [STM32H5 驱动适配约定](../arch/st/stm32h5/README.md)。它和 `arch/st/st.md` 的分工是：
  前者说本芯片是什么行为，后者说适配层该怎么写。
- 每类外设的 backend 契约就是第 3 节索引里那些 `drivers/<driver>/porting.md`，与同目录的
  驱动主文档成对：`gpio.md` 讲怎么用，`gpio/porting.md` 讲怎么实现。它们和 `st.md` 的分工是：
  `st.md` 管所有 IP 共有的分层与命名，`porting.md` 只管这一类 IP 的 ops 与验收。

## 3. 完整文档索引

### 框架接入与工具

| 文档                             | 解决的问题                                                                               |
|----------------------------------|------------------------------------------------------------------------------------------|
| [Usage](usage.md)                | 框架接入的最短流程、Kconfig UI、HTC、纳入构建、注册段、初始化和排查                      |
| [Platform](platform/platform.md) | 已有平台安装、`install.toml`、新芯片移植和 `mr_port_*` 契约                              |
| [脚本教程](scripts/README.md)    | CLI 入口及 `kconfig`、`htc`、`platform`、`sections`、`rpc-idl`、`mdk`、`echo` 的详细用法 |
| [Test](test/test.md)             | 测试框架、测试用例注册和运行方式                                                         |

### 内核服务

| 模块                | 文档                                  | 模块                | 文档                              |
|---------------------|---------------------------------------|---------------------|-----------------------------------|
| 时钟 Clock          | [clock.md](kernel/clock.md)           | 入口 Entry          | [entry.md](kernel/entry.md)       |
| FIFO                | [fifo.md](kernel/fifo.md)             | 哈希表 Hashmap      | [hashmap.md](kernel/hashmap.md)   |
| 钩子 Hook           | [hook.md](kernel/hook.md)             | 初始化调用 Initcall | [initcall.md](kernel/initcall.md) |
| 中断 Irq            | [irq.md](kernel/irq.md)               | 日志 Log            | [log.md](kernel/log.md)           |
| 对象系统 Object     | [object.md](kernel/object.md)         | 格式化输出 Printf   | [printf.md](kernel/printf.md)     |
| 内存块分配器 Slab   | [slab.md](kernel/slab.md)             | 软定时器 Timer      | [timer.md](kernel/timer.md)       |
| 等待队列 Wait       | [wait.md](kernel/wait.md)             | 工作队列 Work       | [work.md](kernel/work.md)         |
| 基础工具 Foundation | [foundation.md](kernel/foundation.md) |                     |                                   |

### C 库

| 模块              | 文档                        | 模块           | 文档                          |
|-------------------|-----------------------------|----------------|-------------------------------|
| 内存分配 Malloc   | [malloc.md](libc/malloc.md) | 格式化 Sprintf | [sprintf.md](libc/sprintf.md) |
| 格式化解析 Sscanf | [sscanf.md](libc/sscanf.md) | 字符串 String  | [string.md](libc/string.md)   |

### 设备驱动

| 模块            | 文档                                        | 模块     | 文档                                |
|-----------------|---------------------------------------------|----------|-------------------------------------|
| 驱动框架 Driver | [driver.md](drivers/driver.md)              | GPIO     | [gpio.md](drivers/gpio/gpio.md)     |
| UART            | [uart.md](drivers/uart/uart.md)             | I2C      | [i2c.md](drivers/i2c/i2c.md)        |
| SPI             | [spi.md](drivers/spi/spi.md)                | ADC      | [adc.md](drivers/adc/adc.md)        |
| DAC             | [dac.md](drivers/dac/dac.md)                | RAM 设备 | [ram.md](drivers/ram/ram.md)        |
| DMA             | [dma.md](drivers/dma/dma.md)                | TMR      | [tmr.md](drivers/tmr/tmr.md)        |
| WDT             | [wdt.md](drivers/wdt/wdt.md)                | FLASH    | [flash.md](drivers/flash/flash.md)  |
| PWM             | [pwm.md](drivers/pwm/pwm.md)                |          |                                     |
平台 backend 开发请阅读对应驱动目录下的 `porting.md`：
[GPIO](drivers/gpio/porting.md)、[UART](drivers/uart/porting.md)、[I2C](drivers/i2c/porting.md)、[SPI](drivers/spi/porting.md)、[ADC](drivers/adc/porting.md)、[DAC](drivers/dac/porting.md)、[DMA](drivers/dma/porting.md)、[TMR](drivers/tmr/porting.md)、[PWM](drivers/pwm/porting.md)、[WDT](drivers/wdt/porting.md)、[RAM](drivers/ram/porting.md)、[FLASH](drivers/flash/porting.md)。

每篇 `porting.md` 只讲该类外设的 ops 契约与验收；适配层的分层、资源归属和文件骨架统一由
[芯片厂商适配](#芯片厂商适配)一节指向的规范文件约束，而具体芯片的运行期语义在源码侧
`mrX/arch/<vendor>/<target>/README.md`。

### 文件系统

| 模块              | 文档                      | 模块               | 文档                    |
|-------------------|---------------------------|--------------------|-------------------------|
| 文件系统 VFS      | [fs.md](fs/fs.md)         | RAM 文件系统 RamFS | [ramfs.md](fs/ramfs.md) |
| 根文件系统 RootFS | [rootfs.md](fs/rootfs.md) |                    |                         |

### 上层模块

| 模块             | 文档                                  | 模块                | 文档                            |
|------------------|---------------------------------------|---------------------|---------------------------------|
| 远程过程调用 RPC | [rpc.md](modules/rpc/rpc.md)          | 虚拟机 XVM          | [xvm.md](modules/xvm/xvm.md)    |
| 调试器 Dbg       | [dbg.md](modules/xvm/dbg.md)          | GDB 桩 Gdb          | [gdb.md](modules/xvm/gdb.md)    |
| 密码算法 Crypto  | [crypto.md](modules/crypto/crypto.md) | 启动与固件更新 Boot | [boot.md](modules/boot/boot.md) |

### 芯片厂商适配

| 厂商 / 系列  | 适配规范（怎么写）     | 运行期语义（是什么行为）                                                             |
|--------------|------------------------|--------------------------------------------------------------------------------------|
| ST / STM32H5 | [st.md](arch/st/st.md) | [stm32h5/README.md](../arch/st/stm32h5/README.md)：单位、中断顺序、IO 约定、能力限制 |

适配规范在文档目录，芯片的运行期语义在源码目录，跟着适配代码走，避免两边漂移。新增厂商时两处都要补齐：
`document/arch/<vendor>/` 下写规范，`mrX/arch/<vendor>/<target>/README.md` 下写该芯片的行为。

## 4. 阅读方法：每篇模块文档怎么用

模块文档统一按 **用户指南 → 开发者指南**组织：

- **前半部分（用户指南）**回答“怎么用”：模块做什么、依赖什么、怎么配、API 长什么样、完整例子、常见问题。只写应用的话，读到这里就够了。
- **后半部分（开发者指南）**回答“为什么这样做、怎么扩展”：架构、数据结构、核心流程、并发与中断安全、源码导读。只在排查问题或者要改框架时才需要往下读。

驱动目录下的 `porting.md` 是例外，它只面向平台 backend 开发者，直接按移植步骤组织，不重复用户指南，顺序往下读即可。`arch/`
下的芯片厂商文档同样不套模块骨架。

文档里出现的 `MR_USE_*` 是功能开关，`MR_CFG_*` 是资源参数，都在 Kconfig 里配置；每篇文档开头的表格会告诉你它依赖哪个功能宏和哪个头文件。示例都取自
`mrX/test/` 或能直接按公开 API 编译，看到例子可以直接抄。

## 5. 卡住了怎么办

- **接入阶段报错**：先看 [Usage 的常见问题排查](usage.md#8-常见问题排查)，它按平台、配置、生成、构建、初始化五个阶段给了排查顺序。
- **适配阶段卡住**：先看 [Platform 的 FAQ](platform/platform.md#7-常见问题faq)；接口契约以 `platform.md` 为准，
  分层写法以 [ST 适配规范](arch/st/st.md) 为准。
- **某个模块行为不对**：进对应模块文档的用户指南，先看 FAQ 一节。
- **命令行工具用不起来**：看 [脚本教程](scripts/README.md)，里面区分了“用户只需执行 `cli.py`”和“库代码不要直接运行”。
- **文档本身有问题**：修改规则见 [文档编写与维护规范](documentation.md)。

## 6. 文档维护

文档编写规则、模块文档骨架、示例约束和维护要求单独放在
[文档编写与维护规范](documentation.md)。它是维护者参考，不是 mrX 使用者的入门材料。

新增、删除或重命名文档时，请同步更新本文第 2 节的目录说明和第 3 节的索引，并检查第 1 节“按目标找文档”里的链接是否仍然有效。

# mrX

mrX 是一套面向嵌入式系统的模块化 C 框架与主机端工具链。它不接管你的应用构建系统，也不强绑定特定
RTOS 调度器，而是以源码依赖的形式嵌入已有工程，按需提供内核基础服务、独立 C
库、统一设备驱动、虚拟文件系统以及 Boot / Crypto / RPC / XVM 等上层组件。

## 架构概览

```text
  ┌───────────────────────────────────────────────────────────────────────────┐
  │                      你的应用工程 (业务代码 / 构建系统)                   │
  └─────────────────────────────┬───────────────────────────────┬─────────────┘
                                │ 调用 API / mr_user_init()     ▲ 生成头文件与
                                ▼                               │ 注入链接段
  ┌─────────────────────────────────────────────────────────────┴─────────────┐
  │                                mrX 框架                                   │
  │                                                                           │
  │  ┌───────────┬───────────────┬──────────────┬──────────────────────────┐  │
  │  │   Boot    │    Crypto     │     RPC      │      XVM                 │  │
  │  │ A/B+Recov │ SHA256/ECDSA  │ 异步会话/IDL │ RV32 + GDB桩             │  │
  │  └───────────┴───────────────┴──────────────┴──────────────────────────┘  │
  │                             上层组件 modules/                             │
  │  ┌───────────────────────────────────────┬─────────────────────────────┐  │
  │  │ 设备驱动 drivers/                     │ 文件系统 fs/                │  │
  │  │ GPIO/UART/I2C/SPI/ADC/DAC/DMA/TMR/PWM │ VFS / RootFS / RamFS        │  │
  │  │ WDT/FLASH/ETH/RAM + 外扩芯片驱动      │                             │  │
  │  ├───────────────────────────────────────┼─────────────────────────────┤  │
  │  │ 内核服务 kernel/                      │ 基础 C 库 libc/             │  │
  │  │ Object/Clock/Timer/Work/Wait/IRQ      │ malloc / string             │  │
  │  │ FIFO/Slab/Hashmap/Log/Printf/Initcall │ sprintf / sscanf            │  │
  │  └───────────────────────────────────────┴─────────────────────────────┘  │
  │  ┌─────────────────────────────────────────────────────────────────────┐  │
  │  │ 平台适配层 platform/ (由 arch/ 通过 platform install 安装)          │  │
  │  │ 内核端口 mr_port_* (Tick/IRQ/Output/Assert/Cache) + 外设 Backend    │  │
  │  └─────────────────────────────────────────────────────────────────────┘  │
  └───────────────────────────────────▲─────────────────────────▲─────────────┘
                                      │ platform install        │ 构建前生成
  ┌───────────────────────────────────┴──────┐  ┌───────────────┴─────────────┐
  │ 芯片适配仓库 arch/                       │  │ 主机工具 scripts/cli.py     │
  │ STM32 (HAL) / CH32 (EVT) / Windows       │  │ kconfig / htc / rpc-idl     │
  │                                          │  │ platform / sections / mdk   │
  └──────────────────────────────────────────┘  └─────────────────────────────┘
```

## 核心特性

- **按需裁剪与低侵入接入**：所有功能通过 Kconfig（`MR_USE_*` / `MR_CFG_*`
  ）控制开关与资源预算；应用工程保留原有目录、SDK 与构建方式，仅需加入头文件路径和框架
  `.c` 文件并在入口调用 `mr_user_init()`。
- **统一对象模型与注册段机制**：基于 `mr_object_t`
  提供引用计数、类继承以及静态/动态双生命周期；Initcall、驱动、RPC 服务与单元测试用例均通过链接器段（
  `sections.toml`）自动收集注册。
- **硬件描述与驱动解耦**：内置类 DTS 语法的硬件树编译器（HTC），将板级 `.hts` /
  `.htsi` 编译为 C 宏头文件 `mr_hardgen.h`，驱动按节点路径、`compatible`、`label` 与
  `alias` 实例化。
- **完备的外设驱动与外扩芯片支持**：除 13 类标准外设框架外，内置常见外扩芯片驱动（如
  74HC595 /
  74HC165、PCF8574、PCA9555、MCP23x17、ADS1x15、MCP3008、MCP4725、MCP4922、PCA9685、SPI
  NOR Flash，以及 GPIO 模拟 I2C/SPI）。
- **高阶嵌入式组件**：自带轻量级虚拟文件系统（VFS + RootFS +
  RamFS）、无硬件依赖密码学库（SHA-256 / HMAC-SHA-256 / NIST P-256 / ECDSA
  验签）、A/B + Recovery 固件引导模块、带 IDL 代码生成的 RPC，以及支持 GDB 远程调试的
  RISC-V32 虚拟机（XVM）。
- **完整测试与文档体系**：内置 gtest 风格轻量单元测试框架与覆盖全子系统的测试用例，配套结构化中文文档（快速上手、API
  速查与外设 `porting.md` 移植契约）。

## 目录结构

```text
mrX/
├── kernel/         # 内核服务：对象、时钟、软定时器、工作队列、等待队列、FIFO、Slab、哈希表、日志等
├── libc/           # 独立 C 库：内存分配、字符串、格式化输入输出
├── drivers/        # 设备驱动框架、标准外设类与外扩芯片驱动
├── fs/             # 虚拟文件系统 VFS、RootFS 与 RamFS
├── modules/        # 上层组件：Boot、Crypto、RPC、XVM
├── include/        # 公共头文件与各子系统 Kconfig
├── arch/           # 可安装的平台适配仓库（不直接参与编译）
├── platform/       # 当前已安装平台的工作副本（参与编译）
├── scripts/        # 主机端 Python CLI 工具链（Python 3.11+）
├── test/           # 单元测试框架与各子系统测试用例
├── document/       # 完整文档体系
├── Kconfig         # 顶层配置入口
└── sections.toml   # 注册段定义（initcall / driver / rpc / testcase）
```

## 已支持平台

平台适配存放在 `arch/` 中，使用前通过 `scripts/cli.py platform install` 安装到
`platform/`：

| 平台                | 路径            | 说明                                                                   | 文档                                         |
|---------------------|-----------------|------------------------------------------------------------------------|----------------------------------------------|
| **STM32（通用）**   | `arch/st/stm32` | 基于 HAL/Cube 工程，编译期自动探测不同系列外设寄存器与缓存差异         | [STM32 平台适配](arch/st/stm32/README.md)    |
| **WCH CH32**        | `arch/wch/ch32` | 基于 EVT SDK，覆盖 CH32V00x / VM00X / V10x / V20x / V205 / V30x / V4x7 | [WCH CH32 平台适配](arch/wch/ch32/README.md) |
| **Windows（宿主）** | `arch/win`      | 用于主机端仿真开发与单元测试运行                                       | [平台移植层](document/platform/platform.md)  |

## 快速开始

> 以下主机命令均假设当前工作目录为 `mrX/` 根目录，主机需要 **Python 3.11+**。

### 1. 安装主机工具依赖

```bash
python -m pip install -r ./scripts/requirements.txt
```

### 2. 安装目标平台

必须在配置 Kconfig 之前安装平台，以便平台选项进入配置菜单（以下以 STM32 为例，CH32
或 Windows 替换对应的 `install.toml` 即可）：

```bash
python ./scripts/cli.py platform install -i ./arch/st/stm32/install.toml
```

### 3. 配置功能与生成头文件

```bash
# 打开 menuconfig 交互配置并生成 include/mrX/mr_config.h
python ./scripts/cli.py kconfig

# （可选）使用硬件树驱动时，编译板级 .hts 生成 include/mrX/mr_hardgen.h
python ./scripts/cli.py htc -i <board>.hts
```

### 4. 纳入应用构建与链接段

1. 将 `<mrX>/include` 加入头文件搜索路径。
2. 递归收集以下目录中的 `.c` 源文件直接参与链接（ **不要**收集 `arch/` 与 `test/`
   ，也 **不要**先打成静态库，否则注册段会被链接器丢弃）：
   `kernel/`、`libc/`、`drivers/`、`fs/`、`modules/`、`platform/`。
3. 使用 GNU ld 链接脚本时，注入注册段规则（Keil MDK / armlink 无需此步骤）：

```bash
python ./scripts/cli.py sections -i <linker_script>.lds
```

### 5. 在应用中启动框架

在底层时钟与硬件基础初始化完成后调用一次 `mr_user_init()`：

```c
#include <mrX.h>

int main(void) {
    /* 1. 完成板级基础时钟与启动初始化 */

    /* 2. 初始化平台、已启用内核服务及所有注册的 initcall */
    mr_user_init();

    /* 3. 进入应用主循环 */
    for (;;) {
    }
}
```

完整接入流程、排错顺序与 Keil MDK 同步方法见 [框架使用指南](document/usage.md)。

## 主机端 CLI 速查

统一入口为 `scripts/cli.py`，不带参数运行可进入交互式 Shell：

| 命令                                                         | 用途                                       |
|--------------------------------------------------------------|--------------------------------------------|
| `python ./scripts/cli.py platform install -i <install.toml>` | 安装指定平台适配到 `platform/`             |
| `python ./scripts/cli.py kconfig [--no-ui]`                  | 打开 menuconfig 或非交互生成 `mr_config.h` |
| `python ./scripts/cli.py htc -i <board>.hts`                 | 将 `.hts` / `.htsi` 编译为 `mr_hardgen.h`  |
| `python ./scripts/cli.py rpc-idl -i <service>.idl`           | 将 `.idl` 编译为 RPC 收发与编解码代码      |
| `python ./scripts/cli.py sections -i <link>.lds`             | 为 GNU ld 链接脚本幂等注入 mrX 注册段      |
| `python ./scripts/cli.py mdk ...`                            | 同步或维护 Keil MDK 工程文件               |
| `python ./scripts/cli.py list`                               | 列出所有可用子命令                         |

## 文档导航

- [文档总入口与完整索引](document/README.md)
- [框架使用指南（Usage）](document/usage.md)
- [平台移植层（Platform）](document/platform/platform.md)
- [内核服务（Kernel）](document/kernel/README.md) · [设备驱动（Drivers）](document/drivers/driver.md) · [文件系统（FS）](document/fs/fs.md)
-

上层组件：[Boot](document/modules/boot/boot.md) · [Crypto](document/modules/crypto/crypto.md) · [RPC](document/modules/rpc/rpc.md) · [XVM](document/modules/xvm/xvm.md)

- [主机脚本教程（Scripts）](document/scripts/README.md) · [测试框架（Test）](document/test/test.md)

## 许可证

本项目基于 [Apache-2.0](LICENSE) 许可证开源。

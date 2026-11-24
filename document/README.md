# mrX 文档

mrX 是一套可嵌入现有应用工程的轻量级嵌入式内核与组件集合，业务代码保留在你自己的工程目录中，mrX
以源码依赖的形式接入。

```text
  ┌─────────────────────────────────────────────────────────────────────┐
  │                       你的应用工程（业务代码）                      │
  └──────────────────────────┬───────────────────────────┬──────────────┘
                             │ 调用服务 / 驱动 / 内核      │ mr_user_init()
                             ▼                           ▼
  ┌─────────────────────────────────────────────────────────────────────┐
  │  上层组件 modules/    │  文件系统 fs/       │  设备驱动 drivers/    │
  │  Boot · RPC · XVM     │  VFS · RamFS       │  统一框架 · 13 类外设  │
  │  Crypto · XTrace      │  RootFS            │  外部扩展芯片          │
  ├───────────────────────┴────────────────────┴────────────────────────┤
  │  内核服务 kernel/                          │  C 基础库 libc/        │
  │  Initcall · Clock · Timer · IRQ · Wait     │  Malloc · Sprintf      │
  │  Work · Hook · Object · Slab · FIFO        │  Sscanf · String       │
  │  Hashmap · Log · Trace · Printf            │                        │
  ├────────────────────────────────────────────┴────────────────────────┤
  │  平台适配层 platform/（由 arch/ 通过 platform install 安装）        │
  │  内核端口 mr_port_*（tick/irq/output/assert/cache）外设 Backend ops │
  └─────────────────────────────────────────────────────────────────────┘
                             ▲
                             │ 构建前生成 mr_config.h / mr_hardgen.h / 注册段
  ┌──────────────────────────┴──────────────────────────────────────────┐
  │  主机脚本 scripts/：kconfig · htc · rpc-idl · sections · mdk · cli  │
  └─────────────────────────────────────────────────────────────────────┘
```

---

## 从哪里开始

根据你的角色选择入口：

- **我要把 mrX 接入现有工程**：直接看 [框架使用指南](usage.md)（环境准备 →
  安装平台 → Kconfig → 硬件树 HTC → 构建与链接段 → `mr_user_init()`）。
- **我要适配新芯片或写外设驱动**：先看 [平台移植层](platform/platform.md) 了解
  `mr_port_*` 契约，再到对应驱动目录看 `porting.md`。
- **我要用命令行工具生成配置或代码**：看 [主机脚本](scripts/README.md)（`cli.py`
  统一入口）。

---

## 接入、平台与工具

| 文档                               | 内容                                                                        |
|------------------------------------|-----------------------------------------------------------------------------|
| [框架使用指南](usage.md)           | 从零接入 mrX 的完整流程与常见构建排查                                       |
| [平台移植层](platform/platform.md) | 平台包安装、`install.toml` 规范与 `mr_port_*` 移植契约                      |
| [主机脚本](scripts/README.md)      | `cli.py` 及 `kconfig`、`htc`、`platform`、`sections`、`rpc-idl`、`mdk` 用法 |
| [测试框架](test/test.md)           | 单元测试注册宏与运行方式                                                    |

### 芯片厂商适配

厂商级命名与分层规范放在 `document/arch/`，具体芯片系列的运行期行为与外设限制随源码放在
`arch/<vendor>/<target>/README.md`。

| 厂商 / 目标 | 适配规范                  | 芯片运行期说明                                |
|-------------|---------------------------|-----------------------------------------------|
| ST (STM32)  | [st.md](arch/st/st.md)    | [stm32/README.md](../arch/st/stm32/README.md) |
| WCH (CH32)  | [wch.md](arch/wch/wch.md) | [ch32/README.md](../arch/wch/ch32/README.md)  |

---

## 内核与 C 库

### 内核服务（[目录总览](kernel/README.md)）

| 模块                                        | 说明                                                    |
|---------------------------------------------|---------------------------------------------------------|
| [初始化调用 Initcall](kernel/initcall.md)   | 基于链接段的自动初始化顺序控制                          |
| [入口 Entry](kernel/entry.md)               | 在 `main()` 之前完成内核初始化的入口封装                |
| [时钟 Clock](kernel/clock.md)               | 系统 tick / uptime 基准、同步 hook 与 tickless 剩余时间 |
| [软定时器 Timer](kernel/timer.md)           | 基于 tick 的单次与周期回调定时器                        |
| [中断 Irq](kernel/irq.md)                   | 中断控制器、action 管理与可选 defer 下半部              |
| [等待队列 Wait](kernel/wait.md)             | `waitable` / `waiter` 同步回调事件通知                  |
| [工作队列 Work](kernel/work.md)             | 立即或延时 work 排队与驱动执行                          |
| [钩子 Hook](kernel/hook.md)                 | 定长追加式回调表                                        |
| [对象系统 Object](kernel/object.md)         | 对象类型注册、引用计数与生命周期                        |
| [块分配器 Slab](kernel/slab.md)             | 固定块大小对象池（原子位图管理）                        |
| [环形缓冲 FIFO](kernel/fifo.md)             | 定长环形缓冲区（SPSC 无锁安全）                         |
| [哈希表 Hashmap](kernel/hashmap.md)         | 32 位整型键哈希表，节点内嵌无堆分配                     |
| [日志 Log](kernel/log.md)                   | 分级、带时间戳与标签过滤的日志输出                      |
| [跟踪 Trace](kernel/trace.md)               | 轻量级区间、事件与计数器内核跟踪接口                    |
| [格式化输出 Printf](kernel/printf.md)       | 经由平台输出接口的格式化打印                            |
| [基础工具 Foundation](kernel/foundation.md) | 侵入式链表、自旋锁、引用计数与工具宏                    |

### C 基础库

| 模块                                  | 说明                                    |
|---------------------------------------|-----------------------------------------|
| [动态内存 Malloc](libc/malloc.md)     | 多堆区内存分配与统计                    |
| [格式化输出 Sprintf](libc/sprintf.md) | 可重入格式化字符串输出                  |
| [格式化解析 Sscanf](libc/sscanf.md)   | 格式化输入解析封装                      |
| [字符串 String](libc/string.md)       | 内存、字符串操作与位查找（`ffs`/`fls`） |

---

## 设备驱动

通用驱动模型与生命周期见 [驱动框架 Driver](drivers/driver.md)
，跨外设联动见 [驱动组合示例](drivers/examples.md)
，串口外设通用说明见 [串口外设说明](drivers/serial_peripheral.md)。

| 外设  | 使用文档                           | 移植文档（Backend）                    | 扩展芯片                                                                                                                                                                                              |
|-------|------------------------------------|----------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| GPIO  | [gpio.md](drivers/gpio/gpio.md)    | [porting.md](drivers/gpio/porting.md)  | [HC165](drivers/gpio/gpio_hc165.md)、[HC595](drivers/gpio/gpio_hc595.md)、[MCP23x17](drivers/gpio/gpio_mcp23x17.md)、[PCA9555](drivers/gpio/gpio_pca9555.md)、[PCF8574](drivers/gpio/gpio_pcf8574.md) |
| UART  | [uart.md](drivers/uart/uart.md)    | [porting.md](drivers/uart/porting.md)  | —                                                                                                                                                                                                     |
| I2C   | [i2c.md](drivers/i2c/i2c.md)       | [porting.md](drivers/i2c/porting.md)   | —                                                                                                                                                                                                     |
| SPI   | [spi.md](drivers/spi/spi.md)       | [porting.md](drivers/spi/porting.md)   | —                                                                                                                                                                                                     |
| ADC   | [adc.md](drivers/adc/adc.md)       | [porting.md](drivers/adc/porting.md)   | [ADS1x15](drivers/adc/adc_ads1x15.md)、[MCP3008](drivers/adc/adc_mcp3008.md)                                                                                                                          |
| DAC   | [dac.md](drivers/dac/dac.md)       | [porting.md](drivers/dac/porting.md)   | [MCP4725](drivers/dac/dac_mcp4725.md)、[MCP4922](drivers/dac/dac_mcp4922.md)                                                                                                                          |
| DMA   | [dma.md](drivers/dma/dma.md)       | [porting.md](drivers/dma/porting.md)   | —                                                                                                                                                                                                     |
| TMR   | [tmr.md](drivers/tmr/tmr.md)       | [porting.md](drivers/tmr/porting.md)   | —                                                                                                                                                                                                     |
| PWM   | [pwm.md](drivers/pwm/pwm.md)       | [porting.md](drivers/pwm/porting.md)   | [PCA9685](drivers/pwm/pwm_pca9685.md)                                                                                                                                                                 |
| WDT   | [wdt.md](drivers/wdt/wdt.md)       | [porting.md](drivers/wdt/porting.md)   | —                                                                                                                                                                                                     |
| RAM   | [ram.md](drivers/ram/ram.md)       | [porting.md](drivers/ram/porting.md)   | —                                                                                                                                                                                                     |
| FLASH | [flash.md](drivers/flash/flash.md) | [porting.md](drivers/flash/porting.md) | —                                                                                                                                                                                                     |
| ETH   | [eth.md](drivers/eth/eth.md)       | [porting.md](drivers/eth/porting.md)   | —                                                                                                                                                                                                     |

---

## 文件系统与上层组件

### 文件系统

| 模块                              | 说明                                |
|-----------------------------------|-------------------------------------|
| [虚拟文件系统 VFS](fs/fs.md)      | 统一文件/目录句柄、挂载点与后端接口 |
| [内存文件系统 RamFS](fs/ramfs.md) | 基于内存的目录、普通文件与 FIFO     |
| [根文件系统 RootFS](fs/rootfs.md) | 启动期自动挂载的只读根骨架          |

### 上层组件

| 模块                                        | 说明                                      |
|---------------------------------------------|-------------------------------------------|
| [启动与升级 Boot](modules/boot/boot.md)     | A/B 分区启动、镜像校验、回退与 Recovery   |
| [远程过程调用 RPC](modules/rpc/rpc.md)      | 二进制帧协议、会话管理与 IDL 代码生成     |
| [虚拟机 XVM](modules/xvm/xvm.md)            | 嵌入式字节码虚拟机与系统调用接口          |
| [XVM 调试器 Dbg](modules/xvm/dbg.md)        | XVM 单步、断点与探针调试接口              |
| [XVM GDB 桩](modules/xvm/gdb.md)            | 基于 RSP 协议的远程 GDB 调试服务端        |
| [密码算法 Crypto](modules/crypto/crypto.md) | SHA-256、HMAC-SHA-256 与 ECDSA P-256 验签 |
| [可扩展跟踪 XTrace](modules/xtrace/xtrace.md) | 可插拔协议编码（CTF / SystemView）与异步输出缓冲 |

---

## 文档维护

文档目录约定与写作规范见 [documentation.md](documentation.md)。

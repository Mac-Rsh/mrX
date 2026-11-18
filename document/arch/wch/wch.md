# WCH（CH32）平台适配规范

> 本文对应源码目录 `mrX/arch/wch/`，规定 WCH 厂商适配的目录结构、命名约定与分层归属。
> 具体芯片的运行期语义（时间单位、中断顺序、能力限制）由源码侧的
> [WCH CH32 平台适配](../../arch/wch/ch32/README.md) 管理，不在本文重复。

| 项目     | 说明                                              |
|----------|---------------------------------------------------|
| 源码目录 | `mrX/arch/wch/ch32/`                              |
| 平台名称 | `wch,ch32`                                        |
| 平台 Kconfig | `MR_USE_CH32`（固定启用）、SDK 家族 choice、`MR_USE_ICACHE`（本核取指同步）、`MR_USE_CH32_DEBUG_OUTPUT` |
| 参考文档 | [平台移植层](../platform/platform.md)、[CH32 运行期语义](../../arch/wch/ch32/README.md) |

## 1. 规范范围

本文与相邻文档的分工：

- [平台移植层](../platform/platform.md) 规定 `mr_port_*` 内核契约与移植步骤，对全部
  厂商生效，本文不重复。
- 各驱动目录下的 `porting.md` 规定该类外设的 ops 契约与验收，本文不重复。
- 本文规定 WCH 适配的**厂商级约定**：目录结构、SDK 家族选择、安装方式、HardTree 命名
  与资源归属。
- [WCH CH32 平台适配](../../arch/wch/ch32/README.md)
  规定该平台的**运行期行为**：每个 compatible 的属性语义、量化规则、错误码和各 SDK
  家族的能力差异。修改平台源码行为时同步更新该文档。

## 2. 目录结构

```text
mrX/arch/wch/ch32/
├── install.toml                    # 平台名 wch,ch32 与复制规则
├── include/platform/
│   ├── Kconfig                     # SDK 家族 choice、取指同步与调试输出配置
│   └── mr_platform.h               # mr_platform_init 与 CH32 专属辅助接口
└── platform/
    ├── platform.c                  # mr_platform_init：初始化时钟端口
    ├── port/                       # clock、irq、output、assert、cache 端口实现
    └── drivers/                    # 片上外设 backend（gpio、uart、spi……）
```

安装目标为 `mrX/platform/` 与 `mrX/include/mrX/platform/`，安装流程见
[平台移植层](../platform/platform.md#11-选择并安装已有平台)。同一输出目录同时只能安装
一个平台。

## 3. SDK 家族选择

平台 Kconfig 用 choice 选择一套 WCH EVT SDK：

| Kconfig | EVT SDK | 设备头文件 |
| --- | --- | --- |
| `MR_USE_CH32V00X` | CH32V003（CH32V00x） | `ch32v00x.h` |
| `MR_USE_CH32VM00X` | CH32V002/004/005/006/007（CH32VM00X） | `ch32v00X.h` |
| `MR_USE_CH32V10X` | CH32V103（CH32V10x） | `ch32v10x.h` |
| `MR_USE_CH32V20X` | CH32V20x | `ch32v20x.h` |
| `MR_USE_CH32V205` | CH32V205 / CH32V203CCT6 | `ch32v205.h` |
| `MR_USE_CH32V30X` | CH32V30x | `ch32v30x.h` |
| `MR_USE_CH32V4X7` | CH32V407 / CH32V467 | `ch32v4x7.h` |

实际芯片型号、封装、启动文件、链接脚本和引脚由板级 EVT 工程决定，平台包不重复描述。
板级 HTS 按实际芯片声明外设实例。V317 使用片内 Ethernet PHY 时，在
`wch,ch32-eth` 节点加入 `wch,internal-rmii-phy;`。
SDK 之间的能力差异（SysTick 位宽、外设实例数、Flash 粒度等）在
[CH32 运行期语义](../../arch/wch/ch32/README.md) 中按型号说明；新增 SDK 家族时只扩展
choice 与对应驱动的能力分支，不引入厂商级编译开关以外的机制。

`MR_USE_CH32_DEBUG_OUTPUT` 把诊断输出交给 EVT `Debug/debug.c` 的 `_write`；板级自带
日志通道时关闭该配置并覆盖 `mr_ch32_platform_output()`。

## 4. 平台接入约定

接入统一走 WCH EVT 工程，步骤概要（细节见
[WCH CH32 平台适配](../../arch/wch/ch32/README.md#接入-evt-工程)）：

1. `platform install` 安装平台，Kconfig 中选择 SDK 家族。
2. 板级 HardTree 描述外设实例，`htc -i` 生成 `mr_hardgen.h`。
3. EVT 的 GNU ld 脚本经 `sections` 工具收集 mrX 注册段。
4. EVT 启动代码完成 `SystemInit` 进入 `main()` 后，初始化诊断通道再调用
   `mr_user_init()`；平台接管 `SysTick_Handler` 与 SysTick 寄存器，运行期间保持
   `SystemCoreClock` 对应的时钟频率。

## 5. HardTree 命名约定

WCH 适配的 compatible 统一为 `wch,ch32-<外设>`，节点属性统一使用 `wch,` 前缀；HardTree
属性名中的连字符在生成头文件时映射为下划线（`wch,clock-mask` 对应源码中的
`wch_clock_mask`）。

| 属性形态 | 约定 |
| --- | --- |
| `wch,gpiox`、`wch,uartx`、`wch,timx` 等 | EVT SDK 寄存器实例，直接写芯片头文件中的实例符号 |
| `wch,clock-mask` | RCC 时钟使能位掩码，GPIO 后端使用 |
| `wch,clken` / `wch,prepare` | 板级 `void function(void)`，负责使能外设时钟；`wch,prepare` 另承担 AFIO 重映射与总线引脚配置 |
| `wch,irqn` / `wch,irq-handler` | 中断编号与处理函数必须成对提供，使用 EVT 启动向量的 IRQ 编号及符号；mrX 不重定向向量 |
| `wch,irq-priority` | PFIC 编码的优先级，缺省 0 |
| GPIO 引用 `<&gpio节点 pin>` | 属性元胞为 `[phandle, pin]`，V00x/VM00X 每端口最多 8 位，其余家族最多 16 位 |
| DMA 引用 `<&dma节点 channel>` | 属性元胞为 `[phandle, channel]`，channel 与请求映射数组下标一致 |
| 数字掩码属性（`wch,exti-mask`、`exist-mask` 等） | 以整数掩码书写，缺省值在运行期文档中说明 |

带复用引脚的外设（UART、PWM 等）通过 GPIO 后端的
`mr_ch32_gpio_pin_alternate()` / `mr_ch32_gpio_pin_release()` 接管和释放引脚；TMR 与
PWM 通过 `mr_ch32_timer_claim()` 认领定时器实例的独占使用权（重复认领返回
`-MR_EBUSY`）。

TMR 与 PWM 共用 `mr_ch32_timer_make_period()` 和 `mr_ch32_timer_set_reload()`。
平台按型号选择重装寄存器位宽，各后端提供计数上限。PWM 的比较值通过
`mr_ch32_timer_make_pulse()` 按同一时基量化。中央对齐 PWM 的周期与脉宽换算
均计入两次计数行程，高级定时器每两次计数转向产生一次更新事件。

## 6. 资源归属

- **板级负责**：时钟树与 `SystemCoreClock` 维护、时钟使能与 AFIO 重映射回调、启动文件
  向量表、ld 脚本与内存布局、外部 PHY 与总线初始化。
- **平台后端负责**：外设寄存器配置、引脚复用接管、中断使能与事件分发。
- **框架负责**：驱动注册、等待队列、超时与事件语义，契约以各驱动
  `porting.md` 为准。

后端与板级的分界原则：凡是同一芯片上随板级设计变化的内容（引脚、时钟使能、中断符号、
DMA 请求映射）都由 HardTree 属性交给板级声明，后端不做猜测。

## 7. 外设 compatible 索引

| Compatible | 驱动 | 运行期语义 |
| --- | --- | --- |
| `wch,ch32-gpio` | GPIO | [GPIO](../../arch/wch/ch32/README.md#gpio) |
| `wch,ch32-uart` | UART | [UART](../../arch/wch/ch32/README.md#uart) |
| `wch,ch32-spi` | SPI | [SPI 与 I2C](../../arch/wch/ch32/README.md#spi-与-i2c) |
| `wch,ch32-i2c` | I2C | [SPI 与 I2C](../../arch/wch/ch32/README.md#spi-与-i2c) |
| `wch,ch32-tmr` | TMR | [定时器与模拟外设](../../arch/wch/ch32/README.md#定时器与模拟外设) |
| `wch,ch32-pwm` | PWM | [定时器与模拟外设](../../arch/wch/ch32/README.md#定时器与模拟外设) |
| `wch,ch32-adc` | ADC | [定时器与模拟外设](../../arch/wch/ch32/README.md#定时器与模拟外设) |
| `wch,ch32-dac` | DAC | [定时器与模拟外设](../../arch/wch/ch32/README.md#定时器与模拟外设) |
| `wch,ch32-dma` | DMA | [DMA 与 Flash](../../arch/wch/ch32/README.md#dma-与-flash) |
| `wch,ch32-flash` | FLASH | [DMA 与 Flash](../../arch/wch/ch32/README.md#dma-与-flash) |
| `wch,ch32-eth` | ETH | [Ethernet](../../arch/wch/ch32/README.md#ethernet) |
| `wch,ch32-iwdg` | WDT | [IWDG 和 WWDG](../../arch/wch/ch32/README.md#iwdg-和-wwdg) |
| `wch,ch32-wwdg` | WDT | [IWDG 和 WWDG](../../arch/wch/ch32/README.md#iwdg-和-wwdg) |

各 compatible 的使用条件（依赖的 `MR_USE_DRIVER_*` 宏）由对应驱动源码和
`porting.md` 决定；enable 属性、status 语义与节点组织遵循
[框架使用指南：HTC](../usage.md#4-htc-硬件描述编译)。

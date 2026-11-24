# WCH（CH32）平台适配规范

本文规定 `mrX/arch/wch/ch32/` 的厂商级目录结构、SDK 家族选择、接入约定与 HardTree
命名；各芯片家族的运行期行为与能力差异见 [WCH CH32 平台适配](../../../arch/wch/ch32/README.md)
，内核通用 `port` 契约见 [平台移植层](../../platform/platform.md)。

| 项目         | 说明                                                                        |
|--------------|-----------------------------------------------------------------------------|
| 源码目录     | `mrX/arch/wch/ch32/`                                                        |
| 平台名称     | `wch,ch32`                                                                  |
| 平台 Kconfig | `MR_USE_CH32`、SDK 家族 choice、`MR_USE_ICACHE`、`MR_USE_CH32_DEBUG_OUTPUT` |
| 运行期文档   | [WCH CH32 平台适配](../../../arch/wch/ch32/README.md)                       |

## 目录结构与 SDK 家族选择

```text
mrX/arch/wch/ch32/
├── install.toml                    # 平台名 wch,ch32 与复制规则
├── include/platform/
│   ├── Kconfig                     # SDK 家族 choice、取指同步与调试输出配置
│   └── mr_platform.h               # mr_platform_init 与 CH32 专属辅助接口
└── platform/
    ├── platform.c                  # mr_platform_init：初始化时钟端口
    ├── port/                       # clock、irq、output、assert、cache 端口实现
    └── drivers/                    # 片上外设 backend
```

平台 Kconfig 通过 choice 选择对应的 WCH EVT SDK 家族：

| Kconfig            | EVT SDK                               | 设备头文件   |
|--------------------|---------------------------------------|--------------|
| `MR_USE_CH32V00X`  | CH32V003（CH32V00x）                  | `ch32v00x.h` |
| `MR_USE_CH32VM00X` | CH32V002/004/005/006/007（CH32VM00X） | `ch32v00X.h` |
| `MR_USE_CH32V10X`  | CH32V103（CH32V10x）                  | `ch32v10x.h` |
| `MR_USE_CH32V20X`  | CH32V20x                              | `ch32v20x.h` |
| `MR_USE_CH32V205`  | CH32V205 / CH32V203CCT6               | `ch32v205.h` |
| `MR_USE_CH32V30X`  | CH32V30x                              | `ch32v30x.h` |
| `MR_USE_CH32V4X7`  | CH32V407 / CH32V467                   | `ch32v4x7.h` |

- `MR_USE_CH32_DEBUG_OUTPUT`（默认 `y`）：将诊断输出转发至 EVT `Debug/debug.c` 的
  `_write`；若板级使用自定义日志通道，可关闭该配置并覆盖
  `mr_ch32_platform_output()`。
- `MR_USE_ICACHE`（默认 `n`）：通过 `fence.i` 提供本地核取指同步（CH32 无软件可控
  I-cache，启停接口返回 `-MR_ENOSYS`，line size 为 `0`）。

## 接入约定

1. 执行 `platform install -i arch/wch/ch32/install.toml` 安装平台，在 `kconfig`
   中选择目标 EVT SDK 家族。
2. 编写板级 `.hts` 并用 `htc` 生成 `mr_hardgen.h`（V317 使用片内 Ethernet PHY 时在
   `wch,ch32-eth` 节点添加 `wch,internal-rmii-phy;`）。
3. 使用 `sections` 工具向 EVT 的 GNU ld 链接脚本注入 mrX 注册段收集规则。
4. EVT 启动代码完成 `SystemInit` 进入 `main()` 后，先初始化调试通道再调用
   `mr_user_init()`；平台接管 `SysTick_Handler` 与 SysTick 寄存器，运行期需保持
   `SystemCoreClock` 与实际时钟一致。

## HardTree 命名与资源归属

WCH 适配的 `compatible` 统一为 `wch,ch32-<外设>`，属性统一使用 `wch,` 前缀（`-`
在头文件中映射为 `_`）：

| 属性形态                                         | 约定                                                                                              |
|--------------------------------------------------|---------------------------------------------------------------------------------------------------|
| `wch,gpiox`、`wch,uartx`、`wch,timx` 等          | EVT SDK 外设寄存器实例符号                                                                        |
| `wch,clock-mask`                                 | RCC 时钟使能位掩码（GPIO 后端使用）                                                               |
| `wch,clken` / `wch,prepare`                      | 板级 `void function(void)` 回调，负责外设时钟使能；`wch,prepare` 另承担 AFIO 重映射与总线引脚配置 |
| `wch,irqn` / `wch,irq-handler`                   | EVT 启动向量的 IRQ 编号与处理函数符号，必须成对提供                                               |
| `wch,irq-priority`                               | PFIC 编码优先级，缺省为 `0`                                                                       |
| GPIO 引用 `<&gpio节点 pin>`                      | `[phandle, pin]` 元胞，V00x/VM00X 每端口最多 8 位，其余家族最多 16 位                             |
| DMA 引用 `<&dma节点 channel>`                    | `[phandle, channel]` 元胞，`channel` 与请求映射数组下标一致                                       |
| 数字掩码属性（`wch,exti-mask`、`exist-mask` 等） | 整数掩码，缺省值见运行期文档                                                                      |

复用引脚通过 `mr_ch32_gpio_pin_alternate()` / `mr_ch32_gpio_pin_release()`
管理；TMR 与 PWM 通过 `mr_ch32_timer_claim()` 互斥认领定时器实例（重复认领返回
`-MR_EBUSY`），并共用 `mr_ch32_timer_make_period()`、`mr_ch32_timer_set_reload()` 与
`mr_ch32_timer_make_pulse()` 进行时基量化（中央对齐 PWM 计入两次计数行程）。

**资源归属**：板级负责时钟树与 `SystemCoreClock`、时钟使能与 AFIO 重映射回调、向量表、ld
脚本与外部 PHY/总线初始化；平台后端负责寄存器配置、引脚复用接管与中断事件分发。

## 外设 compatible 索引

| Compatible       | 驱动  | 运行期语义                                                            |
|------------------|-------|-----------------------------------------------------------------------|
| `wch,ch32-gpio`  | GPIO  | [GPIO](../../../arch/wch/ch32/README.md#gpio)                         |
| `wch,ch32-uart`  | UART  | [UART](../../../arch/wch/ch32/README.md#uart)                         |
| `wch,ch32-spi`   | SPI   | [SPI 与 I2C](../../../arch/wch/ch32/README.md#spi-与-i2c)             |
| `wch,ch32-i2c`   | I2C   | [SPI 与 I2C](../../../arch/wch/ch32/README.md#spi-与-i2c)             |
| `wch,ch32-tmr`   | TMR   | [定时器与模拟外设](../../../arch/wch/ch32/README.md#定时器与模拟外设) |
| `wch,ch32-pwm`   | PWM   | [定时器与模拟外设](../../../arch/wch/ch32/README.md#定时器与模拟外设) |
| `wch,ch32-adc`   | ADC   | [定时器与模拟外设](../../../arch/wch/ch32/README.md#定时器与模拟外设) |
| `wch,ch32-dac`   | DAC   | [定时器与模拟外设](../../../arch/wch/ch32/README.md#定时器与模拟外设) |
| `wch,ch32-dma`   | DMA   | [DMA 与 Flash](../../../arch/wch/ch32/README.md#dma-与-flash)         |
| `wch,ch32-flash` | FLASH | [DMA 与 Flash](../../../arch/wch/ch32/README.md#dma-与-flash)         |
| `wch,ch32-eth`   | ETH   | [Ethernet](../../../arch/wch/ch32/README.md#ethernet)                 |
| `wch,ch32-iwdg`  | WDT   | [IWDG 和 WWDG](../../../arch/wch/ch32/README.md#iwdg-和-wwdg)         |
| `wch,ch32-wwdg`  | WDT   | [IWDG 和 WWDG](../../../arch/wch/ch32/README.md#iwdg-和-wwdg)         |

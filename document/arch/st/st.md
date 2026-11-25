# ST（STM32）平台适配规范

本文规定 `mrX/arch/st/stm32/` 的厂商级目录结构、接入约定、HardTree 命名与 HAL
变体探测原则；具体芯片外设的运行期行为与能力限制见 [STM32 平台适配](../../../arch/st/stm32/README.md)
，内核通用 `port` 契约见 [平台移植层](../../platform/platform.md)。

| 项目         | 说明                                                                                                                                                                                   |
|--------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 源码目录     | `mrX/arch/st/stm32/`                                                                                                                                                                   |
| 平台名称     | `st,stm32`                                                                                                                                                                             |
| 平台 Kconfig | `MR_USE_STM32`、`MR_CFG_STM32_HAL_HEADER`、`MR_USE_DCACHE`、`MR_USE_ICACHE`、`MR_CFG_STM32_CLOCK_IRQ_PRIORITY`、`MR_CFG_STM32_XTRACE_RTT_UP_SIZE`、`MR_CFG_STM32_XTRACE_RTT_DOWN_SIZE` |
| 运行期文档   | [STM32 平台适配](../../../arch/st/stm32/README.md)                                                                                                                                     |

## 目录结构与接入

```text
mrX/arch/st/stm32/
├── install.toml                    # 平台名 st,stm32 与复制规则
├── include/platform/
│   ├── Kconfig                     # 平台配置项
│   ├── mr_platform.h               # mr_platform_init 与 ST 专属辅助接口
│   └── mr_stm32_tim.h              # TMR 与 PWM 共用的定时器辅助内联接口
└── platform/
    ├── platform.c                  # mr_platform_init：初始化时钟端口
    ├── port/                       # clock、irq、output、assert、cache、trace 端口实现
    └── drivers/                    # 片上外设 backend
```

接入 STM32 Cube/HAL 工程的基本步骤：

1. 执行 `platform install -i arch/st/stm32/install.toml` 安装平台，在 `kconfig`
   中将 `MR_CFG_STM32_HAL_HEADER` 设为板级 HAL 伞头文件（如 `"stm32f4xx_hal.h"`、
   `"stm32g4xx_hal.h"`）；若芯片无 DWT CYCCNT，需关闭 `MR_USE_CLOCK_HIRES`。
2. 编写板级 `.hts` 并用 `htc` 生成 `mr_hardgen.h`；GNU ld 工程使用 `sections`
   注入托管段规则（Keil MDK 无需执行 `sections`）。
3. 在 `HAL_Init()` 与系统时钟配置完成后调用 `mr_user_init()`；平台接管
   `HAL_IncTick()`，由 SysTick 中断每节拍调用一次。

## HardTree 命名约定

ST 适配的 `compatible` 统一为 `st,stm32-<外设>`，属性统一使用 `st,` 前缀（生成头文件时
`-` 自动映射为 `_`，如 `st,exti-mask` 对应 `st_exti_mask`）：

| 属性形态                                        | 约定                                                                          |
|-------------------------------------------------|-------------------------------------------------------------------------------|
| `st,gpiox`、`st,uartx`、`st,timx` 等            | HAL 外设寄存器实例，直接填写芯片头文件中的实例符号                            |
| `st,clken`                                      | 板级 `void function(void)` 回调，负责使能对应外设总线时钟（后端在配置前调用） |
| `st,irqn` / `st,irq-handler`                    | 中断号与处理函数名，必须成对提供且与启动文件向量表符号一致                    |
| `st,irq-priority`                               | NVIC 抢占优先级，缺省为 `0`                                                   |
| GPIO 引用 `<&gpio节点 pin>`                     | `[phandle, pin]` 元胞，`pin` 范围为 `0..15`                                   |
| DMA 引用 `<&dma节点 channel>`                   | `[phandle, channel]` 元胞，`channel` 与 `st,requests` 数组下标对应            |
| 数字掩码属性（`st,exti-mask`、`exist-mask` 等） | 以整数掩码书写，缺省值见运行期文档                                            |

带复用引脚的外设（如 UART、PWM、ETH）通过 GPIO 后端的
`mr_stm32_gpio_pin_alternate()` / `mr_stm32_gpio_pin_release()` 接管和释放引脚，板级
HardTree 只提供引脚与 AF 编号。

## 资源归属与 HAL 变体探测

- **板级负责**：时钟树与 `SystemCoreClock` 维护、时钟使能回调、向量表、链接脚本与内存布局、外部
  PHY 参考时钟等板级条件。
- **平台后端负责**：外设寄存器配置、引脚复用接管、中断使能与事件分发、缓存维护。
- **变体探测原则**：通用平台包不绑定具体 STM32 系列，外设差异（GPIO 布局、UART
  数据寄存器、DMA 形态、EXTI 布局等）一律通过 `MR_CFG_STM32_HAL_HEADER`
  引入的宏在编译期探测——探测到对应能力则按该分支编译，缺少必需能力时触发 `#error`
  ，缺少可选能力时降级返回 `-MR_ENOSYS`。

## 外设 compatible 索引

| Compatible       | 驱动  | 运行期语义                                                    |
|------------------|-------|---------------------------------------------------------------|
| `st,stm32-gpio`  | GPIO  | [GPIO](../../../arch/st/stm32/README.md#gpio)                 |
| `st,stm32-uart`  | UART  | [UART](../../../arch/st/stm32/README.md#uart)                 |
| `st,stm32-spi`   | SPI   | [SPI 与 I2C](../../../arch/st/stm32/README.md#spi-与-i2c)     |
| `st,stm32-i2c`   | I2C   | [SPI 与 I2C](../../../arch/st/stm32/README.md#spi-与-i2c)     |
| `st,stm32-tmr`   | TMR   | [定时器与 PWM](../../../arch/st/stm32/README.md#定时器与-pwm) |
| `st,stm32-pwm`   | PWM   | [定时器与 PWM](../../../arch/st/stm32/README.md#定时器与-pwm) |
| `st,stm32-adc`   | ADC   | [ADC 与 DAC](../../../arch/st/stm32/README.md#adc-与-dac)     |
| `st,stm32-dac`   | DAC   | [ADC 与 DAC](../../../arch/st/stm32/README.md#adc-与-dac)     |
| `st,stm32-dma`   | DMA   | [DMA](../../../arch/st/stm32/README.md#dma)                   |
| `st,stm32-flash` | FLASH | [Flash](../../../arch/st/stm32/README.md#flash)               |
| `st,stm32-eth`   | ETH   | [Ethernet](../../../arch/st/stm32/README.md#ethernet)         |
| `st,stm32-wdt`   | WDT   | [IWDG](../../../arch/st/stm32/README.md#iwdg)                 |

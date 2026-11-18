# ST（STM32）平台适配规范

> 本文对应源码目录 `mrX/arch/st/`，规定 ST 厂商适配的目录结构、命名约定与分层归属。
> 具体芯片的运行期语义（时间单位、中断顺序、能力限制）由源码侧的
> [STM32 平台适配](../../arch/st/stm32/README.md) 管理，不在本文重复。

| 项目     | 说明                                            |
|----------|-------------------------------------------------|
| 源码目录 | `mrX/arch/st/stm32/`                            |
| 平台名称 | `st,stm32`                                      |
| 平台 Kconfig | `MR_USE_STM32`（固定启用）与平台级 `MR_CFG_*` |
| 参考文档 | [平台移植层](../platform/platform.md)、[芯片运行期语义](../../arch/st/stm32/README.md) |

## 1. 规范范围

本文与相邻文档的分工：

- [平台移植层](../platform/platform.md) 规定 `mr_port_*` 内核契约与移植步骤，对全部
  厂商生效，本文不重复。
- 各驱动目录下的 `porting.md` 规定该类外设的 ops 契约与验收，本文不重复。
- 本文规定 ST 适配的**厂商级约定**：目录结构、安装方式、HardTree 命名、资源归属和
  HAL 变体探测原则。
- [STM32 平台适配](../../arch/st/stm32/README.md)
  规定该平台的**运行期行为**：每个 compatible 的属性语义、量化规则、错误码和能力边界。
  修改平台源码行为时同步更新该文档。

## 2. 目录结构

```text
mrX/arch/st/stm32/
├── install.toml                    # 平台名 st,stm32 与复制规则
├── include/platform/
│   ├── Kconfig                     # 平台配置项
│   └── mr_platform.h               # mr_platform_init 与 ST 专属辅助接口
└── platform/
    ├── platform.c                  # mr_platform_init：初始化时钟端口
    ├── port/                       # clock、irq、output、assert、cache 端口实现
    └── drivers/                    # 片上外设 backend（gpio、uart、dma……）
```

安装目标为 `mrX/platform/` 与 `mrX/include/mrX/platform/`，安装流程见
[平台移植层](../platform/platform.md#11-选择并安装已有平台)。同一输出目录同时只能安装
一个平台。

## 3. 平台接入约定

接入统一走 Cube/HAL 工程，步骤概要（细节见
[STM32 平台适配](../../arch/st/stm32/README.md)）：

1. `platform install` 安装平台，Kconfig 中把 `MR_CFG_STM32_HAL_HEADER` 指向板级 HAL
   伞头文件（如 `stm32f4xx_hal.h`、`stm32g4xx_hal.h`）。
2. 板级 HardTree 描述外设实例，`htc -i` 生成 `mr_hardgen.h`。
3. 应用的 ld 脚本经 `sections` 工具收集 mrX 注册段。
4. `HAL_Init()` 与时钟配置完成后调用 `mr_user_init()`；平台接管 `HAL_IncTick()`，
   SysTick 处理函数每节拍调用一次 `HAL_IncTick()`。

## 4. HardTree 命名约定

ST 适配的 compatible 统一为 `st,stm32-<外设>`，节点属性统一使用 `st,` 前缀；HardTree
属性名中的连字符在生成头文件时映射为下划线（`st,exti-mask` 对应源码中的
`st_exti_mask`）。

| 属性形态 | 约定 |
| --- | --- |
| `st,gpiox`、`st,uartx`、`st,timx` 等 | HAL 寄存器实例，直接写芯片头文件中的实例符号 |
| `st,clken` | 板级 `void function(void)`，负责使能所选外设的总线时钟；后端在每次配置前调用 |
| `st,irqn` / `st,irq-handler` | 中断编号与处理函数必须成对提供；处理函数名必须与启动文件向量表条目一致，mrX 不重定向向量 |
| `st,irq-priority` | NVIC 优先级，缺省 0 |
| GPIO 引用 `<&gpio节点 pin>` | 属性元胞为 `[phandle, pin]`，pin 为 0–15 |
| DMA 引用 `<&dma节点 channel>` | 属性元胞为 `[phandle, channel]`，channel 与 `st,requests` 数组下标一致 |
| 数字掩码属性（`st,exti-mask`、`exist-mask` 等） | 以整数掩码书写，缺省值在运行期文档中说明 |

带复用引脚的外设（UART、PWM、ETH）通过 GPIO 后端的
`mr_stm32_gpio_pin_alternate()` / `mr_stm32_gpio_pin_release()` 接管和释放引脚；板级
HardTree 只提供引脚与 AF 号，不直接配置引脚寄存器。

## 5. 资源归属

- **板级负责**：时钟树与 `SystemCoreClock` 维护、时钟使能回调、启动文件向量表、
  ld 脚本与内存布局、PHY 参考时钟等外部条件。
- **平台后端负责**：外设寄存器配置、引脚复用接管、中断使能与事件分发、缓存维护（启用
  缓存端口时）。
- **框架负责**：驱动注册、等待队列、超时与事件语义，契约以各驱动
  `porting.md` 为准。

后端与板级的分界原则：凡是同一芯片上随板级设计变化的内容（引脚、时钟使能、中断符号、
DMA 请求映射）都由 HardTree 属性交给板级声明，后端不做猜测。

## 6. HAL 变体探测原则

通用平台包不绑定具体系列。外设能力差异（GPIO 布局、UART 数据寄存器、DMA IP 形态、EXTI
布局、采样时间档位表等）一律通过所选 HAL 头文件的宏在编译期探测：

- 能探测到对应能力时按该布局编译；
- 探测不到且该能力为必需时 `#error` 提示更换 HAL 头文件；
- 可选能力探测不到时降级返回 `-MR_ENOSYS`。

新增系列支持时，只扩展对应驱动的变体探测分支，不引入厂商级编译开关。

## 7. 外设 compatible 索引

| Compatible | 驱动 | 运行期语义 |
| --- | --- | --- |
| `st,stm32-gpio` | GPIO | [GPIO](../../arch/st/stm32/README.md#gpio) |
| `st,stm32-uart` | UART | [UART](../../arch/st/stm32/README.md#uart) |
| `st,stm32-tmr` | TMR | [定时器与 PWM](../../arch/st/stm32/README.md#定时器与-pwm) |
| `st,stm32-pwm` | PWM | [定时器与 PWM](../../arch/st/stm32/README.md#定时器与-pwm) |
| `st,stm32-adc` | ADC | [ADC 与 DAC](../../arch/st/stm32/README.md#adc-与-dac) |
| `st,stm32-dac` | DAC | [ADC 与 DAC](../../arch/st/stm32/README.md#adc-与-dac) |
| `st,stm32-dma` | DMA | [DMA](../../arch/st/stm32/README.md#dma) |
| `st,stm32-flash` | FLASH | [Flash](../../arch/st/stm32/README.md#flash) |
| `st,stm32-eth` | ETH | [Ethernet](../../arch/st/stm32/README.md#ethernet) |
| `st,stm32-wdt` | WDT | [IWDG](../../arch/st/stm32/README.md#iwdg) |

各 compatible 的使用条件（依赖的 `MR_USE_DRIVER_*` 宏）由对应驱动源码和
`porting.md` 决定；enable 属性、status 语义与节点组织遵循
[框架使用指南：HTC](../usage.md#4-htc-硬件描述编译)。

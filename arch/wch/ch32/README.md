# WCH CH32 平台适配

WCH EVT SDK 共用一个 mrX 平台包。Kconfig 选择 SDK，实际芯片型号、封装、启动文件、链接脚本和引脚由板级工程决定。

当前平台提供 SysTick、高分辨率时钟、中断临界区、诊断输出和断言端口。片上驱动按 mrX 已有驱动类别接入，实际实例由芯片型号、封装和板级 HardTree 决定。

| Kconfig | WCH EVT SDK | 设备头文件 |
| --- | --- | --- |
| `MR_USE_CH32V00X` | [CH32V003](https://github.com/openwch/ch32v003) | `ch32v00x.h` |
| `MR_USE_CH32VM00X` | [CH32V002/004/005/006/007](https://github.com/openwch/ch32v002_004_005_006_007) | `ch32v00X.h` |
| `MR_USE_CH32V10X` | [CH32V103](https://github.com/openwch/ch32v103) | `ch32v10x.h` |
| `MR_USE_CH32V20X` | [CH32V20x](https://github.com/openwch/ch32v20x) | `ch32v20x.h` |
| `MR_USE_CH32V205` | [CH32V205 / CH32V203CCT6](https://www.wch.cn/downloads/CH32V205EVT_ZIP.html) | `ch32v205.h` |
| `MR_USE_CH32V30X` | [CH32V30x](https://github.com/openwch/ch32v307) | `ch32v30x.h` |
| `MR_USE_CH32V4X7` | [CH32V407 / CH32V467](https://github.com/openwch/ch32v407_ch32v467) | `ch32v4x7.h` |

板级 EVT 工程选择对应的设备宏。板级 HTS 按实际芯片和封装声明外设实例、引脚、
IRQ 和 DMA 请求。每个物理控制器只声明一个节点，IRQ 编号与处理函数按 EVT 启动向量成对填写。

## 接入 EVT 工程

1. 在包含 `scripts/cli.py` 的 mrX 目录运行下列命令，然后在 Platform 菜单选择 SDK。启用片上驱动时，添加板级 HardTree 节点并用 `htc -i` 生成 `mr_hardgen.h`。

   ```powershell
   python ./scripts/cli.py platform install -i ./arch/wch/ch32/install.toml
   python ./scripts/cli.py kconfig
   ```

2. 在 MounRiver Studio 或其他使用 WCH RISC-V GCC 的工程中，沿用目标芯片的 EVT 启动文件、`SystemInit`、型号宏和编译选项。将 EVT 的 Core、Peripheral/inc 和包含 `system_ch32*.h` 的目录加入头文件搜索路径。按 [mrX 接入指南](../../../document/usage.md) 加入框架源码与 `platform/` 下的已安装源码。使用芯片对应的 `-march` 与 `-mabi`，工具链须支持 `interrupt("WCH-Interrupt-fast")`。V00x 可使用 mrX 内置的中断屏蔽式原子操作。

3. 对应用实际使用的 EVT GNU ld 脚本运行 `python ./scripts/cli.py sections -i <Link.ld 路径>`，保留原有内存布局和向量表，并在 `.text` 中收集 mrX 注册段。

4. EVT 启动代码完成 `SystemInit` 后进入 `main()`。板级代码先初始化诊断通道，再调用 `mr_user_init()`。mrX 接管 `SysTick_Handler` 和 SysTick 寄存器，应用中如有同名处理函数须移除。运行期间保持 `SystemCoreClock` 对应的时钟频率，并使用 mrX 延时接口，EVT 的 `Delay_Us` 和 `Delay_Ms` 会重配同一个计数器。

CH32V003 只有 2 KiB SRAM。使用内置堆时，通用配置的 4096 字节堆超出芯片容量。按板级内存预算缩小 `MR_CFG_HEAP_SIZE`，并为静态数据和栈预留空间。V00x、VM00X、V205 和 V4x7 的 SysTick 计数器为 32 位，连续两次 SysTick 中断服务的间隔应短于一个计数回绕周期（48 MHz 下约 89 秒，200 MHz 下约 21 秒）。启用 `MR_USE_IRQ_DEFER` 时，由应用主循环调用 `mr_irq_defer_execute()`。

CH32V006F4U6 使用 VM00X SDK 的 `CH32V006` 选择，板级链接脚本使用 16 KiB CodeFlash
和 4 KiB SRAM，内部 Flash 节点的 `wch,flash-limit` 填写 `<16384>`。该型号只声明
USART1 节点。其余 V006 型号的 CodeFlash 为 62 KiB、SRAM 为 8 KiB。
[CH32V006 数据手册，型号资源表和表 2-1-2](https://ch32-riscv-ug.github.io/CH32V006/datasheet_zh/CH32V006DS0.PDF)

默认 `MR_USE_CH32_DEBUG_OUTPUT` 将诊断输出交给 EVT `Debug/debug.c` 的 `_write`，并需要将 `Debug` 加入头文件搜索路径。使用 USART 日志时，加入 SDK 的 RCC、GPIO 和 USART 实现，在 `mr_user_init()` 前调用 EVT 的 `USART_Printf_Init`，并为日志保留独立于 mrX UART 实例的外设。若板级工程接管日志，关闭该配置并覆盖 `mr_ch32_platform_output()`。输出通过轮询发送，重入返回 `-MR_EBUSY`。

使用 SDI 时，`debug.c` 和平台的 `output.c` 使用相同的 `SDI_PRINT` 设置。连接调试器，并在 `mr_user_init()` 前调用 `SDI_Printf_Enable()`。断言现场也保存在 `__assert_context` 中，可用调试器读取。

CH32V203F6P6 使用 EVT 串口日志时，将 EVT 的 `DEBUG` 设为 `DEBUG_UART2`。
需要将 USART2 交给 mrX UART 驱动时，可使用 SDI 或板级输出回调。

### CH32V103

选择 `CH32V103 (CH32V10x SDK)`，使用 EVT 的 `startup_ch32v10x.S` 和
`system_ch32v10x.c`。64 位 SysTick 使用 HCLK/8，向量入口为
`SysTick_Handler`。应用接管 EVT 示例时移除其中同名的中断处理函数。
使用 EVT 的 RCC 查询及串口日志时，链接 `ch32v10x_rcc.c` 和日志所需的 GPIO、
USART 实现。

按具体封装配置 GPIO、USART、SPI、I2C、TMR、PWM、ADC、DMA、内部 Flash 和看门狗。
TIM1 使用 APB2 定时器时钟，其余定时器使用 APB1 定时器时钟。I2C 标准模式要求
PCLK1 为 2–36 MHz，快速模式为 4–36 MHz。内部 Flash 使用 1 KiB 标准页擦除和
半字编程，需链接 `ch32v10x_flash.c`。

### CH32V205 / CH32V203CCT6

选择 `CH32V205/V203CCT6 (CH32V205 SDK)`，沿用该 EVT 的
`startup_ch32v205.S`、`system_ch32v205.c` 和目标型号链接脚本。
青稞 V3B 使用 32 位 SysTick、`SysTick_IRQn` 与 `SysTick_Handler`，沿用 EVT 的
全局中断和硬件压栈设置。原子操作可使用 mrX 内置的中断屏蔽实现。
使用 EVT 串口日志或查询外设时钟时，加入 `ch32v205_rcc.c` 及日志所需的 GPIO、
USART 实现。

GPIO 时钟掩码使用 EVT 的 `RCC_IOPxEN`。USART1 与 SPI1 使用 PCLK2，其余
USART 和 SPI2 使用 PCLK1。流控按封装引脚选择。TIM1 使用 PB2 定时器时钟，
TIM2–4 使用 PB1 定时器时钟。TIM4 的重装和比较寄存器为 32 位，板级中断属性使用
`TIM4_IRQn` 和 `TIM4_IRQHandler`。I2C 的 PCLK1 上限为 60 MHz。DMA1、DMA2
各有 8 个通道，节点索引为 0–7。内部 Flash 使用 256 字节页编程和擦除。

### CH32V407 / CH32V467

选择 `CH32V407/V467 (CH32V4x7 SDK)`，使用板级 EVT 的 `startup_ch32v4x7.S` 和
`system_ch32v4x7.c`。mrX 使用 `SysTick_IRQn` 和 `SysTick_Handler`。沿用 EVT 的
全局中断与硬件压栈设置。使用 EVT 串口日志时，链接 `ch32v4x7_rcc.c`、
`ch32v4x7_gpio.c` 和 `ch32v4x7_usart.c`。

V4x7 可接入本页列出的驱动类别，包括 GPIO、USART、SPI、I2C、TMR、PWM、
ADC、DAC、DMA、内部 Flash、Ethernet、IWDG 和 WWDG。端口时钟掩码使用 V4x7 SDK 的
`RCC_IOPxEN`，外设实例、引脚复用、DMA 请求和中断向量按具体芯片和封装配置。

## Cache

当前 SDK 对应的芯片没有可由此端口管理的 CPU 缓存。V4x7 与其余系列共用取指同步实现，
Flash 预取和加速由板级 EVT 工程配置。
平台不提供 `MR_USE_DCACHE`，手动定义该宏会触发编译错误。

在平台 Kconfig 中打开 `MR_USE_ICACHE` 后，可以通过 `mr_port_icache_invalidate()` 和
`mr_port_icache_invalidate_all()` 执行本核取指同步。非空区间和全局操作均执行 `fence.i`，
使本核后续取指看到已对本核可见的代码写入。区间操作不需要地址对齐，长度为 0 时直接返回成功。
`mr_port_icache_line_size()` 返回 0，`mr_port_icache_enable()` 和
`mr_port_icache_disable()` 返回 `-MR_ENOSYS`。该配置默认关闭。

## GPIO

`wch,ch32-gpio` 每个节点对应一个 GPIO 端口。`wch,gpiox` 指定 SDK 寄存器实例，`wch,clock-mask` 指定 RCC 端口时钟位，`wch,port-source` 指定 AFIO 路由编码。`exist-mask` 只列出当前芯片封装实际引出的引脚。V00x/VM00X 每端口最多 8 位，其余家族最多 16 位。

`wch,exti-mask` 使用数字掩码，缺省为 0。配置上升沿、下降沿或双边沿后，才能订阅并启用相应 EXTI 事件。同号 EXTI 线在不同端口之间互斥，切换到普通 GPIO 模式会释放该线并取消其等待项。输出配置支持推挽与开漏，内部上下拉用于输入。mrX 接管掩码所涉及的整组 PFIC 向量。等待回调在中断中同步执行，分发期间屏蔽嵌套中断，回调应保持简短。

需要归还 EXTI 线时，在 `mr_driver_shutdown()` 前将对应引脚切回普通 GPIO 模式。

以下以 CH32V003 的 GPIOC 为例，使用 PC2 的 EXTI。

```dts
gpio_c: gpio-c {
    compatible = "wch,ch32-gpio";
    status = "early";
    wch,gpiox = GPIOC;
    wch,clock-mask = RCC_IOPCEN;
    wch,port-source = <2>;
    exist-mask = <0xff>;
    wch,exti-mask = <0x04>;
};
```

## UART

`wch,ch32-uart` 依赖 CH32 GPIO 后端。`wch,uartx` 选用目标设备的 `USARTx` 或 `UARTx`，`tx` 和 `rx` 各使用 `<&gpio节点 pin>`，按实际启用的方向提供。`wch,clken` 指定板级 `void function(void)` 函数，用来打开 USART 时钟和设置所需的 AFIO 重映射。编译所选系列的 EVT RCC 实现供 UART 查询外设时钟。

支持轮询收发，帧格式为 8N、7E/O、8E/O，支持 1、1.5 或 2 个停止位。可选 `wch,irqn` 和 `wch,irq-handler` 必须成对提供，使用 EVT 启动向量的 IRQ 编号及符号。`wch,irq-priority` 为 PFIC 编码值，缺省 0。轮询数据路径可上报 `RX_READY` 与 `TX_DONE`。

硬件流控使用 `rts` 和 `cts` GPIO spec。板级工程按芯片封装和 AFIO 重映射选择
实际可用的流控引脚，包括 V4x7 的 USART1–10 和具备流控引脚的 V203 UART4。
RTS 需要启用接收，CTS 需要启用发送。请求流控方向时须提供对应的 GPIO spec。
[WCH V4x7 参考手册，表 10-21 至 10-30](https://ch32-riscv-ug.github.io/CH32V407/datasheet_zh/CH32V407RM.PDF) ·
[WCH V20x/V30x 参考手册，表 10-27](https://ch32-riscv-ug.github.io/CH32V20x/datasheet_en/CH32FV2x_V3xRM.PDF)

配置 UART 向量可订阅 `RX_IDLE` 和线路 `RX_ERROR`。启动 RX DMA 前应先关闭
`RX_IDLE` 并读完中断暂存的字节。RX DMA 运行期间，`RX_ERROR` 仅报告 DMA 传输错误。

启用 UART stream 和 DMA 后，可用 `rx-dma`、`tx-dma` 指定独占的 DMA 通道 `<&dma节点 channel>`。RX stream 需要 8 位数据帧。单次 TX stream 还需要 UART 中断，用于确认最后一个字节已移出；循环 TX stream 按 DMA 缓冲区回绕报告完成。配置 UART、切换到轮询或关闭驱动前，先停止已启动的 stream。在 `mr_driver_shutdown()` 前关闭已启用的 UART 事件。板级工程负责后续外设时钟和引脚的交接。

V4x7 使用 EVT 的 `USART1` 至 `USART10` 实例名，USART1 取 PCLK2，其余取 PCLK1。
仅在具体封装引出对应引脚、且芯片请求表提供所选方向的 DMA 请求时启用相应功能。

CH32V003 的 USART1 默认使用 PD5 发送和 PD6 接收。以下节点使用这组引脚，板级 `board_uart1_clock_enable()` 调用 `RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE)`，诊断通道可选 SDI。

```dts
gpio_d: gpio-d {
    compatible = "wch,ch32-gpio";
    status = "early";
    wch,gpiox = GPIOD;
    wch,clock-mask = RCC_IOPDEN;
    wch,port-source = <3>;
    exist-mask = <0x60>;
};

uart1: serial-1 {
    compatible = "wch,ch32-uart";
    status = "okay";
    wch,uartx = USART1;
    wch,clken = board_uart1_clock_enable;
    tx = <&gpio_d 5>;
    rx = <&gpio_d 6>;
    wch,irqn = USART1_IRQn;
    wch,irq-handler = USART1_IRQHandler;
};
```

## SPI 与 I2C

| Compatible | HardTree 属性 | 能力 |
| --- | --- | --- |
| `wch,ch32-spi` | `wch,spix`、`wch,prepare`、`cs`，可选 `wch,timeout-us`（默认 1000） | SPI master，模式 0–3，8/16 位，MSB/LSB。`cs` 使用 `<<&gpio0 4> <&gpio1 0>>`，一次消息组共用一次片选。缺少的 TX 帧发送全 1。 |
| `wch,ch32-i2c` | `wch,i2cx`、`wch,prepare`，可选 `wch,timeout-us`（默认 25000） | I2C master，标准/快速模式，7/10 位地址、重复 START 和 STOP 分组。支持零长度写地址探测。 |

SPI 和 I2C 的 `wch,prepare` 指向板级 `void function(void)`，负责使能控制器时钟、选择重映射并配置总线引脚。SPI 主机后端管理低有效 GPIO 片选，GPIO 节点需先完成 setup。启用 `MR_USE_DRIVER_SPI_STREAM` 并声明成对的 `wch,irqn`、`wch,irq-handler` 后，SPI 主机可使用中断驱动的单次或循环 stream，可选配置 `wch,irq-priority`。

SPI 从机声明 `wch,slave` 和同一组 SPI 向量属性。`cs` 只列出一个硬件 NSS 输入，该引脚应支持双边沿 GPIO EXTI，以报告片选和释放。收发各有 16 帧队列，支持 8 位和 16 位帧。主机须在 NSS 拉低到首帧之间以及帧间留出足够的中断处理时间。关闭驱动前应在 NSS 释放时将 `clock_rate` 配置为 0。

I2C 的 SCL、SDA 应配置为复用开漏并接外部上拉。I2C 仲裁丢失返回 `-MR_EAGAIN`，总线忙返回 `-MR_EBUSY`，NACK/总线错误返回 `-MR_EIO`，等待超时返回 `-MR_ETIMEDOUT`。控制器可复位自身状态机；外部设备持续拉低总线时，由板级处理总线恢复。I2C 主机末消息仍须发送 STOP。

I2C 从机需同时声明 `wch,ev-irqn`、`wch,ev-irq-handler`、`wch,er-irqn` 和 `wch,er-irq-handler`，可选 `wch,irq-priority`。读写使用各 32 字节的 FIFO，调用可返回短计数。RX 满时丢弃新到字节并上报错误。TX 无待发数据时发送 `0xff`。关闭驱动前，先将时钟配置为 0 退出从机模式。

V4x7 使用 `I2C1`，配置驱动前须将 PCLK1 设为 4–60 MHz。`wch,ch32-spi` 使用 SPI 模式，
SPI1 取 PCLK2，SPI2/3 取 PCLK1。按封装选择可用的 SPI 实例和引脚。

## 定时器与模拟外设

| Compatible | HardTree 属性 | 能力 |
| --- | --- | --- |
| `wch,ch32-tmr` | `wch,timx`、`wch,clken`，可选成对的 `wch,irqn`、`wch,irq-handler` 和 `wch,irq-priority` | 周期或单次计数，周期单位为 ns，可通过更新事件输出 TRGO，配置向量后可上报超时。 |
| `wch,ch32-pwm` | `wch,timx`、`wch,clken`、`channel-mask`、`wch,ch1` 至 `wch,ch4` GPIO spec。可选 `comp-mask` 和对应的 `wch,ch1n` 至 `wch,ch3n`，以及成对的 `wch,irqn`、`wch,irq-handler` 和 `wch,irq-priority` | 使用同一计数器的 PWM 通道。掩码位 0 对应硬件 CH1。可通过 TRGO 输出周期信号，配置更新中断向量后可上报 `MR_PWM_WAIT_PERIOD`。 |
| `wch,ch32-adc` | `wch,adcx`、`wch,clken`、`exist-mask`，可选 `wch,ref` 和 `ref-internal` | 单端采样。V003 为 10 位，其余为 12 位。通道索引对应 ADC 硬件通道。 |
| `wch,ch32-dac` | `wch,clken`、`exist-mask`，可选 `wch,ref` 和 `ref-internal` | 适用于 V30x 和 V4x7 中具有 DAC 的型号。通道掩码位 0/1 对应硬件通道 1/2，支持 12 位即时输出。 |

`wch,clken` 指向板级 `void function(void)`。TMR 和 PWM 分别独占所选的 TIM 实例。
CH32V203RBT6（EVT D8）和 CH32V208（D8W）的 TIM5 使用 32 位重装与比较寄存器，
板级中断属性使用 `TIM5_IRQn` 和 `TIM5_IRQHandler`。D6 板级工程使用 TIM1–4。
16 位和 32 位定时器的预分频范围均为 1–65536。TMR 每周期最多
计数 65536 次或 2^32 次。PWM 的上限各少一次，以便比较值能够表示 100% 占空比。
周期与脉宽均按硬件可表示值量化，查询返回量化后的纳秒数。

V4x7 的 TIM1/8 取 PB2 时钟，TIM2–7 取 PB1 时钟。对应 PB 总线分频大于 1 时，
定时器时钟为该总线 PCLK 的两倍。TIM6/7 可作基础定时器，PWM 使用具备比较输出的实例。

CH32V006、CH32V007 和 CH32M007 可用 `wch,ch32-tmr` 接入简易定时器 TIM3。
将 `wch,timx` 设为 `TIM3`，由 `wch,clken` 的板级回调通过
`RCC_PB1PeriphClockCmd(RCC_PB1Periph_TIM3, ENABLE)` 开启时钟，省略 IRQ 属性。
TIM3 支持周期向上计数，周期量化为 2–65536 个输入时钟周期且无预分频。
单次模式和超时等待返回 `-MR_ENOSYS`。

- `wch,tim3-input-hz` 省略或为 0 时使用 HCLK。非零时将 TIM1 TRGO 接到 TIM3 的
  外部计数时钟，属性填入 TIM1 实际提供的固定脉冲频率（Hz）。应用先配置 TIM1
  周期信号并启动 TIM3，再启动 TIM1。TIM3 启动后即进入已使能状态，收到 TIM1
  触发才推进计数。
- `wch,tim3-dma-compare` 可选 `<3>` 或 `<4>`。配置后，启用
  `MR_TMR_SIGNAL_PERIOD` 会在该比较通道匹配重装值时产生 DMA 请求，每周期一次。
  V006/V007 的 CH3 对应 DMA1 硬件通道 1（mrX 索引 0），CH4 对应硬件通道 4
  （索引 3）。M007 按芯片手册选通道。应用先配置并启动对应 DMA，再启动 TIM3，
  停止 TIM3 时后端关闭请求。没有配置该属性时，周期信号返回 `-MR_ENOSYS`。
  停止后如需修改周期，先关闭已启用的周期信号。

- `wch,tim3-cc1-ticks` 和 `wch,tim3-cc2-ticks` 分别设置 TIM3 CH1/CH2 的匹配位置，
  单位为所选输入时钟的计数次数。有效值为 1 至量化后的周期计数减 1，超出范围时
  配置返回 `-MR_ERANGE`。省略或填 0 时由板级保留该比较寄存器的配置。匹配事件随
  TIM3 计数产生，ADC 侧负责启停注入组外部触发。使用板级 EVT 注入组采样时，先配置
  ADC 触发源并武装转换，再启动 TIM3。

V006/V007 的 TIM3 CH3/CH4 分别与 ADC 规则组和 USART1 TX 共用 DMA1 硬件通道 1 和 4，
同一时间每个通道只能分配给一个请求源。
[VM00X 参考手册，表 8-2、9-3、9-4](https://ch32-riscv-ug.github.io/CH32V006/datasheet_en/CH32V00XRM.PDF)

PWM 支持边沿向上计数、连续输出、周期边界更新和有效或无效空闲电平。高级定时器
还支持 `MR_PWM_ALIGN_CENTER`，周期和脉宽均按完整的上下计数周期以纳秒表示。
重复计数器让影子寄存器更新、周期中断和 TRGO 每完整周期发生一次。
周期边界可批量提交 PWM 占空比、强制有效和强制无效。`comp-mask` 为 0 时，计数器运行期间可配置和启动其他通道，新通道从当前计数相位开始输出。使用 `MR_PWM_SIGNAL_PERIOD` 启用 TRGO，信号偏移固定为 0。更新中断向量由 PWM 实例独占。

互补输出使用芯片具备的高级定时器 TIM1、TIM8、TIM9 或 TIM10。`comp-mask` 只置位硬件
CH1–CH3 中实际能引出主、互补两路的通道。在同一节点声明 `wch,chX` 和 `wch,chXn`，并
在通道配置中设置 `comp`。板级回调配置相应引脚复用。CH32V303CBT6 和 CH32V303RBT6 无 TIM8。
[V30x/V31x 数据手册](https://ch32-riscv-ug.github.io/CH32V307/datasheet_en/CH32V20x_30xDS0.PDF)

设有 `comp-mask` 的控制器使用硬件总输出门控和空闲电平。启动须一次包含全部已配置通道，停止须
一次包含全部运行通道，修改通道配置前须停组。主路和互补路可独立选择极性与空闲电平，两路同时
有效的空闲组合和高阻模式返回 `-MR_ENOSYS`。批量更新时，成对输出可使用 `PWM/PWM`、
`ACTIVE/INACTIVE` 或 `INACTIVE/ACTIVE`。调用 `mr_driver_shutdown()` 之前须先停止 PWM。

每对通道的 `dead_rise` 和 `dead_fall` 须相等。同个定时器中，各互补通道共用一个死区码；
编码冲突返回 `-MR_ENOSYS`，重新配置控制器可重设整组。死区使用预分频前的定时器时钟，
向上量化到可表示值，最大为 1008 个时钟周期。查询返回量化后的整数纳秒值。
[VM00X 参考手册，第 11 章高级定时器](https://ch32-riscv-ug.github.io/CH32V006/datasheet_en/CH32V00XRM.PDF)

高级定时器可声明 `fault-mask = <1>` 和 `wch,bkin = <&gpio节点 pin>`，将故障 0 接到
该定时器的 BKIN。板级须配置该型号的 BKIN 引脚复用和外部偏置。可选的 `wch,brk-irqn`、
`wch,brk-irq-handler` 成对使用，并选择独立于周期更新中断的 EVT 刹车向量。无中断配置时，
硬件关断与 `mr_pwm_state()` 的故障查询仍可使用。

先配置 PWM 控制器，再用 `mr_pwm_fault_configure()` 配置故障 0。当前支持高或低有效的锁存
模式，`filter` 填 0。配置 BKIN 的控制器成组启停，主路和互补路均使用无效空闲电平。触发时
定时器硬件清除总输出使能，`fault_mask` 置位。`running_mask` 保留本次启动的通道，供应用
停组。可选中断在锁存时报告 `MR_PWM_WAIT_FAULT`，通知开关独立于硬件保护。

恢复时先停止全部运行通道，待 BKIN 解除后再调用 `mr_pwm_fault_clear()`，随后显式调用
`mr_pwm_start()`。清除故障不会恢复输出。修改控制器时间基准之前，先清除故障并禁用 BKIN。
当前未接入硬件滤波及逐周期自动恢复。

ADC 时钟分频、模拟输入引脚和 DAC 模拟输出引脚由板级回调配置。ADC 的 `oversample` 字段在该后端用作采样时间编码（0–7），ADC/DAC 缓冲区使用 `uint16_t` 原始码值。

ADC 的 `rx-dma` 支持最多 16 个通道按编号递增扫描，可使用单次或循环缓冲，缓冲长度必须是完整扫描帧的整数倍。软件触发连续转换，外部触发使用板级 `wch,trigger` 指定的硬件选择值。具备对应内部通道的 V10x/V20x/V30x/V4x7 由 ADC1 采样通道 16/17。仅在芯片支持该 ADC 实例的 DMA 请求时声明 `rx-dma`。具备 ADC PGA 的型号可在外部通道选择 ×1、×4、×16 或 ×64，启用缓冲增益时采样时间编码须至少为 1，DMA 单次扫描所有通道的增益须一致。

CH32V203RBT6（D8）和 CH32V208（D8W）只声明 ADC1 节点。

VM00X 可为唯一的 `wch,ch32-adc` 节点声明 `wch,injected-trigger`，启用 `MR_USE_DRIVER_ADC_STREAM`
后使用 ADC1 的注入组中断流。该节点省略 `rx-dma` 和 `wch,trigger`，并独占 EVT 的
`ADC1_IRQHandler`。`wch,irq-priority` 可选，缺省为 0。中断流无需启用 DMA，普通轮询
`mr_adc_read()` 仍可在停流后使用。

`wch,injected-trigger` 使用 `JEXTSEL` 编码，0–3 分别选择 TIM1 CH3/CH4、TIM2 CH3/CH4，
4/5 选择 V006/V007/M007 的 TIM3 CH1/CH2，6 选择注入组外部引脚。VM00X 的规则组 DMA
使用 `EXTSEL`，其中 `wch,trigger = <4>` 和 `<5>` 分别选择 TIM2 CH1/CH2。
使用 TIM3 时，先配置相应的比较位置，调用 `mr_adc_stream_start()` 以
`MR_ADC_STREAM_SOURCE_EXTERNAL` 武装 ADC，再启动 TIM3。停止采集时先停止触发源，再停止 ADC 流。
[VM00X 参考手册，表 9-3、9-4 及第 13 章](https://ch32-riscv-ug.github.io/CH32V006/datasheet_en/CH32V00XRM.PDF)

注入组每次触发转换 1–4 个通道，按通道编号递增写入 12 位原始值，每个结果占 2 字节。
支持单次及循环缓冲，长度须为完整帧的整数倍，同一扫描所有通道使用相同增益。HALF 在写满至少
一半且缓冲尚未写满时按完整帧上报，FULL 在写满时上报。单次完成时 offset 保留总字节数，循环
写满时 offset 回绕至 0。注入组只支持 HALF 和 FULL 订阅。触发间隔须足够完成一次扫描并让
ADC IRQ 搬运结果，硬件缺少可用于检测丢帧的标志。

PWM 批量写入预装载寄存器期间会短暂屏蔽计数器更新事件。如果计数器在此期间跨越周期边界，该次 TRGO 和周期中断会被跳过，所提交的输出值在后续周期边界生效。

DAC 轮询输出支持 8 或 12 位有效码值，均使用 `uint16_t` 容器。DAC 的 `tx-dma` 使用 `wch,trigger` 时支持单通道外部触发循环输出。板级也可使用 `wch,dma-request` 或 `wch,software-dma-request` 回调控制同一个 `tx-dma` 通道对应的硬件请求源，以支持单次或循环以及双通道同步输出。回调签名为 `void function(mr_bool_t enable)`，可能在屏蔽中断时或 DMA IRQ 中调用，须同步开关请求且不可阻塞。双通道每帧按低半字 CH1、高半字 CH2 排列，使用 32 位对齐缓冲。ADC 和 DAC 均需要已配置中断的独占 DMA 通道。

## DMA 与 Flash

`wch,ch32-dma` 使用 `wch,dmax` 选择 `DMA1` 或 `DMA2`，`exist-mask` 按实际可用通道填写。通道索引从 0 开始。各通道可配置成对的 `wch,irqn-N`、`wch,irq-handler-N` 与控制器级 `wch,irq-priority`。固定请求和可重映射请求均由板级按芯片手册分配。单次传输用空 `next`，循环传输将 `next` 指向自身。

DMA 支持 8/16/32 位、地址递增或固定，以及外设与内存之间或内存到内存的传输。
内存到内存传输只支持单次模式。
`count` 表示传输项数，单次最多 65535 项。`offset` 返回源端字节进度。
V4x7 的 DMA1 支持硬件通道 1–13（索引 0–12），DMA2 支持通道 1–11（索引 0–10）。
缓冲区须位于所选 DMA 可访问的地址范围，IRQ 属性使用对应通道的 EVT 向量。

`wch,ch32-flash` 的 `wch,flash-offset`、`wch,flash-size` 以字节声明用户 Flash 内的一块擦除对齐区域，`wch,flash-limit` 必须填写当前具体芯片的物理 Flash 总容量。后端只接受完全落在 `[0, wch,flash-limit)` 内的窗口，该值应与目标芯片数据手册、EVT 型号宏和板级链接脚本一致。V003/V103 擦除粒度为 1 KiB，V20x/V30x 为 4 KiB，这些型号均以半字写入，并需链接对应的 EVT Flash 源码。VM00X 和 V205 SDK 对应的型号使用 256 字节页编程和擦除。

V20x/V30x 的擦写还受 Flash 控制器工作时钟约束。后端要求 Flash 时钟不超过 60 MHz，分别对应全速模式下 HCLK 不超过 60 MHz、半速模式下不超过 120 MHz。
启用增强读取模式的板级工程在擦写前退出该模式，模式仍有效时擦写返回 `-MR_EBUSY`。
用于擦除的代码须链接到零等待区。V003 擦写时，板级工程须保持 HSI 开启且稳定。
[WCH V20x/V30x 参考手册，第 32 章](https://ch32-riscv-ug.github.io/CH32V20x/datasheet_en/CH32FV2x_V3xRM.PDF) ·
[WCH V003 参考手册，第 16 章](https://ch32-riscv-ug.github.io/CH32V003/datasheet_en/CH32V003RM.PDF)

V4x7 链接 `ch32v4x7_flash.c`，以 4 KiB 扇区擦除、2 字节半字编程。
板级需把访问时钟设为 HCLK、HCLK/2 或 HCLK/4 中的一种，且不得超过 100 MHz。
Flash 窗口由板级预留，避开启动区和正在执行的固件。

## Ethernet

`wch,ch32-eth` 适用于 CH32V203RBT6、V208、V307、V317 和 V4x7。支持直接帧收发与 RX/TX 中断通知。V203RBT6（D8）和 V208（D8W）使用片内 10 Mbps MAC/PHY，收发缓冲区须落在实际 SRAM 容量内及 SRAM 起始的 64 KiB 窗口内。V307、V317 和 V4x7 使用描述符 DMA。V307 内置 PHY 为 10 Mbps，板级外接 PHY 可配置 100 Mbps。V317 和 V4x7 的内置 PHY 支持 10/100 Mbps。

声明 `mac`、`wch,prepare`，并按板级需要设置 `mtu`、`wch,irq-priority` 和 `wch,unprepare`。`wch,prepare` 为 `mr_err_t function(void)`，负责 Ethernet 时钟、PHY 和引脚的板级设置。可选的 `wch,unprepare` 为 `void function(void)`。V30x 可另设 `wch,internal-phy`（默认 1）、`wch,phy-address`（默认 1）及 `wch,mdio-div`（默认 `ETH_MACMIIAR_CR_Div42`）。板级工程须为所选 PHY 提供 EVT 规定的参考时钟，并按 HCLK 选择 MDIO 分频。后端占用 `ETH_IRQHandler`。

D8/D8W 的 `wch,prepare` 需要提供 60 MHz Ethernet 时钟。WCH 原始帧示例使用
120 MHz 系统时钟，调用 `RCC_ETHDIVConfig(RCC_ETHCLK_Div2)`。
[WCH V20x Ethernet 原始帧示例](https://github.com/openwch/ch32v20x/tree/main/EVT/EXAM/ETH/MAC_RAW)

| 型号 | IEEE 802.3x Pause | 校验和卸载 |
| --- | --- | --- |
| V203RBT6、V208 | 接收 Pause | 当前无卸载后端 |
| V307/V317、V4x7 | 收发 Pause | RX 校验、TX 生成 |

使用 `flow-rx` 和 `flow-tx` 配置 Pause 方向。仅在全双工生效。自动协商按两端能力决定实际
启用方向，固定模式由两端按相同链路参数配置。通过 `mr_eth_link()` 更新 MAC 并读取实际方向。
描述符型 MAC 在 RX 描述符耗尽时请求发送 32 个时隙的 Pause。V203RBT6 和 V208 的 Pause 发送尚未接入，
请求该方向、校验和卸载或固定半双工流控均返回 `-MR_ENOSYS`。
[WCH V20x 设备头](https://github.com/openwch/ch32v20x/blob/main/EVT/EXAM/SRC/Peripheral/inc/ch32v20x.h) ·
[WCH V30x Ethernet SDK](https://github.com/openwch/ch32v307/blob/main/EVT/EXAM/SRC/Peripheral/inc/ch32v30x_eth.h)

描述符型 MAC 通过 `csum-rx` 丢弃硬件识别的校验错误帧。`csum-tx` 处理无 VLAN 标签的
Ethernet II IPv4 与基本 IPv6 帧。IPv4 分片只生成 IP 首部校验和，完整的 IPv4 TCP/UDP/ICMP
以及无扩展头的 IPv6 TCP/UDP/ICMPv6 帧生成传输层校验和。其余帧由应用提供校验和。
直接收发与 Stream 使用同一套卸载设置。

启用 `MR_USE_DRIVER_ETH_STREAM` 后，RX 和 TX 可独立启停，各允许一个在途帧。流与同方向
的直接收发互斥。DMA 使用后端自有缓冲区，提交的应用缓冲区通过 CPU 复制收发。RX 可在链路
建立前提交，缓冲不足时丢弃整帧并通过 `MR_ETH_WAIT_RX_ERROR` 归还。RX 再次提交时会处理
已经留在硬件缓冲区中的帧。TX 启动时查询链路，运行期间需从应用调用 `mr_eth_link()` 更新
链路状态，TX 提交和完成回调无需等待 MDIO。

停流、关闭或重新配置端口时，以 `-MR_EPERM` 归还受影响方向的在途帧。单独停止 TX 流时，
已经复制到后端缓冲区的传输可能继续完成，其硬件完成中断不会重复归还原帧。端口关闭或重新
配置会复位传输引擎。DMA 总线致命错误会退出两路流并以 `-MR_EIO` 归还在途帧，重新配置
端口后可重新启动流。

V317 使用 EVT 的 `CH32V30x_D8C` 设备宏，在 `wch,ch32-eth` 节点加入布尔属性
`wch,internal-rmii-phy;`，并由 `wch,prepare` 按 V317 EVT 配置
片内 RMII 引脚和参考时钟。官方原始帧示例采用 8 MHz HSE，并将 PLL3 输出配置为
100 MHz。板级回调在时钟稳定后返回，延时使用 mrX 接口。
后端通过 MDIO 复位 PHY、应用 EVT 的中继和 RMII 时钟设置并配置链路协商，
在查询链路时把协商出的速率和双工设置到 MAC。
[WCH V317 以太网原始帧示例](https://github.com/openwch/ch32v307/blob/main/EVT/EXAM/ETH/MAC_RAW/ETH_Driver/eth_driver_CH32V317.c)

V4x7 使用内置 PHY，`wch,internal-phy` 保持为 1。可设置 `wch,phy-address` 和
`wch,mdio-div`，缺省分别为 1 和按 HCLK 自动选择分频。板级 `wch,prepare` 需配置
EVT 规定的 PHY 参考时钟和引脚。使用自动协商且只接收帧时，通过 `mr_eth_link()`
轮询链路以更新 MAC 的速率和双工模式。

## IWDG 和 WWDG

`wch,ch32-iwdg` 允许一个启用实例，使用芯片唯一的独立看门狗。选项字节应设置为软件启动模式。`feed_max` 以毫秒为单位，V00x 和 VM00X 按标称 128 kHz LSI 换算，其余家族按标称 40 kHz 换算。可配置范围分别为 1～8192 ms 和 1～26214 ms。后端向上取整到硬件计数周期，配置查询返回量化后取整到毫秒的标称时间，实际超时随 LSI 频率变化。`feed_min` 和 `pretimeout` 填 0。启动后由 `mr_wdt_feed()` 喂狗，硬件持续运行至复位。

```dts
iwdg: watchdog {
    compatible = "wch,ch32-iwdg";
    status = "okay";
};
```

`wch,ch32-wwdg` 使用窗口看门狗，最多声明一个实例。`wch,clken` 指向使能 WWDG 时钟的板级 `void function(void)`。V00x/VM00X 使用 HCLK，其余家族使用 PCLK1。和 IWDG、STM32H5 WDT 一样，`feed_min`、`feed_max`、`pretimeout` 统一以 **ms** 为单位。硬件计数周期可能不足 1 ms，因此配置查询对最早允许喂狗时间向上取整、对最晚允许喂狗时间和提前预警时间向下取整，给出整数毫秒级的保守边界；若当前时钟无法提供所请求的整数毫秒预警，非零 `pretimeout` 将返回 `-MR_ERANGE`；`feed_min` 和 `feed_max` 量化后无法形成可表示的窗口时也返回 `-MR_ERANGE`。应用应在喂狗窗口内预留调度余量。看门狗运行中，窗口开启前喂狗返回 `-MR_EAGAIN`，启动后持续运行至复位。

V4x7 的 IWDG 按标称 40 kHz LSI 换算，WWDG 使用 PCLK1。

需要提前预警时，在节点加入 `wch,pretimeout-irq`，可选 `wch,irq-priority`，并保留 `WWDG_IRQHandler` 给后端。预警时刻固定为复位前一个计数周期，`pretimeout` 填写所需的最小提前量，量化后的实际提前量可通过配置查询取得。启动前启用 `MR_WDT_WAIT_PRETIMEOUT` 可接收该事件。

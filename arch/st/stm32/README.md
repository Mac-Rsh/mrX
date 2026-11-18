# STM32 平台适配

一套通用 STM32 平台包覆盖各系列 Cortex-M STM32（HAL/Cube 工程）。`MR_CFG_STM32_HAL_HEADER`
选择板级工程提供的 HAL 伞头文件，实际芯片型号、封装、启动文件、链接脚本和引脚由板级工程决定。各系列差异
由 HAL 头文件中的宏在编译期自动探测（如 GPIO MODER/AFR、UART RDR/TDR、DMA GPDMA/Stream/Channel）。

当前平台提供 SysTick 低分辨率时钟、DWT 高分辨率时钟、中断临界区、ITM 诊断输出、断言和可选
D/I-cache 端口。片上驱动按 mrX 已有驱动类别接入，实际实例由芯片型号、封装和板级 HardTree 决定。

| Kconfig | 说明 |
| --- | --- |
| `MR_USE_STM32` | 固定启用 |
| `MR_CFG_STM32_HAL_HEADER` | HAL 伞头文件，如 `stm32f4xx_hal.h`、`stm32g4xx_hal.h` 或 `stm32h5xx_hal.h`；默认占位符 `stm32xxxx_hal.h` 必须由板级配置替换 |
| `MR_USE_DCACHE` / `MR_USE_ICACHE` | 启用数据/指令缓存端口 |
| `MR_CFG_STM32_CLOCK_IRQ_PRIORITY` | SysTick 中断优先级，0 最高，默认 15 |

## 接入 Cube 工程

1. 在包含 `scripts/cli.py` 的 mrX 目录运行下列命令安装平台，然后在 Kconfig 中把
   `MR_CFG_STM32_HAL_HEADER` 改为实际 HAL 伞头文件。启用片上驱动时，添加板级 HardTree
   节点并用 `htc -i` 生成 `mr_hardgen.h`。

   ```powershell
   python ./scripts/cli.py platform install -i ./arch/st/stm32/install.toml
   python ./scripts/cli.py kconfig
   ```

2. 在 STM32CubeMX 生成或自建的 Cortex-M 工程中，沿用 HAL、启动文件、`SystemClock_Config`
   和型号宏。将 HAL 头文件目录与按 [mrX 接入指南](../../../document/usage.md)
   安装的框架源码、`platform/` 副本一起纳入编译。使用芯片对应的 Cortex-M 内核（部分能力
   依赖 DWT 或缓存外设，见下文）。

3. 对应用的 GNU ld 脚本运行 `python ./scripts/cli.py sections -i <脚本路径>`，保留原有内存
   布局和向量表，并在 `.text` 中收集 mrX 注册段。

4. 板级代码在 `HAL_Init()` 和时钟配置完成后调用 `mr_user_init()`。平台接管
   `HAL_IncTick()`：它同时递增 HAL tick 和 mrX 时钟，应用中不得再实现同名函数。SysTick
   处理函数（通常由 Cube 的中断文件提供）必须每节拍恰好调用一次 `HAL_IncTick()`。运行期间
   保持 `SystemCoreClock` 与实际主频一致。

低分辨率时钟按 `MR_CFG_TICK_PER_SECOND` 分数累积换算，允许 HAL tick 频率与 mrX
tick 频率不同。`uwTickFreq` 为 0 或 SysTick 未运行时，忙等接口直接返回。启用
`MR_USE_CLOCK_HIRES` 时使用 DWT `CYCCNT`；不支持 DWT 的内核（如 Cortex-M0/M0+）必须关闭
该配置，否则编译报错。主频变更后应重新调用 `mr_stm32_platform_clock_init()`，使高分辨率
时钟的周期折算与新主频一致。

诊断输出 `mr_port_output` 通过 ITM（SWO）发送；未定义 `ITM` 的芯片输出不可用并返回
-1。断言失败时输出文件、行号和表达式，随后关中断死循环，现场可用调试器读取。

启用缓存端口时，缓存变体从所选芯片的能力自动选择：存在 HAL DCACHE/ICACHE 外设时使用外设
（HAL DCACHE 实例由缓存端口惰性初始化），否则使用 CMSIS SCB 核心缓存。板级不要用并行的
HAL 句柄管理同一个缓存。DMA 与 FLASH 后端在启用缓存端口时自动进行缓存维护。

## GPIO

`st,stm32-gpio` 每个节点对应一个 GPIO 端口。`st,gpiox` 指定 HAL 寄存器实例，`st,clken`
指向使能端口时钟的板级 `void function(void)`。`st,exti-mask` 用数字掩码列出可用 EXTI
线，缺省由声明的 `st,irq-handler-N` 推导；`st,irq-priority` 为 EXTI 中断优先级，缺省
0。引脚 N 的中断使用成对的 `st,irqn-N` 和 `st,irq-handler-N`，缺省 IRQ 编号为
`EXTIx_IRQn`；处理函数名必须与启动文件向量表一致。

EXTI 线 0–15 在不同端口之间互斥，每号一线全局唯一占用。IRQ 模式引脚必须落在
`st,exti-mask` 内，否则返回 `-MR_EPERM`；已被其他节点占用的线返回 `-MR_EBUSY`。EXTI
不支持电平触发，返回 `-MR_ENOSYS`。输出配置先预装电平再切换模式，避免模式切换毛刺；输出
速度固定为高速。端口读写经 BSRR/IDR 原子完成。事件为 `MR_GPIO_WAIT_EXTI`，等待回调在中断
中同步执行，回调应保持简短。

需要归还 EXTI 线时，把引脚配置回普通输入/输出模式即可释放所有权并取消其等待项。带 AFR
的 GPIO 通过 `mr_stm32_gpio_pin_alternate()` 与 `mr_stm32_gpio_pin_release()` 支持其他
驱动的复用接管；传统 CRL/CRH 布局不支持复用接管与配置查询，返回 `-MR_ENOSYS`。

以下节点使用 PA0 的 EXTI，板级 `board_gpioa_clock_enable()` 使能 GPIOA 时钟。

```dts
gpio_a: gpio-a {
    compatible = "st,stm32-gpio";
    status = "early";
    st,gpiox = GPIOA;
    st,clken = board_gpioa_clock_enable;
    st,exti-mask = <0x0001>;
    st,irq-handler-0 = EXTI0_IRQHandler;
};
```

## UART

`st,stm32-uart` 依赖 GPIO 后端。`st,uartx` 指定 `USARTx` 或 `UARTx`，`st,tx` 和
`st,rx` 各使用 `<&gpio节点 pin>`，可选 `st,tx-af` 和 `st,rx-af` 给出复用 AF 号。按实际
启用的方向提供引脚；配置为仅接收或仅发送时会释放另一方向引脚。`st,clken` 指向使能
USART 时钟的板级回调。成对的 `st,irqn` 和 `st,irq-handler` 必须提供，`st,irq-priority`
缺省 0。

支持轮询收发，帧格式为 7/8 位数据、无/奇/偶校验、1/1.5/2 个停止位；7 位无校验和 1.5
个停止位需要所选 HAL 支持。硬件流控不支持，返回 `-MR_ENOSYS`。读返回短计数。可订阅
`RX_READY`、`RX_IDLE`、`TX_DONE` 与线路 `RX_ERROR`；`TX_DONE` 由 TC 中断确认最后一个
字节已移出。

启用 UART stream 和 DMA 后，可用 `st,rx-dma`、`st,tx-dma` 指定独占 DMA 通道
`<&dma节点 channel>`。RX stream 需要 8 位数据帧；7 位数据带校验的 RX stream 不支持，DMA
无法剥离校验位。单次 TX stream 在 DMA 供数完成后再等 TC 报告完成；循环 TX stream 按 DMA
缓冲区回绕报告完成。重新配置、切换轮询或关闭驱动前，先停止已启动的 stream。

## 定时器与 PWM

| Compatible | HardTree 属性 | 能力 |
| --- | --- | --- |
| `st,stm32-tmr` | `st,timx`、`st,clken`，成对的 `st,irqn`、`st,irq-handler` 和 `st,irq-priority` | 周期或单次计数，周期单位为 ns，向上计数，配置向量后可上报超时。 |
| `st,stm32-pwm` | `st,timx`、`st,clken`，`st,channels` 每项 `[逻辑通道号, HAL 通道, &gpio节点, 引脚, AF]` | 使用同一计数器的 PWM 通道，按节点列举生成通道掩码。 |

TMR 与 PWM 分别独占所选的 TIM 实例。周期与脉宽折算使用 HAL 时钟查询按 TIMPRE 规则计算
APB1/APB2 定时器时钟，运行期间不要绕过 HAL 修改时钟树。预分频在 65536 内最多搜索 64
步，取最接近目标周期的量化值；32 位计数器定时器支持更长周期。中心对齐按 2 倍计数折算。

PWM 支持边沿向上/向下与中心对齐、周期边界更新和有效/无效空闲电平；停止通道时按 idle
配置保持电平或关闭输出。运行中仅支持单通道脉宽更新，一次更新一个通道。互补输出、死区、
相位和故障输入超出当前后端范围。`MR_PWM_SIGNAL_PERIOD` 通过 TRGO 输出周期信号，仅
master 定时器可用且信号偏移固定为 0。TMR 的超时事件为 `MR_TMR_WAIT_TIMEOUT`。

## ADC 与 DAC

| Compatible | HardTree 属性 | 能力 |
| --- | --- | --- |
| `st,stm32-adc` | `st,adcx`、`st,clken`、`st,ref`、`st,channels` 每项 `[硬件通道号, &gpio节点, 引脚]`，可选 `st,trigger`、`st,trigger-edge`、`st,dma`、成对的 `st,irqn`、`st,irq-handler`、`st,irq-priority` | 单端采样，12 位分辨率；轮询读序列可请求芯片支持的更低分辨率。 |
| `st,stm32-dac` | `st,dacx`、`st,clken`，`st,channels` 每项 `[HAL 通道, &gpio节点, 引脚]`，可选 `st,ref`、`st,trigger`、`st,dma`、成对的 `st,irqn`、`st,irq-handler`、`st,irq-priority` | 双通道型号按节点列举决定通道掩码，支持 8/12 位右对齐输出。 |

ADC 的 `st,ref` 填写本实例使用的参考源编号；DAC 未提供 `st,ref` 时默认内部参考，
`ref-internal` 未提供时默认 3300 mV。通道索引由 `st,channels` 的列举顺序决定，模拟引脚
由后端配置为模拟输入/输出。

ADC 的 `oversample` 字段用作采样时间编码（0–7），映射到所选芯片的采样时间档位表，由 HAL
宏在编译期选择；通道配置查询时使用最长采样时间。每次轮询转换独立启动 ADC 并等待，超时
100 ms。

启用 ADC stream 后，软件触发连续转换或使用 `st,trigger` 指定的外部触发（边沿由
`st,trigger-edge` 指定，缺省上升沿）。regular 序列最多 8 个通道，按通道号升序排列；缓冲
长度必须是完整扫描帧的整数倍。有 `st,dma` 时走 DMA 数据路径，否则必须提供成对 IRQ 属性
走中断路径。`half` 事件仅当总样本数为偶数且半帧是通道数的整数倍时可用；溢出上报 stream
error 并停流。

DAC 轮询输出无触发；stream 仅支持外部触发、单通道。无 DMA 时仅支持单样本单次输出。单次
DMA stream 在 DMA 供数完成后经 underrun 中断确认最后一个样本已输出，underrun 在此之前
发生则上报错误；循环 stream 样本数为偶数时可用 `half` 事件。

## DMA

`st,stm32-dma` 自动识别三类 IP：GPDMA（`GPDMA1`/`GPDMA2`）、Stream 型（`DMA1_Stream0`
等）和 Channel 型（`DMA1_Channel1` 等）。`st,gpdmax` 或 `st,dmax` 选择控制器实例，
`st,clken` 使能控制器时钟，`exist-mask` 缺省按 `st,requests` 数量全部启用。通道索引从
0 开始。

`st,requests` 是与通道一一对应的元素数组：classic DMA 每项 `[请求编号]`；GPDMA 每项
`[请求编号, 触发模式, 触发极性, 触发选择]`，后三项可省略，缺省为块传输、屏蔽极性和 0。
GPDMA 的 M2M 传输自动使用软件请求；classic DMA 在芯片提供 MEM2MEM 请求时同样自动选择，
否则沿用板级请求映射。每通道可提供 `st,irqn-N` 与 `st,irq-handler-N`；GPDMA 缺省
使用固定通道 IRQ。共享 IRQ 用 `st,irq-mask-N` 指明处理函数覆盖的通道掩码，缺省只覆盖通道
N。`st,irq-priority` 为控制器级优先级。

DMA 支持 8/16/32 位宽度（64 位不支持）、地址递增或固定（递减不支持），以及 M2P、P2M 和
M2M 传输。单次传输用空 `next`，循环传输将 `next` 指向自身；classic 的 M2M 循环不支持。
Stream 型要求源宿宽度一致（无 FIFO 打包），burst 仅支持单次。classic 计数上限 65535 个
传输单元，GPDMA 以源侧字节数计。地址必须落在 32 位空间并按宽度对齐。

事件为 `MR_DMA_WAIT_HALF`、`MR_DMA_WAIT_DONE` 与 `MR_DMA_WAIT_ERROR`，错误中断不受事件
订阅影响始终开启。`offset` 查询返回最后观测到的源侧字节进度。启用缓存端口时，后端在启动、
停止与半/全传输边界自动 clean/invalidate 缓冲区，已完成范围的数据在事件上报前交还 CPU，
应用不必再手工维护缓存。

## Flash

`st,stm32-flash` 节点无属性。后端仅支持 128 位 quadword 编程加扇区擦除的 Flash IP；编程
粒度 16 字节，擦除粒度取 SDK 的 `FLASH_SECTOR_SIZE`（未定义时按 8 KiB）。读为内存映射
读取。擦除逐扇区执行以支持跨 bank（存在 `FLASH_BANK_2` 时按双 bank 定位）。

写入按 16 字节 quadword 对齐分头、中、尾执行；不完整的 quadword 会读出合并，其中已编程
字节非 0xFF 时返回 `-MR_EIO`，即同一位置不支持二次写入。启用缓存端口时，写与擦除成功后
自动失效 D/I-cache 对应范围。

## Ethernet

`st,stm32-eth` 仅支持一个 MAC 实例，依赖 GPIO 后端与描述符式 HAL_ETH，工程需开启
`HAL_ETH_MODULE_ENABLED` 并设置 `USE_HAL_ETH_REGISTER_CALLBACKS=1`。`st,ethx` 指定 MAC
实例，`st,clken` 使能 MAC 总线时钟。`st,phy-address` 填写 PHY 地址（0–31），缺省读取兼容属性
`st,phy-addr`。`st,pins` 每项 `[&gpio节点, 引脚, AF]` 声明 RMII/MII 引脚。可选 `st,media` 缺省 RMII
（可设 MII），`st,tx-prefetch` 为 1 时启用 TX 二包预取，`st,dma-section` 指定描述符与缓冲区
放置的 section。启用 Ethernet stream 时必须提供 `st,irqn`、`st,irq-handler`、
`st,irq-priority` 和 `st,dma-region`（`[基址, 大小]`，32 字节对齐且至少 1536 字节），
处理函数名保留给后端。

HAL 提供 `__HAL_RCC_ETHTX_CLK_ENABLE()` 或 `__HAL_RCC_ETHRX_CLK_ENABLE()` 时，后端会在
`st,clken` 之后、ETH 软复位之前开启相应的 TX/RX 时钟门。PHY 参考时钟由板级提供。

板级须将 `st,dma-section` 和 `st,dma-region` 放在 ETH DMA 可访问的内存。启用数据缓存时，
板级须将这两处内存配置为不可缓存，分别覆盖描述符和轮询缓冲区，以及所有提交的 Stream 帧缓冲区。

PHY 使用 Clause 22 标准寄存器，支持 10/100 Mbit/s、自协商或固定速率，双工与 pause
流控按 BMSR/ANAR/ANLPAR 协商。回环模式中，`MR_ETH_LOOPBACK_MAC` 在 MAC 内部回送数据，
`MR_ETH_LOOPBACK_PHY` 使用 PHY 本地回环，两者都要求固定速率；回环建立时 `link.up`
表示本地回环数据路径已建立，而不是外部网线链路。

轮询模式单缓冲收发：TX 复制到内部缓冲并阻塞等待完成（超时 20 ms），无链路返回
`-MR_EAGAIN`，无帧时读返回 0。链路状态每 100 ms 节流查询，链路参数变化时会重新初始化 MAC。
轮询中检测到 DMA 致命总线错误时，后端复位 MAC 并返回 `-MR_EIO`，下一次收发或链路查询会在
链路可用时重启 MAC。Stream 模式按帧提交：RX 需要预先提交至少 `ETH_RX_DESC_CNT+1` 个帧，
帧缓冲落在 `st,dma-region` 内且 32 字节对齐；TX 环深度 `ETH_TX_DESC_CNT`，按提交
顺序回报完成。DMA 致命故障停止 stream 并以错误状态归还所有未决帧。`configure` 与
`link` 等控制操作不可在中断上下文调用。

## IWDG

`st,stm32-wdt` 允许一个启用实例，使用芯片唯一的独立看门狗。`st,iwdgx` 指定 IWDG 实例；
可选成对的 `st,irqn`、`st,irq-handler` 与 `st,irq-priority` 用于提前预警，要求所选 IP
具备 `IWDG_EWCR` 早期唤醒中断。

`feed_min` 必须为 0，`feed_max` 与 `pretimeout` 统一以 **ms** 为单位。超时按 `LSI_VALUE`
折算并向上取整到硬件计数周期，范围受重载寄存器（4095）与最大分频限制；配置查询返回量化
后取整到毫秒的标称时间，实际超时随 LSI 频率变化。`pretimeout` 需要 EWI 支持，预警计数须
至少为 2 且小于超时计数，否则返回 `-MR_EINVAL`。启动后由 `mr_wdt_feed()` 喂狗，硬件持续
运行至复位。启动前启用 `MR_WDT_WAIT_PRETIMEOUT` 可接收预警事件。

```dts
iwdg: watchdog {
    compatible = "st,stm32-wdt";
    status = "okay";
    st,iwdgx = IWDG;
};
```

# STM32H5 驱动适配约定

本目录提供 GPIO、ADC、DAC、GPDMA、UART、基本定时器、片上 FLASH 和 IWDG 适配。
分层、资源所有权、HardTree 约定和代码骨架见
[ST 平台适配规范](../../../document/arch/st/st.md)；本文只记录本芯片
运行期语义。

各驱动由对应 `MR_USE_DRIVER_*` 开关启用；ADC/DAC stream 可选使用 DMA，
UART stream 需要 DMA。HAL 模块与 HardTree 资源由板级工程配置。
没有对应 compat 实例时，backend 被 `MR_HT_HAS_DRIVER()` 裁掉。

## HardTree 属性

GPIO（`st_stm32h5_gpio`）：`st_gpiox`、`st_clken`、`st_irq_priority`；可选
`exist_mask` / `invert_mask`。EXTI 线 `n` 仅在存在 `st_irq_handler_n`
（向量名，如 `EXTI5_IRQHandler`）时生成处理函数。

DMA（`st_stm32h5_gpdma`）：`st_gpdmax`、`st_clken`、`st_irq_priority`、
`st_requests`（按下标对应硬件通道的 request/trigger 路由）。每个元素可写成
`<request>`，也可写成
`<request trigger_mode trigger_polarity trigger_selection>`；后三项省略时默认为
`DMA_TRIGM_BLOCK_TRANSFER`、`DMA_TRIG_POLARITY_MASKED`、`0`，因此旧模板行为不变。
通道数组长度等于 `st_requests` 元素数；省略 `exist_mask` 时默认为低 `N` 位。可选
`st_irq_handler_0`…`st_irq_handler_7`；若声明了任一通道 IRQ，需提供
`st_ctrl`（0=GPDMA1，1=GPDMA2）。

例如让 channel 0 的 SPI TX 在 EXTI0 上升沿到来时释放一个完整 block，而 channel 1
的 SPI RX 只由 SPI 外设 request 推进：

```dts
st_requests = <<GPDMA1_REQUEST_SPI1_TX
                 DMA_TRIGM_BLOCK_TRANSFER
                 DMA_TRIG_POLARITY_RISING
                 GPDMA1_TRIGGER_EXTI_LINE0>
               <GPDMA1_REQUEST_SPI1_RX>>;
```

trigger 只负责门控 DMA：EXTI event、定时器 TRGO 或其他 trigger 生产者的引脚复用、
边沿和路由仍由板级工程配置。SPI 双向流应先启动未门控的 RX DMA，再启动被 trigger
门控的 TX DMA；TX 开始写 SPI 数据寄存器后，RX 由 SPI RX request 自然推进。

TMR（`st_stm32h5_tmr`）：`st_timx`、`st_clken`、`st_irqn`、
`st_irq_priority`、`st_irq_handler`。`MR_TMR_SIGNAL_PERIOD` 独立控制 update 到 TRGO
的输出，不启动定时器，也不启用 TIMEOUT 中断。signal 启用期间重配 TMR 返回
`-MR_EBUSY`。

PWM（`st_stm32h5_pwm`）：`st_timx`、`st_clken` 和 `st_channels`；每个 channel
单元为 `<logical_channel, hal_channel, gpio_phandle, pin, af>`。`channel_mask` 必须只包含
实际列出的 logical channel，当前 H5 backend 支持普通主输出、连续周期和 PERIOD signal；
仅支持 `MR_PWM_UPDATE_PERIOD`、单通道 update，phase 必须为 0。多通道必须在定时器空闲时
一次性 start，运行中追加通道返回 `-MR_ENOSYS`。互补输出、dead-time、fault、有限周期、
COMPARE signal 和 wait event 也返回 `-MR_ENOSYS`。PERIOD signal 使用 update
到 TRGO；中心对齐通过 repetition counter 保证每个完整 PWM 周期只产生一次 update。
signal 启用期间重配控制器返回 `-MR_EBUSY`；重配时基后必须重新配置通道。
任一通道运行时，H5 backend 也拒绝修改其他通道的静态配置。

UART（`st_stm32h5_uart`）：`st_uartx`、`st_clken`、`st_irqn`、
`st_irq_priority`、`st_irq_handler`。方向引脚 `st_tx` / `st_rx` 为 GPIO
spec `[phandle, pin]`，`st_tx_af` / `st_rx_af` 为 AF 编号；不用的方向可省略。
`st_rx_dma` / `st_tx_dma` 按实例可选。AF 由 GPIO 拥有者 helper 配置，
不要把这些引脚再配成 EXTI。

ADC（`st_stm32h5_adc`）：`st_adcx`、`st_clken`、`st_ref`；`st_channels`
单元为 `<hal_channel, gpio_phandle, pin>`。模拟脚必须落在对应 GPIO 的
`exist_mask` 里。`st_dma` 可选：存在时 stream 使用 DMA，否则使用 ADC 中断，
此时实例还需提供 `st_irqn`、`st_irq_handler`，`st_irq_priority` 可选。
外部源由 `st_trigger` 与可选 `st_trigger_edge` 静态选择；ADC 不配置或启停信号生产者。

DAC（`st_stm32h5_dac`）：`st_dacx`、`st_clken`；`st_channels` 单元为
`<HAL DAC channel, gpio_phandle, pin>`。外部 stream 使用 `st_trigger`，有限流还需
`st_irqn`、`st_irq_handler`（`st_irq_priority` 可选），以 DAC underrun 中断确认最后一次
DHR 到 DOR 的传送。`st_dma` 可选；无 DMA 时仅支持单样本有限流。H5 backend
不为软件源提供隐式时基，因此软件源返回 `-MR_ENOSYS`。

FLASH（`st_stm32h5_flash`）：片上阵列，无需额外 GPIO/DMA 属性。扇区 8 KB，
编程宽度 128 bit。H50x/H52x/H53x 为单 bank，其余系列为双 bank。

WDT（`st_stm32h5_wdt`）：`st_iwdgx`、`st_irqn`、`st_irq_priority`、
`st_irq_handler`。时间单位为毫秒。`feed_min` 必须为 0（无窗口）；
`feed_max` 为超时；`pretimeout` 为提前唤醒提前量，0 表示关闭 EWI。
IWDG 启动后不可停止，`stop` 为 `MR_NULL`。TIMEOUT 直接复位芯片，
不提供 TIMEOUT 事件。

## 流传输与资源生命周期

- 外部信号生产者由应用配置和启停；ADC/DAC stream 只配置并武装外设输入。
- TMR/PWM signal 与软件 wait event 独立；signal enable/disable 不启停生产者计数。
- TIM 的中性 TRGO 配置仍可能在软件 UG 时产生脉冲；因此必须先完成生产者重配，再武装
  ADC/DAC stream，不能把 signal disable 当作消费者侧的硬件隔离。
- 配置了 `st_dma` 的 stream 独占对应 DMA 通道；未配置时按上述各外设限制走 IRQ。
- ADC buffer 至少容纳一个完整通道 frame，大小必须是 frame 的整数倍；只有能在
  完整 frame 边界平分时才产生 HALF，单 frame 只产生 FULL。
- DAC 有限流的 DONE 在最后一个样本由外部信号送入 DOR 后产生，不等同于 DMA TC。
- ADC、DAC、UART 的 stream stop 失败时保留活动状态和 DMA waiter，
  调用方应保留 buffer、驱动和关联资源，并重试 stop；不能将错误返回视为
  已经停止硬件。成功停止后才释放 waiter 和流状态。
- 在普通调用上下文执行 stop；DMA 的阻塞停止可能依赖 HAL 时基，
  不应在同步事件回调的 IRQ 上下文执行。
- DMA offset 的单位为字节，错误回调保存当前进度；因 HAL ERROR 状态
  重建通道时保留停止进度，直到下一次配置或启动传输。
- 有限 DMA 始终保留 TC 中断以完成 HAL 状态和 cache 收尾。循环 DMA 仅在订阅
  HALF/DONE 或 cache 所有权维护需要时启用对应完成中断；无完成事件订阅的固定 TX
  模式不会每轮打断 CPU，ERROR 中断仍始终启用。

## UART 事件

- `TX_STREAM_DONE` 表示 DMA 已将本轮 buffer 送入 UART。
- 有限 TX stream 在 DMA 完成后才启用 TC 完成通知，`TX_DONE` 表示
  最后一个停止位已发送。DMA 供数间隙出现的 TC 不作为整条流完成。
- stream 方向必须与 UART 配置的收发方向相容，否则返回 `-MR_EINVAL`。

## 普通 ADC 读取

普通读取在后续转换失败时返回此前已经写入 buffer 的字节数；
尚未完成任何样本时返回相应错误码。

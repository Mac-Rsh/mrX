# STM32H5 驱动适配约定

本目录提供 GPIO、ADC、DAC、GPDMA、UART、基本定时器、片上 FLASH 和 IWDG 适配。
分层、资源所有权、HardTree 约定和代码骨架见
[ST 平台适配规范](../../../document/arch/st/st.md)；本文只记录本芯片
运行期语义。

各驱动由对应 `MR_USE_DRIVER_*` 开关启用；ADC/DAC stream 还需要 DMA
和 TMR，UART stream 需要 DMA。HAL 模块与 HardTree 资源由板级工程配置。
没有对应 compat 实例时，backend 被 `MR_HT_HAS_DRIVER()` 裁掉。

## HardTree 属性

GPIO（`st_stm32h5_gpio`）：`st_gpiox`、`st_clken`、`st_irq_priority`；可选
`exist_mask` / `invert_mask`。EXTI 线 `n` 仅在存在 `st_irq_handler_n`
（向量名，如 `EXTI5_IRQHandler`）时生成处理函数。

DMA（`st_stm32h5_gpdma`）：`st_gpdmax`、`st_clken`、`st_irq_priority`、
`st_requests`（按下标对应硬件通道的 request ID）。通道数组长度等于
`st_requests` 元素数；省略 `exist_mask` 时默认为低 `N` 位。可选
`st_irq_handler_0`…`st_irq_handler_7`；若声明了任一通道 IRQ，需提供
`st_ctrl`（0=GPDMA1，1=GPDMA2）。

TMR（`st_stm32h5_tmr`）：`st_timx`、`st_clken`、`st_irqn`、
`st_irq_priority`、`st_irq_handler`。

UART（`st_stm32h5_uart`）：`st_uartx`、`st_clken`、`st_irqn`、
`st_irq_priority`、`st_irq_handler`。方向引脚 `st_tx` / `st_rx` 为 GPIO
spec `[phandle, pin]`，`st_tx_af` / `st_rx_af` 为 AF 编号；不用的方向可省略。
`st_rx_dma` / `st_tx_dma` 按实例可选。AF 由 GPIO 拥有者 helper 配置，
不要把这些引脚再配成 EXTI。

ADC（`st_stm32h5_adc`）：`st_adcx`、`st_clken`、`st_ref`；`st_channels`
单元为 `<hal_channel, gpio_phandle, pin>`。模拟脚必须落在对应 GPIO 的
`exist_mask` 里。`st_dma` / `st_timer` / `st_trigger` 按实例可选。

DAC（`st_stm32h5_dac`）：`st_dacx`、`st_clken`；`st_channels` 单元为
`<HAL DAC channel, gpio_phandle, pin>`。stream 属性同 ADC。

FLASH（`st_stm32h5_flash`）：片上阵列，无需额外 GPIO/DMA 属性。扇区 8 KB，
编程宽度 128 bit。H50x/H52x/H53x 为单 bank，其余系列为双 bank。

WDT（`st_stm32h5_wdt`）：`st_iwdgx`、`st_irqn`、`st_irq_priority`、
`st_irq_handler`。时间单位为毫秒。`feed_min` 必须为 0（无窗口）；
`feed_max` 为超时；`pretimeout` 为提前唤醒提前量，0 表示关闭 EWI。
IWDG 启动后不可停止，`stop` 为 `MR_NULL`。TIMEOUT 直接复位芯片，
不提供 TIMEOUT 事件。

## 流传输与资源生命周期

- ADC/DAC 的触发定时器及各 stream 使用的 DMA 通道须由调用方独占。
- ADC 在启动转换前配置定时器，避免配置时的 UG 更新事件提前触发采样。
- ADC、DAC、UART 的 stream stop 失败时保留活动状态和 DMA waiter，
  调用方应保留 buffer、驱动和关联资源，并重试 stop；不能将错误返回视为
  已经停止硬件。成功停止后才释放 waiter 和流状态。
- 在普通调用上下文执行 stop；DMA 的阻塞停止可能依赖 HAL 时基，
  不应在同步事件回调的 IRQ 上下文执行。
- DMA offset 的单位为字节，错误回调保存当前进度；因 HAL ERROR 状态
  重建通道时保留停止进度，直到下一次配置或启动传输。

## UART 事件

- `TX_STREAM_DONE` 表示 DMA 已将本轮 buffer 送入 UART。
- 有限 TX stream 在 DMA 完成后才启用 TC 完成通知，`TX_DONE` 表示
  最后一个停止位已发送。DMA 供数间隙出现的 TC 不作为整条流完成。
- stream 方向必须与 UART 配置的收发方向相容，否则返回 `-MR_EINVAL`。

## 普通 ADC 读取

普通读取在后续转换失败时返回此前已经写入 buffer 的字节数；
尚未完成任何样本时返回相应错误码。

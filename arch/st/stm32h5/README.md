# STM32H5 驱动适配约定

本目录提供 GPIO、ADC、DAC、GPDMA、UART 和基本定时器适配。
各驱动由对应 `MR_USE_DRIVER_*` 开关启用；ADC/DAC stream 还需要 DMA
和 TMR，UART stream 需要 DMA。HAL 模块与 HardTree 资源由板级工程配置。

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

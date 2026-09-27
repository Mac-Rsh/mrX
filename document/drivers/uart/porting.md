# UART 驱动移植指南

> 本文说明如何为新的 UART/USART 控制器实现 mrX backend。公共 API 和事件语义见 [UART](uart.md)。

## 1. 移植边界

UART 公共层负责类型化 API、waitable 生命周期和 stream 能力门控；backend 负责波特率/帧格式映射、普通 read/write、中断事件以及可选
DMA stream。

建议实现位置：

```text
mrX/arch/<vendor>/<target>/platform/drivers/uart/
```

## 2. 操作表

基础 callback：

- `configure`
- `configuration`
- `read`
- `write`

启用 `MR_USE_DRIVER_UART_STREAM` 后可提供 RX/TX 各自的：

- `stream_start`
- `stream_stop`
- `stream_is_busy`
- `stream_offset`

`event_control` 是可选事件能力。

普通 read/write 与 stream 是两套路径；backend 必须明确同一方向同时使用时的互斥规则。

## 3. 配置映射

`mr_uart_config_t` 需要映射：

- baud rate；
- data bits；
- stop bits；
- parity；
- flow control；
- RX/TX mode。

无法实现的合法组合返回明确错误，不要静默改成相近配置。`configuration` 应返回实际采用值。

## 4. 普通 read/write

普通 `read`/`write` 是直接、短操作接口。推荐 backend：

- read 只取当前硬件已经可读的数据；
- write 只写当前硬件能够接受的数据；
- 返回实际完成字节数；
- 不在公共 API 背后偷偷创建长期异步 buffer 所有权。

这样上层可以结合 UART wait 事件形成非 stream 的中断驱动收发。

## 5. 普通 UART 事件

当前公共事件：

```text
MR_UART_WAIT_RX_IDLE
MR_UART_WAIT_RX_READY
MR_UART_WAIT_TX_DONE
MR_UART_WAIT_RX_ERROR
MR_UART_WAIT_TX_ERROR
```

建议映射：

- `RX_READY`：普通 `mr_uart_read()` 已经有数据可取，对应 RXNE/RXFNE 类条件；
- `RX_IDLE`：接收线路进入 idle；
- `TX_DONE`：最后一个 stop bit 已真正发送完成，对应 TC 类条件；
- RX/TX ERROR：平台可区分后合并到对应公共错误事件。

RX_READY 常来自电平型条件。若 waiter 可能只投递任务而不立即读取，backend 应避免因状态持续有效造成 IRQ storm，可采用
one-shot 屏蔽并在 read 后 rearm 等策略。

TX_DONE 不应在“写入 TDR/FIFO”时触发，而应等物理 UART 发送完成。

## 6. Stream 与 DMA

stream 事件：

```text
MR_UART_WAIT_RX_STREAM_HALF
MR_UART_WAIT_RX_STREAM_FULL
MR_UART_WAIT_TX_STREAM_HALF
MR_UART_WAIT_TX_STREAM_DONE
```

DMA stream 需要区分：

- `TX_STREAM_DONE`：DMA 已完成本轮 buffer 搬运；
- `TX_DONE`：UART shift register 已发送最后一个 stop bit。

二者不能由同一个 DMA DONE 状态简单等价触发。

RX stream 激活期间，普通 RX_READY 路径不应再和 DMA 同时读取 RDR；TX stream 激活期间普通 write 同理应与 DMA 所有权互斥。

## 7. ISR 与事件控制

`event_control` 维护平台事件 mask，并据此开关真实 IRQ source。ISR 的推荐顺序：

1. 判断 flag 与 interrupt source；
2. 清除或临时屏蔽会持续触发的状态；
3. 更新 backend 状态；
4. 聚合公共 event bits；
5. 调用 `mr_uart_event_raise()`。

waiter 同步运行在 raise 上下文，ISR 路径不能依赖 waiter 阻塞等待。

## 8. HardTree 与安装

平台可用 `MR_HT_UART_INSTALL` 注册实例。HardTree 通常描述 UART instance、TX/RX pin、alternate、clock、IRQ、priority 和可选
RX/TX DMA spec。

平台私有数据应保存 HAL handle/寄存器基址、event mask、当前配置和 stream 状态。

## 9. STM32H5 参考实现

当前参考：

```text
mrX/arch/st/stm32h5/platform/drivers/uart/uart.c
```

该 backend 已实现：

- 普通非阻塞 read/write；
- RX_READY / RX_IDLE / TX_DONE / error；
- RX_READY one-shot + read 后 rearm；
- stream 与普通 RX 路径互斥；
- DMA HALF/FULL/DONE/ERROR 转换；
- TX_STREAM_DONE 与物理 TX_DONE 分离；
- HardTree 实例安装。

## 10. 移植检查清单

- 所有基础 callback 已实现；
- baud/frame 不支持组合能明确失败；
- read/write 返回实际完成字节数；
- RX_READY 不产生持续 IRQ storm；
- TX_DONE 确实代表线路发送完成；
- stream 与普通 IO 不争用 RDR/TDR；
- DMA DONE 与 UART TC 语义分离；
- stop 后 DMA/IRQ/buffer 生命周期结束明确；
- HardTree pin/clock/IRQ/DMA 资源正确。

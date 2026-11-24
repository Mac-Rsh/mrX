# UART 驱动移植指南

面向平台 backend 开发者，说明如何为 UART/USART 控制器实现 `mr_uart_ops_t`。公共
API 与事件定义见 [UART](uart.md)。

## 1. 移植边界

| 职责                                        | 公共层 | Backend |
|---------------------------------------------|--------|---------|
| 类型化 API 校验与 `-MR_ENOSYS` 降级         | 负责   | —       |
| `mr_uart_data_t.event` 初始化与销毁         | 负责   | —       |
| 波特率、帧格式、流控、引脚复用与时钟映射    | —      | 负责    |
| 普通 `read`/`write` 数据搬运与阻塞/互斥策略 | —      | 负责    |
| 可选 RX/TX stream（DMA/中断）与缓冲区管理   | —      | 负责    |
| 硬件中断处理与 `event_control` 掩码门控     | —      | 负责    |

平台代码通常放在 `mrX/arch/<vendor>/<target>/platform/drivers/uart/`。

## 2. 操作表（ops）

backend 需提供 `<mrX/drivers/uart/mr_uart.h>` 中定义的 `mr_uart_ops_t`：

| 回调                | 签名要点                                                                           | 必需                                 | 缺失返回值   |
|---------------------|------------------------------------------------------------------------------------|--------------------------------------|--------------|
| `configure`         | `(drv, const mr_uart_config_t *config)`                                            | 是                                   | 断言拦截     |
| `configuration`     | `(drv, mr_uart_config_t *config)`                                                  | 是                                   | 断言拦截     |
| `read`              | `(drv, void *buf, mr_size_t size)`，返回字节数或负错误码                           | 是                                   | 断言拦截     |
| `write`             | `(drv, const void *buf, mr_size_t size)`，返回字节数或负错误码                     | 是                                   | 断言拦截     |
| `rx_stream_start`   | `(drv, void *buf, mr_size_t size, const mr_uart_rx_stream_config_t *config)`       | 否（需 `MR_USE_DRIVER_UART_STREAM`） | `-MR_ENOSYS` |
| `rx_stream_stop`    | `(drv)`                                                                            | 否（需 `MR_USE_DRIVER_UART_STREAM`） | `-MR_ENOSYS` |
| `rx_stream_is_busy` | `(drv, mr_bool_t *busy)`                                                           | 否（需 `MR_USE_DRIVER_UART_STREAM`） | `-MR_ENOSYS` |
| `rx_stream_offset`  | `(drv, mr_size_t *offset)`                                                         | 否（需 `MR_USE_DRIVER_UART_STREAM`） | `-MR_ENOSYS` |
| `tx_stream_start`   | `(drv, const void *buf, mr_size_t size, const mr_uart_tx_stream_config_t *config)` | 否（需 `MR_USE_DRIVER_UART_STREAM`） | `-MR_ENOSYS` |
| `tx_stream_stop`    | `(drv)`                                                                            | 否（需 `MR_USE_DRIVER_UART_STREAM`） | `-MR_ENOSYS` |
| `tx_stream_is_busy` | `(drv, mr_bool_t *busy)`                                                           | 否（需 `MR_USE_DRIVER_UART_STREAM`） | `-MR_ENOSYS` |
| `tx_stream_offset`  | `(drv, mr_size_t *offset)`                                                         | 否（需 `MR_USE_DRIVER_UART_STREAM`） | `-MR_ENOSYS` |
| `event_control`     | `(drv, mr_uint32_t events, mr_bool_t enable)`                                      | 否                                   | `-MR_ENOSYS` |

## 3. 关键契约

- **配置映射**：`configure`
  需将波特率、数据位、停止位、校验、流控及方向映射到硬件寄存器，对不支持的组合返回负错误码；重配方向时不得保留旧方向的硬件或
  DMA 占用。
- **普通 I/O 与 Stream 互斥**：普通 `read`/`write` 与 RX/TX stream
  是两条独立路径，backend 需明确并强制同方向互斥（禁止 DMA 与普通读同时消费接收寄存器）。
  `write` 仅要求调用者缓冲区在函数返回前有效（若异步发送需自行拷贝或明确契约）。
- **Stream 缓冲区归属**：`stream_start` 成功后硬件持有调用者缓冲区；`stream_stop`
  必须在返回前中止硬件传输并确保不再访问该缓冲区。`shutdown` 时公共层会自动调用已实现的
  `rx_stream_stop` / `tx_stream_stop`。
- **事件上报**：ISR 中须先清除硬件 pending 标志，再调用
  `mr_uart_event_raise(drv, events)` 通知 waiter，避免反复重入。

## 4. HardTree 安装

使用 `MR_HT_UART_INSTALL(_id, _priv, _ops)` 安装实例，宏会自动定义零初始化的
`mr_uart_data_t` 并绑定到 `MR_HT_DRIVER_INSTALL`。外设基地址、TX/RX 引脚复用、IRQ 与
DMA 通道等专有属性由对应平台文档定义。

## 5. 验收要点

- 波特率、数据位、停止位、校验、流控及重复 `configure` 行为正确。
- 普通 `read`/`write` 与单字节收发返回值、零长度处理及错误返回符合预期。
- 若启用 stream：RX/TX 独立启停、`is_busy`、`offset`、循环模式及 `stop` 后的缓冲区归还正确。
- `event_enable`/`event_disable` 门控与 ISR 事件上报无丢失或死循环，资源
  `shutdown` 后无残留中断或 DMA 访问。

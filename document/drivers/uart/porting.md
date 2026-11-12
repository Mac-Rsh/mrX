# UART 驱动移植指南

> 本文面向平台 backend 开发者，说明如何为新的 UART/USART 控制器实现 mrX
> 操作表。公共 API 与事件语义见 [UART](uart.md)；芯片特有运行期约束见对应平台
> 的 `README.md`。

## 1. 移植边界

UART 公共层负责类型化 API、`mr_uart_data_t.event` 的生命周期、事件位门控和
`-MR_ENOSYS` 降级；backend 负责波特率/帧格式映射、普通 read/write、中断事件
来源以及可选的 DMA stream。

| 关注点                         | 负责方                         |
|--------------------------------|--------------------------------|
| `mr_waitable_t` 的 init/deinit | 公共层（clazz 的 init/deinit） |
| 事件位掩码记录与哪些事件可订阅 | backend                        |
| 波特率、帧格式、方向引脚       | backend                        |
| 普通 read/write 的数据搬运     | backend                        |
| RX/TX DMA 通道与 buffer 所有权 | backend                        |

平台实现通常位于：

```text
mrX/arch/<vendor>/<target>/platform/drivers/uart/
```

后端私有数据通常保存 硬件上下文 或寄存器基址、事件掩码、当前配置和 stream
状态。

## 2. 操作表

backend 实现 `mrX/include/mrX/drivers/uart/mr_uart.h:104` 的 `mr_uart_ops_t`。

基础能力：

| 成员            | 签名要点                                                 | 必需 |
|-----------------|----------------------------------------------------------|------|
| `configure`     | `(drv, const mr_uart_config_t *config)`                  | 是   |
| `configuration` | `(drv, mr_uart_config_t *config)`                        | 是   |
| `read`          | `(drv, void *buf, mr_size_t size)`，返回已读字节数       | 是   |
| `write`         | `(drv, const void *buf, mr_size_t size)`，返回已写字节数 | 是   |

启用 `MR_USE_DRIVER_UART_STREAM` 后，两个方向各自独立，成员名带方向前缀—— **rx 与
tx 不是同一个成员**：

| 成员                | 签名要点                                                                           |
|---------------------|------------------------------------------------------------------------------------|
| `rx_stream_start`   | `(drv, void *buf, mr_size_t size, const mr_uart_rx_stream_config_t *config)`       |
| `rx_stream_stop`    | `(drv)`                                                                            |
| `rx_stream_is_busy` | `(drv, mr_bool_t *busy)`                                                           |
| `rx_stream_offset`  | `(drv, mr_size_t *offset)`                                                         |
| `tx_stream_start`   | `(drv, const void *buf, mr_size_t size, const mr_uart_tx_stream_config_t *config)` |
| `tx_stream_stop`    | `(drv)`                                                                            |
| `tx_stream_is_busy` | `(drv, mr_bool_t *busy)`                                                           |
| `tx_stream_offset`  | `(drv, mr_size_t *offset)`                                                         |

可选能力：

| 成员            | 签名要点                                      | 缺失时       |
|-----------------|-----------------------------------------------|--------------|
| `event_control` | `(drv, mr_uint32_t events, mr_bool_t enable)` | `-MR_ENOSYS` |

普通 read/write 与 stream 是两套路径：backend 必须明确同一方向同时使用时的互斥
规则，不能让 DMA 与普通读同时消费 RDR。

## 3. 配置映射

将 `mr_uart_config_t` 的波特率、数据位、停止位、校验与方向选项映射到硬件；对不支持的组合返回错误。复用引脚、时钟、IRQ 与 DMA 资源由平台管理，重新配置时不得留下旧方向的硬件占用。

## 4. 普通 read/write

普通接收以调用者提供的容量为上限，普通发送只接受调用者在函数返回前保证有效的数据。成功返回实际读写长度或约定值；无法立即发送时是否阻塞及队列所有权应按 backend 契约清楚说明。

## 5. 事件

事件控制与 `mr_uart_event_raise()` 负责传播收发状态和错误；先清硬件 pending 再通知 waiter，不能假定所有目标都以相同 UART FIFO 状态定义 RX/TX 可用性。公共层不替 backend 解析寄存器标志。

## 6. Stream

启用 stream 后，按公共流式接口维护方向 busy、offset、完成和停止语义。硬件使用应用缓冲区期间禁止调用者复用，stop 必须在返回前保证不再访问相应数据。DMA、IRQ 或其它流式实现均由后台选择，不得把任何控制器的 DMA 规则写成公共要求。

## 7. HardTree 与安装

使用 `MR_HT_UART_INSTALL` 或通用设备注册宏。外设实例、TX/RX 复用、IRQ 与 DMA 的属性名称及合法值在对应平台文档定义；公共文档只列通用配置和接口。

## 8. 移植验收

- [ ] 速率、数据位、奇偶校验、方向及重配行为正确。
- [ ] 普通 I/O、等待事件及 stream 的长度、offset 与停止语义一致。
- [ ] 资源关闭和失败回滚不会留下硬件访问或晚到通知。

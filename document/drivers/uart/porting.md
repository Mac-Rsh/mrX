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

后端私有数据通常保存 HAL handle 或寄存器基址、事件掩码、当前配置和 stream
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

启用 `MR_USE_DRIVER_UART_STREAM` 后，两个方向各自独立，成员名带方向前缀—— **rx 与 tx 不是同一个成员**：

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

`mr_uart_config_t` 需要映射 `baud_rate`、`data_bits`、`stop_bits`、`parity`、
`flow_ctrl` 和 `mode`。

约定：

- 合法但本平台不支持的组合必须返回明确错误，不要静默改成相近配置。区分
  `-MR_EINVAL`（参数本身非法）与 `-MR_ENOSYS`（参数合法但本平台不支持）是有意义
  的：前者是调用错误，后者是可以降级或换实例的能力缺失。
- `configuration` 应返回 backend 实际采用的值。
- 方向映射要注意：`MR_UART_MODE_TX`/`RX`/`TX_RX` 决定实际需要的引脚。切换方向时
  应释放不再使用的方向引脚（STM32H5 通过 GPIO owner helper 完成），并校验新方向
  需要的引脚确实已由 HardTree 提供。
- 重新配置与正在进行的发送/接收冲突时返回 `-MR_EBUSY`，不要中断已有数据。

## 4. 普通 read/write

普通 `read`/`write` 是直接、短操作接口：

- `read` 只取当前硬件已经可读的数据；
- `write` 只写当前硬件能够接受的数据；
- 两者返回 **实际完成**的字节数，可以是 0；
- 不要在公共 API 背后偷偷创建长期异步 buffer 所有权。

这样上层可以结合 UART wait 事件构成中断驱动的非 stream 收发。

## 5. 事件

公共事件位共 **9** 个（`mrX/include/mrX/drivers/uart/mr_uart.h:28-36`）：

| 事件位                        | 位 | 含义                                                    |
|-------------------------------|----|---------------------------------------------------------|
| `MR_UART_WAIT_RX_IDLE`        | 0  | 接收线路进入 idle                                       |
| `MR_UART_WAIT_RX_READY`       | 1  | 已有数据可被普通 `mr_uart_read()` 取走（RXNE/RXFNE 类） |
| `MR_UART_WAIT_TX_DONE`        | 2  | 最后一个停止位已真正发出（TC 类）                       |
| `MR_UART_WAIT_RX_ERROR`       | 3  | 接收侧错误（PE/FE/NE/ORE 类）                           |
| `MR_UART_WAIT_TX_ERROR`       | 4  | 发送侧错误                                              |
| `MR_UART_WAIT_RX_STREAM_HALF` | 5  | RX stream buffer 半满                                   |
| `MR_UART_WAIT_RX_STREAM_FULL` | 6  | RX stream buffer 全满                                   |
| `MR_UART_WAIT_TX_STREAM_HALF` | 7  | TX stream buffer 半空                                   |
| `MR_UART_WAIT_TX_STREAM_DONE` | 8  | TX stream 本轮 buffer 已由 DMA 搬完                     |

注意： **没有 `MR_UART_WAIT_TX_STREAM_FULL`**，TX 方向的完成位是
`TX_STREAM_DONE`；全满类事件只存在于 RX 方向。

`event_control` 维护 backend 的事件掩码，并据此开关真实 IRQ source。ISR 推荐
顺序：

1. 判断 flag 与 interrupt source（两者都要判断，只判断 flag 会在未使能时误报）；
2. 清除或临时屏蔽会持续触发的状态；
3. 更新 backend 状态；
4. 聚合公共 event bits；
5. 调用 `mr_uart_event_raise()`。

映射建议：

- `RX_READY` 常来自电平型条件。若 waiter 只投递任务而不立即读取，应避免因状态持续
  有效造成 IRQ storm，可采用 one-shot 屏蔽并在 read 后 rearm。
- `TX_DONE` 不应在“写入 TDR/FIFO”时触发，而应等物理 UART 发送完成。
- RX/TX ERROR 平台可区分后合并到对应公共错误事件。

waiter 同步运行在 raise 上下文，ISR 路径不能依赖 waiter 阻塞等待。

## 6. Stream 与 DMA

- stream 事件见第 5 节的位 5~8。`TX_STREAM_DONE` 表示 DMA 已完成本轮 buffer 搬运，
  不等同于最后一个 stop bit 已发出；二者不能由同一个 DMA DONE 状态简单等价触发。
- stream 方向必须与当前配置的方向相容；向配置为 TX 的实例启动 RX stream（或反之）
  应返回 `-MR_EINVAL`。
- RX stream 激活期间，普通 `RX_READY` 路径不应再和 DMA 同时读取 RDR；TX stream
  激活期间普通 `write` 应与 DMA 所有权互斥。
- 已在运行的 stream 再次启动返回 `-MR_EBUSY`；一次性 stream 已完成时应先内部收尾再
  接受新请求。
- stream 依赖静态 DMA 资源。未绑定 DMA 时返回 `-MR_ENODEV`；未配置（未
  `configure`）的实例同样返回 `-MR_ENODEV`。
- stop 返回前必须停止外设请求和 DMA/中断，并保证不再访问用户 buffer。stop 失败时
  保留活动状态和 DMA waiter，调用方应保留 buffer、驱动和关联资源并重试， **不能**
  把错误返回当作已经停止硬件。
- 可能被 DMA 访问的 buffer 需按平台 cache 契约处理一致性（RX 方向 invalidate，
  TX 方向 clean 与必要 barrier）。

## 7. HardTree 与安装

用 `MR_HT_UART_INSTALL(_id, _priv, _ops)` 按 compat 实例批量安装。

STM32H5 backend（compat `st_stm32h5_uart`）读取的属性：

| 属性              | 必需 | 说明                                             |
|-------------------|------|--------------------------------------------------|
| `st_uartx`        | 是   | UART 实例基址（如 `USART1`）                     |
| `st_clken`        | 是   | 外设时钟使能函数                                 |
| `st_irqn`         | 是   | NVIC 中断号                                      |
| `st_irq_priority` | 是   | NVIC 优先级                                      |
| `st_irq_handler`  | 是   | 该实例的向量函数名；backend 据此生成中断处理函数 |
| `st_tx`           | 否   | TX 引脚 GPIO spec `[phandle, pin]`               |
| `st_tx_af`        | 否   | TX 复用功能编号，缺省 `0`                        |
| `st_rx`           | 否   | RX 引脚 GPIO spec `[phandle, pin]`               |
| `st_rx_af`        | 否   | RX 复用功能编号，缺省 `0`                        |
| `st_rx_dma`       | 否   | RX stream 使用的 DMA spec（`MR_HT_DMA_SPEC`）    |
| `st_tx_dma`       | 否   | TX stream 使用的 DMA spec（`MR_HT_DMA_SPEC`）    |

两个 DMA 属性只有在 `MR_USE_DRIVER_UART_STREAM` 启用时才会被展开。引脚规格
`st_tx`/`st_rx` 里的 `pin` 与 `st_tx_af`/`st_rx_af` 都必须在 0~15 范围内，否则该
方向在 `configure` 时得到 `-MR_EINVAL`。

复用功能由 GPIO 拥有者 helper 配置； **不要**把这些引脚再配成 EXTI，也不要让 UART
backend 自己直接写 GPIO 寄存器。

## 8. 参考实现与芯片能力边界

参考实现：

```text
mrX/arch/st/stm32h5/platform/drivers/uart/uart.c
```

它已实现普通非阻塞 read/write、`RX_READY`/`RX_IDLE`/`TX_DONE`/错误事件、
`RX_READY` one-shot 加 read 后 rearm、stream 与普通 RX 路径互斥、DMA
HALF/FULL/DONE/ERROR 转换、`TX_STREAM_DONE` 与物理 `TX_DONE` 分离，以及 HardTree
实例安装。

可作为取舍参考的 STM32H5 能力边界（实现新 backend 时按目标芯片重新确认，不要
默认继承）：

- `flow_ctrl` 只支持 `MR_UART_FLOW_CTRL_NONE`，其他取值返回 `-MR_ENOSYS`；
- `data_bits` 只支持 7 位和 8 位，其他取值返回 `-MR_ENOSYS`；
- `stop_bits` 和 `parity` 的越界取值返回 `-MR_EINVAL`；
- RX stream 在 7 位数据 + 非 `MR_UART_PARITY_NONE` 时返回 `-MR_ENOSYS`，因为 DMA
  无法剥离校验位；
- 两个方向都必须通过 HardTree 提供 `st_rx_dma`/`st_tx_dma` 才能使用 stream，否则
  返回 `-MR_ENODEV`。

## 9. 移植验收清单

- `configure`/`configuration` 对每一个字段都能稳定往返，不合法的取值有明确错误。
- 不支持的合法组合返回 `-MR_ENOSYS` 而不是静默降级。
- `read`/`write` 返回实际完成字节数，可以是 0。
- 事件掩码只放开已订阅的事件位；ISR 同时判断 flag 与 interrupt source。
- `RX_READY` 不产生持续 IRQ storm（电平型来源有 one-shot 或等价策略）。
- `TX_DONE` 确实代表线路发送完成，而不是写入数据寄存器。
- stream 方向与 UART 方向相容性有校验；不兼容返回 `-MR_EINVAL`。
- stream 未绑定 DMA 时返回 `-MR_ENODEV`；未 configure 时同样返回 `-MR_ENODEV`。
- `TX_STREAM_DONE` 与物理 `TX_DONE` 语义分离。
- stream 与普通 IO 不争用 RDR/TDR。
- stop 成功后硬件不再访问 buffer；stop 失败时保留可重试状态。
- RX/TX buffer 的 cache 一致性按平台契约处理。
- HardTree 的 pin/clock/IRQ/DMA 属性齐全且与板级一致。

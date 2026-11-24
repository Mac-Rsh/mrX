# UART 驱动（UART）

提供异步串行通信抽象：支持波特率/帧格式/流控配置、无软件 FIFO 的直接字节读写，以及收发双方向独立的可选
stream 传输与等待事件通知。

| 项目   | 说明                                                                                                                          |
|--------|-------------------------------------------------------------------------------------------------------------------------------|
| 功能宏 | `MR_USE_DRIVER_UART`、`MR_USE_DRIVER_UART_STREAM`                                                                             |
| 头文件 | `<mrX/drivers/uart/mr_uart.h>`                                                                                                |
| 依赖   | [驱动框架 Driver](../driver.md)、[等待队列 Wait](../../kernel/wait.md)、[动态内存 Malloc](../../libc/malloc.md)（仅动态创建） |
| 测试   | `test/drivers/uart/uart.c`                                                                                                    |

## 配置

| 配置项                      | 类型 | 默认值 | 说明                                                             |
|-----------------------------|------|--------|------------------------------------------------------------------|
| `MR_USE_DRIVER_UART`        | bool | n      | 启用 UART 驱动框架，依赖 `MR_USE_DRIVER`                         |
| `MR_USE_DRIVER_UART_STREAM` | bool | y      | 启用收发 stream 传输与相关 `ops` 回调，依赖 `MR_USE_DRIVER_UART` |

## 快速上手

配置 115200-8-N-1 并收发字符：

```c
#include <mrX/drivers/uart/mr_uart.h>

void uart_example(mr_driver_t *drv) {
    mr_uart_config_t config = {
        .baud_rate = 115200,
        .data_bits = MR_UART_DATA_BITS_8,
        .stop_bits = MR_UART_STOP_BITS_1,
        .parity = MR_UART_PARITY_NONE,
        .flow_ctrl = MR_UART_FLOW_CTRL_NONE,
        .mode = MR_UART_MODE_TX_RX,
    };
    mr_uint8_t ch;

    if (mr_uart_configure(drv, &config) != 0) {
        return;
    }
    mr_uart_getchar(drv, &ch);
    mr_uart_putchar(drv, 'A');
}
```

订阅接收就绪、stream 进度与错误事件：

```c
static void uart_waiter(mr_waiter_t *waiter, void *param,
                        mr_wait_event_t event, const void *data) {
    /* 运行在 backend raise 所在上下文（可能为 ISR），禁止阻塞 */
}

void uart_event_example(mr_driver_t *drv) {
    static mr_waiter_t waiter;
    mr_uint32_t mask = MR_UART_WAIT_RX_READY | MR_UART_WAIT_RX_STREAM_HALF |
                       MR_UART_WAIT_RX_STREAM_FULL | MR_UART_WAIT_TX_DONE |
                       MR_UART_WAIT_RX_ERROR | MR_UART_WAIT_TX_ERROR;

    mr_waiter_init(&waiter, uart_waiter, MR_NULL, mask);
    mr_uart_event_attach(drv, &waiter);
    mr_uart_event_enable(drv, mask);
    /* 启动传输 ... 结束后调用 mr_uart_event_disable(drv, mask) */
}
```

## 核心机制与约束

- **配置模型**：`mr_uart_config_t` 包含 `baud_rate`、`data_bits`（`5`/`6`/`7`/`8`）、
  `stop_bits`（`1`/`1_5`/`2`）、`parity`（`NONE`/`ODD`/`EVEN`）、`flow_ctrl`（`NONE`/
  `RTS`/`CTS`/`RTS_CTS`）与 `mode`（`RX`/`TX`/`TX_RX`）。公共层直接透传给
  `ops->configure` / `ops->configuration`，是否支持特定组合由 backend 决定。
- **无软件 FIFO 直接读写**：`mr_uart_read` / `mr_uart_write` 在 `size == 0` 时直接返回
  0，其余情况透传 backend 并返回实际读写字节数或负错误码。`mr_uart_getchar` /
  `mr_uart_putchar` 为单字节封装。阻塞/超时策略及与 stream 的互斥由具体 backend
  定义。
- **双方向独立 Stream**（`MR_USE_DRIVER_UART_STREAM`）：RX 和 TX 各自提供 `start`、
  `stop`、`is_busy`、`offset` 四个可选回调（未实现返回 `-MR_ENOSYS`）。`start` 要求
  `buf != MR_NULL` 且 `size != 0`；配置结构 `mr_uart_rx_stream_config_t` /
  `mr_uart_tx_stream_config_t` 仅含 `circular` 字段，循环缓冲、offset 语义与缓冲区所有权归
  backend 约定。
- **生命周期与停流**：`setup` 时初始化 `mr_uart_data_t.event`；`shutdown` 时若启用了
  stream，会先尝试停止已实现的 RX/TX stream，普通 `shutdown` 中任一 `stream_stop`
  返回错误会中止卸载并返回错误，而 `mr_driver_del`（带 `MR_DRIVER_IS_DYING`
  标志）会忽略停流错误继续销毁 `event`。
- **事件机制**：`mr_uart_event_attach` 将 waiter 挂入框架 `waitable`；
  `event_enable` / `event_disable` 透传事件掩码给 `ops->event_control`（未实现返回
  `-MR_ENOSYS`）。`mr_uart_event_raise` 以无 payload（`MR_NULL`）同步通知
  waiter，回调运行在 `raise` 所在上下文。

| 事件常量                      | 位值      | 语义             |
|-------------------------------|-----------|------------------|
| `MR_UART_WAIT_RX_IDLE`        | `1U << 0` | 接收总线空闲     |
| `MR_UART_WAIT_RX_READY`       | `1U << 1` | 普通接收数据就绪 |
| `MR_UART_WAIT_TX_DONE`        | `1U << 2` | 普通发送完成     |
| `MR_UART_WAIT_RX_ERROR`       | `1U << 3` | 接收错误         |
| `MR_UART_WAIT_TX_ERROR`       | `1U << 4` | 发送错误         |
| `MR_UART_WAIT_RX_STREAM_HALF` | `1U << 5` | RX stream 半满   |
| `MR_UART_WAIT_RX_STREAM_FULL` | `1U << 6` | RX stream 全满   |
| `MR_UART_WAIT_TX_STREAM_HALF` | `1U << 7` | TX stream 半完成 |
| `MR_UART_WAIT_TX_STREAM_DONE` | `1U << 8` | TX stream 完成   |

## API 速查

### 初始化与销毁

| API                                                  | 说明                                                       |
|------------------------------------------------------|------------------------------------------------------------|
| `MR_UART_DEFINE(_name, _data, _priv, _ops)`          | 编译期定义静态 UART 驱动（不注册）                         |
| `MR_UART_INSTALL(_name, _data, _priv, _ops, _level)` | 编译期定义并注册 UART 驱动                                 |
| `MR_HT_UART_INSTALL(_id, _priv, _ops)`               | 通过 HardTree 节点自动定义零初始化 `data` 并安装 UART 实例 |
| `mr_uart_init(drv, data, priv, ops)`                 | 运行时初始化静态 UART 驱动                                 |
| `mr_uart_create(data, priv, ops)`                    | 堆上动态创建 UART 驱动，失败返回 `MR_NULL`                 |
| `mr_uart_del(drv)`                                   | 注销并删除 UART 驱动                                       |
| `mr_uart_get(drv)` / `mr_uart_put(drv)`              | 引用计数加一 / 减一                                        |

### 配置与直接收发

| API                                  | 说明                                           |
|--------------------------------------|------------------------------------------------|
| `mr_uart_configure(drv, config)`     | 写入 UART 通信参数                             |
| `mr_uart_configuration(drv, config)` | 读取当前 UART 通信参数                         |
| `mr_uart_read(drv, buf, size)`       | 读取最多 `size` 字节，返回实际字节数或负错误码 |
| `mr_uart_write(drv, buf, size)`      | 写入最多 `size` 字节，返回实际字节数或负错误码 |
| `mr_uart_getchar(drv, ch)`           | 读取单个字节                                   |
| `mr_uart_putchar(drv, ch)`           | 写入单个字节                                   |

### Stream 与事件（Stream 需 `MR_USE_DRIVER_UART_STREAM`）

| API                                               | 说明                                         |
|---------------------------------------------------|----------------------------------------------|
| `mr_uart_rx_stream_start(drv, buf, size, config)` | 启动 RX stream                               |
| `mr_uart_rx_stream_stop(drv)`                     | 停止 RX stream                               |
| `mr_uart_rx_stream_is_busy(drv, busy)`            | 查询 RX stream 忙碌状态                      |
| `mr_uart_rx_stream_offset(drv, offset)`           | 查询 RX stream 当前偏移                      |
| `mr_uart_tx_stream_start(drv, buf, size, config)` | 启动 TX stream                               |
| `mr_uart_tx_stream_stop(drv)`                     | 停止 TX stream                               |
| `mr_uart_tx_stream_is_busy(drv, busy)`            | 查询 TX stream 忙碌状态                      |
| `mr_uart_tx_stream_offset(drv, offset)`           | 查询 TX stream 当前偏移                      |
| `mr_uart_event_attach(drv, entry)`                | 挂载 waiter 到事件队列（不自动使能硬件中断） |
| `mr_uart_event_enable(drv, events)`               | 使能指定事件掩码                             |
| `mr_uart_event_disable(drv, events)`              | 禁用指定事件掩码                             |
| `mr_uart_event_raise(drv, events)`                | 触发事件通知（供 backend 调用）              |

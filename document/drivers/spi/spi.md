# SPI 驱动（SPI）

提供 SPI 控制器级的按片选索引（`cs`）配置、同步全双工多消息传输、可选单消息异步
stream、Slave 帧队列收发及 waitable 事件通知。

| 项目   | 说明                                                                                                                          |
|--------|-------------------------------------------------------------------------------------------------------------------------------|
| 功能宏 | `MR_USE_DRIVER_SPI`、`MR_USE_DRIVER_SPI_STREAM`                                                                               |
| 头文件 | `<mrX/drivers/spi/mr_spi.h>`                                                                                                  |
| 依赖   | [驱动框架 Driver](../driver.md)、[等待队列 Wait](../../kernel/wait.md)、[动态内存 Malloc](../../libc/malloc.md)（仅动态创建） |
| 测试   | `test/drivers/spi/spi.c`                                                                                                      |

## 配置

| 配置项                     | 类型 | 默认值 | 说明                                                                  |
|----------------------------|------|--------|-----------------------------------------------------------------------|
| `MR_USE_DRIVER_SPI`        | bool | n      | 启用 SPI 控制器驱动，依赖 `MR_USE_DRIVER`                             |
| `MR_USE_DRIVER_SPI_STREAM` | bool | y      | 启用单消息异步 stream 传输与相关 `ops` 回调，依赖 `MR_USE_DRIVER_SPI` |

## 快速上手

配置片选 0 并执行一次“发送命令 + 接收 3 字节响应”传输：

```c
#include <mrX/drivers/spi/mr_spi.h>

void spi_master_example(mr_driver_t *spi) {
    mr_spi_config_t config = {
        .clock_rate = 1000000U,
        .data_bits = 8U,
        .mode = MR_SPI_MODE_0,
        .bit_order = MR_SPI_BIT_ORDER_MSB,
        .role = MR_SPI_ROLE_MASTER,
    };
    mr_uint8_t cmd[] = {0x9fU};
    mr_uint8_t id[3] = {0};
    mr_spi_msg_t msgs[] = {
        {.tx_buf = cmd, .tx_len = sizeof(cmd)},
        {.rx_buf = id,  .rx_len = sizeof(id)},
    };

    if (mr_spi_configure(spi, 0U, &config) != 0) {
        return;
    }
    /* 整个 msgs 数组期间 CS 保持有效，成功返回已完成的消息数（此处为 2） */
    mr_ssize_t ret = mr_spi_transfer(spi, 0U, msgs, 2U);
    (void)ret;
}
```

## 核心机制与约束

- **控制器与片选（CS）模型**：SPI 驱动代表一颗控制器（不单独创建 `spi_device`
  对象），下挂从设备通过逻辑片选索引 `cs` 区分，配置按 `cs` 独立保存。HardTree 消费方可用
  `[phandle, cs]` 属性通过 `MR_HT_SPI_SPEC` / `MR_HT_NODE_SPI_SPEC` 生成
  `mr_ht_spi_spec_t`，再使用 `mr_ht_spi_*` 包装函数操作。
- **配置与数据帧宽度**：`mr_spi_config_t` 包含 `clock_rate`（`0` 表示禁用该 CS）、
  `data_bits`、`mode`（`MR_SPI_MODE_0`～`3`）、`bit_order`（`MSB`/`LSB`）与 `role`（
  `MASTER`/`SLAVE`）。`mr_spi_msg_t` 中的 `tx_len`/`rx_len` 为 **字节数**，且必须按
  `data_bits` 对应的元素宽度对齐：

| `data_bits` | Buffer 元素类型 | 对齐字节数 |
|-------------|-----------------|------------|
| 1～8        | `mr_uint8_t`    | 1          |
| 9～16       | `mr_uint16_t`   | 2          |
| 17～32      | `mr_uint32_t`   | 4          |

- **传输语义**：`mr_spi_transfer` 在整个 `msgs` 数组期间保持 CS 有效，`num == 0`
  直接返回 0。每条消息时钟输出 `max(tx_len, rx_len)` 字节对应帧数：TX 缓冲区为空或耗尽时发送全
  1 dummy 帧；RX 缓冲区为空或耗尽时丢弃接收数据；两个缓冲区均为空时仍可仅产生时钟。
- **单消息异步 Stream**（`MR_USE_DRIVER_SPI_STREAM`）：`mr_spi_stream_start` 仅接受单条
  `mr_spi_msg_t`（不支持消息数组）。非循环模式（`circular == MR_FALSE`）传输
  `max(tx_len, rx_len)` 字节；循环模式（`circular == MR_TRUE`）下非空的 TX/RX
  缓冲区各自按自身长度独立回绕。活动 stream **独占整个控制器**，运行期间调用方必须保持
  `msg` 与缓冲区有效，且不得并发访问或重配同一控制器；`mr_spi_stream_stop` 返回 0
  后硬件才释放缓冲区所有权。
- **并发与总线互斥**：公共层不提供总线锁或跨调用 CS 保持，调用方必须串行化同一控制器的访问。
- **Slave 与事件**：`mr_spi_slave_read` / `mr_spi_slave_write` 按 `cs` 访问 Slave
  帧队列（`size` 须按元素宽度对齐）。`mr_spi_event_raise` 同步分发事件并携带
  `mr_spi_event_data_t`（内含 `cs` 字段标识事件来源，回调不得保存该指针）。

| 事件常量                     | 位值       | 语义                                       |
|------------------------------|------------|--------------------------------------------|
| `MR_SPI_WAIT_SELECT`         | `1U << 0`  | CS 有效，Slave 事务开始                    |
| `MR_SPI_WAIT_RX_READY`       | `1U << 1`  | Slave RX 队列已有接收帧                    |
| `MR_SPI_WAIT_TX_READY`       | `1U << 2`  | Slave TX 队列可继续写入                    |
| `MR_SPI_WAIT_DESELECT`       | `1U << 3`  | CS 释放，Slave 事务结束                    |
| `MR_SPI_WAIT_ERROR`          | `1U << 4`  | 普通传输/Slave 错误                        |
| `MR_SPI_WAIT_RX_STREAM_HALF` | `1U << 5`  | RX stream 前半区完成                       |
| `MR_SPI_WAIT_RX_STREAM_FULL` | `1U << 6`  | RX stream 填满（循环模式下回绕）           |
| `MR_SPI_WAIT_TX_STREAM_HALF` | `1U << 7`  | TX stream 前半区发送完成                   |
| `MR_SPI_WAIT_TX_STREAM_FULL` | `1U << 8`  | TX stream 发送完成（循环模式下回绕）       |
| `MR_SPI_WAIT_STREAM_ERROR`   | `1U << 9`  | Stream 出错，整体失败                      |
| `MR_SPI_WAIT_STREAM_DONE`    | `1U << 10` | 非循环 stream 彻底结束（Master 已释放 CS） |

## API 速查

### 初始化与销毁

| API                                                 | 说明                                                      |
|-----------------------------------------------------|-----------------------------------------------------------|
| `MR_SPI_DEFINE(_name, _data, _priv, _ops)`          | 编译期定义静态 SPI 驱动（不注册）                         |
| `MR_SPI_INSTALL(_name, _data, _priv, _ops, _level)` | 编译期定义并注册 SPI 驱动                                 |
| `MR_HT_SPI_INSTALL(_id, _priv, _ops)`               | 通过 HardTree 节点自动定义零初始化 `data` 并安装 SPI 实例 |
| `mr_spi_init(drv, data, priv, ops)`                 | 运行时初始化静态 SPI 驱动                                 |
| `mr_spi_create(data, priv, ops)`                    | 堆上动态创建 SPI 驱动，失败返回 `MR_NULL`                 |
| `mr_spi_del(drv)`                                   | 注销并删除 SPI 驱动                                       |
| `mr_spi_get(drv)` / `mr_spi_put(drv)`               | 引用计数加一 / 减一                                       |

### 配置、传输与 Slave

| API                                      | 说明                                         |
|------------------------------------------|----------------------------------------------|
| `mr_spi_configure(drv, cs, config)`      | 配置指定 `cs` 的总线参数                     |
| `mr_spi_configuration(drv, cs, config)`  | 读取指定 `cs` 的当前配置                     |
| `mr_spi_transfer(drv, cs, msgs, num)`    | 同步传输消息数组，返回完成的消息数或负错误码 |
| `mr_spi_slave_read(drv, cs, buf, size)`  | 读取 Slave RX 队列数据                       |
| `mr_spi_slave_write(drv, cs, buf, size)` | 写入 Slave TX 队列数据                       |

### Stream、事件与 HardTree Spec（Stream 需 `MR_USE_DRIVER_SPI_STREAM`）

| API                                                                              | 说明                                                                    |
|----------------------------------------------------------------------------------|-------------------------------------------------------------------------|
| `mr_spi_stream_start(drv, cs, msg, config)`                                      | 启动单消息异步 stream                                                   |
| `mr_spi_stream_stop(drv)`                                                        | 停止当前控制器上的 stream                                               |
| `mr_spi_stream_is_busy(drv, busy)`                                               | 查询 stream 是否忙碌                                                    |
| `mr_spi_rx_stream_offset(drv, offset)` / `mr_spi_tx_stream_offset(drv, offset)`  | 查询 RX / TX stream 当前字节偏移                                        |
| `mr_spi_event_attach(drv, entry)`                                                | 挂载 waiter 到控制器事件队列                                            |
| `mr_spi_event_enable(drv, cs, events)` / `mr_spi_event_disable(drv, cs, events)` | 使能 / 禁用指定 `cs` 的事件掩码                                         |
| `mr_spi_event_raise(drv, events, data)`                                          | 同步触发事件并传递 `mr_spi_event_data_t`（供 backend 调用）             |
| `MR_HT_NODE_SPI_SPEC(_node, _prop)` / `MR_HT_SPI_SPEC(_id, _prop)`               | 从 `[phandle, cs]` 属性展开 `mr_ht_spi_spec_t`（另有 `*_FOREACH` 变体） |
| `mr_ht_spi_*`                                                                    | 基于 `mr_ht_spi_spec_t` 调用对应 `mr_spi_*` 接口的包装函数              |

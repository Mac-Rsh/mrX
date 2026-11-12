# SPI 驱动（SPI）

> SPI 公共层提供按片选索引配置、同步传输、可选的单 message stream、Slave 收发和
> wait/event。片选资源、配置缓存与实际总线操作由
> backend 实现。

| 项目     | 说明                                                                              |
|----------|-----------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_DRIVER_SPI`（默认关闭）；`MR_USE_DRIVER_SPI_STREAM`（默认开启，依赖 SPI） |
| 头文件   | `<mrX/drivers/spi/mr_spi.h>`                                                      |
| 依赖模块 | [驱动框架 Driver](../driver.md)、[等待队列 Wait](../../kernel/wait.md)            |

---

## 用户指南

### 1. 模型

SPI driver 表示一颗 Controller，不另外创建 `spi_device` 对象。Controller 下的从设备由
`cs`
索引区分：

```text
SPI Controller
    +-- cs 0 + config 0
    +-- cs 1 + config 1
    `-- cs n + config n
```

`cs` 只是 backend 资源表的索引。公共层不解释索引的硬件含义，也不维护 CS 数量。GPIO、硬件
NSS
或其他片选方式均由 backend 映射。

HardTree 中可用 `mr_ht_spi_spec_t` 表示一个逻辑 SPI endpoint：

```c
typedef struct mr_ht_spi_spec {
    mr_driver_t *drv;
    mr_uint32_t cs;
} mr_ht_spi_spec_t;
```

spec 属性单元格式为 `[phandle, cs]`。其中 `phandle` 引用 SPI Controller，`cs` 是该
Controller 的
逻辑片选索引。例如消费 SPI 的设备可声明 `spi = <&spi0 1>`，再通过
`MR_HT_NODE_SPI_SPEC(node, spi)` 或 `MR_HT_SPI_SPEC(id, spi)` 生成 spec。它与 SPI
Controller
节点中描述物理片选资源的 `cs = <<&gpio0 4> <&gpio1 0>>` 不同，后者只供 backend 建立
CS
资源表。

### 2. 配置

```c
typedef struct mr_spi_config {
    mr_uint32_t clock_rate;
    mr_uint8_t data_bits;
    mr_uint8_t mode: 2;
    mr_uint8_t bit_order: 1;
    mr_uint8_t role: 1;
} mr_spi_config_t;
```

- `clock_rate`：请求的 SPI 时钟频率；backend 决定是否能精确实现。
- `data_bits`：每个 frame 的有效位数。
- `mode`：`MR_SPI_MODE_0` ～ `MR_SPI_MODE_3`。
- `bit_order`：`MR_SPI_BIT_ORDER_MSB` 或 `MR_SPI_BIT_ORDER_LSB`。
- `role`：`MR_SPI_ROLE_MASTER` 或 `MR_SPI_ROLE_SLAVE`。

Master 的 `clock_rate` 是请求输出频率；Slave 的 `clock_rate` 表示允许的最大输入频率。Backend
无法实现指定
角色时返回 `-MR_ENOSYS`。`clock_rate == 0` 仍表示禁用该 CS。

配置按 CS 索引保存：

```c
mr_spi_config_t config = {
    .clock_rate = 1000000U,
    .data_bits = 8U,
    .mode = MR_SPI_MODE_0,
    .bit_order = MR_SPI_BIT_ORDER_MSB,
    .role = MR_SPI_ROLE_MASTER,
};

mr_err_t ret = mr_spi_configure(spi, 0U, &config);
```

`mr_spi_configuration` 读取指定 CS 的当前配置。索引无效时 backend 应返回
`-MR_EINVAL`。

### 3. Message 与传输

```c
typedef struct mr_spi_msg {
    const void *tx_buf;
    mr_size_t tx_len;
    void *rx_buf;
    mr_size_t rx_len;
} mr_spi_msg_t;
```

`tx_len` 和 `rx_len` 分别表示 TX/RX buffer 的字节数。一个 frame 在 buffer
中占用的元素宽度由
`data_bits` 决定：

| data_bits | Buffer 元素   |
|-----------|---------------|
| 1～8      | `mr_uint8_t`  |
| 9～16     | `mr_uint16_t` |
| 17～32    | `mr_uint32_t` |

两个长度都必须是元素宽度的整数倍，否则 backend 返回 `-MR_EINVAL`。Backend
在内部使用长度除以
buffer 元素宽度得到对应方向的 frame 数。例如 3 个 12-bit frame 使用
`mr_uint16_t[3]` 保存，该方向长度为
6，而总线传输 36 个有效 bit。

Buffer 语义：

- 每个 message 产生 `max(tx_len, rx_len)` 字节对应的总线 frame。
- TX buffer 为空或先用尽时，剩余 frame 发送全 1 dummy 数据。
- RX buffer 为空或先用尽时，剩余接收数据被丢弃。
- 两个 buffer 都为空时，仍可由任一非零长度请求只产生时钟。

```c
mr_uint8_t command[] = {0x9fU};
mr_uint8_t id[3] = {0};
mr_spi_msg_t msgs[] = {
    {.tx_buf = command, .tx_len = sizeof(command)},
    {.rx_buf = id, .rx_len = sizeof(id)},
};

mr_ssize_t ret = mr_spi_transfer(spi, 0U, msgs, 2U);
```

一次 `mr_spi_transfer` 只使用一个 CS。Backend 在整个 message 数组期间保持 CS
有效，成功返回完成的
message 数。`num == 0` 时公共层直接返回 0，不访问 backend 或片选。

### 4. Stream

SPI stream 配合 GPIO reset/enable 的完整顺序、Master DONE 与 TX FULL 的区别见
[驱动组合与进阶示例](../examples.md)。

启用 `MR_USE_DRIVER_SPI_STREAM` 后，SPI 提供一次只处理一个 `mr_spi_msg_t` 的异步
stream：

```c
typedef struct mr_spi_stream_config {
    mr_bool_t circular;
} mr_spi_stream_config_t;

mr_err_t mr_spi_stream_start(mr_driver_t *spi, mr_uint32_t cs,
                             const mr_spi_msg_t *msg,
                             const mr_spi_stream_config_t *config);
mr_err_t mr_spi_stream_is_busy(mr_driver_t *spi, mr_bool_t *busy);
mr_err_t mr_spi_stream_stop(mr_driver_t *spi);
mr_err_t mr_spi_rx_stream_offset(mr_driver_t *spi, mr_size_t *offset);
mr_err_t mr_spi_tx_stream_offset(mr_driver_t *spi, mr_size_t *offset);
```

Stream 不接受 message 数组。`circular == MR_FALSE` 时传输 `max(tx_len, rx_len)`
字节，较短的方向分别补
dummy 或丢弃数据。`circular == MR_TRUE` 时，非空的 TX/RX buffer 分别按照自己的长度独立回绕；例如
`tx_len == 1`、`rx_len == 1024` 表示循环发送一个 TX 元素，同时连续接收 1024 字节后回绕。

`start` 返回 0 只表示异步传输已启动。TX/RX buffer 的进度分别通知：

| 事件                         | 含义                                   |
|------------------------------|----------------------------------------|
| `MR_SPI_WAIT_RX_STREAM_HALF` | RX buffer 前半区已经完成               |
| `MR_SPI_WAIT_RX_STREAM_FULL` | RX buffer 已填满；循环模式下随后回绕   |
| `MR_SPI_WAIT_TX_STREAM_HALF` | TX buffer 前半区已经发送               |
| `MR_SPI_WAIT_TX_STREAM_FULL` | TX buffer 已发送完；循环模式下随后回绕 |
| `MR_SPI_WAIT_STREAM_ERROR`   | 任一数据路径出错，整个 stream 失败     |
| `MR_SPI_WAIT_STREAM_DONE`    | 非循环 SPI 事务结束且 Master 已释放 CS |

`mr_spi_rx_stream_offset` 和 `mr_spi_tx_stream_offset` 分别查询两个方向已经完成的字节数。非循环方向完成后
等于相应的 `rx_len` 或 `tx_len`；循环模式下 offset 在各自长度范围内回绕。Message
至少有一个非零长度；
订阅某个方向的 HALF 事件时，该方向 buffer 必须能分成两个包含完整 frame 的等长半区，否则
backend 返回
`-MR_EINVAL`。

同一 Controller 同时只能有一个活动 stream。Stream 活动期间，调用方必须保持 message
及其 TX/RX
buffer 有效，并且不能调用会访问或重新配置同一 Controller 的其他 SPI 操作。`stop`
成功返回后，backend
必须保证不再访问这些对象。硬件 backend 可以使用 DMA 或中断实现 stream；不支持时返回
`-MR_ENOSYS`。

Master 非循环 stream 的 `MR_SPI_WAIT_STREAM_DONE` 表示最后一个 frame 已经移出并且
CS 已释放，不能把
`MR_SPI_WAIT_TX_STREAM_FULL` 或 TX DMA 完成直接视为事务完成。Master 循环 stream
在运行期间保持 CS
有效，方向 FULL 事件仅表示对应 buffer 已经回绕，不表示 stream 已停止。Slave
的外部事务边界仍由
SELECT/DESELECT 事件表示。

TX/RX 长度和进度事件互相独立，不要求两个方向同时跨过 buffer 边界。RX 事件上报前，对应区域必须对
CPU
可见；TX 事件上报前，对应区域必须已经不再被 DMA 读取。

### 5. Slave 与事件

SPI Slave 不存在 I2C 那样的地址和读写方向阶段；MOSI 与 MISO 会在同一时钟下同时移位。因此公共层提供按
CS 索引访问的 RX/TX frame 队列：

```c
mr_ssize_t mr_spi_slave_read(mr_driver_t *spi, mr_uint32_t cs,
                             void *buf, mr_size_t size);
mr_ssize_t mr_spi_slave_write(mr_driver_t *spi, mr_uint32_t cs,
                              const void *buf, mr_size_t size);
```

`size` 是字节数，且必须按 `data_bits` 对应的 buffer 元素宽度对齐。`slave_write`
把后续需要移出的
frame 放入 backend TX 队列，`slave_read` 从 backend RX 队列取出已收到的
frame；成功时均返回实际处理的
字节数。

可等待事件如下：

| 事件                         | 含义                                    |
|------------------------------|-----------------------------------------|
| `MR_SPI_WAIT_SELECT`         | CS 进入有效状态，Slave 事务开始         |
| `MR_SPI_WAIT_RX_READY`       | RX 队列已有接收 frame                   |
| `MR_SPI_WAIT_TX_READY`       | TX 队列可以接受更多待发送 frame         |
| `MR_SPI_WAIT_DESELECT`       | CS 退出有效状态，Slave 事务结束         |
| `MR_SPI_WAIT_ERROR`          | 溢出、欠载、模式错误或 backend 硬件错误 |
| `MR_SPI_WAIT_RX_STREAM_HALF` | RX stream 前半区已经完成                |
| `MR_SPI_WAIT_RX_STREAM_FULL` | RX stream buffer 已完成                 |
| `MR_SPI_WAIT_TX_STREAM_HALF` | TX stream 前半区已经完成                |
| `MR_SPI_WAIT_TX_STREAM_FULL` | TX stream buffer 已完成                 |
| `MR_SPI_WAIT_STREAM_ERROR`   | 任一数据路径出错，整个 stream 失败      |
| `MR_SPI_WAIT_STREAM_DONE`    | 非循环 SPI 事务已经完全结束             |

使用 `mr_spi_event_attach` 将 `mr_waiter_t` 挂到 Controller，使用
`mr_spi_event_enable/disable(spi, cs, events)` 控制某个 CS 的底层通知。Backend 通过
`mr_spi_event_raise(spi, events, &data)` 上报，其中 `data` 是调用方提供的
`mr_spi_event_data_t`，`cs` 标识事件来源。事件分发是同步的，`data` 只需在该调用返回前保持有效；
callback 收到的 `data` 指针不能保存。

### 6. 并发与事务边界

当前公共层不提供总线互斥或跨调用保持 CS 的接口。调用方必须保证同一 Controller
不被并发访问；活动 stream
独占整个 Controller，而不只是启动时选择的 CS。

Backend 必须在每次 transfer 中重新应用 `config[cs]`，不能假定前一次传输留下的
Controller 配置
仍属于当前 CS。

### 7. 生命周期与 API

| API                           | 说明                                  |
|-------------------------------|---------------------------------------|
| `MR_SPI_DEFINE`               | 定义静态 SPI driver                   |
| `MR_SPI_INSTALL`              | 定义并安装静态 SPI driver             |
| `mr_spi_init/create/del`      | 初始化、创建和删除 SPI driver         |
| `mr_spi_configure`            | 配置一个 CS 索引                      |
| `mr_spi_configuration`        | 读取一个 CS 索引的配置                |
| `mr_spi_transfer`             | 在一个 CS 下传输 message 数组         |
| `mr_spi_stream_start/stop`    | 启停单 message 异步 stream            |
| `mr_spi_stream_is_busy`       | 查询整个 stream 状态                  |
| `mr_spi_rx/tx_stream_offset`  | 分别查询 RX/TX stream 字节进度        |
| `mr_spi_slave_read/write`     | 访问一个 Slave CS 的 RX/TX frame 队列 |
| `mr_spi_event_enable/disable` | 控制一个 CS 的底层事件                |
| `mr_spi_event_attach/raise`   | 挂接 waiter / 由 backend 上报事件     |
| `mr_spi_get/put`              | 管理 driver 引用                      |
| `MR_HT_SPI_INSTALL`           | 通过 HardTree 安装 SPI Controller     |
| `mr_ht_spi_*`                 | 通过 `{drv, cs}` spec 调用 SPI API    |
| `MR_HT_*SPI_SPEC*`            | 从 `[phandle, cs]` 属性生成 SPI spec  |

`mr_ht_spi_stream_stop/is_busy/rx_stream_offset/tx_stream_offset` 和
`mr_ht_spi_event_attach` 虽然接受 spec，但操作作用于 spec 引用的整个
Controller。活动 stream 独占
Controller；event waiter 也可能收到其他 CS 的事件，应通过 callback 的
`mr_spi_event_data_t::cs`
判断来源。

## 开发者指南

### 8. Backend 操作表

`configure` 和 `configuration` 是必需 callback，公共 API 会用 `MR_ASSERT` 检查。
`transfer`、stream、
`slave_read`、`slave_write` 和 `event_control` 是可选能力，缺失时相应 API 返回
`-MR_ENOSYS`。

Backend 负责：

1. 验证 `cs` 范围和配置能力。
2. 按 CS 索引保存配置。
3. transfer 开始前应用该配置。
4. 使能 CS，顺序传输所有 message，最后释放 CS。
5. 发生错误时仍尽力还原 SCK 和 CS。

Controller backend 的详细契约见 [SPI 驱动移植指南](porting.md)。

### 9. 源码导读

| 文件                               | 职责                          |
|------------------------------------|-------------------------------|
| `include/mrX/drivers/spi/mr_spi.h` | 配置、message、ops 与公共 API |
| `drivers/spi/spi.c`                | API 边界与 clazz 生命周期     |
| `drivers/spi/spi_gpio.c`           | GPIO 软件 Master backend      |
| `include/mrX/drivers/spi/Kconfig`  | SPI 与可选 stream 配置        |


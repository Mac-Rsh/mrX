# SPI 驱动移植指南

> 本文说明如何实现 SPI Controller backend。公共类型和 API 见 [SPI](spi.md)。

## 1. 目录与分层

平台硬件 backend 建议放在：

```text
mrX/arch/<vendor>/<target>/platform/drivers/spi/
```

公共层只定义 Controller 生命周期和调用边界。Backend 拥有 CS 资源表、每个 CS 的配置和底层硬件状态。

## 2. Ops 契约

```c
typedef struct mr_spi_ops {
    mr_err_t (*configure)(const mr_driver_t *drv, mr_uint32_t cs,
                          const mr_spi_config_t *config);
    mr_err_t (*configuration)(const mr_driver_t *drv, mr_uint32_t cs,
                              mr_spi_config_t *config);
    mr_ssize_t (*transfer)(const mr_driver_t *drv, mr_uint32_t cs,
                           const mr_spi_msg_t *msgs, mr_size_t num);
#if defined(MR_USE_DRIVER_SPI_STREAM)
    mr_err_t (*stream_start)(const mr_driver_t *drv, mr_uint32_t cs,
                             const mr_spi_msg_t *msg,
                             const mr_spi_stream_config_t *config);
    mr_err_t (*stream_stop)(const mr_driver_t *drv);
    mr_err_t (*stream_is_busy)(const mr_driver_t *drv, mr_bool_t *busy);
    mr_err_t (*rx_stream_offset)(const mr_driver_t *drv, mr_size_t *offset);
    mr_err_t (*tx_stream_offset)(const mr_driver_t *drv, mr_size_t *offset);
#endif
    mr_ssize_t (*slave_read)(const mr_driver_t *drv, mr_uint32_t cs,
                             void *buf, mr_size_t size);
    mr_ssize_t (*slave_write)(const mr_driver_t *drv, mr_uint32_t cs,
                              const void *buf, mr_size_t size);
    mr_err_t (*event_control)(const mr_driver_t *drv, mr_uint32_t cs,
                              mr_uint32_t events, mr_bool_t enable);
} mr_spi_ops_t;
```

成员签名要点与必需性一览（完整声明以 `<mrX/drivers/spi/mr_spi.h>` 为准，首参均为
`const mr_driver_t *drv`）：

| ops 成员           | 签名要点                                                                   | 必需性 | 缺失时公共层行为                                     |
|--------------------|----------------------------------------------------------------------------|--------|------------------------------------------------------|
| `configure`        | `(drv, cs, const mr_spi_config_t *config)`                                 | 必需   | `mr_spi_configure` 的 `MR_ASSERT` 失败，不返回错误码 |
| `configuration`    | `(drv, cs, mr_spi_config_t *config)`                                       | 必需   | `mr_spi_configuration` 的 `MR_ASSERT` 失败           |
| `transfer`         | `(drv, cs, const mr_spi_msg_t *msgs, mr_size_t num)` → 完成消息数          | 可选   | `-MR_ENOSYS`                                         |
| `stream_start`     | `(drv, cs, const mr_spi_msg_t *msg, const mr_spi_stream_config_t *config)` | 可选   | `-MR_ENOSYS`                                         |
| `stream_stop`      | `(drv)`                                                                    | 可选   | `-MR_ENOSYS`                                         |
| `stream_is_busy`   | `(drv, mr_bool_t *busy)`                                                   | 可选   | `-MR_ENOSYS`                                         |
| `rx_stream_offset` | `(drv, mr_size_t *offset)`                                                 | 可选   | `-MR_ENOSYS`                                         |
| `tx_stream_offset` | `(drv, mr_size_t *offset)`                                                 | 可选   | `-MR_ENOSYS`                                         |
| `slave_read`       | `(drv, cs, void *buf, mr_size_t size)` → 实际字节数                        | 可选   | `-MR_ENOSYS`                                         |
| `slave_write`      | `(drv, cs, const void *buf, mr_size_t size)` → 实际字节数                  | 可选   | `-MR_ENOSYS`                                         |
| `event_control`    | `(drv, cs, mr_uint32_t events, mr_bool_t enable)`                          | 可选   | `-MR_ENOSYS`                                         |

`configure` 与 `configuration` 是基础能力，公共 API 在断言开启时会要求它们非空。`transfer`、
`slave_read`、`slave_write`、`event_control` 以及整个 stream 成员集都是可选能力：置
`MR_NULL` 时对应公共 API 返回 `-MR_ENOSYS`。Master 与 Slave 是相互独立的能力，只做 Master 的
backend 不必伪造 Slave 通路。

### configure

- 无效 CS 索引返回 `-MR_EINVAL`。
- 对无法实现的时钟、位宽、mode 或位序返回负错误码。
- 对无法实现的 Master/Slave 角色返回 `-MR_ENOSYS`。
- 配置应按 CS 索引保存，不应只保存一份 Controller 全局配置。
- `clock_rate == 0` 可用于禁用该 CS；后续 transfer 应返回 `-MR_ENODEV`。

### configuration

- 返回指定 CS 当前保存的配置。
- 如果硬件只能近似请求频率，建议返回实际配置值。

### transfer

- 传输前重新应用 `config[cs]`。
- 在第一个 frame 前使能 CS，所有 message 完成后释放 CS。
- message 之间不释放 CS。
- `msg.tx_len` 和 `msg.rx_len` 是两个方向各自的字节数；都必须按 `data_bits` 对应的 1、2 或 4 字节
  buffer 元素宽度对齐，否则返回 `-MR_EINVAL`。
- 每个 message 产生 `max(tx_len, rx_len)` 字节对应的总线 frame。TX buffer 为空或先用尽时发送全 1
  dummy frame；RX buffer 为空或先用尽时丢弃接收数据。
- 成功返回完成的 message 数，失败返回负错误码。
- 任何失败路径都应尽力还原 SCK 空闲电平并释放 CS。

### stream

- 一次 `stream_start` 只接受一个 message，不支持 message 数组。
- `config.circular` 选择一次性或循环传输。Message 至少有一个非零方向；循环模式下 TX/RX buffer 分别
  按照自己的长度独立回绕。
- `start` 返回 0 表示异步操作已经启动。TX/RX 方向分别上报 `*_STREAM_HALF` 和 `*_STREAM_FULL`；任一
  数据路径出错都上报 `MR_SPI_WAIT_STREAM_ERROR` 并使整个 stream 失败；非循环事务在外设真正结束后额外
  上报 `MR_SPI_WAIT_STREAM_DONE`。
- 同一 Controller 同时只允许一个活动 stream。活动期间会访问或重新配置该 Controller 的其他操作应返回
  `-MR_EBUSY`。
- `rx_stream_offset` 和 `tx_stream_offset` 分别返回两个方向已经完成的字节数；一次性传输完成时等于对应
  长度，循环传输在各自长度范围内回绕。
- Master 非循环传输只有在 RX/TX 数据路径完成、最后一个 frame 已移出且 CS 已释放后才能上报
  `MR_SPI_WAIT_STREAM_DONE`。方向 FULL 或 DMA 完成不能替代该事件。
- Master 循环传输在运行期间保持 CS 有效；方向 FULL 表示对应 buffer 回绕，不表示 stream 已停止。
- Slave 的 SELECT/DESELECT 仍独立表示外部事务边界。
- TX/RX HALF/FULL 独立上报，不等待另一个方向跨过边界。上报前应完成对应 RX 区域的 cache 一致性处理，
  并保证 DMA 不再访问对应 TX 区域。
- `stream_stop` 成功返回前必须停止外设请求和 DMA/中断，并保证不再访问 message 或 buffer。停止失败时应保留
  资源所有权，使调用方可以重试。
- Message 及其 TX/RX buffer 必须保持有效，直到 stream 完成或者 `stream_stop` 成功。

### slave_read / slave_write

- 两个操作都按 `cs` 访问 backend 管理的逻辑 Slave endpoint。
- `size` 是字节数，必须按该 CS 的 `data_bits` 对应的 buffer 元素宽度对齐。
- `slave_read` 从接收队列取走 frame，`slave_write` 向发送队列追加 frame。
- 成功返回实际读取或写入的字节数。
- 如果硬件只支持一个 Slave NSS，使用 CS 索引 0，并对其他索引返回 `-MR_EINVAL`。
- 队列暂时无数据或无空间时，backend 可按自身非阻塞约定返回 0 或 `-MR_EAGAIN`，但必须在移植文档中说明。

### event_control 与事件上报

- `event_control(drv, cs, events, enable)` 负责开关指定 CS 对应的中断、DMA 或通知源。
- backend 填充 `mr_spi_event_data_t`，再使用
  `mr_spi_event_raise(drv, events, &data)` 上报 `SELECT`、`RX_READY`、`TX_READY`、`DESELECT`、
  `ERROR`、方向 `STREAM_HALF/FULL`、整体 `STREAM_ERROR` 和 `STREAM_DONE`。
- waiter callback 的 `data` 是 `mr_spi_event_data_t *`，其 `cs` 可用于区分共享 Controller
  下的 endpoint。事件分发是同步的，调用方提供的数据只需在 `mr_spi_event_raise` 返回前保持有效；
  callback 不能保存该指针。

## 3. CS 索引和 HardTree

CS 映射是 backend 契约，不是公共 SPI 对象。GPIO CS backend 可在 HardTree 中声明 spec 数组：

```dts
spi0: spi {
    compatible = "spi-gpio";
    sck = <&gpioa 5>;
    mosi = <&gpioa 7>;
    miso = <&gpioa 6>;
    cs = <<&gpioa 4> <&gpiob 0>>;
    status = "okay";
};
```

`cs = <<...> <...>>` 的每个子数组都是 `[phandle, pin]`，可用 `MR_HT_GPIO_SPEC_FOREACH(_id, cs)`
生成 `mr_ht_gpio_spec_t` 数组。`cs_index` 直接作为该数组下标。

GPIO SPI backend 按标准 SPI 语义将 CS 视为低电平有效；如果某个片选需要反相，应通过对应 GPIO
控制器的 `invert_mask` 配置完成，不在 SPI backend 中单独维护极性。

对于硬件 NSS，backend 可将同一索引映射为 NSS 通道；公共 API 无需区分 GPIO 和硬件片选。

消费 SPI 的设备可使用 `[phandle, cs]` 属性引用一个逻辑 endpoint：

```dts
sensor0: sensor {
    spi = <&spi0 1>;
};
```

`MR_HT_NODE_SPI_SPEC(sensor0, spi)` 将该属性展开为 `mr_ht_spi_spec_t {drv, cs}`。也可使用
`MR_HT_NODE_SPI_SPEC_FOREACH`、`MR_HT_SPI_SPEC` 和 `MR_HT_SPI_SPEC_FOREACH` 处理节点或驱动
属性。这些 spec 宏描述消费方使用的逻辑 endpoint，不解析 Controller 自身用于建立物理资源表的
`cs` 属性。

## 4. GPIO 软件 Master

`drivers/spi/spi_gpio.c` 是本驱动唯一的树内实现。它的编译条件是
`MR_USE_DRIVER_SPI && MR_USE_DRIVER_GPIO`，并且只有硬件模板里存在 compatible 为
`spi_gpio` 的实例时才会生成代码；它 **没有**独立的 Kconfig 选项。

该 backend 使用 `spi_gpio` compatible，属性如下：

| 属性   | 必需 | 说明                     |
|--------|------|--------------------------|
| `sck`  | 是   | 时钟 GPIO spec           |
| `mosi` | 是   | Master 输出 GPIO spec    |
| `miso` | 否   | Master 输入 GPIO spec    |
| `cs`   | 是   | 一个或多个片选 GPIO spec |

该 backend 支持：

- SPI mode 0～3；
- MSB-first 和 LSB-first；
- 1～32 bit frame（`clock_rate` 非 0 时 `data_bits` 为 0 或大于 32 返回 `-MR_EINVAL`）；
- 全双工、仅发送、仅接收和只产生时钟；
- 最高 500 kHz 请求频率（超过返回 `-MR_ENOSYS`）。

该 backend 只支持 `MR_SPI_ROLE_MASTER`。GPIO 调用链没有保证 Slave 所需边沿响应延迟，配置
`MR_SPI_ROLE_SLAVE` 返回 `-MR_ENOSYS`；Slave 应由带 NSS/SCK 中断、FIFO 或 DMA 的硬件 backend 实现。
`slave_read`/`slave_write`/`event_control` 与整个 stream 成员集都未实现，公共层据此返回
`-MR_ENOSYS`。

请求接收数据（`rx_buf` 非空且 `rx_len` 非 0）但没有配置 `miso` 属性时返回 `-MR_ENOSYS`；
`tx_len`/`rx_len` 不是 `data_bits` 对应元素宽度的整数倍时返回 `-MR_EINVAL`。

`clock_rate = 0` 用于禁用该 `cs`（后续 transfer 返回 `-MR_ENODEV`），并且此时
`data_bits` 与 `role` 的校验会被跳过。

GPIO 调用、软件循环和 `mr_clock_busy_wait` 会引入额外开销，实际时钟低于请求值是正常现象。高速或时序
敏感设备应使用平台硬件 backend。

## 5. 并发、中断和 DMA

Master `transfer` 是同步接口，不提供总线锁、abort 或跨调用 CS 所有权。调用方必须串行化同一 Controller
的 configure/configuration/transfer。活动 stream 独占整个 Controller；其 start/stop/busy 和方向 offset
生命周期由 backend 管理。Slave backend 可通过公共 wait/event 接口异步报告 CS 边界、队列状态和错误，但 RX/TX 队列
的容量、溢出与欠载策略由 backend 决定。

硬件 backend 可在 `transfer` 内部使用 polling、IRQ 或 DMA，但必须在完成整个同步事务后才返回。Stream 可由
DMA 或中断实现；DMA 完成只表示搬运结束，Master 仍需等待外设的最终发送完成条件后释放 CS 和上报 DONE。

## 6. GPIO 软件 backend 能力边界（新 port 不要默认继承）

| 能力/约定        | `spi_gpio` 现状                                                                               | 新 port 需要确认                            |
|------------------|-----------------------------------------------------------------------------------------------|---------------------------------------------|
| 角色             | 只支持 `MR_SPI_ROLE_MASTER`；Slave 返回 `-MR_ENOSYS`                                          | 是否有 NSS/SCK 中断、FIFO 或 DMA 支撑 Slave |
| 最高频率         | 请求频率 > 500 kHz 返回 `-MR_ENOSYS`；实际频率受 GPIO 调用开销限制                            | 硬件可达频率与分频粒度                      |
| 帧宽             | 1～32 bit；`clock_rate` 非 0 时 `data_bits` 为 0 或 > 32 返回 `-MR_EINVAL`                    | 支持的帧宽及其与 buffer 元素宽度的关系      |
| 接收能力         | 未配置 `miso` 时任何接收请求返回 `-MR_ENOSYS`                                                 | MISO 是否可选、是否支持仅发送               |
| `cs` 数量        | 由 `cs` 属性数组长度决定，配置按 `cs` 下标独立保存                                            | 片选数量上限、是否使用硬件 NSS              |
| stream           | 整个 stream 成员集未实现，全部返回 `-MR_ENOSYS`                                               | 是否需要异步/DMA 传输                       |
| slave_read/write | 未实现，返回 `-MR_ENOSYS`                                                                     | 是否需要 Slave 帧队列                       |
| 事件             | `event_control` 未实现                                                                        | 需要哪些事件位与上报时机                    |
| 禁用语义         | `clock_rate = 0` 禁用该 `cs`，后续 transfer 返回 `-MR_ENODEV`，并跳过 `data_bits`/`role` 校验 | 是否采用同样的“0 即禁用”约定                |

任何一条“`spi_gpio` 支持/不支持”都不能当作通用结论写进硬件 backend 的文档。

## 7. 移植检查清单

- 每个 CS 的配置能独立保存并在 transfer 时重新应用。
- Master/Slave 能力与不支持时的 `-MR_ENOSYS` 行为明确。
- mode 0～3 的 CPOL/CPHA 时序正确。
- MSB/LSB 位序和 data_bits 对应的 buffer 步长正确。
- TX/RX 空指针语义与公共契约一致。
- message 边界不释放 CS，事务结束后释放 CS。
- Stream 一次只处理一个 message，支持一次性和循环传输，TX/RX offset 使用字节单位。
- TX/RX buffer 长度独立，循环模式下分别回绕。
- 订阅 HALF 的方向可以划分为两个 frame 对齐的等长半区。
- TX/RX HALF/FULL 分别上报，不要求两个方向的 buffer 边界重合。
- 整体 STREAM_DONE 不早于外设最终发送完成和 Master CS 释放。
- Stream stop 成功后不会继续访问 message 或 buffer。
- 失败路径不会将 CS 长期留在有效状态。
- 无效 CS 返回 `-MR_EINVAL`，未配置或已禁用 CS 返回 `-MR_ENODEV`。
- HardTree 实例能通过 `MR_HT_SPI_INSTALL` 正确安装。
- Slave backend 能按 CS 管理 RX/TX frame 队列，并在 callback payload 中提供正确的 CS 索引。


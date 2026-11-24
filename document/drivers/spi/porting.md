# SPI 驱动移植指南

面向平台 backend 开发者，说明如何为 SPI 控制器实现 `mr_spi_ops_t`
。公共类型、帧对齐规则与事件定义见 [SPI](spi.md)。

## 1. 移植边界

| 职责                                                         | 公共层 | Backend |
|--------------------------------------------------------------|--------|---------|
| 类型校验、`-MR_ENOSYS` 降级与 `mr_spi_data_t.event` 生命周期 | 负责   | —       |
| 按 `cs` 索引保存配置与校验 `cs` 范围                         | —      | 负责    |
| 每次传输前重应用 `config[cs]` 及片选（GPIO/硬件 NSS）控制    | —      | 负责    |
| 同步 `transfer`、单消息异步 `stream` 与 Slave 帧队列         | —      | 负责    |
| 硬件中断/DMA 处理与 `mr_spi_event_raise` 上报                | —      | 负责    |

平台实现通常位于 `mrX/arch/<vendor>/<target>/platform/drivers/spi/`；树内 GPIO 软件
Master 参考 `drivers/spi/spi_gpio.c`（仅支持 `MR_SPI_ROLE_MASTER`、最高 500 kHz，无
stream/Slave/事件）。

## 2. 操作表（ops）

backend 需实现 `<mrX/drivers/spi/mr_spi.h>` 中的 `mr_spi_ops_t`：

| 回调               | 签名要点                                                                   | 必需                                | 缺失返回值   |
|--------------------|----------------------------------------------------------------------------|-------------------------------------|--------------|
| `configure`        | `(drv, cs, const mr_spi_config_t *config)`                                 | 是                                  | 断言拦截     |
| `configuration`    | `(drv, cs, mr_spi_config_t *config)`                                       | 是                                  | 断言拦截     |
| `transfer`         | `(drv, cs, const mr_spi_msg_t *msgs, mr_size_t num)`，返回完成的消息数     | 否                                  | `-MR_ENOSYS` |
| `stream_start`     | `(drv, cs, const mr_spi_msg_t *msg, const mr_spi_stream_config_t *config)` | 否（需 `MR_USE_DRIVER_SPI_STREAM`） | `-MR_ENOSYS` |
| `stream_stop`      | `(drv)`                                                                    | 否（需 `MR_USE_DRIVER_SPI_STREAM`） | `-MR_ENOSYS` |
| `stream_is_busy`   | `(drv, mr_bool_t *busy)`                                                   | 否（需 `MR_USE_DRIVER_SPI_STREAM`） | `-MR_ENOSYS` |
| `rx_stream_offset` | `(drv, mr_size_t *offset)`                                                 | 否（需 `MR_USE_DRIVER_SPI_STREAM`） | `-MR_ENOSYS` |
| `tx_stream_offset` | `(drv, mr_size_t *offset)`                                                 | 否（需 `MR_USE_DRIVER_SPI_STREAM`） | `-MR_ENOSYS` |
| `slave_read`       | `(drv, cs, void *buf, mr_size_t size)`，返回实际字节数                     | 否                                  | `-MR_ENOSYS` |
| `slave_write`      | `(drv, cs, const void *buf, mr_size_t size)`，返回实际字节数               | 否                                  | `-MR_ENOSYS` |
| `event_control`    | `(drv, cs, mr_uint32_t events, mr_bool_t enable)`                          | 否                                  | `-MR_ENOSYS` |

## 3. 关键契约

- **按 CS 保存与应用配置**：无效 `cs` 返回 `-MR_EINVAL`；不支持的 `role` 返回
  `-MR_ENOSYS`；`clock_rate == 0` 禁用该 `cs`（后续传输返回 `-MR_ENODEV`）。每次
  `transfer` 开始前必须重新应用 `config[cs]`。
- **同步 `transfer`**：第一个帧前使能 CS，消息之间保持 CS 有效，全部完成后释放
  CS；出错时须还原 SCK 空闲电平并释放 CS。`tx_len`/`rx_len` 须按 `data_bits` 对应的
  1/2/4 字节元素宽度对齐，否则返回 `-MR_EINVAL`。
- **单消息 `stream`**：
    - 同一控制器同时仅允许一个活动 stream，期间对控制器的其他访问或重配应返回
      `-MR_EBUSY`。
    - RX/TX HALF/FULL 相互独立上报（订阅 HALF 的方向须能划分为两个帧对齐的等长半区，否则返回
      `-MR_EINVAL`）。上报 RX 事件前须完成 cache 一致性处理。
    - Master 非循环 stream 必须在最后一个帧移出且 CS 释放后才能上报
      `MR_SPI_WAIT_STREAM_DONE`（不可用 TX DMA 完成替代）。
    - `stream_stop` 返回 0 前必须停止硬件传输与 DMA/中断，确保不再访问 `msg`
      与缓冲区。
- **Slave 与事件**：`slave_read`/`slave_write` 按 `cs` 操作帧队列（仅支持单硬件
  NSS 时使用 `cs = 0`，其他返回 `-MR_EINVAL`）。上报事件时填充
  `mr_spi_event_data_t`（含 `cs`）并调用 `mr_spi_event_raise`。

## 4. HardTree 安装

使用 `MR_HT_SPI_INSTALL(_id, _priv, _ops)` 安装整颗控制器（自动定义零初始化
`mr_spi_data_t`）。物理片选列表（如 `cs = <<&gpio0 4> <&gpio1 0>>`
）由控制器自身属性定义，消费端则通过 `[phandle, cs]` 属性配合 `MR_HT_SPI_SPEC`
引用逻辑片选。树内 `spi_gpio` 兼容驱动使用 `sck`、`mosi`、可选 `miso` 及 `cs` 属性。

## 5. 验收要点

- 每个 `cs` 配置独立保存与回读；无效 `cs` 返回 `-MR_EINVAL`，禁用 `cs` 返回
  `-MR_ENODEV`。
- `MR_SPI_MODE_0`～`3`、`MSB`/`LSB`、1/2/4 字节对齐、空 TX/RX 缓冲区语义及连续消息保持
  CS 行为正确。
- 若启用 stream：单消息启动、独立 TX/RX offset 与回绕、HALF 对齐校验、`STREAM_DONE`
  在 CS 释放后触发、`stream_stop` 安全释放缓冲区。
- 错误路径下 SCK 与 CS 正确复位；Slave 模式事件 `cs` 来源与帧队列行为符合平台文档。

# ETH 驱动移植指南

本文面向平台与外设驱动开发者，说明如何实现 `mr_eth_ops_t` 并接入 ETH 驱动框架。公共
API 与使用方式见 [ETH 驱动](eth.md)。

## 移植边界

| 职责                                                                               | 归属       |
|------------------------------------------------------------------------------------|------------|
| 设备类型校验、MAC（非零单播）/MTU（1~1500）/枚举合法性校验                         | ETH 公共层 |
| `mr_eth_data_t.config` 缓存、`stats` 统计维护与 `event`（`mr_waitable_t`）生命周期 | ETH 公共层 |
| 直接收发与流式提交的帧长边界检查（`-MR_ERANGE`）                                   | ETH 公共层 |
| 可选流回调缺失时的 `-MR_ENOSYS` 返回                                               | ETH 公共层 |
| MAC、PHY 协商、时钟、引脚、IRQ 与 DMA 描述符环管理                                 | Backend    |
| 直接收发数据搬运、padding/FCS 处理及在途帧所有权同步                               | Backend    |
| DMA Cache 一致性维护与完成/取消事件的精确一次归还（`mr_eth_event_raise`）          | Backend    |

控制器自带专用 DMA 时由 ETH backend 直接管理，无需依赖通用 `MR_USE_DRIVER_DMA`。

## 操作表（ops）

### 必需回调

| 成员        | 原型要点                                               | 约定                                                                                                          |
|-------------|--------------------------------------------------------|---------------------------------------------------------------------------------------------------------------|
| `configure` | `(drv, const mr_eth_config_t *config) -> mr_err_t`     | 应用 MAC、MTU、速率、双工、回环、流控与校验和卸载；`config->speed == MR_ETH_SPEED_OFF` 时关闭端口并归还在途帧 |
| `link`      | `(drv, mr_eth_link_t *link) -> mr_err_t`               | 填充 `up`、实际 `speed`/`duplex`/`flow`（`stats` 由公共层填充，无需修改）                                     |
| `read`      | `(drv, void *buf, mr_size_t size) -> mr_ssize_t`       | 读取一帧（含 padding、不含 FCS）；无帧返回 `0`，缓冲区不足返回 `-MR_ERANGE`                                   |
| `write`     | `(drv, const void *buf, mr_size_t size) -> mr_ssize_t` | 发送一帧（硬件/后端补齐 padding 与 FCS）；返回时调用者缓冲区必须可复用                                        |

### 可选流回调（`MR_USE_DRIVER_ETH_STREAM`）

未实现的成员置 `MR_NULL`，公共层返回 `-MR_ENOSYS`。

| 成员                                      | 原型要点                                   | 约定                                                                                                                      |
|-------------------------------------------|--------------------------------------------|---------------------------------------------------------------------------------------------------------------------------|
| `rx_stream_start` / `tx_stream_start`     | `(drv) -> mr_err_t`                        | 启用对应方向流（允许无初始帧）；重复启动返回 `-MR_EBUSY`                                                                  |
| `rx_stream_stop` / `tx_stream_stop`       | `(drv) -> mr_err_t`                        | 停止对应方向流并归还该方向所有在途帧；支持重复调用                                                                        |
| `rx_stream_is_busy` / `tx_stream_is_busy` | `(drv, mr_bool_t *busy) -> mr_err_t`       | 返回该方向流是否处于启用状态                                                                                              |
| `rx_stream_submit` / `tx_stream_submit`   | `(drv, mr_eth_frame_t *frame) -> mr_err_t` | 未启动返回 `-MR_EPERM`，资源满返回 `-MR_EAGAIN`，重复提交返回 `-MR_EBUSY`；返回 `0` 时接管 `frame` 与 `frame->buf` 所有权 |

## 关键契约

- **所有权与精确一次归还**：仅当 `*_stream_submit` 返回 `0` 时，backend 才接管
  `frame` 及 `frame->buf`（并原样保留 `frame->owner`）。在正常完成、`*_stream_stop`
  或 `configure(MR_ETH_SPEED_OFF)` 时，每个已提交帧必须且只能通过
  `mr_eth_event_raise` 归还一次，归还前需确保硬件/DMA 已停止访问并完成 D-Cache
  无效化/清理与内存屏障：
    1. 确认 DMA 不再访问描述符与缓冲区，完成 Cache 维护。
    2. 设置 `frame->status`（成功 `0`，硬件错误负值，停流/关机取消 `-MR_EPERM`
       ）与接收长度 `frame->size`。
    3. 解除描述符关联并释放提交锁，再调用
       `mr_eth_event_raise(drv, event, frame)`。
- **Lifecycle 与 Shutdown**：`eth_init` 先初始化 `waitable`，再调用
  `ops->configure`；失败时回滚硬件并由公共层销毁 `waitable`。`eth_deinit` 调用
  `ops->configure(..., MR_ETH_SPEED_OFF)`，若驱动处于 `MR_DRIVER_IS_DYING(drv)`
  状态，后端必须强制停止硬件与 DMA，跳过 Wait 通知直接结束。

## HardTree 安装

使用 `<mrX/drivers/eth/mr_eth.h>` 提供的 `MR_HT_ETH_INSTALL(_id, _priv, _ops)`
宏安装静态实例，该宏从节点读取以下标准属性：

| 属性                  | 必需 | 默认值                 | 说明                                        |
|-----------------------|------|------------------------|---------------------------------------------|
| `mac`                 | 是   | -                      | 6 字节 MAC 地址（如 `[02 00 00 00 00 01]`） |
| `mtu`                 | 否   | `1500`                 | 最大传输单元（1~1500）                      |
| `speed`               | 否   | `MR_ETH_SPEED_AUTO`    | 初始速率枚举                                |
| `duplex`              | 否   | `MR_ETH_DUPLEX_AUTO`   | 初始双工模式枚举                            |
| `loopback`            | 否   | `MR_ETH_LOOPBACK_NONE` | 初始回环模式枚举                            |
| `flow_rx` / `flow_tx` | 否   | `0`                    | 802.3x 流控接收 / 发送使能                  |
| `csum_rx` / `csum_tx` | 否   | `0`                    | 硬件校验和接收校验 / 发送卸载使能           |

## 验收要点

- [ ] `configure` 支持 `MR_ETH_SPEED_OFF` 关口，不支持的速率/回环/卸载组合返回
  `-MR_ENOSYS`。
- [ ] 直接 `read` / `write` 的短帧补齐、FCS 剥离与缓冲区复用语义正确。
- [ ] Stream 提交、完成、错误与 `stop` / `shutdown` 取消路径下，所有帧均精确归还一次，无
  DMA 越界或 Cache 脏数据。

# 以太网驱动（ETH）

ETH 驱动提供面向完整以太网帧（不含 FCS）的收发接口、链路状态查询与流量统计，同时支持轮询式直接收发与基于异步提交归还的
RX/TX 流式（Stream）传输。

| 项目   | 说明                                                                 |
|--------|----------------------------------------------------------------------|
| 功能宏 | `MR_USE_DRIVER_ETH`（流式传输需额外启用 `MR_USE_DRIVER_ETH_STREAM`） |
| 头文件 | `<mrX/drivers/eth/mr_eth.h>`                                         |
| 依赖   | [驱动框架](../driver.md)、[等待队列](../../kernel/wait.md)           |
| 测试   | `test/drivers/eth/eth.c`                                             |

## 配置

| 配置项                     | 类型 | 默认值 | 说明                                                    |
|----------------------------|------|--------|---------------------------------------------------------|
| `MR_USE_DRIVER_ETH`        | bool | n      | 启用 ETH 驱动（依赖 `MR_USE_DRIVER`）                   |
| `MR_USE_DRIVER_ETH_STREAM` | bool | y      | 启用独立 RX/TX 流式提交接口（依赖 `MR_USE_DRIVER_ETH`） |

## 快速上手

```c
#include <mrX/drivers/eth/mr_eth.h>

mr_ssize_t eth_poll_rx(const char *name, void *buf, mr_size_t capacity) {
    mr_driver_t *drv = mr_driver_find(name);
    mr_ssize_t ret;

    if (drv == MR_NULL) {
        return -MR_ENODEV;
    }

    if ((MR_DRIVER_TYPE(drv) & MR_DRIVER_TYPE_MASK) != MR_DRIVER_TYPE_ETH) {
        ret = -MR_EINVAL;
    } else if (!mr_driver_is_ready(drv)) {
        ret = -MR_EPERM;
    } else {
        ret = mr_eth_read(drv, buf, capacity);
    }

    mr_driver_put(drv);
    return ret;
}
```

## 核心机制与约束

- **帧边界与长度约定**：公共层采用 14 字节无 VLAN 标签帧头，数据从目的 MAC
  开始。发送时后端或硬件负责补齐短帧与生成 FCS，接收结果包含 padding 且不含 FCS。
  `mr_eth_read` 要求非零缓冲区容量不少于 `MR_ETH_FRAME_MIN`（60 字节，推荐按
  `max(60, 14 + mtu)` 分配），不足返回 `-MR_ERANGE`；`mr_eth_write` 要求非零帧长在
  `14` 到 `14 + mtu` 之间，越界返回 `-MR_ERANGE`。
- **配置与统计**：`mr_eth_config_t` 中的 `mac` 必须为 6 字节非零单播地址（最低位为
  0），`mtu` 范围为 `1..1500`（`MR_ETH_MTU_MAX`）。`mr_eth_configure` 须在
  `setup` 后调用，立即下发后端并清零 `stats`。
  `mr_eth_link` 返回物理链路通断、实际速率/双工/流控及累计收发统计（`-MR_EAGAIN`、
  `-MR_EBUSY`、`-MR_EPERM` 不计入错误统计）。
- **直接收发 vs Stream 收发**：直接 `mr_eth_read` / `mr_eth_write`
  在调用返回后缓冲区即可复用。开启 `MR_USE_DRIVER_ETH_STREAM` 后，应用先通过
  `mr_eth_event_attach` 订阅事件并启动对应方向流（`rx_stream_start` /
  `tx_stream_start`），再提交 `mr_eth_frame_t`（`rx` 要求
  `capacity >= max(60, 14 + mtu)`，`tx` 要求 `14 <= size <= 14 + mtu` 且
  `size <= capacity`）。提交成功后缓冲区归后端独占，直到在 `mr_eth_event_raise`
  回调中收到原 `mr_eth_frame_t *`（检查 `frame->status == 0`）后方可复用。
- **停机与删除**：`mr_driver_shutdown` 会以 `MR_ETH_SPEED_OFF` 关闭端口并销毁内部
  `mr_waitable_t`；停机前后端会以 `status = -MR_EPERM`
  归还在途帧（不计入错误统计）。若停机失败且驱动未处于删除流程，等待队列与在途帧保留以便重试；删除动态实例前应先完成
  `shutdown` 并等待所有 `mr_driver_put` 归还。
- **并发与中断安全**：Wait 回调在事件发起者上下文（通常为 ISR）同步执行，不可阻塞；配置、启停与
  `shutdown` 应由调用方串行化，避免与在途 I/O 竞态。

## API 速查

### 生命周期与配置

| API                                            | 说明                                                                                       |
|------------------------------------------------|--------------------------------------------------------------------------------------------|
| `MR_ETH_DEFINE` / `MR_ETH_INSTALL`             | 编译期静态定义 / 安装 ETH 驱动                                                             |
| `MR_ETH_DATA_INIT`                             | 初始化 `mr_eth_data_t`（`_mtu`、`_speed`、花括号 MAC 地址）                                |
| `MR_HT_ETH_INSTALL`                            | HardTree 实例安装（读取 `mac` 及可选 `mtu`/`speed`/`duplex`/`loopback`/`flow_*`/`csum_*`） |
| `mr_eth_init` / `mr_eth_create` / `mr_eth_del` | 初始化静态实例 / 动态创建 / 删除 ETH 驱动                                                  |
| `mr_eth_configure` / `mr_eth_configuration`    | 写入 / 回读端口配置（MAC、MTU、速率、双工、回环、流控、校验和卸载）                        |
| `mr_eth_link`                                  | 查询链路通断、协商参数与流量统计                                                           |

### 直接收发

| API            | 说明                                                                                |
|----------------|-------------------------------------------------------------------------------------|
| `mr_eth_read`  | 轮询读取一帧到 `buf`；成功返回帧长，无帧或 `size == 0` 返回 `0`，失败返回负数错误码 |
| `mr_eth_write` | 发送一帧；成功返回接受字节数，`size == 0` 返回 `0`，失败返回负数错误码              |

### Stream 与事件（`MR_USE_DRIVER_ETH_STREAM`）

| API                                              | 说明                                                                                          |
|--------------------------------------------------|-----------------------------------------------------------------------------------------------|
| `mr_eth_rx_stream_start` / `tx_stream_start`     | 启用 RX / TX 流方向                                                                           |
| `mr_eth_rx_stream_stop` / `tx_stream_stop`       | 停止流并归还该方向所有在途帧                                                                  |
| `mr_eth_rx_stream_is_busy` / `tx_stream_is_busy` | 查询流方向是否已启用                                                                          |
| `mr_eth_rx_stream_submit` / `tx_stream_submit`   | 提交空 RX 帧缓冲 / 待发送 TX 帧描述符                                                         |
| `mr_eth_event_attach`                            | 挂接 `mr_waiter_t` 订阅流事件（`MR_ETH_WAIT_RX_READY` / `TX_DONE` / `RX_ERROR` / `TX_ERROR`） |
| `mr_eth_event_raise`                             | 统计流完成/错误计数并同步唤醒 waiter（供后端调用，`data` 为 `mr_eth_frame_t *` 或 `MR_NULL`） |

## 内部实现

`drivers/eth/eth.c` 中的 `eth_init` 先校验配置并初始化 `waitable`，再调用
`ops->configure` 应用初始配置；`eth_deinit` 下发 `MR_ETH_SPEED_OFF` 关闭端口后销毁
`waitable`；`eth_release` 释放动态创建的 `mr_eth_data_t`。公共层在 `mr_eth_read` /
`mr_eth_write` 与 `mr_eth_event_raise` 中统一维护 `mr_eth_data_t.stats`。

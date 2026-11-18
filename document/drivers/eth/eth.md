# 以太网驱动（ETH）

> 通过统一设备接口收发完整的以太网帧，并查询物理链路状态。

| 项目     | 说明                                                   |
|----------|--------------------------------------------------------|
| 功能宏   | `MR_USE_DRIVER_ETH`，可选 `MR_USE_DRIVER_ETH_STREAM`   |
| 头文件   | `<mrX/drivers/eth/mr_eth.h>`                           |
| 依赖模块 | [驱动框架](../driver.md)、[Wait](../../kernel/wait.md) |
| 平台适配 | [ETH 驱动移植指南](porting.md)                         |

---

## 用户指南

### 1. 模块概述

以太网以帧为单位交付。调用者提供的字节从目的 MAC 地址开始，包含源 MAC
地址、类型或长度字段和帧数据。ETH 公共接口采用 14 字节的无 VLAN 标签帧头，MTU
约束帧头之后的有效负载。发送时后端负责补齐短帧和生成帧校验序列（FCS），接收结果包含
padding，排除 FCS。地址解析、IP 配置和协议处理由上层完成。

设备提供两种整帧收发方式。选择的依据是缓冲区何时可以复用。

| 方式                  | 使用时机                    | 缓冲区使用权                                            |
|-----------------------|-----------------------------|---------------------------------------------------------|
| 直接 `read` / `write` | 轮询或逐帧调用              | 调用返回后即可复用缓冲区                                |
| RX / TX Stream        | 为支持 DMA 的后端持续提供帧 | `submit` 成功后由后端使用，完成或取消通知交回后方可复用 |

### 2. 配置与依赖

| 配置                       | 默认值 | 作用                                                                        |
|----------------------------|--------|-----------------------------------------------------------------------------|
| `MR_USE_DRIVER_ETH`        | `n`    | 启用 ETH 公共驱动，依赖 `MR_USE_DRIVER`，驱动框架选中 `MR_USE_WAIT`         |
| `MR_USE_DRIVER_ETH_STREAM` | `y`    | 在启用 ETH 时编译 Stream API 和对应操作表成员。每个方向的实际能力由后端提供 |

应用还需要目标平台注册的 ETH 实例。MAC、PHY、GPIO、DMA 内存与中断等资源由目标 backend 负责，
平台能力和接线均应参阅该平台自己的文档；公共 API 及移植约束见[移植指南](porting.md)。
配置头由项目的 [Kconfig 工具](../../scripts/kconfig/kconfig.md)生成。

### 3. 快速上手与示例

1. 启用 ETH 并按平台流程注册实例。自动启动的实例使用注册时的配置，由
   `mr_user_init()` 执行 setup。
2. 对延迟启动的实例，先获取设备，按需用 `mr_eth_configure()` 设置 MAC、MTU、链路速率、双工、回环、流控和校验和卸载，
   再调用 `mr_driver_setup()` 应用配置。已 setup 的实例可直接调用 `mr_eth_configure()` 应用新配置。
3. 用 `mr_eth_link()` 查询物理链路。准备容量至少为 `max(60, 14 + mtu)` 的 RX
   缓冲区，然后轮询 `mr_eth_read()`，或构造整帧并调用 `mr_eth_write()`。
4. 调用 `mr_driver_find()` 取得的引用用 `mr_driver_put()` 归还。设备的使用者停止
   I/O 后，由负责设备生命周期的一方执行 shutdown。

下面的函数轮询一次已 setup 的设备。`name` 是平台注册的非空设备名，`buf` 和
`capacity` 由调用者提供。返回正值时，`buf` 包含一帧。示例由调用方负责设备启停、调用顺序和缓冲区分配。

```c
#include <mrX/drivers/eth/mr_eth.h>

mr_ssize_t eth_receive_once(const char *name, void *buf, mr_size_t capacity) {
    mr_driver_t *drv;
    mr_ssize_t result;

    drv = mr_driver_find(name);
    if (drv == MR_NULL) {
        return -MR_ENODEV;
    }

    if ((MR_DRIVER_TYPE(drv) & MR_DRIVER_TYPE_MASK) != MR_DRIVER_TYPE_ETH) {
        result = -MR_EINVAL;
    } else if (!mr_driver_is_ready(drv)) {
        result = -MR_EPERM;
    } else {
        result = mr_eth_read(drv, buf, capacity);
    }

    mr_driver_put(drv);
    return result;
}
```

### 4. 使用说明

#### 帧长和配置

`mr_eth_config_t.mac` 是六字节非零单播地址，`mtu` 取 1 到 `MR_ETH_MTU_MAX`
（1500）。`speed`、`duplex` 和 `loopback` 在配置结构中使用紧凑位域，取值分别对应 `mr_eth_speed_t`、`mr_eth_duplex_t` 和 `mr_eth_loopback_t`。`loopback` 可选 NONE/MAC/PHY；`flow.rx/tx` 分别控制 IEEE 802.3x Pause 的接收/发送；`csum.rx/tx` 分别控制硬件校验和检查/生成。默认自动双工、关闭回环，并关闭流控和校验和卸载。每次成功配置后统计清零，失败时不清零。

| 值 | 枚举 | 含义 |
|---:|------|------|
| 0 | `MR_ETH_SPEED_OFF` | 关闭端口 |
| 1 | `MR_ETH_SPEED_AUTO` | 自动协商 |
| 2 | `MR_ETH_SPEED_10M` | 10 Mbit/s |
| 3 | `MR_ETH_SPEED_100M` | 100 Mbit/s |
| 4 | `MR_ETH_SPEED_1000M` | 1000 Mbit/s |
| 5 | `MR_ETH_SPEED_2_5G` | 2.5 Gbit/s |

回环模式中，`MR_ETH_LOOPBACK_MAC` 在 MAC 内部回送数据，`MR_ETH_LOOPBACK_PHY` 使用 PHY 本地回环。平台可以限制回环支持的速率或模式；STM32 后端当前要求回环时使用固定 10/100 Mbit/s 速率。回环启用时 `link.up` 表示本地回环数据路径已建立，而不是外部网线链路。

HardTree 可通过 `speed`、`duplex`、`loopback`、`flow_rx`、`flow_tx`、`csum_rx` 和 `csum_tx` 指定初始配置；省略时分别使用自动协商、自动双工、关闭回环，并关闭流控和校验和卸载。`mtu` 省略时使用 1500。
平台可用的速率由后端决定。运行时关闭端口后，可以重新配置速率启用端口，再按需启动 Stream。

| 路径         | 非空请求的长度要求                                                                  |
|--------------|-------------------------------------------------------------------------------------|
| 直接接收     | 缓冲区容量至少 60 字节。按 `max(60, 14 + mtu)` 分配可容纳配置范围内的整帧           |
| Stream 接收  | `frame.capacity` 至少为 `max(60, 14 + mtu)`。成功完成后由 `frame.size` 给出实际长度 |
| 两种发送路径 | 从 14 字节到 `14 + mtu` 字节。Stream 还要求 `frame.capacity >= frame.size`          |

短帧补齐后的接收长度至少为 60 字节，故小 MTU 下仍需留出相应容量。直接接收遇到放不进缓冲区的帧时，丢弃整帧并返回
`-MR_ERANGE`。

#### Stream 收发和归还

1. 完成 setup，初始化 `mr_waiter_t`，订阅所用方向的成功与错误事件，再调用
   `mr_eth_event_attach()`。同一事件可连续出现，逐帧订阅使用普通模式，省略
   `MR_WAIT_EDGE`。
2. 用 `mr_eth_rx_stream_start()` 或 `mr_eth_tx_stream_start()` 启用所需方向。
   启动时可以没有帧。支持独立启停的后端允许另一方向继续使用普通收发。
   两路绑定的后端会同时启用两个方向，具体规则见平台接入文档。
3. 对应方向启动成功后再提交。RX 提交帧描述对象和空的可写缓冲区，链路未建立时也可提交。
   TX 在描述对象中填入完整帧的 `size`，等端口可发送时提交。提交成功后，后端独占描述对象和缓冲区的使用权，
   调用者保留它们直到收到完成或取消通知。接收名额或发送资源暂不可用时返回 `-MR_EAGAIN`，帧留在调用者手中。
4. 回调通过 `data` 收到原 `mr_eth_frame_t` 指针。读取 `status` 判断结果，成功 RX
   才读取 `size`。一帧归还后，上层可以处理它，并在本次通知的所有消费者处理完后释放或重新提交。

| 事件                                           | 帧完成结果                                                                                  |
|------------------------------------------------|---------------------------------------------------------------------------------------------|
| `MR_ETH_WAIT_RX_READY`                         | 收到完整帧，`status = 0`，`size` 为接收长度                                                 |
| `MR_ETH_WAIT_TX_DONE`                          | 后端完成此帧的发送处理，`status = 0`，可以回收缓冲区                                        |
| `MR_ETH_WAIT_RX_ERROR`、`MR_ETH_WAIT_TX_ERROR` | 对应帧失败或取消，`status` 为负错误码。没有对应帧的链路或引擎错误可用 `data = MR_NULL` 通知 |

Wait 回调在事件发起者的上下文同步执行，可能处于中断。回调应及时交接帧，将耗时处理移到应用执行上下文。存在多个订阅者时，由一个消费者负责回收。

#### 停机与删除

在回调外调用 `*_stream_stop()`，可结束 Stream 并归还受影响方向的未完成帧。
调用 `mr_driver_shutdown()` 会用 `MR_ETH_SPEED_OFF` 关闭整个端口。后端在停机成功前归还两路未完成帧，
包括已经接受、尚未挂入 DMA 的 RX 缓冲，取消状态为 `-MR_EPERM`。waiter 保留至解除挂接且相关回调退出。
停机失败时，Wait 和尚未归还的帧仍有效，调用者可重试 shutdown。
成功停机后，框架解除 waiter 的挂接，下一次 setup 会重新应用保存的配置，应用需要重新挂接 waiter。

先成功执行 `mr_driver_shutdown()` 并处理帧取消通知，再调用 `mr_eth_del()`。
若还有驱动引用，对象释放会推迟到最后一次引用归还。跳过正常停机时，调用者自行处理在途帧和缓冲区，不能依赖最终释放发出取消通知。

### 5. API 参考

#### 实例、配置与链路

| API                                                | 说明                        |
|----------------------------------------------------|-----------------------------|
| `mr_eth_init()`、`mr_eth_create()`、`mr_eth_del()` | 初始化、创建和删除 ETH 对象 |
| `mr_eth_configure()`、`mr_eth_configuration()`     | 写入或读取配置              |
| `mr_eth_link()`                                    | 查询链路状态和累计统计      |

```c
mr_err_t mr_eth_init(mr_driver_t *drv, mr_eth_data_t *data,
                     void *priv, mr_eth_ops_t *ops);
mr_driver_t *mr_eth_create(mr_eth_data_t *data, void *priv, mr_eth_ops_t *ops);
mr_err_t mr_eth_del(mr_driver_t *drv);
mr_err_t mr_eth_configure(mr_driver_t *drv, const mr_eth_config_t *config);
mr_err_t mr_eth_configuration(mr_driver_t *drv, mr_eth_config_t *config);
mr_err_t mr_eth_link(mr_driver_t *drv, mr_eth_link_t *link);
```

- `init` 绑定调用者提供的 `drv` 和 `data`，`priv` 指向后端私有状态，`ops` 提供操作表。
  `create` 分配驱动对象，成功时接管由 `mr_malloc()` 分配的 `data`，失败时返回
  `MR_NULL`，此时 `data` 仍归调用者。两者均需单独 setup。`priv` 和 `ops` 由后端管理。
- `del` 释放对象的初始引用。应用仅通过 `mr_driver_find()` 借用实例时，用
  `mr_driver_put()` 归还查找引用。
- `configure` 在 setup 前保存配置，setup 后将配置交给后端，应用成功后再保存。非法 MAC、速率、双工或回环枚举返回 `-MR_EINVAL`，MTU 越界返回 `-MR_ERANGE`。配置、启停与删除需由调用者串行执行。
  `configuration` 读取保存的配置。自动协商时，保存的速率为 `MR_ETH_SPEED_AUTO`。
- `link` 要求设备已 setup。成功时通过 `link` 输出链路通断、实际速率、双工、自动协商后实际生效的 `flow`，以及 RX/TX 帧数、字节数、错误数；普通收发按结果计数，Stream 按完成事件计数，取消帧和资源忙不作为错误。其余状态接口以 0
  表示成功、负数表示错误。

#### 直接收发

| API              | 说明              |
|------------------|-------------------|
| `mr_eth_read()`  | 读一帧到 `buf`    |
| `mr_eth_write()` | 从 `buf` 提交一帧 |

```c
mr_ssize_t mr_eth_read(mr_driver_t *drv, void *buf, mr_size_t size);
mr_ssize_t mr_eth_write(mr_driver_t *drv, const void *buf, mr_size_t size);
```

两者均要求设备已 setup。读操作的 `size` 是缓冲区容量，返回帧长、无帧时的 0
或负错误码。写操作的 `size` 是帧长度，返回接受的帧长或负错误码。`size == 0` 时直接返回
0。长度限制见[帧长和配置](#帧长和配置)。

#### Stream 与事件

| API                                                        | 说明                            |
|------------------------------------------------------------|---------------------------------|
| `mr_eth_rx_stream_start()`、`mr_eth_tx_stream_start()`     | 启用指定方向的 Stream           |
| `mr_eth_rx_stream_stop()`、`mr_eth_tx_stream_stop()`       | 停止指定方向并归还未完成帧       |
| `mr_eth_rx_stream_is_busy()`、`mr_eth_tx_stream_is_busy()`  | 查询指定方向的 Stream 是否启用  |
| `mr_eth_rx_stream_submit()`、`mr_eth_tx_stream_submit()`   | 分别提交接收空间和待发送帧      |
| `mr_eth_event_attach()`、`mr_eth_event_raise()`            | 挂接 waiter、由后端同步分发事件 |

```c
mr_err_t mr_eth_rx_stream_start(mr_driver_t *drv);
mr_err_t mr_eth_rx_stream_stop(mr_driver_t *drv);
mr_err_t mr_eth_rx_stream_is_busy(mr_driver_t *drv, mr_bool_t *busy);
mr_err_t mr_eth_rx_stream_submit(mr_driver_t *drv, mr_eth_frame_t *frame);
mr_err_t mr_eth_tx_stream_start(mr_driver_t *drv);
mr_err_t mr_eth_tx_stream_stop(mr_driver_t *drv);
mr_err_t mr_eth_tx_stream_is_busy(mr_driver_t *drv, mr_bool_t *busy);
mr_err_t mr_eth_tx_stream_submit(mr_driver_t *drv, mr_eth_frame_t *frame);
mr_err_t mr_eth_event_attach(mr_driver_t *drv, mr_waiter_t *entry);
mr_err_t mr_eth_event_raise(mr_driver_t *drv, mr_uint32_t events,
                            const void *data);
```

- `submit` 接收调用者的 `frame`，返回 0 表示后端已接管使用权。空 `frame.buf` 返回
  `-MR_EINVAL`，容量或长度超限返回 `-MR_ERANGE`。后端缺少本方向所需操作时返回
  `-MR_ENOSYS`。后端检查对应方向是否启用，未启用返回 `-MR_EPERM`。端口暂时无法接受该帧返回
  `-MR_EAGAIN`。重复提交仍在途的帧返回 `-MR_EBUSY`。接收资源的共用限制由后端报告。
- Stream 启用后，即使没有在途帧，`is_busy()` 也报告为真。重复启动返回
  `-MR_EBUSY`。缺少对应回调的入口返回 `-MR_ENOSYS`。
- `event_attach` 在 setup 后挂接已初始化的 `entry`，已挂接的 waiter 返回
  `-MR_EBUSY`。`event_raise` 接收事件位和可选 `data`，要求驱动对象已初始化、已 setup 且 Wait 对象有效，
  同步分发并返回 0。事件入口的生命周期约束见[移植指南](porting.md)。

### 6. 常见问题

**链路已通，为什么 `read()` 返回 0？** 链路状态表示物理连接可用。直接 `read()` 返回
0 表示本次没有帧可读，空请求也返回 0。

**`submit()` 返回 `-MR_EAGAIN` 怎么办？**
帧仍归应用。等待接收名额空出、发送完成或端口恢复发送后再重试。

**为什么配置开了，仍然找不到 ETH 实例？** 功能宏决定公共 API
是否编译。查找需要后端注册实例，并由链接脚本保留驱动注册段。核对设备名和启动级别。 [驱动注册与查找](../driver.md)

---

## 开发者指南

### 7. 架构与数据结构

| 层次         | 职责                                                         |
|--------------|--------------------------------------------------------------|
| 应用或协议栈 | 构造与消费帧，管理软件队列、缓冲池和重试                     |
| ETH 公共层   | 校验对象、配置及帧边界，分派操作，管理 Wait 生命周期         |
| 平台后端     | 驱动 MAC、PHY 和 DMA，维护描述符与在途帧的关联，产生完成事件 |

`mr_driver_t` 关联 `data`、`priv` 和 `ops`。`mr_eth_data_t` 保存配置、统计和 Wait，
硬件状态放在 `priv`。`mr_eth_frame_t` 描述 `buf`、`capacity`、`size`、
`status` 和调用者上下文 `owner`。公共层在提交前检查长度，描述符与帧的关联由后端维护，因此上层可以按自己的内存与调度策略分配帧。

### 8. 核心流程与并发边界

`eth_init()` 先检查配置并初始化 Wait，再把配置交给 `ops->configure()`。通用驱动在回调成功后设置
`SETUP`。`eth_deinit()` 用 `MR_ETH_SPEED_OFF` 配置关闭端口，完成帧归还后删除 Wait。动态对象的 `eth_release()`
释放 `data`，静态对象由提供方保管。

后端事件入口使用 `MR_DRIVER_ASSERT()` 校验驱动，再由 Wait 检查事件对象是否有效。
正常 shutdown 在驱动对象仍有效时执行 `deinit`，因此后端可以发出帧取消通知。

Wait 的内部锁保护订阅链表。配置和 I/O 应与 shutdown、删除串行，后端负责停止事件来源并等正在执行的回调退出。

### 9. 扩展与源码导读

后端使用 `MR_ETH_DATA_INIT(mtu, speed, ...)` 构造公共数据，最后传入用花括号括起的 MAC 地址。
`MR_ETH_DEFINE` 定义静态对象，
`MR_ETH_INSTALL` 增加命名注册项，`MR_HT_ETH_INSTALL` 从 HardTree
取得节点配置并注册对象。注册级别、板级资源和操作表实现见 [ETH 驱动移植指南](porting.md)。

| 源码                                   | 阅读入口                                                       |
|----------------------------------------|----------------------------------------------------------------|
| `mrX/include/mrX/drivers/eth/Kconfig`  | 公共驱动与 Stream 开关                                         |
| `mrX/include/mrX/drivers/eth/mr_eth.h` | `mr_eth_ops_t`、帧结构、事件及安装宏                           |
| `mrX/drivers/eth/eth.c`                | `eth_check_config()`、收发入口、`eth_init()` 和 `eth_deinit()` |
| `mrX/drivers/driver.c`                 | setup、shutdown、注册遍历及最后一个引用的释放路径              |
| `mrX/kernel/wait.c`                    | Wait 同步分发、挂接与解除订阅                                  |
| `mrX/test/drivers/eth/eth.c`            | 直接收发、可选提交、帧归还与停机重试的单元测试                  |

按[测试框架接入流程](../../test/test.md)启用 `MR_USE_TEST`，将 ETH 用例加入测试工程后，
调用 `mr_testsuite_run("eth")`。关闭 Stream 时只运行直接收发与端口生命周期用例。

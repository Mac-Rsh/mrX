# UART 驱动（UART）

> UART 驱动在驱动框架之上提供统一的异步串行通信抽象：一次性配置波特率/数据位/停止位/校验/流控，
> 支持无软件 FIFO 的直接读写，并为收发两个方向各自提供可选的 stream 传输与半满/全满/完成事件。

| 项       | 说明                                                                                                                                    |
|----------|-----------------------------------------------------------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_DRIVER_UART`（默认关闭）；`MR_USE_DRIVER_UART_STREAM`（默认开启，依赖 UART）                                                    |
| 头文件   | `<mrX/drivers/uart/mr_uart.h>`，也通过 `<mrX.h>` 引入（`mrX/include/mrX.h:72`）                                                         |
| 依赖模块 | [驱动框架 Driver](../driver.md)、[等待队列 Wait](../../kernel/wait.md)（事件）；[动态内存 Malloc](../../libc/malloc.md)（动态对象释放） |
| 参考测试 | `mrX/test/drivers/uart/uart.c`（公共层行为测试；UART 类型驱动另经 `mrX/test/drivers/drivers_deep.c` 覆盖）                              |

---

## 用户指南

### 1. 模块概述

UART 驱动解决的问题是： **用同一套 API 完成串口参数配置、直接读写和可选的流式收发**，"写寄存器 / 装 DMA /
比中断标志"这类硬件细节收敛到平台 `ops` 里。它沿用 [ADC](../adc/adc.md) 的 `data / priv / ops`
分层、生命周期与 waitable 事件模型， **不内置软件 FIFO**——FIFO 封装、Modbus 等上层可以直接提交各自的缓冲区。

特性：

- 统一配置：`mr_uart_config_t` 描述波特率、5~8 位有效数据、1/1.5/2 个停止位、 无/奇/偶校验、RTS/CTS 流控与收发方向（`mode`
  ）；写入（
  `mr_uart_configure`）与回读 （`mr_uart_configuration`）使用同一个结构。
- 直接读写：`mr_uart_read` / `mr_uart_write` 在零长度时由公共层直接返回 0，其余情况透传 backend；`mr_uart_getchar` /
  `mr_uart_putchar` 是长度为 1 的封装。公共层不规定阻塞方式或缓存策略。
- 分方向 stream（`MR_USE_DRIVER_UART_STREAM`）：RX 与 TX 分别提供 start / stop / is_busy / offset 四组可选 backend
  回调；配置结构目前只有 `circular` 字段，具体运行语义由 backend 定义。
- 事件通知：公共头文件定义 8 个 `MR_UART_WAIT_*` 事件位；`event_attach` 订阅 waiter，`event_enable`/`event_disable` 按 mask
  控制后端支持的来源，平台在事件发生时可调用 `event_raise`。
- hardtree 安装：`MR_HT_UART_INSTALL` 在硬件模板中安装整颗 UART 实例，转发到驱动框架的
  `MR_HT_DRIVER_INSTALL`，实例名与初始化级别沿用驱动框架规则。

与 [ADC](../adc/adc.md) / [DAC](../dac/dac.md) 的差别：UART 没有通道概念，配置与事件都作用于整颗设备， 因此不提供
`mr_ht_uart_spec_t`
这类“通道规格”宏；UART 的 `data` 里也没有
`exist_mask` / `ref_internal` 字段，只有事件对象。

不适用于：软件 FIFO、协议解析（帧间隔、分帧、校验）、地址过滤、RS-485 方向自动切换等由上层 或协议层承担的职责。

### 2. 配置与依赖

| 配置项                      | 类型 | 默认值 | 说明                                                                                         |
|-----------------------------|------|--------|----------------------------------------------------------------------------------------------|
| `MR_USE_DRIVER_UART`        | bool | n      | 启用 UART 驱动（`mrX/include/mrX/drivers/uart/Kconfig:6`），依赖 `MR_USE_DRIVER`             |
| `MR_USE_DRIVER_UART_STREAM` | bool | y      | 启用 stream 收发 API（`mrX/include/mrX/drivers/uart/Kconfig:21`），依赖 `MR_USE_DRIVER_UART` |

启用后重新生成 `mr_config.h`，包含头文件即可使用。驱动 Kconfig 已通过父目录的通配 `rsource`
接入，不要手动修改生成的 `mr_config.h`：

```c
#include <mrX/drivers/uart/mr_uart.h>
```

关闭 `MR_USE_DRIVER_UART_STREAM` 可裁掉 `mr_uart_rx_*` / `mr_uart_tx_*` 全部函数与
`mr_uart_ops_t` 中的 8 个流回调，但平台 `ops` 若依赖流回调需同步裁剪；
`uart_deinit` 中的“先停流再销毁事件”逻辑同样在该宏下条件编译（`mrX/drivers/uart/uart.c:84`）。

平台接入需要提供 `mr_uart_ops_t` 与硬件私有数据；`configure`、`configuration`、`read`、
`write` 是必需回调（框架对这些指针做非空断言），8 个流回调全部可选。

### 3. 快速上手

配置 115200-8-N-1 并收发一个字节：

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
        return;                             /* Backend error */
    }
    mr_uart_getchar(drv, &ch);              /* Semantics are defined by backend read */
    mr_uart_putchar(drv, 'A');              /* Semantics are defined by backend write */
}
```

`drv` 为 UART 类型驱动指针，可来自平台安装的驱动或 `mr_driver_find`。

在应用里挂接事件（普通接收就绪、stream 进度 + 发送完成）：

```c
static void uart_waiter(mr_waiter_t *waiter, void *param,
                        mr_wait_event_t event, const void *data) {
    /* 回调在 raise 所在上下文执行；若处于中断不要阻塞，可直接处理或投递任务 */
}

void uart_event_example(mr_driver_t *drv) {
    static mr_waiter_t waiter;

    mr_waiter_init(&waiter, uart_waiter, MR_NULL,
                   MR_UART_WAIT_RX_READY | MR_UART_WAIT_RX_STREAM_HALF
                       | MR_UART_WAIT_RX_STREAM_FULL | MR_UART_WAIT_TX_DONE
                       | MR_UART_WAIT_RX_ERROR
                       | MR_UART_WAIT_TX_ERROR);
    mr_uart_event_attach(drv, &waiter);
    mr_uart_event_enable(drv, MR_UART_WAIT_RX_READY | MR_UART_WAIT_RX_STREAM_HALF
                              | MR_UART_WAIT_RX_STREAM_FULL | MR_UART_WAIT_TX_DONE
                              | MR_UART_WAIT_RX_ERROR
                              | MR_UART_WAIT_TX_ERROR);
    /* 需要的来源启用后再启动对应传输；使用结束后调用 event_disable。 */
}
```

### 4. 使用说明

**配置模型**：`mr_uart_config_t` 只描述 UART 的请求配置：`baud_rate`、`data_bits`、
`stop_bits`、`parity`、`flow_ctrl` 与 `mode`。公共层不检查这些字段的组合，也不负责把 配置换算成寄存器值；`mr_uart_configure`
直接把结构交给 `ops->configure`，
`mr_uart_configuration` 直接调用 `ops->configuration`。因此参数是否可实现、失败返回什么 错误码，都由具体 backend 决定。

**普通读写**：`mr_uart_read` / `mr_uart_write` 的公共层只做驱动类型、指针与必需 回调检查；`size == 0` 时直接返回 0，其余情况原样调用
backend。接口返回 `mr_ssize_t`， 因此 backend 可以返回实际处理字节数或负错误码。公共层没有软件 FIFO，也没有定义 阻塞/非阻塞、超时、与
stream 的互斥关系或调用返回后的缓冲区所有权规则；这些必须由 backend 或更上层接口自行约定。

`mr_uart_getchar` / `mr_uart_putchar` 只是把长度固定为 1 的 `read` / `write` 封装， 不会额外等待或重试。

**stream API**：仅在 `MR_USE_DRIVER_UART_STREAM` 启用时存在。RX/TX 各有
`start`、`stop`、`is_busy`、`offset` 四个独立回调，每个公共 API 只检查自己所需的 回调是否存在；缺少时返回 `-MR_ENOSYS`。
`start` 额外断言 `buf != MR_NULL` 且
`size != 0`，但当前公共层并不要求 `config` 非空，也不解释 `circular` 的具体运行规则： 这些属于 backend 能力。

```c
typedef struct mr_uart_rx_stream_config {
    mr_bool_t circular;
} mr_uart_rx_stream_config_t;

typedef struct mr_uart_tx_stream_config {
    mr_bool_t circular;
} mr_uart_tx_stream_config_t;
```

`is_busy` 与 `offset` 都是纯 backend 查询接口。公共层只保证成功时由对应输出参数返回 backend 填入的值，不规定 offset
的范围、完成后的保留方式、DMA 可见性或“busy”与线路空闲 之间的关系。实现新 backend 时应在该 backend 的文档中把这些语义写清楚。

**生命周期**：setup 时 `uart_init` 初始化 `mr_uart_data_t.event`。shutdown 时，如果启用了 stream 功能，`uart_deinit`
会依次尝试已实现的 RX/TX `stream_stop`；普通 shutdown 中， 任一 stop 返回错误会立即把错误向上传递，并暂不删除 event。删除路径已经带
`MR_DRIVER_IS_DYING` 标志，此时 stop 错误不会阻止继续清理，最终仍会删除 event；动态对象随后 由 `uart_release` 释放 `data`。

**事件**：`mr_uart_event_attach` 只把 waiter 挂到框架自己的 waitable 上；
`mr_uart_event_enable` / `mr_uart_event_disable` 只是把 `events` 原样交给可选的
`event_control`，公共层不校验 mask。backend 可调用 `mr_uart_event_raise`，它通过
`mr_waitable_raise(..., MR_NULL)` 同步分发事件且不携带 payload。waiter 回调运行在 raise 的 调用上下文；若某 backend 从 ISR
raise，回调自然也在 ISR 上下文执行。

当前公共头文件定义的事件位如下。名称表达事件类别；具体硬件何时 raise、stream 进度如何映射到这些事件，由各平台 backend 约定（树内
stm32h5 后端的实现见 `arch/st/stm32h5/platform/drivers/uart/uart.c`）。

| 事件（省略 `MR_UART_WAIT_` 前缀） | 位        |
|-----------------------------------|-----------|
| `RX_IDLE`                         | `1U << 0` |
| `RX_READY`                        | `1U << 1` |
| `TX_DONE`                         | `1U << 2` |
| `RX_ERROR`                        | `1U << 3` |
| `TX_ERROR`                        | `1U << 4` |
| `RX_STREAM_HALF`                  | `1U << 5` |
| `RX_STREAM_FULL`                  | `1U << 6` |
| `TX_STREAM_HALF`                  | `1U << 7` |
| `TX_STREAM_DONE`                  | `1U << 8` |

在树内 STM32H5 backend 中，`RX_READY` 对应 RXNE/RXFNE 条件，表示普通 `mr_uart_read()` 已有数据可取。backend 在触发
`RX_READY` 前会暂时屏蔽 RXNE 中断，避免 waiter 延后处理时因 RXNE 持续有效而重复进入中断；调用 `mr_uart_read()` 取走当前数据后会按
event mask 重新 arm。RX stream 激活期间该普通接收中断同样会被屏蔽，stream 停止后再恢复。`TX_DONE` 对应 UART TC，表示最后一个
stop bit 已经发送完成；它与 `TX_STREAM_DONE` 不同，后者只表示 DMA 已经完成本轮 buffer 搬运。

**HardTree**：`MR_HT_UART_INSTALL(_id, _priv, _ops)` 为实例定义零初始化的
`mr_uart_data_t`，再通过 `MR_HT_DRIVER_INSTALL` 安装整颗 UART。UART 没有像 GPIO pin 或 ADC channel 那样的子资源规格，因此当前头文件不提供
`mr_ht_uart_spec_t`。

### 5. API 参考

初始化与销毁：

| API               | 说明                                                                 |
|-------------------|----------------------------------------------------------------------|
| `MR_UART_DEFINE`  | 编译期定义 UART 驱动（不注册）                                       |
| `MR_UART_INSTALL` | 编译期定义并注册 UART 驱动（可被 `mr_driver_find` 查找、自动 setup） |
| `mr_uart_init`    | 运行时初始化一个静态 UART 驱动对象                                   |
| `mr_uart_create`  | 在堆上动态创建 UART 驱动                                             |
| `mr_uart_del`     | 删除 UART 驱动                                                       |

控制：

| API                       | 说明                                             |
|---------------------------|--------------------------------------------------|
| `mr_uart_configure`       | 写入整颗 UART 的通信参数                         |
| `mr_uart_configuration`   | 由 backend 填充 UART 配置                        |
| `mr_uart_read`            | 调用 backend `read`，返回其字节数或负错误码      |
| `mr_uart_write`           | 调用 backend `write`，返回其字节数或负错误码     |
| `mr_uart_getchar`         | 单字节读（`mr_uart_read` 的封装）                |
| `mr_uart_putchar`         | 单字节写（`mr_uart_write` 的封装）               |
| `mr_uart_rx_stream_start` | 启动 RX stream（需 `MR_USE_DRIVER_UART_STREAM`） |
| `mr_uart_rx_stream_stop`  | 停止 RX stream                                   |
| `mr_uart_tx_stream_start` | 启动 TX stream（需 `MR_USE_DRIVER_UART_STREAM`） |
| `mr_uart_tx_stream_stop`  | 停止 TX stream                                   |

参数与查询：

| API                         | 说明                                        |
|-----------------------------|---------------------------------------------|
| `mr_uart_rx_stream_is_busy` | 查询 RX stream 是否忙碌（经 `busy` 输出）   |
| `mr_uart_rx_stream_offset`  | 查询 backend 定义的 RX stream offset        |
| `mr_uart_tx_stream_is_busy` | 查询 TX stream 是否忙碌（经 `busy` 输出）   |
| `mr_uart_tx_stream_offset`  | 查询 backend 定义的 TX stream offset        |
| `mr_uart_event_attach`      | 订阅事件 waiter，不启用后端事件源           |
| `mr_uart_event_enable`      | 按 `MR_UART_WAIT_*` mask 启用后端支持的来源 |
| `mr_uart_event_disable`     | 按 `MR_UART_WAIT_*` mask 停用事件来源       |
| `mr_uart_event_raise`       | 触发事件（平台实现可调用）                  |

引用管理：

| API           | 说明                                          |
|---------------|-----------------------------------------------|
| `mr_uart_get` | 引用计数加一，返回驱动指针                    |
| `mr_uart_put` | 引用计数减一，返回 `MR_TRUE` 表示生命周期结束 |

**`MR_UART_DEFINE` / `MR_UART_INSTALL`**

```c
#define MR_UART_DEFINE(_name, _data, _priv, _ops)
#define MR_UART_INSTALL(_name, _data, _priv, _ops, _level)
```

- `_name`：驱动名（`MR_UART_INSTALL` 的名字可被 `mr_driver_find` 查找）。
- `_data`：`mr_uart_data_t` 实例；`_priv`：平台私有数据；`_ops`：`mr_uart_ops_t`。
- `_level`：初始化级别，由驱动框架按级别顺序自动 setup。
- 注意：两者都是编译期实例；`MR_UART_DEFINE` 只定义不注册，需要自行调用
  `mr_uart_init` 或框架安装接口。

**`mr_uart_init`**

```c
MR_INLINE mr_err_t mr_uart_init(mr_driver_t *drv, mr_uart_data_t *data,
                                void *priv, mr_uart_ops_t *ops);
```

- `drv` / `data` / `ops`：均不可为 `MR_NULL`（断言保护）；`priv` 可为 `MR_NULL`。
- 返回：成功 `0`，失败负数错误码（透传 `mr_driver_init`）。
- 注意：`data` 由调用者提供并保证生命周期；初始化为对象模型，不含硬件初始化，硬件动作在 setup（`uart_init`）阶段发生。

**`mr_uart_create`**

```c
MR_INLINE mr_driver_t *mr_uart_create(mr_uart_data_t *data, void *priv,
                                      mr_uart_ops_t *ops);
```

- `data` / `ops`：不可为 `MR_NULL`（断言保护）；`priv` 可为 `MR_NULL`。
- 返回：成功返回驱动指针，失败返回 `MR_NULL`。
- 注意：`data` 必须用 `mr_malloc` 分配——驱动删除时框架通过 `uart_release` 调用 `mr_free`；
  `priv` 与 `ops` 的资源由平台自行管理。

**`mr_uart_del`**

```c
MR_INLINE mr_err_t mr_uart_del(mr_driver_t *drv);
```

- `drv`：必须是 UART 类型驱动（`MR_DRIVER_ASSERT_TYPE` 校验，不要求已 setup）。
- 返回：成功 `0`，失败负数错误码。
- 注意：shutdown 时流停止失败会保留事件对象以便重试；删除路径即使停止失败也会销毁事件。

**`mr_uart_configure`**

```c
mr_err_t mr_uart_configure(mr_driver_t *drv, const mr_uart_config_t *config);
```

- `config`：不可为 `MR_NULL`（断言保护）。
- 返回：`ops->configure` 的返回值原样透传。
- 注意：公共层不检查字段范围、字段组合、当前是否正在传输，也不指定 backend 应使用哪一种错误码。

**`mr_uart_configuration`**

```c
mr_err_t mr_uart_configuration(mr_driver_t *drv, mr_uart_config_t *config);
```

- `config`：不可为 `MR_NULL`，由 backend 填充（断言保护）。
- 返回：`ops->configuration` 的返回值原样透传。
- 注意：公共层不缓存上一份配置，也不对 backend 填入的内容做二次处理。

**`mr_uart_read` / `mr_uart_write`**

```c
MR_INLINE mr_ssize_t mr_uart_read(mr_driver_t *drv, void *buf, mr_size_t size);
MR_INLINE mr_ssize_t mr_uart_write(mr_driver_t *drv, const void *buf,
                                   mr_size_t size);
```

- `buf`：非空或 `size` 为 0（`MR_ASSERT((buf != MR_NULL) || (size == 0))`）。
- `size`：请求字节数；为 0 时框架直接返回 0，不进入 `ops`。
- 返回：backend `read` / `write` 的 `mr_ssize_t` 返回值原样透传。
- 注意：公共层不定义阻塞/非阻塞、短读短写、超时、stream 并发或调用返回后的缓冲区生命周期；这些语义必须由具体 backend 说明。

**`mr_uart_getchar` / `mr_uart_putchar`**

```c
MR_INLINE mr_ssize_t mr_uart_getchar(mr_driver_t *drv, mr_uint8_t *ch);
MR_INLINE mr_ssize_t mr_uart_putchar(mr_driver_t *drv, mr_uint8_t ch);
```

- `ch`：读接口的输出参数，不可为 `MR_NULL`（断言保护）；写接口按值传参。
- 返回：分别等同于 `mr_uart_read(..., 1)` / `mr_uart_write(..., 1)` 的返回值，因此可能是 0、1 或负错误码，取决于 backend。
- 注意：两者不提供额外等待或重试。

**`mr_uart_rx_stream_start` / `mr_uart_tx_stream_start`**

```c
MR_INLINE mr_err_t mr_uart_rx_stream_start(mr_driver_t *drv, void *buf,
                                           mr_size_t size,
                                           const mr_uart_rx_stream_config_t *config);
MR_INLINE mr_err_t mr_uart_tx_stream_start(mr_driver_t *drv, const void *buf,
                                           mr_size_t size,
                                           const mr_uart_tx_stream_config_t *config);
```

- `buf`：不可为 `MR_NULL`（断言保护）；`size`：不可为 0（断言保护）。
- `config`：公共层不会断言其非空，也不会解释 `circular`，而是原样传给 backend。
- 返回：该方向 `stream_start` 未实现时公共层返回 `-MR_ENOSYS`；否则透传 backend 返回值。
- 注意：缓冲区所有权、循环模式语义、忙时行为与错误码均由 backend 约定。

**`mr_uart_rx_stream_stop` / `mr_uart_tx_stream_stop`**

```c
MR_INLINE mr_err_t mr_uart_rx_stream_stop(mr_driver_t *drv);
MR_INLINE mr_err_t mr_uart_tx_stream_stop(mr_driver_t *drv);
```

- `drv`：必须是 UART 类型驱动。
- 返回：未实现该方向的 `stream_stop` 时 `-MR_ENOSYS`；否则透传 backend 返回值。
- 注意：公共层不规定 stop 是否幂等，也不规定 stop 返回后 DMA/硬件、buffer 与 offset 的状态；backend 必须自行定义这些语义。

**`mr_uart_rx_stream_is_busy` / `mr_uart_tx_stream_is_busy`**

```c
MR_INLINE mr_err_t mr_uart_rx_stream_is_busy(mr_driver_t *drv, mr_bool_t *busy);
MR_INLINE mr_err_t mr_uart_tx_stream_is_busy(mr_driver_t *drv, mr_bool_t *busy);
```

- `busy`：输出参数，不可为 `MR_NULL`（断言保护）。
- 返回：未实现 `stream_is_busy` 时 `-MR_ENOSYS`；否则透传 backend 返回值并由 backend 填写 `busy`。
- 注意：公共层不定义 busy 的具体状态机。

**`mr_uart_rx_stream_offset` / `mr_uart_tx_stream_offset`**

```c
MR_INLINE mr_err_t mr_uart_rx_stream_offset(mr_driver_t *drv, mr_size_t *offset);
MR_INLINE mr_err_t mr_uart_tx_stream_offset(mr_driver_t *drv, mr_size_t *offset);
```

- `offset`：输出参数，不可为 `MR_NULL`（断言保护）。
- 返回：未实现 `stream_offset` 时 `-MR_ENOSYS`；否则透传 backend 返回值并由 backend 填写 `offset`。
- 注意：offset 的单位、范围以及 RX/TX 对它的具体解释当前没有由公共层进一步约束。

**`mr_uart_event_attach`**

```c
MR_INLINE mr_err_t mr_uart_event_attach(mr_driver_t *drv, mr_waiter_t *entry);
```

- `entry`：不可为 `MR_NULL`，应先按 wait API 初始化（断言保护）。
- 返回：透传 `mr_waiter_wait(entry, &ua_data->event)` 的结果。
- 注意：只把 waiter 挂到 UART 的 waitable，不调用 backend，也不会自动启用硬件事件来源。

**`mr_uart_event_enable` / `mr_uart_event_disable`**

```c
MR_INLINE mr_err_t mr_uart_event_enable(mr_driver_t *drv, mr_uint32_t events);
MR_INLINE mr_err_t mr_uart_event_disable(mr_driver_t *drv, mr_uint32_t events);
```

- `events`：公共层不校验 mask，原样传给 backend。
- 返回：未实现 `event_control` 时 `-MR_ENOSYS`；否则透传 backend 返回值。
- 注意：哪些事件可控制、不同组合是否合法，都由具体 backend 决定。

**`mr_uart_event_raise`**

```c
MR_INLINE mr_err_t mr_uart_event_raise(mr_driver_t *drv, mr_uint32_t events);
```

- `events`：传给 `mr_waitable_raise` 的事件位，可组合。
- 返回：`mr_waitable_raise` 的返回值。
- 注意：调用固定使用 `MR_NULL` payload；waiter 会同步运行在调用 `mr_uart_event_raise` 的上下文。

**`mr_uart_get` / `mr_uart_put`**

```c
MR_INLINE mr_driver_t *mr_uart_get(mr_driver_t *drv);
MR_INLINE mr_bool_t mr_uart_put(mr_driver_t *drv);
```

- `drv`：UART 类型驱动指针。
- 返回：`get` 返回驱动指针；`put` 返回 `MR_TRUE` 表示生命周期结束，否则 `MR_FALSE`。
- 注意：传输或回调可能访问驱动期间必须持有引用；二者分别转调
  `mr_driver_get` / `mr_driver_put`。

### 6. 完整示例

下面示例按公开 API 直接编写，平台 `ops`
只给出与头文件签名一致的桩实现，便于独立编译；错误处理按要求省略。

示例 1：动态创建 UART 驱动、配置并读写：

```c
#include <mrX/drivers/uart/mr_uart.h>
#include <mrX/libc/mr_malloc.h>

/* 签名与 mr_uart_ops_t 一致的最小桩：实际实现由平台寄存器/DMA 代码填充 */
static mr_err_t board_uart_configure(const mr_driver_t *drv,
                                     const mr_uart_config_t *config) {
    /* 写分频、数据位、停止位、校验、流控；不支持的有效配置返回 -MR_ENOSYS */
    return 0;
}

static mr_err_t board_uart_configuration(const mr_driver_t *drv,
                                         mr_uart_config_t *config) {
    /* 回读实际生效配置 */
    return 0;
}

static mr_ssize_t board_uart_read(const mr_driver_t *drv, void *buf,
                                  mr_size_t size) {
    /* 返回当前可用字节数，不等待；无数据返回 0 */
    return 0;
}

static mr_ssize_t board_uart_write(const mr_driver_t *drv, const void *buf,
                                   mr_size_t size) {
    /* 返回已交给硬件的字节数，允许短写 */
    return (mr_ssize_t)size;
}

static const mr_uart_ops_t board_uart_ops = {
    .configure = board_uart_configure,
    .configuration = board_uart_configuration,
    .read = board_uart_read,
    .write = board_uart_write,
};

void uart_dynamic_example(void) {
    mr_uart_data_t *data = mr_malloc(sizeof(*data));
    mr_uart_config_t config = {.baud_rate = 115200,
                               .data_bits = MR_UART_DATA_BITS_8,
                               .stop_bits = MR_UART_STOP_BITS_1,
                               .parity = MR_UART_PARITY_NONE,
                               .flow_ctrl = MR_UART_FLOW_CTRL_NONE,
                               .mode = MR_UART_MODE_TX_RX};
    mr_uint8_t ch;
    mr_driver_t *drv;

    if (data == MR_NULL) {
        return;
    }
    drv = mr_uart_create(data, MR_NULL, (mr_uart_ops_t *)&board_uart_ops);
    if (drv == MR_NULL) {
        mr_free(data);
        return;
    }
    mr_driver_setup(drv);                    /* uart_init：初始化事件对象 */

    mr_uart_configure(drv, &config);
    mr_uart_putchar(drv, 'O');
    if (mr_uart_getchar(drv, &ch) == 1) {
        /* ch 保存本次读到的字节 */
    }
    mr_uart_del(drv);                        /* 释放 data（mr_free） */
}
```

示例 2：TX stream 与事件 API 的组合方式（事件触发时机由 backend 定义）：

```c
static void uart_tx_done(mr_waiter_t *waiter, void *param,
                         mr_wait_event_t event, const void *data) {
    mr_bool_t *done = param;

    *done = MR_TRUE;     /* This backend chose to raise TX_DONE here. */
}

void uart_tx_stream_example(mr_driver_t *drv) {
    static const mr_uint8_t frame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
    static mr_waiter_t waiter;
    mr_uart_tx_stream_config_t stream_cfg = {.circular = MR_FALSE};
    mr_bool_t done = MR_FALSE;

    mr_waiter_init(&waiter, uart_tx_done, &done, MR_UART_WAIT_TX_DONE);
    mr_uart_event_attach(drv, &waiter);      /* 先订阅，再启动 */
    mr_uart_event_enable(drv, MR_UART_WAIT_TX_DONE);
    if (mr_uart_tx_stream_start(drv, frame, sizeof(frame), &stream_cfg) == 0) {
        /* Backend defines buffer lifetime and completion semantics. */
    }
    mr_uart_tx_stream_stop(drv);
    mr_uart_event_disable(drv, MR_UART_WAIT_TX_DONE);
}
```

示例 3：RX stream + 事件与 offset 查询接口：

```c
static void uart_rx_progress(mr_waiter_t *waiter, void *param,
                             mr_wait_event_t event, const void *data) {
    int *count = param;

    (*count)++;     /* Event meaning is defined by the backend. */
}

void uart_rx_stream_example(mr_driver_t *drv) {
    static mr_uint8_t rx_buffer[128];
    static mr_waiter_t waiter;
    static int event_count;
    mr_uart_rx_stream_config_t stream_cfg = {.circular = MR_TRUE};
    mr_size_t offset;

    mr_waiter_init(&waiter, uart_rx_progress, &event_count,
                   MR_UART_WAIT_RX_STREAM_HALF | MR_UART_WAIT_RX_STREAM_FULL);
    mr_uart_event_attach(drv, &waiter);
    mr_uart_event_enable(drv, MR_UART_WAIT_RX_STREAM_HALF | MR_UART_WAIT_RX_STREAM_FULL);
    mr_uart_rx_stream_start(drv, rx_buffer, sizeof(rx_buffer), &stream_cfg);
    mr_uart_rx_stream_offset(drv, &offset);  /* Backend-defined offset. */
    mr_uart_rx_stream_stop(drv);
    mr_uart_event_disable(drv, MR_UART_WAIT_RX_STREAM_HALF
                              | MR_UART_WAIT_RX_STREAM_FULL);
}
```

示例 4：HardTree 安装整颗实例（平台侧，需设置 `MR_HT_COMPAT`）：

```c
MR_HT_UART_INSTALL(uart0, &board_uart_priv, &board_uart_ops);
```

### 7. 常见问题（FAQ）

**Q：`mr_uart_rx_stream_stop` / `offset` / `is_busy` 返回 `-MR_ENOSYS`？**

对应 backend 回调没有实现。四个 stream API 是分别判定能力的：start、stop、is_busy、offset 互不替代，一个存在不代表另外三个也存在。

**Q：`mr_uart_event_enable` 返回 `-MR_ENOSYS`？**

backend 没有提供 `event_control`。`event_attach` 仍然是框架自己的 waitable 挂接接口，二者是 独立能力；能够挂 waiter
不代表硬件事件源一定支持 enable/disable。

**Q：某个 `MR_UART_WAIT_*` 位具体在什么时候触发？**

公共头文件只定义事件位名称，没有跨平台的统一时序契约。具体平台应在 自己的 backend 文档中说明何时 raise、与 DMA/线路状态之间的关系。

**Q：`read`/`write` 是阻塞还是非阻塞？0 返回值代表什么？**

公共 UART 层没有规定。除 `size == 0` 时框架直接返回 0 以外，非零请求的返回值完全来自 backend， 所以必须查看具体平台实现。

**Q：停止失败后能直接删除驱动吗？**

普通 `mr_driver_shutdown` 路径会把 stream stop 错误向上传递并保留 event；删除路径已经进入 DYING 状态，会忽略 stop
错误并继续清理。若应用关心设备是否真正停止，应在删除前显式成功 shutdown，而不是依赖删除路径报告 stop 结果。

**Q：为什么没有 `mr_ht_uart_spec_t`？**

UART 没有通道或引脚这类可取属性的子资源，配置作用于整颗设备。hardtree 只提供
`MR_HT_UART_INSTALL` 安装整颗实例，使用时直接拿 `mr_driver_t *` 调用 `mr_uart_*`。

---

## 开发者指南

### 8. 架构总览

UART 驱动与 [ADC](../adc/adc.md) / [DAC](../dac/dac.md) 同构，是[驱动框架](../driver.md)的派生模块，
`data / priv / ops` 三层分工、生命周期与 waitable 事件模型一致；差别在于 UART 是 **双方向**
设备：RX 与 TX 各有独立的流能力与事件位，且没有通道概念。

```
        +--------------------------------------------------+
        |  应用 / 上层封装（FIFO、Modbus、协议栈）           |
        |  mr_uart_read/write、mr_uart_rx_/tx_stream_*      |
        +-------------------------+------------------------+
                                  | 类型断言 / 零长度短路 / 能力检查
        +-------------------------v------------------------+
        |  UART 框架层（drivers/uart/uart.c + mr_uart.h）   |
        |  配置透传、可选流能力门控、事件对象管理             |
        +-------------------------+------------------------+
                                  | mr_uart_ops_t（configure / configuration /
                                  |  read / write + RX/TX 各 4 个流回调）
        +-------------------------v------------------------+
        |  平台实现：片上 UART（中断或 DMA 流）             |
        |  提供 mr_uart_ops_t 与硬件私有数据                |
        +--------------------------------------------------+
             依赖：驱动框架、等待队列（收发事件）、动态内存（仅动态创建）
```

### 9. 数据结构

模型数据（`mrX/include/mrX/drivers/uart/mr_uart.h:96`）：

```c
MR_DRIVER_CLAZZES_EXPORT(uart);
typedef struct mr_uart_data {
    mr_waitable_t event;    /* RX/TX 事件共用的 waitable */
} mr_uart_data_t;
```

与 [ADC](../adc/adc.md) / [DAC](../dac/dac.md) 不同，UART 的模型数据只有事件对象：没有 `exist_mask`
（无通道）、没有 `ref_internal`（无参考电压），收发方向的状态与缓冲区由平台私有数据保存。

配置结构（`mr_uart.h:75`）：

| 结构               | 字段                                                                                | 说明                                                                               |
|--------------------|-------------------------------------------------------------------------------------|------------------------------------------------------------------------------------|
| `mr_uart_config_t` | `baud_rate` / `data_bits:2` / `stop_bits:2` / `parity:2` / `flow_ctrl:2` / `mode:2` | 波特率（Hz）、5~8 位有效数据、1/1.5/2 停止位、无/奇/偶校验、4 种流控、3 种收发方向 |

配套枚举（`mr_uart.h:38`/`:46`/`:53`/`:60`/`:68`）：`mr_uart_data_bits_t`、
`mr_uart_stop_bits_t`、`mr_uart_parity_t`、`mr_uart_flow_control_t`、`mr_uart_mode_t`。例如
`MR_UART_DATA_BITS_5`~`MR_UART_DATA_BITS_8` 表示 5~8 数据位，但枚举的底层数值本身是从 0 开始的普通枚举值；公共层不会按枚举上界做配置校验。

事件位（`mr_uart.h:28`~`:35`）：8 个 `MR_UART_WAIT_*` 位分别对应 RX/TX 的半满、全满、 RX 空闲、TX 完成与两个方向错误，全部与
`mr_wait_event_t`（`mr_uint32_t`）兼容，可组合订阅。

操作表（`mr_uart.h:103`）：`configure`、`configuration`、`read`、`write` 为必需回调 （框架对应路径有 `MR_ASSERT` 非空保护）；8
个流回调 （`rx_stream_start`/`rx_stream_stop`/`rx_stream_is_busy`/`rx_stream_offset` 与 TX 同名的 4 个） 仅在
`MR_USE_DRIVER_UART_STREAM` 下声明，全部可选。 **启动接口只判定该方向的 `stream_start` 回调**：
`rx_stream_start`/`tx_stream_start` 未实现时直接返回 `-MR_ENOSYS`；`stop`/`is_busy`/
`offset` 各自只判定自身回调。`event_control` 是可选后端回调，`event_attach` 是框架 waitable 订阅函数。所有 ops
都同步运行在调用它们的上下文，公共层不额外切换执行上下文。

### 10. 核心流程

**clazz**：UART 定义了完整的 init/deinit/release （`MR_DRIVER_CLAZZES_DEFINE(uart, uart_init, uart_deinit, uart_release)`，
`mrX/drivers/uart/uart.c:17`）。`uart_init`（`uart.c:51`）在 setup 阶段调用
`mr_waitable_init(&ua_data->event)` 创建事件，失败即返回错误；`uart_deinit`（`uart.c:69`） 在 shutdown 阶段依次调用已实现的
`rx_stream_stop`、`tx_stream_stop`，任一失败立即返回错误 并 **保留事件对象**，只有两个方向都停止成功后才执行
`mr_waitable_del`。删除路径（驱动已标记 `DYING`）即使停止失败也会继续停另一方向并销毁事件，避免 `release` 释放 `data` 时泄漏
waitable。
`uart_release`（`uart.c:107`）对动态创建的 驱动执行 `mr_free(MR_DRIVER_DATA(drv))`
。未启用流功能（`MR_USE_DRIVER_UART_STREAM` 关闭） 时，deinit 直接销毁事件。

**配置路径**：`mr_uart_configure`（`uart.c:19`）断言驱动类型、`ops` 非空与
`config` 非空后直接透传 `ops->configure`；回读路径 `mr_uart_configuration`
（`uart.c:35`）不做校验，直接透传 `ops->configuration`，返回值原样传递。

**普通读写路径**：`mr_uart_read`（`mr_uart.h:236`）与 `mr_uart_write`
（`mr_uart.h:266`）只做断言（驱动类型、`ops` 非空、`buf`/`size` 一致性、回调非空），
`size == 0` 时直接返回 0，不触碰硬件；其余情况透传 `ops->read`/`ops->write`，框架不拷贝 数据、不缓存、不重试。
`mr_uart_getchar`（`mr_uart.h:309`）与 `mr_uart_putchar`
（`mr_uart.h:296`）是对应的单字节封装。

**流路径**：`mr_uart_rx_stream_start`（`mr_uart.h:327`）先断言 `buf != MR_NULL` 与
`size != 0`，再检查 `stream_start` 回调是否存在（`mr_uart.h:342`），未实现即返回
`-MR_ENOSYS`，否则透传 `ops->rx_stream_start`；TX 版本对称 （`mr_uart.h:440`、`mr_uart.h:455`）。`stop`/`is_busy`/`offset`
各自只检查自身回调 （`mr_uart.h:384`、`:357`、`:410` 与 `:497`、`:470`、`:523`），未实现返回 `-MR_ENOSYS`；
`is_busy`/`offset` 成功时通过输出指针返回状态，失败时输出内容不可用。

**事件路径**：`mr_uart_event_attach` 只通过 `mr_waiter_wait(entry, &ua_data->event)` 订阅 waiter；
`mr_uart_event_enable`/`mr_uart_event_disable` 通过可选的 `event_control` 后端回调控制来源，缺少该回调时返回
`-MR_ENOSYS`。 平台后端可调用 `mr_uart_event_raise`，它以
`mr_waitable_raise(&ua_data->event, events, MR_NULL)` 无载荷同步通知；框架不假定某个具体 UART 硬件中断后端，也不额外规定
raise 前后的 backend 状态发布顺序。

**HardTree 路径**：`MR_HT_UART_INSTALL(_id, _priv, _ops)`（`mr_uart.h:671`）先定义静态
`mr_uart_data_t __mr_uart_data_<id>`，再转调
`MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(uart), MR_DRIVER_TYPE_UART, ...)`
（`mrX/include/mrX/drivers/mr_driver.h:679`），实例名取自 `MR_HT_DRIVER_NAME`
（`mr_driver.h:627`），初始化级别取 `INIT_LEVEL` 属性，缺省
`MR_DRIVER_LEVEL_DEFER`（`mr_driver.h:682`）。实例遍历使用 `MR_HT_COMPAT`
（`mr_driver.h:505`~`:512`）。

### 11. 并发与中断安全

- 公共 UART 层没有传输锁、队列或状态机。configure/read/write/stream API 之间能否并发、RX 与 TX 是否真正独立，都由 backend
  实现决定。
- stream 的 busy/offset 只是 backend 查询接口；公共层不会据此自动阻止其他操作，也不会自行返回 `-MR_EBUSY`。
- 事件通知遵循[等待队列](../../kernel/wait.md)的同步分发语义：waiter 回调运行在 `mr_uart_event_raise` 的调用上下文。若
  backend 从 ISR raise，回调同样位于 ISR。
- `mr_uart_data_t.event` 在 setup 时初始化，在成功 shutdown 或删除清理时销毁。普通 shutdown 中 stream stop 失败会让 deinit
  提前返回并保留 event；删除路径会忽略 stop 错误继续清理。
- 共享 buffer、DMA cache、一致性和对象引用的并发规则不由公共 UART 层自动解决，backend 与上层必须自行建立有效生命周期。

### 12. 扩展与派生

- **平台移植**：普通中断收发、`RX_READY`/`TX_DONE`、stream/DMA、ISR 与 HardTree 接入见 [UART 驱动移植指南](porting.md)。
- **注册方式**：静态板级设备用 `MR_UART_DEFINE` / `MR_UART_INSTALL`（注册 + 自动 setup）； 运行期才确定存在的设备用
  `mr_uart_create` / `mr_uart_del`；硬件模板驱动的平台用
  `MR_HT_UART_INSTALL`。资源紧张、不依赖堆的场合优先静态方式。
- **上层封装**：软件 FIFO、协议分帧、RS-485 方向控制等可以建立在 UART API 之上，但不能假定某个事件位天然等价于某种平台线路状态；应以具体
  backend 文档为准。
- **当前实现范围**：当前提供配置/读写透传、可选 stream 能力门控、waitable 事件和生命周期管理；没有软件 FIFO 与协议解析。公共层行为测试见
  `mrX/test/drivers/uart/uart.c`。

### 13. 源码导读

| 位置                                         | 职责                                                                                          |
|----------------------------------------------|-----------------------------------------------------------------------------------------------|
| `mrX/include/mrX/drivers/uart/mr_uart.h:28`  | 9 个事件位（RX_IDLE、RX_READY、TX_DONE、RX/TX_ERROR、RX/TX STREAM HALF/FULL、TX_STREAM_DONE） |
| `mrX/include/mrX/drivers/uart/mr_uart.h:38`  | 数据位/停止位/校验/流控/模式枚举                                                              |
| `mrX/include/mrX/drivers/uart/mr_uart.h:75`  | `mr_uart_config_t` 配置结构                                                                   |
| `mrX/include/mrX/drivers/uart/mr_uart.h:96`  | clazz 导出与模型数据（仅事件对象）                                                            |
| `mrX/include/mrX/drivers/uart/mr_uart.h:103` | 操作表（必需 4 个 + 条件编译的 8 个流回调）                                                   |
| `mrX/include/mrX/drivers/uart/mr_uart.h:136` | `MR_UART_DEFINE` / `MR_UART_INSTALL`                                                          |
| `mrX/include/mrX/drivers/uart/mr_uart.h:153` | `mr_uart_init` / `create` / `del`                                                             |
| `mrX/include/mrX/drivers/uart/mr_uart.h:236` | 普通读写入口（断言 + 零长度短路 + 透传）                                                      |
| `mrX/include/mrX/drivers/uart/mr_uart.h:296` | 单字节封装 `putchar` / `getchar`                                                              |
| `mrX/include/mrX/drivers/uart/mr_uart.h:327` | RX 流 API（start/is_busy/stop/offset）                                                        |
| `mrX/include/mrX/drivers/uart/mr_uart.h:440` | TX 流 API（start/is_busy/stop/offset）                                                        |
| `mrX/include/mrX/drivers/uart/mr_uart.h:552` | 事件启停与挂接                                                                                |
| `mrX/include/mrX/drivers/uart/mr_uart.h:646` | 引用管理 `mr_uart_get` / `mr_uart_put`                                                        |
| `mrX/include/mrX/drivers/uart/mr_uart.h:671` | `MR_HT_UART_INSTALL` 实例安装宏                                                               |
| `mrX/drivers/uart/uart.c:17`                 | UART clazz（init/deinit/release）                                                             |
| `mrX/drivers/uart/uart.c:19`                 | 配置路径（断言 + 透传）                                                                       |
| `mrX/drivers/uart/uart.c:35`                 | 回读路径（透传，不校验）                                                                      |
| `mrX/drivers/uart/uart.c:51`                 | setup：`mr_waitable_init` 创建事件                                                            |
| `mrX/drivers/uart/uart.c:69`                 | shutdown：先停 RX/TX，再销毁事件                                                              |
| `mrX/drivers/uart/uart.c:107`                | release：`mr_free` 释放模型数据                                                               |
| `mrX/include/mrX/drivers/uart/Kconfig:6`     | `MR_USE_DRIVER_UART`                                                                          |
| `mrX/include/mrX/drivers/uart/Kconfig:21`    | `MR_USE_DRIVER_UART_STREAM`                                                                   |
| `mrX/include/mrX/drivers/mr_driver.h:677`    | `MR_HT_DRIVER_INSTALL`（HardTree 安装转发目标）                                               |
| `mrX/test/drivers/uart/uart.c`               | UART 公共层行为测试                                                                           |

### 14. 附录：配置宏与错误码

**配置宏**

| 宏                                                            | 说明                                          |
|---------------------------------------------------------------|-----------------------------------------------|
| `MR_USE_DRIVER_UART`                                          | 模块开关，默认 n，依赖 `MR_USE_DRIVER`        |
| `MR_USE_DRIVER_UART_STREAM`                                   | 流收发开关，默认 y，依赖 `MR_USE_DRIVER_UART` |
| `MR_UART_WAIT_RX_STREAM_HALF` / `MR_UART_WAIT_RX_STREAM_FULL` | RX stream 事件位                              |
| `MR_UART_WAIT_RX_IDLE`                                        | RX idle 事件位                                |
| `MR_UART_WAIT_RX_READY`                                       | RX 数据就绪事件位                             |
| `MR_UART_WAIT_TX_STREAM_HALF` / `MR_UART_WAIT_TX_STREAM_DONE` | TX stream 事件位                              |
| `MR_UART_WAIT_TX_DONE`                                        | TX 完成事件位                                 |
| `MR_UART_WAIT_RX_ERROR` / `MR_UART_WAIT_TX_ERROR`             | RX/TX error 事件位                            |

**错误码**：公共 UART 包装层明确使用 `-MR_ENOSYS` 表示某个可选 stream 回调或 `event_control` 未实现；其余
configure/read/write/stream/backend 错误码均原样透传。空指针、stream 的空 buffer 或零长度等编程错误在断言开启时由
`MR_ASSERT` 拦截。

# UART 驱动（UART）

> UART 驱动在驱动框架之上提供统一的异步串行通信抽象：一次性配置波特率/数据位/停止位/校验/流控，
> 支持无软件 FIFO 的直接读写，并为收发两个方向各自提供可选的 stream 传输与半满/全满/完成事件。

| 项       | 说明                                                                                                                             |
|----------|----------------------------------------------------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_DRIVER_UART`（默认关闭）；`MR_USE_DRIVER_UART_STREAM`（默认开启，依赖 UART）                                             |
| 头文件   | `<mrX/drivers/uart/mr_uart.h>`，也通过 `<mrX.h>` 引入（`mrX/include/mrX.h:72`）                                                  |
| 依赖模块 | [驱动框架 Driver](driver.md)、[等待队列 Wait](../kernel/wait.md)（收发事件）；[动态内存 Malloc](../libc/malloc.md)（仅动态创建） |
| 参考测试 | 无（`mrX/test/drivers/` 下暂无 uart 测试；UART 类型驱动仅经驱动框架被覆盖，见 `mrX/test/drivers/drivers_deep2.c:148`）           |

---

## 用户指南

### 1. 模块概述

UART 驱动解决的问题是： **用同一套 API 完成串口参数配置、直接读写和可选的流式收发**，"写寄存器 / 装 DMA /
比中断标志"这类硬件细节收敛到平台 `ops` 里。它沿用 [ADC](adc.md) 的 `data / priv / ops`
分层、生命周期与 waitable 事件模型， **不内置软件 FIFO**——FIFO 封装、Modbus 等上层可以直接提交各自的缓冲区。

特性：

- 统一配置：`mr_uart_config_t` 描述波特率、5~8 位有效数据、1/1.5/2 个停止位、 无/奇/偶校验以及 RTS/CTS 流控；写入（
  `mr_uart_configure`）与回读 （`mr_uart_configuration`）使用同一个结构。
- 直接读写：`mr_uart_read` / `mr_uart_write` 把调用方缓冲区直接交给硬件，允许短读短写， 不等待、不缓存；`mr_uart_getchar` /
  `mr_uart_putchar` 是单字节封装。
- 分方向 stream（`MR_USE_DRIVER_UART_STREAM`）：RX 与 TX 各自独立，每个方向提供 启动 / 停止 / 忙查询 / 偏移查询，支持循环模式，可由中断或
  DMA 实现，两个方向可并行。
- 事件通知：`mr_uart_event_attach` 订阅、`mr_uart_event_raise` 由平台触发，共 8 个事件位， 覆盖半满/全满、接收空闲、发送完成和方向错误。
- hardtree 安装：`MR_HT_UART_INSTALL` 在硬件模板中安装整颗 UART 实例，转发到驱动框架的
  `MR_HT_DRIVER_INSTALL`，实例名与初始化级别沿用驱动框架规则。

与 [ADC](adc.md) / [DAC](dac.md) 的差别：UART 没有通道概念，配置与事件都作用于整颗设备， 因此不提供 `mr_ht_uart_spec_t`
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
`uart_deinit` 中的“先停流再销毁事件”逻辑同样在该宏下条件编译（`mrX/drivers/uart/uart.c:91`）。

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
        .flow_control = MR_UART_FLOW_CONTROL_NONE,
    };
    mr_uint8_t ch;

    if (mr_uart_configure(drv, &config) != 0) {
        return;                             /* -MR_EINVAL / -MR_ENOSYS */
    }
    mr_uart_getchar(drv, &ch);              /* 读当前可用字节，无数据返回 0 */
    mr_uart_putchar(drv, 'A');              /* 交给硬件，允许短写 */
}
```

`drv` 为 UART 类型驱动指针，可来自平台安装的驱动或 `mr_driver_find`。

在应用里挂接事件（半满/全满 + 发送完成）：

```c
static void uart_waiter(mr_waiter_t *waiter, void *param,
                        mr_wait_event_t event, const void *data) {
    /* 中断上下文：只置标志或投递任务，不要阻塞 */
}

void uart_event_example(mr_driver_t *drv) {
    static mr_waiter_t waiter;

    mr_waiter_init(&waiter, uart_waiter, MR_NULL,
                   MR_UART_WAIT_RX_STREAM_HALF | MR_UART_WAIT_RX_STREAM_FULL
                       | MR_UART_WAIT_TX_COMPLETE | MR_UART_WAIT_RX_ERROR
                       | MR_UART_WAIT_TX_ERROR);
    mr_uart_event_attach(drv, &waiter);
}
```

先订阅再启动传输；附加到已经在跑的传输上时，还应查询一次当前状态（忙状态与偏移）， 避免漏掉订阅之前发生的状态变化。

### 4. 使用说明

**配置模型**：`mr_uart_config_t` 的 5 个字段分别取
`baud_rate`（非 0）、`data_bits`（`MR_UART_DATA_BITS_5`~`_8`）、
`stop_bits`（`MR_UART_STOP_BITS_1` / `_1_5` / `_2`）、`parity`（`_NONE` / `_ODD` / `_EVEN`）、
`flow_control`（`_NONE` / `_RTS` / `_CTS` / `_RTS_CTS`）。每个数据元素占一个字节， 有效数据位不包含校验位。字段越界或波特率为
0 时框架直接返回 `-MR_EINVAL`
（`mrX/drivers/uart/uart.c:32`）；平台不支持的有效配置返回 `-MR_ENOSYS`，不应静默降级。
`mr_uart_configuration` 回读实际生效配置。

**普通读写**：`mr_uart_read(drv, buf, size)` 读取硬件当前可用数据，不等待新数据；
`mr_uart_write(drv, buf, size)` 把数据交给硬件，允许短写。两者返回实际字节数，失败返回负 错误码，零长度请求直接返回零（框架在进入
`ops` 前短路）。无接收数据返回零， **不表示文件 结束**；硬件暂时无法接收发送数据也可以返回零。调用返回后平台不再访问传入的缓冲区。
同方向 stream 运行时，非空读写请求返回 `-MR_EBUSY`。普通写入不承诺线路发送完成，也不触发 本次写入专属的完成事件——需要确定发送结束时机的上层应使用单次
TX stream。

**配置与传输的互斥**：任一方向正在传输（包括线路仍在发送）时，配置修改返回 `-MR_EBUSY`。
同一方向的操作、配置修改和关闭必须由平台正确同步，通用层没有传输锁或队列。

**RTS/CTS 流控**：`flow_control` 只描述硬件流控引脚的使用方式，握手时序完全在平台实现内， 框架不做握手指令或超时处理。

**stream 启动与缓冲区**：`mr_uart_rx_stream_start(drv, buf, size, circular)` /
`mr_uart_tx_stream_start(drv, buf, size, circular)` 要求非空缓冲区和非零长度，成功返回零，
仅表示已经接受任务；失败不得保留缓冲区或留下运行中的传输。同方向忙时返回
`-MR_EBUSY`。平台要支持一个方向的 stream，必须同时提供该方向的 start、stop、is_busy 和 offset 四个回调，否则通用启动接口直接返回
`-MR_ENOSYS`
（`mrX/include/mrX/drivers/uart/mr_uart.h:317`、`mr_uart.h:430`）。

| 缓冲区              | 调用者约束                                                                         |
|---------------------|------------------------------------------------------------------------------------|
| 非循环 RX / 循环 RX | 保持有效直到单次传输完成或停止成功；已提交区域可读取但不可修改，未提交区域不可读取 |
| TX（含循环）        | 保持有效且**不可修改**直到单次传输完成或停止成功                                   |
| 循环 RX             | 硬件会覆盖旧数据，消费者必须在覆盖前处理完                                         |

平台负责 DMA 内存约束检查、缓存维护及发布顺序；无法满足时返回错误。

**循环与非循环**：非循环 RX 填满后结束，重新提交另一块缓冲区可能存在接收间隙；循环模式持续 使用同一缓冲区直到停止，平台不支持该模式时返回
`-MR_ENOSYS`。循环 TX 重复发送同一份不可修改 的数据。

**偏移与忙状态**：

- 非循环偏移为 `0..size`，循环偏移为 `0..size-1`。
- RX 偏移表示已经写入内存、对 CPU 可见的数据边界；TX 偏移表示已经交给硬件的字节进度， **不表示线路发送进度**。
- 停止或完成后保留最终偏移，直到下一次成功启动；查询必须返回一致的快照；第一次启动前返回 空闲和零偏移。
- 循环偏移不记录圈数，不能单凭位置判断是否丢失整圈数据。
- 非循环 TX 在最后一个停止位发出之前保持忙；循环 stream 在停止前保持忙。
- 循环 RX 的 offset 查询不锁定数据区域。仅提供半满/全满通知和回绕位置不能保证任意调度延迟
  下无损接收，上层需要控制消费延迟，不能把事件次数当作可靠字节计数。

**停止与生命周期**：`mr_uart_rx_stream_stop` / `mr_uart_tx_stream_stop` 幂等。成功返回时， 平台必须保证硬件和 DMA
不再访问对应缓冲区，且旧任务不会再产生通知；停止后可以查询最终偏移。 停止 TX **不保证线路空闲**，硬件中已经装入的数据仍可能发送，不应据此切换
RS-485 方向。 停止失败时不保证缓冲区已经归还，调用者应重试停止，不能假设回调不再触发。

框架在 setup 阶段创建事件、shutdown 阶段依次停止 RX、TX，再删除事件。停止失败时反初始化 返回错误并 **保留事件**
，已成功停止的方向不会自动恢复（`mrX/drivers/uart/uart.c:78`）。 因此可能停止失败的设备应先成功
shutdown，再删除；不能依赖对象最终释放路径处理可恢复的停止 失败。

**静态与动态创建**：`MR_UART_DEFINE`、`MR_UART_INSTALL`、`mr_uart_init` / `mr_uart_create` /
`mr_uart_del` 以及 `mr_uart_get` / `mr_uart_put` 沿用 ADC 的管理方式。动态创建成功后 UART 数据由驱动拥有，最终释放时调用
`mr_free`（`mrX/drivers/uart/uart.c:114`）；`priv` 和 `ops`
的资源仍由平台管理；静态驱动的数据不能在动态驱动创建接口中转交。

**引用管理**：传输或回调可能访问驱动期间必须持有驱动引用。删除前先成功停止传输。平台还需确保
关闭时普通硬件访问已经结束，并按自身资源管理约定处理外设时钟、引脚和中断资源。

**事件**：使用 `mr_uart_event_attach` 订阅，平台使用 `mr_uart_event_raise` 通知。先发布状态和
缓冲区内容，再发事件；回调可能位于中断上下文，不得阻塞。先订阅再启动传输；附加到运行中的 传输后还应查询当前状态，避免漏掉状态变化。

| 事件（省略 `MR_UART_WAIT_` 前缀） | 位        | 含义                                                   |
|-----------------------------------|-----------|--------------------------------------------------------|
| `RX_STREAM_HALF`                  | `1U << 0` | 接收缓冲区半满                                         |
| `RX_STREAM_FULL`                  | `1U << 1` | 接收缓冲区全满；循环模式每圈报告                       |
| `RX_IDLE`                         | `1U << 2` | 可选的硬件接收空闲提示，不停止接收，也不代表协议帧结束 |
| `TX_STREAM_HALF`                  | `1U << 3` | 发送缓冲区半块已交给硬件                               |
| `TX_STREAM_FULL`                  | `1U << 4` | 发送缓冲区整块已交给硬件                               |
| `TX_COMPLETE`                     | `1U << 5` | 单次发送最后一个停止位已发出，缓冲区可回收             |
| `RX_ERROR`                        | `1U << 6` | 接收方向发生错误                                       |
| `TX_ERROR`                        | `1U << 7` | 发送方向发生错误                                       |

半满阈值为 `size / 2` 字节，长度为一时不产生半满事件。平台必须保证单次 RX 完成时先置为空闲 再通知 FULL，单次 TX 完成时先置为空闲再通知
COMPLETE。循环传输的 FULL 只表示一次回绕， 不归还缓冲区；循环 TX 不产生表示整个任务完成的 COMPLETE 事件。

遇到无法继续保证数据有效性的错误，平台停止对应方向、保留有效偏移，随后报告错误事件；另一 方向可以继续工作。错误后忙状态为假
**不表示正常完成**，上层必须结合错误事件判断。当前接口 只统一方向错误通知，不提供具体硬件错误明细查询。

**上层封装约定**：FIFO 封装可以提交连续读写区域；Modbus 可以直接提交协议帧缓冲区。对于 Modbus，协议层负责帧间隔和分帧，RS-485
正常发送结束使用 `TX_COMPLETE` 判断。

**HardTree**：`MR_HT_UART_INSTALL(_id, _priv, _ops)` 与 [RAM](ram.md) 一样，只负责安装整颗 UART 实例：为实例定义静态
`mr_uart_data_t`，并通过 `MR_HT_DRIVER_INSTALL` 安装。平台设置
`MR_HT_COMPAT` 后，可在实例遍历中调用该宏；实例名称和初始化级别沿用驱动框架的 HardTree 规则。UART
没有通道或引脚这类子资源，配置作用于整颗设备，使用方直接拿
`mr_driver_t *` 调用 `mr_uart_*`，因此不提供 `mr_ht_uart_spec_t`。

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

| API                       | 说明                                               |
|---------------------------|----------------------------------------------------|
| `mr_uart_configure`       | 写入整颗 UART 的通信参数                           |
| `mr_uart_configuration`   | 回读实际生效的通信参数                             |
| `mr_uart_read`            | 直接读取硬件当前可用数据，返回实际字节数或负错误码 |
| `mr_uart_write`           | 直接把数据交给硬件，返回实际字节数或负错误码       |
| `mr_uart_getchar`         | 单字节读（`mr_uart_read` 的封装）                  |
| `mr_uart_putchar`         | 单字节写（`mr_uart_write` 的封装）                 |
| `mr_uart_rx_stream_start` | 启动 RX stream（需 `MR_USE_DRIVER_UART_STREAM`）   |
| `mr_uart_rx_stream_stop`  | 停止 RX stream                                     |
| `mr_uart_tx_stream_start` | 启动 TX stream（需 `MR_USE_DRIVER_UART_STREAM`）   |
| `mr_uart_tx_stream_stop`  | 停止 TX stream                                     |

参数与查询：

| API                         | 说明                                      |
|-----------------------------|-------------------------------------------|
| `mr_uart_rx_stream_is_busy` | 查询 RX stream 是否忙碌（经 `busy` 输出） |
| `mr_uart_rx_stream_offset`  | 查询 RX stream 已写入内存的偏移           |
| `mr_uart_tx_stream_is_busy` | 查询 TX stream 是否忙碌（经 `busy` 输出） |
| `mr_uart_tx_stream_offset`  | 查询 TX stream 已交给硬件的偏移           |
| `mr_uart_event_attach`      | 挂接事件等待者                            |
| `mr_uart_event_raise`       | 触发事件（一般由平台 `ops` 内部调用）     |

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

- `drv`：必须是 UART 类型驱动（`MR_DRIVER_ASSERT` 校验）。
- 返回：成功 `0`，失败负数错误码。
- 注意：删除前应先成功停止两个方向的传输；通用删除路径不传播 deinit 错误。

**`mr_uart_configure`**

```c
mr_err_t mr_uart_configure(mr_driver_t *drv, const mr_uart_config_t *config);
```

- `config`：不可为 `MR_NULL`（断言保护），字段取值见“配置模型”。
- 返回：成功 `0`；字段非法 `-MR_EINVAL`；平台不支持的有效配置 `-MR_ENOSYS`；平台返回的其他 错误码原样透传。
- 注意：任一方向正在传输时返回 `-MR_EBUSY`；框架只做 5 个字段的范围校验，具体分频/寄存器 可行性由平台判断。

**`mr_uart_configuration`**

```c
mr_err_t mr_uart_configuration(mr_driver_t *drv, mr_uart_config_t *config);
```

- `config`：不可为 `MR_NULL`，由平台填充（断言保护）。
- 返回：成功 `0`，失败负数错误码。
- 注意：返回的是 **实际生效**配置，可能与上次写入的请求值不同（平台可能做就近分频等调整）。

**`mr_uart_read` / `mr_uart_write`**

```c
MR_INLINE mr_ssize_t mr_uart_read(mr_driver_t *drv, void *buf, mr_size_t size);
MR_INLINE mr_ssize_t mr_uart_write(mr_driver_t *drv, const void *buf,
                                   mr_size_t size);
```

- `buf`：非空或 `size` 为 0（`MR_ASSERT((buf != MR_NULL) || (size == 0))`）。
- `size`：请求字节数；为 0 时框架直接返回 0，不进入 `ops`。
- 返回：成功返回实际字节数（允许小于 `size`），失败负数错误码。
- 注意：`read` 不等待新数据，无数据返回 0；`write` 允许短写，硬件暂不可接收也返回 0；
  `write` 返回只表示数据已交给硬件，不表示线路发送完成；同方向 stream 运行时非空请求返回
  `-MR_EBUSY`；返回后平台不再访问 `buf`。

**`mr_uart_getchar` / `mr_uart_putchar`**

```c
MR_INLINE mr_ssize_t mr_uart_getchar(mr_driver_t *drv, mr_uint8_t *ch);
MR_INLINE mr_ssize_t mr_uart_putchar(mr_driver_t *drv, mr_uint8_t ch);
```

- `ch`：读接口的输出参数，不可为 `MR_NULL`（断言保护）；写接口按值传参。
- 返回：成功返回 `sizeof(mr_uint8_t)` 即 1；`getchar` 无数据返回 0；失败负数错误码。
- 注意：两者分别是 `mr_uart_read` / `mr_uart_write` 的单字节封装，语义与读写路径完全一致， 不提供额外等待或重试。

**`mr_uart_rx_stream_start` / `mr_uart_tx_stream_start`**

```c
MR_INLINE mr_err_t mr_uart_rx_stream_start(mr_driver_t *drv, void *buf,
                                           mr_size_t size, mr_bool_t circular);
MR_INLINE mr_err_t mr_uart_tx_stream_start(mr_driver_t *drv, const void *buf,
                                           mr_size_t size, mr_bool_t circular);
```

- `buf`：不可为 `MR_NULL`（断言保护）；`size`：不可为 0（断言保护）。
- `circular`：`MR_TRUE` 为循环模式，`MR_FALSE` 为单次传输。
- 返回：成功 `0`（仅表示任务已接受）；同方向忙 `-MR_EBUSY`；该方向四个流回调不全
  `-MR_ENOSYS`；平台错误码原样透传。
- 注意：失败时不得保留缓冲区或留下运行中的传输；平台不支持循环模式时同样返回
  `-MR_ENOSYS`；RX 缓冲区已提交区域只读、未提交区域不可读，TX 缓冲区在传输期间不可修改。

**`mr_uart_rx_stream_stop` / `mr_uart_tx_stream_stop`**

```c
MR_INLINE mr_err_t mr_uart_rx_stream_stop(mr_driver_t *drv);
MR_INLINE mr_err_t mr_uart_tx_stream_stop(mr_driver_t *drv);
```

- `drv`：必须是 UART 类型驱动。
- 返回：成功 `0`；`ops` 未实现该方向的 `stream_stop` 时 `-MR_ENOSYS`；平台错误码原样透传。
- 注意：幂等；成功返回后平台不得再访问缓冲区或产生该方向的旧通知；停止 TX 不保证线路空闲，
  也不保证硬件中已装入的数据不再发送；停止或完成后最终偏移保留到下一次成功启动。

**`mr_uart_rx_stream_is_busy` / `mr_uart_tx_stream_is_busy`**

```c
MR_INLINE mr_err_t mr_uart_rx_stream_is_busy(mr_driver_t *drv, mr_bool_t *busy);
MR_INLINE mr_err_t mr_uart_tx_stream_is_busy(mr_driver_t *drv, mr_bool_t *busy);
```

- `busy`：输出参数，不可为 `MR_NULL`（断言保护）。
- 返回：成功 `0`，通过 `busy` 输出状态；未实现 `stream_is_busy` 时 `-MR_ENOSYS`；平台错误码 原样透传。
- 注意：只有返回 0 时 `busy` 才有意义；第一次启动前应返回空闲；非循环 TX 在最后一个停止位 发出前保持忙；错误导致的忙状态为假不能当作正常完成。

**`mr_uart_rx_stream_offset` / `mr_uart_tx_stream_offset`**

```c
MR_INLINE mr_err_t mr_uart_rx_stream_offset(mr_driver_t *drv, mr_size_t *offset);
MR_INLINE mr_err_t mr_uart_tx_stream_offset(mr_driver_t *drv, mr_size_t *offset);
```

- `offset`：输出参数，不可为 `MR_NULL`（断言保护）。
- 返回：成功 `0`，通过 `offset` 输出偏移；未实现 `stream_offset` 时 `-MR_ENOSYS`；平台错误码 原样透传。
- 注意：非循环范围为 `0..size`，循环为 `0..size-1`；RX 偏移是 CPU 可见的数据边界，TX 偏移 只是交给硬件的进度；查询必须是自洽快照；循环模式偏移不记录圈数。

**`mr_uart_event_attach`**

```c
MR_INLINE mr_err_t mr_uart_event_attach(mr_driver_t *drv, mr_waiter_t *entry);
```

- `entry`：不可为 `MR_NULL`，需先用 `mr_waiter_init` 初始化并声明期望事件位（断言保护）。
- 返回：成功 `0`，失败负数错误码（透传 `mr_waiter_wait`）。
- 注意：内部转调 `mr_waiter_wait(entry, &ua_data->event)`；挂接会重置等待者的边沿状态， 因此应在启动传输前挂接；附加到运行中的传输后应再查询一次忙状态与偏移。

**`mr_uart_event_raise`**

```c
MR_INLINE mr_err_t mr_uart_event_raise(mr_driver_t *drv, mr_wait_event_t events);
```

- `events`：`MR_UART_WAIT_*` 位掩码（可组合），由平台按实际进度与错误构造。
- 返回：成功 `0`，失败负数错误码。
- 注意：平台内部接口，转调 `mr_waitable_raise(&ua_data->event, events, MR_NULL)`，不带附加
  载荷；必须在发布状态和缓冲区内容之后调用；回调可能运行在中断上下文。

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

现有仓库中没有 `mrX/test/drivers/uart/` 测试文件，下面示例按公开 API 直接编写， 平台 `ops`
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
                               .flow_control = MR_UART_FLOW_CONTROL_NONE};
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

示例 2：单次 TX stream + `TX_COMPLETE` 判断线路发送结束：

```c
static void uart_tx_done(mr_waiter_t *waiter, void *param,
                         mr_wait_event_t event, const void *data) {
    mr_bool_t *done = param;

    *done = MR_TRUE;     /* 最后一个停止位已发出；中断上下文，不要阻塞 */
}

void uart_tx_stream_example(mr_driver_t *drv) {
    static const mr_uint8_t frame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
    static mr_waiter_t waiter;
    mr_bool_t done = MR_FALSE;

    mr_waiter_init(&waiter, uart_tx_done, &done, MR_UART_WAIT_TX_COMPLETE);
    mr_uart_event_attach(drv, &waiter);      /* 先订阅，再启动 */
    if (mr_uart_tx_stream_start(drv, frame, sizeof(frame), MR_FALSE) == 0) {
        /* 等待 done 或轮询 mr_uart_tx_stream_is_busy；完成后可回收/复用缓冲 */
    }
    /* ... 之后：mr_uart_tx_stream_stop(drv); */
}
```

示例 3：循环 RX stream + 半满/全满事件与偏移消费：

```c
static void uart_rx_progress(mr_waiter_t *waiter, void *param,
                             mr_wait_event_t event, const void *data) {
    int *count = param;

    (*count)++;     /* 只记事件；在任务上下文用 offset 取走 [last, offset) 数据 */
}

void uart_rx_stream_example(mr_driver_t *drv) {
    static mr_uint8_t rx_buffer[128];
    static mr_waiter_t waiter;
    static int event_count;
    mr_size_t offset;

    mr_waiter_init(&waiter, uart_rx_progress, &event_count,
                   MR_UART_WAIT_RX_STREAM_HALF | MR_UART_WAIT_RX_STREAM_FULL);
    mr_uart_event_attach(drv, &waiter);
    mr_uart_rx_stream_start(drv, rx_buffer, sizeof(rx_buffer), MR_TRUE);
    mr_uart_rx_stream_offset(drv, &offset);  /* 已写入内存、CPU 可见的数据边界 */
    /* ... 消费者必须在覆盖前处理完；循环 offset 不记录圈数 */
}
```

示例 4：HardTree 安装整颗实例（平台侧，需设置 `MR_HT_COMPAT`）：

```c
MR_HT_UART_INSTALL(uart0, &board_uart_priv, &board_uart_ops);
```

### 7. 常见问题（FAQ）

**Q：`mr_uart_configure` 返回 `-MR_EINVAL`？**

字段超出枚举范围或 `baud_rate` 为 0。检查 `data_bits` 是否在
`MR_UART_DATA_BITS_5`~`MR_UART_DATA_BITS_8`、`stop_bits` 是否不大于
`MR_UART_STOP_BITS_2`、`parity` 是否不大于 `MR_UART_PARITY_EVEN`、`flow_control` 是否不大于
`MR_UART_FLOW_CONTROL_RTS_CTS`。

**Q：`mr_uart_configure` 返回 `-MR_ENOSYS`？**

配置本身合法，但平台无法产生该波特率/帧格式（例如分频不可达、芯片不支持 1.5 停止位或
RTS/CTS）。框架不应静默降级，平台必须显式返回错误；换用平台支持的组合，或在平台 `ops`
中补齐实现。

**Q：`mr_uart_configure` 返回 `-MR_EBUSY`？**

任一方向正在传输，包括数据已装入硬件、线路仍在发送的阶段。先等 `TX_COMPLETE` 或停止 stream，再改配置。

**Q：`mr_uart_read` 返回 0 是不是读完了？**

不是。0 表示当前没有可用数据，语义上“暂时为空”，不代表文件结束；同理
`mr_uart_write` 返回小于 `size` 或 0 只说明硬件暂时接收不了这么多，需要上层重试剩余部分。

**Q：为什么 `mr_uart_tx_stream_offset` 已经到 `size`，线路却还没发完？**

TX 偏移表示已交给硬件的字节进度，不表示线路发送进度。要判断线路发送结束，使用单次 TX stream 并等 `TX_COMPLETE`
（最后一个停止位已发出），不要用偏移或 `stream_is_busy` 推断。

**Q：`mr_uart_rx_stream_stop` / `offset` / `is_busy` 返回 `-MR_ENOSYS`？**

平台 `ops` 未实现对应回调。注意：`rx/tx_stream_start` 要求该方向四个回调 （start/stop/is_busy/offset） **全部**存在，缺一个就返回
`-MR_ENOSYS`；而 `stop`、`is_busy`、
`offset` 各自只检查自己的回调。

**Q：收不到半满/全满事件？**

排查三点：等待者期望事件是否包含 `MR_UART_WAIT_RX_STREAM_HALF`/
`MR_UART_WAIT_RX_STREAM_FULL`；`mr_uart_event_attach` 是否在 `mr_driver_setup` 之后调用 （事件对象在 setup 时创建），且处于
`stream_start` 之前；平台是否在缓冲进度点调用了
`mr_uart_event_raise`。另外注意 `size == 1` 时不产生半满事件，半满阈值为 `size / 2`。

**Q：循环 RX 下丢数据怎么排查？**

先确认读者是否在数据被覆盖前处理完：循环 RX 会覆盖旧数据，`offset` 只是当前写入位置，
不记录圈数，半满/全满事件次数也不是可靠字节计数。上层需要控制消费延迟（缩短处理周期、 减小缓冲区、或改用非循环模式分包提交），不能只依赖事件次数。

**Q：`stream_stop` 成功后能立即切换 RS-485 方向吗？**

不能。停止 TX 不保证线路空闲，硬件中已经装入的数据仍可能发送完毕；RS-485 的正常发送结束应 用 `TX_COMPLETE` 判断。

**Q：停止失败后能直接删除驱动吗？**

不建议。`uart_deinit` 在停止失败时返回错误并保留事件，已成功停止的方向不会自动恢复；通用 删除路径不传播 deinit 错误。应先重试
shutdown（停止）直到成功，再删除。

**Q：为什么没有 `mr_ht_uart_spec_t`？**

UART 没有通道或引脚这类可取属性的子资源，配置作用于整颗设备。hardtree 只提供
`MR_HT_UART_INSTALL` 安装整颗实例，使用时直接拿 `mr_driver_t *` 调用 `mr_uart_*`。

---

## 开发者指南

### 8. 架构总览

UART 驱动与 [ADC](adc.md) / [DAC](dac.md) 同构，是[驱动框架](driver.md)的派生模块，
`data / priv / ops` 三层分工、生命周期与 waitable 事件模型一致；差别在于 UART 是 **双方向**
设备：RX 与 TX 各有独立的流能力与事件位，且没有通道概念。

```
        +--------------------------------------------------+
        |  应用 / 上层封装（FIFO、Modbus、协议栈）           |
        |  mr_uart_read/write、mr_uart_rx_/tx_stream_*      |
        +-------------------------+------------------------+
                                  | 类型断言 / 配置校验 / EBUSY 判定
        +-------------------------v------------------------+
        |  UART 框架层（drivers/uart/uart.c + mr_uart.h）   |
        |  配置字段范围校验、流能力门控、事件对象管理         |
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

模型数据（`mrX/include/mrX/drivers/uart/mr_uart.h:78`）：

```c
MR_DRIVER_CLAZZES_EXPORT(uart);
typedef struct mr_uart_data {
    mr_waitable_t event;    /* RX/TX 事件共用的 waitable */
} mr_uart_data_t;
```

与 [ADC](adc.md) / [DAC](dac.md) 不同，UART 的模型数据只有事件对象：没有 `exist_mask`
（无通道）、没有 `ref_internal`（无参考电压），收发方向的状态与缓冲区由平台私有数据保存。

配置结构（`mr_uart.h:68`）：

| 结构               | 字段                                                                        | 说明                                                                 |
|--------------------|-----------------------------------------------------------------------------|----------------------------------------------------------------------|
| `mr_uart_config_t` | `baud_rate` / `data_bits:8` / `stop_bits:8` / `parity:8` / `flow_control:8` | 波特率（Hz）、5~8 位有效数据、1/1.5/2 停止位、无/奇/偶校验、4 种流控 |

配套枚举（`mr_uart.h:38`/`:46`/`:53`/`:60`）：`mr_uart_data_bits_t`（值 5~8）、
`mr_uart_stop_bits_t`、`mr_uart_parity_t`、`mr_uart_flow_control_t`；`stop_bits`、`parity`、
`flow_control` 依赖枚举全序来做上界校验，顺序不可随意调整。

事件位（`mr_uart.h:28`~`:35`）：8 个 `MR_UART_WAIT_*` 位分别对应 RX/TX 的半满、全满、 RX 空闲、TX 完成与两个方向错误，全部与
`mr_wait_event_t`（`mr_uint32_t`）兼容，可组合订阅。

操作表（`mr_uart.h:83`）：`configure`、`configuration`、`read`、`write` 为必需回调 （框架对应路径有 `MR_ASSERT` 非空保护）；8
个流回调 （`rx_stream_start`/`rx_stream_stop`/`rx_stream_is_busy`/`rx_stream_offset` 与 TX 同名的 4 个） 仅在
`MR_USE_DRIVER_UART_STREAM` 下声明，全部可选。 **启动接口的能力判定以方向为单位**：
`rx_stream_start` 要求 RX 的 4 个回调同时存在，否则直接返回 `-MR_ENOSYS`；`stop`/`is_busy`/
`offset` 各自只判定自身回调。流回调可能重入中断上下文，平台实现需自行保证缓冲区一致性。

### 10. 核心流程

**clazz**：UART 定义了完整的 init/deinit/release （`MR_DRIVER_CLAZZES_DEFINE(uart, uart_init, uart_deinit, uart_release)`，
`mrX/drivers/uart/uart.c:17`）。`uart_init`（`uart.c:60`）在 setup 阶段调用
`mr_waitable_init(&ua_data->event)` 创建事件，失败即返回错误；`uart_deinit`（`uart.c:78`） 在 shutdown 阶段依次调用已实现的
`rx_stream_stop`、`tx_stream_stop`，任一失败立即返回错误 并 **保留事件对象**，只有两个方向都停止成功后才执行
`mr_waitable_del`
（`uart.c:95`、`uart.c:101`、`uart.c:110`）。`uart_release`（`uart.c:114`）对动态创建的 驱动执行 `mr_free(MR_DRIVER_DATA(drv))`
。未启用流功能（`MR_USE_DRIVER_UART_STREAM` 关闭） 时，deinit 直接销毁事件。

**配置路径**：`mr_uart_configure`（`uart.c:19`）在透传前做 5 个字段的范围校验 （`uart.c:32`：`baud_rate == 0`、`data_bits` 不在
5~8、`stop_bits > 2`、`parity > 2`、
`flow_control > 3` 之一即返回 `-MR_EINVAL`），随后透传 `ops->configure`
（`uart.c:41`）。回读路径 `mr_uart_configuration`（`uart.c:44`）不做校验，直接透传
`ops->configuration`（`uart.c:57`），返回值原样传递。

**普通读写路径**：`mr_uart_read`（`mr_uart.h:212`）与 `mr_uart_write`
（`mr_uart.h:242`）只做断言（驱动类型、`ops` 非空、`buf`/`size` 一致性、回调非空），
`size == 0` 时直接返回 0，不触碰硬件；其余情况透传 `ops->read`/`ops->write`，框架不拷贝 数据、不缓存、不重试。
`mr_uart_getchar`（`mr_uart.h:285`）与 `mr_uart_putchar`
（`mr_uart.h:272`）是对应的单字节封装。

**流路径**：`mr_uart_rx_stream_start`（`mr_uart.h:303`）先断言 `buf != MR_NULL` 与
`size != 0`，再检查 RX 方向 4 个回调是否齐全（`mr_uart.h:317`），缺一即返回
`-MR_ENOSYS`，否则透传 `ops->rx_stream_start`；TX 版本对称 （`mr_uart.h:416`、`mr_uart.h:430`）。`stop`/`is_busy`/`offset`
各自只检查自身回调 （`mr_uart.h:360`、`:333`、`:386` 与 `:473`、`:446`、`:499`），未实现返回 `-MR_ENOSYS`；
`is_busy`/`offset` 成功时通过输出指针返回状态，失败时输出内容不可用。

**事件路径**：`mr_uart_event_attach`（`mr_uart.h:528`）调用
`mr_waiter_wait(entry, &ua_data->event)` 把等待者挂到模型数据的事件上；
`mr_uart_event_raise`（`mr_uart.h:550`）以
`mr_waitable_raise(&ua_data->event, events, MR_NULL)` 触发，无附加载荷。平台流实现 （DMA 半满/全满中断、TX
完成中断、空闲检测、错误中断）在发布状态与缓冲内容之后调用 raise，等待者回调随即执行。

**HardTree 路径**：`MR_HT_UART_INSTALL(_id, _priv, _ops)`（`mr_uart.h:596`）先定义静态
`mr_uart_data_t __mr_uart_data_<id>`，再转调
`MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(uart), MR_DRIVER_TYPE_UART, ...)`
（`mrX/include/mrX/drivers/mr_driver.h:654`），实例名取自 `MR_HT_DRIVER_NAME`
（`mr_driver.h:656`），初始化级别取 `INIT_LEVEL` 属性，缺省
`MR_DRIVER_LEVEL_DEFER`（`mr_driver.h:657`）。实例遍历使用 `MR_HT_COMPAT`
（`mr_driver.h:487`~`:510`）。

### 11. 并发与中断安全

- 框架层 API 无内部互斥、无传输锁、无队列。同一方向的 start/stop/offset/配置修改的并发由 平台 `ops` 或应用保证；UART
  通用层只做能力门控与字段校验。
- 双方向的原子性由平台负责：RX 与 TX 可以并行传输，但“同方向忙”必须由平台如实返回
  `-MR_EBUSY`，否则会出现缓冲区被覆盖或偏移错乱。
- 事件挂接触发（`mr_uart_event_attach`/`mr_uart_event_raise`）与等待者回调的上下文约定遵循
  [等待队列](../kernel/wait.md)：raise 常发生于平台中断，回调在中断上下文执行，禁止阻塞。
- `mr_uart_data_t.event` 在 setup/shutdown 之间有效；shutdown 后仍挂着的等待者由等待队列的 删除语义处理，应用应在 shutdown
  前摘除等待者。停止失败时事件被保留，此时事件仍可用， 但对应方向的传输状态是未定义的，必须重试停止。
- 循环 RX 的内存可见性由平台保证：RX 偏移所表示的边界必须是“已经写入内存、对 CPU 可见” 的范围（DMA
  场景需要缓存维护与发布顺序）；平台无法保证时返回错误，而不是让上层读到脏数据。
- 删除与引用的竞争：传输或回调可能访问驱动期间必须持有引用（`mr_uart_get`/`mr_uart_put`）， 删除前先成功停止传输。

### 12. 扩展与派生

- **平台移植**：实现 `mr_uart_ops_t`，必需回调 `configure`/`configuration`/`read`/`write`； 有流能力时按方向补齐 4 个回调（例如
  DMA + 半满/全满/完成中断，或中断逐字节收发）。平台 私有数据里保存外设基址、RX/TX 缓冲区指针与长度、`circular`
  、当前偏移、忙标志等状态，并在 进度点调用 `mr_uart_event_raise`。
- **注册方式**：静态板级设备用 `MR_UART_DEFINE` / `MR_UART_INSTALL`（注册 + 自动 setup）； 运行期才确定存在的设备用
  `mr_uart_create` / `mr_uart_del`；硬件模板驱动的平台用
  `MR_HT_UART_INSTALL`。资源紧张、不依赖堆的场合优先静态方式。
- **上层封装**：软件 FIFO 可以基于 `mr_uart_read`/`write` 或 RX/TX stream 实现连续区域提交； Modbus
  等协议栈直接提交帧缓冲区，帧间隔与分帧由协议层负责，RS-485 方向切换以
  `TX_COMPLETE` 为发送结束依据。
- **当前实现范围**：当前提供通用接口、配置校验、事件和生命周期管理；没有软件 FIFO、协议 解析、具体芯片寄存器操作或主机模拟后端。硬件收发能力由平台操作表实现。
- **参考现状**：当前平台树与 `mrX/test/drivers/` 下都没有 UART 的 `ops` 实现或专项测试；
  可参照 [ADC](adc.md) / [DAC](dac.md) 的移植路径（属性展开、`ops` 实现、
  `MR_HT_*_INSTALL` 安装）以及 `mrX/test/drivers/adc/adc.c` 的桩 `ops`（回调签名最小参考）。

### 13. 源码导读

| 位置                                         | 职责                                                             |
|----------------------------------------------|------------------------------------------------------------------|
| `mrX/include/mrX/drivers/uart/mr_uart.h:28`  | 8 个事件位（RX/TX HALF/FULL、RX_IDLE、TX_COMPLETE、RX/TX_ERROR） |
| `mrX/include/mrX/drivers/uart/mr_uart.h:38`  | 数据位/停止位/校验/流控枚举                                      |
| `mrX/include/mrX/drivers/uart/mr_uart.h:68`  | `mr_uart_config_t` 配置结构                                      |
| `mrX/include/mrX/drivers/uart/mr_uart.h:77`  | clazz 导出与模型数据（仅事件对象）                               |
| `mrX/include/mrX/drivers/uart/mr_uart.h:83`  | 操作表（必需 4 个 + 条件编译的 8 个流回调）                      |
| `mrX/include/mrX/drivers/uart/mr_uart.h:112` | `MR_UART_DEFINE` / `MR_UART_INSTALL`                             |
| `mrX/include/mrX/drivers/uart/mr_uart.h:138` | `mr_uart_init` / `create` / `del`                                |
| `mrX/include/mrX/drivers/uart/mr_uart.h:212` | 普通读写入口（断言 + 零长度短路 + 透传）                         |
| `mrX/include/mrX/drivers/uart/mr_uart.h:272` | 单字节封装 `putchar` / `getchar`                                 |
| `mrX/include/mrX/drivers/uart/mr_uart.h:303` | RX 流 API（start/is_busy/stop/offset）                           |
| `mrX/include/mrX/drivers/uart/mr_uart.h:416` | TX 流 API（start/is_busy/stop/offset）                           |
| `mrX/include/mrX/drivers/uart/mr_uart.h:528` | 事件挂接与触发                                                   |
| `mrX/include/mrX/drivers/uart/mr_uart.h:571` | 引用管理 `mr_uart_get` / `mr_uart_put`                           |
| `mrX/include/mrX/drivers/uart/mr_uart.h:596` | `MR_HT_UART_INSTALL` 实例安装宏                                  |
| `mrX/drivers/uart/uart.c:17`                 | UART clazz（init/deinit/release）                                |
| `mrX/drivers/uart/uart.c:19`                 | 配置路径（字段范围校验 + 透传）                                  |
| `mrX/drivers/uart/uart.c:44`                 | 回读路径（透传，不校验）                                         |
| `mrX/drivers/uart/uart.c:60`                 | setup：`mr_waitable_init` 创建事件                               |
| `mrX/drivers/uart/uart.c:78`                 | shutdown：先停 RX/TX，再销毁事件                                 |
| `mrX/drivers/uart/uart.c:114`                | release：`mr_free` 释放模型数据                                  |
| `mrX/include/mrX/drivers/uart/Kconfig:6`     | `MR_USE_DRIVER_UART`                                             |
| `mrX/include/mrX/drivers/uart/Kconfig:21`    | `MR_USE_DRIVER_UART_STREAM`                                      |
| `mrX/include/mrX/drivers/mr_driver.h:654`    | `MR_HT_DRIVER_INSTALL`（HardTree 安装转发目标）                  |
| `mrX/test/drivers/drivers_deep2.c:148`       | UART 类型驱动的创建/生命周期覆盖（无 UART 专项测试）             |

### 14. 附录：配置宏与错误码

**配置宏**

| 宏                                                            | 说明                                          |
|---------------------------------------------------------------|-----------------------------------------------|
| `MR_USE_DRIVER_UART`                                          | 模块开关，默认 n，依赖 `MR_USE_DRIVER`        |
| `MR_USE_DRIVER_UART_STREAM`                                   | 流收发开关，默认 y，依赖 `MR_USE_DRIVER_UART` |
| `MR_UART_WAIT_RX_STREAM_HALF` / `MR_UART_WAIT_RX_STREAM_FULL` | RX 缓冲半满 / 全满事件位                      |
| `MR_UART_WAIT_RX_IDLE`                                        | 可选的接收空闲提示事件位                      |
| `MR_UART_WAIT_TX_STREAM_HALF` / `MR_UART_WAIT_TX_STREAM_FULL` | TX 缓冲半块 / 整块已交给硬件的事件位          |
| `MR_UART_WAIT_TX_COMPLETE`                                    | 单次发送最后一个停止位已发出的事件位          |
| `MR_UART_WAIT_RX_ERROR` / `MR_UART_WAIT_TX_ERROR`             | 方向错误事件位                                |

**错误码**：`-MR_EINVAL`（配置字段非法或波特率为 0）、`-MR_ENOSYS`（平台不支持该有效配置、 循环模式，或该方向的流能力未实现）、
`-MR_EBUSY`（同方向忙时的非空读写、启动或配置修改）、 底层 `ops` 错误码透传（如 `-MR_EIO`），定义见
`mrX/include/mrX/libc/mr_errno.h`； 空指针、零长度 stream 缓冲区等编程错误在断言开启时由 `MR_ASSERT` 拦截。

# I2C 驱动移植指南

> 本文面向平台 backend 开发者，说明如何为新的 I2C 控制器实现 mrX 操作表。公共
> 消息、配置和事件 API 见 [I2C](i2c.md)。

## 1. 移植边界

I2C 公共层定义 controller 配置、message 数组、waitable 生命周期和事件位常量；
backend 负责总线状态机、START/RESTART/STOP、地址阶段、ACK/NACK、读写方向、错误
恢复以及可选的 Slave 模式。

| 关注点                                     | 负责方                    |
|--------------------------------------------|---------------------------|
| `mr_i2c_msg_t` 数组的语义与返回消息数      | 公共层与 backend 共同约定 |
| controller 配置校验与 `configuration` 回显 | backend                   |
| 总线时序、地址阶段、ACK/NACK               | backend                   |
| 事件 waitable 与事件位聚合                 | backend                   |
| Slave 收发队列（若支持）                   | backend                   |

平台实现通常位于：

```text
mrX/arch/<vendor>/<target>/platform/drivers/i2c/
```

当前 `mrX/arch/st/stm32h5/` 下 **没有** I2C backend；树内唯一的实现是
`mrX/drivers/i2c/i2c_gpio.c` 的 GPIO 软件 Master（见第 8 节），它同时是
“只实现部分能力”的参考样板。

## 2. 操作表

backend 实现 `mrX/include/mrX/drivers/i2c/mr_i2c.h:71` 的 `mr_i2c_ops_t`：

| 成员            | 签名要点                                                           | 必需 |
|-----------------|--------------------------------------------------------------------|------|
| `configure`     | `(drv, const mr_i2c_config_t *config)`                             | 是   |
| `configuration` | `(drv, mr_i2c_config_t *config)`                                   | 是   |
| `transfer`      | `(drv, const mr_i2c_msg_t *msgs, mr_size_t num)`，返回完成的消息数 | 否   |
| `slave_read`    | `(drv, void *buf, mr_size_t size)`，返回实际读取字节数             | 否   |
| `slave_write`   | `(drv, const void *buf, mr_size_t size)`，返回实际写入字节数       | 否   |
| `event_control` | `(drv, mr_uint32_t events, mr_bool_t enable)`                      | 否   |

`configure` 与 `configuration` 是必需能力。`transfer`/`slave_read`/
`slave_write`/`event_control` 都可留 `MR_NULL`，此时对应公共 API 返回
`-MR_ENOSYS`。Master 与 Slave 是 **独立**的可选能力：只做 Master 的 backend 完全
可以只提供 `transfer`，不必为了“配置合法”而伪造 Slave 路径。

## 3. 配置映射

`mr_i2c_config_t` 包含 `clock_rate`、`own_addr`、地址宽度和 `role`。backend 负责：

- 计算实际总线时序，并判断目标频率是否可实现；
- 配置 Master/Slave 角色；
- 配置 own address（Slave 场景）；
- 通过 `configuration` 返回 **实际采用**的值。

约定：

- 参数本身非法（例如地址超出所选地址宽度的范围）返回 `-MR_EINVAL`；参数合法但本
  平台不支持（例如只支持 Standard-mode 却收到更高的 `clock_rate`，或只支持 Master
  却收到 `MR_I2C_ROLE_SLAVE`）返回 `-MR_ENOSYS`。区分这两者是 backend 的责任。
- `clock_rate == 0` 可作为“禁用该 controller”的约定；此时后续 `transfer` 应返回
  `-MR_ENODEV`。若采用该约定，必须在平台文档中写明。
- 配置失败时不要把实例留在“半配置”状态：要么保持旧配置可用，要么明确进入禁用
  状态，让后续操作返回可预期的错误。

## 4. Master transfer

`mr_i2c_transfer()` 把 message 数组原样交给 backend。backend 逐条解释
（`mrX/include/mrX/drivers/i2c/mr_i2c.h:36-40`）：

```text
MR_I2C_MSG_WRITE
MR_I2C_MSG_READ
MR_I2C_MSG_STOP
MR_I2C_MSG_NOSTOP
MR_I2C_MSG_ADDR10
```

约定：

- 每条 message 以 START 或 repeated START 加地址阶段开始；连续 message 之间是否
  产生 repeated START、何时发 STOP，由 flag 与总线状态共同决定。
- 返回值是 **完成的消息数**。出现错误时不要把尚未完成的 message 计入结果；成功
  路径返回 `num`。
- `MR_I2C_MSG_STOP` 可以把一批 message 分成若干组，每组末尾发 STOP；最后一条
  message 若未显式 `STOP`，实现应在事务结束时自动 STOP。
- `MR_I2C_MSG_NOSTOP` 表示“跨 API 保持总线有效”。公共层没有跨调用的总线所有权，
  因此这个 flag 无法被正确实现：对最后一条 message 带 `MR_I2C_MSG_NOSTOP` 的请求，
  应返回 `-MR_ENOSYS` 而不是静默补一个 STOP。同一个 message 上
  `MR_I2C_MSG_STOP | MR_I2C_MSG_NOSTOP` 同时置位属于非法组合，返回 `-MR_EINVAL`。
- 只消费自己认识的 flag；不要额外引入一个“允许 flag mask”去因为未知位直接拒绝
  整个 message。
- 出错路径应尽力把总线恢复到空闲（产生 STOP 或至少释放 SDA/SCL），不要留下被
  持续拉低的总线。 **例外**：仲裁丢失时不应产生 STOP，因为总线已属于另一个
  Master。

## 5. Slave 模式与事件

公共 Slave 事件位共 6 个（`mr_i2c.h:28-33`）：

| 事件位                 | 位 | 典型含义                                     |
|------------------------|----|----------------------------------------------|
| `MR_I2C_WAIT_RX_START` | 0  | 被寻址且本次方向为接收                       |
| `MR_I2C_WAIT_RX_READY` | 1  | 已有 Master 写入的数据可由 `slave_read` 取走 |
| `MR_I2C_WAIT_TX_START` | 2  | 被寻址且本次方向为发送，需要首个 TX 字节     |
| `MR_I2C_WAIT_TX_READY` | 3  | 硬件需要更多 Slave TX 数据                   |
| `MR_I2C_WAIT_STOP`     | 4  | 本次总线事务结束                             |
| `MR_I2C_WAIT_ERROR`    | 5  | NACK、仲裁/总线错误或平台定义的失败          |

`event_control` 负责开关对应硬件来源；backend 用 `mr_i2c_event_raise()` 通知
waiter，分发是同步的，因此 ISR 中 raise 时回调也处于 ISR 上下文，不能阻塞。

Slave 实现需要自行决定：时钟拉伸策略、一次 `slave_read`/`slave_write` 搬运多少
字节、以及 RX/TX 缓冲或队列的容量与溢出策略。这些必须写进平台文档，因为它们直接
决定上层回调的时序要求（例如“回调必须在下一个字节收完前读走数据”）。

## 6. 错误与恢复

backend 应明确处理并在平台文档中列出：

- address NACK / data NACK；
- arbitration lost（不得再产生 STOP）；
- bus error；
- timeout（含时钟拉伸超时）；
- bus busy（START 前发现总线非空闲）；
- peripheral state machine 卡死后的 recovery（例如针对从机被复位在字节中间的情况
  发出固定数量的时钟脉冲再补一个 STOP）。

公共层不提供 bus lock、transaction queue 或自动 recovery。

## 7. HardTree 与安装

`MR_HT_I2C_INSTALL(_id, _priv, _ops)` 安装整颗 controller，不创建 I2C client/device
对象。HardTree 可描述 controller 实例、pins、clock/IRQ 以及平台需要的静态资源。

## 8. GPIO 软件 Master

`mrX/drivers/i2c/i2c_gpio.c` 是本驱动唯一的树内实现。它的编译条件是
`MR_USE_DRIVER_I2C && MR_USE_DRIVER_GPIO`，并且只有硬件模板里存在
compatible 为 `i2c_gpio` 的实例时才会生成代码；它 **没有**独立的 Kconfig 选项。

参考路径：

```text
mrX/drivers/i2c/i2c_gpio.c               # GPIO 软件 Master（唯一的树内实现）
mrX/test/drivers/i2c/i2c.c               # 公共契约与边界用例
mrX/include/mrX/drivers/i2c/mr_i2c.h     # ops 与 message flag 定义
```

值得按函数名对照的实现：`drv_transfer`（START/repeated START 与 STOP 分组）、
`i2c_gpio_validate_messages`（先整体校验再上总线）、`i2c_gpio_start`（总线忙检测）、
`i2c_gpio_scl_high`（时钟拉伸与 `-MR_ETIMEDOUT`）、`i2c_gpio_write_bit` 的仲裁丢失
检测、`i2c_gpio_bus_clear`（9 脉冲恢复）。

HardTree 属性（compat `i2c_gpio`）：

| 属性                 | 必需 | 说明                                 |
|----------------------|------|--------------------------------------|
| `scl`                | 是   | SCL 的 GPIO spec `[phandle, pin]`    |
| `sda`                | 是   | SDA 的 GPIO spec `[phandle, pin]`    |
| `stretch_timeout_us` | 否   | 时钟拉伸超时，单位微秒，缺省 `25000` |

两根信号线必须支持开漏输出和输入回读，且外部必须有上拉电阻。

当前实现的能力与限制（以表格形式列出，便于新 port 逐项确认）：

| 能力/约定           | `i2c_gpio` 现状                                                             | 新 port 需要确认                     |
|---------------------|-----------------------------------------------------------------------------|--------------------------------------|
| 角色                | 只支持 `MR_I2C_ROLE_MASTER`；`MR_I2C_ROLE_SLAVE` 返回 `-MR_ENOSYS`          | 是否实现 Slave 数据通路              |
| 事件                | `event_control` 为 `MR_NULL`，事件 API 返回 `-MR_ENOSYS`                    | 需要哪些事件位与上报时机             |
| Slave 数据通路      | `slave_read`/`slave_write` 为 `MR_NULL`，返回 `-MR_ENOSYS`                  | 是否需要 Slave 队列及其容量/溢出策略 |
| 速率                | 只支持 Standard-mode，`clock_rate` > 100000 返回 `-MR_ENOSYS`               | 硬件可达速率与时序计算方式           |
| 地址                | 7 位与 10 位地址、零长度寻址都可用                                          | own address 与地址宽度支持           |
| 总线事务            | repeated START、中间 `STOP` 分组、末尾自动 STOP 可用                        | START/RESTART/STOP 的硬件表达        |
| `MR_I2C_MSG_NOSTOP` | 无法跨调用保持总线，返回 `-MR_ENOSYS`                                       | 是否有跨调用的总线所有权模型         |
| 时钟拉伸            | 可检测，超时返回 `-MR_ETIMEDOUT`                                            | 硬件是否自动处理拉伸                 |
| 总线忙检测          | 正常 START 前发现 SDA 被拉低返回 `-MR_EBUSY`                                | 忙状态来源                           |
| 仲裁丢失            | 返回 `-MR_EAGAIN` 且**不**产生 STOP                                         | 硬件是否报告仲裁丢失及其恢复方式     |
| 卡死恢复            | `configure` 时等待 SCL 释放，SDA 被占用则 9 脉冲 bus clear 加 STOP          | 是否有等价的 recovery 手段           |
| 禁用语义            | `clock_rate = 0` 释放两个 GPIO 为无上下拉输入，`transfer` 返回 `-MR_ENODEV` | 是否采用同样的“0 即禁用”约定         |
| 并发                | 无 DMA、无跨调用总线锁；调用方需串行化同一 controller                       | 是否需要内部锁或队列                 |

实际可达速率受 GPIO backend 的重配置耗时、中断优先级和系统最大关中断时间限制，
100 kHz 并不总是可达。

**注意**：当前实现没有 Slave 模式。如果目标平台需要 Slave，应参考
`mrX/test/drivers/i2c/i2c.c` 的公共契约和 `mr_i2c_ops_t` 的四个可选成员自行实现，
不要假设 `i2c_gpio` 可以直接当从机使用。

任何一条“`i2c_gpio` 支持/不支持”都不能当作通用结论写进新 backend 的文档。

## 9. 移植验收清单

- `configure`/`configuration` 对 `clock_rate`、地址宽度与角色都能稳定往返，且
  `configuration` 返回实际采用值。
- 非法参数返回 `-MR_EINVAL`，合法但不支持的能力返回 `-MR_ENOSYS`，两者不混用。
- 未提供的可选成员保持 `MR_NULL`，公共层据此返回 `-MR_ENOSYS`。
- 每个 message 正确地以 START 或 repeated START 开始，地址宽度与 flag 一致。
- STOP 分组与末尾自动 STOP 正确；`MR_I2C_MSG_NOSTOP` 在最后一条 message 上返回
  `-MR_ENOSYS`；`STOP | NOSTOP` 同时置位返回 `-MR_EINVAL`。
- 成功返回完成的消息数，失败时不把未完成的消息计入。
- 出错路径恢复总线；仲裁丢失不产生 STOP。
- 若支持 Slave：事件位语义、缓冲区容量、NACK 与欠载策略都在平台文档中写明，且
  ISR 中 raise 的回调被明确要求保持短小。
- HardTree 的 `scl`/`sda` 指向的引脚确实支持开漏输出与输入回读，外部上拉已到位。

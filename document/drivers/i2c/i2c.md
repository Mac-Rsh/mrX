# I2C 驱动（I2C）

> I2C 驱动提供控制器级的 Master 消息传输、Slave 数据通路和 waitable 事件接口。
> 公共层保持轻量：负责驱动对象、基础参数和能力检查，具体总线时序与硬件能力由 backend 实现。

| 项       | 说明                                                                                            |
|----------|-------------------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_DRIVER_I2C`（默认关闭）                                                                 |
| 头文件   | `<mrX/drivers/i2c/mr_i2c.h>`，也通过 `<mrX.h>` 条件引入                                         |
| 依赖模块 | [驱动框架 Driver](../driver.md)、[等待队列 Wait](../../kernel/wait.md)；动态对象释放使用 Malloc |
| 参考测试 | `mrX/test/drivers/i2c/i2c.c`                                                                    |

---

## 用户指南

### 1. 模块概述

I2C 公共层把控制器能力分成三部分：

- 配置：`mr_i2c_configure` / `mr_i2c_configuration`；
- Master：`mr_i2c_transfer` 传入消息数组；
- Slave：`mr_i2c_slave_read` / `mr_i2c_slave_write` 与 waitable 事件。

公共层不负责设备地址所有权、寄存器协议、传输排队、DMA/IRQ 选择或总线恢复策略。这些能力由 backend 或更上层设备驱动承担。

### 2. 配置与数据结构

配置结构：

```c
typedef enum mr_i2c_role {
    MR_I2C_ROLE_MASTER,
    MR_I2C_ROLE_SLAVE,
} mr_i2c_role_t;

typedef struct mr_i2c_config {
    mr_uint32_t clock_rate;
    mr_uint16_t own_addr;
    mr_uint8_t addr10: 1;
    mr_uint8_t role: 1;
} mr_i2c_config_t;
```

字段只表达请求，不由公共层校验组合是否合法：

- `clock_rate`：I2C 时钟速率字段；
- `own_addr`：本控制器地址字段，通常用于 Slave；
- `addr10`：本控制器地址是否使用 10-bit 形式；
- `role`：`MR_I2C_ROLE_MASTER` 或 `MR_I2C_ROLE_SLAVE`。

`mr_i2c_configure` 会断言 `config != MR_NULL` 和 `ops->configure != MR_NULL`，然后直接调用 backend。`mr_i2c_configuration`
同理。

Master 消息结构：

```c
typedef struct mr_i2c_msg {
    mr_uint16_t addr;
    mr_uint16_t flags;
    mr_uint8_t *buf;
    mr_size_t len;
} mr_i2c_msg_t;
```

当前公共 flag：

```c
#define MR_I2C_MSG_WRITE    0U
#define MR_I2C_MSG_READ     (1U << 0)
#define MR_I2C_MSG_STOP     (1U << 1)
#define MR_I2C_MSG_NOSTOP   (1U << 2)
#define MR_I2C_MSG_ADDR10   (1U << 3)
```

这里没有 `MR_I2C_MSG_FLAG_MASK`。公共层不会用白名单拒绝未知 flag bit，也不会检查地址范围或 STOP/NOSTOP 组合；`flags` 与
`addr` 会原样交给 backend。这样 backend 可以按控制器能力解释公共 flag，也可以忽略自己不支持或不认识的扩展 bit。

### 3. Master 传输

```c
mr_ssize_t mr_i2c_transfer(mr_driver_t *drv,
                           const mr_i2c_msg_t *msgs,
                           mr_size_t num);
```

公共层行为只有：

1. 校验驱动类型和 ops 指针；
2. 允许 `num == 0`，此时直接返回 0；
3. 非零消息数要求 `msgs != MR_NULL`；
4. 每个消息允许 `len == 0`，但 `len != 0` 时要求 `buf != MR_NULL`；
5. `ops->transfer == MR_NULL` 时返回 `-MR_ENOSYS`；
6. 其余情况直接调用 `ops->transfer(drv, msgs, num)`。

头文件把成功返回值定义为“完成的 message 数”。是否支持 repeated START、10-bit 地址、STOP/NOSTOP、零长度 address-only
transaction，以及错误发生后的总线恢复行为，都由 backend 实现。

示例：一次写后读请求：

```c
mr_uint8_t reg = 0x10;
mr_uint8_t data[2];
mr_i2c_msg_t msgs[] = {
    {
        .addr = 0x50,
        .flags = MR_I2C_MSG_WRITE,
        .buf = &reg,
        .len = sizeof(reg),
    },
    {
        .addr = 0x50,
        .flags = MR_I2C_MSG_READ,
        .buf = data,
        .len = sizeof(data),
    },
};

mr_ssize_t ret = mr_i2c_transfer(drv, msgs, 2);
```

消息之间实际产生 START、repeated START、STOP 的方式由 backend 根据 flags 实现。

### 4. Slave 数据通路

```c
mr_ssize_t mr_i2c_slave_read(mr_driver_t *drv, void *buf, mr_size_t size);
mr_ssize_t mr_i2c_slave_write(mr_driver_t *drv, const void *buf, mr_size_t size);
```

这两个接口不主动发起 Master transaction，而是把当前 Slave controller 的数据收发能力暴露给上层。

公共层规则：

- `size == 0` 时直接返回 0；
- 非零长度要求 `buf != MR_NULL`；
- 对应 callback 缺失时返回 `-MR_ENOSYS`；
- 其他返回值直接来自 backend。

一次调用对应 FIFO、数据寄存器还是整段缓冲区，由具体控制器 backend 决定。

### 5. 事件

公共事件位：

```c
#define MR_I2C_WAIT_RX_START  (1U << 0)
#define MR_I2C_WAIT_RX_READY  (1U << 1)
#define MR_I2C_WAIT_TX_START  (1U << 2)
#define MR_I2C_WAIT_TX_READY  (1U << 3)
#define MR_I2C_WAIT_STOP      (1U << 4)
#define MR_I2C_WAIT_ERROR     (1U << 5)
```

接口：

```c
mr_i2c_event_attach(drv, entry);
mr_i2c_event_enable(drv, events);
mr_i2c_event_disable(drv, events);
mr_i2c_event_raise(drv, events);
```

`mr_i2c_event_attach` 是框架级 waitable 挂接，不依赖 backend callback。`event_enable` / `event_disable` 通过可选的
`ops->event_control` 控制来源；缺少该 callback 时返回 `-MR_ENOSYS`，公共层不会校验 event mask。

`mr_i2c_event_raise` 调用：

```c
mr_waitable_raise(&i2c_data->event, (mr_wait_event_t)events, MR_NULL);
```

因此没有额外 payload，并且 waiter 会同步运行在 raise 的调用上下文。如果 backend 从硬件 ISR 调用 `mr_i2c_event_raise`
，waiter 也会在该 ISR 上下文执行。

对于 Slave backend，`RX_START/RX_READY/TX_START/TX_READY/STOP/ERROR` 可以作为协议时序通知；具体在哪个硬件状态点 raise、是否使用
clock stretching、一次 `slave_read/write` 处理多少数据，仍由 backend 定义。

### 6. 生命周期

模型数据只有一个事件对象：

```c
typedef struct mr_i2c_data {
    mr_waitable_t event;
} mr_i2c_data_t;
```

setup 时 `i2c_init` 初始化 waitable，shutdown 时 `i2c_deinit` 删除 waitable。动态创建使用 dynamic clazz，最终 release 会：

```c
mr_free(MR_DRIVER_DATA(drv));
```

因此传给 `mr_i2c_create` 的 `data` 必须符合这一所有权规则；`priv` 和 `ops` 不由 I2C release 自动释放。

### 7. API 参考

| API                         | 说明                                            |
|-----------------------------|-------------------------------------------------|
| `MR_I2C_DEFINE`             | 定义静态 I2C 驱动                               |
| `MR_I2C_INSTALL`            | 定义并安装静态 I2C 驱动                         |
| `mr_i2c_init`               | 初始化调用方提供的驱动对象                      |
| `mr_i2c_create`             | 创建动态 I2C 驱动                               |
| `mr_i2c_del`                | 删除 I2C 驱动                                   |
| `mr_i2c_configure`          | 配置 I2C，直接透传必需 `configure` callback     |
| `mr_i2c_configuration`      | 获取配置，直接透传必需 `configuration` callback |
| `mr_i2c_transfer`           | Master message 数组传输，可选能力               |
| `mr_i2c_slave_read`         | Slave RX 数据读取，可选能力                     |
| `mr_i2c_slave_write`        | Slave TX 数据写入，可选能力                     |
| `mr_i2c_event_attach`       | 挂接 waiter                                     |
| `mr_i2c_event_enable`       | 请求 backend 启用事件来源                       |
| `mr_i2c_event_disable`      | 请求 backend 禁用事件来源                       |
| `mr_i2c_event_raise`        | 同步 raise I2C wait event                       |
| `mr_i2c_get` / `mr_i2c_put` | 驱动引用管理                                    |
| `MR_HT_I2C_INSTALL`         | HardTree 安装整颗 I2C controller                |

### 8. 常见问题

**Q：为什么公共层不检查 7-bit/10-bit 地址范围？**

I2C core 当前只负责统一数据结构和调用边界。地址是否合法、控制器是否支持某种地址模式，由 backend 判断；这样不会在 core
中提前封死控制器扩展能力。

**Q：未知 message flag 会返回 `-MR_EINVAL` 吗？**

不会。公共层没有 flag mask，也没有未知 flag 校验。backend 可以解释、忽略或按自己的能力处理这些 bit。

**Q：为什么 `mr_i2c_transfer` 会返回 `-MR_ENOSYS`？**

该 driver 的 `ops->transfer` 为空。这允许一个 backend 只实现 Slave 能力而不实现 Master transfer。

**Q：为什么 `mr_i2c_slave_read/write` 也可能返回 `-MR_ENOSYS`？**

Slave 数据路径同样是可选能力；纯 Master controller 可以不提供这些 callback。

**Q：event attach 成功，但 event enable 返回 `-MR_ENOSYS`？**

waitable 属于 I2C core，所以 attach 可以工作；事件源控制属于 backend，如果没有 `event_control` 就会返回 `-MR_ENOSYS`。

---

## 开发者指南

### 9. 架构

```text
upper device/protocol layer
          |
          | mr_i2c_configure / transfer / slave_* / event_*
          v
+-------------------------------+
| I2C common layer              |
| - driver type/assert          |
| - optional capability gate    |
| - waitable event object       |
+---------------+---------------+
                |
                | mr_i2c_ops_t
                v
+-------------------------------+
| controller backend            |
| polling / IRQ / DMA / FIFO    |
+-------------------------------+
```

公共层不建立额外 bus/device 对象，也不管理上层设备地址所有权。

### 10. `mr_i2c_ops_t`

```c
typedef struct mr_i2c_ops {
    mr_err_t (*configure)(const mr_driver_t *drv,
                          const mr_i2c_config_t *config);
    mr_err_t (*configuration)(const mr_driver_t *drv,
                              mr_i2c_config_t *config);
    mr_ssize_t (*transfer)(const mr_driver_t *drv,
                           const mr_i2c_msg_t *msgs,
                           mr_size_t num);
    mr_ssize_t (*slave_read)(const mr_driver_t *drv,
                             void *buf,
                             mr_size_t size);
    mr_ssize_t (*slave_write)(const mr_driver_t *drv,
                              const void *buf,
                              mr_size_t size);
    mr_err_t (*event_control)(const mr_driver_t *drv,
                              mr_uint32_t events,
                              mr_bool_t enable);
} mr_i2c_ops_t;
```

`configure` 与 `configuration` 是当前公共 API 会直接 `MR_ASSERT` 的基础 callback。`transfer`、`slave_read`、`slave_write`、
`event_control` 都按可选能力处理，缺失时相应公共 API 返回 `-MR_ENOSYS`。

### 11. 实现注意事项

- `mr_i2c_transfer` 不会替 backend 检查 message address、flag 组合或未知 flag；backend 不应假定这些已经被 core 清洗。
- message descriptor 是 `const mr_i2c_msg_t *`，但 READ message 可以通过 `msg->buf` 写入接收数据。
- 零长度 message 是公共层允许的输入；是否有实际总线意义由 backend 决定。
- Slave 事件通过 waitable 同步分发。如果 backend 在 IRQ 中 raise，回调会直接进入同一 IRQ 调用链，因此 backend
  设计应考虑这种重入与执行时间。
- 公共层没有 bus lock、transaction queue 或自动 recovery；需要这些能力时放在 backend 或更上层。

### 12. HardTree

```c
#define MR_HT_I2C_INSTALL(_id, _priv, _ops) \
    static mr_i2c_data_t MR_CAT(__mr_i2c_data_, _id) = {0}; \
    MR_HT_DRIVER_INSTALL(_id, MR_DRIVER_CLAZZES_STATIK(i2c), \
                         MR_DRIVER_TYPE_I2C, \
                         &MR_CAT(__mr_i2c_data_, _id), \
                         _priv, _ops)
```

它只安装整颗控制器，没有 I2C client/device spec。

平台 controller backend 的总线状态机、Master transfer、Slave 事件、HardTree 与验收见 [I2C 驱动移植指南](porting.md)。

### 13. 源码导读

| 位置                                   | 职责                                               |
|----------------------------------------|----------------------------------------------------|
| `mrX/include/mrX/drivers/i2c/mr_i2c.h` | 配置、消息、事件、ops 与公共 inline API            |
| `mrX/drivers/i2c/i2c.c`                | configure/configuration/transfer 与 clazz 生命周期 |
| `mrX/include/mrX/drivers/i2c/Kconfig`  | `MR_USE_DRIVER_I2C` 配置                           |
| `mrX/include/mrX.h`                    | I2C 条件 include                                   |

### 14. 错误与断言

公共层明确产生的错误主要是：

- `-MR_ENOSYS`：可选 `transfer`、`slave_read`、`slave_write` 或 `event_control` 没有实现；
- 其他 configure/transfer/slave/backend 错误码直接透传。

驱动类型、必需 callback、空指针与非零长度 buffer 等编程约束通过 `MR_ASSERT` / `MR_DRIVER_ASSERT` 检查，而不是统一转换为
`-MR_EINVAL`。

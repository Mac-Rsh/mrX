# I2C 驱动（I2C）

提供 I2C 控制器级的 Master 消息数组传输、Slave 数据收发通路及 waitable
事件接口。公共层负责驱动对象生命周期与基础参数检查，具体总线时序、地址处理与错误恢复由
backend 实现。

| 项目   | 说明                                                                                                                          |
|--------|-------------------------------------------------------------------------------------------------------------------------------|
| 功能宏 | `MR_USE_DRIVER_I2C`                                                                                                           |
| 头文件 | `<mrX/drivers/i2c/mr_i2c.h>`                                                                                                  |
| 依赖   | [驱动框架 Driver](../driver.md)、[等待队列 Wait](../../kernel/wait.md)、[动态内存 Malloc](../../libc/malloc.md)（仅动态创建） |
| 测试   | `test/drivers/i2c/i2c.c`                                                                                                      |

## 配置

| 配置项              | 类型 | 默认值 | 说明                                      |
|---------------------|------|--------|-------------------------------------------|
| `MR_USE_DRIVER_I2C` | bool | n      | 启用 I2C 控制器驱动，依赖 `MR_USE_DRIVER` |

## 快速上手

配置为主机模式并执行一次“写寄存器地址 + 读数据”复合传输：

```c
#include <mrX/drivers/i2c/mr_i2c.h>

void i2c_master_example(mr_driver_t *drv) {
    mr_i2c_config_t cfg = {
        .clock_rate = 100000,
        .own_addr = 0,
        .addr10 = 0,
        .role = MR_I2C_ROLE_MASTER,
    };
    mr_uint8_t reg = 0x10;
    mr_uint8_t data[2];
    mr_i2c_msg_t msgs[] = {
        {.addr = 0x50, .flags = MR_I2C_MSG_WRITE, .buf = &reg, .len = sizeof(reg)},
        {.addr = 0x50, .flags = MR_I2C_MSG_READ,  .buf = data, .len = sizeof(data)},
    };

    if (mr_i2c_configure(drv, &cfg) != 0) {
        return;
    }
    /* 成功返回已完成的消息数（此处为 2） */
    mr_ssize_t ret = mr_i2c_transfer(drv, msgs, 2);
    (void)ret;
}
```

## 核心机制与约束

- **配置模型**：`mr_i2c_config_t` 包含 `clock_rate`、`own_addr`（Slave 本机地址）、
  `addr10`（1 位：是否 10-bit 地址）和 `role`（`MR_I2C_ROLE_MASTER` /
  `MR_I2C_ROLE_SLAVE`）。公共层不校验字段组合，直接透传给 `ops->configure` /
  `ops->configuration`。
- **Master 消息传输**：`mr_i2c_transfer` 接受 `mr_i2c_msg_t` 数组（`addr`、`flags`、
  `buf`、`len`）。`num == 0` 时直接返回 0；`ops->transfer` 为空时返回 `-MR_ENOSYS`
  。公共层不校验 `flags` 掩码或地址范围，原样交给 backend，成功返回已完成的消息数，失败返回负错误码。

| 消息标志            | 值        | 说明                    |
|---------------------|-----------|-------------------------|
| `MR_I2C_MSG_WRITE`  | `0U`      | 写传输（默认方向）      |
| `MR_I2C_MSG_READ`   | `1U << 0` | 读传输                  |
| `MR_I2C_MSG_STOP`   | `1U << 1` | 本条消息结束后产生 STOP |
| `MR_I2C_MSG_NOSTOP` | `1U << 2` | 本条消息后不产生 STOP   |
| `MR_I2C_MSG_ADDR10` | `1U << 3` | 使用 10-bit 从机地址    |

- **Slave 数据通路**：`mr_i2c_slave_read` / `mr_i2c_slave_write` 为可选能力（未实现返回
  `-MR_ENOSYS`），`size == 0` 时直接返回 0。缓冲策略、时钟拉伸及单次读写粒度由具体
  Slave backend 决定。
- **总线并发与生命周期**：公共层不提供总线锁、事务队列或设备地址所有权管理，多设备共享同一控制器时需由调用方或
  backend 串行化访问。`setup` 时初始化 `mr_i2c_data_t.event`，`shutdown` 时销毁。
- **事件机制**：`mr_i2c_event_attach` 挂接 waiter 到框架 `waitable`；
  `event_enable` / `event_disable` 透传给可选的 `ops->event_control`（未实现返回
  `-MR_ENOSYS`）。`mr_i2c_event_raise` 以无 payload（`MR_NULL`）同步通知 waiter，若在
  ISR 中触发则回调也运行在 ISR 上下文。

| 事件常量               | 位值      | 语义                       |
|------------------------|-----------|----------------------------|
| `MR_I2C_WAIT_RX_START` | `1U << 0` | 从机接收起始（被寻址为写） |
| `MR_I2C_WAIT_RX_READY` | `1U << 1` | 从机接收数据就绪           |
| `MR_I2C_WAIT_TX_START` | `1U << 2` | 从机发送起始（被寻址为读） |
| `MR_I2C_WAIT_TX_READY` | `1U << 3` | 从机发送数据就绪           |
| `MR_I2C_WAIT_STOP`     | `1U << 4` | 事务结束（STOP）           |
| `MR_I2C_WAIT_ERROR`    | `1U << 5` | 总线/NACK/仲裁错误         |

## API 速查

| API                                                 | 说明                                                      |
|-----------------------------------------------------|-----------------------------------------------------------|
| `MR_I2C_DEFINE(_name, _data, _priv, _ops)`          | 编译期定义静态 I2C 驱动（不注册）                         |
| `MR_I2C_INSTALL(_name, _data, _priv, _ops, _level)` | 编译期定义并注册 I2C 驱动                                 |
| `MR_HT_I2C_INSTALL(_id, _priv, _ops)`               | 通过 HardTree 节点自动定义零初始化 `data` 并安装 I2C 实例 |
| `mr_i2c_init(drv, data, priv, ops)`                 | 运行时初始化静态 I2C 驱动                                 |
| `mr_i2c_create(data, priv, ops)`                    | 堆上动态创建 I2C 驱动，失败返回 `MR_NULL`                 |
| `mr_i2c_del(drv)`                                   | 注销并删除 I2C 驱动                                       |
| `mr_i2c_get(drv)` / `mr_i2c_put(drv)`               | 引用计数加一 / 减一                                       |
| `mr_i2c_configure(drv, config)`                     | 写入 I2C 控制器配置                                       |
| `mr_i2c_configuration(drv, config)`                 | 读取当前 I2C 控制器配置                                   |
| `mr_i2c_transfer(drv, msgs, num)`                   | 执行 Master 消息数组传输，返回完成的消息数或负错误码      |
| `mr_i2c_slave_read(drv, buf, size)`                 | 读取 Slave 接收数据                                       |
| `mr_i2c_slave_write(drv, buf, size)`                | 写入 Slave 发送数据                                       |
| `mr_i2c_event_attach(drv, entry)`                   | 挂载 waiter 到事件队列                                    |
| `mr_i2c_event_enable(drv, events)`                  | 使能指定事件掩码                                          |
| `mr_i2c_event_disable(drv, events)`                 | 禁用指定事件掩码                                          |
| `mr_i2c_event_raise(drv, events)`                   | 同步触发事件通知（供 backend 调用）                       |

# 硬件定时器（TMR）

硬件定时器（TMR）驱动提供周期/单次计数配置、启停控制、周期边界硬件信号输出（
`MR_TMR_SIGNAL_PERIOD`）与超时事件（`MR_TMR_WAIT_TIMEOUT`）订阅接口。

| 项目   | 说明                                                                         |
|--------|------------------------------------------------------------------------------|
| 功能宏 | `MR_USE_DRIVER_TMR`（依赖 `MR_USE_DRIVER`）                                  |
| 头文件 | `<mrX/drivers/tmr/mr_tmr.h>`                                                 |
| 依赖   | [驱动框架（Driver）](../driver.md)、[等待队列（Wait）](../../kernel/wait.md) |
| 测试   | `test/drivers/tmr/tmr.c`                                                     |

## 配置

| 配置项              | 类型 | 默认值 | 说明                                                    |
|---------------------|------|--------|---------------------------------------------------------|
| `MR_USE_DRIVER_TMR` | bool | n      | 启用硬件定时器驱动（`include/mrX/drivers/tmr/Kconfig`） |

## 快速上手

```c
#include <mrX/drivers/tmr/mr_tmr.h>

void tmr_example(mr_driver_t *timer) {
    mr_tmr_config_t cfg = {
        .period = 500U, /* 物理单位与有效范围由平台 backend 定义 */
        .mode = MR_TMR_MODE_PERIODIC,
    };

    mr_driver_setup(timer);
    mr_tmr_configure(timer, &cfg);
    mr_tmr_signal_enable(timer, MR_TMR_SIGNAL_PERIOD); /* 可选：开启硬件周期触发信号 */
    mr_tmr_start(timer);
}
```

若需要超时软件回调通知：

```c
static void tmr_timeout_cb(mr_waiter_t *waiter, void *param,
                           mr_wait_event_t event, const void *data) {
    MR_IGNORE(waiter);
    MR_IGNORE(param);
    MR_IGNORE(event);
    MR_IGNORE(data); /* mr_tmr_event_raise 传入的负载固定为 MR_NULL */
}

void tmr_event_example(mr_driver_t *timer) {
    static mr_waiter_t waiter;

    mr_waiter_init(&waiter, tmr_timeout_cb, MR_NULL, MR_TMR_WAIT_TIMEOUT);
    mr_tmr_event_attach(timer, &waiter);
    mr_tmr_event_enable(timer, MR_TMR_WAIT_TIMEOUT);
}
```

## 核心机制与约束

- **周期单位由 backend 决定**：`mr_tmr_config_t` 包含 `period`（`mr_uint64_t`）和
  `mode`（`MR_TMR_MODE_PERIODIC` 或 `MR_TMR_MODE_ONESHOT`）。公共层不规定 `period`
  的物理单位、量化方式或有效范围，也不校验字段值，使用前需查阅目标平台文档。
- **忙状态门控**：`mr_tmr_configure` 与 `mr_tmr_start` 会先调用 `ops->is_busy`
  ，若定时器正在运行则直接返回 `-MR_EBUSY`；`mr_tmr_stop` 在定时器已空闲时直接返回
  `0`，不会重复调用 `ops->stop`。`mr_tmr_configuration` 不检查忙状态，直接从
  backend 回读当前配置。
- **硬件信号与软件事件独立**：`MR_TMR_SIGNAL_PERIOD` 控制周期边界硬件输出信号（如触发
  ADC/DAC/DMA），`MR_TMR_WAIT_TIMEOUT` 控制软件等待队列事件；两者互相独立，开启硬件信号不会使能中断或通知
  waiter，且跨外设（如先武装消费者再启动 TMR）的启停顺序由上层负责。若 backend 未实现
  `signal_control` 或 `event_control`，对应使能/禁用接口返回 `-MR_ENOSYS`。
- **生命周期边界**：`mr_driver_setup` 仅初始化 `mr_tmr_data_t` 中的 `event` 等待队列，
  `shutdown` 仅删除该等待队列；框架不会在 setup/shutdown/del 时自动配置或停止硬件定时器。调用
  `mr_tmr_create` 时传入的 `mr_tmr_data_t` 必须由 `mr_malloc`
  分配，删除驱动前若硬件仍在运行需先显式调用 `mr_tmr_stop`。

## API 速查

### 实例生命周期与宏

| API                                                 | 说明                               |
|-----------------------------------------------------|------------------------------------|
| `MR_TMR_DEFINE(_name, _data, _priv, _ops)`          | 静态定义 TMR 驱动实例              |
| `MR_TMR_INSTALL(_name, _data, _priv, _ops, _level)` | 静态定义并按初始化级别自动安装驱动 |
| `MR_HT_TMR_INSTALL(_id, _priv, _ops)`               | 通过 HardTree 安装静态实例         |
| `mr_tmr_init(drv, data, priv, ops)`                 | 初始化静态 TMR 驱动                |
| `mr_tmr_create(data, priv, ops)`                    | 动态创建 TMR 驱动                  |
| `mr_tmr_del(drv)`                                   | 删除 TMR 驱动                      |
| `mr_tmr_get(drv)` / `mr_tmr_put(drv)`               | 增加 / 减少驱动引用计数            |

### 配置、控制与事件

| API                                                                        | 说明                                                       |
|----------------------------------------------------------------------------|------------------------------------------------------------|
| `mr_tmr_configure(drv, config)`                                            | 配置周期与模式（运行中返回 `-MR_EBUSY`）                   |
| `mr_tmr_configuration(drv, config)`                                        | 回读当前定时器配置                                         |
| `mr_tmr_start(drv)`                                                        | 启动定时器（已运行返回 `-MR_EBUSY`）                       |
| `mr_tmr_stop(drv)`                                                         | 停止定时器（已空闲直接返回 `0`）                           |
| `mr_tmr_is_busy(drv, busy)`                                                | 查询定时器是否正在运行                                     |
| `mr_tmr_signal_enable(drv, signal)` / `mr_tmr_signal_disable(drv, signal)` | 启停硬件输出信号（如 `MR_TMR_SIGNAL_PERIOD`）              |
| `mr_tmr_event_attach(drv, entry)`                                          | 将 `mr_waiter_t` 挂接到实例等待队列                        |
| `mr_tmr_event_enable(drv, events)` / `mr_tmr_event_disable(drv, events)`   | 启停事件中断源（如 `MR_TMR_WAIT_TIMEOUT`）                 |
| `mr_tmr_event_raise(drv, events)`                                          | 触发事件通知（供 backend 在 ISR 中调用，负载为 `MR_NULL`） |

## 内部实现

TMR 驱动类在 `drivers/tmr/tmr.c` 中注册了 `init`（初始化 `mr_tmr_data_t.event`）、
`deinit`（销毁 `mr_tmr_data_t.event`）与 `release`（释放动态分配的
`mr_tmr_data_t`）。`mr_tmr_configure` 与 `mr_tmr_configuration` 在
`drivers/tmr/tmr.c` 中实现，启停、忙查询、信号与事件控制均在
`include/mrX/drivers/tmr/mr_tmr.h` 中内联实现。

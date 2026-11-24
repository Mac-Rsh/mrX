# 看门狗定时器（WDT）

看门狗定时器（WDT）驱动提供窗口/超时配置、启停、喂狗、忙状态查询以及提前预警/超时事件（
`MR_WDT_WAIT_PRETIMEOUT` / `MR_WDT_WAIT_TIMEOUT`）订阅接口。

| 项目   | 说明                                                                         |
|--------|------------------------------------------------------------------------------|
| 功能宏 | `MR_USE_DRIVER_WDT`（依赖 `MR_USE_DRIVER`）                                  |
| 头文件 | `<mrX/drivers/wdt/mr_wdt.h>`                                                 |
| 依赖   | [驱动框架（Driver）](../driver.md)、[等待队列（Wait）](../../kernel/wait.md) |
| 测试   | `test/drivers/wdt/wdt.c`                                                     |

## 配置

| 配置项              | 类型 | 默认值 | 说明                                                |
|---------------------|------|--------|-----------------------------------------------------|
| `MR_USE_DRIVER_WDT` | bool | n      | 启用看门狗驱动（`include/mrX/drivers/wdt/Kconfig`） |

## 快速上手

```c
#include <mrX/drivers/wdt/mr_wdt.h>

void wdt_example(mr_driver_t *wdt) {
    mr_wdt_config_t cfg = {
        .feed_min = 0U,     /* 物理单位、0 值含义及窗口支持由平台 backend 定义 */
        .feed_max = 1000U,
        .pretimeout = 100U,
    };

    mr_driver_setup(wdt);
    mr_wdt_configure(wdt, &cfg);
    mr_wdt_start(wdt);

    /* 在有效工作完成后喂狗 */
    mr_wdt_feed(wdt);
}
```

## 核心机制与约束

- **时间参数由 backend 解释**：`mr_wdt_config_t` 包含 `feed_min`、`feed_max` 与
  `pretimeout` 三个 `mr_uint64_t` 字段。公共层不规定时间单位、0
  值含义或窗口数学关系，也不校验字段值；具体量化精度与回读语义需查阅目标平台文档。
- **忙状态门控**：`mr_wdt_configure`、`mr_wdt_start`、`mr_wdt_stop` 与
  `mr_wdt_feed` 均会先调用 `ops->is_busy` 检查运行态：
    - `mr_wdt_configure` 与 `mr_wdt_start` 在看门狗已运行时返回 `-MR_EBUSY`。
    - `mr_wdt_feed` 在看门狗未运行时返回 `-MR_EINVAL`。
    - `mr_wdt_stop` 在已停止时直接返回 `0`；若看门狗正在运行且 backend 未实现
      `stop`（硬件不可软件停止，如独立看门狗），返回 `-MR_ENOSYS`。
- **事件通知**：`MR_WDT_WAIT_PRETIMEOUT` 与 `MR_WDT_WAIT_TIMEOUT`
  为公共事件位，具体是否支持中断通知或超时后直接硬件复位取决于芯片与 backend。若
  backend 未实现 `event_control`，`mr_wdt_event_enable`/`disable` 返回
  `-MR_ENOSYS`。
- **生命周期边界**：`mr_driver_setup` 仅初始化 `mr_wdt_data_t` 中的 `event` 等待队列，
  `shutdown` 仅删除该等待队列；框架不会在 setup/shutdown/del 时自动配置、启动或停止硬件看门狗。调用
  `mr_wdt_create` 时传入的 `mr_wdt_data_t` 必须由 `mr_malloc` 分配。

## API 速查

### 实例生命周期与宏

| API                                                 | 说明                               |
|-----------------------------------------------------|------------------------------------|
| `MR_WDT_DEFINE(_name, _data, _priv, _ops)`          | 静态定义 WDT 驱动实例              |
| `MR_WDT_INSTALL(_name, _data, _priv, _ops, _level)` | 静态定义并按初始化级别自动安装驱动 |
| `MR_HT_WDT_INSTALL(_id, _priv, _ops)`               | 通过 HardTree 安装静态实例         |
| `mr_wdt_init(drv, data, priv, ops)`                 | 初始化静态 WDT 驱动                |
| `mr_wdt_create(data, priv, ops)`                    | 动态创建 WDT 驱动                  |
| `mr_wdt_del(drv)`                                   | 删除 WDT 驱动                      |
| `mr_wdt_get(drv)` / `mr_wdt_put(drv)`               | 增加 / 减少驱动引用计数            |

### 配置、控制与事件

| API                                                                      | 说明                                                               |
|--------------------------------------------------------------------------|--------------------------------------------------------------------|
| `mr_wdt_configure(drv, config)`                                          | 配置喂狗窗口与预警时间（运行中返回 `-MR_EBUSY`）                   |
| `mr_wdt_configuration(drv, config)`                                      | 回读当前看门狗配置                                                 |
| `mr_wdt_start(drv)`                                                      | 启动看门狗（已运行返回 `-MR_EBUSY`）                               |
| `mr_wdt_stop(drv)`                                                       | 停止看门狗（未运行返回 `0`，不可停止返回 `-MR_ENOSYS`）            |
| `mr_wdt_feed(drv)`                                                       | 喂狗刷新计数器（未运行返回 `-MR_EINVAL`）                          |
| `mr_wdt_is_busy(drv, busy)`                                              | 查询看门狗是否正在计时                                             |
| `mr_wdt_event_attach(drv, entry)`                                        | 将 `mr_waiter_t` 挂接到实例等待队列                                |
| `mr_wdt_event_enable(drv, events)` / `mr_wdt_event_disable(drv, events)` | 启停事件中断源（`MR_WDT_WAIT_PRETIMEOUT` / `MR_WDT_WAIT_TIMEOUT`） |
| `mr_wdt_event_raise(drv, events)`                                        | 触发事件通知（供 backend 在 ISR 中调用，负载为 `MR_NULL`）         |

## 内部实现

WDT 驱动类在 `drivers/wdt/wdt.c` 中注册了 `init`（初始化 `mr_wdt_data_t.event`）、
`deinit`（销毁 `mr_wdt_data_t.event`）与 `release`（释放动态分配的
`mr_wdt_data_t`）。`mr_wdt_configure` 与 `mr_wdt_configuration` 在
`drivers/wdt/wdt.c` 中实现，启停、喂狗、忙查询与事件接口均在
`include/mrX/drivers/wdt/mr_wdt.h` 中内联实现。

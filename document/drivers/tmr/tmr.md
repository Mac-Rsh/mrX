# 硬件定时器（TMR）

> 提供基础硬件定时器配置、启停、硬件周期信号与 waitable 事件接口。

| 项目     | 说明                         |
|----------|------------------------------|
| 功能宏   | `MR_USE_DRIVER_TMR`          |
| 头文件   | `<mrX/drivers/tmr/mr_tmr.h>` |
| 依赖模块 | [驱动框架](../driver.md)     |
| 参考测试 | `mrX/test/drivers/tmr/tmr.c` |

---

## 用户指南

### 1. 模块概述

每个 TMR 驱动实例表示一个硬件定时器。公共配置包含 `period` 和 `mode`，模式枚举为
`MR_TMR_MODE_PERIODIC` 与 `MR_TMR_MODE_ONESHOT`。独立的 signal 接口当前定义
`MR_TMR_SIGNAL_PERIOD`，用于控制周期边界硬件信号。

公共层不会把 signal 自动连接到 ADC/DAC/DMA。信号的硬件输出和消费者输入映射由
HTS/backend
固定，应用只负责使能生产者并按顺序启动消费者。

公共行为与返回码可参考 `test/drivers/tmr/tmr.c`；启用通用框架本身不会注册或驱动实际硬件。

### 2. 配置与依赖

| 配置项              | 类型 | 默认值 | 说明                                         |
|---------------------|------|--------|----------------------------------------------|
| `MR_USE_DRIVER_TMR` | bool | n      | 启用硬件定时器公共框架，依赖 `MR_USE_DRIVER` |

`MR_HT_TMR_INSTALL` 负责创建占位数据并注册 TMR 实例，不在 setup
阶段读取或配置定时器。应用须在启动定时器前显式调用
`mr_tmr_configure`。

公共层不做 `period`、`mode` 的字段校验，也不会在公共头文件中固定 `period` 的单位：
**单位由 backend 定义**，使用前必须阅读目标平台给出的单位、精度与可表示范围，
不可把某种计时器的单位当作通用 API 保证。`mr_tmr_configuration` 直接把输出交给 backend 填充。

`MR_TMR_SIGNAL_PERIOD` 是硬件输出，`MR_TMR_WAIT_TIMEOUT` 是软件 wait 事件。两者独立控制；启用
signal
不会启用中断或 waiter。

### 3. 快速上手

平台后端为定时器实例提供 `mr_tmr_ops_t`，并通过 `MR_TMR_INSTALL` 或
`MR_HT_TMR_INSTALL` 注册。应用取得并 setup
对应驱动后，配置周期和模式，按需启用周期信号，再调用 start/stop。

例如配置周期模式并启用 PERIOD signal（`period` 的实际单位以 backend 约定为准）：

```c
mr_tmr_config_t config = {
    .period = 500U,              /* 单位由 backend 定义 */
    .mode = MR_TMR_MODE_PERIODIC,
};

mr_tmr_configure(timer, &config);
mr_tmr_signal_enable(timer, MR_TMR_SIGNAL_PERIOD);
mr_tmr_start(timer);
```

signal 只控制已经静态接线的硬件输出，不会启动或配置 ADC/DAC/DMA。消费者应先完成武装，再启动
TMR。

TMR + ADC 周期采集和 TMR + DAC 等间隔波形输出的完整启停顺序见
[驱动组合与进阶示例](../examples.md)。

### 4. 使用说明

- `mr_tmr_configure` 先调用必需的 `is_busy`；查询失败直接透传错误，busy 为真时公共层返回
  `-MR_EBUSY`，空闲时才调用
  `configure`。
- `mr_tmr_start` 同样先查询 `is_busy`，已运行时返回 `-MR_EBUSY`，否则调用必需的
  `start`。
- `mr_tmr_stop` 先查询 `is_busy`；已经空闲时公共层直接返回 0，不调用 backend
  `stop`，运行时才调用 `stop`。
- `mr_tmr_configuration` 不查询 busy，直接调用 backend `configuration`。
- `mr_tmr_signal_enable/disable` 控制 `MR_TMR_SIGNAL_PERIOD`，不改变 TMR
  运行状态，也不控制 wait event。
- `period` 的单位和范围、是否必须先 configure、PERIODIC/ONESHOT 的精确状态变化、signal
  的硬件映射，都由 backend 定义。
- setup 只初始化 waitable，shutdown 只删除 waitable；它们不会自动 configure、start
  或 stop 硬件定时器。
- event enable/disable 只通过可选 `event_control` 透传 `events` mask；公共层不校验
  mask。
- 当前 API 不提供输入捕获、输出比较或计数值读取接口；PWM
  由独立的 [PWM 驱动](../pwm/pwm.md) 提供，并由 HTS 固定硬件定时器的
  TMR/PWM 归属。

### 5. API 参考

| API                                                      | 说明                                       |
|----------------------------------------------------------|--------------------------------------------|
| `mr_tmr_init` / `mr_tmr_create`                          | 初始化静态实例或创建动态驱动               |
| `mr_tmr_del`                                             | 删除驱动                                   |
| `mr_tmr_get` / `mr_tmr_put`                              | 获取或释放驱动引用                         |
| `MR_TMR_DEFINE` / `MR_TMR_INSTALL` / `MR_HT_TMR_INSTALL` | 定义或安装静态驱动实例                     |
| `mr_tmr_configure`                                       | 请求一个硬件定时配置                       |
| `mr_tmr_configuration`                                   | 查询后端生效的配置                         |
| `mr_tmr_start` / `mr_tmr_stop`                           | 启动或停止定时器                           |
| `mr_tmr_is_busy`                                         | 查询定时器是否正在运行                     |
| `mr_tmr_signal_enable` / `mr_tmr_signal_disable`         | 控制周期边界硬件信号                       |
| `mr_tmr_event_attach`                                    | 订阅 waiter，不启用后端事件源              |
| `mr_tmr_event_enable`                                    | 按 `MR_TMR_WAIT_*` mask 启用后端支持的来源 |
| `mr_tmr_event_disable`                                   | 按 `MR_TMR_WAIT_*` mask 停用事件来源       |
| `mr_tmr_event_raise`                                     | 触发事件（平台实现可调用）                 |

**初始化与销毁**

```c
mr_err_t mr_tmr_init(mr_driver_t *drv, mr_tmr_data_t *data,
                         void *priv, mr_tmr_ops_t *ops);
mr_driver_t *mr_tmr_create(mr_tmr_data_t *data, void *priv,
                               mr_tmr_ops_t *ops);
mr_err_t mr_tmr_del(mr_driver_t *drv);
mr_driver_t *mr_tmr_get(mr_driver_t *drv);
mr_bool_t mr_tmr_put(mr_driver_t *drv);
```

init 的参数为驱动对象、驱动数据、平台私有数据和操作表，成功返回 0。create
创建动态驱动，data 必须由动态内存分配器分配，并在
del 时释放；失败返回 `MR_NULL`。del 删除驱动（不要求已 setup；若仍处于 setup 会先
shutdown），成功返回 0。get/put 用于管理驱动引用。

**配置与查询**

```c
mr_err_t mr_tmr_configure(mr_driver_t *drv,
                              const mr_tmr_config_t *config);
mr_err_t mr_tmr_configuration(mr_driver_t *drv,
                                  mr_tmr_config_t *config);
```

`config` 包含 `period` 和 `mode`。公共层在 configure 前查询运行状态，busy 时返回
`-MR_EBUSY`；字段内容不做校验。
`configuration` 直接调用 backend 填充结构。

**控制与状态**

```c
MR_INLINE mr_err_t mr_tmr_start(mr_driver_t *drv);
MR_INLINE mr_err_t mr_tmr_stop(mr_driver_t *drv);
MR_INLINE mr_err_t mr_tmr_is_busy(mr_driver_t *drv, mr_bool_t *busy);
MR_INLINE mr_err_t mr_tmr_signal_enable(mr_driver_t *drv,
                                        mr_tmr_signal_t signal);
MR_INLINE mr_err_t mr_tmr_signal_disable(mr_driver_t *drv,
                                         mr_tmr_signal_t signal);
```

start 和 stop 都先通过必需 `is_busy` 查询状态。start 在 busy 时由公共层返回
`-MR_EBUSY`；stop 在不 busy 时由公共层直接返回
0。其他 start/stop 行为和错误码由 backend 决定，公共层不会检查“是否已配置”。

**事件**

```c
MR_INLINE mr_err_t mr_tmr_event_attach(mr_driver_t *drv,
                                           mr_waiter_t *entry);
MR_INLINE mr_err_t mr_tmr_event_enable(mr_driver_t *drv,
                                           mr_uint32_t events);
MR_INLINE mr_err_t mr_tmr_event_disable(mr_driver_t *drv,
                                            mr_uint32_t events);
MR_INLINE mr_err_t mr_tmr_event_raise(mr_driver_t *drv,
                                          mr_uint32_t events);
```

`mr_tmr_event_attach` 只把 waiter 订阅到框架 waitable，不会调用 backend。
`mr_tmr_event_enable` / `mr_tmr_event_disable` 把
`events` 原样交给可选 `event_control`；回调未实现时返回 `-MR_ENOSYS`，公共层不校验
mask。backend 可调用
`mr_tmr_event_raise`，它以 `MR_NULL` payload 同步分发 waiter。

### 6. 常见问题

**为什么 setup 后还要调用 configure？**

TMR setup 只初始化框架 waitable，不配置硬件。是否允许未经 configure 直接
start，公共层不判断；应遵循具体 backend 的约定。

**为什么配置的 period 和硬件实际周期不同？**

公共 API 没有规定 `period` 单位、量化方式或超范围错误码。具体 backend
可以按自己的时钟/分频模型解释该字段，并通过
`configuration` 返回它定义的当前配置。

**不启用中断还能触发 ADC 吗？**

可以。`MR_TMR_SIGNAL_PERIOD` 与 `MR_TMR_WAIT_TIMEOUT` 独立；backend 可以只输出硬件
signal 而不开启定时器中断。
能否连接到目标 ADC 以及具体路由仍由 HTS/backend 决定。

---

## 开发者指南

### 7. 架构总览

公共头定义配置、操作表和类型化生命周期包装。`tmr.c` 实现
configure/configuration，其中 configure 通过 `is_busy`
阻止运行中重配；setup/shutdown 只初始化、删除 waitable。start/stop/is_busy
和事件接口在公共头中以内联函数提供。

`MR_HT_TMR_INSTALL` 创建占位数据并复用 `MR_HT_DRIVER_INSTALL`
注册实例，不读取频率或周期属性，也不在 setup 阶段配置定时器。

### 8. 数据结构与后端契约

- `mr_tmr_config_t` 包含 `period`、`mode`；公共层没有给 `period` 指定单位。
  `mr_tmr_data_t.event` 是实例级
  waitable，在 setup 时初始化。
- `configure`、`configuration`、`start`、`stop`、`is_busy` 都是必需 callback；
  `signal_control` 和
  `event_control` 为相互独立的可选 callback。
- configure/start/stop 的公共包装都会依赖 `is_busy`，因此 backend 的 busy
  结果必须能在这些控制路径中稳定使用。
- configure 只会在公共层观察到“不 busy”时下传；start 也一样。stop 在“不 busy”时不会调用
  backend。
- setup/shutdown 不配置、检查或停止 backend 定时器。删除路径若设备仍在运行，也不会因为
  clazz deinit 自动调用 `stop`。
- `MR_TMR_MODE_PERIODIC`、`MR_TMR_MODE_ONESHOT`、`MR_TMR_SIGNAL_PERIOD` 和
  `MR_TMR_WAIT_TIMEOUT` 提供统一语义；具体硬件时序、状态机和
  signal/event 映射由 backend 定义。
- `event_control` 接收未经过滤的 event mask；`mr_tmr_event_raise` 通过实例
  waitable 同步分发且 payload 为 `MR_NULL`。

### 9. 生命周期与核心流程

driver setup/shutdown 初始化并删除 waitable，不配置、检查或停止 backend
定时器。硬件计数状态与 driver SETUP 生命周期是两套独立状态；若某
backend 要求删除前先停止硬件，应由调用者按该 backend 契约显式完成。

TMR 公共层也不协调 ADC/DMA 等其他外设，跨外设启动顺序属于上层逻辑。

平台 TMR backend 的 period 求解、硬件 signal、TIMEOUT ISR 与 HardTree
接入见 [TMR 驱动移植指南](porting.md)。

### 10. 源码导读

| 位置                                   | 职责                                                           |
|----------------------------------------|----------------------------------------------------------------|
| `mrX/include/mrX/drivers/tmr/mr_tmr.h` | 公共配置、模式、signal/event、ops、初始化宏和 API              |
| `mrX/drivers/tmr/tmr.c`                | configure 的 busy 门控、configuration 分发与 waitable 生命周期 |
| `mrX/include/mrX/drivers/tmr/Kconfig`  | TMR 功能开关                                                   |


# 看门狗定时器（WDT）

> 提供硬件看门狗配置、启停、喂狗、忙状态查询与 waitable 事件接口。时间字段的单位和硬件语义由
> backend 定义。

| 项目     | 说明                         |
|----------|------------------------------|
| 功能宏   | `MR_USE_DRIVER_WDT`          |
| 头文件   | `<mrX/drivers/wdt/mr_wdt.h>` |
| 依赖模块 | [驱动框架](../driver.md)     |
| 参考测试 | `mrX/test/drivers/wdt/wdt.c` |

---

## 用户指南

### 1. 模块概述

每个 WDT 驱动实例表示一个硬件看门狗。公共配置结构包含三个 64 位字段：`feed_min`、
`feed_max` 与 `pretimeout`
。这些名称用于表达喂狗窗口和提前预警相关参数，但公共层没有规定时间单位、0
值特殊含义或三者之间的数学关系，具体解释由 backend
决定。

能否停止、TIMEOUT 是否有可等待事件以及超时是否触发复位，均取决于实际硬件与 backend；
公共层只提供相应 API 和事件位，不保证所有能力都可用。

### 2. 配置与依赖

| 配置项              | 类型 | 默认值 | 说明                                     |
|---------------------|------|--------|------------------------------------------|
| `MR_USE_DRIVER_WDT` | bool | n      | 启用看门狗公共框架，依赖 `MR_USE_DRIVER` |

`MR_HT_WDT_INSTALL` 负责创建占位数据并注册 WDT 实例，不在 setup
阶段读取或配置看门狗。应用须在启动看门狗前显式调用
`mr_wdt_configure`。

公共层不做 `feed_min`、`feed_max` 与 `pretimeout` 的字段校验，也不指定它们的单位。backend
负责解释这些字段并决定硬件能力与错误码。
`mr_wdt_configuration` 直接调用 backend 填充配置结构，公共层不缓存或换算数值。

### 3. 快速上手

采集与控制通路只在完成一轮有效工作后喂狗的示例见
[驱动组合与进阶示例](../examples.md)。

平台 backend 为看门狗实例提供 `mr_wdt_ops_t`，并通过 `MR_WDT_INSTALL` 或
`MR_HT_WDT_INSTALL` 注册。应用取得并 setup
对应驱动后，可按 backend 约定配置参数、启动并调用 feed。

例如提交一组窗口/预警参数（数值单位由 backend 约定）：

```c
mr_wdt_config_t config = {
    .feed_min = 0U,
    .feed_max = 1000U,
    .pretimeout = 100U,
};

mr_wdt_configure(wdt, &config);
mr_wdt_start(wdt);

/* backend 报告看门狗运行期间喂狗 */
mr_wdt_feed(wdt);
```

### 4. 使用说明

- `mr_wdt_configure` 先调用必需的 `is_busy`；查询失败直接透传，busy 为真时公共层返回
  `-MR_EBUSY`，空闲时才调用 `configure`。
- `mr_wdt_start` 同样先查 busy，已运行时公共层返回 `-MR_EBUSY`，否则调用必需的
  `start`。
- `mr_wdt_stop` 先查 busy：已经空闲时直接返回 0；运行中若 `stop == MR_NULL` 则返回
  `-MR_ENOSYS`，否则调用 backend。
- `mr_wdt_feed` 先查 busy：不 busy 时公共层固定返回 `-MR_EINVAL`，运行时调用必需的
  `feed`。
- `mr_wdt_configuration` 与 `mr_wdt_is_busy` 直接调用各自必需 callback。
- `feed_min/feed_max/pretimeout` 的单位、0 值含义、窗口规则与 pretimeout 时刻均由
  backend 定义。
- setup 只初始化 waitable，shutdown 只删除 waitable；不会自动 configure/start/stop
  硬件看门狗。
- `MR_WDT_WAIT_PRETIMEOUT` 与 `MR_WDT_WAIT_TIMEOUT` 是公共事件位名称；具体硬件何时能够产生这些事件由
  backend 决定。
- `mr_wdt_event_attach` 只订阅框架 waitable；event enable/disable 把 mask 原样交给可选
  `event_control`，公共层不校验 mask。
  `mr_wdt_event_raise` 使用 `MR_NULL` payload 同步分发 waiter。

### 5. API 参考

| API                                                      | 说明                         |
|----------------------------------------------------------|------------------------------|
| `mr_wdt_init` / `mr_wdt_create`                          | 初始化静态实例或创建动态驱动 |
| `mr_wdt_del`                                             | 删除驱动                     |
| `mr_wdt_get` / `mr_wdt_put`                              | 获取或释放驱动引用           |
| `MR_WDT_DEFINE` / `MR_WDT_INSTALL` / `MR_HT_WDT_INSTALL` | 定义或安装静态驱动实例       |
| `mr_wdt_configure`                                       | 请求一个看门狗配置           |
| `mr_wdt_configuration`                                   | 查询后端生效的配置           |
| `mr_wdt_start` / `mr_wdt_stop`                           | 启动或停止看门狗             |
| `mr_wdt_feed`                                            | 刷新看门狗计数器             |
| `mr_wdt_is_busy`                                         | 查询看门狗是否正在计时       |
| `mr_wdt_event_attach`                                    | 订阅 WDT 事件                |
| `mr_wdt_event_enable` / `mr_wdt_event_disable`           | 开关后端事件源               |
| `mr_wdt_event_raise`                                     | 由平台后端上报 WDT 事件      |

**初始化与销毁**

```c
mr_err_t mr_wdt_init(mr_driver_t *drv, mr_wdt_data_t *data, void *priv,
                     mr_wdt_ops_t *ops);
mr_driver_t *mr_wdt_create(mr_wdt_data_t *data, void *priv, mr_wdt_ops_t *ops);
mr_err_t mr_wdt_del(mr_driver_t *drv);
mr_driver_t *mr_wdt_get(mr_driver_t *drv);
mr_bool_t mr_wdt_put(mr_driver_t *drv);
```

init 的参数为驱动对象、驱动数据、平台私有数据和操作表，成功返回 0。create
创建动态驱动，data 必须由动态内存分配器分配，并在
del 时释放；失败返回 `MR_NULL`。del 删除驱动（不要求已 setup；若仍处于 setup 会先
shutdown），成功返回 0。get/put 用于管理驱动引用。

**配置与查询**

```c
mr_err_t mr_wdt_configure(mr_driver_t *drv, const mr_wdt_config_t *config);
mr_err_t mr_wdt_configuration(mr_driver_t *drv, mr_wdt_config_t *config);
```

`config` 包含 `feed_min`、`feed_max`、`pretimeout`。公共层在 configure 前查询运行状态，busy
时返回 `-MR_EBUSY`；字段值本身不做校验。
`configuration` 直接调用 backend 填充结构。三个字段的单位与具体窗口/预警含义必须由
backend 说明。

**控制与状态**

```c
MR_INLINE mr_err_t mr_wdt_start(mr_driver_t *drv);
MR_INLINE mr_err_t mr_wdt_stop(mr_driver_t *drv);
MR_INLINE mr_err_t mr_wdt_feed(mr_driver_t *drv);
MR_INLINE mr_err_t mr_wdt_is_busy(mr_driver_t *drv, mr_bool_t *busy);
```

start/stop/feed 都通过必需 `is_busy` 判断当前状态：start 在 busy 时返回
`-MR_EBUSY`；stop 在不 busy 时直接返回 0，运行中缺少
stop callback 时返回 `-MR_ENOSYS`；feed 在不 busy 时返回 `-MR_EINVAL`
。公共层不会检查“是否已经 configure”。

**事件接口**

```c
MR_INLINE mr_err_t mr_wdt_event_attach(mr_driver_t *drv, mr_waiter_t *entry);
MR_INLINE mr_err_t mr_wdt_event_enable(mr_driver_t *drv, mr_uint32_t events);
MR_INLINE mr_err_t mr_wdt_event_disable(mr_driver_t *drv, mr_uint32_t events);
MR_INLINE mr_err_t mr_wdt_event_raise(mr_driver_t *drv, mr_uint32_t events);
```

事件掩码包括 `MR_WDT_WAIT_PRETIMEOUT` 与 `MR_WDT_WAIT_TIMEOUT`
。公共层只定义这两个事件位，不规定具体硬件阶段如何映射。
`event_control` 可选且接收未经过滤的 mask；`event_raise` 通过框架 waitable 同步通知。

### 6. 常见问题

**为什么 setup 后还要调用 configure？**

WDT setup 只初始化框架 waitable，不配置硬件。公共层不检查 start 前是否调用过
configure；具体 backend 若有此前置条件，应由其实现返回错误。

**为什么配置的 feed_max 和硬件实际超时不同？**

公共 API 没有规定时间单位、量化方式或超范围错误码。backend 可以按自己的时钟/分频模型解释配置，并通过
`configuration`
返回它定义的当前配置。

**为什么 stop 会返回 `-MR_ENOSYS`？**

不少独立看门狗一旦启动就不能软件关闭。已停止时 stop 仍返回 0；只有正在计时且后端不能停止时才返回
`-MR_ENOSYS`。

---

## 开发者指南

### 7. 架构总览

公共头定义配置、操作表和类型化生命周期包装。`wdt.c` 实现 configure 的 busy
门控、configuration 分发和 waitable
生命周期；start/stop/feed/is_busy 与事件接口在公共头中以内联函数提供。setup/shutdown
不配置或停止 backend 看门狗。

`MR_HT_WDT_INSTALL` 创建占位数据并复用 `MR_HT_DRIVER_INSTALL` 注册实例，不读取超时属性，也不在
setup 阶段配置看门狗。

### 8. 数据结构与后端契约

- `mr_wdt_config_t` 包含 `feed_min/feed_max/pretimeout` 三个 64
  位字段；公共层没有规定它们的单位或相互关系。
  `mr_wdt_data_t.event` 是实例级 waitable，在 setup 时初始化。
- `configure`、`configuration`、`start`、`feed`、`is_busy` 是必需 callback；`stop` 与
  `event_control` 可选。运行中缺少 stop 时
  `mr_wdt_stop` 返回 `-MR_ENOSYS`，缺少 event_control 时事件开关返回
  `-MR_ENOSYS`。
- configure/start/stop/feed 都依赖 `is_busy` 的结果，因此 backend 必须提供稳定可用的
  busy 查询。
- configure 和 start 只会在公共层观察到“不 busy”时下传；stop 在“不 busy”时不调用
  backend；feed 在“不 busy”时由公共层返回
  `-MR_EINVAL`。
- setup/shutdown 只管理 waitable，不配置、检查或停止 backend 看门狗。硬件若不可停止，driver
  生命周期结束也不会由 clazz
  deinit 自动关闭它。
- event mask 不由公共层过滤；backend 可调用 `mr_wdt_event_raise` 同步通知
  PRETIMEOUT/TIMEOUT 位。

### 9. 生命周期与核心流程

driver setup/shutdown 会初始化或销毁框架 waitable，但不配置、检查或停止 backend
看门狗。硬件计时状态与 driver SETUP
生命周期相互独立；调用者应根据具体 backend 的硬件约束安排
configure/start/feed/stop。backend 可通过 `mr_wdt_event_raise`
把事件位送入公共 waitable。

平台 WDT backend 的时间量化、不可停止 watchdog、feed window
与事件接入见 [WDT 驱动移植指南](porting.md)。

### 10. 源码导读

| 位置                                   | 职责                                                                    |
|----------------------------------------|-------------------------------------------------------------------------|
| `mrX/include/mrX/drivers/wdt/mr_wdt.h` | 公共配置、事件、ops、初始化宏和 API                                     |
| `mrX/drivers/wdt/wdt.c`                | configure 的 busy 门控、configuration 分发、waitable 与 driver 生命周期 |
| `mrX/include/mrX/drivers/wdt/Kconfig`  | WDT 功能开关                                                            |

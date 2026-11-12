# WDT 驱动移植指南

> 本文面向平台 backend 开发者，说明如何实现硬件看门狗 backend、如何接线 pretimeout
> 事件与 HardTree，以及哪些能力必须按目标芯片重新确认。公共
> API 见 [WDT](wdt.md)；芯片运行期约束见对应平台的 `README.md`。

## 1. 移植边界

WDT 没有 stream，也没有 DMA 数据通路。backend
的职责可以概括为三件事：把三个时间字段换算成硬件超时寄存器、启动与喂狗、按硬件真实能力提供可停止性与事件源。

| 关注点                                  | 负责方                                  |
|-----------------------------------------|-----------------------------------------|
| 驱动类型、引用计数、setup/shutdown      | 公共层                                  |
| `mr_wdt_data_t.event` 的 init/deinit    | 公共层（clazz 的 init/deinit）          |
| configure/start/stop/feed 的 busy 门控  | 公共层                                  |
| 缺少可选能力时的 `-MR_ENOSYS` 降级      | 公共层                                  |
| 事件 mask 的透传（公共层不过滤）        | 公共层                                  |
| 三个时间字段的单位、0 值含义与相互关系  | backend，必须写进平台文档               |
| 时间到 (prescaler, reload) 的量化与回读 | backend                                 |
| 硬件启动、喂狗序列、可选的软件停止      | backend                                 |
| `is_busy` 的真实性                      | backend（公共层所有门控都建立在它之上） |
| pretimeout/timeout 事件源、ISR、NVIC    | backend                                 |
| 看门狗实例、时钟钩子等静态资源          | HardTree + backend                      |

建议实现位置：

```text
mrX/arch/<vendor>/<target>/platform/drivers/wdt/wdt.c
```

同一 SoC 上如果有多个看门狗 IP（独立看门狗、窗口看门狗、系统看门狗），应当各用一个
compatible 和一份 backend 表达，不要把不同
IP 的可停止性、窗口能力和事件能力混进同一个 ops 表。

## 2. 操作表

backend 实现 `mrX/include/mrX/drivers/wdt/mr_wdt.h` 的 `mr_wdt_ops_t`
。成员必须按公共头中的名字逐一对应，缺省的可选成员显式写
`MR_NULL`：

| 成员            | 签名                                                                         | 必需 | 缺失时 / 公共层行为                                       |
|-----------------|------------------------------------------------------------------------------|------|-----------------------------------------------------------|
| `configure`     | `mr_err_t (*)(const mr_driver_t *drv, const mr_wdt_config_t *config)`        | 是   | 断言，无错误码降级                                        |
| `configuration` | `mr_err_t (*)(const mr_driver_t *drv, mr_wdt_config_t *config)`              | 是   | 断言，无错误码降级                                        |
| `start`         | `mr_err_t (*)(const mr_driver_t *drv)`                                       | 是   | 断言，无错误码降级                                        |
| `stop`          | `mr_err_t (*)(const mr_driver_t *drv)`                                       | 否   | busy 时为 `MR_NULL` 则返回 `-MR_ENOSYS`；不 busy 时不下传 |
| `feed`          | `mr_err_t (*)(const mr_driver_t *drv)`                                       | 是   | 断言，无错误码降级                                        |
| `is_busy`       | `mr_err_t (*)(const mr_driver_t *drv, mr_bool_t *busy)`                      | 是   | 断言，无错误码降级                                        |
| `event_control` | `mr_err_t (*)(const mr_driver_t *drv, mr_uint32_t events, mr_bool_t enable)` | 否   | 为 `MR_NULL` 时 enable/disable 都返回 `-MR_ENOSYS`        |

必需成员的缺失由 `MR_ASSERT` 约束：只有在启用 assert 时才断言失败，未启用 assert
的构建里缺失等价于未定义行为。 **没有**
“必需成员留空就自动返回某个错误码”的路径，不要依赖它。

可选成员缺失必须由公共层给出确定行为：运行中缺少 `stop` 得到 `-MR_ENOSYS`
（见 [WDT](wdt.md) 中 `mr_wdt_stop` 的约定），缺少
`event_control` 时事件开关得到 `-MR_ENOSYS`。backend 不要为了让回调“看起来完整”而实现一个返回
0 的空 `stop`
——那会把“硬件不可停止”伪装成“已停止”。

## 3. 配置字段与单位

`mr_wdt_config_t` 包含 `feed_min`、`feed_max` 和 `pretimeout`。公共层不规定它们的时间单位、量化方式、硬件窗口能力与提前预警来源。backend 必须在本平台文档明确有效范围、0 值含义、超范围错误与 `configuration` 返回请求值还是已生效的量化值。

## 4. Busy 与生命周期

公共 `configure`、`start`、`stop` 和 `feed` 先使用 `is_busy`，因此 backend 的 busy 状态必须真实反映硬件。硬件不可停止时可将 `stop` 设为 `MR_NULL`，运行期间公共 stop 按契约报告不支持；不要用空操作伪装成功。删除驱动对象不意味着硬件一定停止。

## 5. Feed 与窗口

`feed` 只在设备已启动时执行。若支持硬件喂狗窗口，backend 应验证过早/过晚喂狗；若无窗口能力，应在平台契约说明并拒绝无法实现的配置。不能在上层任务没有有效进展时依靠无条件喂狗掩盖故障。

## 6. 事件与中断

公共事件位为 `MR_WDT_WAIT_PRETIMEOUT` 与 `MR_WDT_WAIT_TIMEOUT`，但是否有对应的硬件中断由 backend 决定。`event_control` 设置实际硬件事件来源，ISR 处理状态后调用 `mr_wdt_event_raise()`；不具备事件支持时不能假称通知可用。回调是同步执行的，不允许阻塞。

## 7. HardTree 与安装

通过 `MR_HT_WDT_INSTALL` 或驱动框架的注册方式安装实例。时钟、硬件 watchdog 实例、IRQ、预警资源和窗口精度均由平台说明，不在公共文档规定专有属性。

## 8. 移植验收

- [ ] feed 窗口、超时与预警的单位和量化准确，能力缺失有明确错误。
- [ ] start、busy、feed、不可停止状态及资源生命周期一致。
- [ ] 能提供的事件准确通知；不能提供的事件不会被误报。

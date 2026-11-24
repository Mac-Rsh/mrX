# WDT 驱动移植指南

本文面向平台 backend 开发者，说明如何为硬件看门狗（独立看门狗 / 窗口看门狗）实现
WDT 驱动。公共接口用法见 [WDT 驱动](wdt.md)。

## 1. 移植边界

| 职责                    | 公共层                                                     | Backend                                                        |
|-------------------------|------------------------------------------------------------|----------------------------------------------------------------|
| 驱动生命周期与 waitable | 在 `init`/`deinit` 中初始化并销毁 `mr_wdt_data_t.event`    | 无需自行创建 waitable，通过 `mr_wdt_event_raise` 触发事件      |
| 忙状态门控              | `configure`/`start`/`stop`/`feed` 前自动调用 `is_busy`     | 确保 `is_busy` 真实反映硬件是否已启动计时                      |
| 时间参数与窗口校验      | 透传 `mr_wdt_config_t`                                     | 定义 `feed_min`/`feed_max`/`pretimeout` 单位、范围、量化及回读 |
| 启停、喂狗与事件        | 运行中缺失 `stop` 或缺失 `event_control` 返回 `-MR_ENOSYS` | 实现硬件启动/喂狗序列、可选停止及中断 pending 清理             |

同一 SoC 若存在多个不同特性的看门狗（如独立看门狗与窗口看门狗），应分别定义独立的
HardTree `compatible` 与 `mr_wdt_ops_t`，避免在单个实现中混杂不同硬件的可停止性与窗口语义。

## 2. 操作表（ops）

backend 需实现 `include/mrX/drivers/wdt/mr_wdt.h` 中定义的 `mr_wdt_ops_t`：

| 回调            | 签名要点                                                         | 必需 | 返回值约定                                                                |
|-----------------|------------------------------------------------------------------|------|---------------------------------------------------------------------------|
| `configure`     | `(const mr_driver_t *drv, const mr_wdt_config_t *config)`        | 是   | 成功返回 `0`，超范围或不支持的窗口/预警配置返回负数错误码                 |
| `configuration` | `(const mr_driver_t *drv, mr_wdt_config_t *config)`              | 是   | 成功返回 `0`，失败返回负数错误码                                          |
| `start`         | `(const mr_driver_t *drv)`                                       | 是   | 成功返回 `0`，失败返回负数错误码                                          |
| `stop`          | `(const mr_driver_t *drv)`                                       | 否   | 成功返回 `0`；硬件不可软件停止时留空 `MR_NULL`（公共层返回 `-MR_ENOSYS`） |
| `feed`          | `(const mr_driver_t *drv)`                                       | 是   | 成功返回 `0`，失败返回负数错误码                                          |
| `is_busy`       | `(const mr_driver_t *drv, mr_bool_t *busy)`                      | 是   | 成功返回 `0`，`*busy` 为真实硬件运行状态                                  |
| `event_control` | `(const mr_driver_t *drv, mr_uint32_t events, mr_bool_t enable)` | 否   | 成功返回 `0`，不支持时留空 `MR_NULL`（公共层返回 `-MR_ENOSYS`）           |

`configure`、`configuration`、`start`、`feed`、`is_busy` 在公共层通过 `MR_ASSERT`
强制非空。对于硬件不可停止的看门狗，务必将 `stop` 置为 `MR_NULL`，切勿实现一个返回
`0` 的空函数。

## 3. 关键契约

- **时间单位与量化回读**：backend 必须在平台文档中明确 `feed_min`、`feed_max` 与
  `pretimeout` 的物理单位、0 值含义及有效范围。若硬件不支持下限窗口（要求
  `feed_min == 0`）或不支持提前预警（要求 `pretimeout == 0`），应在传入非零值时返回明确错误；
  `configuration` 应返回经硬件分频/重载量化后实际生效的值。
- **喂狗与窗口语义**：`feed` 仅在 `is_busy`
  为真时由公共层下发。支持窗口看门狗的硬件应在过早或过晚喂狗时按硬件行为处理；无窗口能力的硬件不得静默忽略非零
  `feed_min`。
- **中断与事件上下文**：`MR_WDT_WAIT_PRETIMEOUT` 与 `MR_WDT_WAIT_TIMEOUT`
  对应硬件中断，ISR 须先清除硬件中断标志再调用 `mr_wdt_event_raise(drv, events)`
  ，waiter 回调同步执行于 ISR 上下文，不得阻塞。

## 4. HardTree 安装

使用 `MR_HT_WDT_INSTALL(_id, _priv, _ops)` 安装实例，宏会自动定义静态
`mr_wdt_data_t`。看门狗寄存器基址、时钟源、IRQ 号等属于平台私有属性，由 backend
自行解析。

## 5. 验收要点

- 验证平台定义的 `feed_min`/`feed_max`/`pretimeout` 单位、范围、量化及
  `mr_wdt_configuration` 回读一致。
- 验证 `start` -> `is_busy` -> `feed`（未启动时返回 `-MR_EINVAL`，运行中重配返回
  `-MR_EBUSY`）及不可停止看门狗调用 `mr_wdt_stop` 返回 `-MR_ENOSYS`。
- 若支持预警/超时事件：验证 `event_control` 开关与 ISR 中 `mr_wdt_event_raise`
  通知正常，不支持的事件掩码返回明确错误。
- 跑通 `test/drivers/wdt/wdt.c` 行为测试。

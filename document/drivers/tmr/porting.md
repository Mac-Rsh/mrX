# TMR 驱动移植指南

本文面向平台 backend 开发者，说明如何为硬件定时器实现 TMR
驱动。公共接口用法见 [TMR 驱动](tmr.md)。

## 1. 移植边界

| 职责                    | 公共层                                                    | Backend                                                        |
|-------------------------|-----------------------------------------------------------|----------------------------------------------------------------|
| 驱动生命周期与 waitable | 在 `init`/`deinit` 中初始化并销毁 `mr_tmr_data_t.event`   | 无需自行创建 waitable，通过 `mr_tmr_event_raise` 触发事件      |
| 忙状态门控              | `configure`/`start`/`stop` 前自动调用 `is_busy`           | 确保 `is_busy` 反映真实硬件运行态（含单次模式到期后）          |
| 周期与模式映射          | 透传 `mr_tmr_config_t`                                    | 定义 `period` 单位、范围、PSC/ARR 量化及回读计算               |
| 硬件信号与中断          | 透传 `signal` 与 `events` 掩码，缺失回调返回 `-MR_ENOSYS` | 路由 `MR_TMR_SIGNAL_PERIOD` 硬件触发，管理 IRQ 与 pending 清理 |

同一定时器外设在 HardTree 中只能由一种角色拥有：基础计时安装 `MR_HT_TMR_INSTALL`
，PWM 输出安装 PWM 驱动，不可同时挂载两个驱动。

## 2. 操作表（ops）

backend 需实现 `include/mrX/drivers/tmr/mr_tmr.h` 中定义的 `mr_tmr_ops_t`：

| 回调             | 签名要点                                                             | 必需 | 返回值约定                                            |
|------------------|----------------------------------------------------------------------|------|-------------------------------------------------------|
| `configure`      | `(const mr_driver_t *drv, const mr_tmr_config_t *config)`            | 是   | 成功返回 `0`，超范围或不支持返回负数错误码            |
| `configuration`  | `(const mr_driver_t *drv, mr_tmr_config_t *config)`                  | 是   | 成功返回 `0`，失败返回负数错误码                      |
| `start`          | `(const mr_driver_t *drv)`                                           | 是   | 成功返回 `0`，失败返回负数错误码                      |
| `stop`           | `(const mr_driver_t *drv)`                                           | 是   | 成功返回 `0`，失败返回负数错误码                      |
| `is_busy`        | `(const mr_driver_t *drv, mr_bool_t *busy)`                          | 是   | 成功返回 `0`，`*busy` 为真实硬件运行状态              |
| `signal_control` | `(const mr_driver_t *drv, mr_tmr_signal_t signal, mr_bool_t enable)` | 否   | 成功返回 `0`，不支持时留空（公共层返回 `-MR_ENOSYS`） |
| `event_control`  | `(const mr_driver_t *drv, mr_uint32_t events, mr_bool_t enable)`     | 否   | 成功返回 `0`，不支持时留空（公共层返回 `-MR_ENOSYS`） |

前 5 个基础回调（含 `stop`）在公共层通过 `MR_ASSERT` 强制非空，不可省略；
`signal_control` 与 `event_control` 为相互独立的可选回调。

## 3. 关键契约

- **`period` 单位与回读**：公共层不规定 `period` 的物理单位，backend
  必须在平台文档中明确单位（如微秒、纳秒或时钟 tick）、有效范围及量化误差。
  `configure` 失败时不得留下半完成状态；`configuration` 应根据实际生效的硬件分频/重载值反算回读。
- **启停与单次模式**：`start` 前应清理遗留计数或中断标志以保证首个周期完整；
  `MR_TMR_MODE_ONESHOT` 到期停止后，`is_busy` 必须随之返回 `MR_FALSE`。`stop`
  路径需停止计数并屏蔽可能继续触发事件的硬件来源。
- **信号与事件独立性**：`MR_TMR_SIGNAL_PERIOD` 控制周期边界硬件触发输出（如 TRGO），
  `MR_TMR_WAIT_TIMEOUT` 控制超时中断与等待队列唤醒，两者互不隐含。重新配置期间应避免向已武装的下游外设产生毛刺触发。
- **ISR 上下文**：超时中断触发时，ISR 须先清除硬件 pending 标志再调用
  `mr_tmr_event_raise(drv, MR_TMR_WAIT_TIMEOUT)`，waiter 回调同步运行于 ISR
  上下文，不得阻塞。

## 4. HardTree 安装

使用 `MR_HT_TMR_INSTALL(_id, _priv, _ops)` 安装实例，宏会自动定义静态
`mr_tmr_data_t`。定时器寄存器基址、内核时钟、IRQ 号及触发路由等均属于平台私有属性，由
backend 自行解析。

## 5. 验收要点

- 验证平台定义的 `period` 单位/范围/量化误差与 `mr_tmr_configuration` 回读一致。
- 验证 `PERIODIC`/`ONESHOT` 模式、运行中重配返回 `-MR_EBUSY`、空闲时 `stop` 幂等及
  `is_busy` 状态准确。
- 验证 `MR_TMR_SIGNAL_PERIOD` 与 `MR_TMR_WAIT_TIMEOUT` 相互独立，ISR
  清标志与回调通知无遗漏或重复进入。
- 跑通 `test/drivers/tmr/tmr.c` 行为测试。

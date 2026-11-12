# TMR 驱动移植指南

> 本文面向平台 backend 开发者，说明如何实现基础硬件定时器 backend、如何接线周期
> signal 与 TIMEOUT 事件，以及哪些能力必须按目标芯片重新确认。公共
> API 见 [TMR](tmr.md)；芯片运行期约束见对应平台的 `README.md`。

## 1. 移植边界

TMR 表示硬件定时器的基础计时角色：周期/单次计数、周期边界硬件 signal 与 update
中断事件。它不提供输入捕获、输出比较或计数值读取；PWM
是同一类定时器的另一种静态角色，由 [PWM](../pwm/pwm.md) 与对应 compatible 拥有。

| 关注点                                          | 负责方                         |
|-------------------------------------------------|--------------------------------|
| 驱动类型、引用计数、setup/shutdown              | 公共层                         |
| `mr_tmr_data_t.event` 的 init/deinit            | 公共层（clazz 的 init/deinit） |
| configure/start/stop 的 busy 门控               | 公共层                         |
| 缺少可选能力时的 `-MR_ENOSYS` 降级              | 公共层                         |
| 事件 mask 与 signal 编号的透传（公共层不校验）  | 公共层                         |
| `period` 的单位、可表示范围与误差               | backend，必须写进平台文档      |
| timer kernel clock 的推导                       | backend                        |
| period → PSC/ARR 的求解与回读                   | backend                        |
| 启动、停止、计数器与标志位的清理                | backend                        |
| `is_busy` 的真实性（含 one-pulse 到期后的状态） | backend                        |
| signal 路由（TRGO 或等价通路）与消费者准备顺序  | backend + 上层组合逻辑         |
| TIMEOUT 中断源、NVIC、ISR 顺序                  | backend                        |
| 定时器实例、时钟钩子等静态资源                  | HardTree + backend             |

公共层不协调 ADC/DAC/DMA：signal 只控制已经静态接线的硬件输出，消费者何时武装、DMA
何时启动都属于上层组合逻辑。

同一定时器实例在 HardTree 中只能由一种 compatible 拥有：基础计时用途安装
`MR_HT_TMR_INSTALL`，PWM 输出用途安装 PWM
的安装宏，不得同时安装两个 backend，也不得在 ops 内临时切换角色。

建议实现位置：

```text
mrX/arch/<vendor>/<target>/platform/drivers/tmr/tmr.c
```

## 2. 操作表

backend 实现 `mrX/include/mrX/drivers/tmr/mr_tmr.h` 的 `mr_tmr_ops_t`：

| 成员             | 签名                                                                             | 必需 | 缺失时 / 公共层行为                                |
|------------------|----------------------------------------------------------------------------------|------|----------------------------------------------------|
| `configure`      | `mr_err_t (*)(const mr_driver_t *drv, const mr_tmr_config_t *config)`            | 是   | 断言，无错误码降级                                 |
| `configuration`  | `mr_err_t (*)(const mr_driver_t *drv, mr_tmr_config_t *config)`                  | 是   | 断言，无错误码降级                                 |
| `start`          | `mr_err_t (*)(const mr_driver_t *drv)`                                           | 是   | 断言，无错误码降级                                 |
| `stop`           | `mr_err_t (*)(const mr_driver_t *drv)`                                           | 是   | 断言，无错误码降级                                 |
| `is_busy`        | `mr_err_t (*)(const mr_driver_t *drv, mr_bool_t *busy)`                          | 是   | 断言，无错误码降级                                 |
| `signal_control` | `mr_err_t (*)(const mr_driver_t *drv, mr_tmr_signal_t signal, mr_bool_t enable)` | 否   | 为 `MR_NULL` 时 enable/disable 都返回 `-MR_ENOSYS` |
| `event_control`  | `mr_err_t (*)(const mr_driver_t *drv, mr_uint32_t events, mr_bool_t enable)`     | 否   | 为 `MR_NULL` 时 enable/disable 都返回 `-MR_ENOSYS` |

必需成员由 `MR_ASSERT` 约束：启用 assert 时缺失即断言失败，未启用 assert 时为未定义行为，
**没有**错误码降级路径。

注意 `stop` 在 TMR 上是必需能力，与 WDT 中 `stop` 可选、可用 `MR_NULL`
表达“不可停止”的约定不同：定时器可以被停止，缺少
`stop` 属于 backend 不完整。

`signal_control` 与 `event_control` 是两个相互独立的可选能力：缺少其中一个不影响另一个，也不影响周期
signal 与软件事件的语义独立性。

## 3. Period、单位与量化

`mr_tmr_config_t` 包含 `period` 与 `mode`。公共层不规定 period 的物理单位，也不替 backend 计算计数器/预分频器。每个移植必须在自己的平台文档说明单位、有效范围、舍入/量化、最大误差与配置回读值，不能把某个计时器的位宽或频率算法写成框架保证。

## 4. 配置与错误

在访问寄存器前校验输入、实例能力及资源占用状态；将硬件失败映射为负错误码。`is_busy` 必须反映实际运行状态，不能简单复用软件“已配置”标记。失败时不得留下被视作有效的半完成配置。

## 5. mode、启动与停止

`MR_TMR_MODE_PERIODIC` 与 `MR_TMR_MODE_ONESHOT` 的效果应与硬件计数方式一致。开始新一轮计时时清理遗留的中断标志并使首个周期完整；单次模式到期后必须使 busy 与真实硬件状态一致。停止路径必须屏蔽会继续产生事件的硬件来源。

## 6. Signal 接线

`MR_TMR_SIGNAL_PERIOD` 表示周期边界的硬件输出信号。backend 决定触发输出路由，并清楚区分 signal 使能、计时器 start 与 TIMEOUT wait 事件；三者互不隐含。重新配置期间必须避免在下游已武装时产生意外触发。

## 7. TIMEOUT 与 ISR

`MR_TMR_WAIT_TIMEOUT` 从硬件中断映射到 `mr_tmr_event_raise()`。ISR 应在通知前处理硬件 pending，防止重复进入；waiter 回调运行在 raise 的调用上下文，不得阻塞。事件位支持情况及中断控制语义以公共接口和 backend 能力为准。

## 8. HardTree 与安装

使用 `MR_HT_TMR_INSTALL` 或对应注册机制。计时器实例、时钟、IRQ、路由及可用信号消费者属于平台私有属性，必须在各平台文档定义，不得写成通用 TMR 契约。

## 9. 移植验收

- [ ] period 单位/范围/量化与 configuration 结果一致。
- [ ] 一次模式、周期模式、重启、停止及 busy 的行为正确。
- [ ] signal 与 TIMEOUT 事件相互独立，ISR 清理和 waiter 生命周期安全。
- [ ] 不支持的硬件资源和能力被清晰拒绝。

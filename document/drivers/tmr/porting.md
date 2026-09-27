# TMR 驱动移植指南

> 本文说明如何为平台定时器实现 TMR backend。公共 API 见 [TMR](tmr.md)。

## 1. 移植边界

TMR 公共层负责配置调用门控、busy 查询和 waitable 生命周期；backend 负责 period 到硬件计数参数的换算、启动/停止、trigger
signal 和 timeout 中断。

建议实现位置：

```text
mrX/arch/<vendor>/<target>/platform/drivers/tmr/
```

## 2. 操作表

`mr_tmr_ops_t` 中：

- `configure`
- `configuration`
- `start`
- `stop`
- `is_busy`

是基础能力；`event_control` 是可选事件能力。

公共层会依据 `is_busy` 阻止运行中 configure/start，并在已经停止时跳过 stop，因此 busy 必须稳定反映 backend 状态。

## 3. Period 换算

`mr_tmr_config_t.period` 的具体单位和量化由 backend 定义。移植时应明确：

- timer input clock；
- prescaler 范围；
- counter/period 最大值；
- 向上/向下计数；
- 周期模式与 oneshot；
- 实际误差和溢出处理。

若需要在多个 prescaler/count 组合中求解，应优先最小化周期误差，并通过 `configuration` 返回 backend 实际采用的配置。

## 4. Signal 与跨外设触发

`MR_TMR_SIGNAL_UPDATE` 只是一种公共 signal 符号。平台可以把它映射到 TRGO、内部触发矩阵或其他 SoC 事件。

TMR 公共层不会自动协调 ADC/DAC/DMA，因此：

- timer trigger route；
- 被触发外设的准备顺序；
- DMA 与 timer 的启动先后

都由平台 backend 和上层组合逻辑决定。

## 5. Timeout 事件

`event_control` 负责打开/关闭 `MR_TMR_WAIT_TIMEOUT` 的硬件来源。ISR 清除 update/timeout 状态后调用
`mr_tmr_event_raise()`。

raise 为同步分发；ISR 中的 waiter 不得执行阻塞工作。

oneshot 到期后 busy 状态应与硬件停止状态一致。

## 6. HardTree 与安装

平台可通过 `MR_HT_TMR_INSTALL` 注册实例。HardTree 可提供 timer 实例、输入时钟、IRQ、counter width、prescaler 上限等平台私有资源。

## 7. 参考实现

当前可参考：

```text
mrX/arch/st/stm32h5/platform/drivers/tmr/tmr.c
```

它展示了 timer clock/PSC/PER 求解、HAL 配置、事件控制与 HardTree 实例安装。

## 8. 移植检查清单

- timer_clock 来源正确；
- period 求解不会溢出；
- configuration 能反映量化后的实际结果；
- periodic/oneshot busy 状态正确；
- stop 后中断来源和硬件计数停止；
- TIMEOUT pending 正确清除；
- trigger signal 路由与外设启动顺序明确。

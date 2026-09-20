# WDT 驱动移植指南

> 本文说明如何实现硬件看门狗 backend。公共 API 见 [WDT](wdt.md)。

## 1. 移植边界

WDT 公共层负责 busy 门控和 waitable 生命周期；backend 负责时间参数换算、启动、喂狗、可选停止以及 pretimeout/timeout 事件来源。

## 2. 操作表

基础能力：

- `configure`
- `configuration`
- `start`
- `feed`
- `is_busy`

可选能力：

- `stop`
- `event_control`

很多独立看门狗启动后不可停止，因此 `stop = MR_NULL` 是合法能力表达；运行中调用公共 stop 时会得到 `-MR_ENOSYS`。

## 3. 时间参数

`mr_wdt_config_t` 包含：

```text
feed_min
feed_max
pretimeout
```

公共层没有规定单位。backend 必须在平台文档中明确：

- 时间单位；
- 0 值含义；
- window 上下限关系；
- pretimeout 是相对 timeout 还是独立值；
- 硬件量化方式与最大范围。

`configuration` 应返回 backend 实际采用的量化结果。

## 4. Busy 与生命周期

公共 configure/start/stop/feed 都依赖 `is_busy`，所以 busy 必须可靠：

- start 后及时进入 busy；
- 已停止时 stop 不会下传 backend；
- 不可停止 WDT 一旦 start，busy 应持续反映硬件真实状态；
- driver shutdown 不会自动关闭硬件 WDT。

不要把“driver 对象销毁”误认为“硬件 watchdog 已停止”。

## 5. Feed

`feed` 只在公共层观察到 busy 时下传。backend 需要遵守硬件 window 规则，过早或过晚 feed 应返回平台能够提供的错误，而不是静默吞掉。

若 feed 寄存器需要特定 key sequence，应保证该 sequence 在当前并发/中断模型下不可被破坏。

## 6. 事件

公共事件：

```text
MR_WDT_WAIT_PRETIMEOUT
MR_WDT_WAIT_TIMEOUT
```

`event_control` 打开/关闭对应硬件来源，ISR 清 pending 后通过 `mr_wdt_event_raise()` 通知。

某些硬件 timeout 会直接 reset，根本没有可执行的 TIMEOUT ISR；这种情况下不要伪造不可能发生的事件能力。

## 7. HardTree 与安装

`MR_HT_WDT_INSTALL` 注册 WDT 实例。HardTree 可描述 watchdog 实例、clock/IRQ 等静态资源；timeout 数值仍由运行时 configure 决定。

## 8. 当前参考

当前可参考：

```text
mrX/arch/st/stm32h5/platform/drivers/wdt/wdt.c
```

该 backend 展示了 IWDG 量化、不可停止的 `stop = MR_NULL`、EWI PRETIMEOUT，
以及 TIMEOUT 直接复位、不伪造事件。公共契约见 `mrX/test/drivers/wdt/wdt.c`。

## 9. 移植检查清单

- 时间单位和量化规则明确；
- configuration 返回实际配置；
- busy 与硬件运行状态一致；
- 不可停止硬件正确表达为无 stop；
- feed window 错误不会被隐藏；
- PRETIMEOUT/TIMEOUT 能力与硬件真实能力一致；
- reset 型 timeout 不伪造 ISR 事件；
- driver 生命周期与硬件生命周期的差异已明确。

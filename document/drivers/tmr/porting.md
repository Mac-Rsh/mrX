# TMR 驱动移植指南

> 本文面向平台 backend 开发者，说明如何实现基础硬件定时器 backend、如何接线周期 signal 与 TIMEOUT 事件，以及哪些能力必须按目标芯片重新确认。公共
> API 见 [TMR](tmr.md)；芯片运行期约束见对应平台的 `README.md`。

## 1. 移植边界

TMR 表示硬件定时器的基础计时角色：周期/单次计数、周期边界硬件 signal 与 update 中断事件。它不提供输入捕获、输出比较或计数值读取；PWM
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

公共层不协调 ADC/DAC/DMA：signal 只控制已经静态接线的硬件输出，消费者何时武装、DMA 何时启动都属于上层组合逻辑。

同一定时器实例在 HardTree 中只能由一种 compatible 拥有：基础计时用途安装 `MR_HT_TMR_INSTALL`，PWM 输出用途安装 PWM
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

必需成员由 `MR_ASSERT` 约束：启用 assert 时缺失即断言失败，未启用 assert 时为未定义行为， **没有**错误码降级路径。

注意 `stop` 在 TMR 上是必需能力，与 WDT 中 `stop` 可选、可用 `MR_NULL` 表达“不可停止”的约定不同：定时器可以被停止，缺少
`stop` 属于 backend 不完整。

`signal_control` 与 `event_control` 是两个相互独立的可选能力：缺少其中一个不影响另一个，也不影响周期 signal 与软件事件的语义独立性。

## 3. Period 字段、单位与量化

`mr_tmr_config_t` 只有 `period` 和 `mode` 两个字段，公共层不校验、不解释、不缓存，`configuration` 直接由 backend 填充。因此
backend 必须在平台文档中写明：`period` 的单位、可表示范围、量化方式、实际误差量级、超范围错误码，以及哪些 mode 被支持。

STM32H5 的映射如下（`mrX/arch/st/stm32h5/platform/drivers/tmr/tmr.c`）：

- `period` 的单位是 **纳秒**。这不是“公共层未规定所以随便定义”的单位，而是该 backend 明确采用的单位，必须写进平台文档。
- 求解流程（读源码即可对照，不必照抄实现）：先得到该实例的 timer kernel clock，再把纳秒换算成整数拍（不足 1
  秒的余数四舍五入到最近一拍）；随后判断拍数是否落在 `counter_max * prescaler_max` 以内；再从“最小可用分频比”（使计数值不超过
  counter width 的最小分频比）开始，在固定长度为 64 个候选的窗口内逐个试算计数值，取拍数误差最小的一组，最终写入 PSC/ARR。
- counter width 由实例决定：32 位计数器实例与 16 位实例的可表示周期不同。同一个 `period` 在 16 位实例上可能只能靠更大的分频比近似，甚至不可表示。
- 误差只保证“候选窗口内 tick 量化误差最小”，不保证纳秒级精确，也不保证全局最优（找到误差 0 的组合会提前结束搜索）。
- 不可表示的 `period` 返回 `-MR_ERANGE`。要区分两类失败：`period == 0` 与非法的 `mode` 是参数错误（`-MR_EINVAL`）；合法但太小的
  `period`（取整后不足 1 拍）或超出可表示范围属于范围错误（`-MR_ERANGE`）。
- `configuration` 返回 backend 保存的配置：`period` 是量化后并折算回纳秒的实际值，`mode` 原样返回。从未成功 `configure`
  过的实例返回 `-MR_ENODEV`（注意与 WDT 的 `-MR_EINVAL` 不同）。
- 失败路径不得留下半成品配置：只有在硬件接受并生效之后，才更新保存的配置与“已配置”状态。

## 4. 前置条件与错误码

STM32H5 的 `configure` 按固定顺序检查，移植时保持“先判参数、再判静态资源、再判动态占用”的顺序，便于调用者区分错误来源：

| 条件                                 | 返回值                                                                      |
|--------------------------------------|-----------------------------------------------------------------------------|
| 实例不是合法的 TIM 实例              | `-MR_EINVAL`                                                                |
| `period == 0`                        | `-MR_EINVAL`                                                                |
| `mode` 高于 `MR_TMR_MODE_ONESHOT`    | `-MR_EINVAL`                                                                |
| 时钟使能钩子缺失                     | `-MR_ENODEV`                                                                |
| 某个 signal 正处于使能状态           | `-MR_EBUSY`                                                                 |
| 定时器正在运行（计数器使能中）       | `-MR_EBUSY`                                                                 |
| `period` 不可表示                    | `-MR_ERANGE`                                                                |
| HAL 时基初始化失败                   | HAL 状态映射（见下）                                                        |
| `configuration` 时时间基准从未初始化 | `-MR_ENODEV`                                                                |
| `start` 时从未成功 `configure`       | `-MR_ENODEV`                                                                |
| `start`/`stop` 的 HAL 状态失败       | `HAL_BUSY` → `-MR_EBUSY`，`HAL_TIMEOUT` → `-MR_ETIMEDOUT`，其它 → `-MR_EIO` |

说明：

- 公共层在进入 `configure`/`start` 之前已经用 `is_busy` 拦过一次运行中的请求（返回 `-MR_EBUSY`）；backend 内部仍要再判一次，因为
  busy 查询会顺带修正 one-pulse 到期后的 HAL 状态，而且 backend 不能假设自己只会被公共层调用。
- 检查顺序是“参数 → 时钟钩子 → signal 占用 → 运行状态”：时钟钩子缺失比 signal 占用更早判定，而只要某个 signal
  还开着，即使定时器空闲也会在时基求解之前被拒绝。
- `configure` 只在 **第一次**成功初始化时设置并使能 NVIC 优先级与中断线。因此“没有订阅 TIMEOUT 事件”不等于“不占用中断向量”。
- 移植时不要把 `-MR_EBUSY`（当前被占用，可稍后重试）与 `-MR_ENOSYS`（本平台无此能力）混用。

## 5. mode、启停与计数状态

- `MR_TMR_MODE_PERIODIC` 与 `MR_TMR_MODE_ONESHOT` 分别映射到硬件的单次触发位（STM32H5 为 `CR1.OPM`）：周期模式清除该位，单次模式置位。
- `start` 需要“重新武装”而不是简单使能计数器：恢复配置的 mode、把计数器清零、清除 update 标志，再根据 TIMEOUT
  是否已订阅选择带中断启动或先关闭 update 中断再启动。计数器清零保证第一个周期是完整的，清标志保证不会带出上一次运行遗留的
  pending。
- 单次模式到期后硬件会自行清除计数器使能位。backend 必须在 busy 查询路径上把 HAL 状态与硬件对齐，否则 busy 会永远为真，公共层就不会再调用
  `stop`，后面的 `start`/`configure` 也会被自己的 busy 挡住。这一对齐是 backend 的责任，公共层无法察觉。
- `stop` 只在公共层观察到 busy 时下传；已经空闲时公共层直接返回 0，backend 不会收到调用。backend 的停止路径要同时关掉计数与中断来源，失败时按第
  4 节的映射返回错误，不能把失败当成已停止。
- 停止与中断来源的对应关系必须写清楚：signal 与 TIMEOUT 中断是独立开关，`stop` 停止的是计数与中断，不是 signal 路由。

## 6. Signal 接线

公共 signal 当前只有 `MR_TMR_SIGNAL_PERIOD`，语义是“自然周期边界产生的硬件信号”。平台可以把它映射到 TRGO、内部触发矩阵或其它
SoC 事件。

STM32H5 的映射是把 update 事件路由到 TRGO：使能时选择 update 作为主输出触发源，禁用时把触发源复位。backend 记录 signal
的使能状态，用于在 `configure` 时拒绝重配。

必须明确“不做什么”：

- 使能 signal **不**启动计数器、 **不**配置消费者（ADC/DAC/DMA）、 **不**使能 TIMEOUT 中断；
- 禁用 signal **不**停止定时器，也不是消费者侧的硬件隔离手段。TIM 的中性 TRGO 配置在软件 UG（软件产生 update
  事件）时仍可能输出脉冲，所以正确顺序是：先完成信号生产者的重配，再武装消费者，而不是靠 signal disable 保护消费者。

`signal_control` 的错误码（STM32H5）：

| 条件                                 | 返回值       |
|--------------------------------------|--------------|
| `signal` 不是 `MR_TMR_SIGNAL_PERIOD` | `-MR_EINVAL` |
| 实例从未成功 `configure`             | `-MR_ENODEV` |
| 实例不具备 master 输出能力           | `-MR_ENOSYS` |
| HAL 主输出配置失败                   | HAL 状态映射 |
| 成功                                 | 0            |

`signal` 的校验先于实例状态检查：非法 signal 值在任何状态下都返回 `-MR_EINVAL`。

平台约束：signal 使能期间重新 `configure` 同一个 TMR 返回 `-MR_EBUSY`。因此“让 signal 先跑着、之后再调整周期或模式”在
STM32H5 上不可行，必须显式 disable 后再重配。

## 7. TIMEOUT 事件与 ISR 顺序

公共事件只有 `MR_TMR_WAIT_TIMEOUT`（bit 0），由 `event_control` 开关硬件来源，ISR 通过 `mr_tmr_event_raise()` 上报。

`event_control` 的错误码（STM32H5）：

| 条件                                           | 返回值          |
|------------------------------------------------|-----------------|
| `events == 0`                                  | `-MR_EINVAL`    |
| `events` 含 `MR_TMR_WAIT_TIMEOUT` 之外的任何位 | `-MR_EINVAL`    |
| 定时器正在运行                                 | `-MR_EBUSY`     |
| 合法请求                                       | 0，记录订阅状态 |

STM32H5 的具体接线：

- 中断源是 update 中断（UIF/UIE）；
- NVIC 优先级与中断线在第一次成功 `configure` 时设置并使能；
- 中断本体在 `start` 时按订阅状态启用：已订阅则走带中断的 HAL 启动路径，未订阅则先关闭 update 中断再启动，避免未订阅时反复进入
  ISR；
- `stop` 走带中断的停止路径，关闭计数与中断。

ISR 顺序是硬性要求：

1. **先取** update 标志 **和**中断使能来源，两者都要判断——只看标志会在未订阅时误报，只看使能位会漏掉真正的 pending；
2. 再把中断交给 HAL 处理（标志位在这一步被清除）；
3. 只有“进入时确有 pending 且事件已订阅”时才 `mr_tmr_event_raise(drv, MR_TMR_WAIT_TIMEOUT)`。

`mr_tmr_event_raise()` 是同步分发：waiter 直接运行在 ISR 上下文中，不得阻塞、不得做长耗时工作；ISR
返回前必须已经清掉会重复触发的中断源。未订阅事件的实例即使因为其它原因进入该向量，也不得上报。

## 8. HardTree 与安装

用 `MR_HT_TMR_INSTALL(_id, _priv, _ops)` 按 compat 实例批量安装；STM32H5 的 compat 为 `st_stm32h5_tmr`，没有对应实例时整个文件被
`MR_HT_HAS_DRIVER()` 裁掉。

STM32H5 backend 读取的属性：

| 属性              | 必需 | 含义                                                 | 默认值 |
|-------------------|------|------------------------------------------------------|--------|
| `st_timx`         | 是   | TIM 实例（如 `TIM2`）                                | 无     |
| `st_clken`        | 是   | 该实例的时钟使能函数名，backend 在生成的钩子里调用它 | 无     |
| `st_irqn`         | 是   | NVIC 中断号                                          | 无     |
| `st_irq_priority` | 是   | NVIC 优先级                                          | 无     |
| `st_irq_handler`  | 是   | 该实例的向量函数名；backend 据此生成中断处理函数     | 无     |

五个属性都用 `MR_HT_DRIVER_PROP` 读取，没有缺省值。`st_clken` 给出的是函数符号名，因为多数平台把 `__HAL_RCC_*_CLK_ENABLE`
一类宏在安装宏里包成无参函数指针。

- 安装宏只描述静态资源并注册驱动，不在安装阶段读取频率、配置时基或启动定时器。时钟与 NVIC 在 `configure` 路径按需初始化。
- 时钟使能只在 `configure` 路径发生；`start` 不会重新调用 `st_clken`。若平台会在运行期关闭定时器时钟，需要自己保证 `start`
  前时钟已开。
- 向量函数只为模板声明过的实例生成，避免与 CubeMX 生成的处理函数抢符号。
- 同一定时器实例不要同时安装 TMR 与 PWM backend。
- **不要假设另一个芯片具备相同能力**：timer kernel clock 的推导方式、哪些实例有 master 输出能力、哪些实例是 32 位计数器、哪些实例走
  APB1/APB2、signal 能接到哪些消费者，都是芯片相关事实，必须按目标芯片手册与 HAL 重新确认，不能从 STM32H5 的属性表继承。

## 9. 参考实现

参考实现：

```text
mrX/arch/st/stm32h5/platform/drivers/tmr/tmr.c
```

值得阅读的函数与符号（不要直接照搬实现，重点是它划分职责的方式）：

- `tmr_is_apb2` / `tmr_clock`：从 RCC 的 PPRE/TIMPRE 与 HAL 频率接口推导 timer kernel clock；
- `tmr_make_period`：纳秒 → (PSC, ARR, 实际纳秒) 的求解、候选窗口搜索与误差最小化；
- `tmr_configure` / `tmr_configuration`：前置条件顺序、时基写入、mode 映射、NVIC 首次使能、配置保存与回读；
- `tmr_hw_busy`：由 `CR1.CEN`/`CR1.OPM` 推导 busy，并在 one-pulse 到期后对齐 HAL 状态（这个函数带副作用，不要当成纯查询）；
- `tmr_start` / `tmr_stop`：重新武装、计数器清零、清 update 标志、中断/轮询两种启动路径、HAL 状态到 mrX 错误码的映射；
- `tmr_signal_control`：update 到 TRGO 的路由与 signal 使能状态记录；
- `tmr_event_control` / `tmr_irq`：事件订阅与 ISR 中“先取标志与来源、再由 HAL 清除、最后上报”的顺序；
- `ST_HT_TMR_INSTALL`：HardTree 属性到私有数据的映射，以及 `st_irq_handler` 向量的生成方式。

该实现固定的时基选择也要注意：向上计数、时钟分频 DIV1、ARR preload 关闭。移植到新平台时应按目标 HAL 与芯片确认这些固定项是否仍然合适。

公共层的行为测试在 `mrX/test/drivers/tmr/tmr.c`，它只覆盖 busy 门控与降级路径，不能作为硬件时序行为的证据。

## 10. STM32H5 能力边界（按目标芯片重新确认，不要默认继承）

- **`period` 单位是纳秒**，不是 timer 拍数，也不是毫秒。
- **求解是窗口搜索**：从最小可用分频比起最多试 64 个候选，取窗口内 tick 误差最小者，误差为 0 时提前结束；不承诺全局最优，也不承诺纳秒级精度。
- **counter width 按实例不同**：16 位与 32 位实例的可表示周期上限不同，`-MR_ERANGE` 的触发点因此依赖实例。
- **signal 只支持 `MR_TMR_SIGNAL_PERIOD`**：其它 signal 值返回 `-MR_EINVAL`；不具备 master 输出能力的实例返回
  `-MR_ENOSYS`。
- **signal 使能期间无法 `configure`**：返回 `-MR_EBUSY`。
- **不支持输入捕获、输出比较、计数值读取**；PWM 由独立驱动与 compatible 拥有。
- **`event_control` 只支持 `MR_TMR_WAIT_TIMEOUT`**，且 `events == 0` 也是 `-MR_EINVAL`。
- **运行中不能改订阅**：`event_control` 在运行中返回 `-MR_EBUSY`；订阅状态只在 `start` 时决定中断是否使能。
- **NVIC 在第一次成功 `configure` 时使能**：未订阅事件的实例同样占用自己的中断向量。
- **`stop` 使用带中断的 HAL 停止路径**：失败按 `HAL_BUSY`/`HAL_TIMEOUT`/其它映射为 `-MR_EBUSY`/`-MR_ETIMEDOUT`/`-MR_EIO`。
- **kernel clock 依赖 RCC 配置**：PPRE 分频、TIMPRE 位与 HAL 的 `HAL_RCC_GetPCLKx`/`HCLKFreq` 必须彼此一致，否则 period
  会系统性偏离；换芯片必须重推这段逻辑。

## 11. 移植验收清单

- [ ] `period` 的单位、可表示范围、量化方式与误差量级已写进平台文档。
- [ ] kernel clock 的推导与目标芯片的 RCC 配置一致（分频、倍频、HAL 频率接口）。
- [ ] period 求解不会溢出，且把“参数非法”与“范围不可表示”区分成 `-MR_EINVAL` 与 `-MR_ERANGE`。
- [ ] `configuration` 返回量化后的实际 `period` 与真实 `mode`；未配置时有明确错误码。
- [ ] `configure` 对实例、时钟钩子、signal 占用、运行中的检查顺序与错误码明确。
- [ ] signal 使能期间拒绝重配（`-MR_EBUSY`）。
- [ ] periodic/oneshot 的 mode 映射正确；`start` 会清零计数器并清除遗留的 update 标志。
- [ ] one-pulse 到期后 busy 能反映已停止，且 HAL 状态被对齐，不需要靠调用者额外 `stop`。
- [ ] 已经空闲时 `stop` 不会到达 backend；停止路径同时关掉中断来源。
- [ ] signal 的硬件映射（TRGO 或等价通路）、`-MR_ENOSYS` 的实例限制、非法 signal 的 `-MR_EINVAL` 都已写明。
- [ ] 使能/禁用 signal 不会启动、停止或配置消费者，也不会开启 TIMEOUT 中断。
- [ ] 消费者武装顺序（生产者先重配、再武装消费者）已在平台文档中给出。
- [ ] `event_control` 对 `events == 0`、未知事件位、运行中的返回值明确。
- [ ] ISR 同时判断 update 标志与中断使能来源，先取后清，只有订阅且确有 pending 时才上报。
- [ ] `mr_tmr_event_raise()` 的同步分发不会在 ISR 中阻塞。
- [ ] HardTree 的 `st_timx`/`st_clken`/`st_irqn`/`st_irq_priority`/`st_irq_handler` 与板级资源一致。
- [ ] 同一定时器实例没有被 TMR 与 PWM 两个 backend 同时安装。
- [ ] 没有把 STM32H5 的能力表（单位、求解窗口、signal 支持面、counter width 分布）当作目标芯片的默认能力。

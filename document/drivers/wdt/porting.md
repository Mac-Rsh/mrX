# WDT 驱动移植指南

> 本文面向平台 backend 开发者，说明如何实现硬件看门狗 backend、如何接线 pretimeout 事件与 HardTree，以及哪些能力必须按目标芯片重新确认。公共
> API 见 [WDT](wdt.md)；芯片运行期约束见对应平台的 `README.md`。

## 1. 移植边界

WDT 没有 stream，也没有 DMA 数据通路。backend 的职责可以概括为三件事：把三个时间字段换算成硬件超时寄存器、启动与喂狗、按硬件真实能力提供可停止性与事件源。

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

同一 SoC 上如果有多个看门狗 IP（独立看门狗、窗口看门狗、系统看门狗），应当各用一个 compatible 和一份 backend 表达，不要把不同
IP 的可停止性、窗口能力和事件能力混进同一个 ops 表。

## 2. 操作表

backend 实现 `mrX/include/mrX/drivers/wdt/mr_wdt.h` 的 `mr_wdt_ops_t`。成员必须按公共头中的名字逐一对应，缺省的可选成员显式写
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

必需成员的缺失由 `MR_ASSERT` 约束：只有在启用 assert 时才断言失败，未启用 assert 的构建里缺失等价于未定义行为。 **没有**
“必需成员留空就自动返回某个错误码”的路径，不要依赖它。

可选成员缺失必须由公共层给出确定行为：运行中缺少 `stop` 得到 `-MR_ENOSYS`（见 [WDT](wdt.md) 中 `mr_wdt_stop` 的约定），缺少
`event_control` 时事件开关得到 `-MR_ENOSYS`。backend 不要为了让回调“看起来完整”而实现一个返回 0 的空 `stop`
——那会把“硬件不可停止”伪装成“已停止”。

## 3. 配置字段、单位与量化

`mr_wdt_config_t` 只有三个字段，公共层不校验取值、不规定单位，也不缓存数值；`configuration` 直接由 backend 填充。因此每个
backend 都必须在自己的平台文档里写明下表内容。

| 字段         | 语义（backend 定义）           | STM32H5（IWDG）取值 |
|--------------|--------------------------------|---------------------|
| `feed_min`   | 允许喂狗的最早时刻（窗口下界） | 必须为 0            |
| `feed_max`   | 超时（窗口上界）               | 毫秒                |
| `pretimeout` | 提前预警时刻                   | 毫秒，0 表示关闭    |

移植时需要明确并写进文档的六件事：单位、0 值含义、`feed_min`/`feed_max` 的窗口关系、`pretimeout`
是相对超时的提前量还是独立时刻、硬件量化方式、可表示范围与超范围错误码。

STM32H5 的映射如下（`mrX/arch/st/stm32h5/platform/drivers/wdt/wdt.c`）：

- 三个字段统一为 **毫秒**。
- `feed_min` 必须为 0：IWDG 的 window 被禁用，不支持喂狗窗口；非 0 直接返回 `-MR_EINVAL`。这条要写进平台文档，否则调用者无法从
  API 看出窗口不受支持。
- `feed_max` 是超时，量化到 (prescaler, reload)：LSI 按 32 kHz 计，分频比从 /4 起按 2 的幂增大（/4 … /1024），reload 上限 4095
  拍；换算时按毫秒向上取整到拍数。
- 超范围判定分两层：显式上界 `WDT_TIMEOUT_MAX` 为 `4096 * 1024 / 32 ms` = 131072 ms，超过它、以及 timeout 为 0，都返回
  `-MR_EINVAL`；再叠加 reload ≤ 4095 的量化约束后，实际可接受的上限约为 131040 ms，`(131040, 131072] ms` 区间同样返回
  `-MR_EINVAL`。超时字段是毫秒整数，1 ms 是可表示的最小非零值（在 /4 分频下正好是 8 拍）。
- `pretimeout` 用 **同一个** prescaler 换算成拍数，不重新求解分频比：换算结果必须 ≥ 2 拍且严格小于超时拍数，否则
  `-MR_EINVAL`；`pretimeout == 0` 表示关闭 EWI。因为与超时共用分频比，pretimeout 的量化步长由超时的 prescaler 决定，请求 1
  个超时拍以内的提前量通常无法表示。
- `configuration` 返回 **已经写进硬件的量化结果**：`feed_min = 0`，`feed_max` 与 `pretimeout`
  由实际拍数反算回毫秒并向下取整。回读值与请求值的差异不超过“一个量化步长 + 1 ms
  向下取整”，可能略小、也可能相等，因此“回读 == 请求”不是契约，“回读是实际生效值”才是。
- 从未成功 `configure` 过的实例调用 `configuration` 返回 `-MR_EINVAL`（注意与 TMR 的 `-MR_ENODEV` 不同）。

## 4. Busy 与生命周期

公共层的 configure/start/stop/feed 全部先调用必需的 `is_busy`，所以 busy 是整套契约的地基：

- `start` 成功后必须立即进入 busy，且 busy 要持续反映硬件真实状态，而不是“driver 处于 setup”之类的软件状态；
- 已经停止时，公共 `stop` 直接返回 0， **不会**调用 backend `stop`；backend 的 `stop` 只需要处理“正在运行”的情况；
- 不可停止的看门狗：`stop = MR_NULL` 是合法且推荐的能力表达，运行中公共 `stop` 返回 `-MR_ENOSYS`；
- 喂狗只在公共层观察到 busy 时下传（见第 5 节）；
- driver 对象销毁（`mr_wdt_del` / shutdown） **不会**停止硬件看门狗，backend 不提供隐式关闭，也没有 clazz deinit 钩子替调用者关狗；
- 一个启动后永不停止的硬件意味着 busy 会一直为真，这是预期行为，不是状态机缺陷。

## 5. Feed 与窗口

- `feed` 只在公共层看到 busy 时到达 backend；不 busy 时公共层固定返回 `-MR_EINVAL`，backend 不会收到该调用。
- 硬件窗口违规（过早或过晚喂狗）必须作为错误返回，不能静默吞掉。若硬件压根没有窗口能力，应当在平台文档中写明“不支持窗口”，并通过
  `configure` 拒绝非 0 的 `feed_min` 把这件事表达出来（STM32H5 即如此），而不是假装窗口恒成立。
- 若喂狗需要写 key/解锁序列（IWDG 需要先解锁寄存器写保护），必须保证该序列不会被并发调用或中断打断成两半，或者明确声明该
  backend 只允许串行调用。

## 6. 事件与 ISR 接线

公共事件位只有 `MR_WDT_WAIT_PRETIMEOUT` 与 `MR_WDT_WAIT_TIMEOUT`。`event_control` 负责开关对应硬件来源，ISR 通过
`mr_wdt_event_raise()` 上报；公共层原样透传 mask，不做合法性过滤。

STM32H5（IWDG）的事件映射：

| 公共事件                 | STM32H5 硬件来源                       | 存在条件                                          |
|--------------------------|----------------------------------------|---------------------------------------------------|
| `MR_WDT_WAIT_PRETIMEOUT` | IWDG 提前唤醒中断（`EWCR.EWIE`）+ NVIC | 只在该实例 `configure` 了非 0 `pretimeout` 时存在 |
| `MR_WDT_WAIT_TIMEOUT`    | 无                                     | 永远不提供                                        |

IWDG 超时是 **芯片复位**，没有可执行的 TIMEOUT ISR。backend 不提供 TIMEOUT 事件，平台文档和示例都不得暗示它可用；这与“事件位在公共头里存在”是两件事。

`event_control` 的错误码（STM32H5）：

| 条件                                                                            | 返回值                                                                                           |
|---------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------|
| `events` 含除 `MR_WDT_WAIT_PRETIMEOUT` 之外的任何位（含 `MR_WDT_WAIT_TIMEOUT`） | `-MR_EINVAL`                                                                                     |
| 该实例没有配置过 pretimeout（EWI 不存在）                                       | `-MR_ENOSYS`                                                                                     |
| 合法请求                                                                        | 0，直接改写 EWIE                                                                                 |
| `events == 0`                                                                   | 没有位需要拒绝，但该实例仍需配置过 pretimeout 才不走上一行；随后 backend 仍按 `enable` 改写 EWIE |

最后一行是 STM32H5 的实际行为，不要以“0 个事件”调用 `event_control`；移植到新平台时应自行决定是否显式拒绝 `events == 0`
，并把结论写进平台文档。

ISR 顺序要求：

1. 硬件事件先由 HAL 回调进入 backend，此时只做“是否需要上报”的判断；
2. 只有在看门狗已经启动、且驱动对象已绑定（否则回调可能早于 `start` 到达）时才上报；
3. 调用 `mr_wdt_event_raise(drv, MR_WDT_WAIT_PRETIMEOUT)`。

`mr_wdt_event_raise()` 是同步分发，waiter 直接运行在 ISR 调用链里，因此 ISR 中不能阻塞、不能长时间持锁，也不要试图在事件回调里“补喂狗”。

若实例配置了 pretimeout，`start` 需要设置 NVIC 优先级并使能该 IRQ；没有 pretimeout 时不要使能一条永远不会触发的向量。

## 7. HardTree 与安装

用 `MR_HT_WDT_INSTALL(_id, _priv, _ops)` 按 compat 实例批量安装；STM32H5 的 compat 为 `st_stm32h5_wdt`，没有对应实例时整个文件被
`MR_HT_HAS_DRIVER()` 裁掉。

STM32H5 backend 读取的属性：

| 属性              | 必需 | 含义                                             | 默认值 |
|-------------------|------|--------------------------------------------------|--------|
| `st_iwdgx`        | 是   | IWDG 实例（如 `IWDG`）                           | 无     |
| `st_irqn`         | 是   | NVIC 中断号                                      | 无     |
| `st_irq_priority` | 是   | NVIC 优先级                                      | 无     |
| `st_irq_handler`  | 是   | 该实例的向量函数名；backend 据此生成中断处理函数 | 无     |

四个属性都用 `MR_HT_DRIVER_PROP` 读取，没有缺省值，HardTree 必须显式声明。

- 时钟：IWDG 走 LSI，LSI 随 IWDG 启动被硬件强制打开；backend 的时钟使能钩子在安装宏里本地生成，作用是确保 RTC 时钟已开。因此这里
  **没有** `st_clken` 属性，不要照抄 TMR 的属性表。
- `st_irqn`、`st_irq_priority`、`st_irq_handler` 只在实例配置了 pretimeout 时被真正使用（NVIC 使能发生在 `start`
  ）；但在模板里它们仍是必需字段。
- 安装宏只描述静态资源并注册驱动，不在安装阶段配置或启动看门狗；超时数值始终由运行时 `configure` 决定。
- **不要假设另一个芯片具备同样的能力**：有没有喂狗窗口、能否停止、timeout 是否复位芯片、有没有可用的提前预警中断、时钟属性怎么描述，都必须按目标芯片的手册和
  HAL 重新确认，而不是从 STM32H5 的属性表继承。

## 8. 参考实现

参考实现：

```text
mrX/arch/st/stm32h5/platform/drivers/wdt/wdt.c
```

值得阅读的函数与符号（不要直接照搬实现，重点是它划分职责的方式）：

- `wdt_quantize_timeout`：毫秒超时到 (prescaler, 拍数) 的量化与超范围判定；
- `wdt_configure` / `wdt_configuration`：请求值与量化值的往返，以及 pretimeout 与超时共用分频比的约束；
- `wdt_start`：时钟钩子、prescaler/reload/window/EWI 写入、pretimeout 下的 NVIC 使能；
- `wdt_feed` / `wdt_is_busy`：喂狗与 busy 来源；
- `wdt_event_control`：mask 校验与 EWIE 改写；
- `HAL_IWDG_EarlyWakeupCallback`：从 HAL 回调把 pretimeout 转发给 owning driver；
- `ST_HT_WDT_INSTALL`：HardTree 属性到私有数据的映射，以及 `st_irq_handler` 向量的生成方式。

公共层的行为测试在 `mrX/test/drivers/wdt/wdt.c`，它只覆盖 busy 门控与降级路径，不能作为硬件时间行为的证据。

## 9. STM32H5 能力边界（按目标芯片重新确认，不要默认继承）

- **不可停止**：`stop = MR_NULL`，没有软件关狗路径，运行中公共 `stop` 得到 `-MR_ENOSYS`。
- **无喂狗窗口**：`feed_min` 必须为 0，非 0 返回 `-MR_EINVAL`。
- **超时上限**：显式上界 131072 ms，受量化约束后实际上限约 131040 ms；超时只能落在 32 kHz 与 /4…/1024
  分频比组合出的离散拍数上，不保证毫秒级精确。
- **pretimeout 受限**：必须是“至少 2 拍且严格小于超时拍数”的相对提前量，且与超时共用分频比；0 表示关闭。
- **事件只有 pretimeout**：TIMEOUT 是芯片复位，不提供公共事件。
- **busy 是软件运行标志**：`start` 时置位后不再清除，不读取硬件的运行状态位。移植时若芯片提供可靠的运行状态位，应优先读硬件。
- **`configure` 不校验实例合法性**：STM32H5 的 WDT backend 在 configure 阶段完全不接触 IWDG 实例寄存器（与 TMR 的
  `IS_TIM_INSTANCE` 检查不同），错误的 `st_iwdgx` 不会被这里拒绝。
- **LSI 精度未做校准**：不要把毫秒值当成绝对时间承诺，也不要在文档里承诺 ms 级精度。
- **`start` 到首次超时的起算点属于 HAL 行为**：backend 只负责把 prescaler/reload/window/EWI 交给 HAL
  初始化并启动，如果应用对“首次超时从配置时刻还是从头一次喂狗起算”敏感，需要按所用 HAL 版本确认并写进平台文档。

## 10. 移植验收清单

- [ ] 时间单位、0 值含义、窗口上下限关系、pretimeout 语义已写进平台文档。
- [ ] `feed_min` 等不支持的能力有明确拒绝路径（`-MR_EINVAL`），不是静默忽略。
- [ ] 超范围与不可表示的超时有明确错误码，且 `-MR_EINVAL`（参数非法）与 `-MR_ENOSYS`（能力缺失）没有混用。
- [ ] `configuration` 返回量化后的实际值；未配置时有明确的错误码。
- [ ] busy 与硬件运行状态一致：`start` 后立刻为真，不可停止硬件的 busy 持续为真。
- [ ] 不可停止硬件用 `stop = MR_NULL` 表达；运行中公共 `stop` 返回 `-MR_ENOSYS`。
- [ ] 已经停止时 `stop` 不会到达 backend，backend 的 `stop` 只处理运行中的情况。
- [ ] `feed` 的窗口或解锁序列违规不会被静默吞掉。
- [ ] 事件能力与硬件真实能力一致：只有确实存在 ISR 的位才会上报。
- [ ] 复位型 timeout 没有伪造 TIMEOUT 事件或空 ISR。
- [ ] ISR 只在已启动且事件有意义时 raise；`raise` 的同步分发型 waiter 不做阻塞工作。
- [ ] pretimeout 未配置时 `event_control` 的行为（`-MR_ENOSYS`）已明确定义。
- [ ] 缺省可选回调不会被“返回 0 的空实现”替换，公共层降级路径保持有效。
- [ ] driver 生命周期与硬件看门狗生命周期的差异（销毁不停狗）已写明。
- [ ] HardTree 的 `st_iwdgx`/`st_irqn`/`st_irq_priority`/`st_irq_handler` 与板级资源一致。
- [ ] 没有把 STM32H5 的能力表当作目标芯片的默认能力。

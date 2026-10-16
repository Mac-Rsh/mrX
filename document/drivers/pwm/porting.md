# PWM 驱动移植指南

> 本文给出 PWM backend 的实现契约：ops 表与必需性、状态门控、纳秒量化、update 事务、fault/signal 语义、事件、
> HardTree 属性和验收清单。公共 API 见 [PWM 驱动](pwm.md)。

---

## 1. 移植边界

PWM 是定时器的一种静态角色，不是 TMR 的运行时模式。同一定时器实例在 HardTree 中只能由一种 compatible 拥有：基础计时用途
安装 TMR，PWM 输出用途安装 PWM；不得在运行期切换角色，也不得在 ops 内临时借用 TMR 对象。多个 backend 可以共用无状态的
周期求解 helper，但不能共享 HAL handle 或寄存器所有权。

本文是 PWM 通道、互补输出、fault 与 signal 资源的权威声明；[ST 平台适配规范](../../arch/st/st.md) 第 6.2 节的
“PWM 移植契约”即指向本文。

| 事项           | 公共层                                                                                                        | backend                                                |
|----------------|---------------------------------------------------------------------------------------------------------------|--------------------------------------------------------|
| 类型与生命周期 | `MR_DRIVER_TYPE_PWM` 校验；setup 校验 `channel_mask != 0` 且 `comp_mask ⊆ channel_mask`；waitable 生命周期    | 不实现 init/deinit；私有状态自行持有                   |
| 资源存在性     | 用 `channel_mask`/`comp_mask`/`fault_mask` 校验资源编号与互补能力                                             | 提供与板级资源一致的掩码（HardTree 属性）              |
| 枚举与范围校验 | `period != 0`、alignment 范围、idle/comp_idle 范围、PERIOD signal 的 `offset == 0`                            | 时间可表示性：量化、对齐支持、死区、phase              |
| busy 门控      | configure 要求控制器空闲；channel_configure 只要求目标通道空闲；start 要求请求通道未运行；stop 过滤已停止通道 | 额外约束：运行中追加通道、任一通道运行时禁止改静态配置 |
| update 事务    | 校验 op 存在、空事务（`count == 0`）直接成功、每个 `channel` 合法；不替 backend 提供提交原子性                         | 先校验全部条目再改硬件；成功返回保持单次事务语义       |
| signal         | 校验 signal 类型与 PERIOD 的 `offset`                                                                         | TRGO/TRGO2 资源、主从同步、消费者接线                  |
| fault 安全     | 只校验资源编号/掩码与 op 是否缺失                                                                             | 硬件关断路径、锁存与清除条件、ISR 顺序                 |
| 事件           | 提供实例级 waitable 与 `mr_pwm_event_raise` 同步分发                                                          | `event_control` 只开关通知来源，不得关闭保护           |

建议实现位置：

```text
mrX/arch/<vendor>/<target>/platform/drivers/pwm/pwm.c
```

整个实现包在 `MR_HT_HAS_DRIVER()` 内；安装宏只保存资源描述，时钟、GPIO alternate、定时器初始化和 NVIC 出现在首次使用路径，
安装时不得点亮任何功率输出。

---

## 2. 操作表

`mr_pwm_ops_t` 的完整成员如下（顺序与头文件一致）。所有成员的第一个参数都是 `const mr_driver_t *drv`。

| ops 成员                | 签名要点                                                 | 必需性 | 公共层在 NULL 时的行为                                                                        |
|-------------------------|----------------------------------------------------------|--------|-----------------------------------------------------------------------------------------------|
| `configure`             | `(drv, const mr_pwm_config_t *config)`                   | 必需   | `mr_pwm_configure` 的 `MR_ASSERT` 失败                                                        |
| `configuration`         | `(drv, mr_pwm_config_t *config)`                         | 必需   | `mr_pwm_configuration` 的 `MR_ASSERT` 失败                                                    |
| `channel_configure`     | `(drv, channel, const mr_pwm_channel_config_t *config)`  | 必需   | `mr_pwm_channel_configure` 的 `MR_ASSERT` 失败                                                |
| `channel_configuration` | `(drv, channel, mr_pwm_channel_config_t *config)`        | 必需   | `mr_pwm_channel_configuration` 的 `MR_ASSERT` 失败                                            |
| `update`                | `(drv, const mr_pwm_update_t *updates, mr_size_t count)` | 可选   | `-MR_ENOSYS`                                                                                  |
| `start`                 | `(drv, mr_uint32_t channels)`（通道掩码）                | 必需   | `mr_pwm_start` 的 `MR_ASSERT` 失败                                                            |
| `stop`                  | `(drv, mr_uint32_t channels)`（通道掩码）                | 必需   | `mr_pwm_stop` 的 `MR_ASSERT` 失败                                                             |
| `state`                 | `(drv, mr_pwm_state_t *state)`                           | 必需   | `mr_pwm_state` 的 `MR_ASSERT` 失败；`configure`/`channel_configure`/`start`/`stop` 也会调用它 |
| `fault_configure`       | `(drv, fault, const mr_pwm_fault_config_t *config)`      | 可选   | `-MR_ENOSYS`（该检查先于 fault 编号校验）                                                     |
| `fault_configuration`   | `(drv, fault, mr_pwm_fault_config_t *config)`            | 可选   | `-MR_ENOSYS`（先于编号校验）                                                                  |
| `fault_clear`           | `(drv, faults)`（fault 掩码）                            | 可选   | `-MR_ENOSYS`（先于掩码校验）                                                                  |
| `signal_configure`      | `(drv, signal, const mr_pwm_signal_config_t *config)`    | 可选   | `-MR_ENOSYS`                                                                                  |
| `signal_configuration`  | `(drv, signal, mr_pwm_signal_config_t *config)`          | 可选   | `-MR_ENOSYS`                                                                                  |
| `signal_control`        | `(drv, signal, mr_bool_t enable)`                        | 可选   | `mr_pwm_signal_enable/disable`：先校验 signal（非法返回 `-MR_EINVAL`），再返回 `-MR_ENOSYS`   |
| `event_control`         | `(drv, mr_uint32_t events, mr_bool_t enable)`            | 可选   | `mr_pwm_event_enable/disable` 返回 `-MR_ENOSYS`                                               |

补充说明：

- `state` 虽然属于“基础成员”，但 `mr_pwm_state` 之外还有四个公共入口依赖它，因此它是事实上的必答项：返回的
  `running_mask` 决定 configure 与 start 的 `-MR_EBUSY` 判定、channel_configure 的目标通道门控，以及 stop 的通道过滤。
- `mr_pwm_event_attach` 不经过 ops：它把 waiter 接到 `mr_pwm_data_t.event` 上。因此没有 `event_control` 的 backend
  仍然可以“挂接 waiter”，只是没有任何来源会触发它。
- 可选成员缺失时的返回值必须是 `-MR_ENOSYS`，不要用逐通道写入模拟原子 `update`，也不要用软件模拟 fault 关断。

公共层在分发前完成的校验（顺序对可观察返回值有影响，需按上表实现）：

| 入口                           | 校验顺序                                                                                                              |
|--------------------------------|-----------------------------------------------------------------------------------------------------------------------|
| `mr_pwm_configure`             | 必需 ops 存在 → `period != 0` 且 `alignment` 合法 → 查询 `state` → `running_mask != 0` 判定 `-MR_EBUSY`               |
| `mr_pwm_channel_configure`     | 必需 ops 存在 → 通道编号在 `channel_mask` 内 → `idle`/`comp_idle` 合法 → `comp` 需在 `comp_mask` 内 → 目标通道未运行  |
| `mr_pwm_update`                | `update` 存在 → 空事务直接返回 0 → 逐个 `channel` 在 `channel_mask` 内                                                |
| `mr_pwm_start`                 | 必需 ops 存在 → 掩码非零且 ⊆ `channel_mask` → 请求通道均未运行                                                        |
| `mr_pwm_stop`                  | 必需 ops 存在 → 掩码非零且 ⊆ `channel_mask` → 与实际 `running_mask` 求交，交集为空时直接返回 0                        |
| `mr_pwm_fault_configure/…`     | `fault_*` 存在 → fault 编号在 `fault_mask` 内（`fault_clear` 校验掩码非零且 ⊆ `fault_mask`）                          |
| `mr_pwm_signal_configure/…`    | `signal_configure`/`signal_configuration` 存在 → signal 类型合法（`signal_configure` 另要求 PERIOD 的 `offset == 0`） |
| `mr_pwm_signal_enable/disable` | signal 类型合法 → `signal_control` 存在                                                                               |
| `mr_pwm_event_enable/disable`  | `event_control` 存在                                                                                                  |

`start`/`stop` 的通道掩码是位掩码，`channel_configure`/`channel_configuration`/`update[].channel` 是 0–31 的编号；
两种形式都不允许超出 `channel_mask`。

---

## 3. 控制器、通道、fault 与 signal 配置模型

**时间量化**：公共时间字段（`period`、`pulse`、`phase`、`dead_rise`、`dead_fall`、fault `filter`、signal `offset`）
统一为纳秒。backend 依据定时器 kernel clock、prescaler 与计数器宽度求解实际值，并遵守：

1. 无法表示的范围返回 `-MR_ERANGE`；公共层已拒绝零 period。
2. `configuration` 类接口返回量化后的实际生效值，不回显未生效的请求值。
3. 硬件不具备某能力返回 `-MR_ENOSYS`；硬件具备但当前参数组合非法返回 `-MR_EINVAL`。
4. 不得静默降级互补输出、死区、fault 或同步更新的安全语义。

中心对齐下公共 `period` 表示完整 PWM 周期，而不是单程 counter 行程，ARR/PSC 求解必须计入上下计数倍率。

**控制器配置**（`mr_pwm_config_t`）：`period`、`cycles`、`alignment`、`update_mode`。`cycles == 0` 为连续输出，
大于 0 表示输出指定数量的完整周期；完成后 `state.running_mask` 必须反映已停止状态。`MR_PWM_UPDATE_PERIOD` 表示更新在
周期边界提交。

**通道配置**（`mr_pwm_channel_config_t`）：`pulse`、`phase`、`dead_rise`、`dead_fall`、`polarity`、`comp_polarity`、
`idle`、`comp_idle`、`comp`。互补输出不是独立公共通道，它与主输出共享 `pulse`/`phase`，由 `comp` 与 `comp_idle`/
`comp_polarity` 控制；`idle`/`comp_idle` 是 `stop` 时必须应用的停止态（无效、有效或高阻）。

**运行期更新**（`mr_pwm_update_t`）：`channel`、`pulse`、`phase`、`state`、`comp_state`。`state`/`comp_state` 可取
PWM/强制无效/强制有效/高阻。backend 必须先校验完整事务；成功返回时整批更新作为一次逻辑提交。对无法回滚的外部总线器件，若硬件提交途中发生 I/O 失败，可返回错误并将物理状态标记为需要重新同步，但不得把目标软件状态当作已成功提交。

**fault**：fault 编号与电气来源（GPIO、比较器、timer break 输入）由 HardTree/板级静态确定，运行时配置只设置使能、极性、
滤波与 cycle-by-cycle/锁存模式。锁存故障只能在输入源解除后由 `fault_clear` 清除。`event_control` 只能控制通知，不得
关闭 fault 的硬件保护。

**signal**：每个控制器只有一个对外 signal；`MR_PWM_SIGNAL_PERIOD` 在完整周期边界产生，`MR_PWM_SIGNAL_COMPARE` 在周期内
可配置的 `offset` 位置产生，两者不能同时启用。TRGO/TRGO2、内部 compare 资源、计数方向与消费者连接由 HardTree/backend
固定；若 COMPARE 占用硬件通道，该通道必须从普通 `channel_mask` 中排除。signal 的启停不得启停 PWM 通道或 wait event。

**推荐调用顺序**：`configure` → `channel_configure`（每个通道一次）→ `start` → 运行期用 `update` 调整脉宽/相位。
重新配置时基后，通道静态配置必须重新应用；实现应把“时基已重配、通道待重配”的状态显式表达出来，而不是静默沿用旧比较值。

---

## 4. 状态门控、原子 update 与错误码

公共层的门控行为（backend 需要理解，因为它决定 backend 能看到什么）：

- `mr_pwm_configure` 在任一通道运行时返回 `-MR_EBUSY`，backend 不会收到该请求；时基影响到所有通道，因此不能在线重配。
- `mr_pwm_channel_configure` 只对目标通道做忙判定；其他通道在运行不阻止本次配置的透传。若硬件不允许在任一通道运行时修改
  其他通道的静态配置，backend 必须自己返回 `-MR_EBUSY`。
- `mr_pwm_start` 只拒绝“请求掩码中已在运行”的通道；若请求通道之外的通道正在运行，请求会到达 backend，由 backend 决定
  是接受还是返回 `-MR_ENOSYS`。
- `mr_pwm_stop` 已经把已停止通道过滤掉，因此 backend 收到的掩码只包含正在运行的通道；`stop` 返回前必须应用 idle state。
- `mr_pwm_update` 在进入 backend 前完成资源编号校验；完整参数预校验、成功提交原子性以及失败后的硬件重新同步策略由 backend 负责。

**错误码约定**

| 错误码                                    | 公共层条件                                                                                                                                    | backend 应承担的条件                                                          |
|-------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------|
| `-MR_EINVAL`                              | `period == 0`、alignment/idle/comp_idle 越界、通道编号越界、掩码为零或超出 `channel_mask`/`fault_mask`、signal 非法或 PERIOD 的 `offset != 0` | 资源编号未在静态映射中；参数组合非法（如 `pulse > period`）                   |
| `-MR_ENOSYS`                              | 可选 ops 为 `MR_NULL`（`update`、`fault_*`、`signal_*`、`event_control`）；请求互补输出但通道不在 `comp_mask`                                 | 硬件或本 backend 不具备该能力（对齐、死区、phase、有限周期、COMPARE、事件等） |
| `-MR_EBUSY`                               | 控制器有通道在运行时的 configure；目标通道在运行时的 channel_configure；请求通道已运行的 start                                                | 运行中追加通道；任一通道运行时改其他通道静态配置；signal 启用期间重配控制器   |
| `-MR_ENODEV`                              | —                                                                                                                                             | 尚未 `configure`；目标通道尚未 `channel_configure`；控制器时钟 thunk 缺失     |
| `-MR_ERANGE`                              | —                                                                                                                                             | 时间/计数换算不可表示或溢出                                                   |
| `-MR_EIO` / `-MR_ETIMEDOUT` / `-MR_EBUSY` | —                                                                                                                                             | HAL 调用返回 `HAL_ERROR` / `HAL_TIMEOUT` / `HAL_BUSY` 时的映射                |

`mr_pwm_update` 对 `count == 0` 的空事务直接返回 0，且此时允许 `updates == MR_NULL`；backend 的 `update`
只会被 `count >= 1` 的请求调用，因此不必处理空事务。这与公共头文件注释一致。

---

## 5. 事件、ISR 与上下文

- 事件位为 `MR_PWM_WAIT_PERIOD` 和 `MR_PWM_WAIT_FAULT`；`event_control` 只开关通知来源，不启用/停用 PWM 通道，也不
  启用/停用 wait event 本身。
- `mr_pwm_event_raise` 在调用者上下文同步分发 waiter：waiter 不得阻塞；`data` 可指向 ISR 栈或 backend 临时对象，但只在
  同步分发期间有效。
- fault 的安全关断必须由硬件或 backend ISR 独立完成，不等待 waiter；ISR 应先清除/锁存硬件状态并使输出进入安全态，再调用
  `mr_pwm_event_raise`。
- 事件未订阅时，fault 关断和有限周期计数所需的硬件逻辑仍必须有效。
- 多步寄存器写入（如同时更新多个通道的比较值、切换输出使能）必须用 `mr_port_irq_disable`/`mr_port_irq_enable` 或等价手段
  保护，避免半更新状态。公共层不加锁：同一实例的 configure/start/stop/fault_clear 由调用方串行化；若 `update` 允许在 ISR 与
  线程间并发，backend 必须自行保证成功提交时的单次事务原子性；外部总线在提交途中失败且硬件不可回滚时，必须返回错误、保留旧软件状态，并在平台文档中规定重新同步方法。
- 不得在 waiter 回调里调用可能等待 HAL 时基的阻塞式 HAL 停止。

---

## 6. HardTree 属性

compatible 为 `st_stm32h5_pwm`。驱动节点属性分两组：公共资源掩码由 `MR_HT_PWM_INSTALL` 读取，芯片相关属性由 backend
的安装宏读取。

| 属性           | 要求 | 含义                                                 | 默认值               |
|----------------|------|------------------------------------------------------|----------------------|
| `channel_mask` | 必填 | 实际存在的逻辑 PWM 通道掩码                          | 无（缺失即编译失败） |
| `comp_mask`    | 可选 | 支持互补输出的通道掩码，必须是 `channel_mask` 的子集 | `0`                  |
| `fault_mask`   | 可选 | 已接线的 fault 输入掩码                              | `0`                  |
| `st_timx`      | 必填 | 定时器实例                                           | 无                   |
| `st_clken`     | 必填 | 定时器时钟使能 thunk                                 | 无                   |
| `st_channels`  | 必填 | 逻辑通道到 HAL 通道、GPIO、AF 的映射表               | 无                   |

`st_channels` 的单元布局：

| 下标 | 含义            | 约束                                                                                                            |
|------|-----------------|-----------------------------------------------------------------------------------------------------------------|
| 0    | logical channel | 必须在 `channel_mask` 中置位；若 `channel_mask` 声明了未在此表列出的编号，backend 侧配置该通道返回 `-MR_EINVAL` |
| 1    | HAL channel     | 定时器的 CCx 通道编码（`TIM_CHANNEL_1` … `TIM_CHANNEL_4`）                                                      |
| 2    | GPIO phandle    | 该引脚所属的 GPIO 驱动实例，构成 `mr_ht_gpio_spec_t.drv`                                                        |
| 3    | pin             | 端口内引脚号，0–15                                                                                              |
| 4    | af              | alternate function 编号，0–15                                                                                   |

示例（仅示意的单元布局，实际的 phandle 与宏名以板级模板为准）：

```text
st_channels = <<0 TIM_CHANNEL_1 <&gpioa> 8 1>
               <1 TIM_CHANNEL_2 <&gpioa> 9 1>>;
```

约束与安装规则：

- `channel_mask` 必须只包含 `st_channels` 中实际列出的 logical channel。若掩码声明了未列出的通道，公共层的编号校验会
  通过，但 `channel_configure` 在 backend 找不到映射时失败——这属于模板错误，不要用默认全通道掩码掩盖它。
- GPIO 引脚必须落在对应 GPIO 实例的 `exist_mask` 内，且 AF 编号与数据手册一致；引脚复用通过 GPIO 拥有者的 helper 完成，
  PWM backend 不直接写第二套 HAL GPIO 初始化。
- 本 backend 不使用定时器中断，因此没有 `st_irqn`/`st_irq_handler` 属性；需要周期或 fault 通知的 port 必须显式声明自己
  的中断资源，而不是照搬本节的属性表。
- 安装宏只填充 priv 与注册驱动；时钟、GPIO alternate 与 `HAL_TIM_PWM_Init` 在 `configure`/`start` 路径按需完成，重复配置必须
  幂等。本 backend 不使用 NVIC；需要周期或 fault 中断的 port 才在首次使用路径配置自己的中断资源。

---

## 7. 参考实现

```text
mrX/arch/st/stm32h5/platform/drivers/pwm/pwm.c
```

值得阅读的函数（按它们解决的问题）：

| 函数                                                                         | 看什么                                                             |
|------------------------------------------------------------------------------|--------------------------------------------------------------------|
| `pwm_configure`                                                              | 能力拒绝顺序、signal 启用互锁、时基重配后清空通道配置标记          |
| `pwm_make_period`、`pwm_make_pulse`、`pwm_nanoseconds_to_ticks`、`pwm_clock` | 纳秒↔计数换算、prescaler 搜索与量化误差、中心对齐的倍率处理        |
| `pwm_channel_configure`                                                      | 通道映射查找、HAL 通道能力校验、互补/死区/phase 的拒绝、pulse 回写 |
| `pwm_update`                                                                 | 事务前置校验、临界区保护、运行中与停止时不同的提交方式             |
| `pwm_start`、`pwm_stop`                                                      | 多通道一次性使能、idle state 应用、计数器与 UPDATE 标志处理        |
| `pwm_signal_configure`、`pwm_signal_configuration`、`pwm_signal_control`     | PERIOD 的固定语义、TRGO 配置与状态保存时机                         |
| `ST_HT_PWM_INSTALL`、`__pwm_ops`                                             | HardTree 属性如何展开成通道表与 ops 表                             |

运行期语义（中心的 repetition counter、signal 与重配关系）另见 `mrX/arch/st/stm32h5/README.md` 的 PWM 段落。

---

## 8. STM32H5 能力边界（新 port 必须重新确认，不要直接继承）

下表每一项都是 H5 backend 的实测行为，不是 PWM 驱动的通用要求。新 port 必须按自身硬件逐条重新确认，并在自己的平台文档中
写明结论。

| 能力                  | H5 现状                                                                   | 新 port 需要确认                                                |
|-----------------------|---------------------------------------------------------------------------|-----------------------------------------------------------------|
| 普通主输出            | 支持（PWM 模式 1，带 CCR preload）                                        | 输出模式与 preload 行为                                         |
| 对齐方式              | 支持 `EDGE_UP`、`EDGE_DOWN`、`CENTER`                                     | 支持哪些计数模式；中心对齐的周期定义                            |
| `update_mode`         | 仅 `MR_PWM_UPDATE_PERIOD`；`IMMEDIATE` 返回 `-MR_ENOSYS`                  | 能否做到立即更新且不出现半更新状态                              |
| 多通道 `update`       | 仅单通道；`count > 1` 返回 `-MR_ENOSYS`                                   | 能否在同一周期边界原子提交多通道                                |
| `update` 输出状态     | 只接受 `state == MR_PWM_OUTPUT_PWM`，`comp_state` 不参与校验              | 能否支持强制有效/无效/高阻切换                                  |
| 有限周期 `cycles > 0` | 不支持，返回 `-MR_ENOSYS`                                                 | repetition counter / one-pulse / IRQ 计数的可行性与完成语义     |
| 互补输出与死区        | 不支持（`comp`、`dead_rise`、`dead_fall` 返回 `-MR_ENOSYS`）              | 主/互补引脚、死区发生器、`comp_mask` 与 `idle`/`comp_idle` 语义 |
| `phase`               | 必须为 0，否则 `-MR_ENOSYS`                                               | 是否支持相移；相移的时间基准                                    |
| fault                 | 三个 fault ops 均为 `MR_NULL`，公共层返回 `-MR_ENOSYS`                    | break 输入来源、极性、滤波、cycle-by-cycle/锁存与清除条件       |
| signal                | 仅 `MR_PWM_SIGNAL_PERIOD`，`offset` 必须为 0；`COMPARE` 返回 `-MR_ENOSYS` | TRGO/TRGO2 资源、COMPARE 占用的通道是否需从 `channel_mask` 排除 |
| wait event            | `event_control` 为 `MR_NULL`，公共层返回 `-MR_ENOSYS`                     | 周期/fault 中断来源与订阅方式                                   |
| 多通道 start          | 定时器空闲时一次性使能；运行中追加通道返回 `-MR_ENOSYS`                   | 能否逐通道启停；不能时如何保证同步使能                          |
| 运行中改静态配置      | 任一通道运行时 `channel_configure` 返回 `-MR_EBUSY`                       | 能否在线修改其他通道配置                                        |
| signal 与重配互锁     | signal 启用期间 `configure` 返回 `-MR_EBUSY`                              | signal 与重配、start/stop 的互相约束                            |
| 时基重配后的通道状态  | 通道配置标记被清空；`channel_configuration` 在未重配时返回 `-MR_ENODEV`   | 硬件是否保留旧比较值、是否强制要求重新配置通道                  |
| 停止语义              | 立即生效：写比较值 + 切换输出使能，最后一个通道停止后关闭定时器           | 周期末生效还是立即生效；是否需要硬件 commutation                |
| 量化方式              | prescaler 搜索窗口 64，按最小误差选点；16/32 位计数器区别处理             | 时钟树、计数器宽度、可接受的量化误差                            |
| 多控制器同步          | 不支持（signal 只导出 TRGO，不接收外部同步）                              | 主从同步、armed 状态在 `running_mask` 中的表示                  |

---

## 9. 移植验收清单

- [ ] 模板无该 compatible 时翻译单元被 `MR_HT_HAS_DRIVER()` 裁空；同一 timer 不会同时安装 TMR 与 PWM owner。
- [ ] 15 个 ops 成员按第 2 节实现或置 `MR_NULL`；七个必需成员不为空，可选成员缺失时返回 `-MR_ENOSYS`。
- [ ] `state` 返回准确的 `running_mask` 与 `fault_mask`，四个依赖它的公共入口（configure/channel_configure/start/stop）行为正确。
- [ ] `channel_mask`/`comp_mask`/`fault_mask` 与板级资源一致，且与 `st_channels` 的 logical channel 集合完全对应。
- [ ] `st_channels` 五个单元的含义与取值范围已在平台文档中登记。
- [ ] period/pulse/phase/dead-time/filter 的纳秒量化、溢出与回读正确；不可表示返回 `-MR_ERANGE`，不支持返回 `-MR_ENOSYS`。
- [ ] 中心对齐下公共 `period` 表示完整周期；signal 每个完整周期只产生一次。
- [ ] 多通道 `update`/`start` 成功返回时不出现半提交或相间毛刺；不能提供成功时的整批提交就返回 `-MR_ENOSYS`。外部总线 I/O 失败若可能留下未知物理状态，文档明确恢复/重同步方法。
- [ ] fault 无需 waiter 即可关断输出，锁存/清除条件与 idle state 正确；`event_control` 不会关闭硬件保护。
- [ ] signal 的使能/禁用不启停 PWM 通道与 wait event；signal 与控制器重配的互锁已实现并写入文档。
- [ ] 时基重配后通道配置必须重新应用的规则已实现，`*_configuration` 在未生效时返回 `-MR_ENODEV` 而不是旧值。
- [ ] 多步寄存器写入有临界区保护；并发约束（同实例需外部串行化）已写入平台文档。
- [ ] 安装宏只填充描述；时钟、GPIO alternate、HAL init、NVIC 在首次使用路径完成且幂等。
- [ ] 第 8 节的每一项能力都在新平台文档中重新确认并给出结论，没有照抄 STM32H5 的限制。
- [ ] 平台失败路径（start/stop/update 失败）后的硬件状态与可观察状态已在文档中说明，调用方可以安全重试或回退。

# PWM 驱动移植指南

> 本文给出 PWM backend 的实现契约：ops 表与必需性、状态门控、时基量化、update
> 事务、fault/signal 语义、事件、
> HardTree 属性和验收清单。公共 API 见 [PWM 驱动](pwm.md)。

---

## 1. 移植边界

PWM 和 TMR 均可能使用同一类底层计时资源，但它们是不同的公共驱动类型。
当具体平台为二者分配同一硬件实例时，backend 必须保证资源独占与状态一致，
不得在两个独立设备对象之间共享未经同步的硬件上下文；无状态的换算工具可复用。

本文定义 PWM 通道、互补输出、fault 与 signal 的公共移植契约；
各平台自行记录实际可用资源与实现限制。

| 事项           | 公共层                                                                                                        | backend                                                |
|----------------|---------------------------------------------------------------------------------------------------------------|--------------------------------------------------------|
| 类型与生命周期 | `MR_DRIVER_TYPE_PWM` 校验；setup 校验 `channel_mask != 0` 且 `comp_mask ⊆ channel_mask`；waitable 生命周期    | 不实现 init/deinit；私有状态自行持有                   |
| 资源存在性     | 用 `channel_mask`/`comp_mask`/`fault_mask` 校验资源编号与互补能力                                             | 提供与板级资源一致的掩码（HardTree 属性）              |
| 枚举与范围校验 | `period != 0`、alignment 范围、idle/comp_idle 范围、PERIOD signal 的 `offset == 0`                            | 时间可表示性：量化、对齐支持、死区、phase              |
| busy 门控      | configure 要求控制器空闲；channel_configure 只要求目标通道空闲；start 要求请求通道未运行；stop 过滤已停止通道 | 额外约束：运行中追加通道、任一通道运行时禁止改静态配置 |
| update 事务    | 校验 op 存在、空事务（`count == 0`）直接成功、每个 `channel` 合法；不替 backend 提供提交原子性                | 先校验全部条目再改硬件；成功返回保持单次事务语义       |
| signal         | 校验 signal 类型与 PERIOD 的 `offset`                                                                         | TRGO/TRGO2 资源、主从同步、消费者接线                  |
| fault 安全     | 只校验资源编号/掩码与 op 是否缺失                                                                             | 硬件关断路径、锁存与清除条件、ISR 顺序                 |
| 事件           | 提供实例级 waitable 与 `mr_pwm_event_raise` 同步分发                                                          | `event_control` 只开关通知来源，不得关闭保护           |

建议实现位置：

```text
mrX/arch/<vendor>/<target>/platform/drivers/pwm/pwm.c
```

整个实现包在 `MR_HT_HAS_DRIVER()` 内；安装宏只保存资源描述，时钟、GPIO
alternate、定时器初始化和 NVIC 出现在首次使用路径，
安装时不得点亮任何功率输出。

---

## 2. 操作表

`mr_pwm_ops_t` 的完整成员如下（顺序与头文件一致）。所有成员的第一个参数都是
`const mr_driver_t *drv`。

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
  `running_mask` 决定 configure 与 start 的 `-MR_EBUSY` 判定、channel_configure
  的目标通道门控，以及 stop 的通道过滤。
- `mr_pwm_event_attach` 不经过 ops：它把 waiter 接到 `mr_pwm_data_t.event` 上。因此没有
  `event_control` 的 backend
  仍然可以“挂接 waiter”，只是没有任何来源会触发它。
- 可选成员缺失时的返回值必须是 `-MR_ENOSYS`，不要用逐通道写入模拟原子 `update`
  ，也不要用软件模拟 fault 关断。

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

`start`/`stop` 的通道掩码是位掩码，`channel_configure`/`channel_configuration`/
`update[].channel` 是 0–31 的编号；
两种形式都不允许超出 `channel_mask`。

---

## 3. 控制器、通道、fault 与 signal

实现 `mr_pwm_ops_t` 所定义的控制器时基、通道配置、update、启停和状态读取。`channel_mask` 只标识实际可用资源，不能把某一芯片的通道数量当作通用限制。fault、互补输出、相位、死区、有限周期与 signal 的支持范围由 backend 声明，不支持的组合返回 `-MR_ENOSYS`。

## 4. 状态与原子更新

控制器与通道配置更改应考虑运行状态及硬件提交时机，确保多通道更新不会产生不受控的中间状态。`signal` 是向外设消费者提供硬件触发的独立能力，不等价于 wait event，也不自动启动定时器或 ADC/DAC。支持 `MR_PWM_SIGNAL_PERIOD` 或 `MR_PWM_SIGNAL_COMPARE` 的 backend 应说明该信号如何映射硬件。

## 5. 中断与事件

可选事件控制必须按公共事件位分发，关闭或销毁设备前先屏蔽中断来源。waiter 是同步通知，回调不能阻塞或在未明确允许的情况下重入配置路径。

## 6. HardTree 与安装

用 `MR_HT_PWM_INSTALL` 或驱动框架约定注册实例。定时器资源、硬件 channel、复用引脚、时钟、触发路由和中断向量是平台属性，应在平台文档定义，不可作为通用 PWM 配置字段。

## 7. 移植验收

- [ ] 通道掩码、时基、占空、相位与状态机符合公共接口。
- [ ] start/stop/update 的并发及原子性可解释，资源冲突不误报为成功。
- [ ] signal、fault、事件及可选功能均按本平台能力明确实现或拒绝。

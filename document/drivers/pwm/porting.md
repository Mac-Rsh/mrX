# PWM 驱动移植指南

本文面向平台与外设驱动开发者，说明如何实现 `mr_pwm_ops_t` 并接入 PWM 驱动框架。公共
API 与使用方式见 [PWM 驱动](pwm.md)。

## 移植边界

| 职责                                                                                      | 归属       |
|-------------------------------------------------------------------------------------------|------------|
| 类型校验、`setup` 时资源掩码合法性校验（`channel_mask != 0`、`comp_mask ⊆ channel_mask`） | PWM 公共层 |
| `mr_pwm_data_t.event` 的初始化与销毁                                                      | PWM 公共层 |
| `period != 0`、对齐/idle 枚举、通道/互补/故障掩码校验                                     | PWM 公共层 |
| 基于 `ops->state` 的 `-MR_EBUSY` 门控与 `stop` 已停通道过滤                               | PWM 公共层 |
| 可选回调缺失时的 `-MR_ENOSYS` 返回                                                        | PWM 公共层 |
| 时基分频与纳秒量化、对齐/死区/相位硬件映射                                                | Backend    |
| `update` 事务全批校验与原子提交、周期边界更新                                             | Backend    |
| 硬件故障关断路径、锁存清除与同步信号（TRGO/Compare）路由                                  | Backend    |

PWM 与 TMR 若共用同一硬件定时器，backend 必须在驱动层保证资源独占，禁止两个驱动实例无保护地操作同一硬件上下文。

## 操作表（ops）

### 必需回调

公共层入口对下列成员使用 `MR_ASSERT` 检查，不可为 `MR_NULL`：

| 成员                    | 原型要点                                                            | 约定                                                                                                          |
|-------------------------|---------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------|
| `configure`             | `(drv, const mr_pwm_config_t *config) -> mr_err_t`                  | 量化并配置时基周期、有限周期数、对齐与更新模式；不支持组合返回 `-MR_ENOSYS`                                   |
| `configuration`         | `(drv, mr_pwm_config_t *config) -> mr_err_t`                        | 返回量化后的实际生效配置                                                                                      |
| `channel_configure`     | `(drv, channel, const mr_pwm_channel_config_t *config) -> mr_err_t` | 配置单通道脉宽、相位、极性、死区、idle 与互补使能                                                             |
| `channel_configuration` | `(drv, channel, mr_pwm_channel_config_t *config) -> mr_err_t`       | 回读量化后的通道配置                                                                                          |
| `start`                 | `(drv, mr_uint32_t channels) -> mr_err_t`                           | 启动掩码指定的通道输出                                                                                        |
| `stop`                  | `(drv, mr_uint32_t channels) -> mr_err_t`                           | 停止掩码指定的通道，并立即应用配置的 `idle` 状态                                                              |
| `state`                 | `(drv, mr_pwm_state_t *state) -> mr_err_t`                          | 返回当前 `running_mask` 与 `fault_mask`（公共层 `configure`/`channel_configure`/`start`/`stop` 均依赖此接口） |

### 可选回调

未实现的可选成员置 `MR_NULL`，公共层将返回 `-MR_ENOSYS`（切勿用逐通道写入模拟原子
`update`，或用软件模拟硬件故障关断）。

| 成员                   | 原型要点                                                             | 约定                                                                  |
|------------------------|----------------------------------------------------------------------|-----------------------------------------------------------------------|
| `update`               | `(drv, const mr_pwm_update_t *updates, mr_size_t count) -> mr_err_t` | 先校验全部条目再原子提交；`MR_PWM_UPDATE_PERIOD` 需在共同周期边界生效 |
| `fault_configure`      | `(drv, fault, const mr_pwm_fault_config_t *config) -> mr_err_t`      | 配置故障输入滤波、极性与锁存/逐周期模式                               |
| `fault_configuration`  | `(drv, fault, mr_pwm_fault_config_t *config) -> mr_err_t`            | 回读故障配置                                                          |
| `fault_clear`          | `(drv, mr_uint32_t faults) -> mr_err_t`                              | 清除锁存故障标志                                                      |
| `signal_configure`     | `(drv, signal, const mr_pwm_signal_config_t *config) -> mr_err_t`    | 配置 `MR_PWM_SIGNAL_PERIOD` / `COMPARE` 触发信号参数                  |
| `signal_configuration` | `(drv, signal, mr_pwm_signal_config_t *config) -> mr_err_t`          | 回读同步信号配置                                                      |
| `signal_control`       | `(drv, signal, mr_bool_t enable) -> mr_err_t`                        | 开关控制器对外同步信号                                                |
| `event_control`        | `(drv, mr_uint32_t events, mr_bool_t enable) -> mr_err_t`            | 开关 `MR_PWM_WAIT_PERIOD` / `MR_PWM_WAIT_FAULT` 中断源                |

## 关键契约

- **安装阶段静默**：`MR_HT_PWM_INSTALL`
  仅注册资源描述，不得在系统启动安装阶段开启时钟或点亮任何功率引脚；外设初始化应延迟到
  `configure` / `start` 路径。
- **原子更新与异常恢复**：`update`
  必须先完成所有条目的合法性校验再触碰硬件。对于无法物理回滚的外部串行芯片，若提交中途发生总线错误，必须返回错误且不得更新软件缓存状态。
- **故障安全与事件**：硬件故障关断必须由硬件/刹车路径直接完成，不可依赖
  `mr_pwm_event_raise` 的 waiter 回调。`mr_pwm_event_raise` 在调用者上下文同步派发，
  `data` 指针仅在派发期间有效。
- **Shutdown 行为**：公共层 `pwm_deinit` 不会自动调用 `ops->stop`
  ；若平台有特殊安全要求，应在平台文档中明确说明。

## HardTree 安装

使用 `<mrX/drivers/pwm/mr_pwm.h>` 提供的 `MR_HT_PWM_INSTALL(_id, _priv, _ops)`
宏安装实例，该宏从节点读取以下标准属性：

| 属性           | 必需 | 默认值 | 说明                     |
|----------------|------|--------|--------------------------|
| `channel_mask` | 是   | -      | 物理存在的通道位掩码     |
| `comp_mask`    | 否   | `0`    | 支持互补输出的通道位掩码 |
| `fault_mask`   | 否   | `0`    | 已接线的故障输入位掩码   |

定时器基址、时钟、引脚复用与信号路由等平台私有属性由平台自行定义并填充 `_priv`。

## 验收要点

- [ ] `configure` 与 `channel_configure` 正确量化纳秒参数并在 `*_configuration`
  返回实际生效值。
- [ ] `start` / `stop` / `state` 状态一致，停止后输出严格进入配置的 `idle`
  电平或高阻态。
- [ ] `update` 保证全批校验与多通道同步提交语义，非法条目不产生部分写入。
- [ ] 互补死区、故障锁存/清除、同步信号及事件开关按硬件能力准确实现或返回
  `-MR_ENOSYS`。

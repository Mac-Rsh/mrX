# PWM 驱动（PWM）

PWM 驱动在设备驱动框架之上提供共享时基的多通道脉宽调制抽象，支持对齐方式与周期更新控制、互补输出与死区、故障保护、硬件同步信号（signal）以及事件通知。

| 项目   | 说明                                                       |
|--------|------------------------------------------------------------|
| 功能宏 | `MR_USE_DRIVER_PWM`                                        |
| 头文件 | `<mrX/drivers/pwm/mr_pwm.h>`                               |
| 依赖   | [驱动框架](../driver.md)、[等待队列](../../kernel/wait.md) |
| 测试   | `test/drivers/pwm/pwm.c`                                   |

## 配置

| 配置项              | 类型 | 默认值 | 说明                                  |
|---------------------|------|--------|---------------------------------------|
| `MR_USE_DRIVER_PWM` | bool | n      | 启用 PWM 驱动（依赖 `MR_USE_DRIVER`） |

## 快速上手

```c
#include <mrX/drivers/pwm/mr_pwm.h>

mr_err_t pwm_three_phase_start(const char *name) {
    mr_driver_t *pwm = mr_driver_find(name);
    mr_pwm_config_t ctrl = {
        .period = 50000U,           /* 20 kHz (50000 ns) */
        .cycles = 0U,               /* 连续输出 */
        .alignment = MR_PWM_ALIGN_CENTER,
        .update_mode = MR_PWM_UPDATE_PERIOD,
    };
    mr_pwm_channel_config_t ch = {
        .pulse = 25000U,
        .phase = 0U,
        .dead_rise = 500U,
        .dead_fall = 500U,
        .polarity = MR_PWM_POLARITY_ACTIVE_HIGH,
        .comp_polarity = MR_PWM_POLARITY_ACTIVE_HIGH,
        .idle = MR_PWM_IDLE_INACTIVE,
        .comp_idle = MR_PWM_IDLE_INACTIVE,
        .comp = MR_TRUE,
    };
    mr_pwm_update_t updates[] = {
        {0U, 12000U, 0U, MR_PWM_OUTPUT_PWM, MR_PWM_OUTPUT_PWM},
        {1U, 25000U, 0U, MR_PWM_OUTPUT_PWM, MR_PWM_OUTPUT_PWM},
        {2U, 38000U, 0U, MR_PWM_OUTPUT_PWM, MR_PWM_OUTPUT_PWM},
    };
    mr_err_t ret;

    if ((pwm == MR_NULL) || (mr_driver_setup(pwm) != 0)) {
        return -MR_ENODEV;
    }

    ret = mr_pwm_configure(pwm, &ctrl);
    if (ret != 0) {
        return ret;
    }
    for (mr_uint32_t i = 0U; i < 3U; i++) {
        ret = mr_pwm_channel_configure(pwm, i, &ch);
        if (ret != 0) {
            return ret;
        }
    }
    ret = mr_pwm_start(pwm, 0x07U);
    if (ret != 0) {
        return ret;
    }
    return mr_pwm_update(pwm, updates, 3U);
}
```

## 核心机制与约束

- **时间单位与资源掩码**：`period`、`pulse`、`phase`、`dead_rise`、`dead_fall`、
  `filter` 和 `offset` 统一以 **纳秒**为单位；后端负责量化为硬件计数，并通过
  `*_configuration` 返回实际生效值。`mr_pwm_data_t` 中的 `channel_mask`、
  `comp_mask`、`fault_mask` 分别描述物理通道、互补通道和故障输入资源（bit n 对应编号
  n）；`setup` 阶段会校验 `channel_mask != 0` 且 `comp_mask` 为 `channel_mask`
  的子集。
- **状态门控**：控制器配置（`mr_pwm_configure`）影响共享时基，任一通道运行时返回
  `-MR_EBUSY`。通道配置（`mr_pwm_channel_configure`）仅要求目标通道未运行，请求互补输出但通道不在
  `comp_mask` 时返回 `-MR_ENOSYS`。`mr_pwm_start` 在任一请求通道已运行时返回
  `-MR_EBUSY`；`mr_pwm_stop` 只停止请求掩码中正在运行的通道，全部已停止时直接返回
  `0`。
- **原子更新**：运行中调整脉宽、相位或输出状态应使用 `mr_pwm_update`。
  `updates[0..count-1]` 作为单个逻辑事务提交（`count == 0` 直接返回 `0`），
  `MR_PWM_UPDATE_PERIOD` 要求所有通道在同一周期边界生效。
- **故障与硬件信号**：故障输入（`fault`）支持 cycle-by-cycle 与锁存模式，锁存故障需调用
  `mr_pwm_fault_clear` 清除。硬件同步信号（`MR_PWM_SIGNAL_PERIOD` /
  `MR_PWM_SIGNAL_COMPARE`）属于整个控制器而非单个通道，其中 `PERIOD` 信号的
  `offset` 必须为 `0`；启用信号不会自动启动通道或事件。
- **生命周期与安全**：`pwm_deinit`（`shutdown`）仅销毁内部 `mr_waitable_t`，
  **不会自动停止 PWM 输出**；调用方必须在 `shutdown` 或 `del` 前显式调用
  `mr_pwm_stop` 并确认功率级安全。公共层无内部锁，配置、启停与故障清除需由调用方串行化；
  `mr_pwm_event_raise` 可在中断中调用，此时 waiter 回调同步执行且不可阻塞，
  `mr_pwm_event_data_t` 指针仅在当次派发期间有效。

## API 速查

### 生命周期与管理

| API                                            | 说明                                                                |
|------------------------------------------------|---------------------------------------------------------------------|
| `MR_PWM_DEFINE` / `MR_PWM_INSTALL`             | 编译期静态定义 / 安装 PWM 驱动                                      |
| `MR_PWM_DATA_INIT`                             | 初始化 `mr_pwm_data_t`（`channel_mask`、`comp_mask`、`fault_mask`） |
| `mr_pwm_init` / `mr_pwm_create` / `mr_pwm_del` | 初始化静态实例 / 动态创建 / 删除 PWM 驱动                           |
| `mr_pwm_get` / `mr_pwm_put`                    | 驱动引用计数加一 / 减一                                             |

### 控制器与通道操作

| API                                                         | 说明                                                                    |
|-------------------------------------------------------------|-------------------------------------------------------------------------|
| `mr_pwm_configure` / `mr_pwm_configuration`                 | 配置 / 回读控制器时基（`period`、`cycles`、`alignment`、`update_mode`） |
| `mr_pwm_channel_configure` / `mr_pwm_channel_configuration` | 配置 / 回读通道参数（脉宽、相位、极性、死区、idle 状态、互补使能）      |
| `mr_pwm_update`                                             | 原子更新一个或多个通道的 `pulse`、`phase` 与主/互补输出状态             |
| `mr_pwm_start` / `mr_pwm_stop`                              | 按通道位掩码启动 / 停止 PWM 输出（停止后应用配置的 `idle` 状态）        |
| `mr_pwm_state`                                              | 读取当前 `running_mask` 与 `fault_mask`                                 |

`cycles == 0` 表示连续输出，大于 `0` 表示输出指定周期数。`mr_pwm_update_t` 中的
`state` / `comp_state` 支持 `MR_PWM_OUTPUT_PWM`、`MR_PWM_OUTPUT_INACTIVE`、
`MR_PWM_OUTPUT_ACTIVE` 和 `MR_PWM_OUTPUT_HIGH_Z`。

### 故障、硬件信号与事件

| API                                                       | 说明                                                          |
|-----------------------------------------------------------|---------------------------------------------------------------|
| `mr_pwm_fault_configure` / `mr_pwm_fault_configuration`   | 配置 / 回读故障输入（滤波、极性、模式、使能）                 |
| `mr_pwm_fault_clear`                                      | 按掩码清除锁存故障                                            |
| `mr_pwm_signal_configure` / `mr_pwm_signal_configuration` | 配置 / 回读同步信号偏移（`MR_PWM_SIGNAL_PERIOD` / `COMPARE`） |
| `mr_pwm_signal_enable` / `mr_pwm_signal_disable`          | 开启 / 关闭同步信号输出                                       |
| `mr_pwm_event_attach`                                     | 订阅 `MR_PWM_WAIT_PERIOD` / `MR_PWM_WAIT_FAULT` 事件          |
| `mr_pwm_event_enable` / `mr_pwm_event_disable`            | 开关后端硬件事件源                                            |
| `mr_pwm_event_raise`                                      | 同步派发事件与可选的 `mr_pwm_event_data_t`（供后端调用）      |

### HardTree 规格辅助接口

| API / 宏                                                              | 说明                                                                              |
|-----------------------------------------------------------------------|-----------------------------------------------------------------------------------|
| `MR_HT_PWM_SPEC` / `MR_HT_NODE_PWM_SPEC`                              | 从 `[phandle, channel]` 属性构造 `mr_ht_pwm_spec_t`                               |
| `MR_HT_PWM_SPEC_FOREACH` / `MR_HT_NODE_PWM_SPEC_FOREACH`              | 展开多通道 spec 数组                                                              |
| `mr_ht_pwm_configure` / `configuration`                               | 通过 spec 配置 / 回读控制器                                                       |
| `mr_ht_pwm_channel_configure` / `configuration`                       | 通过 spec 配置 / 回读通道                                                         |
| `mr_ht_pwm_update`                                                    | 通过 spec 单通道原子更新 `pulse` 与 `phase`（输出状态固定为 `MR_PWM_OUTPUT_PWM`） |
| `mr_ht_pwm_start` / `stop` / `state`                                  | 通过 spec 启停通道 / 查询控制器状态                                               |
| `mr_ht_pwm_signal_configure` / `configuration` / `enable` / `disable` | 通过 spec 操作控制器同步信号                                                      |
| `mr_ht_pwm_event_attach` / `enable` / `disable`                       | 通过 spec 操作控制器事件                                                          |

## 内部实现

`drivers/pwm/pwm.c` 实现类型/掩码/枚举校验、`state` 状态门控及 `waitable`
生命周期；公共层不缓存配置副本，所有 `configuration` 与 `state` 查询均直接透传后端。PWM
与 TMR 为独立驱动类型，若底层共用同一硬件定时器，应在 HardTree 中仅启用其一。

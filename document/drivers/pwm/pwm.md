# PWM 驱动（PWM）

> PWM 驱动在统一设备模型上提供控制器时基、多通道输出、互补死区、故障保护和硬件 signal 接口。

| 项目     | 说明                                                                   |
|----------|------------------------------------------------------------------------|
| 功能宏   | `MR_USE_DRIVER_PWM`（默认关闭）                                        |
| 头文件   | `<mrX/drivers/pwm/mr_pwm.h>`                                           |
| 依赖模块 | [驱动框架 Driver](../driver.md)、[等待队列 Wait](../../kernel/wait.md) |

---

## 用户指南

### 1. 模块概述

一个 PWM 驱动实例表示一个共享时基的控制器。控制器配置周期、对齐方式和更新时机；通道配置脉宽、相位、
极性、互补输出、死区和停止后的输出状态。`channel_mask`、`comp_mask` 和 `fault_mask`
描述实例实际拥有的通道与 fault 资源，位 n 对应编号 n。硬件同步 signal 属于整个控制器，不属于单个 PWM 通道。

PWM 与 [TMR](../tmr/tmr.md) 是两种并列驱动类型。同一硬件定时器应在 HTS 中只选择一种 compatible，不应在运行时
在 TMR 和 PWM 之间切换所有权。

公共接口支持：

- 边沿向上、边沿向下和中心对齐；
- 立即更新或在共同周期边界提交多通道更新；
- 主/互补输出、上下沿死区和安全 idle 状态；
- cycle-by-cycle 或锁存故障保护；
- 用于 ADC、DAC 或 DMA 同步的可配置硬件 signal。

具体 backend 可以只实现其硬件支持的可选能力。公共层发现对应 callback 为空时返回 `-MR_ENOSYS`。

### 2. 配置与依赖

| 配置项              | 类型 | 默认值 | 说明                                    |
|---------------------|------|--------|-----------------------------------------|
| `MR_USE_DRIVER_PWM` | bool | n      | 启用 PWM 公共驱动，依赖 `MR_USE_DRIVER` |

启用后重新生成 `mr_config.h`。

`period`、`pulse`、`phase`、`dead_rise`、`dead_fall`、fault `filter` 和 signal `offset`
统一使用纳秒。backend 负责量化为硬件计数，并在 `*_configuration` 接口中返回实际生效值。

### 3. 快速上手

下例配置三个 20 kHz 中心对齐互补通道，然后以一个事务更新三相脉宽。示例省略返回值处理：

```c
#include <mrX/drivers/pwm/mr_pwm.h>

static void pwm_start_three_phase(mr_driver_t *pwm) {
    mr_pwm_config_t controller = {
        .period = 50000U,
        .cycles = 0U,
        .alignment = MR_PWM_ALIGN_CENTER,
        .update_mode = MR_PWM_UPDATE_PERIOD,
    };
    mr_pwm_channel_config_t channel = {
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

    mr_pwm_configure(pwm, &controller);
    mr_pwm_channel_configure(pwm, 0U, &channel);
    mr_pwm_channel_configure(pwm, 1U, &channel);
    mr_pwm_channel_configure(pwm, 2U, &channel);
    mr_pwm_start(pwm, 0x07U);
    mr_pwm_update(pwm, updates, 3U);
}
```

`cycles == 0` 表示连续输出；大于 0 表示输出指定数量的完整周期。运行中调整脉宽或相位应使用
`mr_pwm_update`，不应重新调用通道 configure。

### 4. 使用说明

**控制器与通道配置**

- `mr_pwm_configure` 要求 `period != 0`，并校验 alignment/update mode。任一通道正在输出时返回
  `-MR_EBUSY`。
- `mr_pwm_channel_configure` 只允许配置 `channel_mask` 中的通道；正在运行的目标通道返回
  `-MR_EBUSY`。
- 请求互补输出时，通道还必须存在于 `comp_mask`，否则返回 `-MR_ENOSYS`。
- 公共层校验枚举值和资源编号。硬件能否表示具体脉宽、相位、死区或对齐组合，由 backend 校验。
- `mr_pwm_start` 在任一请求通道已运行时返回 `-MR_EBUSY`；`mr_pwm_stop` 只把请求掩码中实际运行的
  通道交给 backend，全部已停止时直接返回 0。

**原子更新与输出状态**

`mr_pwm_update` 的 `updates[0..count-1]` 是单个逻辑事务：backend 必须在访问硬件前完成整批参数校验；成功返回时，整批更新必须作为一次提交生效。对于无法物理回滚的外部总线器件，如果提交过程中发生 I/O 错误，允许返回错误并把物理输出状态视为待重新同步，但不得把新的软件状态发布为成功。
`MR_PWM_UPDATE_PERIOD` 要求所有通道在同一周期边界生效。每个 update 可把主/互补输出分别设为 PWM、强制无效、
强制有效或高阻。

**故障、signal 与事件**

- fault 可配置有效极性、滤波时间和 cycle-by-cycle/锁存模式。锁存故障通过 `mr_pwm_fault_clear` 清除。
- `MR_PWM_SIGNAL_PERIOD` 在完整周期边界产生；`MR_PWM_SIGNAL_COMPARE` 在配置的 `offset` 位置产生。
- 每个控制器只有一个对外 signal，同一时刻只能启用一种来源。计数方向、TRGO/TRGO2、compare 资源和消费者连接由 HTS/backend 固定。
- `mr_pwm_event_attach` 只订阅 waitable；`event_enable/disable` 调用可选 `event_control`。
- `mr_pwm_event_raise` 同步分发 waiter。`data` 可为 `MR_NULL`；非空指针只在当次分发期间有效。

| 事件                 | 含义               |
|----------------------|--------------------|
| `MR_PWM_WAIT_PERIOD` | PWM 周期边界       |
| `MR_PWM_WAIT_FAULT`  | fault 状态发生变化 |

**HardTree**

`MR_HT_PWM_INSTALL` 从驱动节点读取下列公共属性：

| 属性           | 要求    | 含义                |
|----------------|---------|---------------------|
| `channel_mask` | 必填    | 实际存在的 PWM 通道 |
| `comp_mask`    | 可选，0 | 支持互补输出的通道  |
| `fault_mask`   | 可选，0 | 已接线的 fault 输入 |

消费者属性用 `[phandle, channel]` 创建 `mr_ht_pwm_spec_t`。同一个 spec 既可操作目标通道，也可操作其所属控制器的 signal；
signal 的硬件资源不作为独立消费者 spec 暴露。

### 5. API 参考

| API                                                         | 说明                                   |
|-------------------------------------------------------------|----------------------------------------|
| `mr_pwm_init` / `mr_pwm_create` / `mr_pwm_del`              | 初始化静态实例、创建动态实例、删除驱动 |
| `mr_pwm_get` / `mr_pwm_put`                                 | 获取和释放驱动引用                     |
| `mr_pwm_configure` / `mr_pwm_configuration`                 | 配置/查询控制器公共时基                |
| `mr_pwm_channel_configure` / `mr_pwm_channel_configuration` | 配置/查询通道                          |
| `mr_pwm_update`                                             | 成功时原子更新一个或多个通道           |
| `mr_pwm_start` / `mr_pwm_stop` / `mr_pwm_state`             | 启停通道并查询运行/fault 状态          |
| `mr_pwm_fault_*`                                            | 配置、查询和清除 fault                 |
| `mr_pwm_signal_*`                                           | 配置、查询和控制控制器硬件信号         |
| `mr_pwm_event_*`                                            | 订阅、启停和上报事件                   |
| `MR_PWM_DEFINE` / `MR_PWM_INSTALL` / `MR_HT_PWM_INSTALL`    | 定义或安装静态驱动                     |

所有返回 `mr_err_t` 的接口都以 0 表示成功，负数表示错误码。指针和驱动类型是编程前置条件，由 `MR_ASSERT` /
`MR_DRIVER_ASSERT` 检查。

**控制器、通道和状态**

```c
mr_err_t mr_pwm_configure(mr_driver_t *drv, const mr_pwm_config_t *config);
mr_err_t mr_pwm_configuration(mr_driver_t *drv, mr_pwm_config_t *config);
mr_err_t mr_pwm_channel_configure(mr_driver_t *drv, mr_uint32_t channel,
                                  const mr_pwm_channel_config_t *config);
mr_err_t mr_pwm_channel_configuration(mr_driver_t *drv, mr_uint32_t channel,
                                      mr_pwm_channel_config_t *config);
mr_err_t mr_pwm_update(mr_driver_t *drv, const mr_pwm_update_t *updates,
                       mr_size_t count);
mr_err_t mr_pwm_start(mr_driver_t *drv, mr_uint32_t channels);
mr_err_t mr_pwm_stop(mr_driver_t *drv, mr_uint32_t channels);
mr_err_t mr_pwm_state(mr_driver_t *drv, mr_pwm_state_t *state);
```

`channel` 是 0～31 的通道编号，`channels` 是非零且不超出 `channel_mask` 的位掩码。`count == 0` 的空事务不执行任何更新并
直接返回 `0`，此时 `updates` 允许为 `MR_NULL`；`count > 0` 时 `updates` 必须指向 `count` 个条目。资源不存在或字段枚举越界时返回
`-MR_EINVAL`；不支持 update 时返回 `-MR_ENOSYS`。

**fault 和 signal**

```c
mr_err_t mr_pwm_fault_configure(mr_driver_t *drv, mr_uint32_t fault,
                                const mr_pwm_fault_config_t *config);
mr_err_t mr_pwm_fault_configuration(mr_driver_t *drv, mr_uint32_t fault,
                                    mr_pwm_fault_config_t *config);
mr_err_t mr_pwm_fault_clear(mr_driver_t *drv, mr_uint32_t faults);
mr_err_t mr_pwm_signal_configure(mr_driver_t *drv, mr_pwm_signal_t signal,
                                 const mr_pwm_signal_config_t *config);
mr_err_t mr_pwm_signal_configuration(mr_driver_t *drv,
                                     mr_pwm_signal_t signal,
                                     mr_pwm_signal_config_t *config);
mr_err_t mr_pwm_signal_enable(mr_driver_t *drv, mr_pwm_signal_t signal);
mr_err_t mr_pwm_signal_disable(mr_driver_t *drv, mr_pwm_signal_t signal);
```

`fault` 是资源编号，`faults` 是非零位掩码。signal 必须是 `MR_PWM_SIGNAL_PERIOD` 或
`MR_PWM_SIGNAL_COMPARE`；PERIOD 的 `offset` 必须为 0。signal 配置只能在所有 PWM 通道停止时修改。
callback 缺失返回 `-MR_ENOSYS`，其余 backend 错误原样透传。

**事件**

```c
mr_err_t mr_pwm_event_attach(mr_driver_t *drv, mr_waiter_t *entry);
mr_err_t mr_pwm_event_enable(mr_driver_t *drv, mr_uint32_t events);
mr_err_t mr_pwm_event_disable(mr_driver_t *drv, mr_uint32_t events);
mr_err_t mr_pwm_event_raise(mr_driver_t *drv, mr_uint32_t events,
                            const mr_pwm_event_data_t *data);
```

事件接口与其他驱动保持一致，框架不校验事件 mask；调用者应使用上表定义的事件位，底层
`event_control` 可按硬件能力处理不支持的位。

### 6. 完整示例

除下面的单路输出外，PWM signal 触发 ADC 的同步采集、通用启停顺序和
STM32H5 PERIOD/COMPARE 能力边界见[驱动组合与进阶示例](../examples.md)。当前 PWM
公共 API 不提供外部 PWM 的输入捕获。

下例使用 HardTree 通道 spec 配置单路 PWM，更新脉宽后查询状态；省略错误分支：

```dts
pwm1: pwm@40010000 {
    compatible = "vendor,soc-pwm";
    channel_mask = 0x0f;
    comp_mask = 0x07;
    fault_mask = 0x01;
};

fan0: fan {
    pwm = <&pwm1 3>;
};
```

```c
static mr_ht_pwm_spec_t fan_pwm =
    MR_HT_NODE_PWM_SPEC(MR_HT_LABEL(fan0), pwm);

static void fan_start(void) {
    mr_pwm_config_t controller = {
        .period = 40000U,
        .cycles = 0U,
        .alignment = MR_PWM_ALIGN_EDGE_UP,
        .update_mode = MR_PWM_UPDATE_PERIOD,
    };
    mr_pwm_channel_config_t channel = {
        .pulse = 10000U,
        .polarity = MR_PWM_POLARITY_ACTIVE_HIGH,
        .idle = MR_PWM_IDLE_INACTIVE,
    };
    mr_pwm_state_t state;

    mr_ht_pwm_configure(&fan_pwm, &controller);
    mr_ht_pwm_channel_configure(&fan_pwm, &channel);
    mr_ht_pwm_start(&fan_pwm);
    mr_ht_pwm_update(&fan_pwm, 20000U, 0U);
    mr_ht_pwm_state(&fan_pwm, &state);
}
```

### 7. 常见问题（FAQ）

**为什么 configure 返回 `-MR_EBUSY`？**

控制器配置会影响共享时基，因此只有所有通道停止时才能修改。通道 configure 也不能修改正在输出的目标通道。

**为什么请求互补输出返回 `-MR_ENOSYS`？**

目标通道不在 `comp_mask` 中。确认硬件通道和引脚是否支持互补输出，并修正 HTS 属性。

**为什么 update/fault/signal/event 返回 `-MR_ENOSYS`？**

当前 backend 没有实现该可选 callback。应根据硬件能力选择其他实例、在 backend 补齐能力，或调整上层功能。

**删除驱动时会自动停止输出吗？**

不会。PWM clazz 的 shutdown 只删除内部 waitable。调用方必须先停止所有输出，并确认功率级处于安全状态。

---

## 开发者指南

### 8. 架构总览

PWM 公共层分成三部分：

1. `mr_pwm.h` 定义数据结构、ops、生命周期包装、HardTree 安装/spec 和公共 API。
2. `pwm.c` 完成类型/资源/枚举校验、busy 门控、可选 callback 门控和 waitable 生命周期。
3. 平台 backend 实现时钟求解、寄存器操作、IRQ/DMA、fault 安全路径和触发路由。

公共层不保存配置副本，`configuration` 和 `state` 都由 backend 返回当前有效状态。

### 9. 数据结构

`mr_pwm_data_t` 保存通道、互补、fault 三种静态资源掩码和一个实例级 waitable。资源由平台和 HardTree 确定，不提供运行时查询接口。

`mr_pwm_config_t` 描述公共时基；`mr_pwm_channel_config_t` 描述静态通道行为；`mr_pwm_update_t` 携带可在运行中按单个逻辑事务改变的
通道、脉宽、相位和两路输出状态。成功返回表示整批更新已经提交；外部串行器件的 I/O 失败恢复规则由 backend 文档说明。配置枚举在结构中使用与其取值范围匹配的位域，与现有驱动结构风格一致。

### 10. 核心流程

- setup 校验 `channel_mask != 0` 且 `comp_mask` 是其子集，然后初始化 waitable。
- controller configure 先校验配置，再调用必需 `state`；任一通道运行时拒绝重配。
- channel configure 校验通道和互补能力，然后仅对目标通道做 busy 门控。
- start 拒绝任一已运行的请求通道；stop 过滤掉已停止通道，使其具有幂等的空操作路径。
- update 在进入 backend 前校验所有条目，但事务提交本身仍是 backend 责任。
- shutdown 只销毁 waitable；release 只释放动态 `mr_pwm_data_t`。

### 11. 并发与中断安全

公共层不为同一 PWM 实例建立大锁。调用方应串行化 configure、start/stop 和 fault clear。若 update 允许同时从
ISR 和线程调用，backend 必须自己保证成功提交时的单次事务原子性，并明确 I/O 失败后的重新同步规则。

`mr_pwm_event_raise` 在调用者上下文同步执行 waiter。从 IRQ 上报时，所有 waiter 回调都必须满足平台的 ISR 限制。fault
的安全关断不得依赖 waiter 执行。

### 12. 扩展与派生

新 backend 先实现 `configure`、`configuration`、`channel_configure`、`channel_configuration`、`start`、
`stop` 和 `state` 七个基础 callback。其他 callback 按硬件能力独立实现，未实现的字段置 `MR_NULL`。

平台实现、安全契约、HardTree 资源和验收项见 [PWM 驱动移植指南](porting.md)。

### 13. 源码导读

| 位置                                   | 职责                                             |
|----------------------------------------|--------------------------------------------------|
| `mrX/include/mrX/drivers/pwm/mr_pwm.h` | 公共类型、ops、API、生命周期和 HardTree 包装     |
| `mrX/drivers/pwm/pwm.c`                | 公共校验、能力门控、状态门控和 waitable 生命周期 |
| `mrX/include/mrX/drivers/pwm/Kconfig`  | PWM 功能开关                                     |

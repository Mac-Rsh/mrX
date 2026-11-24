# PCA9685 PWM

I2C 接口的 16 通道 12 位外部 PWM 控制器，通过标准 PWM
驱动接口提供连续、边沿对齐输出能力（遵循[串行总线外扩驱动共同行为](../serial_peripheral.md)）。

| 项目     | 说明                                                                                                  |
|----------|-------------------------------------------------------------------------------------------------------|
| 启用方式 | 随 `MR_USE_DRIVER_PWM` 与 `MR_USE_DRIVER_I2C` 编译，由 HardTree `compatible = "pwm_pca9685"` 节点触发 |
| 头文件   | 复用 `<mrX/drivers/pwm/mr_pwm.h>`                                                                     |

## HardTree 属性

| 属性            | 必需 | 默认值     | 说明                                                         |
|-----------------|------|------------|--------------------------------------------------------------|
| `i2c`           | 是   | -          | 挂载的 I2C 总线控制器 phandle                                |
| `addr`          | 是   | -          | 7 位 I2C 从机地址（`0x40`~`0x77`，不含 ALLCALL 地址 `0x70`） |
| `oscillator_hz` | 否   | `25000000` | 内部振荡器标定频率（Hz，范围 `1000000`~`50000000`）          |
| `channel_mask`  | 否   | `0xffff`   | 可用通道掩码（按 16 位裁剪，不可为 `0`）                     |

`comp_mask` 与 `fault_mask` 固定为 `0`。芯片 `OE` 引脚必须硬件接低电平（驱动不控制
`OE` GPIO）。

## 快速上手

```c
#include <mrX/drivers/pwm/mr_pwm.h>

mr_err_t pca9685_servo_ch0(void) {
    mr_driver_t *pwm = mr_driver_find("servo");
    mr_pwm_config_t ctrl = {
        .period = 20000000ULL,      /* 50 Hz (20 ms) */
        .cycles = 0U,
        .alignment = MR_PWM_ALIGN_EDGE_UP,
        .update_mode = MR_PWM_UPDATE_IMMEDIATE,
    };
    mr_pwm_channel_config_t ch = {
        .pulse = 1500000ULL,        /* 1.5 ms 脉宽 */
        .phase = 0U,
        .polarity = MR_PWM_POLARITY_ACTIVE_HIGH,
        .idle = MR_PWM_IDLE_INACTIVE,
    };
    mr_err_t ret;

    if ((pwm == MR_NULL) || (mr_driver_setup(pwm) != 0)) {
        return -MR_ENODEV;
    }
    ret = mr_pwm_configure(pwm, &ctrl);
    if (ret != 0) {
        return ret;
    }
    ret = mr_pwm_channel_configure(pwm, 0U, &ch);
    if (ret != 0) {
        return ret;
    }
    return mr_pwm_start(pwm, 1U << 0);
}
```

## 运行语义与限制

- **控制器与时基**：16 通道共享同一周期，仅支持 `MR_PWM_ALIGN_EDGE_UP`、连续输出（
  `cycles = 0`）与 `MR_PWM_UPDATE_IMMEDIATE`。预分频器范围为 `3..255`（在 25 MHz
  振荡器下对应约 `655360..41943040` ns），超出范围返回 `-MR_ERANGE`。每次调用
  `mr_pwm_configure` 会清除所有通道配置并将全部输出置为 full-off。
- **通道配置与量化**：通道必须先调用 `mr_pwm_channel_configure` 才能 `start` 或
  `update`（未配置查询返回 `-MR_ENODEV`）。`pulse`（`0..period`）与 `phase`（
  `0..period - 1`）按 4096 刻度量化，`0%` 与 `100%` 使用硬件 full-off/full-on
  位。仅支持 `MR_PWM_POLARITY_ACTIVE_HIGH` 与 `MR_PWM_IDLE_INACTIVE`
  ，不支持互补输出、死区与高阻态。
- **单事务原子提交**：`mr_pwm_update` 支持已配置通道的 `PWM` / `INACTIVE` /
  `ACTIVE` 状态（`HIGH_Z` 返回 `-MR_ENOSYS`），不接受重复通道或超过 16
  个条目。所有通道寄存器（共 65 字节）通过单次 I2C `STOP` 事务提交，底层 I2C 控制器必须支持
  65 字节单消息传输；若传输失败，软件状态保持上一次成功快照，应用需重发目标以重新同步硬件输出。
- **不支持的功能**：不支持故障保护、硬件同步信号（signal）、周期边界更新与异步等待事件；
  `shutdown` / `del` 不会自动关闭输出，停止前需显式调用 `mr_pwm_stop`。

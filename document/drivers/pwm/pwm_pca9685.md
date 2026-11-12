# PCA9685 PWM

复用 [PWM API](pwm.md)
，并遵守[串行总线外扩驱动共同行为](../serial_peripheral.md)。

| 项目     | 说明                                              |
|----------|---------------------------------------------------|
| 功能宏   | `MR_USE_DRIVER_PWM`，及对应的 `MR_USE_DRIVER_I2C` |
| 头文件   | `<mrX/drivers/pwm/mr_pwm.h>`                      |
| 依赖模块 | I2C、Driver；仅 HardTree 有实例时编译             |

## 用户指南

### HardTree 和接线

```dts
servo: servo {
    compatible = "pwm_pca9685";
    status = "defer";
    i2c = &i2c0;
    addr = <0x40>;
    oscillator_hz = <25000000>;
};
```

启用 PWM/I2C。`i2c`、`addr` 必需；
本版本接受个体地址 0x40..0x77，但排除默认 ALLCALL 地址 0x70。
`channel_mask` 缺省 0xffff 并裁剪为 16 位，不能为零；
互补和 fault mask 固定为 0。
`oscillator_hz` 缺省 25 MHz，范围 1..50 MHz，
用于内部振荡器的频率标定，不会启用 EXTCLK。 **OE 必须接低**，驱动不接管 OE
GPIO。电机/舵机供电由板级电路负责。

### API 与时间语义

所有时间单位为纳秒。16 通道共享一个周期；
只支持 EDGE_UP、连续输出（cycles=0）、UPDATE_IMMEDIATE。
prescaler 取最接近的整数，范围 3..255；25 MHz 时周期约 655360..41943040
ns。请求周期超出器件可表示范围时返回 `-MR_ERANGE`。
`configuration` 返回量化后的有效周期。

通道必须先 configure，再 start；周期 configure 会清除全部通道配置，并将所有输出置为
full-off。此后查询尚未重新配置的通道返回 `-MR_ENODEV`。通道支持 pulse、phase，
active-high、idle-inactive；拒绝反相极性、高阻、互补和死区。
pulse 在 0..period，phase 在 0..period-1，按 4096 个计数刻度量化，
配置查询返回量化后的时间。0% 和 100% 使用独立 full-off/full-on 位。
phase 与 pulse 跨周期时按硬件计数器回绕。

```c
mr_pwm_config_t timing = {
    .period = 20000000ULL, .cycles = 0U,
    .alignment = MR_PWM_ALIGN_EDGE_UP,
    .update_mode = MR_PWM_UPDATE_IMMEDIATE,
};
mr_pwm_channel_config_t channel = {
    .pulse = 1500000ULL, .phase = 0U,
    .polarity = MR_PWM_POLARITY_ACTIVE_HIGH,
    .idle = MR_PWM_IDLE_INACTIVE,
};
mr_pwm_configure(drv, &timing);
mr_pwm_channel_configure(drv, 0U, &channel);
mr_pwm_start(drv, 1U);
```

应用须逐步检查返回值，先 configure controller，再 configure channel，再 start。
update 支持已配置通道的 PWM/INACTIVE/ACTIVE 状态；主输出 HIGH_Z 返回
`-MR_ENOSYS`。`state`/`comp_state` 在公共结构中是 2-bit 位域，本器件没有互补输出，因此
`comp_state` 不参与硬件提交。
停止通道上的 update 只修改后续 start 的目标值。
不接受重复通道或超过 16 个条目的事务。
stop 对选中通道施加 full-off，保留配置供下次启动。
fault、signal、wait event 及周期边界更新返回不支持。

## 开发者指南

### 提交和恢复

MODE1 使用 AI，关闭 group address；MODE2 使用 OCH=0 和推挽输出。
所有通道组成一个 65 字节 I2C 消息，STOP 时统一更新输出。
参数验证全部成功后才访问总线；不支持 UPDATE_PERIOD。
底层 controller 必须支持完整 65 字节消息，不能拆为独立 STOP 事务。
总线传输失败仍可能在 STOP 时提交部分寄存器，
不能把通信错误解释为物理输出原子回滚；此时 API
返回总线错误，软件仍保留最后一次成功状态，恢复总线后应重发完整目标以重新同步物理输出。
软件配置只在成功提交后更新，state 是最后成功提交的运行通道集合，
不读取物理输出。

首次 configure 要求芯片处于上电/复位后的内部时钟状态；
不能接管已由其他软件设置 EXTCLK 的实例。
配置期间依次休眠、写分频和输出、唤醒，等待 500 us 后 restart。
临时提交副本含 16 通道配置，调用栈需要为约 1 KiB 私有快照及消息留出空间。
公共 PWM shutdown 没有自动 stop 契约，应用在 shutdown 前应显式 stop；
不要依赖删除对象关闭外部芯片输出。

源码：`drivers/pwm/pwm_pca9685.c`。
协议依据：[NXP 数据手册](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf)。
本轮仅人工静态审查，未构建或测试。


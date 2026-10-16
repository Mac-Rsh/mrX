# PCF8574 / PCF8574A GPIO

复用 [GPIO API](gpio.md)，并遵守[串行总线外扩驱动共同行为](../serial_peripheral.md)。

| 项目 | 说明 |
|---|---|
| 功能宏 | `MR_USE_DRIVER_GPIO`，及对应的 `MR_USE_DRIVER_I2C` |
| 头文件 | `<mrX/drivers/gpio/mr_gpio.h>` |
| 依赖模块 | I2C、Driver；仅 HardTree 有实例时编译 |

## 用户指南

### HardTree

```dts
io8: io8 {
    compatible = "gpio_pcf8574";
    status = "defer";
    i2c = &i2c0;
    addr = <0x20>;
};
```

`i2c`、`addr` 必需。PCF8574 地址 0x20..0x27，PCF8574A 地址 0x38..0x3f。
`exist_mask` 缺省 0xff，并裁剪为 8 位；`invert_mask` 缺省 0。
启用 `MR_USE_DRIVER_GPIO`、`MR_USE_DRIVER_I2C`，总线不超过 100 kHz。

### API 行为

引脚 0..7。支持 INPUT/PULL_NONE 和 OUTPUT/OD/PULL_NONE；
拒绝 PP 输出、可编程上下拉、analog、alternate 和 IRQ 模式。
“OD”用于表达低电平强下拉、高电平释放的 API 语义；
芯片高电平仍有固定弱上拉电流，并非真正无上拉的开漏引脚。
输入配置将该位锁存为 1，所有写操作始终保持输入位为 1。

```c
/* After setting up the I2C controller and this driver */
mr_gpio_input(drv, 0U, MR_GPIO_PULL_NONE);
mr_gpio_output(drv, 1U, MR_GPIO_OTYPE_OD, MR_GPIO_LEVEL_HIGH);
mr_gpio_write(drv, 1U, MR_GPIO_LEVEL_LOW);
```

引脚写只接受软件配置为输出的引脚。端口写更新所有输出位，
保留输入为释放状态。读返回真实引脚电平；配置查询返回软件方向和输出锁存 shadow，
上电软件初始方向为全输入、锁存全 1。读写字节计量沿用 GPIO 公共 API。
不存在额外初始化扫描、地址探测或 INT 事件接线。

## 开发者指南

### 实现说明

源码：`drivers/gpio/gpio_pcf8574.c`。
每次端口操作传输一个数据字节，无寄存器地址。
只有成功写入后才提交软件 shadow；总线错误仍可能造成软硬件失步，
恢复后重发端口值。初次接管必须对应芯片上电状态。

协议依据：[NXP 数据手册](https://www.nxp.com/docs/en/data-sheet/PCF8574_PCF8574A.pdf)。
本轮仅人工静态审查，未构建或测试。


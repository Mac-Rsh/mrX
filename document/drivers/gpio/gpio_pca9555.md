# PCA9555 GPIO

复用 [GPIO API](gpio.md)，并遵守[串行总线外扩驱动共同行为](../serial_peripheral.md)。

| 项目 | 说明 |
|---|---|
| 功能宏 | `MR_USE_DRIVER_GPIO`，及对应的 `MR_USE_DRIVER_I2C` |
| 头文件 | `<mrX/drivers/gpio/mr_gpio.h>` |
| 依赖模块 | I2C、Driver；仅 HardTree 有实例时编译 |

## 用户指南

### HardTree

```dts
io16: io16 {
    compatible = "gpio_pca9555";
    status = "defer";
    i2c = &i2c0;
    addr = <0x20>;
};
```

启用 GPIO/I2C。必需属性为 `i2c`、`addr`，7 位地址范围 0x20..0x27。
`exist_mask` 缺省 0xffff 并裁剪为 16 位；`invert_mask` 缺省 0。

### API 行为

引脚 0..7 = P00..P07，8..15 = P10..P17。
支持 INPUT/PULL_NONE、OUTPUT/PP/PULL_NONE；
其他模式、上下拉、OD、alternate 和事件返回不支持。

输出配置先写 output latch，再切换 configuration 位；
引脚写要求当前方向为输出。端口写只更新已公开且配置为输出的位，
其余锁存位保持不变。读取得 input port 实际电平；
配置查询读取 configuration 和 output latch。

```c
mr_gpio_input(drv, 2U, MR_GPIO_PULL_NONE);
mr_gpio_output(drv, 10U, MR_GPIO_OTYPE_PP, MR_GPIO_LEVEL_HIGH);
```

## 开发者指南

### 实现说明

源码：`drivers/gpio/gpio_pca9555.c`。
首次操作清零两个 polarity inversion 寄存器，保留其他引脚的方向和锁存值。
输入/输出/反相/方向寄存器组分别从 0x00/0x02/0x04/0x06 开始，
每次只访问一个字节。两端口读写分成独立事务，不保证 16 位原子性。
INT 不接入事件系统；需要外部中断时由应用安排任务上下文的同步读取。

协议依据：[NXP 数据手册](https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf)。
本轮仅人工静态审查，未构建或测试。


# MCP23017 / MCP23S17 GPIO

复用 [GPIO API](gpio.md)，并遵守[串行总线外扩驱动共同行为](../serial_peripheral.md)。

| 项目 | 说明 |
|---|---|
| 功能宏 | `MR_USE_DRIVER_GPIO`，及对应的 `MR_USE_DRIVER_I2C` / `MR_USE_DRIVER_SPI` |
| 头文件 | `<mrX/drivers/gpio/mr_gpio.h>` |
| 依赖模块 | I2C / SPI、Driver；仅 HardTree 有实例时编译 |

## 用户指南

### HardTree

MCP23017：

```dts
io16: io16 {
    compatible = "gpio_mcp23x17";
    status = "defer";
    i2c = &i2c0;
    addr = <0x20>;
};
```

MCP23S17 使用 `spi = <&spi0 0>;` 替代 `i2c`，`addr = <0>;`
表示 A2:A0 的硬件地址，范围 0..7；I2C 地址范围 0x20..0x27。
每实例必须且只能声明一种总线。启用 GPIO 和对应 I2C/SPI 类别开关。
`exist_mask` 缺省 0xffff 并裁剪为 16 位；`invert_mask` 缺省 0。
SPI 固定请求 1 MHz、mode 0、8 位、MSB first。

### API 行为

引脚 0..7 = GPA0..7，8..15 = GPB0..7。
支持 INPUT、OUTPUT/PP，支持 PULL_NONE/PULL_UP；
不支持 OD、下拉、alternate、analog、IRQ 模式及事件。
输出配置先预装 OLAT，再切换 IODIR，避免把旧锁存值带入新输出。
引脚写要求当前硬件方向为输出；端口写只更新 `exist_mask` 内的输出位，
其余锁存位保持不变。读返回 GPIO 实际电平，配置查询读取 IODIR、OLAT、GPPU。

```c
mr_gpio_input(drv, 0U, MR_GPIO_PULL_UP);
mr_gpio_output(drv, 8U, MR_GPIO_OTYPE_PP, MR_GPIO_LEVEL_LOW);
mr_gpio_write(drv, 8U, MR_GPIO_LEVEL_HIGH);
```

## 开发者指南

### 实现说明

源码：`drivers/gpio/gpio_mcp23x17.c`。
首次寄存器操作设置 IOCON 为 BANK=0、HAEN=1、SEQOP=0，关闭两组 GPINTEN 和 IPOL；必须在芯片复位后的 BANK=0 状态接管。MCP23S17 复位后 HAEN=0，因此第一次 IOCON SPI 写固定使用硬件地址 0；该写成功后，后续事务才使用 HardTree `addr` 指定的 A2:A0 地址。
RESET 必须保持释放；驱动不控制它。
每次读写一个端口寄存器，不依赖跨端口自动递增。
一次端口写由多个事务构成，不提供 16 位原子更新或同时采样。

不要把多个尚未启用 HAEN 的器件放在同一片选上由不同实例同时接管；
建议每片芯片使用独立 CS。底层总线和芯片均需独占、串行调用。

协议依据：[Microchip 数据手册](https://ww1.microchip.com/downloads/en/DeviceDoc/20001952C.pdf)。
本轮仅人工静态审查，未构建或测试。


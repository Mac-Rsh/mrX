# MCP23017 / MCP23S17 GPIO 扩展（GPIO-MCP23x17）

通过 I2C（MCP23017）或 SPI（MCP23S17）总线驱动 16 位 GPIO 扩展芯片，将其 GPA0~7（引脚
`0`~`7`）与 GPB0 ~7（引脚 `8`~`15`）呈现为标准 [GPIO 驱动](gpio.md) 端口。

| 项目     | 说明                                                                                                                          |
|----------|-------------------------------------------------------------------------------------------------------------------------------|
| 启用方式 | 随 `MR_USE_DRIVER_GPIO` 与（`MR_USE_DRIVER_I2C` 或 `MR_USE_DRIVER_SPI`）编译，由 HardTree `compat = "gpio_mcp23x17"` 节点触发 |
| 头文件   | 复用 `<mrX/drivers/gpio/mr_gpio.h>`                                                                                           |

## HardTree 属性

| 属性          | 必需   | 默认值    | 说明                                                            |
|---------------|--------|-----------|-----------------------------------------------------------------|
| `i2c`         | 二选一 | 无        | I2C 总线驱动 phandle（MCP23017 使用）                           |
| `spi`         | 二选一 | 无        | SPI 规格 `[phandle, cs]`（MCP23S17 使用）                       |
| `addr`        | 是     | -         | I2C 7 位地址（`0x20`~`0x27`）或 SPI 硬件地址 `A2:A0`（`0`~`7`） |
| `exist_mask`  | 否     | `0xffffU` | 存在掩码（强制裁剪为 16 位）                                    |
| `invert_mask` | 否     | `0x0000U` | 低有效反相掩码                                                  |

每实例必须且只能配置 `i2c` 或 `spi` 其中之一，同时配置或均未配置时配置阶段返回
`-MR_EINVAL`。

## 快速上手

```c
#include <mrX/drivers/gpio/mr_gpio.h>

void mcp23x17_example(void) {
    mr_uint8_t level = 0;
    mr_driver_t *drv = mr_driver_find("io16"); /* 名称取自 HardTree 节点名 */

    if (drv == MR_NULL) {
        return;
    }
    mr_driver_setup(drv);

    /* 需先确保上游 I2C/SPI 控制器已完成 setup */
    mr_gpio_input(drv, 0, MR_GPIO_PULL_UP);
    mr_gpio_output(drv, 8, MR_GPIO_OTYPE_PP, MR_GPIO_LEVEL_LOW);
    mr_gpio_write(drv, 8, MR_GPIO_LEVEL_HIGH);
    mr_gpio_read(drv, 0, &level);

    mr_driver_put(drv);
}
```

## 运行语义与限制

- **模式与电气能力**：支持 `MR_GPIO_MODE_INPUT`（`MR_GPIO_PULL_NONE` /
  `MR_GPIO_PULL_UP`）与 `MR_GPIO_MODE_OUTPUT`（仅 `MR_GPIO_OTYPE_PP`
  ）。不支持开漏输出、下拉、复用功能、模拟模式及中断模式，传入返回 `-MR_ENOSYS`；未实现
  `event_attach`/`event_control`（返回 `-MR_ENOSYS`）。
- **首次初始化与 SPI 寻址**：首次调用 `mr_gpio_configure` 时，驱动将 `IOCON` 设为
  `BANK=0`、`HAEN=1`，并清零 `GPINTEN` 与 `POLARITY` 寄存器（极性反相统一由 GPIO
  公共层 `invert_mask` 处理）。由于 MCP23S17 复位后 `HAEN=0`，第一次写 `IOCON` 固定使用
  SPI 地址 `0`，此后事务再使用 HardTree 的 `addr`；因此多片共用片选时切勿同时接管未启用
  `HAEN` 的芯片，RESET 引脚需由外部保持释放。
- **输出预装与端口写**：配置输出模式时先写 `OLAT` 再切换 `IODIR`，避免切换瞬间输出旧锁存值。
  `mr_gpio_port_write` 仅更新当前配置为输出的引脚，输入引脚的锁存位保持不变；两字节端口操作分两次单字节总线事务完成，不保证
  16 位原子性，且 SPI 模式固定配置为 1 MHz、Mode 0、8 位、MSB First。
- **并发与上下文**：总线事务为同步阻塞操作，内部无互斥锁，多执行流并发访问同一实例需由调用方或总线层序列化，不可在
  ISR 中调用。

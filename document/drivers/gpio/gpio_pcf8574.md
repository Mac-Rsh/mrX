# PCF8574 / PCF8574A GPIO 扩展（GPIO-PCF8574）

通过 I2C 总线驱动 8 位准双向 I/O 扩展芯片 PCF8574/PCF8574A，将引脚 `0`~`7`
呈现为标准 [GPIO 驱动](gpio.md) 端口。

| 项目     | 说明                                                                                                |
|----------|-----------------------------------------------------------------------------------------------------|
| 启用方式 | 随 `MR_USE_DRIVER_GPIO` 与 `MR_USE_DRIVER_I2C` 编译，由 HardTree `compat = "gpio_pcf8574"` 节点触发 |
| 头文件   | 复用 `<mrX/drivers/gpio/mr_gpio.h>`                                                                 |

## HardTree 属性

| 属性          | 必需 | 默认值  | 说明                                                                 |
|---------------|------|---------|----------------------------------------------------------------------|
| `i2c`         | 是   | -       | I2C 总线驱动 phandle                                                 |
| `addr`        | 是   | -       | 7 位 I2C 地址（PCF8574 为 `0x20`~`0x27`，PCF8574A 为 `0x38`~`0x3f`） |
| `exist_mask`  | 否   | `0xffU` | 存在掩码（强制裁剪为 8 位）                                          |
| `invert_mask` | 否   | `0x00U` | 低有效反相掩码                                                       |

## 快速上手

```c
#include <mrX/drivers/gpio/mr_gpio.h>

void pcf8574_example(void) {
    mr_uint8_t level = 0;
    mr_driver_t *drv = mr_driver_find("io8"); /* 名称取自 HardTree 节点名 */

    if (drv == MR_NULL) {
        return;
    }
    mr_driver_setup(drv);

    /* 需先确保上游 I2C 控制器已完成 setup */
    mr_gpio_input(drv, 0, MR_GPIO_PULL_NONE);
    mr_gpio_output(drv, 1, MR_GPIO_OTYPE_OD, MR_GPIO_LEVEL_HIGH);
    mr_gpio_write(drv, 1, MR_GPIO_LEVEL_LOW);
    mr_gpio_read(drv, 0, &level);

    mr_driver_put(drv);
}
```

## 运行语义与限制

- **准双向 I/O 与模式限制**：仅支持 `MR_GPIO_MODE_INPUT`（`MR_GPIO_PULL_NONE`）与
  `MR_GPIO_MODE_OUTPUT`（仅 `MR_GPIO_OTYPE_OD`、`MR_GPIO_PULL_NONE`
  ）。拒绝推挽输出、上下拉、模拟、复用及中断模式（返回 `-MR_ENOSYS`）；未实现
  `event_attach`/`event_control`（返回 `-MR_ENOSYS`）。芯片内部高电平带有弱上拉电流，此处用
  `MR_GPIO_OTYPE_OD` 表达“低电平强下拉、高电平释放”的准双向语义。
- **输入位锁定与软件缓存**：软件初始状态为全输入（`inputs = 0xff`）、锁存全高（
  `shadow = 0xff`）。每次写端口时均执行 `shadow | inputs`，保证所有输入引脚在总线上维持高电平释放态；
  `mr_gpio_configuration` 直接读取软件缓存的 `inputs` 与 `shadow`。
- **读写语义**：芯片无寄存器地址，每次端口读写仅传输 1 字节。`mr_gpio_read` /
  `mr_gpio_port_read` 直接从 I2C 读取当前真实引脚电平；仅当 I2C 写事务成功时才提交更新
  `inputs` 与 `shadow`，总线出错后可通过重新写入恢复同步。
- **上下文限制**：I2C 传输为同步阻塞操作，内部无互斥锁，不可在 ISR 中调用。

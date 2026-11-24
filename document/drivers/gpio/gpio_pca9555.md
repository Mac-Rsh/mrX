# PCA9555 GPIO 扩展（GPIO-PCA9555）

通过 I2C 总线驱动 16 位 GPIO 扩展芯片 PCA9555，将 P00~P07（引脚 `0`~`7`）与 P10
~P17（引脚 `8`~`15`）呈现为标准 [GPIO 驱动](gpio.md) 端口。

| 项目     | 说明                                                                                                |
|----------|-----------------------------------------------------------------------------------------------------|
| 启用方式 | 随 `MR_USE_DRIVER_GPIO` 与 `MR_USE_DRIVER_I2C` 编译，由 HardTree `compat = "gpio_pca9555"` 节点触发 |
| 头文件   | 复用 `<mrX/drivers/gpio/mr_gpio.h>`                                                                 |

## HardTree 属性

| 属性          | 必需 | 默认值    | 说明                               |
|---------------|------|-----------|------------------------------------|
| `i2c`         | 是   | -         | I2C 总线驱动 phandle               |
| `addr`        | 是   | -         | 7 位 I2C 从机地址（`0x20`~`0x27`） |
| `exist_mask`  | 否   | `0xffffU` | 存在掩码（强制裁剪为 16 位）       |
| `invert_mask` | 否   | `0x0000U` | 低有效反相掩码                     |

## 快速上手

```c
#include <mrX/drivers/gpio/mr_gpio.h>

void pca9555_example(void) {
    mr_uint32_t mask = 0;
    mr_driver_t *drv = mr_driver_find("io16"); /* 名称取自 HardTree 节点名 */

    if (drv == MR_NULL) {
        return;
    }
    mr_driver_setup(drv);

    /* 需先确保上游 I2C 控制器已完成 setup */
    mr_gpio_input(drv, 2, MR_GPIO_PULL_NONE);
    mr_gpio_output(drv, 10, MR_GPIO_OTYPE_PP, MR_GPIO_LEVEL_HIGH);
    mr_gpio_port_read(drv, &mask);

    mr_driver_put(drv);
}
```

## 运行语义与限制

- **模式与电气能力**：仅支持 `MR_GPIO_MODE_INPUT`（`MR_GPIO_PULL_NONE`）与
  `MR_GPIO_MODE_OUTPUT`（仅 `MR_GPIO_OTYPE_PP`、`MR_GPIO_PULL_NONE`
  ）。不支持上下拉、开漏输出、复用功能、模拟模式及中断模式（返回 `-MR_ENOSYS`）；未实现
  `event_attach`/`event_control`（返回 `-MR_ENOSYS`），芯片 `INT`
  引脚需由应用在任务上下文自行轮询或通过宿主 GPIO 监控。
- **输出预装与端口写**：配置输出模式时先更新输出锁存寄存器（`0x02`/`0x03`
  ），再切换方向寄存器（`0x06`/`0x07`）。`mr_gpio_port_write`
  仅更新当前方向为输出的引脚位，输入引脚锁存位保持不变。
- **读写语义**：`mr_gpio_read` / `mr_gpio_port_read` 读取输入端口寄存器（`0x00`/
  `0x01`）反映引脚实际电平；`mr_gpio_configuration` 回读方向与输出锁存寄存器。
- **非原子性与上下文**：两个 8 位端口寄存器分两次独立 I2C 事务读写，不保证 16
  位原子更新或同时采样；接口为同步阻塞总线传输，内部无互斥锁，不可在 ISR 中调用。

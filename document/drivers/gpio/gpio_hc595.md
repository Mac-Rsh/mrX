# 74HC595 并行输出扩展（GPIO-HC595）

基于 3~5 个宿主 GPIO 引脚的 bit-bang 时序驱动 74HC595 串入并出移位寄存器，将级联芯片呈现为可写的
32 位 [GPIO 驱动](gpio.md) 端口。

| 项目     | 说明                                                                       |
|----------|----------------------------------------------------------------------------|
| 启用方式 | 随 `MR_USE_DRIVER_GPIO` 编译，由 HardTree `compat = "gpio_hc595"` 节点触发 |
| 头文件   | 复用 `<mrX/drivers/gpio/mr_gpio.h>`                                        |

## HardTree 属性

| 属性          | 必需 | 默认值        | 说明                                                  |
|---------------|------|---------------|-------------------------------------------------------|
| `ser`         | 是   | -             | 串行数据输入引脚 SER，格式 `[phandle, pin]`           |
| `srclk`       | 是   | -             | 移位时钟引脚 SRCLK，格式 `[phandle, pin]`             |
| `rclk`        | 是   | -             | 锁存时钟引脚 RCLK，格式 `[phandle, pin]`              |
| `oe`          | 否   | 无            | 输出使能引脚 OE（低有效），格式 `[phandle, pin]`      |
| `srclr`       | 否   | 无            | 寄存器清零引脚 SRCLR（低有效），格式 `[phandle, pin]` |
| `cascade_num` | 否   | `1`           | 级联芯片数（`1`~`4`），对应 `cascade_num * 8` 个输出  |
| `exist_mask`  | 否   | `0xffffffffU` | 存在掩码，建议设为实际级联位宽（如 1 片设为 `0xff`）  |
| `invert_mask` | 否   | `0x00000000U` | 低有效反相掩码                                        |

## 快速上手

```c
#include <mrX/drivers/gpio/mr_gpio.h>

void hc595_example(void) {
    mr_driver_t *drv = mr_driver_find("gpio_hc595"); /* 名称取自 HardTree 节点名 */

    if (drv == MR_NULL) {
        return;
    }
    mr_driver_setup(drv);

    /* 首次配置自动初始化宿主引脚、移入全 0 并拉低 OE 使能输出，再应用目标电平 */
    mr_gpio_output(drv, 5, MR_GPIO_OTYPE_PP, MR_GPIO_LEVEL_HIGH);
    mr_gpio_port_write(drv, 0x000000a5U);

    mr_driver_put(drv);
}
```

## 运行语义与限制

- **只写输出与影子寄存器**：`mr_gpio_configure` 仅接受 `MR_GPIO_MODE_OUTPUT`（且
  `pin < cascade_num * 8`），其余返回 `-MR_EINVAL`；`mr_gpio_configuration` 固定回读为
  `OUTPUT`、`PP`、`PULL_NONE` 及影子位电平。`mr_gpio_read` 与 `mr_gpio_port_read`
  直接返回内部 `shadow` 缓存值，不回读硬件引脚。
- **按需移位优化**：`mr_gpio_write` 若检测到目标位与 `shadow` 一致，会跳过移位时序直接返回成功；
  `mr_gpio_port_write` 则始终执行整链移位锁存，可用于强制刷新硬件输出（如外部清零
  `SRCLR` 后恢复同步）。
- **首次上电清零**：首次合法配置时，驱动会先置无效电平（`OE`/`SRCLR`
  拉高），初始化时钟与数据线，移入全 `0` 并锁存后再拉低 `OE` 使能输出，避免上电毛刺；此后再次调用
  `mr_gpio_configure` 仅更新对应引脚输出。
- **无中断与并发保护**：未实现 `event_attach`/`event_control`（返回 `-MR_ENOSYS`
  ）。驱动内部无互斥锁，多执行流并发写入会破坏移位时序并导致 `shadow`
  失步，且不适合在中断上下文中调用。

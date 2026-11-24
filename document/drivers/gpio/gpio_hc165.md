# 74HC165 并行输入扩展（GPIO-HC165）

基于 3~5 个宿主 GPIO 引脚的 bit-bang 时序驱动 74HC165 并行输入移位寄存器，将级联芯片呈现为只读的
32 位 [GPIO 驱动](gpio.md) 端口。

| 项目     | 说明                                                                       |
|----------|----------------------------------------------------------------------------|
| 启用方式 | 随 `MR_USE_DRIVER_GPIO` 编译，由 HardTree `compat = "gpio_hc165"` 节点触发 |
| 头文件   | 复用 `<mrX/drivers/gpio/mr_gpio.h>`                                        |

## HardTree 属性

| 属性          | 必需 | 默认值        | 说明                                                 |
|---------------|------|---------------|------------------------------------------------------|
| `clk`         | 是   | -             | 移位时钟引脚 CLK，格式 `[phandle, pin]`              |
| `sh`          | 是   | -             | 锁存/移位控制引脚 SH/\!LD，格式 `[phandle, pin]`     |
| `qh`          | 是   | -             | 串行数据输出引脚 QH，格式 `[phandle, pin]`           |
| `clk_inh`     | 否   | 无            | 时钟禁止引脚 CLK-INH，格式 `[phandle, pin]`          |
| `ser`         | 否   | 无            | 串行级联输入引脚 SER，格式 `[phandle, pin]`          |
| `cascade_num` | 否   | `1`           | 级联芯片数（`1`~`4`），对应 `cascade_num * 8` 个输入 |
| `exist_mask`  | 否   | `0xffffffffU` | 存在掩码，建议设为实际级联位宽（如 1 片设为 `0xff`） |
| `invert_mask` | 否   | `0x00000000U` | 低有效反相掩码                                       |

## 快速上手

```c
#include <mrX/drivers/gpio/mr_gpio.h>

void hc165_example(void) {
    mr_uint32_t mask = 0;
    mr_driver_t *drv = mr_driver_find("gpio_hc165"); /* 名称取自 HardTree 节点名 */

    if (drv == MR_NULL) {
        return;
    }
    mr_driver_setup(drv);

    /* 首次配置合法引脚时自动初始化 CLK/SH/QH 及可选引脚 */
    mr_gpio_input(drv, 0, MR_GPIO_PULL_NONE);
    mr_gpio_port_read(drv, &mask);

    mr_driver_put(drv);
}
```

## 运行语义与限制

- **只读输入**：`mr_gpio_configure` 仅接受 `MR_GPIO_MODE_INPUT`（且
  `pin < cascade_num * 8`），其余返回 `-MR_EINVAL`；`mr_gpio_configuration` 固定回读为
  `INPUT`、`PULL_NONE`、`ALT_DISABLE`。`mr_gpio_write` 与 `mr_gpio_port_write` 返回
  `-MR_EPERM`。
- **宿主引脚初始化幂等**：内部 `configured`
  标记保证仅在首次合法配置时设置宿主引脚方向与初始电平，后续合法调用仅校验引脚范围与模式。
- **整链移位读取**：每次 `mr_gpio_read` 或 `mr_gpio_port_read` 都会触发完整锁存与
  `cascade_num * 8` 次移位脉冲；同时读取多个引脚时应优先使用 `mr_gpio_port_read`
  。若 `exist_mask` 保留默认全 1，读取超出 `cascade_num * 8` 的高位会返回 `0`。
- **无中断与并发保护**：未实现 `event_attach`/`event_control`（返回 `-MR_ENOSYS`
  ），需轮询读取。驱动内部无互斥锁，多执行流并发读取会破坏移位时序，且不适合在中断上下文中调用。

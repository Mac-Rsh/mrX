# GPIO 驱动（GPIO）

GPIO 驱动在驱动框架之上提供统一的 32 位端口引脚抽象，支持模式配置、电平读写、端口批量操作以及可选的外部中断事件通知，并通过
`exist_mask` 与 `invert_mask` 屏蔽板级引脚分布和低有效电路差异。

| 项目   | 说明                                                                         |
|--------|------------------------------------------------------------------------------|
| 功能宏 | `MR_USE_DRIVER_GPIO`（依赖 `MR_USE_DRIVER`）                                 |
| 头文件 | `<mrX/drivers/gpio/mr_gpio.h>`                                               |
| 依赖   | [驱动框架（Driver）](../driver.md)、[等待队列（Wait）](../../kernel/wait.md) |
| 测试   | `test/drivers/gpio/gpio.c`                                                   |

## 配置

| 配置项               | 类型 | 默认值 | 说明                                                 |
|----------------------|------|--------|------------------------------------------------------|
| `MR_USE_DRIVER_GPIO` | bool | n      | 启用 GPIO 驱动（`include/mrX/drivers/gpio/Kconfig`） |

## 快速上手

```c
#include <mrX/drivers/gpio/mr_gpio.h>

void gpio_example(mr_driver_t *drv) {
    mr_uint8_t level = 0;

    /* 推挽输出，初始逻辑高电平；若该引脚在 invert_mask 中置位则实际输出低电平 */
    mr_gpio_output(drv, 5, MR_GPIO_OTYPE_PP, MR_GPIO_LEVEL_HIGH);
    mr_gpio_write(drv, 5, MR_GPIO_LEVEL_LOW);
    mr_gpio_toggle(drv, 5);

    /* 上拉输入并读取逻辑电平 */
    mr_gpio_input(drv, 12, MR_GPIO_PULL_UP);
    mr_gpio_read(drv, 12, &level);
}
```

若使用中断事件，先将引脚配置为 `MR_GPIO_MODE_IRQ_*`，再初始化 `mr_waiter_t`
并显式启用事件源：

```c
static void key_waiter_cb(mr_waiter_t *waiter, void *param,
                          mr_wait_event_t event, const void *data) {
    MR_IGNORE(waiter);
    MR_IGNORE(param);
    MR_IGNORE(event);
    MR_IGNORE(data); /* 具体负载类型由 backend 约定，通常为 mr_gpio_event_data_t */
}

void gpio_irq_example(mr_driver_t *drv) {
    static mr_waiter_t waiter;
    mr_gpio_config_t cfg = {
        .mode = MR_GPIO_MODE_IRQ_FALLING,
        .pull = MR_GPIO_PULL_UP,
    };

    mr_gpio_configure(drv, 12, &cfg);
    mr_waiter_init(&waiter, key_waiter_cb, MR_NULL, MR_GPIO_WAIT_EXTI);
    mr_gpio_event_attach(drv, 12, &waiter);
    mr_gpio_event_enable(drv, 12, MR_GPIO_WAIT_EXTI);
}
```

## 核心机制与约束

- **引脚编号与掩码**：每个 GPIO 驱动实例对应最多 32 个引脚（编号 `0`~`31`）。
  `mr_gpio_data_t` 中的 `exist_mask` 标记物理存在的引脚，`invert_mask`
  标记低有效（逻辑电平与物理电平相反）的引脚。
- **存在性校验差异**：`mr_gpio_configure`、`mr_gpio_configuration`、
  `mr_gpio_event_attach`、`mr_gpio_event_enable` 和 `mr_gpio_event_disable` 会校验
  `pin < 32` 且在 `exist_mask` 中，否则返回 `-MR_EINVAL`；`mr_gpio_write`、
  `mr_gpio_read`、`mr_gpio_toggle` 仅通过 `MR_ASSERT(pin < 32U)` 校验范围；
  `mr_gpio_port_write` 与 `mr_gpio_port_read` 会自动按 `exist_mask` 过滤无效位。
- **逻辑电平与物理电平**：`mr_gpio_write`/`mr_gpio_read`/`mr_gpio_port_write`/
  `mr_gpio_port_read` 对外统一使用逻辑电平，框架层自动按 `invert_mask` 完成异或换算。
  `mr_gpio_configure` 在 `MR_GPIO_MODE_OUTPUT` 模式下也会先将 `config->level`
  反相换算后再交给底层 `pin_configure`（不额外调用 `pin_write`），
  `mr_gpio_configuration` 回读时对称还原。`mr_gpio_toggle` 与
  `mr_gpio_port_toggle` 直接读物理电平取反写回，不经过 `invert_mask`。
- **中断事件语义**：`mr_gpio_configure` 仅把 `MR_GPIO_MODE_IRQ_*` 传递给
  backend，不会自动使能中断；必须显式调用 `mr_gpio_event_attach` 挂接等待者并调用
  `mr_gpio_event_enable` 开启事件。`events` 掩码（如 `MR_GPIO_WAIT_EXTI`）由公共层原样透传给
  `ops->event_control`，具体支持的触发能力、是否要求先配为 IRQ 模式及负载数据生命周期由
  backend 决定。若 backend 未实现事件回调，接口返回 `-MR_ENOSYS`。
- **并发与动态内存**：框架层读写与配置接口无内部互斥锁，多执行流并发操作同一引脚或端口时需由调用方或底层
  `ops` 保证原子性（尤其 `mr_gpio_toggle` 为读-改-写两步非原子操作）。调用
  `mr_gpio_create` 时传入的 `mr_gpio_data_t` 必须由 `mr_malloc` 分配，驱动删除时由框架自动释放。

## API 速查

### 实例生命周期与宏

| API                                                  | 说明                                                               |
|------------------------------------------------------|--------------------------------------------------------------------|
| `MR_GPIO_DATA_INIT(_exist_mask, _invert_mask)`       | 初始化 `mr_gpio_data_t` 掩码                                       |
| `MR_GPIO_DEFINE(_name, _data, _priv, _ops)`          | 静态定义 GPIO 驱动实例                                             |
| `MR_GPIO_INSTALL(_name, _data, _priv, _ops, _level)` | 静态定义并按初始化级别自动安装驱动                                 |
| `MR_HT_GPIO_INSTALL(_id, _priv, _ops)`               | 通过 HardTree 安装实例（自动读取 `exist_mask`/`invert_mask` 属性） |
| `mr_gpio_init(drv, data, priv, ops)`                 | 初始化静态 GPIO 驱动，返回 `0` 或负数错误码                        |
| `mr_gpio_create(data, priv, ops)`                    | 动态创建 GPIO 驱动，返回驱动指针或 `MR_NULL`                       |
| `mr_gpio_del(drv)`                                   | 删除 GPIO 驱动                                                     |
| `mr_gpio_get(drv)` / `mr_gpio_put(drv)`              | 增加 / 减少驱动引用计数                                            |

### 引脚配置与读写

| API                                       | 说明                                                        |
|-------------------------------------------|-------------------------------------------------------------|
| `mr_gpio_configure(drv, pin, config)`     | 写入引脚配置，输出模式下同步设置初始电平                    |
| `mr_gpio_configuration(drv, pin, config)` | 回读引脚当前配置                                            |
| `mr_gpio_input(drv, pin, pull)`           | 快捷配置为输入模式并指定上下拉                              |
| `mr_gpio_output(drv, pin, otype, value)`  | 快捷配置为输出模式并指定输出类型（推挽/开漏）与初始逻辑电平 |
| `mr_gpio_write(drv, pin, value)`          | 写引脚逻辑电平，成功返回 `1`（字节数），失败返回负数错误码  |
| `mr_gpio_read(drv, pin, value)`           | 读引脚逻辑电平，成功返回 `1`（字节数），失败返回负数错误码  |
| `mr_gpio_toggle(drv, pin)`                | 翻转引脚电平（非原子读-改-写），返回 `0` 或负数错误码       |
| `mr_gpio_port_write(drv, mask)`           | 批量写端口逻辑电平掩码，成功返回 `4`（字节数）              |
| `mr_gpio_port_read(drv, mask)`            | 批量读端口逻辑电平掩码，成功返回 `4`（字节数）              |
| `mr_gpio_port_toggle(drv)`                | 翻转整个端口当前电平                                        |

### 事件控制

| API                                       | 说明                                              |
|-------------------------------------------|---------------------------------------------------|
| `mr_gpio_event_attach(drv, pin, entry)`   | 为指定引脚挂接 `mr_waiter_t` 等待者（不开启中断） |
| `mr_gpio_event_enable(drv, pin, events)`  | 使能引脚事件（如 `MR_GPIO_WAIT_EXTI`）            |
| `mr_gpio_event_disable(drv, pin, events)` | 禁用引脚事件                                      |

### HardTree 规格辅助

`mr_ht_gpio_spec_t` 封装了 `{drv, pin}`，可通过 HardTree 属性单元
`[phandle, pin]` 直接构造并操作：

| API                                          | 说明                                                                                                                                                                |
|----------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `MR_HT_NODE_GPIO_SPEC(_node, _prop)`         | 从 HardTree 节点属性生成 `mr_ht_gpio_spec_t`                                                                                                                        |
| `MR_HT_NODE_GPIO_SPEC_FOREACH(_node, _prop)` | 遍历节点属性数组生成 `mr_ht_gpio_spec_t` 列表                                                                                                                       |
| `MR_HT_GPIO_SPEC(_id, _prop)`                | 从驱动实例 ID 属性生成 `mr_ht_gpio_spec_t`                                                                                                                          |
| `MR_HT_GPIO_SPEC_FOREACH(_id, _prop)`        | 遍历驱动实例 ID 属性数组生成 `mr_ht_gpio_spec_t` 列表                                                                                                               |
| `mr_ht_gpio_*`                               | 对 `mr_ht_gpio_spec_t *spec` 封装的 `configure`/`configuration`/`input`/`output`/`write`/`read`/`toggle`/`event_attach`/`event_enable`/`event_disable` 快捷内联函数 |

`mr_gpio_config_t` 为 1 字节位域结构，包含 `mode`（`MR_GPIO_MODE_ANALOG`、`INPUT`、
`OUTPUT`、`IRQ_RISING`、`IRQ_FALLING`、`IRQ_EDGE`、`IRQ_HIGH`、`IRQ_LOW`）、`alt`（
`MR_GPIO_ALT_DISABLE`/`ENABLE`）、`pull`（`MR_GPIO_PULL_NONE`/`UP`/`DOWN`）、`otype`（
`MR_GPIO_OTYPE_PP`/`OD`）与 `level`（`MR_GPIO_LEVEL_LOW`/`HIGH`）。

## 内部实现

```text
应用调用 (mr_gpio_* / mr_ht_gpio_*)
        │  逻辑电平、引脚号 (0~31)
        ▼
GPIO 公共层 (drivers/gpio/gpio.c + include/mrX/drivers/gpio/mr_gpio.h)
        │  exist_mask 校验、invert_mask 换算、参数断言
        ▼  mr_gpio_ops_t (物理电平)
底层 Backend / 扩展芯片驱动 (pin_configure, pin_write, port_write, ...)
```

GPIO 驱动类在 `drivers/gpio/gpio.c` 中仅注册了 `release` 回调（用于释放
`MR_DRIVER_DATA(drv)`），无 `init`/`deinit` 阶段，`mr_driver_setup` 直接标记就绪。除片上外设
backend 外，`drivers/gpio/` 下还内置了基于 GPIO/I2C/SPI 的扩展芯片驱动（如
HC165、HC595、MCP23x17、PCA9555、PCF8574），均实现同一张 `mr_gpio_ops_t`。

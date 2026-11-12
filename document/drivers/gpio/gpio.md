# GPIO 驱动（GPIO）

> GPIO 驱动在驱动框架之上提供统一的引脚抽象：输入/输出/中断模式配置、电平
> 读写与整端口批量操作，并用"存在掩码 + 反相掩码"适配不同板卡的引脚布局与
> 低有效电路。

| 项       | 说明                                                                               |
|----------|------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_DRIVER_GPIO`（默认关闭，依赖 `MR_USE_DRIVER`）                             |
| 头文件   | `<mrX/drivers/gpio/mr_gpio.h>`                                                     |
| 依赖模块 | [驱动框架 Driver](../driver.md)、[等待队列 Wait](../../kernel/wait.md)（引脚事件） |
| 参考测试 | `mrX/test/drivers/gpio/gpio.c`                                                     |

---

## 用户指南

### 1. 模块概述

GPIO 驱动解决的问题是： **用同一套 API 操作不同 MCU/扩展芯片的数字引脚**，
应用只面对"引脚号 + 配置 + 电平"，硬件差异由具体
`ops` 实现消化。

特性：

- 8 种引脚模式：模拟、输入、输出与 5 种中断触发模式（上升沿/下降沿/双沿/ 高电平/低电平）。
- 引脚级读/写/翻转与端口级（32 位掩码）批量读/写/翻转。
- `exist_mask`（存在掩码）约束可用引脚范围，`invert_mask`（反相掩码）统一
  处理低有效电路，应用始终使用逻辑电平。
- 便捷函数：`mr_gpio_input`/`mr_gpio_output` 一行完成常用配置。
- 可选的引脚事件挂接（EXTI），基于等待队列实现回调通知。
- hardtree 规格宏：可在硬件模板中声明"某驱动某引脚"，代码用
  `mr_ht_gpio_spec_t` 直接引用。

### 2. 配置与依赖

| 配置项               | 类型 | 默认值 | 说明                                                     |
|----------------------|------|--------|----------------------------------------------------------|
| `MR_USE_DRIVER_GPIO` | bool | n      | 启用 GPIO 驱动（`mrX/include/mrX/drivers/gpio/Kconfig`） |
| `MR_USE_DRIVER`      | bool | n      | GPIO 的前置依赖，必须先启用驱动框架                      |

启用后重新生成 `mr_config.h`，包含头文件即可使用：

```c
#include <mrX/drivers/gpio/mr_gpio.h>
```

引脚编号固定为 0~31（对应 `mr_gpio_data_t` 中掩码的 32 个位），一个驱动实例 代表一个
32 位宽的 GPIO 端口（或芯片的全部引脚）。

### 3. 快速上手

点亮一个 LED（输出模式）：

```c
#include <mrX/drivers/gpio/mr_gpio.h>

void led_example(mr_driver_t *drv) {
    /* 推挽输出，初始电平为高 */
    mr_gpio_output(drv, 5, MR_GPIO_OTYPE_PP, MR_GPIO_LEVEL_HIGH);

    mr_gpio_write(drv, 5, 0);       /* 逻辑低电平 */
    mr_gpio_toggle(drv, 5);         /* 再翻转一次 */
}
```

读一个按键（输入模式 + 上拉）：

```c
mr_uint8_t level = 0;

mr_gpio_input(drv, 12, MR_GPIO_PULL_UP);
mr_gpio_read(drv, 12, &level);
if (level == MR_GPIO_LEVEL_LOW) {
    /* 按键按下（低有效接线由 invert_mask 处理） */
}
```

`drv` 为 GPIO 类型驱动指针，可来自平台安装的驱动或 `mr_driver_find`。

### 4. 使用说明

**配置模型**：引脚属性集中在 `mr_gpio_config_t`：`mode`（8 种模式之一）、
`alt`（复用功能开关）、`pull`（none/up/down）、`otype`（推挽/开漏）、
`level`（输出初始电平）。`mr_gpio_configure` 一次写入全部属性；输出模式下
初始电平作为配置的一部分（框架按 `invert_mask`
反相换算后）随
`pin_configure` 一并交给底层写入，框架不再单独调用 `pin_write`。
`mr_gpio_configuration` 回读当前配置。

**逻辑电平与物理电平**：`mr_gpio_write`/`mr_gpio_read`/`mr_gpio_port_write`/
`mr_gpio_port_read` 操作的是逻辑电平，框架自动按 `invert_mask` 取反后交给
硬件。存在掩码校验只出现在配置与事件挂接路径（返回
`-MR_EINVAL`）；引脚级 读写仅断言引脚号小于 32，端口批量读写将存在掩码之外的位直接过滤。
`mr_gpio_toggle`/
`mr_gpio_port_toggle` 直接翻转硬件电平（读硬件值取反写回），不经过反相 换算。

**事件（EXTI）**：`mr_gpio_configure` 可以用 `MR_GPIO_MODE_IRQ_*` 把触发模式交给
backend，但公共 GPIO 层不自动启用事件源。通常先配置
IRQ 模式，再用 `mr_waiter_init` 初始化 waiter 并调用 `mr_gpio_event_attach`
，最后显式调用 `mr_gpio_event_enable`；关闭时调用
`mr_gpio_event_disable`。

公共层只负责检查引脚是否存在以及 `event_attach`/`event_control` 回调是否实现，
`events` mask 会原样传给
backend，因此具体支持哪些事件位、非法 mask 返回什么错误、切换到非 IRQ
模式时是否自动关闭硬件事件，都属于平台实现语义。GPIO 公共头文件提供
`mr_gpio_event_data_t`，其 `pin` 字段用于携带触发引脚号。
当 backend 选择以该结构发送事件时，数据指针仅在同步回调期间有效。

**生命周期**：GPIO 驱动对象本身遵循[驱动框架](../driver.md)的生命周期；注意
`mr_gpio_create` 传入的 `mr_gpio_data_t` 必须用 `mr_malloc` 分配——驱动删除
时框架会释放它。

### 5. API 参考

初始化与销毁：

| API                                  | 说明                                     |
|--------------------------------------|------------------------------------------|
| `mr_gpio_init`                       | 初始化一个静态 GPIO 驱动                 |
| `mr_gpio_create`                     | 在堆上动态创建 GPIO 驱动                 |
| `mr_gpio_del`                        | 删除 GPIO 驱动                           |
| `MR_GPIO_DEFINE` / `MR_GPIO_INSTALL` | 编译期定义 / 安装 GPIO 驱动              |
| `MR_GPIO_DATA_INIT`                  | 初始化 `mr_gpio_data_t`（存在/反相掩码） |

配置：

| API                     | 说明                                     |
|-------------------------|------------------------------------------|
| `mr_gpio_configure`     | 写入引脚配置（输出模式附带写入初始电平） |
| `mr_gpio_configuration` | 回读引脚配置                             |
| `mr_gpio_input`         | 便捷：配置为输入并选择上/下拉            |
| `mr_gpio_output`        | 便捷：配置为输出并指定输出类型与初始电平 |

数据读写：

| API                   | 说明                                    |
|-----------------------|-----------------------------------------|
| `mr_gpio_write`       | 写引脚逻辑电平，成功返回写入字节数（1） |
| `mr_gpio_read`        | 读引脚逻辑电平，成功返回读取字节数（1） |
| `mr_gpio_toggle`      | 翻转引脚硬件电平                        |
| `mr_gpio_port_write`  | 按掩码批量写端口                        |
| `mr_gpio_port_read`   | 按掩码批量读端口                        |
| `mr_gpio_port_toggle` | 翻转整个端口                            |

事件与引用：

| API                           | 说明                                    |
|-------------------------------|-----------------------------------------|
| `mr_gpio_event_attach`        | 把等待者订阅到引脚事件，不启用 EXTI     |
| `mr_gpio_event_enable`        | 按 `MR_GPIO_WAIT_*` mask 启用引脚事件源 |
| `mr_gpio_event_disable`       | 按 `MR_GPIO_WAIT_*` mask 停用引脚事件源 |
| `mr_gpio_get` / `mr_gpio_put` | 引用计数加一 / 减一                     |

**`mr_gpio_configure`**

```c
mr_err_t mr_gpio_configure(mr_driver_t *drv, mr_uint32_t pin,
                           const mr_gpio_config_t *config);
```

- `pin`：引脚号 0~31，且必须在 `exist_mask` 中，否则返回 `-MR_EINVAL`。
- `config`：引脚配置，不可为 `MR_NULL`。
- 返回：成功 `0`，失败负数错误码（含底层 `pin_configure` 的错误码透传）。

**`mr_gpio_write` / `mr_gpio_read`**

```c
mr_ssize_t mr_gpio_write(mr_driver_t *drv, mr_uint32_t pin, mr_uint8_t value);
mr_ssize_t mr_gpio_read(mr_driver_t *drv, mr_uint32_t pin, mr_uint8_t *value);
```

按逻辑电平读写（`invert_mask` 由框架处理）。成功返回 `sizeof(value)` 即 1，
底层失败返回负数错误码。

**`mr_gpio_port_write` / `mr_gpio_port_read`**

```c
mr_ssize_t mr_gpio_port_write(mr_driver_t *drv, mr_uint32_t mask);
mr_ssize_t mr_gpio_port_read(mr_driver_t *drv, mr_uint32_t *mask);
```

整端口掩码读写。写入前框架做 `mask ^ invert_mask` 再 `& exist_mask`，读取后
做同样换算，保证掩码语义与引脚级一致。成功返回
`sizeof(mask)` 即 4。

**`mr_gpio_event_attach`**

```c
MR_INLINE mr_err_t mr_gpio_event_attach(mr_driver_t *drv, mr_uint32_t pin,
                              mr_waiter_t *entry);
```

- `entry`：已用 `mr_waiter_init` 初始化、期望事件含 `MR_GPIO_WAIT_EXTI` 的 等待者。
- 返回：成功 `0`；引脚不存在 `-MR_EINVAL`；平台不支持 `-MR_ENOSYS`。
- 注意：只订阅等待者，不会启用该引脚的 EXTI。

**`mr_gpio_event_enable` / `mr_gpio_event_disable`**

```c
MR_INLINE mr_err_t mr_gpio_event_enable(mr_driver_t *drv, mr_uint32_t pin,
                                        mr_uint32_t mask);
MR_INLINE mr_err_t mr_gpio_event_disable(mr_driver_t *drv, mr_uint32_t pin,
                                         mr_uint32_t mask);
```

公共层不会解释 `events` 的位含义，只检查 pin 是否存在以及 `event_control`
是否实现，然后把 mask 原样传给 backend。引脚不存在返回
`-MR_EINVAL`，未实现 `event_control` 返回 `-MR_ENOSYS`；其余错误由 backend 决定。

某些 backend 要求引脚先配置为 `MR_GPIO_MODE_IRQ_*` 才可启用事件；
该前置条件与返回错误码由各 backend 明确规定，并非 GPIO 公共层统一校验。

**hardtree 规格**

```c
mr_ht_gpio_spec_t spec = MR_HT_GPIO_SPEC(instance_id, gpios);
mr_ht_gpio_output(&spec, MR_GPIO_OTYPE_PP, MR_GPIO_LEVEL_HIGH);
```

`mr_ht_gpio_spec_t` 携带驱动指针与引脚号，由宏从硬件模板属性单元
`[phandle, pin]` 生成；配套 `mr_ht_gpio_configure/configuration/input/output/write/read/
toggle/event_attach/event_enable/event_disable` 与属性数组版本
`MR_HT_GPIO_SPEC_FOREACH`。

### 6. 完整示例

GPIO 中断配合 I2C 传感器 data-ready，以及 GPIO reset/enable 配合 SPI stream
的组合用法见[驱动组合与进阶示例](../examples.md)。

示例 1：动态创建 GPIO 驱动并配置/读写（改编自
`mrX/test/drivers/gpio/gpio.c` 的 `configuration_and_io`）：

```c
void gpio_dynamic_example(void) {
    mr_gpio_data_t *data = mr_malloc(sizeof(*data));
    mr_uint8_t value;
    mr_driver_t *drv;

    if (data == MR_NULL) {
        return;
    }
    data->exist_mask = 0xffffffffU;     /* 32 个引脚全部存在 */
    data->invert_mask = 0x00000001U;    /* 引脚 0 低有效 */
    drv = mr_gpio_create(data, MR_NULL, (mr_gpio_ops_t *)&board_gpio_ops);
    if (drv == MR_NULL) {
        return;
    }
    mr_driver_setup(drv);

    mr_gpio_input(drv, 3, MR_GPIO_PULL_UP);
    mr_gpio_read(drv, 3, &value);
    mr_gpio_output(drv, 0, MR_GPIO_OTYPE_PP, 1);    /* 物理低电平 */

    mr_gpio_del(drv);                   /* 释放 data */
}
```

（`board_gpio_ops` 为板级 `mr_gpio_ops_t`，需实现 `pin_configure`/
`pin_write`/`pin_read`/`port_write`/`port_read`。）

示例 2：挂接按键 EXTI 事件：

```c
static void key_waiter(mr_waiter_t *waiter, void *param,
                       mr_wait_event_t event, const void *data) {
    MR_IGNORE(waiter);
    MR_IGNORE(param);
    MR_IGNORE(event);
    MR_IGNORE(data);  /* payload 语义由 backend 定义 */
}

void key_example(mr_driver_t *drv) {
    static mr_waiter_t waiter;
    mr_gpio_config_t cfg = {.mode = MR_GPIO_MODE_IRQ_FALLING,
                            .pull = MR_GPIO_PULL_UP};

    mr_gpio_configure(drv, 12, &cfg);
    mr_waiter_init(&waiter, key_waiter, MR_NULL, MR_GPIO_WAIT_EXTI);
    mr_gpio_event_attach(drv, 12, &waiter);
    mr_gpio_event_enable(drv, 12, MR_GPIO_WAIT_EXTI);  /* 订阅后显式启用 */

    /* 应用不再需要该事件时显式关闭 */
    mr_gpio_event_disable(drv, 12, MR_GPIO_WAIT_EXTI);
}
```

更多行为（不存在引脚拒绝、配置错误透传、端口掩码换算、事件不支持路径）见
`mrX/test/drivers/gpio/gpio.c`。

### 7. 常见问题（FAQ）

**Q：`mr_gpio_write` 返回负数错误码？**

`mr_gpio_write`/`mr_gpio_read` 不校验 `exist_mask`，负数返回值是底层 `ops`
错误透传（如 `-MR_EIO`）；引脚号大于等于 32 在断言开启时由 `MR_ASSERT`
拦截。`-MR_EINVAL`（引脚不在 `exist_mask` 中）出现在
`mr_gpio_configure`/`mr_gpio_configuration`/`mr_gpio_event_attach`，检查 硬件模板里的
`exist_mask` 属性与引脚号。

**Q：写 1 量出来是 0？**

该引脚在 `invert_mask` 中被声明为低有效。`mr_gpio_write`/`mr_gpio_read`
对外使用逻辑电平，框架会自动完成反相换算。
`mr_gpio_toggle` 直接对 backend 读到的电平执行读-改-写，但无论是否配置
`invert_mask`，最终效果都是把当前逻辑状态翻转一次。

**Q：GPIO 事件 API 返回 `-MR_ENOSYS`？**

平台 `ops` 没有实现请求的事件挂接或启停能力。GPIO 扩展芯片通常没有
EXTI；检查具体后端是否支持该 pin 的事件源。
`event_attach`
只订阅 waiter，仍需在后端支持时调用 `mr_gpio_event_enable` 显式启用。


如果事件源当前被占用或引脚尚未配置成目标 backend 支持的中断模式，应先检查引脚模式、
事件所有权及 waiter 挂接顺序，具体错误码参阅该 backend 的契约。

**Q：输出配置后电平不对？**

`mr_gpio_configure` 只在模式为 `MR_GPIO_MODE_OUTPUT` 时写入 `level` 初始电平；
其他模式忽略 `level`。输入模式下 `otype`/
`level` 不参与配置。

**Q：`mr_gpio_toggle` 和连续 write 有什么区别？**

`toggle` 在框架层直接对硬件电平取反（不经过 `invert_mask`），一次"读-改-写"
完成翻转；对反相引脚连续 `write` 则需要应用自己维护逻辑电平。

---

## 开发者指南

### 8. 架构总览

GPIO 驱动是[驱动框架](../driver.md)的第一个派生模块，采用"框架层换算 + ops
下沉"结构：

```
        +------------------------------------------+
        |  应用（mr_gpio_write / mr_ht_gpio_*）      |
        +-------------------+----------------------+
                            | 逻辑电平/引脚号校验
        +-------------------v----------------------+
        |  GPIO 框架层（drivers/gpio/gpio.c + mr_gpio.h） |
        |  exist_mask 校验、invert_mask 换算、       |
        |  输出初始电平写入、事件挂接分发              |
        +-------------------+----------------------+
                            | mr_gpio_ops_t（物理操作）
        +-------------------v----------------------+
        |   硬件 backend：引脚、电平、中断来源       |
        +------------------------------------------+
             依赖：驱动框架（生命周期/类型检查）、等待队列（GPIO 事件）
```

### 9. 数据结构

模型数据（`mrX/include/mrX/drivers/gpio/mr_gpio.h:78`）：

```c
typedef struct mr_gpio_data {
    mr_uint32_t exist_mask;     /* 物理存在的引脚 */
    mr_uint32_t invert_mask;    /* 低有效接线的引脚 */
} mr_gpio_data_t;
```

配置位域（`mr_gpio.h:68`，共 1 字节）：

| 字段    | 位宽 | 取值                                                                                   |
|---------|------|----------------------------------------------------------------------------------------|
| `mode`  | 3    | `mr_gpio_mode_t`：ANALOG/INPUT/OUTPUT/IRQ_RISING/IRQ_FALLING/IRQ_EDGE/IRQ_HIGH/IRQ_LOW |
| `alt`   | 1    | `mr_gpio_alt_t`：DISABLE/ENABLE                                                        |
| `pull`  | 2    | `mr_gpio_pull_t`：NONE/UP/DOWN                                                         |
| `otype` | 1    | `mr_gpio_otype_t`：PP/OD                                                               |
| `level` | 1    | `mr_gpio_level_t`：LOW/HIGH                                                            |

操作表（`mr_gpio.h:84`）：`pin_configure`、`pin_configuration`、`pin_write`、
`pin_read`、`port_write`、`port_read` 为必需（框架内 `MR_ASSERT` 强制），
`event_attach` 与 `event_control` 为可选事件回调；`event_enable`/`event_disable`
公共 API 通过同一个 `event_control`
回调启停来源。所有回调首参为 `const mr_driver_t *drv`，实现侧用
`MR_DRIVER_DATA`/`MR_DRIVER_PRIV` 取回数据与硬件实例。

hardtree 规格（`mr_gpio.h:619`）：`mr_ht_gpio_spec_t { drv, pin }`，由
`MR_HT_NODE_GPIO_SPEC` 从属性单元 `[phandle, pin]` 填充，`phandle` 经
`MR_HT_DRIVER_FIND` 解析为驱动指针。

### 10. 核心流程

**clazz**：GPIO 只定义了 release（`MR_DRIVER_CLAZZES_DEFINE(gpio, MR_NULL,
MR_NULL, gpio_release)`，`mrX/drivers/gpio/gpio.c:15`），没有 init/deinit—— GPIO 无需
setup 阶段的资源初始化，
`mr_driver_setup` 直接标记成功。

**写路径**：`mr_gpio_write`（`mr_gpio.h:228`）在框架层完成
`value = (!!value) ^ (!!(invert_mask & (1U << pin)))` 后调用
`ops->pin_write`，成功返回 `sizeof(value)`。端口写（`mr_gpio.h:298`）对整个 掩码做
`mask ^ invert_mask` 再 `& exist_mask`
，一步完成换算与过滤。

**配置路径**：`mr_gpio_configure`（`mrX/drivers/gpio/gpio.c:17`）校验引脚 存在性后，若
`config->mode == MR_GPIO_MODE_OUTPUT`
，先按 `invert_mask` 把 逻辑电平换算成物理电平，再调用 `ops->pin_configure`；不另走
`pin_write`。 调用者传入的 `config`
不被修改。`mr_gpio_configuration` 读回时对称地把物理 电平还原为逻辑电平。

**翻转路径**：`mr_gpio_toggle`（`mr_gpio.h:518`）直接连续调用
`ops->pin_read`/`ops->pin_write`，中间值取反但 **不**应用 `invert_mask`——
翻转的是物理电平；`mr_gpio_port_toggle`（
`mr_gpio.h:552`）同理用
`0xffffffff` 异或端口读值。

**事件路径**：`mr_gpio_event_attach` 只校验 pin 与可选 `event_attach` 能力，然后把
waiter 交给 backend；
`mr_gpio_event_enable`/`mr_gpio_event_disable` 同样只校验 pin 与 `event_control`
是否存在，再把 `events` 原样传下去。公共层不维护
EXTI 状态，也不规定 payload。

`event_control` 由 backend 实现，负责实际事件来源的控制和必要的资源清理。
若在中断上下文通知 waiter，回调也同步运行于该上下文；事件负载由具体 backend 约定。

### 11. 并发与中断安全

- 框架层 API（write/read/configure 等）无内部互斥：引脚操作通常要求最轻量
  路径，并发访问同一引脚的序列化由应用或底层 `ops`
  保证（平台实现通常用 寄存器原子置位/清除）。
- `mr_gpio_event_attach` 与等待者回调的关系遵循[等待队列](../../kernel/wait.md)
  的同步分发语义：回调运行在 backend 调用
  raise 的调用上下文，因此是否在中断中运行由 backend 决定。
- `mr_gpio_toggle` 是"读-改-写"两步操作，非原子；多执行流翻转同一引脚会 交错，需要外部同步。

### 12. 扩展与派生

- **移位寄存器扩展**：`mrX/drivers/gpio/gpio_hc595.c`（HC595 输出扩展）与
  `mrX/drivers/gpio/gpio_hc165.c`（HC165 输入扩展）演示了用普通 GPIO 引脚
  拼出新的"GPIO 端口"驱动：私有数据中保存一组
  `mr_ht_gpio_spec_t`（时钟/ 数据/锁存引脚）与影子寄存器，实现同一张
  `mr_gpio_ops_t`，应用完全无感。
- **平台移植**：片上 GPIO backend、EXTI 事件、HardTree GPIO spec
  与验收见 [GPIO 驱动移植指南](porting.md)。
- 新扩展驱动只需实现 `mr_gpio_ops_t` 并通过 `MR_HT_GPIO_INSTALL`/
  `mr_gpio_create` 注册，框架层换算逻辑自动生效。

### 13. 源码导读

| 位置                                         | 职责                                 |
|----------------------------------------------|--------------------------------------|
| `mrX/include/mrX/drivers/gpio/mr_gpio.h:31`  | 模式/复用/上下拉/输出类型/电平枚举   |
| `mrX/include/mrX/drivers/gpio/mr_gpio.h:68`  | 配置位域 `mr_gpio_config_t`          |
| `mrX/include/mrX/drivers/gpio/mr_gpio.h:78`  | 模型数据（存在/反相掩码）            |
| `mrX/include/mrX/drivers/gpio/mr_gpio.h:84`  | 操作表 `mr_gpio_ops_t`               |
| `mrX/include/mrX/drivers/gpio/mr_gpio.h:228` | 写引脚（反相换算）                   |
| `mrX/include/mrX/drivers/gpio/mr_gpio.h:298` | 端口批量写（换算 + 过滤）            |
| `mrX/include/mrX/drivers/gpio/mr_gpio.h:365` | 事件挂接分发                         |
| `mrX/include/mrX/drivers/gpio/mr_gpio.h:610` | hardtree 实例安装宏                  |
| `mrX/drivers/gpio/gpio.c:15`                 | GPIO clazz（仅 release）             |
| `mrX/drivers/gpio/gpio.c:17`                 | 配置路径（反相后交给 pin_configure） |
| `mrX/drivers/gpio/gpio.c:49`                 | 回读配置（还原逻辑电平）             |
| `mrX/drivers/gpio/gpio.c:84`                 | release：释放 `data`                 |
| `mrX/drivers/gpio/gpio_hc595.c`              | HC595 输出扩展驱动                   |
| `mrX/test/drivers/gpio/gpio.c`               | 行为级测试                           |

### 14. 附录：配置宏与错误码

**配置宏**

| 宏                   | 说明                                                |
|----------------------|-----------------------------------------------------|
| `MR_USE_DRIVER_GPIO` | 模块开关，默认 n，依赖 `MR_USE_DRIVER`              |
| `MR_GPIO_WAIT_EXTI`  | 引脚事件位（`1U << 0`），配合 `mr_waiter_init` 使用 |

**错误码**：公共层明确使用 `-MR_EINVAL` 表示配置/事件路径中的引脚不存在，使用
`-MR_ENOSYS` 表示可选事件能力未实现；其他
backend 错误码原样透传。注意 `mr_gpio_write`/`mr_gpio_read` 只断言 `pin < 32U`
，不会检查 `exist_mask`。错误码定义见
`mrX/include/mrX/libc/mr_errno.h`。

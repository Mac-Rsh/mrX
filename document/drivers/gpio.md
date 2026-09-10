# GPIO 驱动（GPIO）

> GPIO 驱动在驱动框架之上提供统一的引脚抽象：输入/输出/中断模式配置、电平
> 读写与整端口批量操作，并用"存在掩码 + 反相掩码"适配不同板卡的引脚布局与
> 低有效电路。

| 项       | 说明                                                                         |
|----------|------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_DRIVER_GPIO`（默认关闭，依赖 `MR_USE_DRIVER`）                       |
| 头文件   | `<mrX/drivers/gpio/mr_gpio.h>`                                               |
| 依赖模块 | [驱动框架 Driver](driver.md)、[等待队列 Wait](../kernel/wait.md)（引脚事件） |
| 参考测试 | `mrX/test/drivers/gpio/gpio.c`                                               |

---

## 用户指南

### 1. 模块概述

GPIO 驱动解决的问题是： **用同一套 API 操作不同 MCU/扩展芯片的数字引脚**， 应用只面对"引脚号 + 配置 + 电平"，硬件差异由具体
`ops` 实现消化。

特性：

- 8 种引脚模式：模拟、输入、输出与 5 种中断触发模式（上升沿/下降沿/双沿/ 高电平/低电平）。
- 引脚级读/写/翻转与端口级（32 位掩码）批量读/写/翻转。
- `exist_mask`（存在掩码）约束可用引脚范围，`invert_mask`（反相掩码）统一 处理低有效电路，应用始终使用逻辑电平。
- 便捷函数：`mr_gpio_input`/`mr_gpio_output` 一行完成常用配置。
- 可选的引脚事件挂接（EXTI），基于等待队列实现回调通知。
- hardtree 规格宏：可在硬件模板中声明"某驱动某引脚"，代码用
  `mr_ht_gpio_spec_t` 直接引用。

不适用于：需要数 MHz 以上翻转速率的场合（每次调用经过框架分层）。

### 2. 配置与依赖

| 配置项               | 类型 | 默认值 | 说明                                                     |
|----------------------|------|--------|----------------------------------------------------------|
| `MR_USE_DRIVER_GPIO` | bool | n      | 启用 GPIO 驱动（`mrX/include/mrX/drivers/gpio/Kconfig`） |
| `MR_USE_DRIVER`      | bool | n      | 上层依赖，启用 GPIO 时自动满足                           |

启用后重新生成 `mr_config.h`，包含头文件即可使用：

```c
#include <mrX/drivers/gpio/mr_gpio.h>
```

引脚编号固定为 0~31（对应 `mr_gpio_data_t` 中掩码的 32 个位），一个驱动实例 代表一个 32 位宽的 GPIO 端口（或芯片的全部引脚）。

### 3. 快速上手

点亮一个 LED（输出模式）：

```c
#include <mrX/drivers/gpio/mr_gpio.h>

void led_example(mr_driver_t *drv) {
    /* Output push-pull, initial level high */
    mr_gpio_output(drv, 5, MR_GPIO_OTYPE_PP, MR_GPIO_LEVEL_HIGH);

    mr_gpio_write(drv, 5, 0);       /* Logic low */
    mr_gpio_toggle(drv, 5);         /* Flip again */
}
```

读一个按键（输入模式 + 上拉）：

```c
mr_uint8_t level = 0;

mr_gpio_input(drv, 12, MR_GPIO_PULL_UP);
mr_gpio_read(drv, 12, &level);
if (level == MR_GPIO_LEVEL_LOW) {
    /* Button pressed (active-low wiring handled by invert_mask) */
}
```

`drv` 为 GPIO 类型驱动指针，可来自平台安装的驱动或 `mr_driver_find`。

### 4. 使用说明

**配置模型**：引脚属性集中在 `mr_gpio_config_t`：`mode`（8 种模式之一）、
`alt`（复用功能开关）、`pull`（none/up/down）、`otype`（推挽/开漏）、
`level`（输出初始电平）。`mr_gpio_configure` 一次写入全部属性；若模式为
`MR_GPIO_MODE_OUTPUT`，配置完成后框架立即按 `level` 写入初始电平。
`mr_gpio_configuration` 回读当前配置。

**逻辑电平与物理电平**：`mr_gpio_write`/`mr_gpio_read`/`mr_gpio_port_write`/
`mr_gpio_port_read` 操作的是逻辑电平，框架自动按 `invert_mask` 取反后交给 硬件。存在掩码校验只出现在配置与事件挂接路径（返回
`-MR_EINVAL`）；引脚级 读写仅断言引脚号小于 32，端口批量读写将存在掩码之外的位直接过滤。
`mr_gpio_toggle`/
`mr_gpio_port_toggle` 直接翻转硬件电平（读硬件值取反写回），不经过反相 换算。

**事件（EXTI）**：先把 `mr_waiter_t` 用 `mr_waiter_init` 初始化并指定期望 事件 `MR_GPIO_WAIT_EXTI`，再调用
`mr_gpio_event_attach` 挂到指定引脚。引脚 按配置的中断模式触发后，等待者回调被执行，事件数据为触发引脚号（类型
`mr_gpio_event_exti_data_t`，即 `mr_uint32_t`）。事件能力是可选的：平台
`ops` 未实现 `event_attach` 时返回 `-MR_ENOSYS`。回调在中断上下文执行， 内部只做轻量操作，不要阻塞。

**生命周期**：GPIO 驱动对象本身遵循[驱动框架](driver.md)的生命周期；注意
`mr_gpio_create` 传入的 `mr_gpio_data_t` 必须用 `mr_malloc` 分配——驱动删除 时框架会释放它。

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

| API                           | 说明                   |
|-------------------------------|------------------------|
| `mr_gpio_event_attach`        | 把等待者挂接到引脚事件 |
| `mr_gpio_get` / `mr_gpio_put` | 引用计数加一 / 减一    |

**`mr_gpio_configure`**

```c
mr_err_t mr_gpio_configure(mr_driver_t *drv, mr_uint32_t pin,
                           mr_gpio_config_t *config);
```

- `pin`：引脚号 0~31，且必须在 `exist_mask` 中，否则返回 `-MR_EINVAL`。
- `config`：引脚配置，不可为 `MR_NULL`。
- 返回：成功 `0`，失败负数错误码（含底层 `pin_configure` 的错误码透传）。

**`mr_gpio_write` / `mr_gpio_read`**

```c
mr_ssize_t mr_gpio_write(mr_driver_t *drv, mr_uint32_t pin, mr_uint8_t value);
mr_ssize_t mr_gpio_read(mr_driver_t *drv, mr_uint32_t pin, mr_uint8_t *value);
```

按逻辑电平读写（`invert_mask` 由框架处理）。成功返回 `sizeof(value)` 即 1， 底层失败返回负数错误码。

**`mr_gpio_port_write` / `mr_gpio_port_read`**

```c
mr_ssize_t mr_gpio_port_write(mr_driver_t *drv, mr_uint32_t mask);
mr_ssize_t mr_gpio_port_read(mr_driver_t *drv, mr_uint32_t *mask);
```

整端口掩码读写。写入前框架做 `mask ^ invert_mask` 再 `& exist_mask`，读取后 做同样换算，保证掩码语义与引脚级一致。成功返回
`sizeof(mask)` 即 4。

**`mr_gpio_event_attach`**

```c
mr_err_t mr_gpio_event_attach(mr_driver_t *drv, mr_uint32_t pin,
                              mr_waiter_t *entry);
```

- `entry`：已用 `mr_waiter_init` 初始化、期望事件含 `MR_GPIO_WAIT_EXTI` 的 等待者。
- 返回：成功 `0`；引脚不存在 `-MR_EINVAL`；平台不支持 `-MR_ENOSYS`。

**hardtree 规格**

```c
mr_ht_gpio_spec_t spec = MR_HT_GPIO_SPEC(instance_id, gpios);
mr_ht_gpio_output(&spec, MR_GPIO_OTYPE_PP, MR_GPIO_LEVEL_HIGH);
```

`mr_ht_gpio_spec_t` 携带驱动指针与引脚号，由宏从硬件模板属性单元
`[phandle, pin]` 生成；配套 `mr_ht_gpio_configure/input/output/write/read/
toggle/event_attach` 与属性数组版本 `MR_HT_GPIO_SPEC_FOREACH`。

### 6. 完整示例

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
    data->exist_mask = 0xffffffffU;     /* All 32 pins exist */
    data->invert_mask = 0x00000001U;    /* Pin 0 is active-low */
    drv = mr_gpio_create(data, MR_NULL, (mr_gpio_ops_t *)&board_gpio_ops);
    if (drv == MR_NULL) {
        return;
    }
    mr_driver_setup(drv);

    mr_gpio_input(drv, 3, MR_GPIO_PULL_UP);
    mr_gpio_read(drv, 3, &value);
    mr_gpio_output(drv, 0, MR_GPIO_OTYPE_PP, 1);    /* Physical low */

    mr_gpio_del(drv);                   /* Frees data */
}
```

（`board_gpio_ops` 为板级 `mr_gpio_ops_t`，需实现 `pin_configure`/
`pin_write`/`pin_read`/`port_write`/`port_read`。）

示例 2：挂接按键 EXTI 事件：

```c
static void key_waiter(mr_waiter_t *waiter, void *param,
                       mr_wait_event_t event, const void *data) {
    mr_gpio_event_exti_data_t pin = (mr_gpio_event_exti_data_t)(size_t)data;

    (void)pin;  /* Pin number that triggered the EXTI */
    /* Set a flag / post to work queue; do not block here */
}

void key_example(mr_driver_t *drv) {
    static mr_waiter_t waiter;
    mr_gpio_config_t cfg = {.mode = MR_GPIO_MODE_IRQ_FALLING,
                            .pull = MR_GPIO_PULL_UP};

    mr_gpio_configure(drv, 12, &cfg);
    mr_waiter_init(&waiter, key_waiter, MR_NULL, MR_GPIO_WAIT_EXTI);
    mr_gpio_event_attach(drv, 12, &waiter);
}
```

更多行为（不存在引脚拒绝、配置错误透传、端口掩码换算、事件不支持路径）见
`mrX/test/drivers/gpio/gpio.c`。

### 7. 常见问题（FAQ）

**Q：`mr_gpio_write` 返回负数错误码？**

`mr_gpio_write`/`mr_gpio_read` 不校验 `exist_mask`，负数返回值是底层 `ops`
错误透传（如 `-MR_EIO`）；引脚号大于等于 32 在断言开启时由 `MR_ASSERT`
拦截。`-MR_EINVAL`（引脚不在 `exist_mask` 中）出现在
`mr_gpio_configure`/`mr_gpio_configuration`/`mr_gpio_event_attach`，检查 硬件模板里的 `exist_mask` 属性与引脚号。

**Q：写 1 量出来是 0？**

该引脚在 `invert_mask` 中被声明为低有效。`write`/`read` 是逻辑电平；需要操作 物理电平时用 `mr_gpio_toggle` 或修正
`invert_mask`。

**Q：`mr_gpio_event_attach` 返回 `-MR_ENOSYS`？**

当前驱动的 `ops` 没有实现 `event_attach`（如移位寄存器扩展 GPIO）。只有平台 片上 GPIO 支持 EXTI 事件。

**Q：输出配置后电平不对？**

`mr_gpio_configure` 只在模式为 `MR_GPIO_MODE_OUTPUT` 时写入 `level` 初始电平； 其他模式忽略 `level`。输入模式下 `otype`/
`level` 不参与配置。

**Q：`mr_gpio_toggle` 和连续 write 有什么区别？**

`toggle` 在框架层直接对硬件电平取反（不经过 `invert_mask`），一次"读-改-写"
完成翻转；对反相引脚连续 `write` 则需要应用自己维护逻辑电平。

---

## 开发者指南

### 8. 架构总览

GPIO 驱动是[驱动框架](driver.md)的第一个派生模块，采用"框架层换算 + ops 下沉"结构：

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
        |  平台实现：ch32v 片上 GPIO / HC165/HC595   |
        +------------------------------------------+
             依赖：驱动框架（生命周期/类型检查）、等待队列（EXTI 事件）
```

### 9. 数据结构

模型数据（`mrX/include/mrX/drivers/gpio/mr_gpio.h:78`）：

```c
typedef struct mr_gpio_data {
    mr_uint32_t exist_mask;     /* Pins that physically exist */
    mr_uint32_t invert_mask;    /* Pins wired active-low */
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
`event_attach` 可选。所有回调首参为 `const mr_driver_t *drv`，实现侧用
`MR_DRIVER_DATA`/`MR_DRIVER_PRIV` 取回数据与硬件实例。

hardtree 规格（`mr_gpio.h:546`）：`mr_ht_gpio_spec_t { drv, pin }`，由
`MR_HT_NODE_GPIO_SPEC` 从属性单元 `[phandle, pin]` 填充，`phandle` 经
`MR_HT_DRIVER_FIND` 解析为驱动指针。

### 10. 核心流程

**clazz**：GPIO 只定义了 release（`MR_DRIVER_CLAZZES_DEFINE(gpio, MR_NULL,
MR_NULL, gpio_release)`，`mrX/drivers/gpio/gpio.c:15`），没有 init/deinit—— GPIO 无需 setup 阶段的资源初始化，
`mr_driver_setup` 直接标记成功。

**写路径**：`mr_gpio_write`（`mr_gpio.h:227`）在框架层完成
`value = (!!value) ^ (!!(invert_mask & (1U << pin)))` 后调用
`ops->pin_write`，成功返回 `sizeof(value)`。端口写（`mr_gpio.h:297`）对整个 掩码做 `mask ^ invert_mask` 再 `& exist_mask`
，一步完成换算与过滤。

**配置路径**：`mr_gpio_configure`（`mrX/drivers/gpio/gpio.c:17`）校验引脚 存在性后，若 `config->mode == MR_GPIO_MODE_OUTPUT`
，先按 `invert_mask` 把 逻辑电平换算成物理电平，再调用 `ops->pin_configure`；不另走 `pin_write`。 调用者传入的 `config`
不被修改。`mr_gpio_configuration` 读回时对称地把物理 电平还原为逻辑电平。

**翻转路径**：`mr_gpio_toggle`（`mr_gpio.h:445`）直接连续调用
`ops->pin_read`/`ops->pin_write`，中间值取反但 **不**应用 `invert_mask`—— 翻转的是物理电平；`mr_gpio_port_toggle`（
`mr_gpio.h:479`）同理用
`0xffffffff` 异或端口读值。

**事件路径**：`mr_gpio_event_attach`（`mr_gpio.h:364`）校验引脚存在与能力位 后直接透传 `ops->event_attach(drv, pin, entry)`
，把等待者加入平台实现的 EXTI 分发表；平台在中断服务中携带 `mr_gpio_event_exti_data_t`（引脚号）触发 等待事件（参考
`mrX/arch/wch/ch32v/platform/drivers/gpio/gpio.c:372`）。

### 11. 并发与中断安全

- 框架层 API（write/read/configure 等）无内部互斥：引脚操作通常要求最轻量 路径，并发访问同一引脚的序列化由应用或底层 `ops`
  保证（平台实现通常用 寄存器原子置位/清除）。
- `mr_gpio_event_attach` 与等待者回调的关系遵循[等待队列](../kernel/wait.md)
  约定：挂接可在中断上下文调用；回调运行在平台 EXTI 中断上下文，内部禁止 阻塞。
- `mr_gpio_toggle` 是"读-改-写"两步操作，非原子；多执行流翻转同一引脚会 交错，需要外部同步。

### 12. 扩展与派生

- **移位寄存器扩展**：`mrX/drivers/gpio/gpio_hc595.c`（HC595 输出扩展）与
  `mrX/drivers/gpio/gpio_hc165.c`（HC165 输入扩展）演示了用普通 GPIO 引脚 拼出新的"GPIO 端口"驱动：私有数据中保存一组
  `mr_ht_gpio_spec_t`（时钟/ 数据/锁存引脚）与影子寄存器，实现同一张 `mr_gpio_ops_t`，应用完全无感。
- **平台移植**：`mrX/arch/wch/ch32v/platform/drivers/gpio/gpio.c` 以
  `MR_HT_GPIO_INSTALL` 按硬件模板实例化片上 GPIO，是编写新平台 GPIO 的参考。
- 新扩展驱动只需实现 `mr_gpio_ops_t` 并通过 `MR_HT_GPIO_INSTALL`/
  `mr_gpio_create` 注册，框架层换算逻辑自动生效。

### 13. 源码导读

| 位置                                         | 职责                                 |
|----------------------------------------------|--------------------------------------|
| `mrX/include/mrX/drivers/gpio/mr_gpio.h:31`  | 模式/复用/上下拉/输出类型/电平枚举   |
| `mrX/include/mrX/drivers/gpio/mr_gpio.h:68`  | 配置位域 `mr_gpio_config_t`          |
| `mrX/include/mrX/drivers/gpio/mr_gpio.h:78`  | 模型数据（存在/反相掩码）            |
| `mrX/include/mrX/drivers/gpio/mr_gpio.h:84`  | 操作表 `mr_gpio_ops_t`               |
| `mrX/include/mrX/drivers/gpio/mr_gpio.h:227` | 写引脚（反相换算）                   |
| `mrX/include/mrX/drivers/gpio/mr_gpio.h:297` | 端口批量写（换算 + 过滤）            |
| `mrX/include/mrX/drivers/gpio/mr_gpio.h:364` | 事件挂接分发                         |
| `mrX/include/mrX/drivers/gpio/mr_gpio.h:537` | hardtree 实例安装宏                  |
| `mrX/drivers/gpio/gpio.c:15`                 | GPIO clazz（仅 release）             |
| `mrX/drivers/gpio/gpio.c:17`                 | 配置路径（反相后交给 pin_configure） |
| `mrX/drivers/gpio/gpio.c:53`                 | 回读配置（还原逻辑电平）             |
| `mrX/drivers/gpio/gpio.c:93`                 | release：释放 `data`                 |
| `mrX/drivers/gpio/gpio_hc595.c`              | HC595 输出扩展驱动                   |
| `mrX/test/drivers/gpio/gpio.c`               | 行为级测试                           |

### 14. 附录：配置宏与错误码

**配置宏**

| 宏                   | 说明                                                |
|----------------------|-----------------------------------------------------|
| `MR_USE_DRIVER_GPIO` | 模块开关，默认 n，依赖 `MR_USE_DRIVER`              |
| `MR_GPIO_WAIT_EXTI`  | 引脚事件位（`1U << 0`），配合 `mr_waiter_init` 使用 |

**错误码**：`-MR_EINVAL`（引脚不存在）、`-MR_ENOSYS`（事件能力未实现）、 底层 `ops` 错误码透传（如 `-MR_EIO`），定义见
`mrX/include/mrX/libc/mr_errno.h`；空指针、引脚号越界（>= 32）等编程错误在 断言开启时由 `MR_ASSERT` 拦截。

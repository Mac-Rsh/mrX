# HC165 并行输入扩展 GPIO（GPIO-HC165）

> HC165 扩展驱动用 3~5 个普通 GPIO 引脚驱动 74HC165 并行输入移位寄存器，
> 把级联芯片的并行输入呈现为一个只读的 32 位 GPIO 端口，应用直接使用
> [GPIO](gpio.md) 的读接口采集外扩数字输入。

| 项       | 说明                                                                                                  |
|----------|-------------------------------------------------------------------------------------------------------|
| 功能宏   | 无独立功能宏（随 `MR_USE_DRIVER_GPIO` 编译；仅当硬件模板存在 compat `gpio_hc165` 实例时展开驱动代码） |
| 头文件   | `<mrX/drivers/gpio/mr_gpio.h>`（复用 GPIO 驱动接口）                                                  |
| 依赖模块 | [GPIO 驱动](gpio.md)、[驱动框架 Driver](../driver.md)                                                    |
| 参考测试 | 无（`mrX/test/drivers/` 下暂无对应测试）                                                              |

---

## 用户指南

### 1. 模块概述

HC165 扩展解决的问题是： **片上 GPIO 引脚不够读入时，用最少的引脚扩展一批 数字输入**。74HC165 通过 SH/\!LD 锁存并行输入、CLK
逐位移出，本驱动把这一 移位时序封装在 GPIO 的 `ops` 之后，应用完全无感。

特性：

- 引脚级读 `mr_gpio_read` 与端口级批量读 `mr_gpio_port_read` 均可用，读到 的是扩展芯片输入的逻辑电平。
- 支持级联：`cascade_num` 描述链上芯片数量（1~4），最多扩展 32 个输入。
- 第一次对合法输入 pin 进行配置时完成宿主引脚的方向与初始电平设置；后续合法输入配置会复用已经初始化好的宿主引脚。
- 可选引脚 CLK-INH（时钟禁止）与 SER（串行级联输入）由硬件模板声明， 存在即自动初始化。
- 存在掩码/反相掩码（`exist_mask`/`invert_mask`）由硬件模板属性生成， 低有效电路的处理与 [GPIO](gpio.md) 完全一致。

不适用于：输出扩展（HC165 是纯输入芯片，写操作固定返回 `-MR_EPERM`）与 中断输入（`event_attach` 未实现，事件挂接返回
`-MR_ENOSYS`）。

### 2. 配置与依赖

| 配置项               | 类型 | 默认值 | 说明                                     |
|----------------------|------|--------|------------------------------------------|
| `MR_USE_DRIVER_GPIO` | bool | n      | 上层开关，启用 GPIO 驱动后本扩展参与编译 |
| `MR_USE_DRIVER`      | bool | n      | `MR_USE_DRIVER_GPIO` 的依赖              |

本扩展没有独立的 Kconfig 选项：使能 `MR_USE_DRIVER_GPIO` 后，驱动代码仅在 硬件模板中存在 compat 为 `gpio_hc165` 的实例时被编译（
`MR_HT_HAS_DRIVER()`
门控）。实例属性在硬件模板节点上描述：

| 属性          | 必需 | 说明                                        |
|---------------|------|---------------------------------------------|
| `clk`         | 是   | 移位时钟引脚 CLK，属性单元 `[phandle, pin]` |
| `sh`          | 是   | 锁存/移位控制引脚 SH/\!LD                   |
| `qh`          | 是   | 串行数据输出引脚 QH（接 MCU 输入）          |
| `clk_inh`     | 否   | 时钟禁止引脚 CLK-INH                        |
| `ser`         | 否   | 串行输入引脚 SER（多芯片级联时使用）        |
| `cascade_num` | 否   | 级联芯片数，缺省 1，允许 1~4                |
| `exist_mask`  | 否   | GPIO 存在掩码，缺省 `0xffffffff`；建议按实际级联宽度显式设置 |
| `invert_mask` | 否   | 反相掩码，缺省 `0x00000000`                 |
| `INIT_LEVEL`  | 否   | 安装级别，缺省 DEFER                        |

启用后重新生成 `mr_config.h`，包含头文件即可使用：

```c
#include <mrX/drivers/gpio/mr_gpio.h>
```

引脚编号 0 ~ `cascade_num * 8 - 1`，一个实例代表整条级联链的全部输入。

### 3. 快速上手

读取 8 路拨码开关：

```c
#include <mrX/drivers/gpio/mr_gpio.h>

void hc165_example(void) {
    mr_uint8_t level;
    mr_driver_t *drv = mr_driver_find("gpio_hc165");    /* Name from template */

    if (drv == MR_NULL) {
        return;
    }
    mr_driver_setup(drv);       /* DEFER level: setup by the application */

    mr_gpio_input(drv, 0, MR_GPIO_PULL_NONE);   /* Initializes host pins once */
    mr_gpio_read(drv, 3, &level);

    mr_driver_put(drv);         /* Release the reference from find */
}
```

第一次对合法引脚调用 `mr_gpio_input`/`mr_gpio_configure` 时，驱动一次性完成 CLK/SH/\!LD/QH（及可选 CLK-INH/SER）的初始化。之后再次配置合法、且仍为 `MR_GPIO_MODE_INPUT` 的引脚时，不会重复初始化这些宿主引脚；pin 范围和 mode 校验仍然每次执行。

### 4. 使用说明

**只读端口**：实例是纯输入端口。`mr_gpio_write`/`mr_gpio_port_write` 一律 返回 `-MR_EPERM`；`mr_gpio_configure` 只接受
`MR_GPIO_MODE_INPUT` 模式， 其他模式返回 `-MR_EINVAL`；`mr_gpio_configuration` 回读时固定报告输入模式、 无上下拉。

**读取语义**：`mr_gpio_read` 每次都会执行完整的移位时序——先脉冲 SH/\!LD 把所有并行输入锁进芯片，再逐位读 QH 并脉冲
CLK，最后从结果中取出目标位。
`mr_gpio_port_read` 执行同样的时序但一次性返回整条链的掩码，需要读多个 引脚时用它只移位一次。

**配置幂等**：宿主引脚的初始化只发生一次（内部 `configured` 标记）。后续合法输入配置在通过 pin/mode 校验后直接成功，不会重复改动时钟与锁存引脚状态；非法 pin 或非 INPUT mode 仍返回 `-MR_EINVAL`。

**事件**：本驱动的 `event_attach` 能力为空，`mr_gpio_event_attach` 返回
`-MR_ENOSYS`；输入变化需要轮询获取。

**生命周期**：驱动实例由硬件模板在编译期安装，安装级别缺省 DEFER，应用 需 `mr_driver_find` 后调用 `mr_driver_setup`
启用；引用管理遵循
[驱动框架](../driver.md)（`mr_gpio_get`/`mr_gpio_put`）。

### 5. API 参考

本扩展不引入新 API，全部经由 [GPIO 驱动](gpio.md)的接口访问。各接口在本 驱动上的行为：

初始化与引用：

| API                                  | 说明                           |
|--------------------------------------|--------------------------------|
| `mr_driver_find` / `mr_driver_setup` | 按硬件模板节点名查找并启动实例 |
| `mr_gpio_get` / `mr_gpio_put`        | 引用计数加一 / 减一            |

输入操作：

| API                     | 说明                                               |
|-------------------------|----------------------------------------------------|
| `mr_gpio_input`         | 配置引脚为输入（首次调用同时初始化宿主引脚）       |
| `mr_gpio_configure`     | 仅接受 `MR_GPIO_MODE_INPUT`，其余返回 `-MR_EINVAL` |
| `mr_gpio_configuration` | 回读配置，固定 INPUT/PULL_NONE/ALT_DISABLE         |
| `mr_gpio_read`          | 移入整条链后返回目标引脚电平，成功返回 1           |
| `mr_gpio_port_read`     | 移入整条链并返回全部输入掩码，成功返回 4           |

不可用操作：

| API                    | 行为                                |
|------------------------|-------------------------------------|
| `mr_gpio_write`        | 返回 `-MR_EPERM`（输入芯片不可写）  |
| `mr_gpio_port_write`   | 返回 `-MR_EPERM`                    |
| `mr_gpio_event_attach` | 返回 `-MR_ENOSYS`（事件能力未实现） |

**`mr_gpio_port_read`**

```c
mr_ssize_t mr_gpio_port_read(mr_driver_t *drv, mr_uint32_t *mask);
```

- `mask`：输出掩码，bit n 对应扩展输入 n。
- 返回：成功 `sizeof(mask)` 即 4，宿主引脚操作失败返回负数错误码。

**`mr_gpio_read`**

```c
mr_ssize_t mr_gpio_read(mr_driver_t *drv, mr_uint32_t pin, mr_uint8_t *value);
```

- `pin`：逻辑有效范围是 0 ~ `cascade_num * 8 - 1`。`mr_gpio_read` 公共层只断言 `pin < 32U`，HC165 的 `drv_pin_read` 也不再次做级联范围检查，所以若 `exist_mask` 仍保持默认全 1，读取实际级联宽度之外的 pin 会从零填充的 mask 高位得到 0。建议让模板中的 `exist_mask` 与实际级联宽度一致。
- `value`：输出逻辑电平（`MR_GPIO_LEVEL_LOW`/`MR_GPIO_LEVEL_HIGH`）。
- 返回：成功 1，失败负数错误码。

### 6. 完整示例

示例 1：轮询 16 路级联输入（`cascade_num = 2`），整链一次移入：

```c
void hc165_poll_example(void) {
    mr_uint32_t mask;
    mr_driver_t *drv = mr_driver_find("gpio_hc165");

    if (drv == MR_NULL) {
        return;
    }
    mr_driver_setup(drv);
    mr_gpio_input(drv, 0, MR_GPIO_PULL_NONE);

    while (1) {
        if (mr_gpio_port_read(drv, &mask) == sizeof(mask)) {
            /* mask bit n is extended input n */
            if ((mask & (1U << 9)) != 0U) {
                /* Input 9 active */
            }
        }
    }
}
```

示例 2：结合反相掩码处理低有效按键（`invert_mask` 声明在硬件模板中， 框架层自动取反，应用始终使用逻辑电平）：

```c
void hc165_active_low_example(mr_driver_t *drv) {
    mr_uint8_t level;

    mr_gpio_input(drv, 5, MR_GPIO_PULL_NONE);
    if (mr_gpio_read(drv, 5, &level) == 1) {
        if (level == MR_GPIO_LEVEL_HIGH) {
            /* Logic high = physical released for an active-low key */
        }
    }
}
```

（本扩展无行为级测试文件，示例按 [GPIO](gpio.md) 的示例风格自拟，仅使用
`mr_gpio.h` 中真实存在的接口。）

### 7. 常见问题（FAQ）

**Q：`mr_gpio_write` 返回 `-MR_EPERM`？**

HC165 是并行输入移位寄存器，实例为只读端口。输出扩展请使用
[HC595](gpio_hc595.md)。

**Q：`mr_gpio_configure` 返回 `-MR_EINVAL`？**

模式不是 `MR_GPIO_MODE_INPUT`，或引脚号不小于 `cascade_num * 8`。检查配置 模式与硬件模板中的 `cascade_num`。

**Q：读到的电平一直是 0 或不变？**

排查：QH 是否接到模板指定的 MCU 引脚；SH/\!LD 与 CLK 引脚定义是否正确；
`cascade_num` 是否与实际级联数量一致（链长不符会整体错位）；低有效电路 是否已在 `invert_mask` 中声明。

**Q：读一个引脚为什么感觉慢？**

每次 `mr_gpio_read` 都会重新锁存并移入整条链（`cascade_num * 8` 次 QH 采样 + CLK 脉冲）。读多个引脚时改用
`mr_gpio_port_read`，一次时序 拿到全部输入。

**Q：反复 `mr_gpio_input` 会破坏时序引脚状态吗？**

对合法的 INPUT 配置不会。宿主引脚只在首次成功配置时初始化；之后仍会先检查 pin 范围与 mode，校验通过后才直接成功。

**Q：`mr_driver_find` 找不到驱动？**

确认硬件模板中存在 compat `gpio_hc165` 的实例、`MR_USE_DRIVER_GPIO` 已 使能并重新生成了 `mr_config.h`；查找名取自模板节点名而非
compat 名。

---

## 开发者指南

### 8. 架构总览

HC165 扩展是 [GPIO 驱动](gpio.md)框架层之下的一个 `ops` 实现，用宿主 GPIO 引脚位拆（bit-bang）出新的"GPIO 端口"：

```
        +------------------------------------------+
        |  应用（mr_gpio_read / mr_gpio_port_read）  |
        +-------------------+----------------------+
                            | exist/invert 换算、引脚校验
        +-------------------v----------------------+
        |  GPIO 框架层（drivers/gpio/gpio.c + mr_gpio.h） |
        +-------------------+----------------------+
                            | mr_gpio_ops_t
        +-------------------v----------------------+
        |  HC165 ops（gpio_hc165.c）                 |
        |  移位时序 = mr_ht_gpio_write/read 序列      |
        +-------------------+----------------------+
                            | mr_ht_gpio_spec_t（宿主引脚）
        +-------------------v----------------------+
        |  平台 GPIO 驱动（片上引脚）                 |
        +------------------------------------------+
             依赖：GPIO 框架层、驱动框架（hardtree 安装）
```

### 9. 数据结构

私有数据（`mrX/drivers/gpio/gpio_hc165.c:38`）：

```c
typedef struct drv_gpio_hc165_priv {
    mr_ht_gpio_spec_t clk_inh;      /* Optional clock-inhibit pin */
    mr_ht_gpio_spec_t ser;          /* Optional serial-input pin */
    mr_ht_gpio_spec_t clk;          /* Shift clock pin */
    mr_ht_gpio_spec_t sh;           /* Shift/load pin (SH/\!LD) */
    mr_ht_gpio_spec_t qh;           /* Serial data-out pin */
    mr_uint8_t cascade_num;         /* Chips in the chain, 1~4 */
    mr_bool_t configured;           /* Host pins initialized */
} drv_gpio_hc165_priv_t;
```

`mr_ht_gpio_spec_t`（`mr_gpio.h:619`）是 `{drv, pin}` 二元组，由安装宏
`MR_HT_GPIO_SPEC(_id, _prop)` 从硬件模板属性单元 `[phandle, pin]` 生成；
`phandle` 经 `MR_HT_DRIVER_FIND` 解析为宿主 GPIO 驱动指针。

操作表（`gpio_hc165.c:27`）：六个必需回调全部实现，`event_attach` 置
`MR_NULL`。`MR_HT_GPIO_INSTALL`（`mr_gpio.h:610`）据模板属性生成
`mr_gpio_data_t`（`exist_mask` 缺省全 1、`invert_mask` 缺省全 0）并以
`MR_DRIVER_CLAZZES_STATIK(gpio)` 安装实例。

### 10. 核心流程

**读时序到 GPIO 读回调的映射**：核心在 `hc165_cascade_read`
（`gpio_hc165.c:48`），对应 74HC165 的"锁存-移位"两阶段：

1. 锁存：SH/\!LD 写低再写高（`gpio_hc165.c:55-62`），并行输入进入芯片 内部移位寄存器。
2. 移位：对 `cascade_num` 个芯片逐个循环，每芯片 8 位；每位先读 QH 引脚 （`mr_ht_gpio_read`），再给 CLK
   一个高→低脉冲把下一位移出 （`gpio_hc165.c:65-85`）。
3. 位序映射：`index = (cascade_num - chip - 1) * 8 + (bit - 1)`，每芯片 内先读到的位是高位（MSB）；链上最先移出的芯片占据端口掩码的最高
   8 位，
   `chip = 0` 即最先移出的芯片（`gpio_hc165.c:72`）。

`drv_pin_read`（`gpio_hc165.c:165`）在整链移入后按 `mask >> pin` 取出 目标位；`drv_port_read`（`gpio_hc165.c:190`）直接返回整链掩码。

**配置路径**：`drv_pin_configure`（`gpio_hc165.c:89`）先断言
`cascade_num` 在 1~4，再校验模式与引脚范围；首次配置时依次初始化可选 CLK-INH/SER（输出低，时钟使能）、CLK（输出低）、SH/\!LD（输出高，空闲
态）、QH（无上下拉输入），然后置位 `configured`。`drv_pin_configuration`
（`gpio_hc165.c:143`）不回读硬件，直接报告固定的输入属性。

**安装路径**：`GPIO_HC165_HT_GPIO_INSTALL`（`gpio_hc165.c:198`）为每个 模板实例展开静态私有数据——可选引脚用
`MR_HT_DRIVER_HAS_PROP` 条件填充 （不存在则为 `{0}`，即 `drv == MR_NULL`，配置时跳过），`cascade_num` 用
`MR_HT_DRIVER_PROP_OR` 取缺省 1——再经 `MR_HT_GPIO_INSTALL` 安装；
`MR_HT_DRIVER_FOREACH`（`gpio_hc165.c:211`）对 compat 组所有实例批量执行。

### 11. 并发与中断安全

- ops 层无任何互斥或临界区：一次端口读是多次宿主引脚写/读组成的完整移位序列，两个执行流并发访问会交替翻转 CLK/SH/\!LD，时序会被破坏。并发访问同一实例必须由上层序列化。
- 宿主引脚操作经由平台 GPIO 驱动完成，但这不能使 HC165 的整段多步时序自动具备原子性。
- 回调同步运行在调用者上下文。驱动本身没有声明 ISR-safe；由于读取需要完整 bit-bang 序列，不应仅因为底层 API 可调用就假定适合在中断上下文执行。
- `configured` 只是普通布尔字段，用来避免重复初始化宿主引脚，不是并发同步原语。

### 12. 扩展与派生

本扩展驱动不注册自己的对象类型：`gpio_hc165.c` 内不存在 `MR_DRIVER_CLAZZES_DEFINE`，安装时通过 `MR_HT_GPIO_INSTALL` 借用
GPIO 框架共享的 clazz（`mrX/include/mrX/drivers/gpio/mr_gpio.h:610`，clazz 实际定义在 `mrX/drivers/gpio/gpio.c:15`
）。因此"派生子类型"的路径在这里不存在，扩展方式是 **组合与新增实例**：

- **新增派生实例**：在硬件模板中声明 compat 为 `gpio_hc165` 的节点并给出 `clk` / `sh` / `qh`（可选 `clk_inh` / `ser` /
  `cascade_num` / `exist_mask` / `invert_mask`），使能 `MR_USE_DRIVER_GPIO`、重新生成 `mr_config.h` 后，
  `MR_HT_DRIVER_FOREACH`（`mrX/drivers/gpio/gpio_hc165.c:211`）会为该 compat 组的每个实例批量展开静态私有数据与安装（
  `gpio_hc165.c:198`）。
- **组合出同类端口**：位拆式输入扩展的通用做法是自定义一张 `mr_gpio_ops_t`（`mr_gpio.h:84`），私有数据里保存上游
  `mr_ht_gpio_spec_t`（`mr_gpio.h:619`）并自行实现时序，再用 `mr_gpio_init`（`mr_gpio.h:147`）或 `mr_gpio_create`（
  `mr_gpio.h:168`）挂到 GPIO 框架上，框架层换算逻辑自动生效；运行期以 ops 组装端口的参考写法见
  `mrX/test/drivers/gpio/gpio.c:123-132`。
- `MR_HT_COMPAT`（`gpio_hc165.c:13`）只用于把本文件绑定到一个 compat 组，新增一种芯片应当新增独立的 compat
  与驱动文件，而不是在本文件里扩展条件分支。

### 13. 源码导读

| 位置                                         | 职责                                          |
|----------------------------------------------|-----------------------------------------------|
| `mrX/drivers/gpio/gpio_hc165.c:13`           | `MR_HT_COMPAT` 定义为 `gpio_hc165`            |
| `mrX/drivers/gpio/gpio_hc165.c:27`           | 操作表（`event_attach` 为空）                 |
| `mrX/drivers/gpio/gpio_hc165.c:38`           | 私有数据（宿主引脚规格 + 级联数）             |
| `mrX/drivers/gpio/gpio_hc165.c:48`           | 移位读时序（锁存 + 逐位采样）                 |
| `mrX/drivers/gpio/gpio_hc165.c:89`           | 配置路径（模式校验 + 宿主引脚初始化）         |
| `mrX/drivers/gpio/gpio_hc165.c:143`          | 配置回读（固定输入属性）                      |
| `mrX/drivers/gpio/gpio_hc165.c:165`          | 引脚读（整链移入后取位）                      |
| `mrX/drivers/gpio/gpio_hc165.c:182`          | 端口写拒绝（`-MR_EPERM`）                     |
| `mrX/drivers/gpio/gpio_hc165.c:190`          | 端口读（整链掩码）                            |
| `mrX/drivers/gpio/gpio_hc165.c:198`          | 实例安装宏（可选属性条件展开）                |
| `mrX/include/mrX/drivers/gpio/mr_gpio.h:610` | `MR_HT_GPIO_INSTALL`（生成 `mr_gpio_data_t`） |
| `mrX/include/mrX/drivers/gpio/mr_gpio.h:619` | 宿主引脚规格 `mr_ht_gpio_spec_t`              |

### 14. 附录：配置宏与错误码

**配置宏**

| 宏                   | 说明                                                       |
|----------------------|------------------------------------------------------------|
| `MR_USE_DRIVER_GPIO` | 上层开关（`mrX/include/mrX/drivers/gpio/Kconfig`），默认 n |
| `MR_HT_COMPAT`       | 本文件内定义为 `gpio_hc165`，与硬件模板 compat 匹配        |

**错误码**：`-MR_EINVAL` 可来自 GPIO 公共层的 `exist_mask` 检查，也可来自 HC165 backend 的非 INPUT 配置或 `pin >= cascade_num * 8`；`-MR_EPERM` 表示写操作不支持；宿主引脚操作失败时透传其错误码。`cascade_num` 的 1~4 约束目前只在配置路径以 `MR_ASSERT` 检查，空指针等编程错误同样由断言保护。

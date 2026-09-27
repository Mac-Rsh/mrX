# HC595 并行输出扩展 GPIO（GPIO-HC595）

> HC595 扩展驱动用 3~5 个普通 GPIO 引脚驱动 74HC595 并行输出移位寄存器，
> 把级联芯片的并行输出呈现为一个 32 位 GPIO 端口，应用直接使用
> [GPIO](gpio.md) 的写接口驱动外扩数字输出。

| 项       | 说明                                                                                                  |
|----------|-------------------------------------------------------------------------------------------------------|
| 功能宏   | 无独立功能宏（随 `MR_USE_DRIVER_GPIO` 编译；仅当硬件模板存在 compat `gpio_hc595` 实例时展开驱动代码） |
| 头文件   | `<mrX/drivers/gpio/mr_gpio.h>`（复用 GPIO 驱动接口）                                                  |
| 依赖模块 | [GPIO 驱动](gpio.md)、[驱动框架 Driver](../driver.md)                                                 |
| 参考测试 | 无（`mrX/test/drivers/` 下暂无对应测试）                                                              |

---

## 用户指南

### 1. 模块概述

HC595 扩展解决的问题是： **片上 GPIO 引脚不够输出时，用最少的引脚扩展一批 数字输出**。74HC595 通过 SRCLK 逐位移入、RCLK
锁存输出，本驱动把这一移位 时序封装在 GPIO 的 `ops` 之后，应用按普通输出引脚的方式读写即可。

特性：

- 引脚级写 `mr_gpio_write` 与端口级批量写 `mr_gpio_port_write` 均可用， 写入的是扩展芯片输出的逻辑电平。
- 影子寄存器（shadow）：读接口直接返回最后写入的值，不改动物理输出； 引脚写入值未变化时跳过整轮移位时序。
- 支持级联：`cascade_num` 描述链上芯片数量（1~4），最多扩展 32 个输出。
- 首次配置时自动复位输出（全 0）并在存在 OE 引脚时使能输出。
- 可选引脚 OE（输出使能，低有效）与 SRCLR（寄存器清零，低有效）由硬件 模板声明，存在即自动初始化。
- 存在掩码/反相掩码（`exist_mask`/`invert_mask`）由硬件模板属性生成， 低有效电路的处理与 [GPIO](gpio.md) 完全一致。

不适用于：输入采集（HC595 无法回读引脚实际电平，读到的始终是影子寄存 器）与中断事件（`event_attach` 未实现）。

### 2. 配置与依赖

| 配置项               | 类型 | 默认值 | 说明                                     |
|----------------------|------|--------|------------------------------------------|
| `MR_USE_DRIVER_GPIO` | bool | n      | 上层开关，启用 GPIO 驱动后本扩展参与编译 |
| `MR_USE_DRIVER`      | bool | n      | `MR_USE_DRIVER_GPIO` 的依赖              |

本扩展没有独立的 Kconfig 选项：使能 `MR_USE_DRIVER_GPIO` 后，驱动代码仅在 硬件模板中存在 compat 为 `gpio_hc595` 的实例时被编译（
`MR_HT_HAS_DRIVER()`
门控）。实例属性在硬件模板节点上描述：

| 属性          | 必需 | 说明                                                         |
|---------------|------|--------------------------------------------------------------|
| `ser`         | 是   | 串行数据输入引脚 SER，属性单元 `[phandle, pin]`              |
| `srclk`       | 是   | 移位时钟引脚 SRCLK                                           |
| `rclk`        | 是   | 锁存时钟引脚 RCLK                                            |
| `oe`          | 否   | 输出使能引脚 OE（低有效）                                    |
| `srclr`       | 否   | 寄存器清零引脚 SRCLR（低有效）                               |
| `cascade_num` | 否   | 级联芯片数，缺省 1，允许 1~4                                 |
| `exist_mask`  | 否   | GPIO 存在掩码，缺省 `0xffffffff`；建议按实际级联宽度显式设置 |
| `invert_mask` | 否   | 反相掩码，缺省 `0x00000000`                                  |
| `INIT_LEVEL`  | 否   | 安装级别，缺省 DEFER                                         |

启用后重新生成 `mr_config.h`，包含头文件即可使用：

```c
#include <mrX/drivers/gpio/mr_gpio.h>
```

引脚编号 0 ~ `cascade_num * 8 - 1`，一个实例代表整条级联链的全部输出。

### 3. 快速上手

点亮接在扩展输出 5 上的 LED：

```c
#include <mrX/drivers/gpio/mr_gpio.h>

void hc595_example(void) {
    mr_driver_t *drv = mr_driver_find("gpio_hc595");    /* Name from template */

    if (drv == MR_NULL) {
        return;
    }
    mr_driver_setup(drv);       /* DEFER level: setup by the application */

    mr_gpio_output(drv, 5, MR_GPIO_OTYPE_PP, MR_GPIO_LEVEL_HIGH);
    /* First configure initializes host pins, shifts 0 and enables output */

    mr_gpio_toggle(drv, 5);     /* Toggle the current output once */

    mr_driver_put(drv);         /* Release the reference from find */
}
```

第一次对合法引脚调用 `mr_gpio_output`/`mr_gpio_configure` 时，驱动一次性完成 SER/SRCLK/RCLK（及可选 OE/SRCLR）初始化、移入全
0 并使能输出；之后再次配置合法 OUTPUT 引脚不会重复初始化宿主引脚，而是直接把新的 `level` 应用到对应扩展输出。pin 范围与
mode 校验仍然每次执行。

### 4. 使用说明

**只写端口（影子回读）**：实例面向输出。`mr_gpio_read`/`mr_gpio_port_read`
返回影子寄存器中的最后写入值，不与芯片通信、不反映引脚实际电平（芯片无 输出回读能力）。若硬件模板声明了 `invert_mask`
，读回的是框架层换算后的 逻辑电平。

如果 `exist_mask` 保持默认全 1，而实际级联少于 4 片，那么 shadow 的未使用高位也可能被软件写入和读回，尽管这些位没有对应物理输出；因此模板应显式把
`exist_mask` 限制到实际 `cascade_num * 8` 位宽。

**写入语义**：HC595 backend 更新目标 shadow 位时，如果值没有变化，会跳过整轮移位并直接以 backend 成功返回；公共
`mr_gpio_write` 仍按 GPIO 统一语义返回 1。值变化时才执行整轮移位——先把整链新值逐位移入，最后脉冲 RCLK 锁存输出。
`mr_gpio_port_write` 按掩码整链写入（框架层先按 `exist_mask`/`invert_mask` 换算），backend 不做无变化检测。

**配置行为**：`mr_gpio_configure` 只接受 `MR_GPIO_MODE_OUTPUT` 模式，其他 模式返回 `-MR_EINVAL`；`mr_gpio_configuration`
回读时固定报告输出、推挽、 无上下拉，电平位取自影子寄存器。首次配置的完整序列：OE/SRCLR（若存在）置无效电平（高）→ SER/SRCLK/RCLK
置低 → 移入全 0 → OE（若存在）拉低使能输出。

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

输出操作：

| API                     | 说明                                                |
|-------------------------|-----------------------------------------------------|
| `mr_gpio_output`        | 配置引脚为输出（首次调用同时初始化芯片）            |
| `mr_gpio_configure`     | 仅接受 `MR_GPIO_MODE_OUTPUT`，其余返回 `-MR_EINVAL` |
| `mr_gpio_configuration` | 回读配置，固定 OUTPUT/PP/PULL_NONE                  |
| `mr_gpio_write`         | 更新影子位并按需移位锁存，成功返回 1                |
| `mr_gpio_port_write`    | 整链移位锁存掩码，成功返回 4                        |
| `mr_gpio_read`          | 返回影子寄存器中的引脚位，成功返回 1                |
| `mr_gpio_port_read`     | 返回影子寄存器，成功返回 4                          |

不可用操作：

| API                                              | 行为                                |
|--------------------------------------------------|-------------------------------------|
| `mr_gpio_event_attach`                           | 返回 `-MR_ENOSYS`（事件挂接未实现） |
| `mr_gpio_event_enable` / `mr_gpio_event_disable` | 返回 `-MR_ENOSYS`（事件控制未实现） |

**`mr_gpio_write`**

```c
mr_ssize_t mr_gpio_write(mr_driver_t *drv, mr_uint32_t pin, mr_uint8_t value);
```

- `pin`：逻辑有效范围是 0 ~ `cascade_num * 8 - 1`。公共 `mr_gpio_write` 只断言 `pin < 32U`，HC595 的 `drv_pin_write`
  也不再次做级联范围检查；如果模板仍使用默认 `exist_mask = 0xffffffffU`，实际级联宽度之外的 pin 可能只改变 shadow
  高位而不会产生物理输出。建议让 `exist_mask` 与实际级联宽度一致。
- `value`：逻辑电平，0 与非 0 均可（内部按 `MR_GPIO_LEVEL_*` 语义处理）。
- 返回：成功 1，宿主引脚操作失败返回负数错误码。

**`mr_gpio_port_write`**

```c
mr_ssize_t mr_gpio_port_write(mr_driver_t *drv, mr_uint32_t mask);
```

- `mask`：输出掩码，bit n 对应扩展输出 n。
- 返回：成功 `sizeof(mask)` 即 4，失败负数错误码。

### 6. 完整示例

示例 1：动态扫描 8 位数码管的段码输出（整链一次移位锁存）：

```c
static const mr_uint8_t seg_table[10] = {
    0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f
};

void hc595_seg_example(void) {
    mr_uint32_t mask;
    mr_driver_t *drv = mr_driver_find("gpio_hc595");

    if (drv == MR_NULL) {
        return;
    }
    mr_driver_setup(drv);
    mr_gpio_output(drv, 0, MR_GPIO_OTYPE_PP, MR_GPIO_LEVEL_LOW);

    mask = seg_table[3];                        /* Bit n is segment n */
    mr_gpio_port_write(drv, mask);              /* One shift-latch cycle */
}
```

示例 2：结合反相掩码驱动低有效继电器（`invert_mask` 声明在硬件模板中， 应用写逻辑 1 即闭合）：

```c
void hc595_relay_example(mr_driver_t *drv) {
    mr_gpio_output(drv, 2, MR_GPIO_OTYPE_PP, MR_GPIO_LEVEL_LOW);    /* Off */
    mr_gpio_write(drv, 2, 1);                   /* Logic high closes relay */
    mr_gpio_toggle(drv, 2);                     /* Physical-level flip */
}
```

（本扩展无行为级测试文件，示例按 [GPIO](gpio.md) 的示例风格自拟，仅使用
`mr_gpio.h` 中真实存在的接口。）

### 7. 常见问题（FAQ）

**Q：`mr_gpio_configure` 返回 `-MR_EINVAL`？**

模式不是 `MR_GPIO_MODE_OUTPUT`，或引脚号不小于 `cascade_num * 8`。检查 配置模式与硬件模板中的 `cascade_num`。

**Q：读回来的值和引脚实际电平不一样？**

读接口返回的是影子寄存器（最后写入值），不是引脚电平。外部负载把引脚 拉偏、OE 使能前、或上电初始态都会造成差异；HC595 不支持输出回读。

**Q：上电后输出引脚为什么不动？**

安装级别缺省 DEFER，`mr_driver_setup` 之前驱动未初始化；且输出在首次 配置（移入全 0 并使能 OE）之前保持未定义状态。确保先
setup 再配置。

**Q：写同一值为什么有快有慢？**

HC595 backend 在目标位与 shadow 一致时会跳过整轮移位时序；对上层而言 `mr_gpio_write` 仍是一次成功写入并返回 1。只有值变化时
backend 才真正移位锁存。需要强制刷新整链输出时可用 `mr_gpio_port_write`，其 backend 不做变化检测。

**Q：写一个引脚为什么其他引脚也动了？**

写入时整链重移：影子寄存器中其他引脚的值会一并移入。若应用绕过驱动直接 改了扩展输出（如外部操作 SRCLR 清零），影子寄存器会与实际输出失步，需
用 `mr_gpio_port_write` 全量重写一次。

**Q：`mr_driver_find` 找不到驱动？**

确认硬件模板中存在 compat `gpio_hc595` 的实例、`MR_USE_DRIVER_GPIO` 已 使能并重新生成了 `mr_config.h`；查找名取自模板节点名而非
compat 名。

---

## 开发者指南

### 8. 架构总览

HC595 扩展是 [GPIO 驱动](gpio.md)框架层之下的一个 `ops` 实现，用宿主 GPIO 引脚位拆（bit-bang）出新的"GPIO 端口"：

```
        +------------------------------------------+
        |  应用（mr_gpio_write / mr_gpio_port_write）|
        +-------------------+----------------------+
                            | exist/invert 换算、引脚校验
        +-------------------v----------------------+
        |  GPIO 框架层（drivers/gpio/gpio.c + mr_gpio.h） |
        +-------------------+----------------------+
                            | mr_gpio_ops_t
        +-------------------v----------------------+
        |  HC595 ops（gpio_hc595.c）                 |
        |  移位时序 = mr_ht_gpio_write 序列           |
        |  影子寄存器缓存最后写入值                    |
        +-------------------+----------------------+
                            | mr_ht_gpio_spec_t（宿主引脚）
        +-------------------v----------------------+
        |  平台 GPIO 驱动（片上引脚）                 |
        +------------------------------------------+
             依赖：GPIO 框架层、驱动框架（hardtree 安装）
```

### 9. 数据结构

私有数据（`mrX/drivers/gpio/gpio_hc595.c:38`）：

```c
typedef struct drv_gpio_hc595_priv {
    mr_ht_gpio_spec_t srclr;        /* Optional clear pin (active-low) */
    mr_ht_gpio_spec_t oe;           /* Optional output-enable pin */
    mr_ht_gpio_spec_t ser;          /* Serial data-in pin */
    mr_ht_gpio_spec_t srclk;        /* Shift clock pin */
    mr_ht_gpio_spec_t rclk;         /* Latch clock pin */
    mr_uint8_t cascade_num;         /* Chips in the chain, 1~4 */
    mr_uint32_t shadow;             /* Last written value */
    mr_bool_t configured;           /* Host pins initialized */
} drv_gpio_hc595_priv_t;
```

`mr_ht_gpio_spec_t`（`mr_gpio.h:619`）是 `{drv, pin}` 二元组，由安装宏
`MR_HT_GPIO_SPEC(_id, _prop)` 从硬件模板属性单元 `[phandle, pin]` 生成；
`phandle` 经 `MR_HT_DRIVER_FIND` 解析为宿主 GPIO 驱动指针。

操作表（`gpio_hc595.c:27`）：六个必需回调全部实现，`event_attach` 置
`MR_NULL`。`MR_HT_GPIO_INSTALL`（`mr_gpio.h:610`）据模板属性生成
`mr_gpio_data_t`（`exist_mask` 缺省全 1、`invert_mask` 缺省全 0）并以
`MR_DRIVER_CLAZZES_STATIK(gpio)` 安装实例。

### 10. 核心流程

**写时序到 GPIO 写回调的映射**：核心在 `hc595_cascade_write`
（`gpio_hc595.c:49`），对应 74HC595 的"移入-锁存"两阶段：

1. 移入：对 `cascade_num` 个芯片逐个循环，每芯片 8 位；每位先把目标值 写到 SER（`mr_ht_gpio_write`），再给 SRCLK
   一个高→低脉冲把该位移入 （`gpio_hc595.c:56-76`）。
2. 锁存：全部位移入后给 RCLK 一个高→低脉冲，移位寄存器内容同时出现在 输出引脚（`gpio_hc595.c:79-86`）。
3. 位序映射：`index = chip * 8 + (bit - 1)`，`bit` 从 8 递减——每芯片 内高位（MSB）先移出，掩码的低字节最先移入（
   `gpio_hc595.c:59`）。

**写路径**：`drv_pin_write`（`gpio_hc595.c:179`）先在影子寄存器上改位， 值未变化直接返回 0；变化时以新影子调用
`hc595_cascade_write`，成功后才 提交影子（`gpio_hc595.c:199`），失败时影子保持旧值。`drv_port_write`
（`gpio_hc595.c:212`）不做变化检测，整链写入后更新影子。

**读路径**：`drv_pin_read`（`gpio_hc595.c:203`）与 `drv_port_read`
（`gpio_hc595.c:225`）只读影子寄存器，不产生任何总线动作——HC595 无法 回读输出电平。

**配置路径**：`drv_pin_configure`（`gpio_hc595.c:90`）先断言
`cascade_num` 在 1~4，再校验模式与引脚范围；首次配置时依次初始化可选 OE/SRCLR（输出高，输出禁用/不清零）、SER/SRCLK/RCLK（输出低），随即
`hc595_cascade_write(priv, 0)` 移入全 0（`gpio_hc595.c:140`），最后把 OE 拉低使能输出（`gpio_hc595.c:146-152`
），保证"配置完成即输出已知的 全 0 电平"。`drv_pin_configuration`（`gpio_hc595.c:161`）不回读硬件， 直接报告固定的输出属性。
已配置（`configured` 置位）后再次调用 `drv_pin_configure` 不再重复初始化， 而是经 `drv_pin_write` 把 `config->level`
应用到目标引脚（`gpio_hc595.c:104-106`）。

**安装路径**：`GPIO_HC595_HT_GPIO_INSTALL`（`gpio_hc595.c:234`）为每个 模板实例展开静态私有数据——可选引脚用
`MR_HT_DRIVER_HAS_PROP` 条件填充 （不存在则为 `{0}`，即 `drv == MR_NULL`，配置时跳过），`cascade_num` 用
`MR_HT_DRIVER_PROP_OR` 取缺省 1——再经 `MR_HT_GPIO_INSTALL` 安装；
`MR_HT_DRIVER_FOREACH`（`gpio_hc595.c:246`）对 compat 组所有实例批量执行。

### 11. 并发与中断安全

- ops 层无任何互斥或临界区：一次端口写是多次宿主 GPIO 写组成的完整移位序列，两个执行流并发写会交替翻转
  SER/SRCLK，导致移位数据错乱，并可能让 shadow 与实际输出失步。并发访问同一实例必须由上层序列化。
- shadow 的“改位 → 写整链 → 提交”不是原子操作，并发 `mr_gpio_write` 可能丢失更新；`configured` 也只是普通布尔字段，不承担同步职责。
- 宿主 GPIO 自身即使支持并发访问，也不能自动保证 HC595 这一整段多步 bit-bang 时序的原子性。
- ops 同步运行在调用者上下文，但本驱动没有声明 ISR-safe；完整移位可能持续较长，不应仅因为底层 GPIO API 可调用就假定适合在中断上下文执行。

### 12. 扩展与派生

HC595 不注册自己的驱动 clazz：它借用共享的 GPIO clazz，经 `MR_HT_GPIO_INSTALL` 挂到 GPIO 框架上（
`mrX/drivers/gpio/gpio_hc595.c:234`），因此 **没有 clazz 派生通道**， 只有两条扩展路径：

- **新增同类端口**：自定义一张 `mr_gpio_ops_t`（`mrX/include/mrX/drivers/gpio/mr_gpio.h:84`）， 私有数据里保存上游
  `mr_ht_gpio_spec_t`（`mr_gpio.h:619`）并自行实现时序，再用
  `mr_gpio_init`（`mr_gpio.h:147`）或 `mr_gpio_create`（`mr_gpio.h:168`）挂到 GPIO 框架上，
  `exist_mask`/`invert_mask` 等框架层换算自动生效；运行期以 ops 组装端口的参考写法见
  `mrX/test/drivers/gpio/gpio.c:123-132`。
- **新增模板实例**：在硬件模板中增加 compat 为 `gpio_hc595` 的节点并给出 `cascade_num`、 可选引脚等属性，
  `MR_HT_DRIVER_FOREACH`（`gpio_hc595.c:246`）会逐实例展开安装，无需改代码。 属性为可选，缺失时按 `{0}` 填充并在运行时跳过（
  `gpio_hc595.c:234`）。

派生或包装 HC595 端口时注意：一次端口写是几十次宿主引脚写的序列， **没有互斥保护**
（见第 11 节），上层若需要并发安全必须自行序列化。仓库中不存在 `.hts` 模板文件， 板级硬件描述由使用者自行提供。

### 13. 源码导读

| 位置                                         | 职责                                          |
|----------------------------------------------|-----------------------------------------------|
| `mrX/drivers/gpio/gpio_hc595.c:13`           | `MR_HT_COMPAT` 定义为 `gpio_hc595`            |
| `mrX/drivers/gpio/gpio_hc595.c:27`           | 操作表（`event_attach` 为空）                 |
| `mrX/drivers/gpio/gpio_hc595.c:38`           | 私有数据（宿主引脚规格 + 影子寄存器）         |
| `mrX/drivers/gpio/gpio_hc595.c:49`           | 移位写时序（逐位移入 + 锁存脉冲）             |
| `mrX/drivers/gpio/gpio_hc595.c:90`           | 配置路径（清零输出 + 使能 OE）                |
| `mrX/drivers/gpio/gpio_hc595.c:161`          | 配置回读（固定输出属性）                      |
| `mrX/drivers/gpio/gpio_hc595.c:179`          | 引脚写（影子改位 + 变化检测）                 |
| `mrX/drivers/gpio/gpio_hc595.c:203`          | 引脚读（影子取位）                            |
| `mrX/drivers/gpio/gpio_hc595.c:212`          | 端口写（整链重写）                            |
| `mrX/drivers/gpio/gpio_hc595.c:225`          | 端口读（返回影子）                            |
| `mrX/drivers/gpio/gpio_hc595.c:234`          | 实例安装宏（可选属性条件展开）                |
| `mrX/include/mrX/drivers/gpio/mr_gpio.h:610` | `MR_HT_GPIO_INSTALL`（生成 `mr_gpio_data_t`） |
| `mrX/include/mrX/drivers/gpio/mr_gpio.h:619` | 宿主引脚规格 `mr_ht_gpio_spec_t`              |

### 14. 附录：配置宏与错误码

**配置宏**

| 宏                   | 说明                                                       |
|----------------------|------------------------------------------------------------|
| `MR_USE_DRIVER_GPIO` | 上层开关（`mrX/include/mrX/drivers/gpio/Kconfig`），默认 n |
| `MR_HT_COMPAT`       | 本文件内定义为 `gpio_hc595`，与硬件模板 compat 匹配        |

**错误码**：`-MR_EINVAL` 可来自 GPIO 公共层的 `exist_mask` 检查，也可来自 HC595 backend 的非 OUTPUT
配置或配置回读时的级联范围检查；宿主引脚操作失败时透传平台 GPIO 错误码。`cascade_num` 的 1~4 约束目前只在配置路径以
`MR_ASSERT` 检查，空指针等编程错误同样由断言保护。

# DAC 驱动（DAC）

> DAC 驱动在驱动框架之上提供统一的数模转换抽象：通道参考源配置、多通道序列输出、
> 可选的连续流（stream）输出与半满缓冲事件，并内置电压到原始码的换算工具。

| 项       | 说明                                                                              |
|----------|-----------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_DRIVER_DAC`（默认关闭）；`MR_USE_DRIVER_DAC_STREAM`（默认开启，依赖 DAC） |
| 头文件   | `<mrX/drivers/dac/mr_dac.h>`                                                      |
| 依赖模块 | [驱动框架 Driver](../driver.md)、[等待队列 Wait](../../kernel/wait.md)（流事件）  |
| 参考测试 | `mrX/test/drivers/dac/dac.c`                                                      |

---

## 用户指南

### 1. 模块概述

DAC 驱动解决的问题是： **用同一套 API 配置通道、把目标电压换算为原始码并输出**， 参考源选择与多通道输出细节不再分散在应用里。

特性：

- 通道配置：7 种参考源（内部参考、4 档 VDD 分档、2 路外部参考）。
- 序列输出 `mr_dac_seq_t`：指定通道位掩码、分辨率与源数据缓冲；公共层只约束分辨率 1~32 和 buf/size 一致性，数据布局与通道输出顺序由
  backend 定义。
- 可选 stream（`MR_USE_DRIVER_DAC_STREAM`）：提供 start/stop/is_busy/offset 四个独立可选回调；`source` 选择软件启动或
  HTS 绑定的外部启动源，`circular` 只控制 buffer 读完后是否回绕。
- 电压换算：`mr_dac_millivolt2raw`/`microvolt2raw`/`volt2raw`，目标电压直接换算 为原始码，只需参考电压与分辨率两个参数。
- hardtree 规格宏：在硬件模板中描述"某驱动某通道"，代码用 `mr_ht_dac_spec_t`
  直接配置与输出。

与姊妹模块 [ADC](../adc/adc.md) 同构：结构、流事件与生命周期一致，方向相反——ADC 把 原始码换算为电压，DAC 把电压换算为原始码；DAC
无增益概念，换算只需参考电压。

### 2. 配置与依赖

| 配置项                     | 类型 | 默认值 | 说明                                                                                 |
|----------------------------|------|--------|--------------------------------------------------------------------------------------|
| `MR_USE_DRIVER_DAC`        | bool | n      | 启用 DAC 驱动（`mrX/include/mrX/drivers/dac/Kconfig:6`），依赖 `MR_USE_DRIVER`       |
| `MR_USE_DRIVER_DAC_STREAM` | bool | y      | 启用流输出 API（依赖 `MR_USE_DRIVER_DAC`，`mrX/include/mrX/drivers/dac/Kconfig:21`） |

启用后重新生成 `mr_config.h`，包含头文件即可使用：

```c
#include <mrX/drivers/dac/mr_dac.h>
```

通道号固定为 0~31（对应 `mr_dac_data_t` 中 `exist_mask` 的 32 个位）。流 API 仅在 `MR_USE_DRIVER_DAC_STREAM`
生效；关闭它可裁掉流相关代码，但平台 `ops`
若依赖流回调需同步裁剪。

### 3. 快速上手

向通道 3 输出 1650 mV（12 位分辨率）：

```c
#include <mrX/drivers/dac/mr_dac.h>

void dac_example(mr_driver_t *drv) {
    mr_uint16_t raw;
    mr_dac_config_t cfg = {.ref = MR_DAC_REF_INTERNAL};
    mr_dac_seq_t seq = {
        .channels = 1U << 3,        /* 通道 3 */
        .resolution = 12,
        .buf = &raw,
        .size = sizeof(raw),
    };

    mr_dac_configure(drv, 3, &cfg);
    raw = (mr_uint16_t)mr_dac_millivolt2raw(
        (mr_int32_t)mr_dac_ref_internal(drv), 12, 1650);
    mr_dac_write(drv, &seq);
}
```

`drv` 为 DAC 类型驱动指针，可来自平台安装的驱动或 `mr_driver_find`。

### 4. 使用说明

**配置模型**：通道属性在 `mr_dac_config_t`，当前只有 `ref`（参考源）一个字段。
`mr_dac_configure` 写入，`mr_dac_configuration` 回读；通道必须存在于
`exist_mask`，否则返回 `-MR_EINVAL`。

**序列输出**：`mr_dac_seq_t` 的 `channels` 是通道位掩码（bit n 对应通道 n），
`resolution` 为 1~32 位，`buf`/`size` 描述源数据缓冲（`buf` 为 `MR_NULL` 时
`size` 必须为 0，断言保护）。`mr_dac_write` 返回实际写入的字节数或负数错误码； 采样值的存放格式与输出顺序由平台 `ops->write`
决定。

**stream**：`mr_dac_stream_start(drv, &seq, &stream_cfg)` 会检查 `seq`、1~32 位分辨率、buf/size 一致性以及
`config != MR_NULL`，之后把请求交给可选 `stream_start`。`stream_stop`、`stream_is_busy`、`stream_offset` 分别检查自己的
callback；缺失时返回 `-MR_ENOSYS`。公共层定义 `source` 与 `circular` 的含义，但具体硬件状态机、busy 和 offset 由
backend 实现。

事件位 `MR_DAC_WAIT_STREAM_HALF` / `DONE` / `ERROR` 由公共头文件定义，但 `event_attach` 只挂接 waitable，
`event_enable/disable` 只是把 mask 原样交给可选 `event_control`。树内 stm32h5 平台 backend 实现了全部流回调与
`event_control`。

**内部参考电压**：`mr_dac_ref_internal` 返回驱动模型数据中的内部参考电压 （单位 mV，hardtree 安装时缺省
3300），是换算函数第一个参数的常用来源。

**生命周期**：DAC 驱动对象遵循[驱动框架](../driver.md)的生命周期；setup 时 框架初始化内部事件对象，shutdown 时销毁。
`mr_dac_create` 传入的
`mr_dac_data_t` 必须用 `mr_malloc` 分配——驱动删除时框架会释放它。

### 5. API 参考

初始化与销毁：

| API                                | 说明                                            |
|------------------------------------|-------------------------------------------------|
| `mr_dac_init`                      | 初始化一个静态 DAC 驱动                         |
| `mr_dac_create`                    | 在堆上动态创建 DAC 驱动                         |
| `mr_dac_del`                       | 删除 DAC 驱动                                   |
| `MR_DAC_DEFINE` / `MR_DAC_INSTALL` | 编译期定义 / 安装 DAC 驱动                      |
| `MR_DAC_DATA_INIT`                 | 初始化 `mr_dac_data_t`（内部参考电压/存在掩码） |

配置：

| API                    | 说明         |
|------------------------|--------------|
| `mr_dac_configure`     | 写入通道配置 |
| `mr_dac_configuration` | 回读通道配置 |

输出：

| API                     | 说明                                            |
|-------------------------|-------------------------------------------------|
| `mr_dac_write`          | 执行一次序列输出，返回实际字节数或负数错误码    |
| `mr_dac_stream_start`   | 启动连续流输出（需 `MR_USE_DRIVER_DAC_STREAM`） |
| `mr_dac_stream_stop`    | 停止流输出                                      |
| `mr_dac_stream_is_busy` | 查询流是否忙碌                                  |
| `mr_dac_stream_offset`  | 查询流当前读取偏移                              |

事件与换算：

| API                                                   | 说明                                       |
|-------------------------------------------------------|--------------------------------------------|
| `mr_dac_event_attach`                                 | 订阅 waiter，不启用后端事件源              |
| `mr_dac_event_enable`                                 | 按 `MR_DAC_WAIT_*` mask 启用后端支持的来源 |
| `mr_dac_event_disable`                                | 按 `MR_DAC_WAIT_*` mask 停用事件来源       |
| `mr_dac_event_raise`                                  | 触发事件（平台实现可调用）                 |
| `mr_dac_ref_internal`                                 | 读取内部参考电压（mV）                     |
| `mr_dac_millivolt2raw` / `microvolt2raw` / `volt2raw` | 电压换算为原始码                           |
| `mr_dac_get` / `mr_dac_put`                           | 引用计数加一 / 减一                        |

**`mr_dac_event_attach`**

```c
MR_INLINE mr_err_t mr_dac_event_attach(mr_driver_t *drv, mr_waiter_t *entry);
```

`entry` 先用 `mr_waiter_init` 初始化并设置期望的 `MR_DAC_WAIT_*` 位。此 API 只订阅 waiter， 不会启用来源；挂接事件对象需在
`mr_driver_setup` 成功后进行。

**`mr_dac_event_enable` / `mr_dac_event_disable`**

```c
MR_INLINE mr_err_t mr_dac_event_enable(mr_driver_t *drv, mr_uint32_t events);
MR_INLINE mr_err_t mr_dac_event_disable(mr_driver_t *drv, mr_uint32_t events);
```

`events` 会原样传给 backend，公共层不校验 mask，也不规定 backend 对未知/不支持事件应返回哪一种错误。未实现 `event_control`
时公共 API 返回 `-MR_ENOSYS`；其他返回值直接透传。

**`mr_dac_write`**

```c
mr_ssize_t mr_dac_write(mr_driver_t *drv, mr_dac_seq_t *seq);
```

- `seq`：输出序列描述，`resolution` 必须在 1~32，`buf` 为 `MR_NULL` 时
  `size` 必须为 0（断言保护）。
- 返回：成功返回实际写入字节数，失败负数错误码。

**`mr_dac_stream_start` / `mr_dac_stream_stop`**

```c
mr_err_t mr_dac_stream_start(mr_driver_t *drv, mr_dac_seq_t *seq,
                             const mr_dac_stream_config_t *config);
mr_err_t mr_dac_stream_stop(mr_driver_t *drv);
```

**`mr_dac_stream_config_t`**

```c
typedef enum mr_dac_stream_source {
    MR_DAC_STREAM_SOURCE_SOFTWARE,
    MR_DAC_STREAM_SOURCE_EXTERNAL,
} mr_dac_stream_source_t;

typedef struct mr_dac_stream_config {
    mr_uint8_t source: 1;
    mr_uint8_t circular: 1;
} mr_dac_stream_config_t;
```

仅在 `MR_USE_DRIVER_DAC_STREAM` 下声明：

- `MR_DAC_STREAM_SOURCE_SOFTWARE`：`stream_start` 通过软件启动输出，DAC/backend 随后自行推进序列；
- `MR_DAC_STREAM_SOURCE_EXTERNAL`：`stream_start` 只武装 DAC，每次更新由 HTS 绑定的外部信号启动；
- `circular`：buffer 读完后是否回绕，与转换启动来源相互独立。

外部启动信号的来源、路由和边沿由 HTS/backend 固定，stream 配置不重复描述这些硬件信息。DAC stream 不会配置或启停外部
TMR/PWM；输出周期也由外部生产者决定。

`stream_start`/`stream_stop`/`stream_is_busy`/`stream_offset` 是彼此独立的可选能力：对应 callback 为空时各自返回
`-MR_ENOSYS`，否则返回 backend 结果。公共层不额外建立 stream 状态机。HardTree 包装接口只是把调用转发到同一组 API。

**`mr_dac_millivolt2raw`**

```c
mr_int64_t mr_dac_millivolt2raw(mr_int32_t ref_mv, mr_uint8_t resolution,
                                mr_int64_t millivolt);
```

按 `millivolt * max_raw / ref_mv` 换算（`max_raw = (1 << resolution) - 1`， 在 `mr_int64_t` 域计算）。例：3300 mV、12 位、1650
mV → 2047。`microvolt2raw`
以微伏为输入，`volt2raw` 接受 `mr_f32_t` 伏值。

**hardtree 规格**

```c
mr_ht_dac_spec_t spec = MR_HT_DAC_SPEC(instance_id, prop_name);
mr_ht_dac_configure(&spec);
mr_ssize_t ret = mr_ht_dac_write(&spec, &seq);
mr_int64_t raw = mr_ht_dac_millivolt2raw(&spec, millivolt);
```

`mr_ht_dac_spec_t` 携带驱动指针、通道、分辨率、参考电压与配置，由宏从属性 单元
`[phandle, channel, resolution, ref_voltage, ref]` 生成（后三项可缺省： 分辨率 8、参考电压 0 表示用内部参考、内部参考）；配套
`mr_ht_dac_configure/configuration` 写入/回读通道配置；流版本
`mr_ht_dac_stream_start`
等需 `MR_USE_DRIVER_DAC_STREAM`；事件来源控制可用 `mr_ht_dac_event_enable/disable(&spec, events)`。

### 6. 完整示例

DAC + TMR 等间隔波形输出、external stream 的启停顺序与 STM32H5 路由前提见
[驱动组合与进阶示例](../examples.md)。

示例 1：动态创建 DAC 驱动并完成配置/输出/换算（改编自
`mrX/test/drivers/dac/dac.c` 的 `configuration_and_reference` 与
`write_and_stream`）：

```c
void dac_dynamic_example(void) {
    mr_dac_data_t *data = mr_malloc(sizeof(*data));
    mr_uint16_t raw;
    mr_dac_seq_t seq = {.channels = 1U << 0, .resolution = 12,
                        .buf = &raw, .size = sizeof(raw)};
    mr_driver_t *drv;

    if (data == MR_NULL) {
        return;
    }
    data->ref_internal = 3300;          /* 3.3 V 参考 */
    data->exist_mask = 0x0000000fU;     /* 通道 0-3 */
    drv = mr_dac_create(data, MR_NULL, (mr_dac_ops_t *)&board_dac_ops);
    if (drv == MR_NULL) {
        return;
    }
    mr_driver_setup(drv);

    raw = (mr_uint16_t)mr_dac_millivolt2raw(3300, 12, 1650);
    mr_dac_write(drv, &seq);
    mr_dac_del(drv);                    /* 删除时释放 data */
}
```

示例 2：waitable 事件挂接与 raise：

```c
static void dac_stream_waiter(mr_waiter_t *waiter, void *param,
                              mr_wait_event_t event, const void *data) {
    int *count = param;
    (*count)++;     /* 事件由 mr_dac_event_raise 同步派发。 */
}

void dac_event_example(mr_driver_t *drv) {
    static mr_waiter_t waiter;
    static int event_count;

    mr_waiter_init(&waiter, dac_stream_waiter, &event_count,
                   MR_DAC_WAIT_STREAM_HALF);
    mr_dac_event_attach(drv, &waiter);
    mr_dac_event_raise(drv, MR_DAC_WAIT_STREAM_HALF);
}
```

通道存在性、write、换算、流回调与事件路径的测试见 `mrX/test/drivers/dac/dac.c`。

### 7. 常见问题（FAQ）

**Q：`mr_dac_configure` 返回 `-MR_EINVAL`？**

通道号大于等于 32 或不在 `exist_mask` 中。检查硬件模板的 `exist_mask`
属性与通道号。

**Q：`mr_dac_stream_stop` 返回 `-MR_ENOSYS`？**

平台 `ops` 未实现对应流回调；四个流回调彼此独立，任一未实现时对应 API 返回 `-MR_ENOSYS`。

**Q：收不到 half/full 事件？**

`mr_dac_event_attach` 只挂 waitable；若 backend 实现 `event_control`，才可用 enable/disable 控制来源。backend 未实现
`event_control` 时，`mr_dac_event_enable` 会返回 `-MR_ENOSYS`。backend 也必须主动调用 `mr_dac_event_raise`，waiter 才会收到事件。

**Q：换算出的 raw 值不对？**

确认换算用的参考电压与实际一致：`mr_dac_ref_internal` 返回的是模型数据里的 标称值；使用外部参考时应在
`mr_ht_dac_spec_t.ref_voltage` 或换算参数中传入 实际电压。换算结果向下取整（1650 mV、12 位、3300 mV 参考得 2047 而非 2048），
且 `resolution` 参数必须与 `seq.resolution` 一致。

**Q：`buf` 与 `size` 怎么定？**

由平台 `ops->write` 决定布局；常见为每个采样值 `resolution` 对齐后的整数倍 （如 12 位常存 `mr_uint16_t`
）。以平台驱动文档/实现为准，框架只做非空 一致性断言。

**Q：静态和动态方式怎么选？**

板上固定外设用 `MR_DAC_DEFINE` / `MR_DAC_INSTALL`（注册 + 自动启动）；运行期 才确定存在的设备用 `mr_dac_create` /
`mr_dac_del`。资源紧张的场合优先静态， 不依赖堆。

---

## 开发者指南

### 8. 架构总览

DAC 驱动与 [ADC](../adc/adc.md) 同构，是[驱动框架](../driver.md)的派生模块，额外引入 等待队列与换算工具：

```
        +------------------------------------------+
        |  应用（mr_dac_write / millivolt2raw）      |
        +-------------------+----------------------+
                            | 通道校验 / seq 断言
        +-------------------v----------------------+
        |  DAC 框架层（dac.c + mr_dac.h）            |
        |  exist_mask 校验、流 API 门控、             |
        |  waitable 事件、电压换算                    |
        +-------------------+----------------------+
                            | mr_dac_ops_t
        +-------------------v----------------------+
        |  平台实现：如 stm32h5 片上 DAC                |
        +------------------------------------------+
             依赖：驱动框架、等待队列（流事件）
```

### 9. 数据结构

模型数据（`mrX/include/mrX/drivers/dac/mr_dac.h:66`）：

```c
typedef struct mr_dac_data {
    mr_uint16_t ref_internal;   /* Internal reference in millivolts */
    mr_uint32_t exist_mask;     /* Channels that physically exist */
    mr_waitable_t event;        /* Stream half/full waitable */
} mr_dac_data_t;
```

配置与序列（`mr_dac.h:43`、`mr_dac.h:48`）：

| 结构              | 字段                                       | 说明                                             |
|-------------------|--------------------------------------------|--------------------------------------------------|
| `mr_dac_config_t` | `ref:3`                                    | 参考源枚举 `mr_dac_ref_t`（7 种，`mr_dac.h:33`） |
| `mr_dac_seq_t`    | `channels` / `resolution` / `buf` / `size` | 通道位掩码、1~32 位分辨率、const 源数据缓冲      |

操作表（`mr_dac.h:73`）：`channel_configure`、`channel_configuration`、
`write` 必需（框架断言非空）；`stream_start`/`stream_stop`/`stream_is_busy`/
`stream_offset` 仅在 `MR_USE_DRIVER_DAC_STREAM` 下声明，全部为可选（框架对空回调有缺省返回）。`event_control` 后端回调可选；
`event_attach` 是框架订阅 waitable 的公共函数，并不要求后端回调。具体后端实现哪些事件源由平台决定；ops
同步运行在调用者上下文，公共层不额外切换上下文。

hardtree 规格（`mr_dac.h:559`）：`mr_ht_dac_spec_t { drv, channel, resolution,
ref_voltage, config }`，属性单元为 `[phandle, channel, resolution,
ref_voltage, ref]`，`ref_voltage` 为 0 时换算函数回退到 `mr_dac_ref_internal`。

### 10. 核心流程

**clazz**：DAC 定义了完整的 init/deinit（`MR_DRIVER_CLAZZES_DEFINE(dac,
dac_init, dac_deinit, dac_release)`，`mrX/drivers/dac/dac.c:17`）。
`dac_init`（`dac.c:69`）在 setup 阶段调用 `mr_waitable_init` 初始化流事件；
`dac_deinit` 在 shutdown 阶段先调用已实现的 `stream_stop`，成功后才通过
`mr_waitable_del` 销毁事件；停止失败时返回错误并保留事件对象，以便重试 shutdown。 删除路径（驱动已标记 `DYING`
）即使停止失败也会销毁事件，避免 `release` 释放 `data` 时泄漏 waitable。 未启用流功能或没有停止回调时直接销毁事件。公共层不规定
`stream_stop` 的幂等性或成功后的 buffer/hardware 状态，这些由 backend 定义。`dac_release` 释放动态对象的 `data`。

**配置路径**：`mr_dac_configure`（`dac.c:19`）校验通道存在性（`channel >= 32`
或 `exist_mask` 不含该位返回 `-MR_EINVAL`，`dac.c:36`）后透传
`ops->channel_configure`；回读路径 `mr_dac_configuration`（`dac.c:44`）对称。

**输出路径**：`mr_dac_write`（`mr_dac.h:214`）仅做断言校验（驱动类型、
`resolution` 1~32、buf/size 一致性）后透传 `ops->write`，返回值原样透传， 框架不拷贝数据。

**流路径**：`mr_dac_stream_start`（`mr_dac.h:241`）对未实现的 `stream_start`
返回 `-MR_ENOSYS`，否则透传 `ops->stream_start(drv, seq, config)`；
`mr_dac_stream_is_busy(drv, &busy)` 成功返回 0，通过 `busy` 输出状态； 未实现时返回 `-MR_ENOSYS`，后端错误原样返回，失败时不能使用
`busy`；
`mr_dac_stream_stop`/`mr_dac_stream_offset`
（`mr_dac.h:298`、`mr_dac.h:324`）对未实现的能力返回 `-MR_ENOSYS`。

**事件路径**：`mr_dac_event_attach` 只订阅 waiter，不打开来源；`mr_dac_event_enable`/`disable` 通过可选
`event_control` 后端回调切换来源。平台后端可在支持的事件发生时调用 `mr_dac_event_raise`，通过 waitable 通知 waiter；
公共接口不保证具体平台实现或硬件中断源。

**换算实现**：三个换算函数均为 `MR_INLINE` 纯函数（`mr_dac.h:468`、
`mr_dac.h:490`、`mr_dac.h:512`）：`millivolt2raw` 在 `mr_int64_t` 域计算
`millivolt * ((1 << resolution) - 1) / ref_mv`，避免 32 位溢出，整数除法 向下取整；`microvolt2raw` 把分母放大 1000 倍复用同一公式；
`volt2raw` 先把 伏值乘 1000 转毫伏再走 `millivolt2raw`。

### 11. 并发与中断安全

- 框架层 API 无内部互斥；`mr_dac_write` 与流启动对同一 DAC 外设的并发由平台
  `ops` 或应用保证。
- 事件通知遵循[等待队列](../../kernel/wait.md)的回调上下文约定：waiter 回调在后端调用 raise 的上下文执行； 若后端从中断调用
  raise，回调中禁止阻塞。
- `mr_dac_data_t.event` 在 setup/shutdown 之间有效；shutdown 后仍挂着的 等待者由等待队列的删除语义处理，应用应在 shutdown
  前摘除等待者。
- 换算函数为纯函数（无共享状态），任意上下文可用。

### 12. 扩展与派生

- **平台移植**：DAC backend 的操作表实现、stream/DMA/外部启动接线、HardTree 安装与验收见 [DAC 驱动移植指南](porting.md)。

### 13. 源码导读

| 位置                                       | 职责                                |
|--------------------------------------------|-------------------------------------|
| `mrX/include/mrX/drivers/dac/mr_dac.h:28`  | 流事件位（HALF/DONE/ERROR）         |
| `mrX/include/mrX/drivers/dac/mr_dac.h:43`  | 通道配置与输出序列结构              |
| `mrX/include/mrX/drivers/dac/mr_dac.h:66`  | 模型数据（参考电压/掩码/事件）      |
| `mrX/include/mrX/drivers/dac/mr_dac.h:73`  | 操作表（流回调条件编译）            |
| `mrX/include/mrX/drivers/dac/mr_dac.h:214` | 序列输出入口（断言 + 透传）         |
| `mrX/include/mrX/drivers/dac/mr_dac.h:241` | 流 API（start/is_busy/stop/offset） |
| `mrX/include/mrX/drivers/dac/mr_dac.h:404` | 事件挂接与触发                      |
| `mrX/include/mrX/drivers/dac/mr_dac.h:468` | 电压到原始码换算                    |
| `mrX/include/mrX/drivers/dac/mr_dac.h:550` | hardtree 实例安装宏                 |
| `mrX/drivers/dac/dac.c:17`                 | DAC clazz（init/deinit/release）    |
| `mrX/drivers/dac/dac.c:19`                 | 通道配置路径（存在性校验）          |
| `mrX/drivers/dac/dac.c:69`                 | setup：初始化流事件                 |
| `mrX/test/drivers/dac/dac.c`               | 行为级测试                          |

### 14. 附录：配置宏与错误码

**配置宏**

| 宏                                                                                 | 说明                                   |
|------------------------------------------------------------------------------------|----------------------------------------|
| `MR_USE_DRIVER_DAC`                                                                | 模块开关，默认 n，依赖 `MR_USE_DRIVER` |
| `MR_USE_DRIVER_DAC_STREAM`                                                         | 流输出开关，默认 y，依赖 DAC           |
| `MR_DAC_WAIT_STREAM_HALF` / `MR_DAC_WAIT_STREAM_DONE` / `MR_DAC_WAIT_STREAM_ERROR` | 流缓冲半满/完成与错误事件位            |

**错误码**：公共 DAC 层明确返回 `-MR_EINVAL` 表示 configure/configuration 的通道不存在，返回 `-MR_ENOSYS` 表示可选 stream
callback 或 `event_control` 未实现；其他 backend 错误码原样透传。空指针、分辨率越界、buf/size 不一致等编程错误在断言开启时由
`MR_ASSERT` 拦截。

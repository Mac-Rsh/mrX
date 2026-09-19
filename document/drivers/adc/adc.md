# ADC 驱动（ADC）

> ADC 驱动在驱动框架之上提供统一的模数转换抽象：通道配置、多通道序列采样、
> 可选的连续流（stream）采样与半满缓冲事件，并内置原始码到电压的换算工具。

| 项       | 说明                                                                              |
|----------|-----------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_DRIVER_ADC`（默认关闭）；`MR_USE_DRIVER_ADC_STREAM`（默认开启，依赖 ADC） |
| 头文件   | `<mrX/drivers/adc/mr_adc.h>`                                                      |
| 依赖模块 | [驱动框架 Driver](../driver.md)、[等待队列 Wait](../../kernel/wait.md)（流事件）        |
| 参考测试 | `mrX/test/drivers/adc/adc.c`                                                      |

---

## 用户指南

### 1. 模块概述

ADC 驱动解决的问题是： **用同一套 API 配置通道、启动采样并拿到统一的原始 码**，电压换算与多通道调度细节不再分散在应用里。

特性：

- 通道配置：单端/差分、7 种参考源、可编程增益（1/6x ~ 128x，17 档）。
- 序列采样 `mr_adc_seq_t`：指定通道位掩码、分辨率、`oversample` 字段与缓冲区；公共层只约束分辨率 1~32 和 buf/size 一致性，通道顺序、缓冲布局和 `oversample` 含义由 backend 定义。
- 可选 stream（`MR_USE_DRIVER_ADC_STREAM`）：提供 start/stop/is_busy/offset 四个独立可选回调，配置结构含 `sample_rate` 与 `circular`；具体连续采样、循环和 offset 语义由 backend 定义。
- 电压换算：`mr_adc_raw2millivolt`/`raw2microvolt`/`raw2volt`，自动计入增益 与分辨率。
- hardtree 规格宏：在硬件模板中描述"某驱动某通道"，代码用 `mr_ht_adc_spec_t`
  直接配置与采样。

### 2. 配置与依赖

| 配置项                     | 类型 | 默认值 | 说明                                                   |
|----------------------------|------|--------|--------------------------------------------------------|
| `MR_USE_DRIVER_ADC`        | bool | n      | 启用 ADC 驱动（`mrX/include/mrX/drivers/adc/Kconfig`） |
| `MR_USE_DRIVER_ADC_STREAM` | bool | y      | 启用流采样 API（依赖 `MR_USE_DRIVER_ADC`）             |

启用后重新生成 `mr_config.h`，包含头文件即可使用：

```c
#include <mrX/drivers/adc/mr_adc.h>
```

通道号固定为 0~31（对应 `mr_adc_data_t` 中 `exist_mask` 的 32 个位）。流 API 仅在 `MR_USE_DRIVER_ADC_STREAM`
生效；关闭它可裁掉流相关代码，但平台 `ops`
若依赖流回调需同步裁剪。

### 3. 快速上手

单次读取通道 3，换算为毫伏：

```c
#include <mrX/drivers/adc/mr_adc.h>

void adc_example(mr_driver_t *drv) {
    mr_uint16_t raw;
    mr_adc_config_t cfg = {.diff = MR_ADC_DIFF_SINGLE,
                           .ref = MR_ADC_REF_INTERNAL,
                           .gain = MR_ADC_GAIN_1};
    mr_adc_seq_t seq = {
        .channels = 1U << 3,        /* Channel 3 */
        .resolution = 12,
        .oversample = 0,
        .buf = &raw,
        .size = sizeof(raw),
    };

    mr_adc_configure(drv, 3, &cfg);
    if (mr_adc_read(drv, &seq) == sizeof(raw)) {
        mr_int64_t mv = mr_adc_raw2millivolt(
            (mr_int32_t)mr_adc_ref_internal(drv), MR_ADC_GAIN_1, 12, raw);
        /* Use mv here */
    }
}
```

`drv` 为 ADC 类型驱动指针，可来自平台安装的驱动或 `mr_driver_find`。

### 4. 使用说明

**配置模型**：通道属性在 `mr_adc_config_t`：`diff`（单端/差分）、`ref`
（参考源）、`gain`（增益）。`mr_adc_configure` 写入，`mr_adc_configuration`
回读；通道必须存在于 `exist_mask`，否则返回 `-MR_EINVAL`。

**序列采样**：`mr_adc_seq_t` 的 `channels` 是通道位掩码（bit n 对应通道 n），
`resolution` 为 1~32 位，`buf`/`size` 描述采样缓冲（`buf` 为 `MR_NULL` 时
`size` 必须为 0，断言保护）。`mr_adc_read` 返回实际读取的字节数或负数错误 码；具体采样顺序与缓冲布局由平台 `ops->read` 决定。

**stream**：`mr_adc_stream_start(drv, &seq, &stream_cfg)` 会检查 `seq`、1~32 位分辨率、buf/size 一致性以及 `config != MR_NULL`，之后把请求交给可选 `stream_start`。`stream_stop`、`stream_is_busy`、`stream_offset` 也分别检查自己的 callback；缺失时返回 `-MR_ENOSYS`。公共层不会解释 `sample_rate`、`circular`、busy 或 offset 的具体状态机。

事件位 `MR_ADC_WAIT_STREAM_HALF` / `FULL` / `ERROR` 由公共头文件定义，但 `event_attach` 只挂接 waitable，`event_enable/disable` 只是把 mask 原样交给可选 `event_control`。是否实现由平台决定；树内 stm32h5 平台 backend 实现了全部流回调与 `event_control`。

**内部参考电压**：`mr_adc_ref_internal` 返回驱动模型数据中的内部参考电压 （单位 mV，hardtree 安装时缺省
3300），是换算函数第一个参数的常用来源。

**生命周期**：ADC 驱动对象遵循[驱动框架](../driver.md)的生命周期；setup 时 框架初始化内部事件对象，shutdown 时销毁。
`mr_adc_create` 传入的
`mr_adc_data_t` 必须用 `mr_malloc` 分配——驱动删除时框架会释放它。

### 5. API 参考

初始化与销毁：

| API                                | 说明                                            |
|------------------------------------|-------------------------------------------------|
| `mr_adc_init`                      | 初始化一个静态 ADC 驱动                         |
| `mr_adc_create`                    | 在堆上动态创建 ADC 驱动                         |
| `mr_adc_del`                       | 删除 ADC 驱动                                   |
| `MR_ADC_DEFINE` / `MR_ADC_INSTALL` | 编译期定义 / 安装 ADC 驱动                      |
| `MR_ADC_DATA_INIT`                 | 初始化 `mr_adc_data_t`（内部参考电压/存在掩码） |

配置：

| API                    | 说明         |
|------------------------|--------------|
| `mr_adc_configure`     | 写入通道配置 |
| `mr_adc_configuration` | 回读通道配置 |

采样：

| API                     | 说明                                            |
|-------------------------|-------------------------------------------------|
| `mr_adc_read`           | 执行一次序列采样，返回实际字节数或负数错误码    |
| `mr_adc_stream_start`   | 启动连续流采样（需 `MR_USE_DRIVER_ADC_STREAM`） |
| `mr_adc_stream_stop`    | 停止流采样                                      |
| `mr_adc_stream_is_busy` | 查询流是否忙碌                                  |
| `mr_adc_stream_offset`  | 查询流当前写入偏移                              |

事件与换算：

| API                                                   | 说明                                    |
|-------------------------------------------------------|-----------------------------------------|
| `mr_adc_event_attach`                                 | 订阅 waiter，不启用后端事件源             |
| `mr_adc_event_enable`                                 | 按 `MR_ADC_WAIT_*` mask 启用后端支持的来源 |
| `mr_adc_event_disable`                                | 按 `MR_ADC_WAIT_*` mask 停用事件来源       |
| `mr_adc_event_raise`                                  | 触发事件（平台实现可调用）                |

| `mr_adc_ref_internal`                                 | 读取内部参考电压（mV）                  |
| `mr_adc_raw2millivolt` / `raw2microvolt` / `raw2volt` | 原始码换算为电压                        |
| `mr_adc_get` / `mr_adc_put`                           | 引用计数加一 / 减一                     |

**`mr_adc_event_attach`**

```c
MR_INLINE mr_err_t mr_adc_event_attach(mr_driver_t *drv, mr_waiter_t *entry);
```

`entry` 先用 `mr_waiter_init` 初始化并设置期望的 `MR_ADC_WAIT_*` 位。此 API 只订阅 waiter，
不会启用来源；挂接事件对象需在 `mr_driver_setup` 成功后进行。

**`mr_adc_event_enable` / `mr_adc_event_disable`**

```c
MR_INLINE mr_err_t mr_adc_event_enable(mr_driver_t *drv, mr_uint32_t events);
MR_INLINE mr_err_t mr_adc_event_disable(mr_driver_t *drv, mr_uint32_t events);
```

`events` 会原样传给 backend，公共层不校验 mask，也不规定 backend 对未知/不支持事件应返回哪一种错误。未实现 `event_control` 时公共 API 返回 `-MR_ENOSYS`；其他返回值直接透传。

**`mr_adc_stream_config_t`**

```c
typedef struct mr_adc_stream_config {
    mr_uint64_t sample_rate;    /* Requested sample rate in Hz */
    mr_bool_t circular;         /* MR_TRUE: wrap around when buffer full */
} mr_adc_stream_config_t;
```

仅在 `MR_USE_DRIVER_ADC_STREAM` 下声明。字段名表达采样率与循环请求，但公共层只负责透传，是否支持以及具体含义由 backend 决定。

**`mr_adc_read`**

```c
mr_ssize_t mr_adc_read(mr_driver_t *drv, mr_adc_seq_t *seq);
```

- `seq`：采样序列描述，`resolution` 必须在 1~32，`buf` 为 `MR_NULL` 时
  `size` 必须为 0（断言保护）。
- 返回：成功返回实际读取字节数，失败负数错误码。

**`mr_adc_stream_start` / `mr_adc_stream_stop`**

```c
mr_err_t mr_adc_stream_start(mr_driver_t *drv, mr_adc_seq_t *seq,
                             const mr_adc_stream_config_t *config);
mr_err_t mr_adc_stream_stop(mr_driver_t *drv);
```

`stream_start`/`stream_stop`/`stream_is_busy`/`stream_offset` 是彼此独立的可选能力：对应 callback 为空时各自返回 `-MR_ENOSYS`，否则返回 backend 结果。公共层不额外建立 stream 状态机。HardTree 包装接口只是把调用转发到同一组 API。

**`mr_adc_raw2millivolt`**

```c
mr_int64_t mr_adc_raw2millivolt(mr_int32_t ref_mv, mr_adc_gain_t gain,
                                mr_uint8_t resolution, mr_int64_t raw);
```

按 `ref_mv * raw * div / (mul << resolution)` 换算（`mul`/`div` 来自增益 表）。例：3300 mV、增益 1、12 位、码值 2048 → 1650 mV。
`raw2microvolt` 在 此基础上放大 1000 倍，`raw2volt` 返回 `mr_f32_t` 伏值。

**hardtree 规格**

```c
mr_ht_adc_spec_t spec = MR_HT_ADC_SPEC(instance_id, channels);
mr_ht_adc_configure(&spec);
mr_ssize_t ret = mr_ht_adc_read(&spec, &seq);
mr_int64_t mv = mr_ht_adc_raw2millivolt(&spec, raw);
```

`mr_ht_adc_spec_t` 携带驱动指针、通道、分辨率、参考电压与配置，由宏从属性 单元
`[phandle, channel, resolution, ref_voltage, diff, ref, gain]` 生成 （后五项可缺省：分辨率 8、参考电压 0 表示用内部参考、单端、内部参考、增益
1）；配套 `mr_ht_adc_configure/configuration` 写入/回读通道配置；流版本 `mr_ht_adc_stream_start` 等需 `MR_USE_DRIVER_ADC_STREAM`；事件来源控制可用 `mr_ht_adc_event_enable/disable(&spec, events)`。

### 6. 完整示例

示例 1：动态创建 ADC 驱动并完成配置/采样/换算（改编自
`mrX/test/drivers/adc/adc.c` 的 `read_and_conversion`）：

```c
void adc_dynamic_example(void) {
    mr_adc_data_t *data = mr_malloc(sizeof(*data));
    mr_uint16_t raw;
    mr_adc_seq_t seq = {.channels = 1U << 2, .resolution = 12,
                        .oversample = 2, .buf = &raw, .size = sizeof(raw)};
    mr_driver_t *drv;

    if (data == MR_NULL) {
        return;
    }
    data->ref_internal = 1200;          /* 1.2 V bandgap */
    data->exist_mask = 0x0000000fU;     /* Channels 0-3 */
    drv = mr_adc_create(data, MR_NULL, (mr_adc_ops_t *)&board_adc_ops);
    if (drv == MR_NULL) {
        return;
    }
    mr_driver_setup(drv);

    if (mr_adc_read(drv, &seq) == sizeof(raw)) {
        mr_int64_t mv = mr_adc_raw2millivolt(
            1200, MR_ADC_GAIN_1_2, 12, raw);
        (void)mv;
    }
    mr_adc_del(drv);                    /* Frees data */
}
```

示例 2：waitable 事件挂接与 raise（与当前测试 `event_paths` 一致）：

```c
static void adc_stream_waiter(mr_waiter_t *waiter, void *param,
                              mr_wait_event_t event, const void *data) {
    int *count = param;
    (*count)++;     /* Event is dispatched synchronously by mr_adc_event_raise. */
}

void adc_event_example(mr_driver_t *drv) {
    static mr_waiter_t waiter;
    static int event_count;

    mr_waiter_init(&waiter, adc_stream_waiter, &event_count,
                   MR_ADC_WAIT_STREAM_HALF);
    mr_adc_event_attach(drv, &waiter);
    mr_adc_event_raise(drv, MR_ADC_WAIT_STREAM_HALF);
}
```

通道存在性、读路径、换算与可选 stream callback 的测试见 `mrX/test/drivers/adc/adc.c`。

### 7. 常见问题（FAQ）

**Q：`mr_adc_configure` 返回 `-MR_EINVAL`？**

通道号大于等于 32 或不在 `exist_mask` 中。检查硬件模板的 `exist_mask`
属性与通道号。

**Q：`mr_adc_stream_stop`/`mr_adc_stream_offset` 返回 `-MR_ENOSYS`？**

平台 `ops` 未实现对应回调。`stream_start`/`stream_stop`/`stream_offset`
未实现时返回 `-MR_ENOSYS`；`stream_is_busy` 未实现时也返回 `-MR_ENOSYS`。

**Q：收不到 half/full 事件？**

先区分 attach 与硬件事件源控制：`mr_adc_event_attach` 只挂 waitable；若 backend 实现 `event_control`，才可用 enable/disable 控制来源。backend 未实现 `event_control` 时（如自定义轮询后端），`mr_adc_event_enable` 会返回 `-MR_ENOSYS`。backend 也必须主动调用 `mr_adc_event_raise`，waiter 才会收到事件。

**Q：换算结果和万用表对不上？**

确认换算用的参考电压与实际一致：`mr_adc_ref_internal` 返回的是模型数据里的 标称值；使用外部参考时应在
`mr_ht_adc_spec_t.ref_voltage` 或换算参数中传入 实际电压。同时确认增益与分辨率参数和通道配置一致。

**Q：`buf` 与 `size` 怎么定？**

由平台 `ops->read` 决定布局；常见为每个采样值 `resolution` 对齐后的整数倍 （如 12 位常存 `mr_uint16_t`
）。以平台驱动文档/实现为准，框架只做非空 一致性断言。

---

## 开发者指南

### 8. 架构总览

ADC 驱动与 GPIO 同构，是[驱动框架](../driver.md)的派生模块，额外引入等待队列 与换算工具：

```
        +------------------------------------------+
        |  应用（mr_adc_read / raw2millivolt）       |
        +-------------------+----------------------+
                            | 通道校验 / seq 断言
        +-------------------v----------------------+
        |  ADC 框架层（drivers/adc/adc.c + mr_adc.h）  |
        |  exist_mask 校验、流 API 门控、             |
        |  waitable 事件、电压换算                    |
        +-------------------+----------------------+
                            | mr_adc_ops_t
        +-------------------v----------------------+
        |  平台实现：如 stm32h5 片上 ADC                |
        +------------------------------------------+
             依赖：驱动框架、等待队列（流事件）
```

### 9. 数据结构

模型数据（`mrX/include/mrX/drivers/adc/mr_adc.h:94`）：

```c
typedef struct mr_adc_data {
    mr_uint16_t ref_internal;   /* Internal reference in millivolts */
    mr_uint32_t exist_mask;     /* Channels that physically exist */
    mr_waitable_t event;        /* Stream half/full waitable */
} mr_adc_data_t;
```

配置与序列（`mr_adc.h:70`、`mr_adc.h:77`）：

| 结构              | 字段                                                      | 说明                                      |
|-------------------|-----------------------------------------------------------|-------------------------------------------|
| `mr_adc_config_t` | `diff:1` / `ref:3` / `gain`                               | 单端/差分、参考源（7 种）、增益枚举       |
| `mr_adc_seq_t`    | `channels` / `resolution` / `oversample` / `buf` / `size` | 通道位掩码、1~32 位分辨率、过采样率、缓冲 |

操作表（`mr_adc.h:102`）：`channel_configure`、`channel_configuration`、
`read` 必需；`stream_start`/`stream_stop`/`stream_is_busy`/`stream_offset`
仅在 `MR_USE_DRIVER_ADC_STREAM` 下声明，全部为可选（框架对空回调有缺省返回）。`event_control` 后端回调可选；
`event_attach` 是框架订阅 waitable 的公共函数，并不要求后端回调。具体后端实现哪些事件源由平台决定；ops 同步运行在调用者上下文，公共层不额外切换上下文。

hardtree 规格（`mr_adc.h:610`）：`mr_ht_adc_spec_t { drv, channel,
resolution, ref_voltage, config }`，属性单元为
`[phandle, channel, resolution, ref_voltage, diff, ref, gain]`，
`ref_voltage` 为 0 时换算函数回退到 `mr_adc_ref_internal`。

### 10. 核心流程

**clazz**：ADC 定义了完整的 init/deinit（`MR_DRIVER_CLAZZES_DEFINE(adc,
adc_init, adc_deinit, adc_release)`，`mrX/drivers/adc/adc.c:17`）。
`adc_init`（`adc.c:69`）在 setup 阶段调用 `mr_waitable_init` 初始化流事件；
`adc_deinit` 在 shutdown 阶段先调用已实现的 `stream_stop`，成功后才通过
`mr_waitable_del` 销毁事件；停止失败时返回错误并保留事件对象，以便重试 shutdown。
删除路径（驱动已标记 `DYING`）即使停止失败也会销毁事件，避免 `release` 释放 `data` 时泄漏 waitable。
未启用流功能或没有停止回调时直接销毁事件。公共层不规定 `stream_stop` 的幂等性或成功后的 buffer/hardware 状态，这些由 backend 定义。`adc_release` 释放动态对象的 `data`。

**配置路径**：`mr_adc_configure`（`adc.c:19`）校验通道存在性（`channel >= 32`
或 `exist_mask` 不含该位返回 `-MR_EINVAL`）后透传
`ops->channel_configure`；回读路径对称。

**采样路径**：`mr_adc_read`（`mr_adc.h:244`）仅做断言校验（驱动类型、
`resolution` 1~32、buf/size 一致性）后透传 `ops->read`，返回值原样透传， 框架不拷贝数据。

**流路径**：`mr_adc_stream_start`（`mr_adc.h:271`）对未实现的 `stream_start`
返回 `-MR_ENOSYS`，否则透传 `ops->stream_start(drv, seq, config)`；
`mr_adc_stream_is_busy(drv, &busy)` 成功返回 0，通过 `busy` 输出状态； 未实现时返回 `-MR_ENOSYS`，后端错误原样返回，失败时不能使用
`busy`；
`mr_adc_stream_stop`/`mr_adc_stream_offset`
（`mr_adc.h:328`、`mr_adc.h:354`）对未实现的能力返回 `-MR_ENOSYS`。

**事件路径**：`mr_adc_event_attach` 只订阅 waiter，不打开来源；`mr_adc_event_enable`/`disable` 通过可选
`event_control` 后端回调切换来源。平台后端可在支持的事件发生时调用 `mr_adc_event_raise`，通过 waitable 通知 waiter；
公共接口不保证具体平台实现或硬件中断源。

**换算实现**：`mr_adc_raw2millivolt`（`mr_adc.h:499`）用常量表把 17 档增益 映射为 `mul`/`div` 分数，在 `mr_int64_t` 域计算
`ref_mv * raw * div / (mul << resolution)`，避免 32 位溢出；`raw2microvolt`
把 raw 预乘 1000 后复用同一公式，`raw2volt` 走微伏再除以 1e6 得 `mr_f32_t`。

### 11. 并发与中断安全

- 框架层 API 无内部互斥；`mr_adc_read` 与流启动对同一 ADC 外设的并发由平台
  `ops` 或应用保证。
- 事件通知遵循[等待队列](../../kernel/wait.md)的回调上下文约定：waiter 回调在后端调用 raise 的上下文执行；
  若后端从中断调用 raise，回调中禁止阻塞。
- `mr_adc_data_t.event` 在 setup/shutdown 之间有效；shutdown 后仍挂着的 等待者由等待队列的删除语义处理，应用应在 shutdown
  前摘除等待者。
- 换算函数为纯函数（无共享状态），任意上下文可用。

### 12. 扩展与派生

- **平台移植**：ADC backend 的操作表实现、stream/DMA/TMR 接线、HardTree 安装与验收见 [ADC 驱动移植指南](porting.md)。

### 13. 源码导读

| 位置                                       | 职责                                |
|--------------------------------------------|-------------------------------------|
| `mrX/include/mrX/drivers/adc/mr_adc.h:28`  | 流事件位（HALF/FULL）               |
| `mrX/include/mrX/drivers/adc/mr_adc.h:70`  | 通道配置与采样序列结构              |
| `mrX/include/mrX/drivers/adc/mr_adc.h:87`  | 流配置结构                          |
| `mrX/include/mrX/drivers/adc/mr_adc.h:94`  | 模型数据（参考电压/掩码/事件）      |
| `mrX/include/mrX/drivers/adc/mr_adc.h:102` | 操作表（流回调条件编译）            |
| `mrX/include/mrX/drivers/adc/mr_adc.h:244` | 序列采样入口（断言 + 透传）         |
| `mrX/include/mrX/drivers/adc/mr_adc.h:271` | 流 API（start/is_busy/stop/offset） |
| `mrX/include/mrX/drivers/adc/mr_adc.h:434` | 事件挂接与触发                      |
| `mrX/include/mrX/drivers/adc/mr_adc.h:499` | 增益表与毫伏换算                    |
| `mrX/include/mrX/drivers/adc/mr_adc.h:601` | hardtree 实例安装宏                 |
| `mrX/drivers/adc/adc.c:17`                 | ADC clazz（init/deinit/release）    |
| `mrX/drivers/adc/adc.c:19`                 | 通道配置路径（存在性校验）          |
| `mrX/drivers/adc/adc.c:69`                 | setup：初始化流事件                 |
| `mrX/test/drivers/adc/adc.c`               | 行为级测试                          |

### 14. 附录：配置宏与错误码

**配置宏**

| 宏                                                    | 说明                                   |
|-------------------------------------------------------|----------------------------------------|
| `MR_USE_DRIVER_ADC`                                   | 模块开关，默认 n，依赖 `MR_USE_DRIVER` |
| `MR_USE_DRIVER_ADC_STREAM`                            | 流采样开关，默认 y，依赖 ADC           |
| `MR_ADC_WAIT_STREAM_HALF` / `MR_ADC_WAIT_STREAM_FULL` / `MR_ADC_WAIT_STREAM_ERROR` | 流缓冲半满/全满与错误事件位            |

**错误码**：公共 ADC 层明确返回 `-MR_EINVAL` 表示 configure/configuration 的通道不存在，返回 `-MR_ENOSYS` 表示可选 stream callback 或 `event_control` 未实现；其他 backend 错误码原样透传。空指针、分辨率越界、buf/size 不一致等编程错误在断言开启时由 `MR_ASSERT` 拦截。

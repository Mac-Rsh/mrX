# ADC 驱动（ADC）

> ADC 驱动在驱动框架之上提供统一的模数转换抽象：通道配置、多通道序列采样、
> 可选的连续流（stream）采样与半满缓冲事件，并内置原始码到电压的换算工具。

| 项       | 说明                                                                              |
|----------|-----------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_DRIVER_ADC`（默认关闭）；`MR_USE_DRIVER_ADC_STREAM`（默认开启，依赖 ADC） |
| 头文件   | `<mrX/drivers/adc/mr_adc.h>`                                                      |
| 依赖模块 | [驱动框架 Driver](driver.md)、[等待队列 Wait](../kernel/wait.md)（流事件）        |
| 参考测试 | `mrX/test/drivers/adc/adc.c`                                                      |

---

## 用户指南

### 1. 模块概述

ADC 驱动解决的问题是： **用同一套 API 配置通道、启动采样并拿到统一的原始 码**，电压换算与多通道调度细节不再分散在应用里。

特性：

- 通道配置：单端/差分、7 种参考源、可编程增益（1/6x ~ 128x，17 档）。
- 序列采样 `mr_adc_seq_t`：一次指定通道位掩码、分辨率（1~32 位）、过采样率 与缓冲区，底层一次完成多通道转换。
- 连续流采样（`MR_USE_DRIVER_ADC_STREAM`）：启动/停止/忙查询/偏移查询，支持 循环模式（ping-pong），并以
  `MR_ADC_WAIT_STREAM_HALF`/`MR_ADC_WAIT_STREAM_FULL`
  事件通知半缓冲/全缓冲完成。
- 电压换算：`mr_adc_raw2millivolt`/`raw2microvolt`/`raw2volt`，自动计入增益 与分辨率。
- hardtree 规格宏：在硬件模板中描述"某驱动某通道"，代码用 `mr_ht_adc_spec_t`
  直接配置与采样。

不适用于：需要硬件定时器精确控制采样时刻的场合（时刻控制由平台流实现或 外部触发决定）。

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

**流采样**：`mr_adc_stream_start(drv, &seq, circular)` 启动连续采样，
`circular` 为真时硬件写满后回绕继续；`mr_adc_stream_offset` 查询当前写入 偏移，用于消费 half/full 之间的数据段。平台以
`MR_ADC_WAIT_STREAM_HALF`/
`MR_ADC_WAIT_STREAM_FULL` 事件通知缓冲进度：用 `mr_waiter_init` 初始化等待者 （期望事件为这两个位）后 `mr_adc_event_attach`
挂接。事件回调在中断上下文 执行，内部只做轻量操作。

**内部参考电压**：`mr_adc_ref_internal` 返回驱动模型数据中的内部参考电压 （单位 mV，hardtree 安装时缺省
3300），是换算函数第一个参数的常用来源。

**生命周期**：ADC 驱动对象遵循[驱动框架](driver.md)的生命周期；setup 时 框架初始化内部事件对象，shutdown 时销毁。
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
| `mr_adc_event_attach`                                 | 挂接流事件等待者                        |
| `mr_adc_event_raise`                                  | 触发流事件（一般由平台 `ops` 内部调用） |
| `mr_adc_ref_internal`                                 | 读取内部参考电压（mV）                  |
| `mr_adc_raw2millivolt` / `raw2microvolt` / `raw2volt` | 原始码换算为电压                        |
| `mr_adc_get` / `mr_adc_put`                           | 引用计数加一 / 减一                     |

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
                             mr_bool_t circular);
mr_err_t mr_adc_stream_stop(mr_driver_t *drv);
```

`stream_start`/`stream_stop`/`stream_is_busy`/`stream_offset` 是可选能力：平台
`ops` 未实现 `stream_start`/`stream_stop`/`stream_offset` 时返回 `-MR_ENOSYS`，
`mr_adc_stream_is_busy(drv, &busy)` 成功返回 0 并输出状态，未实现时返回
`-MR_ENOSYS`，后端错误原样返回。HardTree 查询接口同样接收 `busy` 指针。

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
1）；流版本 `mr_ht_adc_stream_start` 等需 `MR_USE_DRIVER_ADC_STREAM`。

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

示例 2：流采样 + 半缓冲事件（改编自测试 `event_paths`）：

```c
static void adc_stream_waiter(mr_waiter_t *waiter, void *param,
                              mr_wait_event_t event, const void *data) {
    int *count = param;
    (*count)++;     /* Half/full buffer filled; consume adc_stream_offset */
}

void adc_stream_example(mr_driver_t *drv) {
    static mr_uint16_t buffer[256];
    static mr_waiter_t waiter;
    static int event_count;
    mr_adc_seq_t seq = {.channels = 1U << 0, .resolution = 12,
                        .buf = buffer, .size = sizeof(buffer)};

    mr_adc_stream_start(drv, &seq, MR_TRUE);    /* Circular stream */
    mr_waiter_init(&waiter, adc_stream_waiter, &event_count,
                   MR_ADC_WAIT_STREAM_HALF | MR_ADC_WAIT_STREAM_FULL);
    mr_adc_event_attach(drv, &waiter);
    /* ... later: mr_adc_stream_stop(drv); */
}
```

更多行为（通道不存在拒绝、流可选能力缺省路径、换算数值）见
`mrX/test/drivers/adc/adc.c`。

### 7. 常见问题（FAQ）

**Q：`mr_adc_configure` 返回 `-MR_EINVAL`？**

通道号大于等于 32 或不在 `exist_mask` 中。检查硬件模板的 `exist_mask`
属性与通道号。

**Q：`mr_adc_stream_stop`/`mr_adc_stream_offset` 返回 `-MR_ENOSYS`？**

平台 `ops` 未实现对应回调。`stream_start`/`stream_stop`/`stream_offset`
未实现时返回 `-MR_ENOSYS`；`stream_is_busy` 未实现时也返回 `-MR_ENOSYS`。

**Q：收不到 half/full 事件？**

排查：等待者期望事件是否包含 `MR_ADC_WAIT_STREAM_HALF`/
`MR_ADC_WAIT_STREAM_FULL`；`mr_adc_event_attach` 是否在 `mr_driver_setup`
之后调用（事件对象在 setup 时初始化）；平台是否在缓冲进度处触发了事件。

**Q：换算结果和万用表对不上？**

确认换算用的参考电压与实际一致：`mr_adc_ref_internal` 返回的是模型数据里的 标称值；使用外部参考时应在
`mr_ht_adc_spec_t.ref_voltage` 或换算参数中传入 实际电压。同时确认增益与分辨率参数和通道配置一致。

**Q：`buf` 与 `size` 怎么定？**

由平台 `ops->read` 决定布局；常见为每个采样值 `resolution` 对齐后的整数倍 （如 12 位常存 `mr_uint16_t`
）。以平台驱动文档/实现为准，框架只做非空 一致性断言。

---

## 开发者指南

### 8. 架构总览

ADC 驱动与 GPIO 同构，是[驱动框架](driver.md)的派生模块，额外引入等待队列 与换算工具：

```
        +------------------------------------------+
        |  应用（mr_adc_read / raw2millivolt）       |
        +-------------------+----------------------+
                            | 通道校验 / seq 断言
        +-------------------v----------------------+
        |  ADC 框架层（mr_adc.c + mr_adc.h）          |
        |  exist_mask 校验、流 API 门控、             |
        |  waitable 事件、电压换算                    |
        +-------------------+----------------------+
                            | mr_adc_ops_t
        +-------------------v----------------------+
        |  平台实现：ch32v ADC（DMA/中断流）           |
        +------------------------------------------+
             依赖：驱动框架、等待队列（流事件）
```

### 9. 数据结构

模型数据（`mrX/include/mrX/drivers/adc/mr_adc.h:87`）：

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

操作表（`mr_adc.h:94`）：`channel_configure`、`channel_configuration`、
`read` 必需；`stream_start`/`stream_stop`/`stream_is_busy`/`stream_offset`
仅在 `MR_USE_DRIVER_ADC_STREAM` 下声明，全部为可选（框架对空回调有缺省返回）。 流回调可重入中断上下文，平台实现需自行保证缓冲一致性。

hardtree 规格（`mr_adc.h:551`）：`mr_ht_adc_spec_t { drv, channel,
resolution, ref_voltage, config }`，属性单元为
`[phandle, channel, resolution, ref_voltage, diff, ref, gain]`，
`ref_voltage` 为 0 时换算函数回退到 `mr_adc_ref_internal`。

### 10. 核心流程

**clazz**：ADC 定义了完整的 init/deinit（`MR_DRIVER_CLAZZES_DEFINE(adc,
adc_init, adc_deinit, adc_release)`，`mrX/drivers/adc/adc.c:17`）。
`adc_init`（`adc.c:69`）在 setup 阶段调用 `mr_waitable_init` 初始化流事件；
`adc_deinit` 在 shutdown 阶段先调用已实现的 `stream_stop`，成功后才通过
`mr_waitable_del` 销毁事件；停止失败时返回错误并保留事件对象。 未启用流功能或没有停止回调时直接销毁事件。
后端停止操作须在空闲时也返回成功，成功后不再访问流缓冲区或触发流事件。
`adc_release` 释放 `data`。通用删除路径不传播 deinit 错误； 可能停止失败的设备应先成功 shutdown，再删除。

**配置路径**：`mr_adc_configure`（`adc.c:19`）校验通道存在性（`channel >= 32`
或 `exist_mask` 不含该位返回 `-MR_EINVAL`）后透传
`ops->channel_configure`；回读路径对称。

**采样路径**：`mr_adc_read`（`mr_adc.h:233`）仅做断言校验（驱动类型、
`resolution` 1~32、buf/size 一致性）后透传 `ops->read`，返回值原样透传， 框架不拷贝数据。

**流路径**：`mr_adc_stream_start`（`mr_adc.h:260`）对未实现的 `stream_start`
返回 `-MR_ENOSYS`，否则透传 `ops->stream_start(drv, seq, circular)`；
`mr_adc_stream_is_busy(drv, &busy)` 成功返回 0，通过 `busy` 输出状态； 未实现时返回 `-MR_ENOSYS`，后端错误原样返回，失败时不能使用
`busy`；
`mr_adc_stream_stop`/`mr_adc_stream_offset`
（`mr_adc.h:320`、`mr_adc.h:346`）对未实现的能力返回 `-MR_ENOSYS`。

**事件路径**：`mr_adc_event_attach`（`mr_adc.h:374`）把等待者挂到
`ad_data->event`；`mr_adc_event_raise`（`mr_adc.h:396`）以
`mr_waitable_raise(event, events, MR_NULL)` 触发，无附加载荷。平台流实现 （DMA 半满/全满中断）在缓冲进度点调用
raise，等待者回调随即执行。

**换算实现**：`mr_adc_raw2millivolt`（`mr_adc.h:440`）用常量表把 17 档增益 映射为 `mul`/`div` 分数，在 `mr_int64_t` 域计算
`ref_mv * raw * div / (mul << resolution)`，避免 32 位溢出；`raw2microvolt`
把 raw 预乘 1000 后复用同一公式，`raw2volt` 走微伏再除以 1e6 得 `mr_f32_t`。

### 11. 并发与中断安全

- 框架层 API 无内部互斥；`mr_adc_read` 与流启动对同一 ADC 外设的并发由平台
  `ops` 或应用保证。
- 事件挂接触发（`mr_adc_event_attach`/`raise`）与等待者回调的上下文约定遵循
  [等待队列](../kernel/wait.md)：raise 常发生于平台中断，回调在中断上下文 执行，禁止阻塞。
- `mr_adc_data_t.event` 在 setup/shutdown 之间有效；shutdown 后仍挂着的 等待者由等待队列的删除语义处理，应用应在 shutdown
  前摘除等待者。
- 换算函数为纯函数（无共享状态），任意上下文可用。

### 12. 扩展与派生

- **平台移植**：`mrX/arch/wch/ch32v/platform/drivers/adc/adc.c` 以
  `MR_HT_ADC_INSTALL` 按硬件模板实例化片上 ADC：通道引脚映射表用
  `MR_HT_DRIVER_PROP_ELEM_FOREACH` 从属性展开，`ops->read` 内部完成通道 轮询转换，是编写新平台 ADC 的参考。
- 新平台驱动实现 `mr_adc_ops_t`（流回调可选）后，通过 `MR_ADC_INSTALL` 或
  `MR_HT_ADC_INSTALL` 注册即可；hardtree 规格宏与换算工具对上层自动可用。

### 13. 源码导读

| 位置                                       | 职责                                |
|--------------------------------------------|-------------------------------------|
| `mrX/include/mrX/drivers/adc/mr_adc.h:28`  | 流事件位（HALF/FULL）               |
| `mrX/include/mrX/drivers/adc/mr_adc.h:70`  | 通道配置与采样序列结构              |
| `mrX/include/mrX/drivers/adc/mr_adc.h:87`  | 模型数据（参考电压/掩码/事件）      |
| `mrX/include/mrX/drivers/adc/mr_adc.h:94`  | 操作表（流回调条件编译）            |
| `mrX/include/mrX/drivers/adc/mr_adc.h:233` | 序列采样入口（断言 + 透传）         |
| `mrX/include/mrX/drivers/adc/mr_adc.h:260` | 流 API（start/is_busy/stop/offset） |
| `mrX/include/mrX/drivers/adc/mr_adc.h:374` | 事件挂接与触发                      |
| `mrX/include/mrX/drivers/adc/mr_adc.h:440` | 增益表与毫伏换算                    |
| `mrX/include/mrX/drivers/adc/mr_adc.h:542` | hardtree 实例安装宏                 |
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
| `MR_ADC_WAIT_STREAM_HALF` / `MR_ADC_WAIT_STREAM_FULL` | 流缓冲半满/全满事件位                  |

**错误码**：`-MR_EINVAL`（通道不存在）、`-MR_ENOSYS`（流能力未实现）、底层
`ops` 错误码透传（如 `-MR_EIO`），定义见 `mrX/include/mrX/libc/mr_errno.h`； 空指针、分辨率越界、buf/size 不一致等编程错误在断言开启时由
`MR_ASSERT`
拦截。

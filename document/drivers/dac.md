# DAC 驱动（DAC）

> DAC 驱动在驱动框架之上提供统一的数模转换抽象：通道参考源配置、多通道序列输出、
> 可选的连续流（stream）输出与半满缓冲事件，并内置电压到原始码的换算工具。

| 项       | 说明                                                                              |
|----------|-----------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_DRIVER_DAC`（默认关闭）；`MR_USE_DRIVER_DAC_STREAM`（默认开启，依赖 DAC） |
| 头文件   | `<mrX/drivers/dac/mr_dac.h>`                                                      |
| 依赖模块 | [驱动框架 Driver](driver.md)、[等待队列 Wait](../kernel/wait.md)（流事件）        |
| 参考测试 | `mrX/test/drivers/dac/dac.c`                                                      |

---

## 用户指南

### 1. 模块概述

DAC 驱动解决的问题是： **用同一套 API 配置通道、把目标电压换算为原始码并输出**， 参考源选择与多通道输出细节不再分散在应用里。

特性：

- 通道配置：7 种参考源（内部参考、4 档 VDD 分档、2 路外部参考）。
- 序列输出 `mr_dac_seq_t`：一次指定通道位掩码、分辨率（1~32 位）与源数据缓冲， 底层一次完成多通道输出。
- 连续流输出（`MR_USE_DRIVER_DAC_STREAM`）：启动/停止/忙查询/偏移查询，支持 循环模式（ping-pong 输出），并以
  `MR_DAC_WAIT_STREAM_HALF`/`MR_DAC_WAIT_STREAM_FULL`
  事件通知半缓冲/全缓冲消费完成。
- 电压换算：`mr_dac_millivolt2raw`/`microvolt2raw`/`volt2raw`，目标电压直接换算 为原始码，只需参考电压与分辨率两个参数。
- hardtree 规格宏：在硬件模板中描述"某驱动某通道"，代码用 `mr_ht_dac_spec_t`
  直接配置与输出。

与姊妹模块 [ADC](adc.md) 同构：结构、流事件与生命周期一致，方向相反——ADC 把 原始码换算为电压，DAC 把电压换算为原始码；DAC
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
        .channels = 1U << 3,        /* Channel 3 */
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

**流输出**：`mr_dac_stream_start(drv, &seq, circular)` 启动连续输出，
`circular` 为真时硬件读完后回绕继续；`mr_dac_stream_offset` 查询当前读取 偏移，用于在 half/full 之间生成下一段数据。平台以
`MR_DAC_WAIT_STREAM_HALF`/
`MR_DAC_WAIT_STREAM_FULL` 事件通知缓冲消费进度：用 `mr_waiter_init` 初始化 等待者（期望事件为这两个位）后
`mr_dac_event_attach` 挂接。事件回调在中断 上下文执行，内部只做轻量操作。

**内部参考电压**：`mr_dac_ref_internal` 返回驱动模型数据中的内部参考电压 （单位 mV，hardtree 安装时缺省
3300），是换算函数第一个参数的常用来源。

**生命周期**：DAC 驱动对象遵循[驱动框架](driver.md)的生命周期；setup 时 框架初始化内部事件对象，shutdown 时销毁。
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

| API                                                   | 说明                                    |
|-------------------------------------------------------|-----------------------------------------|
| `mr_dac_event_attach`                                 | 挂接流事件等待者                        |
| `mr_dac_event_raise`                                  | 触发流事件（一般由平台 `ops` 内部调用） |
| `mr_dac_ref_internal`                                 | 读取内部参考电压（mV）                  |
| `mr_dac_millivolt2raw` / `microvolt2raw` / `volt2raw` | 电压换算为原始码                        |
| `mr_dac_get` / `mr_dac_put`                           | 引用计数加一 / 减一                     |

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
                             mr_bool_t circular);
mr_err_t mr_dac_stream_stop(mr_driver_t *drv);
```

`stream_stop`/`stream_is_busy`/`stream_offset` 是可选能力：平台 `ops` 未实现
`stream_stop`/`stream_offset` 时返回 `-MR_ENOSYS`。
`mr_dac_stream_is_busy(drv, &busy)` 成功返回 0 并输出状态，未实现时返回
`-MR_ENOSYS`，后端错误原样返回。HardTree 查询接口同样接收 `busy` 指针。`stream_start` 未实现 时同样返回 `-MR_ENOSYS`。

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
`[phandle, channel, resolution, ref_voltage, ref]` 生成（后三项可缺省： 分辨率 8、参考电压 0 表示用内部参考、内部参考）；流版本
`mr_ht_dac_stream_start`
等需 `MR_USE_DRIVER_DAC_STREAM`。

### 6. 完整示例

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
    data->ref_internal = 3300;          /* 3.3 V reference */
    data->exist_mask = 0x0000000fU;     /* Channels 0-3 */
    drv = mr_dac_create(data, MR_NULL, (mr_dac_ops_t *)&board_dac_ops);
    if (drv == MR_NULL) {
        return;
    }
    mr_driver_setup(drv);

    raw = (mr_uint16_t)mr_dac_millivolt2raw(3300, 12, 1650);
    mr_dac_write(drv, &seq);
    mr_dac_del(drv);                    /* Frees data */
}
```

示例 2：连续流输出 + 半缓冲事件：

```c
static void dac_stream_waiter(mr_waiter_t *waiter, void *param,
                              mr_wait_event_t event, const void *data) {
    int *count = param;
    (*count)++;     /* Half/full buffer consumed; refill via dac_stream_offset */
}

void dac_stream_example(mr_driver_t *drv) {
    static mr_uint16_t waveform[256];
    static mr_waiter_t waiter;
    static int event_count;
    mr_dac_seq_t seq = {.channels = 1U << 0, .resolution = 12,
                        .buf = waveform, .size = sizeof(waveform)};
    mr_size_t offset;

    mr_dac_stream_start(drv, &seq, MR_TRUE);    /* Circular stream */
    mr_waiter_init(&waiter, dac_stream_waiter, &event_count,
                   MR_DAC_WAIT_STREAM_HALF | MR_DAC_WAIT_STREAM_FULL);
    mr_dac_event_attach(drv, &waiter);
    mr_dac_stream_offset(drv, &offset);
    /* ... later: mr_dac_stream_stop(drv); */
}
```

更多行为（通道不存在拒绝、流可选能力缺省路径、换算数值）见
`mrX/test/drivers/dac/dac.c`。

### 7. 常见问题（FAQ）

**Q：`mr_dac_configure` 返回 `-MR_EINVAL`？**

通道号大于等于 32 或不在 `exist_mask` 中。检查硬件模板的 `exist_mask`
属性与通道号。

**Q：`mr_dac_stream_stop` 返回 `-MR_ENOSYS`？**

平台 `ops` 未实现 `stream_stop`。`stream_start`/`stream_stop`/`stream_offset`
未实现时返回 `-MR_ENOSYS`；`stream_is_busy` 未实现时也返回 `-MR_ENOSYS`。

**Q：收不到 half/full 事件？**

排查：等待者期望事件是否包含 `MR_DAC_WAIT_STREAM_HALF`/
`MR_DAC_WAIT_STREAM_FULL`；`mr_dac_event_attach` 是否在 `mr_driver_setup`
之后调用（事件对象在 setup 时初始化）；平台是否在缓冲进度处触发了事件。

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

DAC 驱动与 [ADC](adc.md) 同构，是[驱动框架](driver.md)的派生模块，额外引入 等待队列与换算工具：

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
        |  平台实现：片上 DAC / DMA 流输出             |
        +------------------------------------------+
             依赖：驱动框架、等待队列（流事件）
```

### 9. 数据结构

模型数据（`mrX/include/mrX/drivers/dac/mr_dac.h:57`）：

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
| `mr_dac_config_t` | `ref:3`                                    | 参考源枚举 `mr_dac_ref_t`（7 种，`mr_dac.h:32`） |
| `mr_dac_seq_t`    | `channels` / `resolution` / `buf` / `size` | 通道位掩码、1~32 位分辨率、const 源数据缓冲      |

操作表（`mr_dac.h:64`）：`channel_configure`、`channel_configuration`、
`write` 必需（框架断言非空）；`stream_start`/`stream_stop`/`stream_is_busy`/
`stream_offset` 仅在 `MR_USE_DRIVER_DAC_STREAM` 下声明，全部为可选（框架对 空回调有缺省返回）。流回调可重入中断上下文，平台实现需自行保证缓冲一致性。

hardtree 规格（`mr_dac.h:500`）：`mr_ht_dac_spec_t { drv, channel, resolution,
ref_voltage, config }`，属性单元为 `[phandle, channel, resolution,
ref_voltage, ref]`，`ref_voltage` 为 0 时换算函数回退到 `mr_dac_ref_internal`。

### 10. 核心流程

**clazz**：DAC 定义了完整的 init/deinit（`MR_DRIVER_CLAZZES_DEFINE(dac,
dac_init, dac_deinit, dac_release)`，`mrX/drivers/dac/dac.c:17`）。
`dac_init`（`dac.c:69`）在 setup 阶段调用 `mr_waitable_init` 初始化流事件；
`dac_deinit` 在 shutdown 阶段先调用已实现的 `stream_stop`，成功后才通过
`mr_waitable_del` 销毁事件；停止失败时返回错误并保留事件对象。 未启用流功能或没有停止回调时直接销毁事件。
后端停止操作须在空闲时也返回成功，成功后不再访问流缓冲区或触发流事件。
`dac_release` 释放 `data`。通用删除路径不传播 deinit 错误； 可能停止失败的设备应先成功 shutdown，再删除。

**配置路径**：`mr_dac_configure`（`dac.c:19`）校验通道存在性（`channel >= 32`
或 `exist_mask` 不含该位返回 `-MR_EINVAL`，`dac.c:36`）后透传
`ops->channel_configure`；回读路径 `mr_dac_configuration`（`dac.c:44`）对称。

**输出路径**：`mr_dac_write`（`mr_dac.h:203`）仅做断言校验（驱动类型、
`resolution` 1~32、buf/size 一致性）后透传 `ops->write`，返回值原样透传， 框架不拷贝数据。

**流路径**：`mr_dac_stream_start`（`mr_dac.h:230`）对未实现的 `stream_start`
返回 `-MR_ENOSYS`，否则透传 `ops->stream_start(drv, seq, circular)`；
`mr_dac_stream_is_busy(drv, &busy)` 成功返回 0，通过 `busy` 输出状态； 未实现时返回 `-MR_ENOSYS`，后端错误原样返回，失败时不能使用
`busy`；
`mr_dac_stream_stop`/`mr_dac_stream_offset`
（`mr_dac.h:290`、`mr_dac.h:316`）对未实现的能力返回 `-MR_ENOSYS`。

**事件路径**：`mr_dac_event_attach`（`mr_dac.h:344`）调用
`mr_waiter_wait(entry, &da_data->event)` 把等待者挂到模型数据的事件上；
`mr_dac_event_raise`（`mr_dac.h:366`）以 `mr_waitable_raise(event, events,
MR_NULL)` 触发，无附加载荷。平台流实现（DMA 半满/全满中断）在缓冲进度点 调用 raise，等待者回调随即执行。

**换算实现**：三个换算函数均为 `MR_INLINE` 纯函数（`mr_dac.h:409`、
`mr_dac.h:431`、`mr_dac.h:453`）：`millivolt2raw` 在 `mr_int64_t` 域计算
`millivolt * ((1 << resolution) - 1) / ref_mv`，避免 32 位溢出，整数除法 向下取整；`microvolt2raw` 把分母放大 1000 倍复用同一公式；
`volt2raw` 先把 伏值乘 1000 转毫伏再走 `millivolt2raw`。

### 11. 并发与中断安全

- 框架层 API 无内部互斥；`mr_dac_write` 与流启动对同一 DAC 外设的并发由平台
  `ops` 或应用保证。
- 事件挂接触发（`mr_dac_event_attach`/`raise`）与等待者回调的上下文约定遵循
  [等待队列](../kernel/wait.md)：raise 常发生于平台中断，回调在中断上下文 执行，禁止阻塞。
- `mr_dac_data_t.event` 在 setup/shutdown 之间有效；shutdown 后仍挂着的 等待者由等待队列的删除语义处理，应用应在 shutdown
  前摘除等待者。
- 换算函数为纯函数（无共享状态），任意上下文可用。

### 12. 扩展与派生

- **平台移植**：接口与 [ADC](adc.md) 完全同构，可参照
  `mrX/arch/wch/ch32v/platform/drivers/adc/adc.c` 的移植路径（属性展开、
  `ops` 实现与 `MR_HT_*_INSTALL` 安装）；当前平台树中尚无 DAC 实现，测试
  `mrX/test/drivers/dac/dac.c` 的桩 `ops` 可作为回调签名的最小参考。
- 新平台驱动实现 `mr_dac_ops_t`（流回调均可选，未实现返回 `-MR_ENOSYS`）后， 通过 `MR_DAC_INSTALL` 或 `MR_HT_DAC_INSTALL`
  注册即可；hardtree 规格宏与 换算工具对上层自动可用。

### 13. 源码导读

| 位置                                       | 职责                                |
|--------------------------------------------|-------------------------------------|
| `mrX/include/mrX/drivers/dac/mr_dac.h:28`  | 流事件位（HALF/FULL）               |
| `mrX/include/mrX/drivers/dac/mr_dac.h:43`  | 通道配置与输出序列结构              |
| `mrX/include/mrX/drivers/dac/mr_dac.h:57`  | 模型数据（参考电压/掩码/事件）      |
| `mrX/include/mrX/drivers/dac/mr_dac.h:64`  | 操作表（流回调条件编译）            |
| `mrX/include/mrX/drivers/dac/mr_dac.h:203` | 序列输出入口（断言 + 透传）         |
| `mrX/include/mrX/drivers/dac/mr_dac.h:230` | 流 API（start/is_busy/stop/offset） |
| `mrX/include/mrX/drivers/dac/mr_dac.h:344` | 事件挂接与触发                      |
| `mrX/include/mrX/drivers/dac/mr_dac.h:409` | 电压到原始码换算                    |
| `mrX/include/mrX/drivers/dac/mr_dac.h:491` | hardtree 实例安装宏                 |
| `mrX/drivers/dac/dac.c:17`                 | DAC clazz（init/deinit/release）    |
| `mrX/drivers/dac/dac.c:19`                 | 通道配置路径（存在性校验）          |
| `mrX/drivers/dac/dac.c:69`                 | setup：初始化流事件                 |
| `mrX/test/drivers/dac/dac.c`               | 行为级测试                          |

### 14. 附录：配置宏与错误码

**配置宏**

| 宏                                                    | 说明                                   |
|-------------------------------------------------------|----------------------------------------|
| `MR_USE_DRIVER_DAC`                                   | 模块开关，默认 n，依赖 `MR_USE_DRIVER` |
| `MR_USE_DRIVER_DAC_STREAM`                            | 流输出开关，默认 y，依赖 DAC           |
| `MR_DAC_WAIT_STREAM_HALF` / `MR_DAC_WAIT_STREAM_FULL` | 流缓冲半满/全满事件位                  |

**错误码**：`-MR_EINVAL`（通道不存在）、`-MR_ENOSYS`（`stream_start`/
`stream_stop`/`stream_offset` 未实现）、底层 `ops` 错误码透传（如 `-MR_EIO`），定义见
`mrX/include/mrX/libc/mr_errno.h`；空指针、分辨率越界、buf/size 不一致等 编程错误在断言开启时由 `MR_ASSERT` 拦截。

# DAC 驱动（DAC）

DAC
驱动在设备驱动框架之上提供统一的数模转换接口，支持通道参考源配置、多通道序列输出、可选的连续流（stream）输出与事件通知，并内置电压到原始码的换算工具。

| 项目   | 说明                                                                 |
|--------|----------------------------------------------------------------------|
| 功能宏 | `MR_USE_DRIVER_DAC`（流输出需额外启用 `MR_USE_DRIVER_DAC_STREAM`）   |
| 头文件 | `<mrX/drivers/dac/mr_dac.h>`                                         |
| 依赖   | [驱动框架](../driver.md)、[等待队列](../../kernel/wait.md)（流事件） |
| 测试   | `test/drivers/dac/dac.c`                                             |

## 配置

| 配置项                     | 类型 | 默认值 | 说明                                       |
|----------------------------|------|--------|--------------------------------------------|
| `MR_USE_DRIVER_DAC`        | bool | n      | 启用 DAC 驱动（依赖 `MR_USE_DRIVER`）      |
| `MR_USE_DRIVER_DAC_STREAM` | bool | y      | 启用流输出接口（依赖 `MR_USE_DRIVER_DAC`） |

## 快速上手

```c
#include <mrX/drivers/dac/mr_dac.h>

mr_err_t dac_output_mv(const char *name, mr_int64_t millivolts) {
    mr_driver_t *drv = mr_driver_find(name);
    mr_uint16_t raw;
    mr_dac_config_t cfg = {.ref = MR_DAC_REF_INTERNAL};
    mr_dac_seq_t seq = {
        .channels = 1U << 3,        /* 输出到通道 3 */
        .resolution = 12U,
        .size = sizeof(raw),
        .buf = &raw,
    };
    mr_ssize_t size;
    mr_err_t ret;

    if ((drv == MR_NULL) || (mr_driver_setup(drv) != 0)) {
        return -MR_ENODEV;
    }

    ret = mr_dac_configure(drv, 3U, &cfg);
    if (ret != 0) {
        return ret;
    }

    raw = (mr_uint16_t)mr_dac_millivolt2raw(
        (mr_int32_t)mr_dac_ref_internal(drv), seq.resolution, millivolts);
    size = mr_dac_write(drv, &seq);
    if (size < 0) {
        return (mr_err_t)size;
    }
    return (size == (mr_ssize_t)sizeof(raw)) ? 0 : -MR_EIO;
}
```

## 核心机制与约束

- **生命周期与所有权**：DAC 遵循[驱动框架](../driver.md)的 `setup` / `shutdown` /
  `del` 生命周期。`setup` 时框架初始化内部 `mr_waitable_t` 事件对象，`shutdown`
  时若启用流功能且后端实现了 `stream_stop`，会先停止流再销毁事件对象。通过
  `mr_dac_create` 动态创建驱动时，传入的 `mr_dac_data_t` 必须由 `mr_malloc`
  分配，驱动删除时框架会自动释放它。
- **通道模型**：通道号范围为 0~31，对应 `mr_dac_data_t.exist_mask` 的 32 个位。
  `mr_dac_configure` 和 `mr_dac_configuration` 会先校验通道是否在 `exist_mask`
  中，不存在则返回 `-MR_EINVAL`；不支持的参考源（`mr_dac_ref_t` 共 7
  种）由后端返回错误，不应静默降级。
- **序列输出**：`mr_dac_seq_t` 用 `channels` 位掩码指定输出通道，`resolution` 必须在
  1~32 之间，`buf` 为 `MR_NULL` 时 `size` 必须为 0（由 `MR_ASSERT`
  检查）。多通道的输出顺序及源缓冲区元素宽度由具体后端决定。
- **流输出与事件**：`stream_start` / `stream_stop` / `stream_is_busy` /
  `stream_offset` 与 `event_control` 均为彼此独立的可选回调，未实现时对应 API 返回
  `-MR_ENOSYS`。`mr_dac_event_attach` 仅将 `mr_waiter_t` 挂接到等待队列，不开启硬件事件源；需配合
  `mr_dac_event_enable` 控制后端中断源，并由后端调用 `mr_dac_event_raise` 派发
  `MR_DAC_WAIT_STREAM_HALF`、`MR_DAC_WAIT_STREAM_DONE` 或
  `MR_DAC_WAIT_STREAM_ERROR` 事件。
- **并发与中断安全**：公共层不加锁，同一 DAC 外设的并发访问由后端或调用方协调。
  `mr_dac_event_raise` 可在中断上下文调用，此时挂接的 waiter 回调在 ISR
  中同步执行，严禁阻塞。电压换算函数为纯函数，可在任意上下文调用。

## API 速查

### 生命周期与管理

| API                                | 说明                                                      |
|------------------------------------|-----------------------------------------------------------|
| `MR_DAC_DEFINE` / `MR_DAC_INSTALL` | 编译期静态定义 / 安装 DAC 驱动                            |
| `MR_DAC_DATA_INIT`                 | 初始化 `mr_dac_data_t`（内部参考电压 mV 与 `exist_mask`） |
| `mr_dac_init`                      | 运行期初始化静态 DAC 驱动                                 |
| `mr_dac_create`                    | 动态创建 DAC 驱动（`data` 需来自 `mr_malloc`）            |
| `mr_dac_del`                       | 删除 DAC 驱动                                             |
| `mr_dac_get` / `mr_dac_put`        | 驱动引用计数加一 / 减一                                   |

### 通道配置与输出

| API                     | 说明                                                             |
|-------------------------|------------------------------------------------------------------|
| `mr_dac_configure`      | 配置指定通道的参考源 `ref`                                       |
| `mr_dac_configuration`  | 回读指定通道配置                                                 |
| `mr_dac_write`          | 执行一次序列输出，成功返回实际写入字节数，失败返回负数错误码     |
| `mr_dac_stream_start`   | 启动连续流输出（需 `MR_USE_DRIVER_DAC_STREAM`）                  |
| `mr_dac_stream_stop`    | 停止连续流输出（需 `MR_USE_DRIVER_DAC_STREAM`）                  |
| `mr_dac_stream_is_busy` | 查询流是否忙碌，结果写入 `busy`（需 `MR_USE_DRIVER_DAC_STREAM`） |
| `mr_dac_stream_offset`  | 查询流当前读取字节偏移（需 `MR_USE_DRIVER_DAC_STREAM`）          |

`mr_dac_stream_config_t` 包含两个单比特字段：`source` 取
`MR_DAC_STREAM_SOURCE_SOFTWARE`（软件自推进）或 `MR_DAC_STREAM_SOURCE_EXTERNAL`（由
HTS 绑定的外部信号逐次触发），`circular` 控制缓冲区读完后是否回绕。外部触发器的配置与启停由生产者或应用负责，DAC
流本身不控制外部定时器。

### 事件与电压换算

| API                                            | 说明                                                         |
|------------------------------------------------|--------------------------------------------------------------|
| `mr_dac_event_attach`                          | 订阅流事件到 `mr_waiter_t`（需在 `mr_driver_setup` 后调用）  |
| `mr_dac_event_enable` / `mr_dac_event_disable` | 按 `MR_DAC_WAIT_STREAM_*` 掩码开关后端事件源                 |
| `mr_dac_event_raise`                           | 触发事件并唤醒匹配的 waiter（供后端调用）                    |
| `mr_dac_ref_internal`                          | 获取驱动内部参考电压标称值（mV，HardTree 默认 3300）         |
| `mr_dac_millivolt2raw`                         | 将毫伏值按参考电压（mV）与分辨率换算为原始码（`mr_int64_t`） |
| `mr_dac_microvolt2raw`                         | 将微伏值换算为原始码（`mr_int64_t`）                         |
| `mr_dac_volt2raw`                              | 将伏特值（`mr_f32_t`）换算为原始码（`mr_int64_t`）           |

`mr_dac_millivolt2raw` 按 `millivolt * ((1 << resolution) - 1) / ref_mv`
向下取整计算。例如参考 3300 mV、12 位分辨率下，1650 mV 换算为 `2047`（而非 2048）。

### HardTree 规格辅助接口

| API / 宏                                                 | 说明                                                                                  |
|----------------------------------------------------------|---------------------------------------------------------------------------------------|
| `MR_HT_DAC_SPEC` / `MR_HT_NODE_DAC_SPEC`                 | 从 HardTree 属性构造 `mr_ht_dac_spec_t`                                               |
| `MR_HT_DAC_SPEC_FOREACH` / `MR_HT_NODE_DAC_SPEC_FOREACH` | 展开多通道 spec 数组                                                                  |
| `mr_ht_dac_configure`                                    | 按 spec 配置通道                                                                      |
| `mr_ht_dac_seq_init`                                     | 根据 spec 填充 `seq->channels` 与 `seq->resolution`（`buf`、`size` 需自行初始化）     |
| `mr_ht_dac_write`                                        | 对 spec 对应驱动执行 `mr_dac_write`                                                   |
| `mr_ht_dac_stream_start` / `stop` / `is_busy` / `offset` | 流操作 spec 包装（需 `MR_USE_DRIVER_DAC_STREAM`）                                     |
| `mr_ht_dac_event_enable` / `disable`                     | 事件开关 spec 包装                                                                    |
| `mr_ht_dac_millivolt2raw` / `microvolt2raw` / `volt2raw` | 使用 spec 参数换算原始码；`spec->ref_voltage == 0` 时自动回退到 `mr_dac_ref_internal` |

HardTree 属性单元格式为 `[phandle, channel, resolution, ref_voltage, ref]`，后 3
项可缺省（默认值依次为 `8`、`0`、`MR_DAC_REF_INTERNAL`）。

## 内部实现

```text
应用层 (mr_dac_write / mr_dac_stream_* / mr_dac_millivolt2raw)
   │ 通道存在性校验 (exist_mask) & seq 断言
   ▼
DAC 公共层 (drivers/dac/dac.c + include/mrX/drivers/dac/mr_dac.h)
   │ 1. clazz 生命周期: dac_init (waitable_init) / dac_deinit (stream_stop + waitable_del)
   │ 2. ops 分发: 必需回调直接断言调用，流与 event_control 缺失返回 -MR_ENOSYS
   ▼
平台 / 芯片后端 (mr_dac_ops_t)
```

`drivers/dac/dac.c` 中 `dac_deinit` 在非删除流程（`!MR_DRIVER_IS_DYING(drv)`）下若
`stream_stop` 返回非零错误，会中止销毁并保留 `event` 等待队列以便重试 `shutdown`
；若驱动正在被删除，则无论停止是否成功都会销毁 `event`，避免 `dac_release` 释放
`data` 时泄漏等待队列。

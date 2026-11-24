# ADC 驱动（ADC）

ADC
驱动在设备驱动框架之上提供统一的模数转换接口，支持通道参数配置、多通道序列采样、可选的连续流（stream）采样与事件通知，并内置原始码到电压的换算工具。

| 项目   | 说明                                                                 |
|--------|----------------------------------------------------------------------|
| 功能宏 | `MR_USE_DRIVER_ADC`（流采样需额外启用 `MR_USE_DRIVER_ADC_STREAM`）   |
| 头文件 | `<mrX/drivers/adc/mr_adc.h>`                                         |
| 依赖   | [驱动框架](../driver.md)、[等待队列](../../kernel/wait.md)（流事件） |
| 测试   | `test/drivers/adc/adc.c`                                             |

## 配置

| 配置项                     | 类型 | 默认值 | 说明                                       |
|----------------------------|------|--------|--------------------------------------------|
| `MR_USE_DRIVER_ADC`        | bool | n      | 启用 ADC 驱动（依赖 `MR_USE_DRIVER`）      |
| `MR_USE_DRIVER_ADC_STREAM` | bool | y      | 启用流采样接口（依赖 `MR_USE_DRIVER_ADC`） |

## 快速上手

```c
#include <mrX/drivers/adc/mr_adc.h>

mr_err_t adc_sample_mv(const char *name, mr_int64_t *millivolts) {
    mr_driver_t *drv = mr_driver_find(name);
    mr_uint16_t raw = 0;
    mr_adc_config_t cfg = {
        .diff = MR_ADC_DIFF_SINGLE,
        .ref = MR_ADC_REF_INTERNAL,
        .gain = MR_ADC_GAIN_1,
    };
    mr_adc_seq_t seq = {
        .channels = 1U << 3,        /* 采样通道 3 */
        .resolution = 12U,
        .oversample = 0U,
        .size = sizeof(raw),
        .buf = &raw,
    };
    mr_ssize_t size;
    mr_err_t ret;

    if ((drv == MR_NULL) || (mr_driver_setup(drv) != 0)) {
        return -MR_ENODEV;
    }

    ret = mr_adc_configure(drv, 3U, &cfg);
    if (ret != 0) {
        return ret;
    }

    size = mr_adc_read(drv, &seq);
    if (size < 0) {
        return (mr_err_t)size;
    }
    if (size != (mr_ssize_t)sizeof(raw)) {
        return -MR_EIO;
    }

    *millivolts = mr_adc_raw2millivolt(
        (mr_int32_t)mr_adc_ref_internal(drv), cfg.gain, seq.resolution, raw);
    return 0;
}
```

## 核心机制与约束

- **生命周期与所有权**：ADC 遵循[驱动框架](../driver.md)的 `setup` / `shutdown` /
  `del` 生命周期。`setup` 时框架初始化内部 `mr_waitable_t` 事件对象，`shutdown`
  时若启用流功能且后端实现了 `stream_stop`，会先停止流再销毁事件对象。通过
  `mr_adc_create` 动态创建驱动时，传入的 `mr_adc_data_t` 必须由 `mr_malloc`
  分配，驱动删除时框架会自动释放它。
- **通道模型**：通道号范围为 0~31，对应 `mr_adc_data_t.exist_mask` 的 32 个位。
  `mr_adc_configure` 和 `mr_adc_configuration` 会先校验通道是否在 `exist_mask`
  中，不存在则返回 `-MR_EINVAL`；不支持的差分模式、参考源或增益由后端返回错误，不应静默降级。
- **序列采样**：`mr_adc_seq_t` 用 `channels` 位掩码指定参与采样的通道，
  `resolution` 必须在 1~32 之间，`buf` 为 `MR_NULL` 时 `size` 必须为 0（由
  `MR_ASSERT` 检查）。多通道的采样顺序、`oversample` 含义以及缓冲区元素宽度由具体后端决定。
- **流采样与事件**：`stream_start` / `stream_stop` / `stream_is_busy` /
  `stream_offset` 与 `event_control` 均为彼此独立的可选回调，未实现时对应 API 返回
  `-MR_ENOSYS`。`mr_adc_event_attach` 仅将 `mr_waiter_t` 挂接到驱动的等待队列，不开启硬件事件源；需配合
  `mr_adc_event_enable` 控制后端中断源，并由后端调用 `mr_adc_event_raise` 派发
  `MR_ADC_WAIT_STREAM_HALF`、`MR_ADC_WAIT_STREAM_FULL` 或
  `MR_ADC_WAIT_STREAM_ERROR` 事件。
- **并发与中断安全**：公共层不加锁，同一 ADC 外设的并发访问由后端或调用方协调。
  `mr_adc_event_raise` 可在中断上下文调用，此时挂接的 waiter 回调在 ISR
  中同步执行，严禁阻塞。电压换算函数为纯函数，可在任意上下文调用。

## API 速查

### 生命周期与管理

| API                                | 说明                                                      |
|------------------------------------|-----------------------------------------------------------|
| `MR_ADC_DEFINE` / `MR_ADC_INSTALL` | 编译期静态定义 / 安装 ADC 驱动                            |
| `MR_ADC_DATA_INIT`                 | 初始化 `mr_adc_data_t`（内部参考电压 mV 与 `exist_mask`） |
| `mr_adc_init`                      | 运行期初始化静态 ADC 驱动                                 |
| `mr_adc_create`                    | 动态创建 ADC 驱动（`data` 需来自 `mr_malloc`）            |
| `mr_adc_del`                       | 删除 ADC 驱动                                             |
| `mr_adc_get` / `mr_adc_put`        | 驱动引用计数加一 / 减一                                   |

### 通道配置与采样

| API                     | 说明                                                             |
|-------------------------|------------------------------------------------------------------|
| `mr_adc_configure`      | 配置指定通道的 `diff`、`ref`、`gain`                             |
| `mr_adc_configuration`  | 回读指定通道配置                                                 |
| `mr_adc_read`           | 执行一次序列采样，成功返回实际读取字节数，失败返回负数错误码     |
| `mr_adc_stream_start`   | 启动连续流采样（需 `MR_USE_DRIVER_ADC_STREAM`）                  |
| `mr_adc_stream_stop`    | 停止连续流采样（需 `MR_USE_DRIVER_ADC_STREAM`）                  |
| `mr_adc_stream_is_busy` | 查询流是否忙碌，结果写入 `busy`（需 `MR_USE_DRIVER_ADC_STREAM`） |
| `mr_adc_stream_offset`  | 查询流当前写入字节偏移（需 `MR_USE_DRIVER_ADC_STREAM`）          |

`mr_adc_stream_config_t` 包含两个单比特字段：`source` 取
`MR_ADC_STREAM_SOURCE_SOFTWARE`（软件自推进）或 `MR_ADC_STREAM_SOURCE_EXTERNAL`（由
HTS 绑定的外部信号逐次触发），`circular` 控制缓冲区写满后是否回绕。外部触发器的配置与启停由生产者或应用负责，ADC
流本身不控制外部定时器。

### 事件与电压换算

| API                                            | 说明                                                             |
|------------------------------------------------|------------------------------------------------------------------|
| `mr_adc_event_attach`                          | 订阅流事件到 `mr_waiter_t`（需在 `mr_driver_setup` 后调用）      |
| `mr_adc_event_enable` / `mr_adc_event_disable` | 按 `MR_ADC_WAIT_STREAM_*` 掩码开关后端事件源                     |
| `mr_adc_event_raise`                           | 触发事件并唤醒匹配的 waiter（供后端调用）                        |
| `mr_adc_ref_internal`                          | 获取驱动内部参考电压标称值（mV，HardTree 默认 3300）             |
| `mr_adc_raw2millivolt`                         | 将原始码按参考电压（mV）、增益与分辨率换算为毫伏（`mr_int64_t`） |
| `mr_adc_raw2microvolt`                         | 换算为微伏（`mr_int64_t`）                                       |
| `mr_adc_raw2volt`                              | 换算为伏特（`mr_f32_t`）                                         |

`mr_adc_raw2millivolt` 按 `ref_mv * raw * div / (mul << resolution)` 计算（`mul`/
`div` 查 17 档增益表，支持 `MR_ADC_GAIN_1_6` 到 `MR_ADC_GAIN_128`）。例如参考 3300
mV、增益 1、12 位分辨率下，码值 2048 换算为 1650 mV。

### HardTree 规格辅助接口

| API / 宏                                                 | 说明                                                                                |
|----------------------------------------------------------|-------------------------------------------------------------------------------------|
| `MR_HT_ADC_SPEC` / `MR_HT_NODE_ADC_SPEC`                 | 从 HardTree 属性构造 `mr_ht_adc_spec_t`                                             |
| `MR_HT_ADC_SPEC_FOREACH` / `MR_HT_NODE_ADC_SPEC_FOREACH` | 展开多通道 spec 数组                                                                |
| `mr_ht_adc_configure`                                    | 按 spec 配置通道                                                                    |
| `mr_ht_adc_seq_init`                                     | 根据 spec 填充 `seq->channels` 与 `seq->resolution`（其余字段需自行初始化）         |
| `mr_ht_adc_read`                                         | 对 spec 对应驱动执行 `mr_adc_read`                                                  |
| `mr_ht_adc_stream_start` / `stop` / `is_busy` / `offset` | 流操作 spec 包装（需 `MR_USE_DRIVER_ADC_STREAM`）                                   |
| `mr_ht_adc_event_enable` / `disable`                     | 事件开关 spec 包装                                                                  |
| `mr_ht_adc_raw2millivolt` / `raw2microvolt` / `raw2volt` | 使用 spec 参数换算电压；`spec->ref_voltage == 0` 时自动回退到 `mr_adc_ref_internal` |

HardTree 属性单元格式为
`[phandle, channel, resolution, ref_voltage, diff, ref, gain]`，后 5 项可缺省（默认值依次为
`8`、`0`、`MR_ADC_DIFF_SINGLE`、`MR_ADC_REF_INTERNAL`、`MR_ADC_GAIN_1`）。

## 内部实现

```text
应用层 (mr_adc_read / mr_adc_stream_* / mr_adc_raw2millivolt)
   │ 通道存在性校验 (exist_mask) & seq 断言
   ▼
ADC 公共层 (drivers/adc/adc.c + include/mrX/drivers/adc/mr_adc.h)
   │ 1. clazz 生命周期: adc_init (waitable_init) / adc_deinit (stream_stop + waitable_del)
   │ 2. ops 分发: 必需回调直接断言调用，流与 event_control 缺失返回 -MR_ENOSYS
   ▼
平台 / 芯片后端 (mr_adc_ops_t)
```

`drivers/adc/adc.c` 中 `adc_deinit` 在非删除流程（`!MR_DRIVER_IS_DYING(drv)`）下若
`stream_stop` 返回非零错误，会中止销毁并保留 `event` 等待队列以便应用重试
`shutdown`；若驱动正在被删除，则无论停止是否成功都会清理 `event`，避免
`adc_release` 释放 `data` 时泄漏等待队列。

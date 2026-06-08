[**中文**](drivers_adc.md) | [**English**](drivers_adc_en.md)

---

# ADC 驱动

ADC 驱动提供模拟到数字转换抽象，支持单端/差分输入、参考电压选择、可编程增益、多通道 sequence 采样，以及可选的 stream 连续采样。

模块只有在 `MR_USE_DRIVER_ADC` 启用时可用。stream 接口只有在 `MR_USE_DRIVER_ADC_STREAM` 启用时可用。

## 核心数据结构和宏

### 通道配置

```c
typedef struct mr_adc_config {
    mr_uint8_t diff: 1;
    mr_uint8_t ref: 3;
    mr_uint8_t gain;
} mr_adc_config_t;
```

`diff` 使用 `mr_adc_diff_t`，支持单端和差分输入。`ref` 使用 `mr_adc_ref_t`，支持内部参考、VDD 分压参考和外部参考。`gain` 使用 `mr_adc_gain_t`，用于描述采样增益。

### 采样 sequence

```c
typedef struct mr_adc_seq {
    mr_uint32_t channels;
    mr_uint8_t resolution;
    mr_uint8_t oversample;
    void *buf;
    mr_size_t size;
} mr_adc_seq_t;
```

`channels` 是通道 bitmask，`resolution` 是分辨率位数，`oversample` 是过采样配置，`buf` 和 `size` 描述采样缓冲区。

### ADC 数据和操作集

```c
typedef struct mr_adc_data {
    mr_uint16_t ref_internal;
    mr_uint32_t exist_mask;
    mr_waitable_t event;
} mr_adc_data_t;

typedef struct mr_adc_ops {
    mr_err_t (*channel_configure)(const mr_driver_t *drv, mr_uint32_t channel,
                                  const mr_adc_config_t *config);
    mr_err_t (*channel_configuration)(const mr_driver_t *drv, mr_uint32_t channel,
                                      mr_adc_config_t *config);
    mr_ssize_t (*read)(const mr_driver_t *drv, mr_adc_seq_t *seq);
} mr_adc_ops_t;
```

启用 stream 后，操作集还包含 `stream_start`、`stream_stop`、`stream_is_busy` 和 `stream_offset`。

### 定义和安装宏

```c
#define MR_ADC_DATA_INIT(_ref_internal, _exist_mask)
#define MR_ADC_DEFINE(_name, _data, _priv, _ops)
#define MR_ADC_INSTALL(_name, _data, _priv, _ops, _level)
#define MR_HT_ADC_INSTALL(_id, _priv, _ops)
```

HardTree 安装默认 `ref_internal` 为 3300，默认 `exist_mask` 为 `0xffffffffU`。

## 模块接口

### 生命周期

```c
mr_err_t mr_adc_init(mr_driver_t *drv, mr_adc_data_t *data,
                     void *priv, mr_adc_ops_t *ops);
mr_driver_t *mr_adc_create(mr_adc_data_t *data, void *priv,
                           mr_adc_ops_t *ops);
mr_err_t mr_adc_del(mr_driver_t *drv);
mr_driver_t *mr_adc_get(mr_driver_t *drv);
mr_bool_t mr_adc_put(mr_driver_t *drv);
```

ADC 初始化会初始化内部 waitable event。动态 ADC 删除时会释放 `MR_DRIVER_DATA(drv)` 指向的数据。

### 配置和读取

```c
mr_err_t mr_adc_configure(mr_driver_t *drv, mr_uint32_t channel,
                          mr_adc_config_t *config);
mr_err_t mr_adc_configuration(mr_driver_t *drv, mr_uint32_t channel,
                              mr_adc_config_t *config);
mr_ssize_t mr_adc_read(mr_driver_t *drv, mr_adc_seq_t *seq);
```

通道配置接口会检查 `channel < 32`，并通过 `exist_mask` 判断通道是否存在。`mr_adc_read()` 按 sequence 调用底层 `ops->read` 完成采样。

### Stream 接口

```c
mr_err_t mr_adc_stream_start(mr_driver_t *drv, mr_adc_seq_t *seq,
                             mr_bool_t circular);
mr_bool_t mr_adc_stream_is_busy(mr_driver_t *drv);
mr_err_t mr_adc_stream_stop(mr_driver_t *drv);
mr_err_t mr_adc_stream_offset(mr_driver_t *drv, mr_size_t *offset);
```

stream start 需要底层同时提供 `stream_is_busy` 和 `stream_start`。忙碌时返回 `-MR_EBUSY`，不支持时返回 `-MR_ENOSYS`。

### 事件和转换工具

```c
#define MR_ADC_WAIT_STREAM_HALF (MR_WAIT_IN | (1ULL << 0))
#define MR_ADC_WAIT_STREAM_FULL (MR_WAIT_IN | (1ULL << 1))

mr_err_t mr_adc_event_attach(mr_driver_t *drv, mr_waiter_t *entry);
mr_err_t mr_adc_event_raise(mr_driver_t *drv, mr_uint64_t events);
mr_uint16_t mr_adc_ref_internal(mr_driver_t *drv);
mr_int64_t mr_adc_raw2millivolt(mr_int32_t ref_mv, mr_adc_gain_t gain,
                                mr_uint8_t resolution, mr_int64_t raw);
mr_int64_t mr_adc_raw2microvolt(mr_int32_t ref_mv, mr_adc_gain_t gain,
                                mr_uint8_t resolution, mr_int64_t raw);
mr_f32_t mr_adc_raw2volt(mr_int32_t ref_mv, mr_adc_gain_t gain,
                         mr_uint8_t resolution, mr_int64_t raw);
```

转换工具按参考电压、增益、分辨率和 raw 值计算电压。

## HardTree ADC spec

ADC spec 属性单元格式为：

```text
[phandle, channel, resolution, ref_voltage, diff, ref, gain]
```

常用封装包括 `mr_ht_adc_configure()`、`mr_ht_adc_read()`、`mr_ht_adc_stream_start()` 和 `mr_ht_adc_raw2millivolt()` 等。

## 使用示例

```c
void sample_adc_usage(mr_driver_t *adc_drv)
{
    mr_uint16_t buf[1];
    mr_adc_config_t cfg = {
        .diff = MR_ADC_DIFF_SINGLE,
        .ref = MR_ADC_REF_INTERNAL,
        .gain = MR_ADC_GAIN_1,
    };
    mr_adc_seq_t seq = {
        .channels = 1U << 0,
        .resolution = 12,
        .oversample = 0,
        .buf = buf,
        .size = sizeof(buf),
    };

    mr_adc_configure(adc_drv, 0, &cfg);
    mr_adc_read(adc_drv, &seq);
    mr_adc_raw2millivolt(3300, MR_ADC_GAIN_1, 12, buf[0]);
}
```

## 注意事项

- 通道号最大为 31，通道有效性由 `exist_mask` 控制。
- `resolution` 必须在 1 到 32 之间。
- `seq->buf` 不能为 `MR_NULL`，除非 `seq->size == 0`。
- `mr_adc_read()` 直接调用底层 `ops->read`，底层驱动必须提供该操作。
- stream API 需要启用 `MR_USE_DRIVER_ADC_STREAM`。

[**中文**](drivers_dac.md) | [**English**](drivers_dac_en.md)

---

# DAC 驱动

DAC 驱动提供数字到模拟输出抽象，支持通道配置、单次 sequence 输出，以及可选的 stream 连续输出。模块还提供 mV、uV、V 到 raw 输出值的转换工具。

模块只有在 `MR_USE_DRIVER_DAC` 启用时可用。stream 接口只有在 `MR_USE_DRIVER_DAC_STREAM` 启用时可用。

## 核心数据结构和宏

### 通道配置和输出 sequence

```c
typedef struct mr_dac_config {
    mr_uint8_t ref: 3;
} mr_dac_config_t;

typedef struct mr_dac_seq {
    mr_uint32_t channels;
    mr_uint8_t resolution;
    const void *buf;
    mr_size_t size;
} mr_dac_seq_t;
```

`ref` 使用 `mr_dac_ref_t`，支持内部参考、VDD 分压参考和外部参考。`channels` 是输出通道 bitmask，`resolution` 是输出分辨率位数，`buf` 和 `size` 描述输出缓冲区。

### DAC 数据和操作集

```c
typedef struct mr_dac_data {
    mr_uint16_t ref_internal;
    mr_uint32_t exist_mask;
    mr_waitable_t event;
} mr_dac_data_t;

typedef struct mr_dac_ops {
    mr_err_t (*channel_configure)(const mr_driver_t *drv, mr_uint32_t channel,
                                  const mr_dac_config_t *config);
    mr_err_t (*channel_configuration)(const mr_driver_t *drv, mr_uint32_t channel,
                                      mr_dac_config_t *config);
    mr_ssize_t (*write)(const mr_driver_t *drv, mr_dac_seq_t *seq);
} mr_dac_ops_t;
```

启用 stream 后，操作集还包含 `stream_start`、`stream_stop`、`stream_is_busy` 和 `stream_offset`。

### 定义和安装宏

```c
#define MR_DAC_DATA_INIT(_ref_internal, _exist_mask)
#define MR_DAC_DEFINE(_name, _data, _priv, _ops)
#define MR_DAC_INSTALL(_name, _data, _priv, _ops, _level)
#define MR_HT_DAC_INSTALL(_id, _priv, _ops)
```

HardTree 安装默认 `ref_internal` 为 3300，默认 `exist_mask` 为 `0xffffffffU`。

## 模块接口

### 生命周期

```c
mr_err_t mr_dac_init(mr_driver_t *drv, mr_dac_data_t *data,
                     void *priv, mr_dac_ops_t *ops);
mr_driver_t *mr_dac_create(mr_dac_data_t *data, void *priv,
                           mr_dac_ops_t *ops);
mr_err_t mr_dac_del(mr_driver_t *drv);
mr_driver_t *mr_dac_get(mr_driver_t *drv);
mr_bool_t mr_dac_put(mr_driver_t *drv);
```

DAC 初始化会初始化内部 waitable event。动态 DAC 删除时会释放 `MR_DRIVER_DATA(drv)` 指向的数据。

### 配置和写入

```c
mr_err_t mr_dac_configure(mr_driver_t *drv, mr_uint32_t channel,
                          mr_dac_config_t *config);
mr_err_t mr_dac_configuration(mr_driver_t *drv, mr_uint32_t channel,
                              mr_dac_config_t *config);
mr_ssize_t mr_dac_write(mr_driver_t *drv, mr_dac_seq_t *seq);
```

通道配置接口会检查 `channel < 32`，并通过 `exist_mask` 判断通道是否存在。`mr_dac_write()` 按 sequence 调用底层 `ops->write` 完成输出。

### Stream 接口

```c
mr_err_t mr_dac_stream_start(mr_driver_t *drv, mr_dac_seq_t *seq,
                             mr_bool_t circular);
mr_bool_t mr_dac_stream_is_busy(mr_driver_t *drv);
mr_err_t mr_dac_stream_stop(mr_driver_t *drv);
mr_err_t mr_dac_stream_offset(mr_driver_t *drv, mr_size_t *offset);
```

stream start 需要底层同时提供 `stream_is_busy` 和 `stream_start`。忙碌时返回 `-MR_EBUSY`，不支持时返回 `-MR_ENOSYS`。

### 事件和转换工具

```c
#define MR_DAC_WAIT_STREAM_HALF (MR_WAIT_IN | (1ULL << 0))
#define MR_DAC_WAIT_STREAM_FULL (MR_WAIT_IN | (1ULL << 1))

mr_err_t mr_dac_event_attach(mr_driver_t *drv, mr_waiter_t *entry);
mr_err_t mr_dac_event_raise(mr_driver_t *drv, mr_uint64_t events);
mr_uint16_t mr_dac_ref_internal(mr_driver_t *drv);
mr_int64_t mr_dac_millivolt2raw(mr_int32_t ref_mv, mr_uint8_t resolution,
                                mr_int64_t mv);
mr_int64_t mr_dac_microvolt2raw(mr_int32_t ref_mv, mr_uint8_t resolution,
                                mr_int64_t uv);
mr_int64_t mr_dac_volt2raw(mr_int32_t ref_mv, mr_uint8_t resolution,
                           mr_f32_t volt);
```

转换工具按参考电压和分辨率将电压值转换为 raw 输出值。

## HardTree DAC spec

DAC spec 属性单元格式为：

```text
[phandle, channel, resolution, ref_voltage, ref]
```

常用封装包括 `mr_ht_dac_configure()`、`mr_ht_dac_write()`、`mr_ht_dac_stream_start()` 和 `mr_ht_dac_millivolt2raw()` 等。

## 使用示例

```c
void sample_dac_usage(mr_driver_t *dac_drv)
{
    mr_uint16_t raw;
    mr_dac_config_t cfg = {
        .ref = MR_DAC_REF_INTERNAL,
    };
    mr_dac_seq_t seq = {
        .channels = 1U << 0,
        .resolution = 12,
        .buf = &raw,
        .size = sizeof(raw),
    };

    mr_dac_configure(dac_drv, 0, &cfg);
    raw = (mr_uint16_t)mr_dac_millivolt2raw(3300, 12, 1200);
    mr_dac_write(dac_drv, &seq);
}
```

## 注意事项

- 通道号最大为 31，通道有效性由 `exist_mask` 控制。
- `resolution` 必须在 1 到 32 之间。
- 转换函数要求参考电压大于 0。
- `mr_dac_write()` 直接调用底层 `ops->write`，底层驱动必须提供该操作。
- stream API 需要启用 `MR_USE_DRIVER_DAC_STREAM`。

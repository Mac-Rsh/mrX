[**中文**](drivers_adc.md) | [**English**](drivers_adc_en.md)

---

# ADC Driver

The ADC driver provides an analog-to-digital conversion abstraction. It supports single-ended and differential input, reference selection, programmable gain, multi-channel sequence sampling, and optional continuous stream sampling.

The module is only available when `MR_USE_DRIVER_ADC` is enabled. Stream APIs are only available when `MR_USE_DRIVER_ADC_STREAM` is enabled.

## Core Data Structures and Macros

### Channel Configuration

```c
typedef struct mr_adc_config {
    mr_uint8_t diff: 1;
    mr_uint8_t ref: 3;
    mr_uint8_t gain;
} mr_adc_config_t;
```

`diff` uses `mr_adc_diff_t` and supports single-ended and differential input. `ref` uses `mr_adc_ref_t` and supports internal, VDD-derived, and external references. `gain` uses `mr_adc_gain_t` to describe sampling gain.

### Sampling Sequence

```c
typedef struct mr_adc_seq {
    mr_uint32_t channels;
    mr_uint8_t resolution;
    mr_uint8_t oversample;
    void *buf;
    mr_size_t size;
} mr_adc_seq_t;
```

`channels` is a channel bitmask. `resolution` is the sample resolution in bits. `oversample` describes oversampling. `buf` and `size` describe the sample buffer.

### ADC Data and Operations

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

When stream support is enabled, the operation table also contains `stream_start`, `stream_stop`, `stream_is_busy`, and `stream_offset`.

### Definition and Installation Macros

```c
#define MR_ADC_DATA_INIT(_ref_internal, _exist_mask)
#define MR_ADC_DEFINE(_name, _data, _priv, _ops)
#define MR_ADC_INSTALL(_name, _data, _priv, _ops, _level)
#define MR_HT_ADC_INSTALL(_id, _priv, _ops)
```

HardTree installation defaults `ref_internal` to 3300 and `exist_mask` to `0xffffffffU`.

## Module Interface

### Lifecycle

```c
mr_err_t mr_adc_init(mr_driver_t *drv, mr_adc_data_t *data,
                     void *priv, mr_adc_ops_t *ops);
mr_driver_t *mr_adc_create(mr_adc_data_t *data, void *priv,
                           mr_adc_ops_t *ops);
mr_err_t mr_adc_del(mr_driver_t *drv);
mr_driver_t *mr_adc_get(mr_driver_t *drv);
mr_bool_t mr_adc_put(mr_driver_t *drv);
```

ADC initialization initializes the internal waitable event. Dynamic ADC deletion frees the data pointed to by `MR_DRIVER_DATA(drv)`.

### Configuration and Reading

```c
mr_err_t mr_adc_configure(mr_driver_t *drv, mr_uint32_t channel,
                          mr_adc_config_t *config);
mr_err_t mr_adc_configuration(mr_driver_t *drv, mr_uint32_t channel,
                              mr_adc_config_t *config);
mr_ssize_t mr_adc_read(mr_driver_t *drv, mr_adc_seq_t *seq);
```

Channel configuration checks `channel < 32` and validates the channel through `exist_mask`. `mr_adc_read()` calls the low-level `ops->read` operation with the sequence.

### Stream Interface

```c
mr_err_t mr_adc_stream_start(mr_driver_t *drv, mr_adc_seq_t *seq,
                             mr_bool_t circular);
mr_bool_t mr_adc_stream_is_busy(mr_driver_t *drv);
mr_err_t mr_adc_stream_stop(mr_driver_t *drv);
mr_err_t mr_adc_stream_offset(mr_driver_t *drv, mr_size_t *offset);
```

Stream start requires both `stream_is_busy` and `stream_start` in the low-level operation table. It returns `-MR_EBUSY` when busy and `-MR_ENOSYS` when unsupported.

### Events and Conversion Helpers

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

Conversion helpers compute voltage from reference voltage, gain, resolution, and raw value.

## HardTree ADC Spec

ADC spec property cells use this format:

```text
[phandle, channel, resolution, ref_voltage, diff, ref, gain]
```

Common wrappers include `mr_ht_adc_configure()`, `mr_ht_adc_read()`, `mr_ht_adc_stream_start()`, and `mr_ht_adc_raw2millivolt()`.

## Usage Example

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

## Notes

- The maximum channel number is 31. Channel validity is controlled by `exist_mask`.
- `resolution` must be between 1 and 32.
- `seq->buf` must not be `MR_NULL` unless `seq->size == 0`.
- `mr_adc_read()` directly calls low-level `ops->read`; the low-level driver must provide it.
- Stream APIs require `MR_USE_DRIVER_ADC_STREAM`.

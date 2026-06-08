[**中文**](drivers_dac.md) | [**English**](drivers_dac_en.md)

---

# DAC Driver

The DAC driver provides a digital-to-analog output abstraction. It supports channel configuration, single sequence output, and optional continuous stream output. The module also provides conversion helpers from mV, uV, and V to raw output values.

The module is only available when `MR_USE_DRIVER_DAC` is enabled. Stream APIs are only available when `MR_USE_DRIVER_DAC_STREAM` is enabled.

## Core Data Structures and Macros

### Channel Configuration and Output Sequence

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

`ref` uses `mr_dac_ref_t` and supports internal, VDD-derived, and external references. `channels` is an output channel bitmask. `resolution` is the output resolution in bits. `buf` and `size` describe the output buffer.

### DAC Data and Operations

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

When stream support is enabled, the operation table also contains `stream_start`, `stream_stop`, `stream_is_busy`, and `stream_offset`.

### Definition and Installation Macros

```c
#define MR_DAC_DATA_INIT(_ref_internal, _exist_mask)
#define MR_DAC_DEFINE(_name, _data, _priv, _ops)
#define MR_DAC_INSTALL(_name, _data, _priv, _ops, _level)
#define MR_HT_DAC_INSTALL(_id, _priv, _ops)
```

HardTree installation defaults `ref_internal` to 3300 and `exist_mask` to `0xffffffffU`.

## Module Interface

### Lifecycle

```c
mr_err_t mr_dac_init(mr_driver_t *drv, mr_dac_data_t *data,
                     void *priv, mr_dac_ops_t *ops);
mr_driver_t *mr_dac_create(mr_dac_data_t *data, void *priv,
                           mr_dac_ops_t *ops);
mr_err_t mr_dac_del(mr_driver_t *drv);
mr_driver_t *mr_dac_get(mr_driver_t *drv);
mr_bool_t mr_dac_put(mr_driver_t *drv);
```

DAC initialization initializes the internal waitable event. Dynamic DAC deletion frees the data pointed to by `MR_DRIVER_DATA(drv)`.

### Configuration and Writing

```c
mr_err_t mr_dac_configure(mr_driver_t *drv, mr_uint32_t channel,
                          mr_dac_config_t *config);
mr_err_t mr_dac_configuration(mr_driver_t *drv, mr_uint32_t channel,
                              mr_dac_config_t *config);
mr_ssize_t mr_dac_write(mr_driver_t *drv, mr_dac_seq_t *seq);
```

Channel configuration checks `channel < 32` and validates the channel through `exist_mask`. `mr_dac_write()` calls the low-level `ops->write` operation with the sequence.

### Stream Interface

```c
mr_err_t mr_dac_stream_start(mr_driver_t *drv, mr_dac_seq_t *seq,
                             mr_bool_t circular);
mr_bool_t mr_dac_stream_is_busy(mr_driver_t *drv);
mr_err_t mr_dac_stream_stop(mr_driver_t *drv);
mr_err_t mr_dac_stream_offset(mr_driver_t *drv, mr_size_t *offset);
```

Stream start requires both `stream_is_busy` and `stream_start` in the low-level operation table. It returns `-MR_EBUSY` when busy and `-MR_ENOSYS` when unsupported.

### Events and Conversion Helpers

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

Conversion helpers convert voltage values to raw output values using reference voltage and resolution.

## HardTree DAC Spec

DAC spec property cells use this format:

```text
[phandle, channel, resolution, ref_voltage, ref]
```

Common wrappers include `mr_ht_dac_configure()`, `mr_ht_dac_write()`, `mr_ht_dac_stream_start()`, and `mr_ht_dac_millivolt2raw()`.

## Usage Example

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

## Notes

- The maximum channel number is 31. Channel validity is controlled by `exist_mask`.
- `resolution` must be between 1 and 32.
- Conversion helpers require a positive reference voltage.
- `mr_dac_write()` directly calls low-level `ops->write`; the low-level driver must provide it.
- Stream APIs require `MR_USE_DRIVER_DAC_STREAM`.

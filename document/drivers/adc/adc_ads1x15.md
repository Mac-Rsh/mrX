# ADS1015 / ADS1115 ADC

I2C 接口的 4 通道 12 位（ADS1015）或 16 位（ADS1115）外部 ADC 芯片，通过标准 ADC
驱动接口对外提供采样能力（遵循[串行总线外扩驱动共同行为](../serial_peripheral.md)）。

| 项目     | 说明                                                                                                  |
|----------|-------------------------------------------------------------------------------------------------------|
| 启用方式 | 随 `MR_USE_DRIVER_ADC` 与 `MR_USE_DRIVER_I2C` 编译，由 HardTree `compatible = "adc_ads1x15"` 节点触发 |
| 头文件   | 复用 `<mrX/drivers/adc/mr_adc.h>`                                                                     |

## HardTree 属性

| 属性         | 必需 | 默认值 | 说明                                            |
|--------------|------|--------|-------------------------------------------------|
| `i2c`        | 是   | -      | 挂载的 I2C 总线控制器 phandle                   |
| `addr`       | 是   | -      | 7 位 I2C 从机地址（`0x48`~`0x4b`）              |
| `resolution` | 是   | -      | 采样分辨率：ADS1015 填 `12`，ADS1115 填 `16`    |
| `data_rate`  | 否   | `4`    | 配置寄存器 DR 编码（`0`~`7`），对应采样率见下表 |
| `exist_mask` | 否   | `0x0f` | 通道存在掩码（按 4 位裁剪）                     |

`data_rate` 编码对应采样率（SPS）：

| 编码          | 0   | 1   | 2   | 3   | 4    | 5    | 6    | 7    |
|---------------|-----|-----|-----|-----|------|------|------|------|
| ADS1115 (SPS) | 8   | 16  | 32  | 64  | 128  | 250  | 475  | 860  |
| ADS1015 (SPS) | 128 | 250 | 490 | 920 | 1600 | 2400 | 3300 | 3300 |

## 快速上手

```c
#include <mrX/drivers/adc/mr_adc.h>

mr_err_t ads1x15_read_ch0(mr_int16_t *raw_out) {
    mr_driver_t *drv = mr_driver_find("ads");
    mr_adc_config_t cfg = {
        .diff = MR_ADC_DIFF_SINGLE,
        .ref = MR_ADC_REF_INTERNAL,
        .gain = MR_ADC_GAIN_1,
    };
    mr_adc_seq_t seq = {
        .channels = 1U << 0,
        .resolution = 16U,          /* ADS1015 需改为 12U */
        .oversample = 0U,
        .size = sizeof(*raw_out),
        .buf = raw_out,
    };

    if ((drv == MR_NULL) || (mr_driver_setup(drv) != 0)) {
        return -MR_ENODEV;
    }
    if (mr_adc_configure(drv, 0U, &cfg) != 0) {
        return -MR_EIO;
    }
    return (mr_adc_read(drv, &seq) == (mr_ssize_t)sizeof(*raw_out)) ? 0 : -MR_EIO;
}
```

## 运行语义与限制

- **通道与差分映射**：逻辑通道 0 ~3 独立缓存配置（默认单端、`MR_ADC_REF_INTERNAL`、
  `MR_ADC_GAIN_1`）。单端模式对应 `AIN0`~`AIN3`；差分模式 0~3 分别对应 `AIN0-AIN1`、
  `AIN0-AIN3`、`AIN1-AIN3`、`AIN2-AIN3`。不支持 ADS1013/1014/1113/1114。
- **参考源与增益**：仅支持 `MR_ADC_REF_INTERNAL`；支持的增益档位与满量程如下：

| 增益        | `MR_ADC_GAIN_1_3` | `MR_ADC_GAIN_1_2` | `MR_ADC_GAIN_1` | `MR_ADC_GAIN_2` | `MR_ADC_GAIN_4` | `MR_ADC_GAIN_8` |
|-------------|-------------------|-------------------|-----------------|-----------------|-----------------|-----------------|
| 满量程 (mV) | ±6144             | ±4096             | ±2048           | ±1024           | ±512            | ±256            |

- **阻塞采样与输出格式**：每次采样触发 single-shot 转换，按 1 ms 间隔轮询 OS
  位（约两倍转换周期超时后返回 `-MR_ETIMEDOUT`）。不支持过采样（`oversample != 0` 返回
  `-MR_ENOSYS`）、流采样和异步事件。每样本固定占 2 字节（ADS1015 右对齐 12 位并符号扩展到
  16 位）；单端负偏移码会被钳位为 `0`，差分结果需先强转为 `mr_int16_t` 再做电压换算。
- **电压换算等效参考**：为适配公共层 `2^resolution` 分母，驱动将 `ref_internal`
  固定设为 `4096`（双极性等效跨度 mV），配合原生 `resolution` 与有符号 `raw` 即可由
  `mr_adc_raw2millivolt` 得到正确电压；HardTree ADC spec 中 `ref_voltage` 应填
  `0` 或 `4096`（切勿填 `2048`，否则换算结果减半）。

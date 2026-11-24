# MCP3008 ADC

SPI 接口的 8 通道 10 位外部 ADC 芯片，通过标准 ADC
驱动接口对外提供采样能力（遵循[串行总线外扩驱动共同行为](../serial_peripheral.md)）。

| 项目     | 说明                                                                                                  |
|----------|-------------------------------------------------------------------------------------------------------|
| 启用方式 | 随 `MR_USE_DRIVER_ADC` 与 `MR_USE_DRIVER_SPI` 编译，由 HardTree `compatible = "adc_mcp3008"` 节点触发 |
| 头文件   | 复用 `<mrX/drivers/adc/mr_adc.h>`                                                                     |

## HardTree 属性

| 属性         | 必需 | 默认值 | 说明                              |
|--------------|------|--------|-----------------------------------|
| `spi`        | 是   | -      | SPI 从设备规格（`<&spi_bus cs>`） |
| `exist_mask` | 否   | `0xff` | 通道存在掩码（按 8 位裁剪）       |

## 快速上手

```c
#include <mrX/drivers/adc/mr_adc.h>

mr_err_t mcp3008_read_ch0(mr_uint16_t *raw_out, mr_int64_t *mv_out) {
    mr_driver_t *drv = mr_driver_find("adc8");
    mr_adc_config_t cfg = {
        .diff = MR_ADC_DIFF_SINGLE,
        .ref = MR_ADC_REF_EXTERNAL_0,
        .gain = MR_ADC_GAIN_1,
    };
    mr_adc_seq_t seq = {
        .channels = 1U << 0,
        .resolution = 10U,
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
    if (mr_adc_read(drv, &seq) != (mr_ssize_t)sizeof(*raw_out)) {
        return -MR_EIO;
    }

    *mv_out = mr_adc_raw2millivolt(3300, cfg.gain, seq.resolution, *raw_out);
    return 0;
}
```

## 运行语义与限制

- **SPI 总线配置**：`mr_adc_configure` 会自动将 SPI 配置为 1 MHz、Mode 0、8 位、MSB
  first、Master 模式，并回读校验配置是否生效。每次采样在单次 CS 片选期间传输 3 字节帧。
- **通道与伪差分**：通道 0~7 默认单端模式；配置为 `MR_ADC_DIFF_DIFF` 时，通道 `n`
  对应 `IN+ = CHn`、`IN- = CH(n ^ 1)`（即 0/1、2/3、4/5、6/7 成对）。MCP3008
  为单极性伪差分转换，负差值会饱和为 `0`，输出始终为无符号 `0..1023`。
- **固定参数与限制**：仅支持 `MR_ADC_REF_EXTERNAL_0` 与 `MR_ADC_GAIN_1`；
  `seq->resolution` 必须为 `10`，`seq->oversample` 必须为 `0`。每样本以本地字节序写入
  2 字节（`mr_uint16_t`），多通道按通道号升序交错存放。不支持流采样与异步事件。
- **电压换算**：芯片无内部参考电压，驱动 `ref_internal` 为 `0`。调用
  `mr_adc_raw2millivolt` 或填写 HardTree ADC spec 的 `ref_voltage` 时，必须显式传入板级实测
  VREF 毫伏值。

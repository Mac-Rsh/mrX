# MCP3008 ADC

复用 [ADC API](adc.md)，并遵守[串行总线外扩驱动共同行为](../serial_peripheral.md)。

| 项目 | 说明 |
|---|---|
| 功能宏 | `MR_USE_DRIVER_ADC`，及对应的 `MR_USE_DRIVER_SPI` |
| 头文件 | `<mrX/drivers/adc/mr_adc.h>` |
| 依赖模块 | SPI、Driver；仅 HardTree 有实例时编译 |

## 用户指南

### HardTree

```dts
adc8: adc8 {
    compatible = "adc_mcp3008";
    status = "defer";
    spi = <&spi0 0>;
};
```

启用 ADC/SPI。`spi` 必需，`exist_mask` 缺省 0xff 并裁剪为 8 位。
SPI 请求 1 MHz、mode 0、8 位、MSB first，每样本保持 CS 连续低电平传完 3 字节，
样本之间释放 CS。VREF 由板级电路提供。

### 配置与数据

通道 0..7 默认单端；只支持 `MR_ADC_REF_EXTERNAL_0` 和 `MR_ADC_GAIN_1`。
支持 `MR_ADC_DIFF_DIFF`，通道 n 对应 CHn 为正端、
CH(n xor 1) 为负端，即 0/1、2/3、4/5、6/7 的两个方向。
这是单极性伪差分转换，负差值饱和为 0，结果不是有符号 ADC 值。

`resolution` 必须是 10，`oversample` 必须是 0。
每样本存为本地字节序的 `mr_uint16_t`，范围 0..1023；
多通道按位号升序交错。硬件返回帧的最后 10 位组成读数。

```c
mr_adc_config_t config = {
    .diff = MR_ADC_DIFF_SINGLE,
    .ref = MR_ADC_REF_EXTERNAL_0,
    .gain = MR_ADC_GAIN_1,
};
mr_adc_configure(drv, 0U, &config);
```

`ref_internal` 为 0，电压换算必须显式提供实测 VREF 的毫伏值；
HardTree ADC consumer spec 的 `ref_voltage` 也必须填写该值。
不能从供电电压或未连接的内部参考推测 VREF。

## 开发者指南

源码：`drivers/adc/adc_mcp3008.c`。
协议依据：[Microchip 数据手册](https://ww1.microchip.com/downloads/en/DeviceDoc/21295d.pdf)。
本轮仅人工静态审查，未构建或测试。


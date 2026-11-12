# ADS1015 / ADS1115 ADC

复用 [ADC API](adc.md)
，并遵守[串行总线外扩驱动共同行为](../serial_peripheral.md)。

| 项目     | 说明                                              |
|----------|---------------------------------------------------|
| 功能宏   | `MR_USE_DRIVER_ADC`，及对应的 `MR_USE_DRIVER_I2C` |
| 头文件   | `<mrX/drivers/adc/mr_adc.h>`                      |
| 依赖模块 | I2C、Driver；仅 HardTree 有实例时编译             |

## 用户指南

### HardTree

```dts
ads: ads {
    compatible = "adc_ads1x15";
    status = "defer";
    i2c = &i2c0;
    addr = <0x48>;
    resolution = <16>;
    data_rate = <4>;
};
```

启用 ADC/I2C。`i2c`、`addr`、`resolution` 必需；
地址 0x48..0x4b；ADS1015 必须指定 12，ADS1115 必须指定 16。
`exist_mask` 缺省 0x0f 并裁剪为 4 位。
不包含 ADS1013/1014/1113/1114 的变体支持。

`data_rate` 是配置寄存器 DR 编码（0..7），缺省 4：

| 编码 | ADS1115 SPS | ADS1015 SPS |
|------|------------:|------------:|
| 0    |           8 |         128 |
| 1    |          16 |         250 |
| 2    |          32 |         490 |
| 3    |          64 |         920 |
| 4    |         128 |        1600 |
| 5    |         250 |        2400 |
| 6    |         475 |        3300 |
| 7    |         860 |        3300 |

### 配置与码值

每个逻辑通道独立缓存配置，缺省单端、INTERNAL、GAIN_1。
单端通道 0..3 对应 AIN0..3；差分通道 0..3
分别对应 AIN0-AIN1、AIN0-AIN3、AIN1-AIN3、AIN2-AIN3。
配置差分只改变对应逻辑通道的 MUX 解释，不增加额外通道号。

只接受 INTERNAL 参考源：

| 公共 gain | 正向满量程 mV |
|-----------|--------------:|
| GAIN_1_3  |          6144 |
| GAIN_1_2  |          4096 |
| GAIN_1    |          2048 |
| GAIN_2    |          1024 |
| GAIN_4    |           512 |
| GAIN_8    |           256 |

每次采样启动 single-shot，关闭 comparator，按 1 ms 间隔轮询 OS，
在约两倍转换周期加余量后返回 `-MR_ETIMEDOUT`。
每样本 2 字节；ADS1015 右对齐到 12 位并符号扩展到 16 位；
ADS1115 保持 16 位。差分值应通过 `mr_int16_t` 读取后再提升到 64 位做电压换算。
单端负偏移码夹为 0，正值最大分别为 2047/32767。

为了兼容公共换算函数的 `2^resolution` 分母，
本 backend 的 `ref_internal` 固定为 **4096 mV 的双极性等效跨度**，
不是声称芯片有 4.096 V 物理参考。使用表中 gain、原生 resolution 和有符号 raw
即可得到正确电压；HardTree ADC spec 的 `ref_voltage` 应设 0，
或者显式设 4096。不要用 2048 替代，否则结果减半。

### 使用片段

```c
mr_adc_config_t config = {
    .diff = MR_ADC_DIFF_SINGLE,
    .ref = MR_ADC_REF_INTERNAL,
    .gain = MR_ADC_GAIN_1,
};
mr_uint16_t raw;
mr_adc_seq_t seq = {
    .channels = 1U, .resolution = 16U, .oversample = 0U,
    .size = sizeof(raw), .buf = &raw,
};
/* Check configure/read return values in application code */
mr_adc_configure(drv, 0U, &config);
mr_adc_read(drv, &seq);
```

## 开发者指南

源码：`drivers/adc/adc_ads1x15.c`。
协议依据：[ADS1115](https://www.ti.com/lit/ds/symlink/ads1115.pdf)、
[ADS1015](https://www.ti.com/lit/ds/symlink/ads1015.pdf) 数据手册。
本轮仅人工静态审查，未构建或测试。


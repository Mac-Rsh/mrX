# MCP4725 DAC

复用 [DAC API](dac.md)
，并遵守[串行总线外扩驱动共同行为](../serial_peripheral.md)。

| 项目     | 说明                                              |
|----------|---------------------------------------------------|
| 功能宏   | `MR_USE_DRIVER_DAC`，及对应的 `MR_USE_DRIVER_I2C` |
| 头文件   | `<mrX/drivers/dac/mr_dac.h>`                      |
| 依赖模块 | I2C、Driver；仅 HardTree 有实例时编译             |

## 用户指南

### HardTree

```dts
dac1: dac1 {
    compatible = "dac_mcp4725";
    status = "defer";
    i2c = &i2c0;
    addr = <0x60>;
};
```

启用 DAC/I2C。`i2c`、`addr` 必需，
7 位地址 0x60..0x67（需与订货地址位和 A0 接线一致）。
`exist_mask` 缺省 1 并裁剪为 1 位。

### API 行为

只有通道 0，配置只接受 `MR_DAC_REF_VDD_1`；
配置查询始终返回该参考源。
分辨率必须是 12，每个样本为本地字节序 `mr_uint16_t`，
码值范围 0..4095。使用两字节 Fast Mode volatile write，
每个样本立即设置正常功耗模式下的 DAC 输出。

```c
mr_uint16_t raw = 2048U;
mr_dac_seq_t seq = {
    .channels = 1U, .resolution = 12U,
    .size = sizeof(raw), .buf = &raw,
};
mr_dac_write(drv, &seq);
```

没有 EEPROM 写入、持久化、power-down 或 stream API。
普通波形输出不会消耗 EEPROM 擦写寿命。
上电时输出可能来自芯片之前保存的 EEPROM；
setup/configure 不清零输出，应用需要主动写入期望值。

`ref_internal` 为 0。毫伏转 raw 或 HardTree DAC consumer spec
必须显式提供实测 VDD，不能按内部参考处理。
电压换算沿用公共函数的 4096 分母。

## 开发者指南

源码：`drivers/dac/dac_mcp4725.c`。
协议依据：[Microchip 数据手册](https://ww1.microchip.com/downloads/en/DeviceDoc/22039d.pdf)。
本轮仅人工静态审查，未构建或测试。


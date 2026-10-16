# MCP4922 DAC

复用 [DAC API](dac.md)，并遵守[串行总线外扩驱动共同行为](../serial_peripheral.md)。

| 项目 | 说明 |
|---|---|
| 功能宏 | `MR_USE_DRIVER_DAC`，及对应的 `MR_USE_DRIVER_SPI` |
| 头文件 | `<mrX/drivers/dac/mr_dac.h>` |
| 依赖模块 | SPI、Driver；仅 HardTree 有实例时编译 |

## 用户指南

### HardTree 和接线

```dts
dac2: dac2 {
    compatible = "dac_mcp4922";
    status = "defer";
    spi = <&spi0 0>;
};
```

启用 DAC/SPI。`spi` 必需，`exist_mask` 缺省 3 并裁剪为 2 位。
SPI 请求 1 MHz、mode 0、8 位、MSB first；
每样本两个字节，CS 在样本间释放。
**LDAC 必须接低，SHDN 必须保持高**，VREFA/VREFB 由板级电路提供。
此版本不接管 LDAC/SHDN GPIO。

### API 行为

通道 0 = DAC A，通道 1 = DAC B。
只支持 `MR_DAC_REF_EXTERNAL_0`，增益固定 1，
参考输入使用 unbuffered，写入时使能对应输出。
分辨率必须是 12，样本为本地字节序 `mr_uint16_t`，
范围 0..4095。

```c
mr_uint16_t raw[2] = {1024U, 3072U};
mr_dac_seq_t seq = {
    .channels = 3U, .resolution = 12U,
    .size = sizeof(raw), .buf = raw,
};
mr_dac_write(drv, &seq);
```

多通道按 A、B 顺序分别锁存，各样本独立 CS 事务，
不保证双通道同时更新。没有 2x 增益、参考缓冲、shutdown、LDAC 同步或 stream 控制。
setup/configure 仅建立软件配置，不主动写输出。

`ref_internal` 为 0，转换必须显式指定目标通道对应的实测 VREF；
两个通道可有不同外部参考，分别在应用的 consumer spec 中描述。
本 backend 的通道配置查询统一返回 EXTERNAL_0，
该值表示外部参考类别，不表示两个通道物理连接到同一参考脚。

## 开发者指南

源码：`drivers/dac/dac_mcp4922.c`。
协议依据：[Microchip 数据手册](https://ww1.microchip.com/downloads/en/DeviceDoc/22250A.pdf)。
本轮仅人工静态审查，未构建或测试。


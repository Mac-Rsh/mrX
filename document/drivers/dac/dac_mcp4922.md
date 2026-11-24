# MCP4922 DAC

SPI 接口的双通道 12 位外部 DAC 芯片，通过标准 DAC
驱动接口对外提供输出能力（遵循[串行总线外扩驱动共同行为](../serial_peripheral.md)）。

| 项目     | 说明                                                                                                  |
|----------|-------------------------------------------------------------------------------------------------------|
| 启用方式 | 随 `MR_USE_DRIVER_DAC` 与 `MR_USE_DRIVER_SPI` 编译，由 HardTree `compatible = "dac_mcp4922"` 节点触发 |
| 头文件   | 复用 `<mrX/drivers/dac/mr_dac.h>`                                                                     |

## HardTree 属性

| 属性         | 必需 | 默认值 | 说明                              |
|--------------|------|--------|-----------------------------------|
| `spi`        | 是   | -      | SPI 从设备规格（`<&spi_bus cs>`） |
| `exist_mask` | 否   | `3`    | 通道存在掩码（按 2 位裁剪）       |

## 快速上手

```c
#include <mrX/drivers/dac/mr_dac.h>

mr_err_t mcp4922_write_ab(mr_uint16_t raw_a, mr_uint16_t raw_b) {
    mr_driver_t *drv = mr_driver_find("dac2");
    mr_dac_config_t cfg = {.ref = MR_DAC_REF_EXTERNAL_0};
    mr_uint16_t buf[2] = {raw_a, raw_b};
    mr_dac_seq_t seq = {
        .channels = (1U << 0) | (1U << 1),
        .resolution = 12U,
        .size = sizeof(buf),
        .buf = buf,
    };

    if ((drv == MR_NULL) || (mr_driver_setup(drv) != 0)) {
        return -MR_ENODEV;
    }
    if ((mr_dac_configure(drv, 0U, &cfg) != 0)
        || (mr_dac_configure(drv, 1U, &cfg) != 0)) {
        return -MR_EIO;
    }
    return (mr_dac_write(drv, &seq) == (mr_ssize_t)sizeof(buf)) ? 0 : -MR_EIO;
}
```

## 运行语义与限制

- **硬件接线与 SPI 配置**：`LDAC` 必须接地（低电平），`SHDN` 必须接高电平，驱动不接管这两个引脚。
  `mr_dac_configure` 会自动将 SPI 配置为 1 MHz、Mode 0、8 位、MSB first、Master
  模式并回读校验。
- **通道与帧格式**：通道 0 对应 DAC A，通道 1 对应 DAC B。仅支持
  `MR_DAC_REF_EXTERNAL_0`，控制位固定为 unbuffered VREF、1x 增益、使能输出。
  `seq->resolution` 必须为 `12`，每样本为本地字节序 `mr_uint16_t`（范围
  `0..4095`）。
- **多通道顺序锁存**：写入前会先校验所有样本范围；多通道按 A、B 升序逐样本通过独立
  CS 事务锁存，不保证双通道严格同时更新。不支持 2x 增益、VREF 缓冲、关断模式、流输出与异步事件。
- **电压换算**：芯片无内部参考，`ref_internal` 为 `0`。VREFA 与 VREFB
  可接不同外部基准，换算时需按目标通道的实际参考电压传入 `mr_dac_millivolt2raw` 或
  consumer spec 的 `ref_voltage`。

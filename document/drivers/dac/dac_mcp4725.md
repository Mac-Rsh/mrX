# MCP4725 DAC

I2C 接口的单通道 12 位外部 DAC 芯片，通过标准 DAC
驱动接口对外提供输出能力（遵循[串行总线外扩驱动共同行为](../serial_peripheral.md)）。

| 项目     | 说明                                                                                                  |
|----------|-------------------------------------------------------------------------------------------------------|
| 启用方式 | 随 `MR_USE_DRIVER_DAC` 与 `MR_USE_DRIVER_I2C` 编译，由 HardTree `compatible = "dac_mcp4725"` 节点触发 |
| 头文件   | 复用 `<mrX/drivers/dac/mr_dac.h>`                                                                     |

## HardTree 属性

| 属性         | 必需 | 默认值 | 说明                                                             |
|--------------|------|--------|------------------------------------------------------------------|
| `i2c`        | 是   | -      | 挂载的 I2C 总线控制器 phandle                                    |
| `addr`       | 是   | -      | 7 位 I2C 从机地址（`0x60`~`0x67`，需与订货地址位和 A0 接线一致） |
| `exist_mask` | 否   | `1`    | 通道存在掩码（按 1 位裁剪）                                      |

## 快速上手

```c
#include <mrX/drivers/dac/mr_dac.h>

mr_err_t mcp4725_set_mv(mr_int64_t millivolts, mr_int32_t vdd_mv) {
    mr_driver_t *drv = mr_driver_find("dac1");
    mr_dac_config_t cfg = {.ref = MR_DAC_REF_VDD_1};
    mr_uint16_t raw = (mr_uint16_t)mr_dac_millivolt2raw(vdd_mv, 12U, millivolts);
    mr_dac_seq_t seq = {
        .channels = 1U << 0,
        .resolution = 12U,
        .size = sizeof(raw),
        .buf = &raw,
    };

    if ((drv == MR_NULL) || (mr_driver_setup(drv) != 0)) {
        return -MR_ENODEV;
    }
    if (mr_dac_configure(drv, 0U, &cfg) != 0) {
        return -MR_EIO;
    }
    return (mr_dac_write(drv, &seq) == (mr_ssize_t)sizeof(raw)) ? 0 : -MR_EIO;
}
```

## 运行语义与限制

- **固定通道与参考源**：仅支持通道 0 与 `MR_DAC_REF_VDD_1`，`mr_dac_configuration`
  固定回读 `MR_DAC_REF_VDD_1`。
- **Fast Mode 易失写入**：`seq->resolution` 必须为 `12`，每样本为本地字节序
  `mr_uint16_t`（范围 `0..4095`）。驱动在写入前先校验整个序列的码值范围，再逐样本以两字节
  Fast Mode 命令写入易失寄存器，不消耗 EEPROM 擦写寿命。
- **不支持的功能**：不支持 EEPROM 写入、power-down 模式、流输出与异步事件。`setup` 与
  `configure` 不会清零芯片输出，上电输出由芯片 EEPROM 决定，应用需主动写入期望值。
- **电压换算**：芯片无内部参考，`ref_internal` 为 `0`。调用 `mr_dac_millivolt2raw`
  或配置 HardTree DAC spec 的 `ref_voltage` 时，必须显式传入实测 VDD 毫伏值。

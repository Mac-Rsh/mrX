# DAC 驱动移植指南

> 本文面向平台驱动开发者，说明如何实现 DAC backend。公共 API 的使用方式见 [DAC](dac.md)。

## 1. 移植边界

DAC 公共层负责通道存在性、waitable 生命周期和电压到 raw 的换算；平台 backend 负责通道配置、实际写入、可选 stream、触发源和事件。

建议实现位置：

```text
mrX/arch/<vendor>/<target>/platform/drivers/dac/
```

## 2. 操作表

需要实现 `mr_dac_ops_t`：

```c
typedef struct mr_dac_ops {
    mr_err_t (*channel_configure)(const mr_driver_t *drv, mr_uint32_t channel,
                                  const mr_dac_config_t *config);
    mr_err_t (*channel_configuration)(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      mr_dac_config_t *config);
    mr_ssize_t (*write)(const mr_driver_t *drv, mr_dac_seq_t *seq);
#if defined(MR_USE_DRIVER_DAC_STREAM)
    mr_err_t (*stream_start)(const mr_driver_t *drv, mr_dac_seq_t *seq,
                             const mr_dac_stream_config_t *config);
    mr_err_t (*stream_stop)(const mr_driver_t *drv);
    mr_err_t (*stream_is_busy)(const mr_driver_t *drv, mr_bool_t *busy);
    mr_err_t (*stream_offset)(const mr_driver_t *drv, mr_size_t *offset);
#endif
    mr_err_t (*event_control)(const mr_driver_t *drv, mr_uint32_t events,
                              mr_bool_t enable);
} mr_dac_ops_t;
```

普通配置与 `write` 是基础路径；stream 和事件控制按能力实现。

## 3. 数据与通道

`mr_dac_data_t` 中的 `ref_internal` 与 `exist_mask` 必须和实际硬件一致。平台私有数据通常保存 DAC
实例、通道映射、GPIO/模拟输出配置、触发定时器、DMA 资源和运行状态。

`channel_configuration` 应返回 backend 实际采用的配置，而不是简单回显用户请求。

## 4. 普通输出

`write` 负责把 `mr_dac_seq_t` 映射为硬件写入。backend 应明确：

- sequence 中多通道的更新顺序；
- raw 宽度、对齐和硬件寄存器格式；
- 同步更新还是逐通道立即更新；
- 不支持的参考源、分辨率或输出模式如何报错。

公共层不会替 backend 自动启动触发定时器或 DMA。

## 5. Stream、DMA 与事件

stream backend 需要定义 `circular`、busy、offset 和 buffer 生命周期。

使用 DMA 时：

- 先准备 DAC/触发源，再启动 DMA 请求；
- 将 DMA HALF/DONE/ERROR 映射为 `MR_DAC_WAIT_STREAM_HALF/DONE/ERROR`；
- stop 后保证 DMA 不再访问用户 buffer，或明确平台例外；
- 有 cache 时处理 clean 与必要屏障。

事件通过 `mr_dac_event_raise()` 同步分发。ISR 中 raise 时 waiter 也位于 ISR 上下文。

## 6. HardTree 与安装

平台可使用 `MR_HT_DAC_INSTALL` 实例化片上 DAC。HardTree 只承载静态硬件资源和连接关系，动态运行状态留在 backend 私有数据中。

## 7. 参考实现

当前可参考：

```text
mrX/arch/st/stm32h5/platform/drivers/dac/dac.c
```

它展示了通道配置、普通 write、stream、event_control 与 `MR_HT_DAC_INSTALL` 的完整接线。

## 8. 移植检查清单

- 通道位图与真实通道一致；
- configuration 能反映实际硬件配置；
- raw 宽度和参考电压与公共换算一致；
- stream 的循环/单次语义明确；
- DONE 表示本轮 stream 搬运完成，不混淆其他硬件状态；
- DMA/cache/trigger 停止顺序安全；
- HardTree 实例能正确安装。

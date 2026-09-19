# ADC 驱动移植指南

> 本文面向平台驱动开发者，说明如何为新的 MCU/SoC 实现 ADC backend。公共 API 的使用方式见 [ADC](adc.md)。

## 1. 移植边界

ADC 公共层负责驱动类型、通道存在性、waitable 生命周期和电压换算；平台 backend 负责把统一配置映射到实际 ADC 外设，并实现采样、可选 stream 和事件来源。

平台实现通常放在：

```text
mrX/arch/<vendor>/<target>/platform/drivers/adc/
```

## 2. 操作表

平台实现 `mr_adc_ops_t`：

```c
typedef struct mr_adc_ops {
    mr_err_t (*channel_configure)(const mr_driver_t *drv, mr_uint32_t channel,
                                  const mr_adc_config_t *config);
    mr_err_t (*channel_configuration)(const mr_driver_t *drv,
                                      mr_uint32_t channel,
                                      mr_adc_config_t *config);
    mr_ssize_t (*read)(const mr_driver_t *drv, mr_adc_seq_t *seq);
#if defined(MR_USE_DRIVER_ADC_STREAM)
    mr_err_t (*stream_start)(const mr_driver_t *drv, mr_adc_seq_t *seq,
                             const mr_adc_stream_config_t *config);
    mr_err_t (*stream_stop)(const mr_driver_t *drv);
    mr_err_t (*stream_is_busy)(const mr_driver_t *drv, mr_bool_t *busy);
    mr_err_t (*stream_offset)(const mr_driver_t *drv, mr_size_t *offset);
#endif
    mr_err_t (*event_control)(const mr_driver_t *drv, mr_uint32_t events,
                              mr_bool_t enable);
} mr_adc_ops_t;
```

普通通道配置与 `read` 是基础能力；stream 与 `event_control` 按平台能力实现。未实现的可选能力应保持为 `MR_NULL`，由公共层返回 `-MR_ENOSYS`。

## 3. 数据与通道

`mr_adc_data_t` 保存内部参考电压、通道存在位图和公共 waitable。平台实例应准确提供：

- `ref_internal`：供公共换算函数使用的内部参考值；
- `exist_mask`：实际存在的 ADC 通道；
- 私有数据：ADC 实例、时钟、GPIO/模拟通道映射、DMA/TMR 资源及运行状态。

`channel_configure` 应只处理目标通道相关硬件状态；`channel_configuration` 返回 backend 当前定义的有效配置。

## 4. 普通采样

`read` 接收 `mr_adc_seq_t`。backend 需要明确：

- sequence 中多通道的硬件排序；
- 原始码宽度与 `resolution` 的关系；
- 单次读取是轮询、同步等待还是利用硬件状态机；
- 不支持的采样时间、参考源、增益或差分模式返回什么错误。

公共层不会替 backend 自动配置 GPIO、ADC 时钟或触发源。

## 5. Stream、DMA 与事件

启用 `MR_USE_DRIVER_ADC_STREAM` 时，stream backend 需要定义 `circular`、busy、offset 和 buffer 生命周期。

若使用 DMA：

- DMA 启动前完成 ADC、触发源与 buffer 配置；
- 明确 DMA HALF/DONE/ERROR 如何映射为 `MR_ADC_WAIT_STREAM_HALF/FULL/ERROR`；
- stop 返回前停止后续 DMA/ADC 访问，或在平台文档中明确仍可能发生的访问；
- 有 D-cache 时按平台 cache 契约处理一致性。

事件通过 `mr_adc_event_raise()` 进入公共 waitable。若从 ISR raise，waiter 同步运行在该 ISR 调用链中，ISR 不应留下会持续触发而无人消费的电平型来源。

## 6. HardTree 与安装

平台可使用 `MR_HT_ADC_INSTALL` 按硬件模板实例化 ADC。HardTree 属性应只描述平台确实需要的静态资源，例如实例、通道位图、参考电压、DMA/TMR 连接和时钟资源。

不要把可以由硬件运行时读取的状态重复固化到公共层。

## 7. 参考实现

当前可参考：

```text
mrX/arch/st/stm32h5/platform/drivers/adc/adc.c
```

该 backend 展示了 `MR_HT_ADC_INSTALL`、通道配置、普通 read、event_control，以及 ADC stream 与 DMA/TMR 的接线方式。

## 8. 移植检查清单

- 通道位图与真实硬件一致；
- configure/configuration 能稳定往返 backend 定义的配置；
- 普通 read 的原始码格式与公共换算假设一致；
- stream start/stop/busy/offset 的生命周期明确；
- HALF/FULL/ERROR 事件来源不会重复失控触发；
- DMA/cache/触发定时器的启动和停止顺序明确；
- HardTree 实例能正确安装并取得平台私有资源。

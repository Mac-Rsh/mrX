# DMA 驱动移植指南

> 本文说明如何为新的 DMA 控制器实现 mrX backend。公共 API 见 [DMA](dma.md)。

## 1. 移植边界

DMA 公共层只统一 channel 编号、存在位图、配置结构、transfer descriptor 和调用接口。请求路由、硬件 channel/stream
映射、descriptor 生命周期、cache 一致性和中断状态全部由 backend 负责。

建议实现位置：

```text
mrX/arch/<vendor>/<target>/platform/drivers/dma/
```

## 2. 操作表

`mr_dma_ops_t` 的基础能力包括：

- `channel_configure`
- `channel_configuration`
- `start`
- `stop`
- `is_busy`
- `offset`

`event_attach` 与 `event_control` 是事件能力，平台不支持时可以留空。

`mr_dma_data_t.exist_mask` 使用 bit n 表示公共 channel n 是否存在。公共 channel 编号不必和芯片寄存器编号相同，但映射必须稳定。

## 3. 配置映射

backend 需要把 `mr_dma_config_t` 映射到具体控制器，包括：

- M2M/M2P/P2M 等方向；
- src/dst width；
- src/dst address adjust；
- burst；
- 芯片特有的 request/channel 选择。

公共层不会预先验证芯片是否支持某个组合。不支持的组合由 backend 返回明确错误。

## 4. Transfer descriptor

`mr_dma_transfer_t` 包含 `src`、`dst`、`count` 与 `next`。backend 必须明确：

- `count` 对当前控制器的单位；
- `next` 是否由硬件链表直接使用还是由 ISR/software 续接；
- start 返回后 descriptor 和 buffer 需要保持多久；
- stop 返回后是否保证硬件不再访问 descriptor/buffer；
- circular 链表或自引用 descriptor 的处理方式。

不要在未说明的情况下保存指向短生命周期栈对象的 descriptor。

## 5. 事件与中断

公共事件为：

```text
MR_DMA_WAIT_HALF
MR_DMA_WAIT_DONE
MR_DMA_WAIT_ERROR
```

事件 waitable 由具体 DMA backend 管理，`event_attach` 负责把 waiter 接入目标 channel，`event_control` 负责开关对应硬件来源。

ISR 应先处理/清除硬件状态，再 raise 对应事件，避免电平型中断重复进入。waiter 在 raise 所在上下文同步执行。

## 6. Offset 与 busy

`is_busy` 必须反映当前 channel 是否仍属于一次活动 transfer。`offset` 的含义要在 backend 中固定，例如“已完成元素数”或“当前
descriptor 内偏移”，不能随硬件路径变化。

上层 UART/ADC/DAC stream 会依赖 busy/offset，因此这两个接口不能只做近似占位。

## 7. Cache 与外设协作

DMA backend 与使用 DMA 的外设驱动共同负责：

- cache clean/invalidate 时机；
- memory barrier；
- 不可 DMA 区域；
- 外设 DMA request 的开关顺序；
- error/abort 后的残余请求清理。

公共 DMA 层不会自动完成这些工作。

## 8. HardTree 与参考实现

平台可通过 HardTree 描述 DMA 控制器实例和外设 request 连接。当前可参考：

```text
mrX/arch/st/stm32h5/platform/drivers/dma/dma.c
```

该实现展示了 channel 私有状态、事件 waitable、HAL/硬件中断与公共 DMA API 的映射。

## 9. 移植检查清单

- exist_mask 与公共 channel 编号稳定；
- 所有基础 callback 均实现；
- width/adjust/burst/direction 映射正确；
- descriptor 与 buffer 生命周期明确；
- HALF/DONE/ERROR 能正确清中断并通知；
- stop 后无悬空 DMA 访问；
- busy/offset 语义稳定；
- cache 与外设 request 顺序经过验证。

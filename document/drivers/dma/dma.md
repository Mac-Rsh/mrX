# DMA 驱动框架

> **状态：通用框架已实现；树内已有 stm32h5 GPDMA 后端（`arch/st/stm32h5/platform/drivers/dma/dma.c`）与公共层行为测试（`test/drivers/dma/dma.c`）。**

| 项目 | 说明 |
|------|------|
| 功能宏 | `MR_USE_DRIVER_DMA`（依赖 `MR_USE_DRIVER`，默认关闭） |
| 公共头文件 | `<mrX/drivers/dma/mr_dma.h>` |
| 驱动类型 | `MR_DRIVER_TYPE_DMA`（`0x0700`） |

## 接口职责

DMA 公共层描述控制器通道、搬运配置和传输操作。SoC 后端负责寄存器、请求路由、可达内存范围、缓存维护以及硬件限制；外设适配层负责配置外设并将外设的数据寄存器和缓冲区提交给 DMA。应用仍使用 ADC、UART、DAC 等外设接口表达采样和收发意图。

当前接口没有通道 claim/release，也没有公共的事件 raise 接口。固定通道分配及避免两个使用者重复配置同一通道，由平台集成和外设适配层负责。事件对象同样属于 backend：可选 `event_attach` 接收 waiter，`event_control` 控制通道事件源；DMA 公共层本身不持有 waitable。

## 通道与驱动生命周期

`mr_dma_data_t.exist_mask` 是 64 位通道存在掩码：第 n 位置 1 表示控制器提供通道 n，通道编号范围为 0–63。公共操作对不存在或超出范围的通道返回 `-MR_EINVAL`。

静态控制器使用 `MR_DMA_DATA_INIT(exist_mask)` 配合 `MR_DMA_DEFINE` / `MR_DMA_INSTALL`；HardTree 安装宏 `MR_HT_DMA_INSTALL` 从控制器属性读取 `exist_mask`。动态控制器使用 `mr_dma_create(data, priv, ops)`，驱动接管 `data` 并在 `mr_dma_del()` 时释放，因此传入的数据对象必须动态分配。除删除外，所有操作都应在 `mr_driver_setup()` 成功后调用。

DMA 驱动不实现 init/deinit 回调，删除时不会自动查询通道忙状态或停止传输。调用方必须先停止所有传输，再删除控制器。

HardTree 通道规格仍采用 `[phandle, channel]`，对应 `mr_ht_dma_spec_t { drv, channel }`。规格只用于寻址，不代表该通道已被独占申请；配套 `mr_ht_dma_configure/configuration/start/stop/is_busy/offset/event_attach/event_enable/event_disable` 可用该规格操作对应通道。

## 公共操作

| 操作 | 语义 |
|------|------|
| `mr_dma_configure` | 配置通道方向、源/目标宽度、地址调整与突发长度         |
| `mr_dma_configuration` | 读取通道当前配置 |
| `mr_dma_start` | 提交传输描述符并启动搬运 |
| `mr_dma_stop` | 请求后端停止通道传输 |
| `mr_dma_is_busy` | 查询传输是否仍在运行 |
| `mr_dma_offset` | 查询当前传输进度 |
| `mr_dma_event_attach` | 订阅通道事件 waiter，不启用后端来源 |
| `mr_dma_event_enable` | 按 `MR_DMA_WAIT_*` mask 启用该通道的后端支持来源 |
| `mr_dma_event_disable` | 按 `MR_DMA_WAIT_*` mask 停用该通道事件来源 |

方向枚举包括外设到内存（P2M）、内存到外设（M2P）和内存到内存（M2M）；宽度枚举覆盖 8、16、32、64，地址调整支持递增、递减和固定，burst 枚举覆盖 1 至 256。公共层不校验这些字段组合，具体枚举映射、硬件可实现性与错误码由 backend 决定。

`mr_dma_transfer_t` 包含 `next`、`count`、`src`、`dst`。公共层只断言首个描述符非空、`count != 0`、`src != MR_NULL`、`dst != MR_NULL`，然后把整个描述符交给 backend；它不遍历 `next`，也没有为 `count` 规定统一的字节/元素换算规则。`mr_dma_offset()` 的公共接口注释明确以字节返回进度。链式描述符、地址对齐、内存可达范围和 count 与硬件计数的映射都由 backend 定义。

DMA 公共事件位为 `MR_DMA_WAIT_HALF`、`MR_DMA_WAIT_DONE` 和 `MR_DMA_WAIT_ERROR`。`mr_dma_event_attach()` 只校验 channel 与可选 callback，然后把 waiter 直接交给 backend；`mr_dma_event_enable/disable()` 同样把 `events` 原样交给 backend。公共层不校验 event mask、不创建 waitable、也不提供 `mr_dma_event_raise`，因此实际事件对象、通知方式与回调上下文完全由 backend 定义。事件挂接或控制 callback 未实现时，对应 API 返回 `-MR_ENOSYS`。

```c
MR_INLINE mr_err_t mr_dma_event_attach(mr_driver_t *drv, mr_uint32_t channel,
                                       mr_waiter_t *entry);
MR_INLINE mr_err_t mr_dma_event_enable(mr_driver_t *drv, mr_uint32_t channel,
                                       mr_uint32_t events);
MR_INLINE mr_err_t mr_dma_event_disable(mr_driver_t *drv, mr_uint32_t channel,
                                        mr_uint32_t events);
```

`channel` 必须小于 64 且在 `exist_mask` 中，否则公共层返回 `-MR_EINVAL`。`events` 不做公共校验，可以为 0 或包含 backend 自己理解的位。`event_attach` 或 `event_control` 未实现时返回 `-MR_ENOSYS`；callback 存在后的返回值由 backend 决定。

示例：在支持相应事件源的 DMA 后端上，先订阅再启用所需来源；fake backend 或没有事件回调的后端不会因此自动产生硬件通知。

```c
static void dma_waiter(mr_waiter_t *waiter, void *param,
                       mr_wait_event_t event, const void *data) {
    /* Handle a supported source; keep the callback short. */
}

void dma_event_example(mr_driver_t *drv, mr_uint32_t channel) {
    static mr_waiter_t waiter;
    mr_wait_event_t mask = MR_DMA_WAIT_HALF | MR_DMA_WAIT_DONE | MR_DMA_WAIT_ERROR;

    mr_waiter_init(&waiter, dma_waiter, MR_NULL, mask);
    if (mr_dma_event_attach(drv, channel, &waiter) != 0) {
        return;
    }
    if (mr_dma_event_enable(drv, channel, mask) != 0) {
        return;
    }
    /* Configure/start the channel according to backend semantics. */
    mr_dma_event_disable(drv, channel, mask);
}
```

公共层不复制 descriptor 或 buffer，也不规定 `start` 返回后 backend 是否继续异步访问它们；真实 DMA backend 必须在自己的契约中明确 descriptor/buffer 生命周期、stop 后硬件访问状态以及不支持配置时采用的错误码。公共层只统一对不存在的 channel 返回 `-MR_EINVAL`。

## 后端约定与当前范围

DMA 后端通过 `mr_dma_ops_t` 实现通道配置、配置读回、启动、停止、忙状态和进度查询；这 6 个 callback 都是公共 API 会直接 `MR_ASSERT` 的基础能力。只有 `event_attach` 和 `event_control` 是可选 callback。公共层不添加通道锁，也不替 backend 推断芯片能力；请求映射、缓存一致性、并发和停止语义都属于 backend。

平台 DMA backend 的通道编号、请求路由、descriptor 生命周期、事件、offset 与 cache 接线见 [DMA 驱动移植指南](porting.md)。公共层行为测试见 `mrX/test/drivers/dma/dma.c`。

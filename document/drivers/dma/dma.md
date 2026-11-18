# 直接存储器访问（DMA）

> DMA 驱动为外设与内存搬运提供统一的通道配置、启停、进度和事件接口。

| 项目     | 说明                                                    |
|----------|---------------------------------------------------------|
| 功能宏   | `MR_USE_DRIVER_DMA`（依赖 `MR_USE_DRIVER`，默认关闭）   |
| 头文件   | `<mrX/drivers/dma/mr_dma.h>`，也通过 `<mrX.h>` 条件引入 |
| 依赖模块 | [驱动框架](../driver.md)、等待队列（仅事件订阅类型）    |
| 参考测试 | `mrX/test/drivers/dma/dma.c`                            |

---

## 用户指南

### 1. 模块概述

DMA 公共层描述控制器通道、搬运配置和传输操作。SoC backend 负责寄存器、请求路由、可达内存范围、cache
一致性与硬件限制；ADC、UART、DAC
等外设适配层则把外设数据寄存器和应用缓冲区组装成传输描述符。

当前公共接口不提供通道 claim/release，也不创建公共 waitable。固定通道分配、请求/trigger
映射及事件对象归 backend 和平台集成管理。

### 2. 配置与依赖

| 配置项              | 默认 | 说明           |
|---------------------|------|----------------|
| `MR_USE_DRIVER`     | n    | 驱动框架总开关 |
| `MR_USE_DRIVER_DMA` | n    | DMA 公共层开关 |

`mr_dma_data_t.exist_mask` 为 64 位通道存在掩码：bit n 为 1 表示通道 n
存在，因此公共通道号范围为 0–63。公共操作对越界或不存在通道返回
`-MR_EINVAL`。

### 3. 快速上手

下例演示一次内存到外设的搬运。`count` 表示逻辑 transfer item 次数；每个 item 在
source/destination 两端分别使用 `src_width` / `dst_width` 指定的访问宽度。寄存器地址约束
仍需以目标 backend 文档为准：

```c
#include <mrX/drivers/dma/mr_dma.h>

static mr_err_t dma_send(mr_driver_t *dma, mr_uint32_t channel,
                         const void *src, void *peripheral,
                         mr_size_t count, mr_dma_transfer_t *transfer) {
    mr_dma_config_t config = {
        .src_width = MR_DMA_WIDTH_8,
        .dst_width = MR_DMA_WIDTH_8,
        .src_adj = MR_DMA_ADJ_INC,
        .dst_adj = MR_DMA_ADJ_CONST,
        .src_burst = MR_DMA_BURST_1,
        .dst_burst = MR_DMA_BURST_1,
        .direction = MR_DMA_DIR_M2P,
    };
    mr_err_t ret;

    *transfer = (mr_dma_transfer_t){
        .next = MR_NULL,
        .count = count,
        .src = src,
        .dst = peripheral,
    };

    ret = mr_dma_configure(dma, channel, &config);
    if (ret != 0) {
        return ret;
    }
    return mr_dma_start(dma, channel, transfer);
}
```

使用前应先完成 `mr_driver_setup()`。`transfer`
指向调用方持有的描述符；描述符、源缓冲和目标区域在传输期间必须保持有效，直到成功停止并确认
backend 不再访问它们；不要把函数局部栈对象传入异步传输。删除驱动前由调用方停止所有通道。

### 4. 配置、传输与事件

`mr_dma_config_t` 描述源/目标宽度、地址调整、burst 及传输方向。宽度支持 8/16/32/64
位，地址可递增、递减或固定，burst 枚举覆盖
1–256，方向为 P2M、M2P 或 M2M。公共层不校验枚举组合是否可由硬件实现。

外部 trigger 不属于单次软件传输参数。需要由 DRDY、定时器 TRGO 等硬件事件控制节奏时，平台应把
trigger source、极性和粒度静态绑定到
DMA channel；应用仍使用同一组 `mr_dma_configure/start/stop` 接口。具体绑定格式和可用
trigger 由目标 backend 规定。

`mr_dma_transfer_t` 含 `next`、`count`、`src`、`dst`。`count` 统一表示逻辑
transfer item 次数；一次 item 即使 source/destination width 不同，也只计一次。公共层只校验
首个描述符及其 `count/src/dst`，不遍历 `next`，也不复制描述符或缓冲区。链式描述符是否支持、
地址对齐和生命期由 backend 定义；`mr_dma_offset` 的公共输出统一为 source-side 字节进度。

事件位为 `MR_DMA_WAIT_HALF`、`MR_DMA_WAIT_DONE`、`MR_DMA_WAIT_ERROR`。
`event_attach` 订阅 backend 提供的事件对象，
`event_enable/disable` 只控制来源；公共层不校验 mask、不生成事件。

### 5. API 参考

| API                                            | 说明                           |
|------------------------------------------------|--------------------------------|
| `mr_dma_init` / `mr_dma_create`                | 初始化静态实例 / 创建动态实例  |
| `mr_dma_del`                                   | 删除驱动；不会自动停止活动通道 |
| `mr_dma_configure` / `mr_dma_configuration`    | 设置 / 读回通道配置            |
| `mr_dma_start` / `mr_dma_stop`                 | 启动 / 停止传输                |
| `mr_dma_is_busy`                               | 通过输出参数返回忙状态         |
| `mr_dma_offset`                                | 通过输出参数返回字节进度       |
| `mr_dma_event_attach`                          | 挂接 waiter                    |
| `mr_dma_event_enable` / `mr_dma_event_disable` | 启停 backend 事件来源          |

```c
mr_err_t mr_dma_configure(mr_driver_t *drv, mr_uint32_t channel,
                          const mr_dma_config_t *config);
mr_err_t mr_dma_configuration(mr_driver_t *drv, mr_uint32_t channel,
                              mr_dma_config_t *config);
mr_err_t mr_dma_start(mr_driver_t *drv, mr_uint32_t channel,
                      mr_dma_transfer_t *transfer);
mr_err_t mr_dma_stop(mr_driver_t *drv, mr_uint32_t channel);
mr_err_t mr_dma_is_busy(mr_driver_t *drv, mr_uint32_t channel,
                        mr_bool_t *busy);
mr_err_t mr_dma_offset(mr_driver_t *drv, mr_uint32_t channel,
                       mr_size_t *offset);
```

上述 API 成功返回 0，失败返回负错误码。通道不存在固定返回 `-MR_EINVAL`；其他错误由
backend 透传。事件回调未实现时返回
`-MR_ENOSYS`。空指针、错误驱动类型、空传输描述符和零 `count` 等前置条件由
`MR_ASSERT` 约束。

HardTree 为上述通道 API 提供同名 `mr_ht_dma_*` 包装，`mr_ht_dma_spec_t`
只保存驱动和通道号，不表示独占权。

### 6. 事件示例

```c
static void dma_waiter(mr_waiter_t *waiter, void *param,
                       mr_wait_event_t event, const void *data) {
    /* 回调保持简短；其执行上下文由 backend 决定。 */
}

static mr_err_t dma_events_start(mr_driver_t *dma, mr_uint32_t channel) {
    static mr_waiter_t waiter;
    const mr_wait_event_t events =
        MR_DMA_WAIT_HALF | MR_DMA_WAIT_DONE | MR_DMA_WAIT_ERROR;
    mr_err_t ret;

    mr_waiter_init(&waiter, dma_waiter, MR_NULL, events);
    ret = mr_dma_event_attach(dma, channel, &waiter);
    if (ret != 0) {
        return ret;
    }
    return mr_dma_event_enable(dma, channel, events);
}
```

### 7. 常见问题（FAQ）

**`mr_dma_start` 成功后能否立即复用 descriptor 或 buffer？**

不能由公共层保证。真实 DMA 往往在返回后继续异步访问它们，必须遵守目标 backend
的生命期和 cache 约定。

**为什么事件 API 返回 `-MR_ENOSYS`？**

该 backend 没有实现 `event_attach` 或 `event_control`。公共层不会用轮询模拟事件。

**能否由两个外设共用一个 channel？**

公共层没有仲裁机制。平台必须在集成阶段确保请求路由和通道分配不冲突。

---

## 开发者指南

### 8. 架构总览

```text
应用 / 外设驱动
        |
        v
mr_dma_* / mr_ht_dma_*
        |
        v
DMA 公共层（类型、通道存在性、生命周期）
        |
        v
mr_dma_ops_t backend（路由、寄存器、IRQ、cache）
```

DMA clazz 不实现 init/deinit，只在动态驱动最终释放时回收 `mr_dma_data_t`
。公共层不保存通道配置、忙状态或传输描述符。

### 9. 数据结构与核心流程

`mr_dma_ops_t` 的 `channel_configure`、`channel_configuration`、`start`、`stop`、
`is_busy`、`offset` 是基础能力，公共 API
直接断言它们存在；`event_attach` 和 `event_control` 是可选能力。

`mr_dma_configure/configuration` 在 `mrX/drivers/dma/dma.c`
中实现，先校验驱动类型和通道存在性，再透传至 backend。其他通道
API 在头文件中内联实现，采用同样的检查顺序。

### 10. 并发与中断安全

- DMA 公共层不添加锁，同一通道的配置、启停与查询是否可并发由 backend 决定。
- waiter 的触发上下文由 backend 的 IRQ/轮询设计决定，回调不因为通过 DMA API
  挂接就自动获得可阻塞性。
- 删除或重新配置前，平台需确认硬件已停止访问 descriptor 和 buffer。
- cache clean/invalidate 时机和对齐由 backend
  处理，详见 [DMA 驱动移植指南](porting.md)。

### 11. 扩展与源码导读

| 位置                                             | 职责                                  |
|--------------------------------------------------|---------------------------------------|
| `mrX/include/mrX/drivers/dma/mr_dma.h`           | 枚举、描述符、ops、生命周期与内联 API |
| `mrX/drivers/dma/dma.c`                          | 配置/读回分发与 clazz                 |
| `mrX/include/mrX/drivers/dma/Kconfig`            | DMA 功能开关                          |
| `mrX/test/drivers/dma/dma.c`                     | 公共层通道、事件和错误路径约束        |

实现新 backend 时还必须明确通道号与硬件实例映射、请求路由、`count` 单位、descriptor
寿命、stop 后硬件状态、offset 语义和事件上下文。

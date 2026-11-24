# DMA 驱动（DMA）

DMA 驱动为外设与内存之间的数据搬运提供统一的通道配置、传输启停、进度查询和完成/错误事件订阅接口。

| 项目   | 说明                                                                         |
|--------|------------------------------------------------------------------------------|
| 功能宏 | `MR_USE_DRIVER_DMA`（依赖 `MR_USE_DRIVER`）                                  |
| 头文件 | `<mrX/drivers/dma/mr_dma.h>`                                                 |
| 依赖   | [驱动框架（Driver）](../driver.md)、[等待队列（Wait）](../../kernel/wait.md) |
| 测试   | `test/drivers/dma/dma.c`                                                     |

## 配置

| 配置项              | 类型 | 默认值 | 说明                                               |
|---------------------|------|--------|----------------------------------------------------|
| `MR_USE_DRIVER_DMA` | bool | n      | 启用 DMA 驱动（`include/mrX/drivers/dma/Kconfig`） |

## 快速上手

```c
#include <mrX/drivers/dma/mr_dma.h>

static mr_err_t dma_m2p_send(mr_driver_t *dma, mr_uint32_t channel,
                             const void *src, void *periph_reg,
                             mr_size_t count, mr_dma_transfer_t *xfer) {
    mr_dma_config_t cfg = {
        .src_width = MR_DMA_WIDTH_8,
        .dst_width = MR_DMA_WIDTH_8,
        .src_adj = MR_DMA_ADJ_INC,
        .dst_adj = MR_DMA_ADJ_CONST,
        .src_burst = MR_DMA_BURST_1,
        .dst_burst = MR_DMA_BURST_1,
        .direction = MR_DMA_DIR_M2P,
    };
    mr_err_t ret;

    *xfer = (mr_dma_transfer_t){
        .next = MR_NULL,
        .count = count, /* 逻辑传输项数，非字节数 */
        .src = src,
        .dst = periph_reg,
    };

    ret = mr_dma_configure(dma, channel, &cfg);
    if (ret != 0) {
        return ret;
    }
    return mr_dma_start(dma, channel, xfer);
}
```

若需要半传输、完成或错误通知，可通过等待队列订阅事件：

```c
static void dma_waiter_cb(mr_waiter_t *waiter, void *param,
                          mr_wait_event_t event, const void *data) {
    MR_IGNORE(waiter);
    MR_IGNORE(param);
    MR_IGNORE(event);
    MR_IGNORE(data);
}

static mr_err_t dma_events_setup(mr_driver_t *dma, mr_uint32_t channel) {
    static mr_waiter_t waiter;
    const mr_wait_event_t events =
        MR_DMA_WAIT_HALF | MR_DMA_WAIT_DONE | MR_DMA_WAIT_ERROR;
    mr_err_t ret;

    mr_waiter_init(&waiter, dma_waiter_cb, MR_NULL, events);
    ret = mr_dma_event_attach(dma, channel, &waiter);
    if (ret != 0) {
        return ret;
    }
    return mr_dma_event_enable(dma, channel, events);
}
```

## 核心机制与约束

- **通道范围与校验**：`mr_dma_data_t` 使用 64 位 `exist_mask` 标记有效通道，通道编号范围为
  `0`~`63`。所有通道接口均校验 `channel < 64U` 且对应掩码位为 `1`，否则返回
  `-MR_EINVAL`。公共层不提供动态通道申请/释放接口，通道与外设请求（request/trigger）的绑定由平台集成阶段静态配置。
- **描述符与缓冲区生命周期**：`mr_dma_start` 仅校验首节点 `transfer != MR_NULL`、
  `count != 0`、`src != MR_NULL`、`dst != MR_NULL`，不会复制描述符或缓冲区，也不遍历
  `transfer->next` 链表。调用方必须保证描述符、源/目的缓冲区在整个异步传输期间持续有效（切勿传入局部栈变量），直至传输完成或调用
  `mr_dma_stop` 确认硬件已停止。
- **项数与字节偏移语义**：`mr_dma_transfer_t.count`
  表示逻辑传输项（item）次数，即使源/目的访问宽度不同，每次传输仍计 1 个 item；而
  `mr_dma_offset` 返回的是源端已处理的字节进度。
- **配置与事件边界**：`mr_dma_config_t` 支持 8/16/32/64
  位宽度、地址递增/递减/固定、1~256 突发长度及 `P2M`/`M2P`/`M2M`
  方向；公共层不校验硬件是否支持特定组合，由 backend 决定。`MR_DMA_WAIT_HALF`/
  `DONE`/`ERROR` 事件掩码同样原样透传给 backend，若 backend 未实现事件回调则返回
  `-MR_ENOSYS`。
- **生命周期与并发**：调用 `mr_dma_create` 传入的 `mr_dma_data_t` 必须由
  `mr_malloc` 分配，驱动删除时由框架释放。`mr_dma_del` 不会自动停止正在运行的通道，删除前需由调用方显式调用
  `mr_dma_stop`。公共层无内部互斥锁，同一通道的并发操作安全性与 cache 一致性由
  backend 保证。

## API 速查

### 实例生命周期与宏

| API                                                 | 说明                                                 |
|-----------------------------------------------------|------------------------------------------------------|
| `MR_DMA_DATA_INIT(_exist_mask)`                     | 初始化 `mr_dma_data_t`（64 位通道掩码）              |
| `MR_DMA_DEFINE(_name, _data, _priv, _ops)`          | 静态定义 DMA 驱动实例                                |
| `MR_DMA_INSTALL(_name, _data, _priv, _ops, _level)` | 静态定义并按初始化级别自动安装驱动                   |
| `MR_HT_DMA_INSTALL(_id, _priv, _ops)`               | 通过 HardTree 安装实例（自动读取 `exist_mask` 属性） |
| `mr_dma_init(drv, data, priv, ops)`                 | 初始化静态 DMA 驱动                                  |
| `mr_dma_create(data, priv, ops)`                    | 动态创建 DMA 驱动                                    |
| `mr_dma_del(drv)`                                   | 删除 DMA 驱动（不自动停止活动通道）                  |

### 通道配置、传输与事件

| API                                          | 说明                                              |
|----------------------------------------------|---------------------------------------------------|
| `mr_dma_configure(drv, channel, config)`     | 配置通道宽度、地址调整、突发与方向                |
| `mr_dma_configuration(drv, channel, config)` | 回读通道当前配置                                  |
| `mr_dma_start(drv, channel, transfer)`       | 启动 DMA 传输                                     |
| `mr_dma_stop(drv, channel)`                  | 停止 DMA 传输                                     |
| `mr_dma_is_busy(drv, channel, busy)`         | 查询通道是否处于忙状态                            |
| `mr_dma_offset(drv, channel, offset)`        | 获取源端已传输字节偏移                            |
| `mr_dma_event_attach(drv, channel, entry)`   | 挂接 `mr_waiter_t` 事件等待者                     |
| `mr_dma_event_enable(drv, channel, events)`  | 使能通道事件（`MR_DMA_WAIT_HALF`/`DONE`/`ERROR`） |
| `mr_dma_event_disable(drv, channel, events)` | 禁用通道事件                                      |

### HardTree 规格辅助

`mr_ht_dma_spec_t` 封装 `{drv, channel}`，从 HardTree 属性单元
`[phandle, channel]` 构造：

| API                                                                                | 说明                                                                                                                      |
|------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------|
| `MR_HT_NODE_DMA_SPEC(_node, _prop)` / `MR_HT_DMA_SPEC(_id, _prop)`                 | 生成单个 `mr_ht_dma_spec_t`                                                                                               |
| `MR_HT_NODE_DMA_SPEC_FOREACH(_node, _prop)` / `MR_HT_DMA_SPEC_FOREACH(_id, _prop)` | 遍历属性数组生成规格列表                                                                                                  |
| `mr_ht_dma_*`                                                                      | 对应 `configure`/`configuration`/`start`/`stop`/`is_busy`/`offset`/`event_attach`/`event_enable`/`event_disable` 包装函数 |

## 内部实现

DMA 驱动类在 `drivers/dma/dma.c` 中仅注册 `release` 回调（用于释放
`MR_DRIVER_DATA(drv)`），无 `init`/`deinit` 阶段。`mr_dma_configure` 与
`mr_dma_configuration` 在 `drivers/dma/dma.c` 中实现，其余通道操作与事件接口均在
`include/mrX/drivers/dma/mr_dma.h` 中内联分发给 `mr_dma_ops_t`。

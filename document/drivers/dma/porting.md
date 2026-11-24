# DMA 驱动移植指南

本文面向平台 backend 开发者，说明如何为目标 SoC/MCU 实现 DMA
控制器驱动。公共接口用法见 [DMA 驱动](dma.md)。

## 1. 移植边界

| 职责               | 公共层                                                     | Backend                                                     |
|--------------------|------------------------------------------------------------|-------------------------------------------------------------|
| 通道存在性校验     | 校验 `channel < 64` 及 `exist_mask` 对应位（`-MR_EINVAL`） | 将公共通道号映射到物理控制器通道/流                         |
| 搬运参数与请求路由 | 透传 `mr_dma_config_t`                                     | 校验并映射宽度/增量/突发/方向，静态绑定外设 request/trigger |
| 描述符与缓存管理   | 仅校验首节点 `count`/`src`/`dst` 非空                      | 解释 `transfer->next` 链表、维护缓冲区生命期与 cache 一致性 |
| 进度与状态         | 透传 `is_busy` / `offset`                                  | 返回硬件真实忙状态及源端字节进度                            |
| 中断与事件通知     | 校验通道后透传可选回调                                     | 管理硬件 IRQ、清除标志位并在 ISR 中唤醒 waiter              |

## 2. 操作表（ops）

backend 需实现 `include/mrX/drivers/dma/mr_dma.h` 中定义的 `mr_dma_ops_t`：

| 回调                    | 签名要点                                                                              | 必需 | 返回值约定                                            |
|-------------------------|---------------------------------------------------------------------------------------|------|-------------------------------------------------------|
| `channel_configure`     | `(const mr_driver_t *drv, mr_uint32_t channel, const mr_dma_config_t *config)`        | 是   | 成功返回 `0`，不支持的参数组合返回负数错误码          |
| `channel_configuration` | `(const mr_driver_t *drv, mr_uint32_t channel, mr_dma_config_t *config)`              | 是   | 成功返回 `0`，失败返回负数错误码                      |
| `start`                 | `(const mr_driver_t *drv, mr_uint32_t channel, const mr_dma_transfer_t *transfer)`    | 是   | 成功返回 `0`，通道忙或参数非法返回负数错误码          |
| `stop`                  | `(const mr_driver_t *drv, mr_uint32_t channel)`                                       | 是   | 确认硬件已停止访问后返回 `0`                          |
| `is_busy`               | `(const mr_driver_t *drv, mr_uint32_t channel, mr_bool_t *busy)`                      | 是   | 成功返回 `0`，`*busy` 表示真实硬件活动状态            |
| `offset`                | `(const mr_driver_t *drv, mr_uint32_t channel, mr_size_t *offset)`                    | 是   | 成功返回 `0`，`*offset` 为源端已传输字节数            |
| `event_attach`          | `(const mr_driver_t *drv, mr_uint32_t channel, mr_waiter_t *entry)`                   | 否   | 成功返回 `0`，不支持时留空（公共层返回 `-MR_ENOSYS`） |
| `event_control`         | `(const mr_driver_t *drv, mr_uint32_t channel, mr_uint32_t events, mr_bool_t enable)` | 否   | 成功返回 `0`，不支持时留空（公共层返回 `-MR_ENOSYS`） |

前 6 个基础回调在公共层通过 `MR_ASSERT` 强制非空；事件回调未实现时留空即可，公共层会返回
`-MR_ENOSYS`。

## 3. 关键契约

- **访问宽度与 item 计数**：`src_width`/`dst_width` 表示 DMA
  实际在源端和目的端发起的总线访问宽度，而非外设寄存器在 C 结构体中的声明宽度。
  `mr_dma_transfer_t.count` 统一表示逻辑传输项（item）次数；`offset`
  则需换算为源端字节进度（已完成 item 数乘以源端访问宽度），循环模式下应明确回绕语义。
- **缓冲区归属与 cache 一致性**：`start` 返回后 backend 即接管描述符与数据缓冲区，直至传输完成或
  `stop` 确认硬件已停止。涉及 D-Cache 的平台，启动前需对描述符及源缓冲区执行
  clean，传输完成或停止后对目的缓冲区执行 invalidate，并校验地址/长度对齐约束。
- **中断与事件上下文**：ISR 须先读取并清除硬件半传输/完成/错误中断标志，再通过等待队列通知已挂接的
  `mr_waiter_t`。回调同步执行于 ISR 上下文，不得阻塞。

## 4. HardTree 安装

使用 `MR_HT_DMA_INSTALL(_id, _priv, _ops)` 安装实例，宏会自动读取节点上的
`exist_mask` 属性（默认 `0xffffffffffffffffULL`）。外设 request/trigger 路由、IRQ
号、时钟及总线地址限制等平台私有信息由 backend 自行解析。

## 5. 验收要点

- 验证 `P2M`/`M2P`/`M2M` 方向、不同源/目的宽度、地址增量及硬件请求路由均按
  `mr_dma_config_t` 生效。
- 验证 `start` -> `is_busy`/`offset` -> `HALF`/`DONE`/`ERROR` 事件 -> `stop`
  全链路一致，且 `stop` 返回后硬件不再访问任何缓冲区。
- 验证越界通道（`-MR_EINVAL`）、不支持的配置组合与未实现事件（`-MR_ENOSYS`）返回正确错误码。
- 跑通 `test/drivers/dma/dma.c` 行为测试。

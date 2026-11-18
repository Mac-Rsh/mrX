# DMA 驱动移植指南

> 本文给出 DMA backend 的实现契约：ops 表与必需性、配置与 request/trigger
> 路由、descriptor 生命周期、busy/offset
> 语义、事件与 cache 协作、错误码、HardTree 属性和验收清单。公共 API
> 见 [DMA 驱动](dma.md)。

---

## 1. 移植边界

DMA 公共层负责设备与通道类型检查、基本参数校验和操作分发；硬件请求路由、
搬运配置、数据所有权、事件处理与缓存一致性由 backend 负责。

| 事项        | 公共层                                                                         | backend                                                         |
|-------------|--------------------------------------------------------------------------------|-----------------------------------------------------------------|
| 驱动类型    | `MR_DRIVER_TYPE_DMA` 校验、clazz 注册、动态实例释放 `mr_dma_data_t`            | 不实现 init/deinit；私有状态自行持有                            |
| 通道编号    | `exist_mask` 的 bit n 表示通道 n；`channel >= 64` 或该位为 0 返回 `-MR_EINVAL` | 把公共通道号稳定映射到硬件 channel/stream                       |
| 搬运配置    | 只透传 `mr_dma_config_t`，不判断硬件能否实现某种组合                           | 校验并映射 width/adjust/burst/direction，不可实现时返回明确错误 |
| 请求与触发  | 无接口                                                                         | 静态绑定 request/trigger，见第 3 节                             |
| descriptor  | 只校验首个 descriptor 的 `count`/`src`/`dst`，不复制、不遍历 `next`            | 解释 `next`，定义 descriptor 与 buffer 的有效生命期             |
| busy/offset | 透传                                                                           | 定义并保持稳定语义；UART/ADC/DAC stream 直接依赖                |
| 事件        | 提供事件位常量；校验通道后调用可选 ops                                         | 持有 waitable，在 ISR 中清标志后再 raise                        |
| 中断        | 不参与                                                                         | 拥有硬件中断源、中断控制器设置与完成回调                             |
| cache       | 不参与                                                                         | clean/invalidate 时机、粒度与边界维护                           |

建议实现位置：

```text
mrX/arch/<vendor>/<target>/platform/drivers/dma/dma.c
```

使用 HardTree 的实现可用 `MR_HT_HAS_DRIVER()` 限制实例展开；
资源表和中断处理器只应在实际启用对应设备时注册。

---

## 2. 操作表

`mr_dma_ops_t` 的完整成员如下。签名以 `<mrX/drivers/dma/mr_dma.h>`
为准，所有成员的第一个参数都是
`const mr_driver_t *drv`。

| ops 成员                | 签名要点                                               | 必需性 | 公共层在 NULL 时的行为                                                        |
|-------------------------|--------------------------------------------------------|--------|-------------------------------------------------------------------------------|
| `channel_configure`     | `(drv, channel, const mr_dma_config_t *config)`        | 必需   | `mr_dma_configure` 的 `MR_ASSERT` 失败，不返回错误码                          |
| `channel_configuration` | `(drv, channel, mr_dma_config_t *config)`              | 必需   | `mr_dma_configuration` 的 `MR_ASSERT` 失败                                    |
| `start`                 | `(drv, channel, const mr_dma_transfer_t *transfer)`    | 必需   | `mr_dma_start` 的 `MR_ASSERT` 失败                                            |
| `stop`                  | `(drv, channel)`                                       | 必需   | `mr_dma_stop` 的 `MR_ASSERT` 失败                                             |
| `is_busy`               | `(drv, channel, mr_bool_t *busy)`                      | 必需   | `mr_dma_is_busy` 的 `MR_ASSERT` 失败                                          |
| `offset`                | `(drv, channel, mr_size_t *offset)`                    | 必需   | `mr_dma_offset` 的 `MR_ASSERT` 失败                                           |
| `event_attach`          | `(drv, channel, mr_waiter_t *entry)`                   | 可选   | 先校验通道，通道非法返回 `-MR_EINVAL`；否则返回 `-MR_ENOSYS`                  |
| `event_control`         | `(drv, channel, mr_uint32_t events, mr_bool_t enable)` | 可选   | `mr_dma_event_enable/disable` 先校验通道（`-MR_EINVAL`），再返回 `-MR_ENOSYS` |

必需性说明：六个基础成员缺失时公共层是断言（编程前置条件），没有“降级或轮询替代”的路径；事件成员缺失时必须返回
`-MR_ENOSYS`，不要用软件轮询模拟。

公共层在分发前完成的校验（backend 可依赖，但不应把它当作运行时输入的完整校验）：

| 入口                                        | 前置校验                                                                     | 结果            |
|---------------------------------------------|------------------------------------------------------------------------------|-----------------|
| 全部通道入口                                | `MR_DRIVER_ASSERT(drv, MR_DRIVER_TYPE_DMA)`、`MR_DRIVER_DATA/OPS != MR_NULL` | 断言            |
| 全部通道入口                                | `channel >= 64` 或 `exist_mask` 对应位为 0                                   | `-MR_EINVAL`    |
| `mr_dma_configure` / `mr_dma_configuration` | `config != MR_NULL`                                                          | 断言            |
| `mr_dma_start`                              | `transfer != MR_NULL`、`count != 0`、`src != MR_NULL`、`dst != MR_NULL`      | 断言            |
| `mr_dma_start`                              | 不校验 `transfer->next`                                                      | 由 backend 决定 |
| `mr_dma_is_busy` / `mr_dma_offset`          | 输出指针 `busy` / `offset` 非空                                              | 断言            |
| `mr_dma_event_attach`                       | `entry != MR_NULL`                                                           | 断言            |
| `mr_dma_event_*`                            | `events` 掩码不做任何校验                                                    | 由 backend 决定 |

`exist_mask` 由 `mr_dma_data_t` 携带，backend 通过 `MR_DMA_DATA_INIT` /
`MR_HT_DMA_INSTALL` 提供；公共 API
没有运行时修改通道集合的入口。

---

## 3. 配置与路由

将 `mr_dma_config_t` 和 transfer descriptor 中的源、目的、数据宽度、方向、增量、请求及循环关系映射到目标 DMA 控制器。公共层不决定 request/trigger 的数值、控制器通道数量及物理连接；backend 应按目标硬件验证路由、地址及能力限制。

`src_width` / `dst_width` 描述的是 DMA 在源端和目的端实际发起的访问宽度，不是 C/CMSIS 结构体成员的声明宽度。比如寄存器成员可能声明为 `uint32_t`，但 ADC 单次结果、12-bit right-aligned DAC 数据或 7/8-bit UART 数据仍可能分别要求 16-bit、16-bit 或 8-bit DMA access；consumer 必须按外设的数据访问语义配置，不能从寄存器成员类型推导。

`mr_dma_transfer_t.count` 统一表示逻辑 transfer item 次数，不表示字节数。一次 item 在 source/destination 两端可以具有不同访问宽度，但 count 仍只增加 1；backend 负责把 item count 映射到目标硬件计数器或 block-size 单位。`mr_dma_offset` 继续返回 source-side 字节进度，因此 backend 应先得到已完成 item 数，再乘 source width 换算。

## 4. Transfer、busy 与 offset

`start` 成功后 backend 持有 transfer 所引用的缓冲区，直到完成或 `stop` 确认硬件已停止访问。对长度、数据宽度、地址对齐和描述符链接不支持的组合返回明确错误。`is_busy` 表示硬件真实活动状态；`offset` 按公共 DMA 契约报告已处理的数据进度，循环流必须明确回绕行为，不能把 DMA 硬件计数直接冒充字节数。

## 5. 中断、事件与缓存

ISR 先确认硬件完成/半完成/错误状态并清理硬件来源，再用公共 DMA 事件接口通知 waiter；同步回调不可阻塞。涉及缓存时，启动前保证描述符和源数据对 DMA 可见，结束后保证 CPU 能看到目的缓冲区内容。DMA 完成、传输停止以及失效路径都不得留下对已归还缓冲区的硬件访问。

## 6. 错误码

公共 API 的参数校验由公共层处理；backend 负责报告不支持的请求、通道占用、配置缺失、硬件传输失败与超范围。不要把运行时忙碌等同于永久不支持，也不要在无法确定传输已停止时返回成功。

## 7. HardTree 与安装

实例的通道、request/trigger 路由、IRQ、时钟和 DMA 可达内存均为平台属性，不能写成公共驱动的固定规则。使用公共的注册/安装宏连接 `mr_dma_ops_t`，具体属性名称与编码留在平台文档。

## 8. 移植验收

- [ ] 方向、宽度、递增、循环与请求路由均按照公共配置解释。
- [ ] 传输进度、HALF/COMPLETE/ERROR 事件及循环 offset 一致。
- [ ] 成功停止后硬件不再访问数据和描述符。
- [ ] 缺失路由、地址/缓存不满足条件或资源冲突时返回明确错误。

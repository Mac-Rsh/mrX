# DMA 驱动移植指南

> 本文给出 DMA backend 的实现契约：ops 表与必需性、配置与 request/trigger 路由、descriptor 生命周期、busy/offset
> 语义、事件与 cache 协作、错误码、HardTree 属性和验收清单。公共 API 见 [DMA 驱动](dma.md)。

---

## 1. 移植边界

DMA backend 把一个控制器（H5 上是一台 GPDMA）抽象成公共通道集合。公共层只统一通道存在性、搬运配置结构和调用接口，
其余全部归 backend。

| 事项        | 公共层                                                                         | backend                                                         |
|-------------|--------------------------------------------------------------------------------|-----------------------------------------------------------------|
| 驱动类型    | `MR_DRIVER_TYPE_DMA` 校验、clazz 注册、动态实例释放 `mr_dma_data_t`            | 不实现 init/deinit；私有状态自行持有                            |
| 通道编号    | `exist_mask` 的 bit n 表示通道 n；`channel >= 64` 或该位为 0 返回 `-MR_EINVAL` | 把公共通道号稳定映射到硬件 channel/stream                       |
| 搬运配置    | 只透传 `mr_dma_config_t`，不判断硬件能否实现某种组合                           | 校验并映射 width/adjust/burst/direction，不可实现时返回明确错误 |
| 请求与触发  | 无接口                                                                         | 静态绑定 request/trigger，见第 3 节                             |
| descriptor  | 只校验首个 descriptor 的 `count`/`src`/`dst`，不复制、不遍历 `next`            | 解释 `next`，定义 descriptor 与 buffer 的有效生命期             |
| busy/offset | 透传                                                                           | 定义并保持稳定语义；UART/ADC/DAC stream 直接依赖                |
| 事件        | 提供事件位常量；校验通道后调用可选 ops                                         | 持有 waitable，在 ISR 中清标志后再 raise                        |
| 中断        | 不参与                                                                         | 拥有 IRQ 向量、NVIC 配置与 HAL 回调                             |
| cache       | 不参与                                                                         | clean/invalidate 时机、粒度与边界维护                           |

建议实现位置：

```text
mrX/arch/<vendor>/<target>/platform/drivers/dma/dma.c
```

整个实现应包在 `MR_HT_HAS_DRIVER()` 内：模板没有该 compatible 时，该翻译单元不应残留 IRQ 符号、waitable 数组或按芯片手册
最大通道数展开的表（见 [ST 平台适配规范](../../arch/st/st.md) 第 3.2、3.3 节）。

---

## 2. 操作表

`mr_dma_ops_t` 的完整成员如下。签名以 `<mrX/drivers/dma/mr_dma.h>` 为准，所有成员的第一个参数都是
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

`exist_mask` 由 `mr_dma_data_t` 携带，backend 通过 `MR_DMA_DATA_INIT` / `MR_HT_DMA_INSTALL` 提供；公共 API
没有运行时修改通道集合的入口。

---

## 3. 配置映射与 request/trigger 路由

`mr_dma_config_t` 是位域结构，backend 必须把每个字段映射到控制器编码，并对不支持的值给出明确错误，而不是静默降级：

| 字段                      | 取值                                  | 建议处理                                                        |
|---------------------------|---------------------------------------|-----------------------------------------------------------------|
| `src_width` / `dst_width` | `MR_DMA_WIDTH_8/16/32/64`             | 8/16/32 支持；`MR_DMA_WIDTH_64` 不支持时返回 `-MR_ENOSYS`       |
| `src_adj` / `dst_adj`     | `MR_DMA_ADJ_INC/DEC/CONST`            | 不支持递减地址时返回 `-MR_ENOSYS`；`CONST` 表示地址固定         |
| `src_burst` / `dst_burst` | `MR_DMA_BURST_1` … `MR_DMA_BURST_256` | 超出硬件 burst 能力返回 `-MR_ENOSYS`；编码越界返回 `-MR_EINVAL` |
| `direction`               | `MR_DMA_DIR_P2M/M2P/M2M`              | 越界返回 `-MR_EINVAL`；M2M 通常走软件请求                       |

`width`、`burst` 是带位宽的位域，字段本身已限定了取值范围，因此“枚举越界”多数不可达；是否可达取决于字段宽度，
backend 仍应保留范围检查。

**request 与 trigger 路由**

- request（外设 DMA 请求线）与 trigger（外部触发门控）是同一通道的静态属性，不由单次传输参数描述。平台用 HardTree
  属性（第 7 节的 `st_requests`）或等价机制在安装时绑定。
- 只有 trigger 负责门控 DMA：EXTI event、定时器 TRGO 等 trigger 生产者的引脚复用、边沿和路由仍属于板级工程配置，
  DMA backend 不配置、不启停它们。
- 双向 SPI stream 应先启动未门控的 RX DMA，再启动被 trigger 门控的 TX DMA：TX 开始写外设数据寄存器后，RX 由外设
  request 自然推进。
- 未声明 trigger 的通道必须保持不门控，以兼容普通外设 request；旧模板只写 `<request>` 时行为不变。
- 普通寄存器传输路径与硬件链表传输路径必须使用同一份 trigger/request 绑定，不能让两条路径的路由不一致。
- M2M 不需要外设 request（H5 使用软件请求），但仍要求该公共通道有路由条目，否则通道不可用。

**公共层不校验芯片可达内存范围、地址对齐或 cache line 共享**。这些属于 backend 与板级集成责任；能校验的必须返回
明确错误（H5 依赖 HAL 与内存布局，见第 9 节）。

---

## 4. Transfer descriptor、busy 与 offset 语义

`mr_dma_transfer_t` 含 `next`、`count`、`src`、`dst`。backend 必须明确并在文档中写清：

- **`next` 的解释**：H5 的约定是 `next == MR_NULL` 表示单次（线性）传输，`next == transfer`（自引用）表示循环传输，
  其他非空值表示链式描述符，返回 `-MR_ENOSYS`。支持链表的 backend 必须说明链表由硬件直接遍历还是由 ISR 续接。
- **`count` 的单位与上限**：必须与 `offset` 的刻度一致（见下）。H5 把 `count` 直接写入 HAL 块长度，超过块长度上限
  返回 `-MR_ERANGE`。
- **生命期**：`start` 返回后硬件可能继续读取 descriptor 与 buffer；调用方在确认 `stop` 成功前不得复用或释放它们。
  H5 在 `start` 中把 `src`/`dst`/`count` 快照进私有状态，循环模式还会把链表节点交给硬件长期读取，因此节点所在对象
  必须与通道同生命周期。
- **`stop` 的完成语义**：`stop` 返回成功即表示硬件不再访问 descriptor/buffer。停止失败时调用方必须保留 buffer 与
  驱动对象并重试，不能把错误返回当作已停止（见 `mrX/arch/st/stm32h5/README.md` 的流传输小节）。
- **`is_busy`**：必须反映通道是否仍属于一次活动传输。H5 把 `HAL_DMA_STATE_BUSY` 和 `HAL_DMA_STATE_SUSPEND` 都视为忙，
  并且 `stop` 对 `SUSPEND` 返回 `-MR_EBUSY`（要求调用方先恢复或取消）。
- **`offset`**：单位必须在平台文档中固定，且与消费者 buffer 的刻度一致；UART/ADC/DAC stream 的进度、有限流的完成判断
  和 `stop` 后剩余数据量都建立在它之上。公共契约（`mr_dma_offset` 的头文件注释与 `dma.md`）固定为 **字节**，H5 backend 据此
  把硬件计数换算为字节：先取 `transfer->count - HAL 计数器`（数据项数），再乘以内存侧元素宽度（M2P 取 `src_width`，
  P2M/M2M 取 `dst_width`）。有限传输完成后等于 `count × 元素宽度`，循环传输先对 `count` 取模再换算，因此进度始终落在
  `[0, count × 元素宽度)` 内。
- **错误后的进度保留**：H5 的错误回调和 `stop` 都会保存当前进度，因 HAL `ERROR` 状态重建通道时也保留该进度，直到下一次
  `configure` 或 `start` 才清零。上层依赖这一点在错误后仍能报告已搬运气量。

---

## 5. 事件、中断顺序与 cache 协作

公共事件位为 `MR_DMA_WAIT_HALF`、`MR_DMA_WAIT_DONE`、`MR_DMA_WAIT_ERROR`。事件对象由 backend 提供：`event_attach`
把 waiter 接到目标通道的 waitable，`event_control` 只开关硬件来源，不生成事件。

**ISR 顺序与上下文**

1. 先处理/清除硬件状态并推进 HAL/backend 状态，再 raise 对应事件，避免电平型中断重复进入。
2. waiter 在 raise 所在上下文同步执行，因此 waiter 不得阻塞；HAL 的阻塞式停止（`HAL_DMA_Abort` 等）可能等待 HAL
   时基，不得在 waiter 回调里调用。
3. 错误检测与恢复不得依赖用户是否订阅 `MR_DMA_WAIT_ERROR`。

**中断使能规则**

| 通道形态                | 完成（TC）中断                                                  | 半完成（HT）中断 | 错误中断 |
|-------------------------|-----------------------------------------------------------------|------------------|----------|
| 有限传输                | 始终使能，用于完成 HAL 状态与 cache 收尾                        | 仅订阅 HALF 时   | 始终使能 |
| 循环传输，订阅 DONE     | 使能                                                            | 仅订阅 HALF 时   | 始终使能 |
| 循环传输，未订阅 DONE   | 仅当 cache 所有权维护需要时使能（H5 在 P2M/M2M 且订阅 HALF 时） | 仅订阅 HALF 时   | 始终使能 |
| 循环固定 TX，无完成订阅 | 不使能，避免每轮打断 CPU                                        | 仅订阅 HALF 时   | 始终使能 |

`event_control` 修改订阅后，若通道正忙应立即重算中断使能，否则新订阅要等到下一次 `start` 才生效。

**cache 协作**

- clean/invalidate 由 DMA backend 承担；调用方只负责内存布局：DMA 写入的 cacheable 区间不得与无关活跃数据共享 cache
  line，否则应放在不可 cache 的内存。
- 典型规则：M2P 传输前 clean 源；P2M 传输前 clean + invalidate 目标；M2M 对源 clean、对目标 clean + invalidate。循环传输
  在 HALF/DONE 边界维护“目标所有权区间”，并在同步 raise 返回后把用户在回调中改写的源区间重新 clean 给 DMA。
- 链表节点自身也在 cacheable 内存中，交硬件前需要 clean。
- 平台没有 D-cache 时这些逻辑应整体不编译，而不是留空函数。

**外设协作**：外设 DMA request 的开关顺序、error/abort 后的残余请求清理、供电/引脚状态由使用 DMA 的外设驱动与板级集成
共同负责，公共 DMA 层不做这些事。同一个 DMA 通道被某个 stream 独占的约束也在平台文档中声明。

---

## 6. 错误码约定

| 错误码                                    | 条件                                                                                                  |
|-------------------------------------------|-------------------------------------------------------------------------------------------------------|
| `-MR_EINVAL`                              | 通道超出控制器范围或未在路由表中；配置枚举越界；事件掩码为 0 或含未知位；路由条目缺失                 |
| `-MR_ENOSYS`                              | 硬件或本 backend 不实现该能力：64 位宽度、地址递减、过大的 burst、链式 `next`、事件能力缺失           |
| `-MR_EBUSY`                               | 通道忙时 `configure`/`start`；`stop` 遇到挂起（SUSPEND）状态                                          |
| `-MR_ENODEV`                              | 通道尚未 `configure`（HAL `RESET`）时调用 `configuration`/`start`/`stop`/`is_busy`/`offset`/`event_*` |
| `-MR_ERANGE`                              | `count` 超过控制器块长度上限；整数换算溢出                                                            |
| `-MR_EIO` / `-MR_ETIMEDOUT` / `-MR_EBUSY` | HAL 调用返回 `HAL_ERROR` / `HAL_TIMEOUT` / `HAL_BUSY` 时的映射                                        |

公共层自身的固定返回值：通道不存在是 `-MR_EINVAL`；可选事件 ops 缺失是 `-MR_ENOSYS`；其余 backend 错误原样透传。
“硬件具备但组合非法”用 `-MR_EINVAL`，“硬件不具备”用 `-MR_ENOSYS`，两者不要混用。

---

## 7. HardTree 属性

compatible 为 `st_stm32h5_gpdma`（一个实例对应一台 GPDMA 控制器）。

| 属性                                    | 要求                    | 含义                                               | 默认值                                                      |
|-----------------------------------------|-------------------------|----------------------------------------------------|-------------------------------------------------------------|
| `st_gpdmax`                             | 必填                    | GPDMA 控制器实例                                   | 无                                                          |
| `st_clken`                              | 必填                    | 控制器时钟使能 thunk                               | 无                                                          |
| `st_irq_priority`                       | 必填                    | 通道 NVIC 优先级                                   | 无                                                          |
| `st_requests`                           | 必填                    | 按下标对应硬件通道的 request/trigger 路由          | 无                                                          |
| `exist_mask`                            | 可选                    | 公共通道存在掩码                                   | 通用 `MR_HT_DMA_INSTALL` 为 64 位全 1；本 backend 为低 N 位 |
| `st_irq_handler_0` … `st_irq_handler_7` | 可选                    | 通道 IRQ 向量名（如 `GPDMA1_Channel0_IRQHandler`） | 缺失即不为该通道生成处理函数                                |
| `st_ctrl`                               | 声明任一通道 IRQ 时必填 | 控制器下标（0 = GPDMA1，1 = GPDMA2）               | 无                                                          |

`st_requests` 的单元布局：

| 下标 | 含义              | 默认值                     |
|------|-------------------|----------------------------|
| 0    | request           | 无（必填）                 |
| 1    | trigger mode      | `DMA_TRIGM_BLOCK_TRANSFER` |
| 2    | trigger polarity  | `DMA_TRIG_POLARITY_MASKED` |
| 3    | trigger selection | `0`                        |

- 数组下标即公共通道号，也即硬件通道下标（H5 上 0–7）；通道寄存器块由控制器基址加固定偏移推出。
- 通道数组长度等于 `st_requests` 的元素数；省略 `exist_mask` 时取低 N 位，N 为该元素数。
- 每个元素可以只写 `<request>`，此时后三项取默认值，旧模板行为不变。
- 声明了通道 IRQ 就必须提供 `st_ctrl`，否则中断处理函数无法定位控制器。

例如让通道 0 的 SPI TX 由 EXTI0 上升沿释放一个完整 block，通道 1 的 SPI RX 只由外设 request 推进：

```dts
st_requests = <<GPDMA1_REQUEST_SPI1_TX
                 DMA_TRIGM_BLOCK_TRANSFER
                 DMA_TRIG_POLARITY_RISING
                 GPDMA1_TRIGGER_EXTI_LINE0>
               <GPDMA1_REQUEST_SPI1_RX>>;
```

消费侧用 `[phandle, channel]` 两个单元构造 `mr_ht_dma_spec_t`（`MR_HT_DMA_SPEC` / `MR_HT_DMA_SPEC_FOREACH`）。
spec 只保存驱动与通道号，不表示独占权，通道分配冲突由板级集成保证。

安装宏只填充资源描述：HAL 初始化、时钟使能、NVIC 使能都发生在首次 `channel_configure`，重复配置必须幂等。

---

## 8. 参考实现

```text
mrX/arch/st/stm32h5/platform/drivers/dma/dma.c
```

值得阅读的函数（按它们解决的问题）：

| 函数                                                                                         | 看什么                                                       |
|----------------------------------------------------------------------------------------------|--------------------------------------------------------------|
| `dma_get_channel`                                                                            | 公共通道号到私有通道对象的边界校验                           |
| `dma_make_config`、`dma_width`、`dma_adjust`、`dma_burst`                                    | 枚举到 HAL 编码的映射与“不具备 vs 组合非法”的错误码区分      |
| `dma_request`、`dma_trigger`                                                                 | request/trigger 静态路由的取用与缺失处理                     |
| `dma_set_mode`、`dma_init_linear`、`dma_build_linked_list`                                   | 线性与循环两条传输路径的一致初始化                           |
| `dma_apply_events`                                                                           | 订阅、完成/半完成中断与“错误中断始终使能”的重算规则          |
| `dma_current_offset`                                                                         | offset 的计算与循环取模                                      |
| `dma_transfer_complete`、`dma_half_complete`、`dma_transfer_error`                           | 有限/循环的进度保存、事件 raise 顺序与 cache 收尾            |
| `dma_cache_prepare`、`dma_cache_complete`、`dma_cache_invalidate_dst`、`dma_cache_clean_src` | cache 所有权区间在传输、HALF、DONE、ERROR 各边界的维护       |
| `dma_stop`                                                                                   | 忙/挂起/错误三种状态的不同处理，以及错误后重建通道并保留进度 |
| `dma_irq_handle`、`ST_HT_DMA_INSTALL`、`__dma_ops`                                           | IRQ 表与 ops 表如何从 HardTree 展开                          |

运行期语义（offset 单位、错误后进度保留、流停止失败保留状态、中断使能规则）另见
`mrX/arch/st/stm32h5/README.md` 的“流传输与资源生命周期”一节。

---

## 9. STM32H5 能力边界（新 port 必须重新确认，不要直接继承）

| 能力/约定        | H5 现状                                                                                 | 新 port 需要确认                                     |
|------------------|-----------------------------------------------------------------------------------------|------------------------------------------------------|
| 通道编号         | 公共通道号 = 硬件通道下标，单控制器 8 通道                                              | 通道数与编号方式；是否需要非连续映射                 |
| 多控制器         | GPDMA1/GPDMA2，用 `st_ctrl` 选择 IRQ 表                                                 | 是否存在多个控制器、IRQ 表如何组织                   |
| 宽度             | 8/16/32 位；64 位返回 `-MR_ENOSYS`                                                      | 支持的宽度与 64 位能力                               |
| 地址调整         | 递增与固定；递减返回 `-MR_ENOSYS`                                                       | 是否支持递减                                         |
| burst            | 1–64；128/256 返回 `-MR_ENOSYS`                                                         | 支持的最大 burst                                     |
| 链式描述符       | 仅 `next == MR_NULL`（线性）与 `next == transfer`（循环）                               | 是否需要链式 `next`，由硬件还是 ISR 续接             |
| `count` 上限     | 直接写入 HAL 块长度，超过 `DMA_CBR1_BNDT` 字段范围时返回 `-MR_ERANGE`                   | 控制器块长度上限及其单位                             |
| offset 刻度      | 字节数：`已完成数据项数 × 内存侧元素宽度`（M2P 取 `src_width`，P2M/M2M 取 `dst_width`） | 元素宽度与字节换算方式，必须与公共契约的字节进度一致 |
| 挂起语义         | `BUSY`/`SUSPEND` 都算忙；`stop` 对 `SUSPEND` 返回 `-MR_EBUSY`                           | 是否有挂起状态及其恢复方式                           |
| HAL `ERROR` 状态 | `stop` 重新配置通道并保留停止进度                                                       | 错误后的状态机与恢复路径                             |
| trigger 门控     | 只门控 DMA；生产者引脚/边沿/路由属板级配置                                              | trigger 生产者由谁配置、是否有硬件互锁               |
| M2M 请求         | 使用软件请求，仍要求通道有路由条目                                                      | 内存到内存是否需要独立通路                           |
| cache            | 有 D-cache 时由 backend 维护所有权区间                                                  | 是否有 D-cache、维护粒度与不可 DMA 区域              |
| IRQ 生成         | 仅对声明了 `st_irq_handler_n` 的通道生成处理函数                                        | 是否需要共享向量、如何使用 `st_ctrl` 之外的定位方式  |

任何一条“H5 支持/不支持”都不能当作通用结论写进新 backend 的文档。

---

## 10. 移植验收清单

- [ ] 模板无该 compatible 时，翻译单元被 `MR_HT_HAS_DRIVER()` 裁空，不残留 IRQ 符号、waitable、按手册最大通道数展开的表。
- [ ] 八个 ops 成员全部按第 2 节实现或置 `MR_NULL`；必需成员不为空，可选成员缺失时确实返回 `-MR_ENOSYS`。
- [ ] `exist_mask`、公共通道编号与硬件通道映射稳定，越界和不存在通道返回 `-MR_EINVAL`。
- [ ] width/adjust/burst/direction 映射正确，“不具备”与“组合非法”分别返回 `-MR_ENOSYS` 与 `-MR_EINVAL`。
- [ ] request/trigger 静态路由对线性与链表两条路径一致；未声明 trigger 的通道保持不门控。
- [ ] `next` 的三种形态（`MR_NULL`/自引用/链式）语义明确，不支持时返回 `-MR_ENOSYS`。
- [ ] `count` 单位、上限与 `offset` 刻度一致，并在平台文档中写明。
- [ ] `start` 后 descriptor/buffer 的有效期、`stop` 成功后的“硬件不再访问”保证都已在文档中声明。
- [ ] `is_busy` 覆盖所有活动状态；`offset` 在有限、循环、停止后、错误后的取值都有明确规则。
- [ ] HALF/DONE/ERROR 能正确清硬件状态后再 raise；错误恢复不依赖 ERROR 订阅。
- [ ] 完成/半完成中断使能符合第 5 节规则；循环无订阅路径不会每轮打断 CPU。
- [ ] cache clean/invalidate 时机与粒度正确；无 D-cache 时相关逻辑不编译。
- [ ] `stop` 失败时保留活动状态与进度，调用方可重试；不在 waiter 上下文调用阻塞式 HAL 停止。
- [ ] HardTree 属性表、`st_requests` 单元布局与默认值已在平台 README 中登记；时钟、HAL init、NVIC 在首次使用路径完成且幂等。
- [ ] 第 9 节的每一项能力都在新平台文档中重新确认，没有照抄 STM32H5 的结论。

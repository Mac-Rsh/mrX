# DAC 驱动移植指南

> 本文面向平台 backend 开发者，说明如何为新 MCU/SoC 实现 DAC backend：操作表、
> 通道与缓冲模型、普通输出、stream 数据通路与 DONE 语义、HardTree 属性与验收要点。
> 公共 API 与电压换算的使用方式见 [DAC](dac.md)，应用侧的组合接线见
> [驱动组合与进阶示例](../examples.md)。

## 1. 移植边界

DAC 公共层负责类型化 API、`exist_mask` 门控、`mr_dac_data_t.event` 的
init/deinit、可选回调的 `-MR_ENOSYS` 降级和电压到原始码的换算；backend 负责把统一
配置映射到真实 DAC 外设、时钟与模拟引脚、普通输出、可选的 stream 数据通路、外部
启动输入和事件来源。

| 关注点                                                                  | 负责方                                     |
|-------------------------------------------------------------------------|--------------------------------------------|
| `mr_dac_data_t.event` 的 init/deinit                                    | 公共层（clazz 的 init/deinit）             |
| 通道存在性校验（`channel >= 32` 或 `exist_mask` 未置位 → `-MR_EINVAL`） | 公共层，仅在 configure/configuration       |
| `resolution` 1~32、`buf`/`size` 一致性断言                              | 公共层                                     |
| `ref_internal`、`exist_mask` 的取值                                     | backend（HardTree 属性或手工初始化）       |
| 逻辑通道到硬件通道/引脚的映射、参考源与能力裁剪                         | backend                                    |
| 普通输出的数据搬运与同步方式                                            | backend                                    |
| stream 数据通路、buffer 所有权、DMA/IRQ 选择                            | backend                                    |
| 有限流最终传送完成的确认（DONE 语义）                                   | backend                                    |
| `event_control` 的事件掩码与真实中断源开关                              | backend                                    |
| 外部触发生产者（TMR/PWM）的配置与启停                                   | 应用或生产者 backend，**不是** DAC backend |

平台实现通常位于：

```text
mrX/arch/<vendor>/<target>/platform/drivers/dac/
```

backend 私有数据通常保存 HAL handle、时钟使能函数、通道表、实例参考源、事件掩码和
stream 状态。

## 2. 操作表

backend 实现 `mrX/include/mrX/drivers/dac/mr_dac.h` 中的 `mr_dac_ops_t`。签名只列参数
与返回值要点，公共 API 的说明见 [DAC](dac.md)。

基础能力（全部必需，公共层入口对空回调使用断言）：

| 成员                    | 签名要点                                                                                    | 必需 |
|-------------------------|---------------------------------------------------------------------------------------------|------|
| `channel_configure`     | `(const mr_driver_t *drv, mr_uint32_t channel, const mr_dac_config_t *config)` → `mr_err_t` | 是   |
| `channel_configuration` | `(const mr_driver_t *drv, mr_uint32_t channel, mr_dac_config_t *config)` → `mr_err_t`       | 是   |
| `write`                 | `(const mr_driver_t *drv, mr_dac_seq_t *seq)` → `mr_ssize_t`（字节数或负数错误码）          | 是   |

启用 `MR_USE_DRIVER_DAC_STREAM` 后声明（四个成员彼此独立，可单独缺省）：

| 成员             | 签名要点                                                                      | 必需 | 空回调时公共 API 返回 |
|------------------|-------------------------------------------------------------------------------|------|-----------------------|
| `stream_start`   | `(drv, mr_dac_seq_t *seq, const mr_dac_stream_config_t *config)` → `mr_err_t` | 否   | `-MR_ENOSYS`          |
| `stream_stop`    | `(drv)` → `mr_err_t`                                                          | 否   | `-MR_ENOSYS`          |
| `stream_is_busy` | `(drv, mr_bool_t *busy)` → `mr_err_t`                                         | 否   | `-MR_ENOSYS`          |
| `stream_offset`  | `(drv, mr_size_t *offset)` → `mr_err_t`                                       | 否   | `-MR_ENOSYS`          |
| `event_control`  | `(drv, mr_uint32_t events, mr_bool_t enable)` → `mr_err_t`                    | 否   | `-MR_ENOSYS`          |

约定：

- 可选成员缺失时公共层返回 `-MR_ENOSYS`，不会用空操作兜底；缺哪个成员就只影响对应
  API，其余能力仍可用。
- 公共层不建立 stream 状态机、不校验 `seq->channels`、不校验 `events` mask，也不
  规定 backend 对未知事件返回什么错误。事件位由公共头文件定义，具体支持哪些、
  如何映射到中断源由 backend 决定。
- 公共层不切换上下文：`ops` 同步运行在调用者上下文；从 ISR 路径触发只能通过
  `mr_dac_event_raise` 通知 waiter，不能阻塞。
- shutdown 时公共层会调用已实现的 `stream_stop`；停止失败且驱动未处于删除流程时
  返回错误并保留 waitable 以便重试，成功后才销毁 waitable。
- `mr_dac_event_attach` 只把 waiter 挂到 `mr_dac_data_t.event`，不打开硬件来源；
  `enable`/`disable` 才经由 `event_control` 开关中断源。两者是独立步骤。

## 3. 数据与通道模型

`mr_dac_data_t` 由 backend 提供：

| 字段           | 含义                      | backend 义务                                                              |
|----------------|---------------------------|---------------------------------------------------------------------------|
| `ref_internal` | 内部参考标称值，单位 mV   | 与真实参考一致；`mr_dac_ref_internal()` 与 HardTree 换算回退依赖它        |
| `exist_mask`   | bit n 表示逻辑通道 n 存在 | configure/configuration 的唯一门控；普通 `write` 与 stream 会另行校验掩码 |
| `event`        | 公共 waitable             | 只由公共层 init/del；backend 通过 `mr_dac_event_raise()` 触发             |

通道号是 0~31 的位号。backend 必须固定两件事：

- 逻辑通道到硬件通道/引脚的映射——HardTree `st_channels` 的数组下标就是逻辑通道号；
- 实例支持的通道数上界（STM32H5 为 2），掩码中出现不存在的通道时返回 `-MR_EINVAL`。

`mr_dac_seq_t` 的解释义务：

| 字段          | 公共层约束                           | backend 必须定义                                                                             |
|---------------|--------------------------------------|----------------------------------------------------------------------------------------------|
| `channels`    | 无（公共层只读取）                   | 位掩码语义（bit n = 逻辑通道 n）；普通 `write` 的多通道交错顺序；stream 是否允许一个以上通道 |
| `resolution`  | 断言 1~32                            | 支持的位数集合；码值存储宽度（是否固定为某个宽度）                                           |
| `size`、`buf` | 断言 `buf != MR_NULL` 或 `size == 0` | 单样本字节数；缓冲是否需要对齐；`size` 是否必须是通道数或 frame 的整数倍                     |

`channel_configuration` 必须返回 backend 实际采用的配置，而不是简单回显用户请求。
`mr_dac_config_t` 当前只有 `ref` 一个字段：即使实例只支持一种参考源，也应按实际
取值回填（STM32H5 直接返回实例参考源，未 configure 过的通道同样返回值）。

## 4. 普通输出与硬件接线

普通 `write` 是同步接口，公共层只做断言；通道掩码、样本格式、交错顺序和错误语义
完全由 backend 定义。移植时必须在平台文档中写明：

- 时钟、GPIO 模拟模式、参考源由谁配置；backend 不直接写 GPIO 寄存器，模拟引脚由
  GPIO owner helper 配成 analog；
- 样本在缓冲中的宽度、对齐方式与硬件数据寄存器的对应关系；
- 多通道掩码下的输出顺序（逐个通道同步更新，还是按样本交错轮转）；
- 输出是否立即生效，还是需要额外触发/装载动作；
- 中途失败时返回已写字节数还是错误码；
- 是否与 stream 互斥；
- 普通 `write` 不应启动 DMA、外部触发或信号生产者。

公共层不会替 backend 启动外部信号生产者或 DMA。

## 5. Stream、数据通路与事件

### 5.1 启动来源

- `MR_DAC_STREAM_SOURCE_EXTERNAL`：`stream_start` 只把 DAC 配置并武装为外部触发
  模式，每次更新由静态选定的信号启动。外部信号的来源、路由和边沿来自 HardTree
  （`st_trigger`），不进入公共 stream 配置。
- `MR_DAC_STREAM_SOURCE_SOFTWARE`：backend 必须有一个隐式时基才能自行推进输出。
  STM32H5 不提供这样的时基，因此软件源直接返回 `-MR_ENOSYS`。移植到新平台时必须
  显式决定：要么提供真正的时基并写清节拍来源与范围，要么同样返回 `-MR_ENOSYS`；
  不要用忙等或隐式延时假装支持。
- `circular` 只决定 buffer 读完后是否回绕，与启动来源相互独立。

生产者所有权与顺序约束（换平台时必须重新确认，不要继承 STM32H5 的结论）：

- DAC backend 不配置、不启停 TMR/PWM 等信号生产者；生产者由应用侧配置和启停。
- TIM 的中性 TRGO 配置在软件 UG 时仍可能产生一次脉冲，因此 **先完成生产者重配，再
  武装 DAC stream**；把 producer signal 关掉不等于消费者侧的硬件隔离。

### 5.2 数据通路选择

| 条件                                 | 数据通路 | 额外要求                                                                                   |
|--------------------------------------|----------|--------------------------------------------------------------------------------------------|
| 实例提供 `st_dma` 且 DMA 驱动已启用  | DMA      | 有限流仍必须提供 `st_irqn` 与 `st_irq_handler` 以确认最终传送；该 DMA 通道由本 stream 独占 |
| 未提供 `st_dma`（或未启用 DMA 驱动） | DAC 中断 | 只支持单样本有限流（`size` 等于单样本字节数且 non-circular），否则 `-MR_ENOSYS`            |

circular 流在 STM32H5 上必须有 DMA；无 DMA 时 `stream_start` 返回 `-MR_ENOSYS`。
有限流必须有 underrun 中断资源（`st_irqn` 与 `st_irq_handler` 同时存在），否则返回
`-MR_ENODEV`——这是第 5.3 节 DONE 语义的硬件前提。

### 5.3 DONE 语义（重点）

- 有限流的 `MR_DAC_WAIT_STREAM_DONE` 表示 **最后一个样本已经由外部信号送入 DOR**，
  不等同于 DMA 传输完成（TC）。把 DMA TC 当作输出完成会让最后一段波形少一个样本。
- 有 DMA 的有限流：DMA 完成本轮 buffer 后只是把流置为“等待最终传送”状态，并不
  触发 DONE；最后一个样本经外部信号进入 DOR 之后，由 DAC underrun 中断触发 DONE。
  若 underrun 在“仍需继续供数”的状态下出现，说明确实供数不及，此时应触发
  `MR_DAC_WAIT_STREAM_ERROR` 而不是 DONE。
- 无 DMA 的有限流：先把唯一样本写入 DHR，外部信号完成一次 DHR→DOR 传送，随后
  的 underrun 中断触发 DONE。
- circular 流：每轮 DMA 完成（buffer 回绕）时触发 DONE；HALF 在 buffer 能均分两半
  时由 DMA 半完成触发。
- 有限流如果只有一个样本且带 DMA，可以不启动 DMA 传输，只做预装并等待 underrun
  中断确认，效果与无 DMA 的单样本有限流一致。

### 5.4 事件

| 事件位                     | backend 义务                                                              |
|----------------------------|---------------------------------------------------------------------------|
| `MR_DAC_WAIT_STREAM_HALF`  | buffer 前半消耗完时触发（仅 HALF 可行且已订阅时）                         |
| `MR_DAC_WAIT_STREAM_DONE`  | 本轮输出结束：finite 在最后样本进入 DOR 后触发，circular 在每轮回绕时触发 |
| `MR_DAC_WAIT_STREAM_ERROR` | underrun 或 DMA 错误导致本轮未按预期完成时触发                            |

`event_control(events, enable)` 维护 backend 事件掩码并据此开关真实中断源，建议：

- `events == 0` 或包含未定义位时返回 `-MR_EINVAL`；
- 流活动期间禁止关闭 `ERROR`；非 circular 流还禁止关闭 `DONE`；`HALF` 允许在活动
  期间关闭。以上冲突返回 `-MR_EBUSY`；
- 关闭通知不等于停止硬件：停止输出只能靠 `stream_stop`，不能靠关中断冒充。

ISR 中应同时判断硬件 flag 与 interrupt source，先清除或屏蔽会持续触发的状态，再
更新 stream 状态、聚合事件位、调用 `mr_dac_event_raise`。waiter 同步运行在 raise 的
调用链中，ISR 不应留下无人消费且持续触发的电平型来源。

### 5.5 busy、offset 与 stop

- `stream_is_busy` 成功时通过 `busy` 输出状态；失败时 `busy` 不可用。若 backend 在
  判定有限流已完成时顺带执行内部停止，该 API 也可能返回停止失败的错误码；circular
  流则通常查询 DMA 状态，缺少 DMA 资源时返回 `-MR_ENODEV`。
- `stream_offset` 的单位是 **字节**，表示已从 buffer 消耗的字节数。DMA 通路通常还要
  加上预装样本的长度（有限流为保证第一次外部触发就有数据，需要先把首样本写入
  DHR）；circular 流不做预装。单位和回绕/终值时机必须写死。
- `stream_stop` 的幂等性由 backend 定义。失败时必须保留活动状态、DMA waiter 和
  “等待最终传送”标记，调用方继续保有 buffer、驱动和关联资源并重试； **不能**把
  错误返回理解为硬件已经停止。只有 stop 成功后才释放 waiter 与 stream 状态
  （ST 实现在失败路径提前返回、不清理）。
- 在普通调用上下文执行 stop：DMA 的阻塞停止可能依赖 HAL 时基，不应在同步事件回调
  的 IRQ 上下文执行。
- 有 D-cache 的平台需按平台 cache 契约处理源 buffer 的 clean 与必要屏障。

### 5.6 错误码

普通 `write`：

| 条件                                                                                                    | 返回                             |
|---------------------------------------------------------------------------------------------------------|----------------------------------|
| `resolution` 不等于 backend 支持值                                                                      | `-MR_EINVAL`                     |
| `size` 不是单样本字节数的整数倍、`channels == 0`、掩码含不存在的通道、`size` 超出 `mr_ssize_t` 表示范围 | `-MR_EINVAL`                     |
| 样本数不是选中通道数的整数倍                                                                            | `-MR_EINVAL`                     |
| 任一样本超出 backend 支持的码值范围                                                                     | `-MR_EINVAL`                     |
| `size == 0`（且通道掩码有效）                                                                           | `0`                              |
| 已有活动流                                                                                              | `-MR_EBUSY`                      |
| 中途失败且已写入至少一个样本                                                                            | 返回已写入的字节数（不是错误码） |
| 中途失败且尚未写入任何样本                                                                              | 对应的错误码                     |

`stream_start`：

| 条件                                                                                                            | 返回                                      |
|-----------------------------------------------------------------------------------------------------------------|-------------------------------------------|
| 已编译 `MR_USE_DRIVER_DAC_STREAM` 但对应 stream 回调为空（未编译时这些 API 不存在）                             | `-MR_ENOSYS`（公共层）                    |
| `source == MR_DAC_STREAM_SOURCE_SOFTWARE`（backend 无隐式时基）                                                 | `-MR_ENOSYS`                              |
| 有限流（non-circular）缺少 `st_irqn` 或 `st_irq_handler`                                                        | `-MR_ENODEV`                              |
| `source == MR_DAC_STREAM_SOURCE_EXTERNAL` 但无 `st_trigger`                                                     | `-MR_ENODEV`                              |
| `buf == MR_NULL`、`size == 0`、`resolution` 不是支持值、`size` 不是单样本字节数的整数倍、`buf` 未按样本宽度对齐 | `-MR_EINVAL`                              |
| `channels == 0`、含不存在的通道、或选中一个以上通道                                                             | `-MR_EINVAL`                              |
| 任一样本超出 backend 支持的码值范围                                                                             | `-MR_EINVAL`                              |
| 无 DMA 且样本数不是 1（无 DMA 只支持单样本有限流）                                                              | `-MR_ENOSYS`                              |
| 无 DMA 且 `circular` 为真                                                                                       | `-MR_ENOSYS`                              |
| 已有活动流                                                                                                      | `-MR_EBUSY`                               |
| HAL 返回 `HAL_BUSY` / `HAL_TIMEOUT` / 其他错误                                                                  | `-MR_EBUSY` / `-MR_ETIMEDOUT` / `-MR_EIO` |

`-MR_ENODEV` 表示静态资源缺失（可在板级补齐），`-MR_ENOSYS` 表示该 backend 未实现
该能力（应换实例或换路径），两者不要混用。校验顺序也应在平台文档中写清：STM32H5
先判定 `source`，再判定有限流的 IRQ 资源与 buffer 参数，无 DMA 的样本数限制先于
码值范围检查，因此同一个非法请求可能命中不同的错误码。

## 6. HardTree 属性与安装

STM32H5 backend（compat `st_stm32h5_dac`）读取的属性：

| 属性              | 必需 | 说明                                                                           | 缺省                                 |
|-------------------|------|--------------------------------------------------------------------------------|--------------------------------------|
| `st_dacx`         | 是   | DAC 实例寄存器基址                                                             | —                                    |
| `st_clken`        | 是   | 外设时钟使能函数                                                               | —                                    |
| `st_channels`     | 是   | 通道数组；数组下标即公共逻辑通道号，长度即实例通道数（1~2）                    | —                                    |
| `st_ref`          | 否   | 实例参考源；`channel_configure` 的 `config->ref` 必须等于它，否则 `-MR_EINVAL` | `MR_DAC_REF_INTERNAL`                |
| `st_trigger`      | 否   | `MR_DAC_STREAM_SOURCE_EXTERNAL` 使用的触发选择器                               | 无（EXTERNAL 启动返回 `-MR_ENODEV`） |
| `st_dma`          | 否   | stream 使用的 DMA spec；存在且启用 DMA 驱动时走 DMA 通路，该通道被 stream 独占 | 无（只支持单样本有限流）             |
| `st_irqn`         | 否   | 中断号；与 `st_irq_handler` 同时存在才构成有效 IRQ 资源；有限流必需            | 无                                   |
| `st_irq_priority` | 否   | NVIC 优先级                                                                    | `0`                                  |
| `st_irq_handler`  | 否   | 该实例的向量函数名，backend 据此生成中断处理函数                               | 无                                   |
| `ref_internal`    | 否   | 公共 `mr_dac_data_t.ref_internal`（mV）                                        | `3300`                               |

`st_channels` 的每个单元是 `<hal_dac_channel, gpio_phandle, pin>`：

- 单元下标是公共逻辑通道号，公共 API 的 `channel` 参数就按它解释；
- `hal_dac_channel` 是该平台 HAL 的通道常量（STM32H5 为 `DAC_CHANNEL_1`/`DAC_CHANNEL_2`），
  由 backend 使用，不暴露给公共层；同一实例内重复的 HAL 通道返回 `-MR_EINVAL`；
- `gpio_phandle` 指向 GPIO 实例，`pin` 是引脚号；`gpio_phandle` 解析不到驱动时返回
  `-MR_ENODEV`，引脚不落在对应 GPIO 的 `exist_mask` 里时返回 `-MR_EINVAL`，DAC 直接
  透传这些错误；DAC 只把引脚配成 analog，不配置复用功能。

安装约定：

- 公共头文件提供 `MR_HT_DAC_INSTALL` 用于按 compat 实例批量安装；STM32H5 参考实现
  没有使用它，而是自行构造 `mr_dac_data_t`，把 `exist_mask` 推导为
  `st_channels` 长度的低位掩码（因此在该实现里 `exist_mask` 属性不生效，而
  `ref_internal` 属性仍被读取）。新 backend 若改用 `MR_HT_DAC_INSTALL`，必须自己
  保证 `exist_mask` 与 `st_channels` 长度一致，否则公共层的通道门控会与实例真实
  通道不符；
- 与 stream 相关的属性（`st_dma`、`st_trigger`、`st_irqn`、`st_irq_priority`、
  `st_irq_handler`）只在 `MR_USE_DRIVER_DAC_STREAM` 下展开；`st_dma` 还需要
  `MR_USE_DRIVER_DMA` 打开才会被读取；
- 没有对应 compat 实例时 backend 会被 `MR_HT_HAS_DRIVER()` 裁掉；
- HardTree 只承载静态硬件资源和连接关系，运行状态（busy、offset、event mask、
  draining 标记）留在 backend 私有数据中。

## 7. 参考实现与芯片能力边界

参考实现：

```text
mrX/arch/st/stm32h5/platform/drivers/dac/dac.c
```

值得按其职责阅读的函数（只在本文列出名字，具体实现以源码为准）：

| 职责               | 函数                                                                                                                                                                           |
|--------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 通道配置与能力裁剪 | `dac_hw_init`、`dac_gpio_configure`、`dac_config_trigger`、`dac_channel_init`、`dac_channel_configure`、`dac_channel_configuration`、`dac_channel_start`                       |
| 普通输出           | `dac_write`、`dac_seq_channels`                                                                                                                                                |
| stream 数据通路    | `dac_channel_trigger_configure`、`dac_stream_start_impl`、`dac_stream_stop_impl`、`dac_stream_is_busy_impl`、`dac_stream_offset_impl`、`dac_stream_halt`、`dac_stream_release` |
| 事件与 ISR         | `dac_dma_event`、`dac_irq`、`dac_event_control`                                                                                                                                |
| HardTree 实例安装  | `ST_HT_DAC_INSTALL`、`__ST_HT_DAC_CHANNEL_IMPL`、`__ST_HT_DAC_STREAM`                                                                                                          |

STM32H5 能力边界（实现新 backend 时按目标芯片重新确认，不要默认继承）：

- 软件源不支持：`MR_DAC_STREAM_SOURCE_SOFTWARE` 在 `stream_start` 的最前面就返回
  `-MR_ENOSYS`，该平台不为软件源提供隐式时基；
- 无 DMA 时只支持单样本有限流；`circular` 必须有 DMA，否则 `-MR_ENOSYS`；
- 有限流必须有 `st_irqn` 与 `st_irq_handler`，用 DAC underrun 中断确认最后一次
  DHR→DOR 传送，DONE 因此不等于 DMA TC；underrun 在非等待最终传送状态下视为
  `ERROR`；
- `resolution` 只支持 12 位，其他取值返回 `-MR_EINVAL`；样本必须按 12 位右对齐存放，
  超过 `0x0FFF` 返回 `-MR_EINVAL`；
- stream 一次只能输出一个通道：`seq->channels` 必须是 2 的幂，多通道返回
  `-MR_EINVAL`；普通 `write` 支持多通道并按升序通道轮转交错样本；
- 实例只支持 1~2 个通道；通道数为 0 或超过 2、或 HAL 通道非法/重复时返回
  `-MR_ENODEV` / `-MR_EINVAL`；
- `mr_dac_config_t` 只有 `ref`：`channel_configure` 只接受 `config->ref == st_ref`，
  其他取值返回 `-MR_EINVAL`；重新配置通道会把该通道以无触发模式重新初始化，并停止
  该通道已经在运行的输出；
- `exist_mask` 由 `st_channels` 长度推导，`exist_mask` 属性在该实现里不生效；
- 事件掩码只放开已订阅的位；活动期间关闭 `ERROR`（以及非 circular 的 `DONE`）
  返回 `-MR_EBUSY`；
- 该 backend 从不配置或启停 TMR/PWM 生产者，也不把 signal disable 当作硬件隔离。

## 8. 移植验收清单

- `configure`/`configuration` 对每个字段都能稳定往返 backend 实际采用的配置；未配置
  过的通道也能返回确定值。
- `write` 的样本宽度、对齐与交错顺序已写死，并与公共换算函数（`resolution`、参考
  电压）的假设一致。
- `write` 中途失败时，已写入至少一个样本就返回已写字节数，尚未写入才返回错误码
  （或明确写出本平台的相反约定）。
- stream 回调可以独立缺省，缺省时只有对应 API 返回 `-MR_ENOSYS`。
- 软件源得到明确结论：要么提供真实时基并写明节拍，要么返回 `-MR_ENOSYS`；不得用
  忙等假装支持。
- 无 DMA 的能力边界明确（本平台只支持单样本有限流）；有限流缺 underrun 中断资源
  返回 `-MR_ENODEV`；`EXTERNAL` 源缺触发选择器返回 `-MR_ENODEV`。
- `size`/`buf` 对齐、样本宽度、样本数与通道数关系、码值范围校验齐全，非法组合返回
  `-MR_EINVAL`。
- `DONE` 确实代表最后一个样本进入 DOR，不与 DMA TC 混淆；circular 的 DONE 表示
  一轮回绕。
- `stream_offset` 的单位、预装偏移与回绕/终值时机已写死（本平台为字节）。
- `event_control` 的活动期禁用规则明确；关闭通知不等于停止硬件。
- `stream_stop` 失败时保留可重试的活动状态、DMA waiter 与调用方资源；成功后才释放。
- DMA 通道的所有权独占与 stop 顺序（先停触发与 DAC 通道，再停 DMA，最后释放）明确；
  有 cache 的平台按契约处理源 buffer 一致性。
- 外部触发生产者先重配、再武装 stream；backend 不配置也不启停生产者。
- HardTree 属性齐全且与板级一致；`st_channels` 下标与公共通道号一致，模拟引脚落在
  对应 GPIO 的 `exist_mask` 内，`exist_mask` 与实例真实通道一致。

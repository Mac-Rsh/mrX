# ADC 驱动移植指南

> 本文面向平台 backend 开发者，说明如何为新 MCU/SoC 实现 ADC backend：操作表、
> 通道与缓冲模型、普通采样、stream 数据通路、HardTree 属性与验收要点。公共 API、
> 序列语义与电压换算的使用方式见 [ADC](adc.md)，应用侧的组合接线见
> [驱动组合与进阶示例](../examples.md)。

## 1. 移植边界

ADC 公共层负责类型化 API、`exist_mask` 门控、`mr_adc_data_t.event` 的
init/deinit、可选回调的 `-MR_ENOSYS` 降级和电压换算；backend 负责把统一配置映射到
真实 ADC 外设、时钟与模拟引脚、普通采样、可选的 stream 数据通路和事件来源。

| 关注点                                                                  | 负责方                                     |
|-------------------------------------------------------------------------|--------------------------------------------|
| `mr_adc_data_t.event` 的 init/deinit                                    | 公共层（clazz 的 init/deinit）             |
| 通道存在性校验（`channel >= 32` 或 `exist_mask` 未置位 → `-MR_EINVAL`） | 公共层，仅在 configure/configuration       |
| `resolution` 1~32、`buf`/`size` 一致性断言                              | 公共层                                     |
| `ref_internal`、`exist_mask` 的取值                                     | backend（HardTree 属性或手工初始化）       |
| 逻辑通道到硬件通道/引脚的映射、采样时间、能力裁剪                       | backend                                    |
| 普通采样的数据搬运与同步方式                                            | backend                                    |
| stream 数据通路、buffer 所有权、DMA/IRQ 选择                            | backend                                    |
| `event_control` 的事件掩码与真实中断源开关                              | backend                                    |
| 外部触发生产者（TMR/PWM）的配置与启停                                   | 应用或生产者 backend，**不是** ADC backend |

平台实现通常位于：

```text
mrX/arch/<vendor>/<target>/platform/drivers/adc/
```

backend 私有数据通常保存 HAL handle 或寄存器基址、时钟使能函数、通道表、事件掩码
和 stream 状态。

## 2. 操作表

backend 实现 `mrX/include/mrX/drivers/adc/mr_adc.h` 中的 `mr_adc_ops_t`。签名只列参数
与返回值要点，公共 API 的说明见 [ADC](adc.md)。

基础能力（全部必需，公共层入口对空回调使用断言）：

| 成员                    | 签名要点                                                                                    | 必需 |
|-------------------------|---------------------------------------------------------------------------------------------|------|
| `channel_configure`     | `(const mr_driver_t *drv, mr_uint32_t channel, const mr_adc_config_t *config)` → `mr_err_t` | 是   |
| `channel_configuration` | `(const mr_driver_t *drv, mr_uint32_t channel, mr_adc_config_t *config)` → `mr_err_t`       | 是   |
| `read`                  | `(const mr_driver_t *drv, mr_adc_seq_t *seq)` → `mr_ssize_t`（字节数或负数错误码）          | 是   |

启用 `MR_USE_DRIVER_ADC_STREAM` 后声明（四个成员彼此独立，可单独缺省）：

| 成员             | 签名要点                                                                      | 必需 | 空回调时公共 API 返回 |
|------------------|-------------------------------------------------------------------------------|------|-----------------------|
| `stream_start`   | `(drv, mr_adc_seq_t *seq, const mr_adc_stream_config_t *config)` → `mr_err_t` | 否   | `-MR_ENOSYS`          |
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
  `mr_adc_event_raise` 通知 waiter，不能阻塞。
- shutdown 时公共层会调用已实现的 `stream_stop`；停止失败且驱动未处于删除流程时
  返回错误并保留 waitable 以便重试，成功后才销毁 waitable。
- `mr_adc_event_attach` 只把 waiter 挂到 `mr_adc_data_t.event`，不打开硬件来源；
  `enable`/`disable` 才经由 `event_control` 开关中断源。两者是独立步骤。

## 3. 数据与通道模型

`mr_adc_data_t` 由 backend 提供：

| 字段           | 含义                      | backend 义务                                                            |
|----------------|---------------------------|-------------------------------------------------------------------------|
| `ref_internal` | 内部参考标称值，单位 mV   | 与真实带隙/参考一致；`mr_adc_ref_internal()` 与 HardTree 换算回退依赖它 |
| `exist_mask`   | bit n 表示逻辑通道 n 存在 | configure/configuration 的唯一门控；普通 `read` 与 stream 不再查它      |
| `event`        | 公共 waitable             | 只由公共层 init/del；backend 通过 `mr_adc_event_raise()` 触发           |

通道号是 0~31 的位号。backend 必须固定两件事：

- 逻辑通道到硬件通道/引脚的映射——HardTree `st_channels` 的数组下标就是逻辑通道号；
- 实例内的通道上界，越界通道返回 `-MR_EINVAL`（例如 STM32H5 的安装宏缺省把
  `exist_mask` 置为全 1，真实边界只能由 backend 自己兜住）。

`mr_adc_seq_t` 的解释义务：

| 字段          | 公共层约束                           | backend 必须定义                                                     |
|---------------|--------------------------------------|----------------------------------------------------------------------|
| `channels`    | 无（公共层只读取）                   | 位掩码语义（bit n = 逻辑通道 n）；采样顺序；同时可选的通道数上限     |
| `resolution`  | 断言 1~32                            | 支持的位数集合；码值存储宽度（是否固定为某个宽度）                   |
| `oversample`  | 无                                   | 字段的真实含义与取值范围（采样时间、平均倍率或硬件过采样倍率）       |
| `size`、`buf` | 断言 `buf != MR_NULL` 或 `size == 0` | 单样本字节数；缓冲是否需要对齐；`size` 是否必须是完整 frame 的整数倍 |

`channel_configuration` 必须返回 backend 实际采用的配置，而不是简单回显用户请求：
backend 可以夹取取值范围、只支持部分参考源或增益。对存在但从未
`channel_configure` 过的通道，也应给出确定值（例如 HardTree 派生的默认配置），
不能让调用者读到未初始化内容。

## 4. 普通采样与硬件接线

普通 `read` 是同步接口，公共层只做断言；通道掩码、frame 布局和错误语义完全由
backend 定义。移植时必须在平台文档中写明：

- 时钟、GPIO 模拟模式、参考源由谁配置，是否需要重新校准；backend 不直接写 GPIO
  寄存器，模拟引脚由 GPIO owner helper 配成 analog；
- 采样顺序与缓冲布局，以及单样本在缓冲中占多少字节；
- 缓冲放不下完整 frame 时的行为：报错，还是填充到放不下为止；
- 中途失败时返回已写字节数还是错误码；
- 是否与 stream 互斥；
- 是否需要外部信号，普通 `read` 通常不应依赖外部触发。

公共层不会替 backend 配置 GPIO、ADC 时钟、触发源或 DMA，也不会启动外部信号生产者。
`read` 的返回值是实际写入缓冲的字节数；具体错误码见第 5.6 节中与之对应的条目。

## 5. Stream、数据通路与事件

### 5.1 启动来源

- `MR_ADC_STREAM_SOURCE_SOFTWARE`：`stream_start` 用软件启动转换，之后由 ADC 自行
  推进序列，不需要外部触发资源。
- `MR_ADC_STREAM_SOURCE_EXTERNAL`：`stream_start` 只把 ADC 配置并武装为外部触发
  模式，每个序列由静态选定的信号启动。外部信号的来源、路由和边沿来自 HardTree
  （`st_trigger`、`st_trigger_edge`），不进入公共 stream 配置。
- `circular` 只决定 buffer 写满后是否回绕，与启动来源相互独立。

生产者所有权与顺序约束（换平台时必须重新确认，不要继承 STM32H5 的结论）：

- ADC backend 不配置、不启停 TMR/PWM 等信号生产者；生产者由应用侧配置和启停。
- TIM 的中性 TRGO 配置在软件 UG 时仍可能产生一次脉冲，因此 **先完成生产者重配，再
  武装 ADC stream**；把 producer signal 关掉不等于消费者侧的硬件隔离。

### 5.2 数据通路选择

| 条件                                 | 数据通路 | 额外要求                                                      |
|--------------------------------------|----------|---------------------------------------------------------------|
| 实例提供 `st_dma` 且 DMA 驱动已启用  | DMA      | 该 DMA 通道由本 stream 独占；可以不提供 ADC 中断资源          |
| 未提供 `st_dma`（或未启用 DMA 驱动） | ADC 中断 | 实例必须提供 `st_irqn` 与 `st_irq_handler`，否则 `-MR_ENODEV` |

“配置了 DMA 的 stream 独占该 DMA 通道”是一条所有权约定：同一通道不能同时被其他
驱动或应用使用，stop 成功前也不得交给别人。

### 5.3 frame 与 HALF 边界

- buffer 必须至少容纳一个完整通道 frame，且 `size` 必须是 frame 的整数倍，否则
  `stream_start` 返回 `-MR_EINVAL`。
- HALF 只在 buffer 能在完整 frame 边界被平分时可行（样本数是 frame 样本数的偶数
  倍）；单 frame buffer 只产生 FULL。不允许在 frame 中间产生 HALF。
- 是否产生 HALF 由 backend 依据 buffer 大小自行决定；即使可行，也只有应用订阅了
  `MR_ADC_WAIT_STREAM_HALF` 时才通知。
- frame 内的样本按 `channels` 位号升序排列，缓冲布局必须在平台文档中写死。

### 5.4 事件

| 事件位                     | backend 义务                                       |
|----------------------------|----------------------------------------------------|
| `MR_ADC_WAIT_STREAM_HALF`  | buffer 前半写完时触发（仅 HALF 可行且已订阅时）    |
| `MR_ADC_WAIT_STREAM_FULL`  | buffer 写满时触发；非 circular 流此时转换已结束    |
| `MR_ADC_WAIT_STREAM_ERROR` | 过采样/溢出或 DMA 错误时触发，并把该轮流置为完成态 |

`event_control(events, enable)` 维护 backend 事件掩码并据此开关真实中断源，建议：

- `events == 0` 或包含未定义位时返回 `-MR_EINVAL`；
- 流活动期间禁止关闭 `ERROR`；非 circular 流还禁止关闭 `FULL`；`HALF` 允许在活动
  期间关闭。以上冲突返回 `-MR_EBUSY`；
- 关闭通知不等于停止硬件：停止转换只能靠 `stream_stop`，不能靠关中断冒充。

ISR 中应同时判断硬件 flag 与 interrupt source（只判断 flag 会在未使能时误报），
先清除或屏蔽会持续触发的状态，再更新 stream 状态、聚合事件位、调用
`mr_adc_event_raise`。waiter 同步运行在 raise 的调用链中，ISR 不应留下无人消费且
持续触发的电平型来源。

### 5.5 busy、offset 与 stop

- `stream_is_busy` 成功时通过 `busy` 输出状态；失败时 `busy` 不可用。若 backend 在
  判定流已完成时顺带执行内部停止，该 API 也可能返回停止失败的错误码，这一点必须
  在平台文档中说明。
- `stream_offset` 的单位是 **字节**，表示本轮已写入 buffer 的字节数；circular 回绕
  后重新从 0 开始；DMA 通路直接反映 DMA 通道的当前进度。单位和回绕时机必须写死。
- `stream_stop` 的幂等性由 backend 定义。失败时必须保留活动状态与 DMA waiter，
  调用方继续保有 buffer、驱动和关联资源并重试； **不能**把错误返回理解为硬件已经
  停止。只有 stop 成功后才释放 waiter 与 stream 状态（ST 实现即在失败路径提前返回、
  不清理）。
- 在普通调用上下文执行 stop：DMA 的阻塞停止可能依赖 HAL 时基，不应在同步事件回调
  的 IRQ 上下文执行。
- 有 D-cache 的平台需按平台 cache 契约处理 buffer 一致性。

### 5.6 错误码

下表未标注“普通 `read`”的行都属于 `stream_start`：

| 条件                                                                                   | 返回                                      |
|----------------------------------------------------------------------------------------|-------------------------------------------|
| 已编译 `MR_USE_DRIVER_ADC_STREAM` 但对应 stream 回调为空（未编译时这些 API 不存在）    | `-MR_ENOSYS`（公共层）                    |
| 无 DMA 且缺 `st_irqn` 或 `st_irq_handler`                                              | `-MR_ENODEV`                              |
| `source == MR_ADC_STREAM_SOURCE_EXTERNAL` 但无 `st_trigger`                            | `-MR_ENODEV`                              |
| `buf == MR_NULL`、`size == 0`、`size` 不是单样本字节数的整数倍、`buf` 未按样本宽度对齐 | `-MR_EINVAL`                              |
| `resolution` 不在 backend 支持的位数集合内                                             | `-MR_EINVAL`                              |
| `oversample` 越界                                                                      | `-MR_EINVAL`                              |
| `channels == 0`、选中越界通道、或超过 backend 的 rank/通道上限                         | `-MR_EINVAL`                              |
| buffer 小于一个完整 frame，或不是 frame 的整数倍                                       | `-MR_EINVAL`                              |
| 已有活动流                                                                             | `-MR_EBUSY`                               |
| HAL 返回 `HAL_BUSY` / `HAL_TIMEOUT` / 其他错误                                         | `-MR_EBUSY` / `-MR_ETIMEDOUT` / `-MR_EIO` |
| 普通 `read` 订阅了非法分辨率的序列                                                     | `-MR_EINVAL`                              |
| 普通 `read` 的 `channels == 0`，或 `size` 小于一个样本                                 | 返回 `0`                                  |
| 普通 `read` 中途失败且已写入至少一个样本                                               | 返回已写入的字节数（不是错误码）          |
| 普通 `read` 中途失败且尚未写入任何样本                                                 | 对应的错误码                              |

`-MR_ENODEV` 表示静态资源缺失（可在板级补齐），`-MR_ENOSYS` 表示该 backend 未实现
该能力（应换实例或换路径），两者不要混用。

## 6. HardTree 属性与安装

STM32H5 backend（compat `st_stm32h5_adc`）读取的属性：

| 属性              | 必需 | 说明                                                                                   | 缺省                                                    |
|-------------------|------|----------------------------------------------------------------------------------------|---------------------------------------------------------|
| `st_adcx`         | 是   | ADC 实例寄存器基址（如 `ADC1`）                                                        | —                                                       |
| `st_clken`        | 是   | 外设时钟使能函数                                                                       | —                                                       |
| `st_ref`          | 是   | 本实例允许的参考源；`channel_configure` 的 `config->ref` 必须等于它，否则 `-MR_EINVAL` | —                                                       |
| `st_channels`     | 是   | 通道数组；数组下标即公共逻辑通道号，长度即实例通道数                                   | —                                                       |
| `st_dma`          | 否   | stream 使用的 DMA spec；存在且启用 DMA 驱动时走 DMA 通路，该通道被 stream 独占         | 无（走 ADC 中断）                                       |
| `st_trigger`      | 否   | `MR_ADC_STREAM_SOURCE_EXTERNAL` 使用的触发选择器                                       | 无（EXTERNAL 启动返回 `-MR_ENODEV`）                    |
| `st_trigger_edge` | 否   | 外部触发边沿                                                                           | `ADC_EXTERNALTRIGCONVEDGE_RISING`                       |
| `st_irqn`         | 否   | 中断号；与 `st_irq_handler` 同时存在才构成有效 IRQ 资源                                | 无                                                      |
| `st_irq_priority` | 否   | NVIC 优先级                                                                            | `0`                                                     |
| `st_irq_handler`  | 否   | 该实例的向量函数名，backend 据此生成中断处理函数                                       | 无                                                      |
| `ref_internal`    | 否   | 公共 `mr_adc_data_t.ref_internal`（mV）                                                | `3300`                                                  |
| `exist_mask`      | 否   | 公共通道存在位图                                                                       | `0xffffffff`（backend 仍按 `st_channels` 长度二次过滤） |

`st_channels` 的每个单元是 `<hal_channel, gpio_phandle, pin>`：

- 单元下标是公共逻辑通道号，公共 API 的 `channel` 参数就按它解释；
- `hal_channel` 是该平台 HAL 的通道常量，由 backend 使用，不暴露给公共层；
- `gpio_phandle` 指向 GPIO 实例，`pin` 是引脚号；`gpio_phandle` 解析不到驱动、
  或引脚不落在对应 GPIO 的 `exist_mask` 里时返回 `-MR_EINVAL`，ADC 直接透传该错误；
  ADC 只把引脚配成 analog，不配置复用功能。

安装约定：

- 使用公共头文件的 `MR_HT_ADC_INSTALL` 按 compat 实例批量安装；
- 与 stream 相关的属性（`st_dma`、`st_trigger`、`st_trigger_edge`、`st_irqn`、
  `st_irq_priority`、`st_irq_handler`）只在 `MR_USE_DRIVER_ADC_STREAM` 下展开；
  `st_dma` 还需要 `MR_USE_DRIVER_DMA` 打开才会被读取；
- 没有对应 compat 实例时 backend 会被 `MR_HT_HAS_DRIVER()` 裁掉；
- HardTree 只承载静态硬件资源和连接关系，运行状态（busy、offset、event mask）留在
  backend 私有数据中。

## 7. 参考实现与芯片能力边界

参考实现：

```text
mrX/arch/st/stm32h5/platform/drivers/adc/adc.c
```

值得按其职责阅读的函数（只在本文列出名字，具体实现以源码为准）：

| 职责               | 函数                                                                                                                                                        |
|--------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 通道配置与能力裁剪 | `adc_channel_configure`、`adc_channel_configuration`、`adc_resolution`、`adc_configure_hw_channel`                                                          |
| 普通采样           | `adc_read`、`adc_convert`、`adc_init`                                                                                                                       |
| stream 数据通路    | `adc_seq_ranks`、`adc_init_stream`、`adc_stream_start_impl`、`adc_stream_stop_impl`、`adc_stream_is_busy_impl`、`adc_stream_offset_impl`、`adc_stream_halt` |
| 事件与 ISR         | `adc_irq`、`adc_dma_event`、`adc_event_control`                                                                                                             |
| HardTree 实例安装  | `ST_HT_ADC_INSTALL`、`__ST_HT_ADC_CHANNEL_IMPL`、`__ST_HT_ADC_STREAM`                                                                                       |

STM32H5 能力边界（实现新 backend 时按目标芯片重新确认，不要默认继承）：

- `channel_configure` 只接受 `MR_ADC_DIFF_SINGLE`、`config->ref == st_ref` 和
  `MR_ADC_GAIN_1`；其他组合返回 `-MR_EINVAL`，即差分、多档参考源与增益在该平台上
  不可用；
- 只支持 12/10/8/6 位分辨率，其他取值返回 `-MR_EINVAL`；样本一律以 `mr_uint16_t`
  右对齐存储，存储宽度与 `resolution` 无关；
- 不使用 HAL 硬件过采样：`oversample` 被当作采样时间索引（8 档），越界返回
  `-MR_EINVAL`，不做平均或抽取；
- stream 的通道数上限为 8（HAL regular rank 表长度），超过返回 `-MR_EINVAL`；
  普通 `read` 不走 rank 表，但通道号必须小于实例通道数；
- 普通 `read` 是逐样本同步轮询，单次转换超时 100 ms，超时映射为 `-MR_ETIMEDOUT`；
- `MR_ADC_STREAM_SOURCE_SOFTWARE` 使用连续转换加软件启动；`EXTERNAL` 关闭连续转换、
  每个序列由静态触发启动，多通道时打开 scan；
- 无 DMA 时必须提供 ADC IRQ 资源，否则 `-MR_ENODEV`；有 DMA 时走 DMA，且只有
  buffer 能在完整 frame 边界平分时才产生 HALF；
- 事件掩码只放开已订阅的位；活动期间关闭 `ERROR`（以及非 circular 的 `FULL`）
  返回 `-MR_EBUSY`；
- 该 backend 从不配置或启停 TMR/PWM 生产者，也不把 signal disable 当作硬件隔离。

## 8. 移植验收清单

- `configure`/`configuration` 对每个字段都能稳定往返 backend 实际采用的配置；未配置
  过的通道也能返回确定值。
- `read` 的原始码宽度、frame 排列与公共换算函数（`resolution`、`gain`、参考电压）
  的假设一致，并在平台文档中写明。
- `read` 中途失败时，已写入至少一个样本就返回已写字节数，尚未写入才返回错误码
  （或明确写出本平台的相反约定）。
- stream 回调可以独立缺省，缺省时只有对应 API 返回 `-MR_ENOSYS`。
- 无 DMA 时的中断资源缺失返回 `-MR_ENODEV`，`EXTERNAL` 源缺触发选择器返回
  `-MR_ENODEV`；两者都不与 `-MR_ENOSYS` 混用。
- `size`/`buf` 对齐、样本宽度、frame 整数倍校验齐全，非法组合返回 `-MR_EINVAL`。
- HALF 只在完整 frame 边界产生；单 frame buffer 只产生 FULL。
- `stream_offset` 的单位与回绕时机已写死（本平台为字节）。
- `event_control` 的活动期禁用规则明确；关闭通知不等于停止硬件。
- `stream_stop` 失败时保留可重试的活动状态、DMA waiter 与调用方资源；成功后才释放。
- DMA 通道的所有权独占与 stop 顺序（先停 DMA/外设请求，再释放）明确；有 cache 的
  平台按契约处理一致性。
- 外部触发生产者先重配、再武装 stream；backend 不配置也不启停生产者。
- HardTree 属性齐全且与板级一致；`st_channels` 下标与公共通道号一致，模拟引脚落在
  对应 GPIO 的 `exist_mask` 内。

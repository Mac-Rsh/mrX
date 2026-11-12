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

backend 私有数据通常保存 硬件上下文、时钟使能函数、通道表、实例参考源、事件掩码和
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

backend 将逻辑通道、参考源、分辨率与输出原始码映射到硬件；公共层并不指定具体引脚或时钟。对不支持的通道、参考源、模式或序列形态，应返回明确错误，不应假装写入成功。

## 4. 普通输出

普通 `write` 依据 [DAC 公共接口](dac.md) 的 sequence 约定提交输出数据，返回实际接受的大小或负错误码。若硬件不能立即使用调用者缓冲区，backend 必须在返回前自行保存需要的数据。

## 5. Stream 与事件

可选 Stream 实现 `start`、`is_busy`、`stop`、`offset` 及事件回调。backend 决定由 DMA、IRQ 或其他方式推进样本；若外部触发，必须由板级提供实际硬件路由，公共层不替外设接线。有限流的完成通知应在最后一个样本实际交付输出之后发出，不能把最后一次 DMA 传输提交误认为物理输出已经结束。

停止时须撤销触发源、禁止继续读取应用缓冲区，处理完完成/取消通知再归还其所有权。循环流的 offset、缓冲回绕与 HALF/FULL/DONE 事件必须彼此一致。无法实现的模式应返回 `-MR_ENOSYS`。

## 6. HardTree 与安装

使用 `MR_HT_DAC_INSTALL` 或驱动框架约定注册实例。公共属性只表达 DAC 公共配置；寄存器、GPIO、时钟、DMA、IRQ、外部触发源等属于平台私有资源，应仅在对应平台文档解释。

## 7. 移植验收

- [ ] 通道、参考源、分辨率及输出 raw 值映射正确。
- [ ] 普通输出返回长度和错误语义符合公共层。
- [ ] 有限与循环流的启动、offset、完成、停止和缓冲区归还规则正确。
- [ ] 外部触发与 DMA/IRQ 资源由平台明确提供，缺失能力不会被误报为成功。

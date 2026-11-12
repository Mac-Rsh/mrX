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

backend 私有数据通常保存 硬件上下文 或寄存器基址、时钟使能函数、通道表、事件掩码
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

backend 将逻辑通道映射到硬件输入，并按公共 `mr_adc_seq_t` 的通道掩码、分辨率、采样次数和缓冲区大小处理请求。只支持部分分辨率、参考源或差分组合时，必须返回明确错误，不能静默降级。通道的物理引脚和触发输入由板级描述提供。

## 4. 普通采样与触发

普通 `read` 必须遵循 [ADC 公共接口](adc.md) 对返回长度及错误码的约定。采样流程和结果排列由 backend 按公共 sequence 契约实现。外部触发需要先在硬件层接通生产者和 ADC 输入；公共 API 不会自动连接外设。

## 5. Stream 与事件

支持 `MR_USE_DRIVER_ADC_STREAM` 时，backend 实现流启动、busy、offset、停止及事件通知；缺失的可选操作按公共层规则返回 `-MR_ENOSYS`。启动成功后保留调用者的缓冲区，直到 stop/完成通知确认硬件不再访问。stream 数据源（软件或外部触发）、DMA/IRQ 路径及缓存一致性由 backend 选择并记录到平台文档。

HALF/FULL 事件与 offset 必须能对应真实写入进度；同步 waiter 不能进行阻塞式停止。stop 要关闭硬件来源并使缓冲区可安全复用。错误、资源占用与未配置状态应以负错误码明确区分。

## 6. HardTree 与安装

用驱动框架的实例展开与 `MR_HT_ADC_INSTALL` 注册设备。公共配置描述通道、分辨率和参考电压；外设寄存器、引脚、DMA、IRQ、时钟和触发路由等私有属性均由具体平台定义，不能作为 ADC 公共驱动的要求。

## 7. 移植验收

- [ ] 通道掩码、分辨率、参考源、单端/差分配置与错误码符合公共头文件。
- [ ] 普通采样返回的数据长度、样本次序和缓冲区边界正确。
- [ ] stream 数据源、HALF/FULL、offset、stop 与缓冲区归还语义一致。
- [ ] 未实现的能力返回明确错误；硬件路由与平台资源在其平台文档中说明。

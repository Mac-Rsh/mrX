# DAC 驱动移植指南

本文面向平台与外设驱动开发者，说明如何实现 `mr_dac_ops_t` 并接入 DAC 驱动框架。公共
API 与使用方式见 [DAC 驱动](dac.md)。

## 移植边界

| 职责                                                                                     | 归属       |
|------------------------------------------------------------------------------------------|------------|
| 类型化 API 封装、通道存在性校验（`channel >= 32` 或不在 `exist_mask` 返回 `-MR_EINVAL`） | DAC 公共层 |
| `resolution`（1~32）与 `buf`/`size` 一致性断言                                           | DAC 公共层 |
| `mr_dac_data_t.event` 的初始化与销毁（`dac_init` / `dac_deinit`）                        | DAC 公共层 |
| 可选流回调与 `event_control` 缺失时的 `-MR_ENOSYS` 返回                                  | DAC 公共层 |
| 电压到原始码换算（`mr_dac_millivolt2raw` 等）                                            | DAC 公共层 |
| 逻辑通道到物理引脚的映射、参考源与参数合法性校验                                         | Backend    |
| 普通输出实现、缓冲区解析格式及多通道输出顺序                                             | Backend    |
| 流输出的 DMA/IRQ 数据通路、循环缓冲、`busy`/`offset` 与 DONE 状态维护                    | Backend    |
| 硬件中断源开关（`event_control`）与事件触发（`mr_dac_event_raise`）                      | Backend    |

## 操作表（ops）

Backend 需提供 `mr_dac_ops_t` 实例，并挂载到 `MR_DAC_DEFINE` /
`MR_HT_DAC_INSTALL`。

### 必需回调

| 成员                    | 原型要点                                                    | 约定                                                                             |
|-------------------------|-------------------------------------------------------------|----------------------------------------------------------------------------------|
| `channel_configure`     | `(drv, channel, const mr_dac_config_t *config) -> mr_err_t` | 校验 `ref` 是否受硬件支持，不支持返回 `-MR_ENOSYS` 或 `-MR_EINVAL`，不可静默降级 |
| `channel_configuration` | `(drv, channel, mr_dac_config_t *config) -> mr_err_t`       | 回读当前通道配置                                                                 |
| `write`                 | `(drv, mr_dac_seq_t *seq) -> mr_ssize_t`                    | 执行同步输出，成功返回实际处理的字节数，失败返回负数错误码                       |

### 可选回调

`stream_*` 四个成员仅在 `MR_USE_DRIVER_DAC_STREAM` 开启时存在于结构体中，且彼此独立，未实现的成员置
`MR_NULL`。

| 成员             | 原型要点                                                                     | 约定                                                     |
|------------------|------------------------------------------------------------------------------|----------------------------------------------------------|
| `stream_start`   | `(drv, mr_dac_seq_t *seq, const mr_dac_stream_config_t *config) -> mr_err_t` | 启动或武装连续输出，成功返回 0                           |
| `stream_stop`    | `(drv) -> mr_err_t`                                                          | 停止输出、关闭触发源与 DMA，使缓冲区可安全复用           |
| `stream_is_busy` | `(drv, mr_bool_t *busy) -> mr_err_t`                                         | 查询流是否仍在运行                                       |
| `stream_offset`  | `(drv, mr_size_t *offset) -> mr_err_t`                                       | 返回当前已读取缓冲区的字节偏移                           |
| `event_control`  | `(drv, mr_uint32_t events, mr_bool_t enable) -> mr_err_t`                    | 按 `MR_DAC_WAIT_STREAM_*` 位掩码开启或关闭对应硬件中断源 |

## 关键契约

- **调用上下文**：公共层不切换上下文，所有 `ops` 均在调用者上下文同步执行。事件上报由
  backend 在 ISR 或传输线程中调用 `mr_dac_event_raise(drv, events)`，不能在 ISR
  中阻塞。
- **缓冲区归属**：普通 `write` 返回前若硬件尚未取走数据，backend 必须自行拷贝；
  `stream_start` 成功后，`seq->buf` 在 `stream_stop`
  返回或非循环模式完成前归硬件（DMA/ISR）访问，backend 需处理 Cache 一致性（如有）。
- **DONE 完成语义**：有限流（非循环模式）的 `MR_DAC_WAIT_STREAM_DONE`
  应在最后一个样本实际交付输出之后发出，避免将最后一次 DMA 请求提交误报为物理输出结束。
- **外部触发边界**：当 `config->source == MR_DAC_STREAM_SOURCE_EXTERNAL` 时，DAC
  backend 仅负责将外设武装为外部触发模式；定时器/PWM 等外部触发生产者的配置与启停由应用或对应驱动负责。
- **Shutdown 交互**：`mr_driver_shutdown` 会调用已实现的 `stream_stop`
  。若驱动未处于删除流程且 `stream_stop` 返回错误，公共层会中止 shutdown
  并保留等待队列以便重试。

## HardTree 安装

使用 `<mrX/drivers/dac/mr_dac.h>` 提供的 `MR_HT_DAC_INSTALL(_id, _priv, _ops)`
宏安装静态实例。该宏会自动从 HardTree 节点读取以下标准属性初始化 `mr_dac_data_t`：

| 属性           | 类型 | 默认值       | 说明                        |
|----------------|------|--------------|-----------------------------|
| `ref_internal` | int  | `3300`       | 内部参考电压标称值（mV）    |
| `exist_mask`   | int  | `0xffffffff` | 存在的物理通道掩码（32 位） |

外设寄存器基址、时钟、引脚复用、DMA 通道与 IRQ 号等芯片私有信息由平台自行在节点中定义并填充
`_priv`。

## 验收要点

- [ ] `channel_configure` 拒绝不支持的参考源，且 `channel_configuration` 回读一致。
- [ ] `write` 对越界码值返回明确错误，`channels == 0` 或 `size == 0` 时安全返回
  `0`。
- [ ] 启用流功能时，`stream_start` / `stream_stop` / `stream_is_busy` /
  `stream_offset` 状态一致；有限流 DONE 与循环流 HALF/DONE 语义准确。
- [ ] `mr_driver_shutdown` 与 `mr_dac_del` 后硬件已彻底停止，无残留中断或 DMA 访问。

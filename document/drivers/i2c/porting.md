# I2C 驱动移植指南

面向平台 backend 开发者，说明如何为 I2C 控制器实现 `mr_i2c_ops_t`。公共消息与事件
API 见 [I2C](i2c.md)。

## 1. 移植边界

| 职责                                                           | 公共层 | Backend |
|----------------------------------------------------------------|--------|---------|
| 类型校验、`-MR_ENOSYS` 降级与 `mr_i2c_data_t.event` 生命周期   | 负责   | —       |
| 速率/角色/本机地址映射与 `configuration` 回显                  | —      | 负责    |
| Master 总线时序（START/RESTART/STOP、7/10-bit 寻址、ACK/NACK） | —      | 负责    |
| 总线错误恢复、超时与仲裁丢失处理                               | —      | 负责    |
| 可选 Slave 收发缓冲及硬件事件上报                              | —      | 负责    |

平台实现通常位于 `mrX/arch/<vendor>/<target>/platform/drivers/i2c/`；树内软件模拟
Master 参考 `drivers/i2c/i2c_gpio.c`。

## 2. 操作表（ops）

backend 需实现 `<mrX/drivers/i2c/mr_i2c.h>` 中的 `mr_i2c_ops_t`：

| 回调            | 签名要点                                                           | 必需 | 缺失返回值   |
|-----------------|--------------------------------------------------------------------|------|--------------|
| `configure`     | `(drv, const mr_i2c_config_t *config)`                             | 是   | 断言拦截     |
| `configuration` | `(drv, mr_i2c_config_t *config)`                                   | 是   | 断言拦截     |
| `transfer`      | `(drv, const mr_i2c_msg_t *msgs, mr_size_t num)`，返回完成的消息数 | 否   | `-MR_ENOSYS` |
| `slave_read`    | `(drv, void *buf, mr_size_t size)`，返回实际读取字节数             | 否   | `-MR_ENOSYS` |
| `slave_write`   | `(drv, const void *buf, mr_size_t size)`，返回实际写入字节数       | 否   | `-MR_ENOSYS` |
| `event_control` | `(drv, mr_uint32_t events, mr_bool_t enable)`                      | 否   | `-MR_ENOSYS` |

Master 与 Slave 能力相互独立：纯 Master backend 只需实现 `configure`、
`configuration` 和 `transfer`。

## 3. 关键契约

- **配置错误区分**：参数本身非法（如地址超出 7/10-bit 范围）返回 `-MR_EINVAL`
  ；参数合法但硬件不支持（如不支持的速率或 `MR_I2C_ROLE_SLAVE`）返回 `-MR_ENOSYS`。
  `configure` 失败时不得将控制器留在半配置状态。
- **Master 消息语义**：
    - 逐条解释 `msgs`：默认在消息间产生 repeated START，遇到 `MR_I2C_MSG_STOP` 发
      STOP，最后一条消息未显式指定时应在事务结束自动发 STOP。
    - 若最后一条消息带 `MR_I2C_MSG_NOSTOP`（当前框架无跨调用总线所有权），应返回
      `-MR_ENOSYS`；同一消息同时设置 `MR_I2C_MSG_STOP | MR_I2C_MSG_NOSTOP` 返回
      `-MR_EINVAL`。
    - 只消费已知 flag，不因未知扩展位拒绝消息；失败时只返回负错误码，不计入未完成消息。
- **总线错误与恢复**：NACK、超时或总线错误时应尽量释放总线（产生 STOP 或发时钟脉冲清线）；
  **仲裁丢失时禁止产生 STOP**（总线已属于其他 Master）。
- **Slave 与事件**：在 ISR 中先清硬件 pending 标志再调用 `mr_i2c_event_raise`
  ，并在平台文档中明确时钟拉伸策略与 `slave_read`/`slave_write` 缓冲容量。

## 4. HardTree 安装

使用 `MR_HT_I2C_INSTALL(_id, _priv, _ops)` 安装整颗控制器（自动定义零初始化
`mr_i2c_data_t`）。引脚复用、时钟、IRQ、DMA 等属性由平台文档定义；树内 `i2c_gpio`
兼容驱动使用 `scl`、`sda` 和可选 `stretch_timeout_us`（默认 `25000`）属性。

## 5. 验收要点

- `configure`/`configuration` 往返正确，非法参数返回 `-MR_EINVAL`，不支持能力返回
  `-MR_ENOSYS`。
- repeated START、中间 `STOP` 分组、末尾自动 STOP、`NOSTOP` 拒绝对齐契约，7/10-bit
  地址及零长度消息行为符合预期。
- NACK、超时、总线忙及仲裁丢失（不发 STOP）后的恢复路径可靠。
- 若支持 Slave：事件位门控、中断上下文上报及缓冲区边界无丢失或卡死。

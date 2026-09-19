# I2C 驱动移植指南

> 本文说明如何实现 I2C controller backend。公共消息、配置和事件 API 见 [I2C](i2c.md)。

## 1. 移植边界

I2C 公共层定义 controller 配置、message 数组和 Slave wait 事件；backend 负责总线状态机、START/RESTART/STOP、地址阶段、ACK/NACK、读写方向、错误恢复以及可选 Slave 模式。

建议实现位置：

```text
mrX/arch/<vendor>/<target>/platform/drivers/i2c/
```

## 2. 操作表

`configure` 与 `configuration` 是基础 callback。

以下能力按 backend 支持情况提供：

- `transfer`：Master message transfer；
- `slave_read`；
- `slave_write`；
- `event_control`。

缺失的可选 callback 保持 `MR_NULL`，公共层返回 `-MR_ENOSYS`。

## 3. 配置映射

`mr_i2c_config_t` 包含 clock rate、own address、7/10-bit 地址和角色。backend 负责：

- 计算实际总线时序；
- 判断目标频率是否可实现；
- 配置 Master/Slave；
- 配置 own address；
- 通过 `configuration` 返回实际采用值。

## 4. Master transfer

`mr_i2c_transfer()` 把 message 数组原样交给 backend。backend 逐条解释：

```text
MR_I2C_MSG_READ
MR_I2C_MSG_STOP
MR_I2C_MSG_NOSTOP
MR_I2C_MSG_ADDR10
```

连续 message 之间是否产生 repeated START、何时发 STOP，由 flag 和总线状态共同决定。

backend 只消费自己认识的 flag；不要额外引入一个“允许 flag mask”去因为未知位直接拒绝整个 message。无法完成某个实际请求时，再按硬件能力返回对应错误。

返回值是完成的 message 数；出现错误时应避免把尚未完成的 message 计入结果。

## 5. Slave 模式与事件

公共 Slave 事件包括：

```text
RX_START
RX_READY
TX_START
TX_READY
STOP
ERROR
```

典型含义：

- START：被寻址并确定本次方向；
- RX_READY：已有 Master 写入的数据可由 `slave_read` 取走；
- TX_READY：硬件需要更多 Slave TX 数据；
- STOP：本次总线事务结束；
- ERROR：NACK、仲裁/总线错误或平台定义的失败。

事件通过 `mr_i2c_event_raise()` 同步通知 waiter。若从 ISR raise，回调也处于 ISR 调用链。时钟拉伸策略、一次 read/write 搬多少字节以及缓冲方式由 backend 决定。

## 6. 错误与恢复

backend 应明确处理：

- address NACK / data NACK；
- arbitration lost；
- bus error；
- timeout；
- bus busy；
- peripheral state machine 卡死后的 recovery。

公共层不提供 bus lock、transaction queue 或自动 recovery。

## 7. HardTree 与安装

`MR_HT_I2C_INSTALL` 安装整颗 controller，不创建 I2C client/device 对象。HardTree 可描述 controller 实例、pins、clock/IRQ 以及平台需要的静态资源。

## 8. 当前参考

当前 STM32H5 平台尚没有 I2C backend。实现时以：

- `mrX/include/mrX/drivers/i2c/mr_i2c.h`
- `mrX/test/drivers/i2c/i2c.c`
- 已有 UART/GPIO 等 STM32H5 平台驱动的 HardTree/IRQ 风格

作为接口和代码样式参考。

## 9. 移植检查清单

- clock_rate 能正确换算为硬件 timing；
- STOP/NOSTOP/repeated START 行为明确；
- 7/10-bit address 处理正确；
- 未识别 flag 不被额外 mask 校验误拒绝；
- Slave READY 事件与 read/write 能形成完整数据流；
- ISR raise 不产生不可控重入或中断风暴；
- NACK/bus error/timeout 后状态机可恢复；
- HardTree controller 实例能正确安装。

# 驱动组合与进阶示例

> 本页展示驱动之间的通用调用顺序。它不规定具体芯片的触发矩阵、通道数量、DMA/IRQ 能力或专有属性。实际接线应由平台文档确认。

## 1. 通用启动与停止原则

1. 启用所需 Kconfig 选项，用 HTC 注册实例，并先完成各设备 setup 与配置。
2. 按各驱动 API 准备消费者缓冲区和 waiter；先启动接收/输出 stream，再启用相应硬件信号生产者。
3. signal、stream 和 wait event 是三个不同能力；启用 signal 不自动启动消费者，也不自动订阅完成事件。
4. 停止时先停止触发生产者，再停止 stream，确认硬件已归还缓冲区后关闭事件与设备。
5. 通过 `mr_driver_find()` 得到的引用必须用 `mr_driver_put()` 归还。

## 2. TMR + ADC 周期采集

1. 按 [TMR](tmr/tmr.md) 与 [ADC](adc/adc.md) 公共 API 配置计时器周期、采样通道、分辨率和 `mr_adc_seq_t` 缓冲区。
2. 在平台的硬件配置中将 TMR 周期信号实际连接到 ADC 的触发输入；如果平台没有该通路，应改用受支持的触发方式。
3. 按 `mr_adc_stream_config_t` 配置并启动 ADC stream，按需挂接 HALF/FULL waiter。
4. 调用 `mr_tmr_signal_enable(timer, MR_TMR_SIGNAL_PERIOD)`，再调用 `mr_tmr_start(timer)`。
5. 从事件和 `mr_adc_stream_offset()` 判断已完成的数据范围；停止时先停止 TMR，再停止 ADC stream 和相关事件。不得读取 DMA 尚未交付的区域。

## 3. PWM + ADC 同步采样

1. 在平台描述中确认 PWM 周期/比较点硬件输出与 ADC 触发输入的实际连接能力。
2. 配置并启动 ADC stream，再设置并启用目标平台支持的 PWM signal，最后启动 PWM。
3. 停止 PWM 后结束 ADC stream，避免重配时产生意外触发。`MR_PWM_SIGNAL_COMPARE` 是公共能力定义，是否实现由目标 backend 决定。

## 4. TMR + DAC 定时输出

1. 配置 [DAC](dac/dac.md) 通道和序列缓冲区，按平台文档确认 DAC 外部触发的硬件接线。
2. 为 DAC 配置外部 stream 并先调用 `mr_dac_stream_start()`，再使能 TMR PERIOD signal 和启动 TMR。
3. 停止时先停 TMR，再停 DAC stream。有限流的 DONE 应以最后样本真正完成输出为准，不能仅凭 DMA 数据搬运完成推断。

## 5. GPIO data-ready + I2C 读取

1. 配置 GPIO 中断模式、初始化 waiter，使用 `mr_gpio_event_attach()` 和 `mr_gpio_event_enable()` 订阅 data-ready。
2. 事件回调只标记数据就绪，在允许执行阻塞 I/O 的普通上下文中发起 I2C 请求。
3. 在同一次 `mr_i2c_transfer()` 中依次提交寄存器地址写消息和数据读消息；repeated START、STOP 与地址模式由 I2C backend 支持情况决定。
4. 处理完数据后清理传感器状态；结束时关闭事件，确保没有在途回调后再释放 waiter。

## 6. GPIO reset/enable + SPI stream

1. 先配置 GPIO 的 reset/enable 和 SPI 总线资源，按外设自身协议执行复位时序。
2. 准备单条 `mr_spi_msg_t`，通过 `mr_spi_stream_start()` 启动 stream；它与可以提交多条 message 的 `mr_spi_transfer()` 不同。
3. 根据 stream busy、offset 和对应通知处理完成进度；循环和非循环数据流应分别处理。
4. 退出前先阻止外设继续输出数据，再停止 SPI stream，确认缓冲区可重用后切换 enable/reset。

## 7. WDT 与业务进展

配置并启动 [WDT](wdt/wdt.md)，按目标 backend 定义解释时间单位、窗口和预警。只有采集和控制路径完成有效工作后才 `mr_wdt_feed()`，不要在无条件循环中喂狗；外设失败时应交给系统恢复策略处理。

## 8. 集成检查

- [ ] 每个驱动的 stream、signal、IRQ 和可选能力都已经由目标平台确认。
- [ ] 消费者先武装、生产者后启动；停机顺序相反。
- [ ] DMA 缓冲区对齐、缓存可见性、所有权与归还时机正确。
- [ ] 各接口的错误返回得到处理，特别是 `-MR_ENOSYS` 与 `-MR_EBUSY`。
- [ ] 平台专属的属性、寄存器与支持矩阵只写在对应 `arch/` 文档中。

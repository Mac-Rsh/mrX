# 串行总线外扩驱动共同行为

本文适用于通过 I2C 或 SPI 接入的
PCF8574、MCP23x17、PCA9555、ADS1x15、MCP3008、MCP4725、MCP4922 和 PCA9685
backend。芯片特有的能力、接线和 HardTree 属性以各自文档为准。

## 生命周期与总线所有权

- 应用必须先 setup 底层 I2C/SPI controller，再 setup 和使用外扩驱动。
- 外扩驱动只保存 HardTree 提供的 controller 指针，不取得 controller 的引用，也不代替应用
  setup 或 shutdown controller。外扩驱动存活或正在执行操作时，controller 对象及其
  backend 私有数据必须保持有效且处于可用状态。
- shutdown 或删除 controller 前，先停止对其所有外扩设备的访问，并 shutdown
  这些外扩驱动。外扩驱动的 shutdown 不会自动关闭底层 controller。

## 并发与调用上下文

- 这些 backend 不提供实例锁或总线锁。调用方必须串行化同一外扩驱动实例的所有
  `configure`、`configuration`、`read`、`write`、`start`、`stop`、`update` 和状态查询操作。
- 同一 controller 上不同设备的访问也必须按 controller 契约串行化。若 controller
  backend 自带总线队列或互斥，仍需保证一个外扩驱动公共调用包含的全部传输不会被另一个调用插入；仅保证单条
  I2C/SPI message 原子性并不足够。
- ADS1x15 的启动转换、轮询和读取跨越多个总线事务，并发调用可能覆盖 MUX
  配置导致读到其他通道的结果；GPIO 的读改写、PCA9685
  的配置和提交也包含多步状态转换，交错执行可能造成丢失更新或软硬件状态失步。
- 不要从中断上下文调用这些 backend。部分操作会轮询、延时或执行多个总线事务，底层
  controller 也未必支持中断上下文调用。

## 传输失败与恢复

- backend 只把完整完成预期消息数或字节数视为成功；短传输按 I/O 错误处理，底层
  controller 返回的负错误码原样向上传递。
- 总线失败不保证物理芯片没有接收部分数据。带软件 shadow
  或缓存状态的驱动只在完整成功后提交软件状态，因此错误后可能出现软硬件失步。恢复总线后，应用应按芯片文档重发完整目标配置或输出值；对安全相关输出，还应先通过板级手段进入安全状态。

# STM32H5 驱动适配约定

本目录提供 GPIO、ADC、DAC、GPDMA、UART、ETH、基本定时器、片上 FLASH 和 IWDG 适配。
分层、资源所有权、HardTree 约定和代码骨架见
[ST 平台适配规范](../../../document/arch/st/st.md)；本文只记录本芯片
运行期语义。

各驱动由对应 `MR_USE_DRIVER_*` 开关启用；ADC/DAC stream 可选使用 DMA，
UART stream 需要 DMA。HAL 模块与 HardTree 资源由板级工程配置。
没有对应 compat 实例时，backend 被 `MR_HT_HAS_DRIVER()` 裁掉。

## HardTree 属性

GPIO（`st_stm32h5_gpio`）：`st_gpiox`、`st_clken`、`st_irq_priority`；可选
`exist_mask` / `invert_mask`。EXTI 线 `n` 仅在存在 `st_irq_handler_n`
（向量名，如 `EXTI5_IRQHandler`）时生成处理函数。同编号的 EXTI 线一次只能由一个
端口占用，其他端口的同号引脚仍可配置或释放 AF。GPIO 退出中断模式时会解除该线的
waiter 订阅，再次进入中断模式后需要重新订阅和启用事件。同一引脚在中断模式间切换时
保留订阅和使能状态。退出或重新配置中断模式时会清除待处理边沿。

DMA（`st_stm32h5_gpdma`）：`st_gpdmax`、`st_clken`、`st_irq_priority`、
`st_requests`（按下标对应硬件通道的 request/trigger 路由）。每个元素可写成
`<request>`，也可写成
`<request trigger_mode trigger_polarity trigger_selection>`；后三项省略时默认为
`DMA_TRIGM_BLOCK_TRANSFER`、`DMA_TRIG_POLARITY_MASKED`、`0`，因此旧模板行为不变。
通道数组长度等于 `st_requests` 元素数；省略 `exist_mask` 时默认为低 `N` 位。可选
`st_irq_handler_0`…`st_irq_handler_7`；若声明了任一通道 IRQ，需提供
`st_ctrl`（0=GPDMA1，1=GPDMA2）。

例如让 channel 0 的 SPI TX 在 EXTI0 上升沿到来时释放一个完整 block，而 channel 1
的 SPI RX 只由 SPI 外设 request 推进：

```dts
st_requests = <<GPDMA1_REQUEST_SPI1_TX
                 DMA_TRIGM_BLOCK_TRANSFER
                 DMA_TRIG_POLARITY_RISING
                 GPDMA1_TRIGGER_EXTI_LINE0>
               <GPDMA1_REQUEST_SPI1_RX>>;
```

trigger 只负责门控 DMA：EXTI event、定时器 TRGO 或其他 trigger 生产者的引脚复用、
边沿和路由仍由板级工程配置。SPI 双向流应先启动未门控的 RX DMA，再启动被 trigger
门控的 TX DMA；TX 开始写 SPI 数据寄存器后，RX 由 SPI RX request 自然推进。

TMR（`st_stm32h5_tmr`）：`st_timx`、`st_clken`、`st_irqn`、
`st_irq_priority`、`st_irq_handler`。`MR_TMR_SIGNAL_PERIOD` 独立控制 update 到 TRGO
的输出，不启动定时器，也不启用 TIMEOUT 中断。signal 启用期间重配 TMR 返回
`-MR_EBUSY`。

PWM（`st_stm32h5_pwm`）：`st_timx`、`st_clken` 和 `st_channels`；每个 channel
单元为 `<logical_channel, hal_channel, gpio_phandle, pin, af>`。`channel_mask` 必须只包含
实际列出的 logical channel，当前 H5 backend 支持普通主输出、连续周期和 PERIOD signal；
仅支持 `MR_PWM_UPDATE_PERIOD`、单通道 update，phase 必须为 0。多通道必须在定时器空闲时
一次性 start，运行中追加通道返回 `-MR_ENOSYS`。互补输出、dead-time、fault、有限周期、
COMPARE signal 和 wait event 也返回 `-MR_ENOSYS`。PERIOD signal 使用 update
到 TRGO；中心对齐通过 repetition counter 保证每个完整 PWM 周期只产生一次 update。
signal 启用期间重配控制器返回 `-MR_EBUSY`；重配时基后必须重新配置通道。
任一通道运行时，H5 backend 也拒绝修改其他通道的静态配置。

UART（`st_stm32h5_uart`）：`st_uartx`、`st_clken`、`st_irqn`、
`st_irq_priority`、`st_irq_handler`。方向引脚 `st_tx` / `st_rx` 为 GPIO
spec `[phandle, pin]`，`st_tx_af` / `st_rx_af` 为 AF 编号；不用的方向可省略。
`st_rx_dma` / `st_tx_dma` 按实例可选。AF 由 GPIO 拥有者 helper 配置，
不要把这些引脚再配成 EXTI。

ADC（`st_stm32h5_adc`）：`st_adcx`、`st_clken`、`st_ref`；`st_channels`
单元为 `<hal_channel, gpio_phandle, pin>`。模拟脚必须落在对应 GPIO 的
`exist_mask` 里。`st_dma` 可选：存在时 stream 使用 DMA，否则使用 ADC 中断，
此时实例还需提供 `st_irqn`、`st_irq_handler`，`st_irq_priority` 可选。
外部源由 `st_trigger` 与可选 `st_trigger_edge` 静态选择；ADC 不配置或启停信号生产者。

DAC（`st_stm32h5_dac`）：`st_dacx`、`st_clken`；`st_channels` 单元为
`<HAL DAC channel, gpio_phandle, pin>`。外部 stream 使用 `st_trigger`，有限流还需
`st_irqn`、`st_irq_handler`（`st_irq_priority` 可选），以 DAC underrun 中断确认最后一次
DHR 到 DOR 的传送。`st_dma` 可选；无 DMA 时仅支持单样本有限流。H5 backend
不为软件源提供隐式时基，因此软件源返回 `-MR_ENOSYS`。

FLASH（`st_stm32h5_flash`）：片上阵列，无需额外 GPIO/DMA 属性。扇区 8 KB，
编程宽度 128 bit。H50x/H52x/H53x 为单 bank，其余系列为双 bank。

WDT（`st_stm32h5_wdt`）：`st_iwdgx`、`st_irqn`、`st_irq_priority`、
`st_irq_handler`。时间单位为毫秒。`feed_min` 必须为 0（无窗口）；
`feed_max` 为超时；`pretimeout` 为提前唤醒提前量，0 表示关闭 EWI。
IWDG 启动后不可停止，`stop` 为 `MR_NULL`。TIMEOUT 直接复位芯片，
不提供 TIMEOUT 事件。

ETH（`st_stm32h5_eth`）只允许一个启用实例。公共属性 `mac` 必填，`mtu`
和 `speed` 见 [ETH 驱动](../../../document/drivers/eth/eth.md)。板级属性如下。

| 属性 | 说明 |
|------|------|
| `st_pins` | 按 `<gpio_phandle pin af>` 列出所有 MII/RMII、MDC 和 MDIO 信号。引脚由 STM32H5 GPIO backend 复用为高速 AF。 |
| `st_phy_addr` | 外部 10/100 Mbit/s Clause 22 PHY 的 MDIO 地址，取值 0 到 31。 |
| `st_media` | `HAL_ETH_RMII_MODE` 或 `HAL_ETH_MII_MODE`，缺省为 RMII。 |
| `st_dma_section` | 链接器段名字符串，例如 `".eth_dma"`。板级链接脚本须把该段放在 ETH DMA 可访问且 CPU 不缓存的 SRAM。 |
| `st_irq_handler` | 启用 Stream 时提供 ETH 向量名，例如 `ETH_IRQHandler`，由后端生成处理函数。 |
| `st_irqn` / `st_irq_priority` | 可选。缺省为 `ETH_IRQn` 和优先级 `5`。 |
| `st_dma_region` | 启用 Stream 时提供 `<base size>`，指定应用帧缓冲区所在的 DMA 内存。起始地址和长度都按 32 字节对齐。 |

## ETH 接入与轮询

- 启用 `MR_USE_DRIVER_ETH`、`MR_USE_DRIVER_GPIO` 和 STM32Cube 的
  `HAL_ETH_MODULE_ENABLED`，设置 `USE_HAL_ETH_REGISTER_CALLBACKS=1`。适配层以
  STM32CubeH5 FW V1.7.0 的 ETH HAL 接口为依据，拥有 ETH MAC 的时钟、复位和 DMA
  环。板级工程负责 PHY 供电、复位和参考时钟，调用 setup 前应准备好 PHY。RMII
  模式应在 HAL 初始化和 MAC 重置期间持续提供 50 MHz 参考时钟。
- STM32H5 ETH 后端内置 Clause 22 PHY 配置和链路解析，通过 HAL 的 MDIO 读写寄存器；
  不需要额外加入通用 PHY 源文件。
- ETH 由 GPIO owner 配置 AF。板级应专用这些引脚。若同一引脚仍占用 EXTI，释放时
  停机调用会在 MAC 已复位后返回错误，可在该引脚退出中断模式后重试。可用
  `status = "defer"` 将 setup 延后到板级 PHY 已就绪。
- 描述符与内部收发缓冲区统一使用 `st_dma_section`，地址须满足板级 DMA 总线、
  MPU 和安全域要求。开启数据缓存时须将此区域映射为不缓存。描述符数量由 HAL
  的 `ETH_RX_DESC_CNT` 和 `ETH_TX_DESC_CNT` 决定。
- 轮询和 Stream 都依赖 HAL 在 RX 补环写 `DMACRDTPR` 前执行 `__DSB()`。
  板级 HAL 若仍使用 `__DMB()`，接入时需同步 ST 的
  [RX 补环修复](https://github.com/STMicroelectronics/stm32h5xx-hal-driver/commit/8d26d85)。
- 自动协商从 Clause 22 的本地通告和对端通告解析 10/100 Mbit/s 及双工。固定
  10M/100M 使用全双工，对端须设置相同模式。PHY 的并行检测结果没有可通用读取的
  速率和双工字段。此时 `link()` 仍报告物理链路，收发等待双方完成受支持的
  协商，或应用改用双方一致的固定速率。
- 端口启用后允许修改 MTU。修改速率或 MAC 需先配置为 `MR_ETH_SPEED_OFF`。
  OFF 停止 MAC 和 DMA，不切断外部 PHY 电源。检测到 DMA 总线错误后也可通过
  OFF 后重新配置来恢复。
- `link()` 即时读取 PHY，`read()` / `write()` 按 100 ms 间隔更新内部链路
  模式。RX 轮询复制完整帧，保留 padding 并去掉 FCS。TX 将帧复制到私有暂存区，
  等待描述符回收最多 20 ms。超时后 DMA 仍可能发送这帧，后端在确认完成或复位前
  保留暂存区，调用方缓冲区可在调用返回后复用。
- 这些接口需要普通执行上下文和持续更新的 HAL 时基。应用应串行调用配置、查询、
  收发和停机。

## ETH Stream

- 开启 `MR_USE_DRIVER_ETH_STREAM`（默认开启）时，每个启用的 STM32H5 ETH 节点
  必须提供 `st_irq_handler` 和 `st_dma_region`。板级工程应让适配层独占 ETH 向量，
  避免重复定义。
- RX 和 TX 共用一组启停状态。挂接普通模式的 Wait 后调用任意一路 `start`，随后可提交两路帧。
  重复启动返回 `-MR_EBUSY`。两路 `is_busy` 返回相同状态。Stream 启用期间，非空普通读写返回
  `-MR_EBUSY`。调用任意一路 `stop` 可同时停两路，之后恢复普通收发。
- 应用拥有 `mr_eth_frame_t` 和帧缓冲区。DMA 直接读写 `frame.buf`，后端仅保留帧和描述符的关联。
  DMA 访问范围必须落在 `st_dma_region` 内，且由板级映射为 ETH DMA 可访问的不缓存内存。
  RX 缓冲区仍须按 32 字节对齐，`capacity` 至少为 1536 字节，涵盖 HAL 设置的 DMA 写入长度和 FCS。
  TX 缓冲区支持字节对齐，DMA 访问范围为 `frame.size`。帧对象独立于 DMA 数据区。
  RX 最多接管 `ETH_RX_DESC_CNT + 1` 个缓冲区，TX 最多接管 `ETH_TX_DESC_CNT` 帧，队列满返回
  `-MR_EAGAIN`。TX 完成和停流时均按提交顺序归还帧。
- Stream 采用一次提交、一次记录、一次通知的所有权模型。RX 提交记录与轮询缓冲池分离；
  HAL 补环只取尚未交给 DMA 的已提交记录，不会自动循环使用已归还的缓冲区。
  TX 提交失败立即撤销本次记录，不通知完成，也不接管帧。正常完成和停机取消共用记录移除逻辑。
  为允许 Wait 回调重新提交，后端在 `raise` 前解除关联并释放锁；`raise` 返回后不再访问该帧，
  也不保留旧缓冲地址。应用如需继续接收，必须显式重新提交。
- TX 在把描述符交给 HAL 前执行 `__DSB()`，确保写入不缓存 SRAM 的帧数据已完成。
  描述符和缓冲区的缓存一致性依赖板级的不缓存映射，驱动无须执行 D-Cache clean 或 invalidate。
- 链路尚未建立时可先提交 RX 缓冲区。应用在普通执行上下文定期调用 `mr_eth_link()` 推进协商和
  链路恢复。已运行的链路失效、协商模式变化或 PHY 查询失败时，后端停止 MAC/DMA，
  以 `-MR_EIO` 归还两路在途帧。Stream 保持启用，可继续提交 RX，等待链路恢复后再发送。
  PHY 查询失败时，`mr_eth_link()` 返回查询错误。发现 DMA 致命错误时，后端归还两路帧并结束 Stream。
- 成功或失败完成通过 Wait 归还原帧。RX 的 `size` 排除 FCS，`owner` 保持原值。Wait 回调可提交新帧，
  配置、启停和链路查询在回调外串行执行。停流先屏蔽 ETH IRQ 并复位 MAC/DMA，再以 `-MR_EPERM`
  归还两路全部在途帧，包括尚未挂入 RX 环的帧。保留 Wait 到停流返回。

## 流传输与资源生命周期

- 外部信号生产者由应用配置和启停；ADC/DAC stream 只配置并武装外设输入。
- TMR/PWM signal 与软件 wait event 独立；signal enable/disable 不启停生产者计数。
- TIM 的中性 TRGO 配置仍可能在软件 UG 时产生脉冲；因此必须先完成生产者重配，再武装
  ADC/DAC stream，不能把 signal disable 当作消费者侧的硬件隔离。
- 配置了 `st_dma` 的 stream 独占对应 DMA 通道；未配置时按上述各外设限制走 IRQ。
- ADC buffer 至少容纳一个完整通道 frame，大小必须是 frame 的整数倍；只有能在
  完整 frame 边界平分时才产生 HALF，单 frame 只产生 FULL。
- DAC 有限流的 DONE 在最后一个样本由外部信号送入 DOR 后产生，不等同于 DMA TC。
- ADC、DAC、UART 的 stream stop 失败时保留活动状态和 DMA waiter，
  调用方应保留 buffer、驱动和关联资源，并重试 stop；不能将错误返回视为
  已经停止硬件。成功停止后才释放 waiter 和流状态。
- 在普通调用上下文执行 stop；DMA 的阻塞停止可能依赖 HAL 时基，
  不应在同步事件回调的 IRQ 上下文执行。
- DMA offset 的单位为字节，错误回调保存当前进度；因 HAL ERROR 状态
  重建通道时保留停止进度，直到下一次配置或启动传输。
- 有限 DMA 始终保留 TC 中断以完成 HAL 状态和 cache 收尾。循环 DMA 仅在订阅
  HALF/DONE 或 cache 所有权维护需要时启用对应完成中断；无完成事件订阅的固定 TX
  模式不会每轮打断 CPU，ERROR 中断仍始终启用。

## UART 事件

- `TX_STREAM_DONE` 表示 DMA 已将本轮 buffer 送入 UART。
- 有限 TX stream 在 DMA 完成后才启用 TC 完成通知，`TX_DONE` 表示
  最后一个停止位已发送。DMA 供数间隙出现的 TC 不作为整条流完成。
- stream 方向必须与 UART 配置的收发方向相容，否则返回 `-MR_EINVAL`。

## 普通 ADC 读取

普通读取在后续转换失败时返回此前已经写入 buffer 的字节数；
尚未完成任何样本时返回相应错误码。

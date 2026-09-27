# GPIO 驱动移植指南

> 本文说明如何为新的 MCU/SoC 实现片上 GPIO backend。GPIO 使用方式见 [GPIO](gpio.md)。

## 1. 移植边界

GPIO 公共层负责存在位图、逻辑反相和类型化 API；平台 backend 负责 pin mode、实际读写、端口批量访问以及可选中断事件。

平台实现通常位于：

```text
mrX/arch/<vendor>/<target>/platform/drivers/gpio/
```

HC165/HC595 属于建立在 GPIO API 上的扩展驱动，不是片上 GPIO 平台 backend。

## 2. 操作表

`mr_gpio_ops_t` 包含：

- `pin_configure` / `pin_configuration`
- `pin_write` / `pin_read`
- `port_write` / `port_read`
- 可选 `event_attach` / `event_control`

`mr_gpio_data_t.exist_mask` 描述实际存在 pin，`invert_mask` 由公共层处理逻辑反相。backend 接收到的 pin value
已经处于公共层定义的硬件侧语义，不要再次重复反相。

## 3. Pin 配置

backend 需要把 `mr_gpio_config_t` 映射到目标芯片的：

- input/output/analog/alternate/IRQ 等模式；
- pull；
- output type；
- alternate function；
- IRQ edge/level（若平台支持）。

芯片不支持的组合应由 backend 明确返回错误，不要悄悄映射成不同模式。

## 4. 读写路径

单 pin 读写应尽量使用芯片提供的原子 set/reset 机制。端口批量读写若硬件有原生寄存器，应直接利用；没有时再按平台需要组合实现。

注意 `mr_gpio_toggle` 是公共层的读-改-写，不具备跨执行流原子性，backend 不需要为了 toggle 在普通 pin_read/write 中额外保存软件影子。

## 5. GPIO 事件

GPIO 的事件 waitable 由 backend 管理。典型实现为每个可中断 pin 保存：

- EXTI/IRQ 映射；
- waiter/waitable；
- 触发类型；
- 当前启用状态。

`event_attach` 只负责订阅，`event_control` 才真正打开/关闭硬件中断来源。ISR 应先清 pending，再通过 waitable raise
`MR_GPIO_WAIT_EXTI`。若提供 payload，保持其格式在同一 backend 内稳定。

切换 pin 离开 IRQ 模式时，应释放对应映射并关闭来源，避免陈旧 waiter 继续收到硬件事件。

## 6. HardTree 与安装

片上 GPIO 推荐使用 `MR_HT_GPIO_INSTALL`。HardTree 属性通常包括端口实例、存在 pin、反相位图以及平台需要的中断/时钟资源。

GPIO spec 会被其他平台驱动复用，因此 pin 编号和端口实例的定义必须长期稳定。

## 7. 参考实现

当前可参考：

```text
mrX/arch/st/stm32h5/platform/drivers/gpio/gpio.c
```

它展示了 HardTree 实例化、GPIO 模式映射、EXTI pin→IRQ 管理和 ISR waitable 通知。

上层扩展实现可参考：

```text
mrX/drivers/gpio/gpio_hc165.c
mrX/drivers/gpio/gpio_hc595.c
```

## 8. 移植检查清单

- exist_mask/invert_mask 与板级定义一致；
- mode/pull/output/alternate 映射无静默降级；
- pin write 使用合适的原子寄存器路径；
- port 操作不误写不存在 pin；
- IRQ pending 正确清除；
- event enable/disable 与 pin reconfigure 生命周期一致；
- HardTree GPIO spec 能被其他驱动稳定引用。

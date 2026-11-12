# GPIO 驱动移植指南

> 本文面向平台 backend 开发者，说明如何为新的 MCU/SoC 或扩展芯片实现 GPIO
> 操作表。公共 API 的使用方式见 [GPIO](gpio.md)；芯片特有运行期语义见对应
> 平台的 `README.md`。

## 1. 移植边界

GPIO 公共层负责存在掩码校验、`invert_mask` 逻辑电平换算、类型化 API 包装和
waitable 分发；平台 backend 负责把 `mr_gpio_config_t` 映射为实际引脚模式、真实
读写、端口批量访问以及可选的硬件引脚事件来源。

公共层与 backend 的职责分工是固定的：

| 关注点                        | 负责方                                |
|-------------------------------|---------------------------------------|
| `exist_mask` / `invert_mask`  | 公共层（`mr_gpio_data_t`）            |
| 输出初始电平的反相换算        | 公共层，在调用 `pin_configure` 前完成 |
| 引脚模式、上下拉、输出类型    | backend                               |
| 端口原子的置位/清除           | backend                               |
| 中断映射、waitable、启用状态 | backend                               |

backend 收到的 `pin` 参数是 **引脚号**（0~31），不是位掩码；`mr_gpio_data_t` 的
`exist_mask`/`invert_mask` 才是位掩码。两套表示不要在实现中混用。

backend 收到 `pin_write` 的 `value` 已经处于 **物理**电平语义（公共层已完成
`invert_mask` 换算），不要再反相一次。

平台实现通常位于：

```text
mrX/arch/<vendor>/<target>/platform/drivers/gpio/
```

HC165/HC595 属于建立在 GPIO API 之上的扩展驱动（`mrX/drivers/gpio/`），不是片上
GPIO backend；它们的编译只依赖 `MR_USE_DRIVER_GPIO` 和对应的 HardTree compat
实例，没有独立 Kconfig 开关。

## 2. 操作表

backend 实现 `mrX/include/mrX/drivers/gpio/mr_gpio.h:84` 的 `mr_gpio_ops_t`：

| 成员                | 签名要点                                           | 必需 |
|---------------------|----------------------------------------------------|------|
| `pin_configure`     | `(drv, pin, const mr_gpio_config_t *config)`       | 是   |
| `pin_configuration` | `(drv, pin, mr_gpio_config_t *config)`             | 是   |
| `pin_write`         | `(drv, pin, mr_uint8_t value)`，返回 `mr_err_t`    | 是   |
| `pin_read`          | `(drv, pin, mr_uint8_t *value)`                    | 是   |
| `port_write`        | `(drv, mr_uint32_t mask)`                          | 是   |
| `port_read`         | `(drv, mr_uint32_t *mask)`                         | 是   |
| `event_attach`      | `(drv, pin, mr_waiter_t *entry)`                   | 否   |
| `event_control`     | `(drv, pin, mr_uint32_t events, mr_bool_t enable)` | 否   |

公共层对前六个成员使用 `MR_ASSERT` 强制非空（`mr_gpio.h` 的 `mr_gpio_write`
等入口），因此 **没有**“省略必需成员返回 `-MR_ENOSYS`”的降级路径。`event_attach`
与 `event_control` 缺失时，`mr_gpio_event_attach`/`mr_gpio_event_enable`/
`mr_gpio_event_disable` 返回 `-MR_ENOSYS`。

所有回调首参为 `const mr_driver_t *drv`；实现侧用 `MR_DRIVER_PRIV(drv)` 取回本
实例私有数据，用 `MR_DRIVER_DATA(drv)` 取回 `mr_gpio_data_t`。

## 3. Pin 配置

backend 应将 `mr_gpio_config_t` 的模式、上下拉、输出类型、复用与初始电平映射到硬件。公共层处理存在掩码和逻辑反相；backend 只针对真实物理引脚执行配置。复用引脚的所有权、释放与可能的资源冲突由平台实现负责。

## 4. 引脚与端口读写

实现单引脚和端口批量 read/write/toggle，确保原子性与并发安全。使用硬件支持的原子置位/清除能力时应保持掩码正确；若只能读改写，须防止并发覆盖。返回的是公共层期望的物理电平或操作结果，不要在 backend 再次应用逻辑反相。

## 5. 事件与中断

可选的 `event_attach` 与 `event_control` 用于配置硬件中断来源和 waiter。backend 负责 IRQ 资源归属、模式切换、pending 清理，以及在正确的上下文中调用 `mr_gpio_event_raise()`。waiter 收到的事件负载应符合公共 `mr_gpio_event_data_t` 契约；暂时未实现的中断能力应明确返回 `-MR_ENOSYS`。

## 6. HardTree 与安装

使用 `MR_HT_GPIO_INSTALL` 或驱动框架的注册机制。`exist_mask`/`invert_mask` 为公共属性；具体寄存器、IRQ、复用编号、时钟和引脚资源均由平台自身描述，不属于 GPIO 公共层规范。

## 7. 移植验收

- [ ] 模式、上下拉、输出类型、逻辑反相和 port 掩码正确。
- [ ] IRQ 触发、开关、数据负载、模式重配和退出清理一致。
- [ ] 同一引脚的复用与事件归属不会泄漏或相互覆盖。

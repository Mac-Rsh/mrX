# GPIO 驱动移植指南

> 本文面向平台 backend 开发者，说明如何为新的 MCU/SoC 或扩展芯片实现 GPIO
> 操作表。公共 API 的使用方式见 [GPIO](gpio.md)；芯片特有运行期语义见对应
> 平台的 `README.md`。

## 1. 移植边界

GPIO 公共层负责存在掩码校验、`invert_mask` 逻辑电平换算、类型化 API 包装和
waitable 分发；平台 backend 负责把 `mr_gpio_config_t` 映射为实际引脚模式、真实
读写、端口批量访问以及可选的 EXTI 事件来源。

公共层与 backend 的职责分工是固定的：

| 关注点                        | 负责方                                |
|-------------------------------|---------------------------------------|
| `exist_mask` / `invert_mask`  | 公共层（`mr_gpio_data_t`）            |
| 输出初始电平的反相换算        | 公共层，在调用 `pin_configure` 前完成 |
| 引脚模式、上下拉、输出类型    | backend                               |
| 端口原子的置位/清除           | backend                               |
| EXTI 映射、waitable、启用状态 | backend                               |

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

`pin_configure` 需要把 `mr_gpio_config_t` 的 5 个位域映射到目标芯片：

| 字段    | 取值                                                                               | 映射要求                          |
|---------|------------------------------------------------------------------------------------|-----------------------------------|
| `mode`  | ANALOG / INPUT / OUTPUT / IRQ_RISING / IRQ_FALLING / IRQ_EDGE / IRQ_HIGH / IRQ_LOW | 全部 8 种取值都要有明确结果       |
| `alt`   | `MR_GPIO_ALT_DISABLE` / `MR_GPIO_ALT_ENABLE`                                       | 见下                              |
| `pull`  | NONE / UP / DOWN                                                                   | 芯片不支持的组合必须报错          |
| `otype` | PP / OD                                                                            | 仅输出模式有意义                  |
| `level` | LOW / HIGH（物理电平）                                                             | 仅 `MR_GPIO_MODE_OUTPUT` 需要写入 |

约定：

- `mode == MR_GPIO_MODE_OUTPUT` 时必须在切换到输出模式 **之前**写入 `level`，避免
  引脚在上电瞬间出现毛刺；公共层不会另外调用 `pin_write`。
- 只有 `MR_GPIO_MODE_OUTPUT` 时才使用 `otype`；其他模式下 `otype`/`level`
  无意义。
- 芯片不支持的组合（例如只支持边沿触发却收到 `MR_GPIO_MODE_IRQ_HIGH`）应返回
  明确错误，不要静默映射成另一种模式。
- `alt` 是一个仅有开关语义的位域， **不携带复用功能编号**。因此“支持复用功能”的
  backend 无法只靠 `mr_gpio_config_t` 完成映射：要么返回 `-MR_ENOSYS`，要么另外
  提供平台私有的复用入口。片上 GPIO 被其他外设复用时，应走该平台的 GPIO
  owner helper（STM32H5 提供 `mr_stm32h5_gpio_pin_alternate` /
  `mr_stm32h5_gpio_pin_release`，声明见 `mrX/arch/st/stm32h5/include/platform/mr_platform.h`），
  而不是让外设自己去写 GPIO 寄存器。
- `pin_configure` 在 IRQ 模式下需要完成中断线归属登记；具体约定见第 5 节。

`pin_configuration` 必须返回 backend **实际采用**的配置，而不是回显用户请求。
大多数 HAL 没有配置回读接口，实现通常直接读回寄存器并按同样的映射表反解。

## 4. 读写路径

- 单引脚写优先使用芯片的原子置位/清除寄存器（例如 STM32 的 BSRR），不要用
  读-改-写。
- `port_write` 的 `mask` 中为 1 的位表示输出高。若硬件有原生端口写寄存器，
  应一次事务完成置位与清除两组引脚；STM32H5 backend 用
  `HAL_GPIO_WriteMultipleStatePin` 同时下发 set 与 reset 两组。
- `port_read` 返回 **全部**端口的物理电平掩码；公共层会负责与 `exist_mask` 求交
  并对 `invert_mask` 反相。backend 不需要自己过滤不存在的引脚。
- `port_write` 收到的是公共层面已经 `& exist_mask` 的掩码，因此 backend 不需要
  再屏蔽不存在的引脚；对超出自身位宽的位（例如 16 位端口收到高 16 位）直接忽略
  即可。
- `mr_gpio_toggle` 是公共层的“读-改-写”，不具备跨执行流原子性；backend 不需要
  为它维护软件影子寄存器。注意 `mr_gpio_toggle` 直接使用 `ops->pin_read`/
  `ops->pin_write` 的物理语义， **不**经过 `invert_mask`。

## 5. GPIO 事件

事件能力由两半组成，职责不同：

- `event_attach(drv, pin, entry)` **只订阅** waiter：调用 `mr_waiter_wait(entry,
  &waitable)` 建立 waiter 与 backend waitable 的绑定，不碰硬件中断使能位。
- `event_control(drv, pin, events, enable)` 才真正打开/关闭硬件事件来源。

典型实现为每条可中断引脚保存一份映射项，内容包括所属驱动指针、触发模式、
当前启用状态和 `mr_waitable_t`。芯片的 EXTI 线通常是 **全局资源**（同一条线在不同
端口间互斥），因此这张表按中断线编号而不是按端口实例建立。

约定：

- 配置为 IRQ 模式时先完成归属登记，再使能 NVIC；切回非 IRQ 模式时释放映射并
  关闭该来源，避免陈旧 waiter 继续收到硬件事件。
- 同一条中断线被不同驱动实例争用时，后到者应返回 `-MR_EBUSY`；实例自身已拥有
  该线时重复配置应保持幂等。
- `events` mask 由公共层原样透传，backend 必须自己校验支持的位；不支持的位
  返回 `-MR_EINVAL`；该引脚未被本驱动登记为 IRQ 模式时返回 `-MR_EPERM`。
- ISR 中先清 pending，再通过 `mr_waitable_raise` 通知。若附带 payload，保持其
  格式在同一 backend 内稳定；GPIO 公共头文件没有定义统一的 payload 类型。
- `waitable` 由 backend 管理，按需使用 `mr_waitable_init`；不要在 ISR 中做阻塞
  操作。

STM32H5 backend 的可作为参考的取舍：它用公共配置中被 HAL 初始化过的 EXTI 状态
直接改 `EXTI->IMR1` 而不重跑 `HAL_GPIO_Init`，并用强定义的
`HAL_GPIO_EXTI_Rising_Callback`/`HAL_GPIO_EXTI_Falling_Callback` 接管 HAL 的弱
回调。

## 6. HardTree 与安装

片上 GPIO 推荐用 `MR_HT_GPIO_INSTALL(_id, _priv, _ops)` 按 compat 实例批量安装。
该宏会自行构造 `mr_gpio_data_t`，因此 backend 不要在私有数据里重复保存掩码。
两个数据成员的取值来自节点属性，缺省为 `exist_mask = 0xffffffff`、
`invert_mask = 0`。

STM32H5 backend（compat `st_stm32h5_gpio`）读取的属性：

| 属性                 | 必需 | 说明                                                                                                                                        |
|----------------------|------|---------------------------------------------------------------------------------------------------------------------------------------------|
| `st_gpiox`           | 是   | 端口寄存器基址（如 `GPIOA`）                                                                                                                |
| `st_clken`           | 是   | 端口时钟使能函数                                                                                                                            |
| `st_irq_priority`    | 是   | EXTI NVIC 优先级                                                                                                                            |
| `exist_mask`         | 否   | 缺省 `0xffffffff`                                                                                                                           |
| `invert_mask`        | 否   | 缺省 `0`                                                                                                                                    |
| `st_irq_handler_<n>` | 否   | 声明 EXTI 线 `n` 的向量名（如 `EXTI5_IRQHandler`）；**只有声明了该属性，线 `n` 的中断处理函数才会生成**，该线也才可能被 `event_attach` 接受 |

注意 `exist_mask` 只影响公共层校验，backend 不会按它限制寄存器访问；把它写成
真实存在的引脚集合是板级描述的责任。

GPIO spec 会被其他平台驱动复用（UART/ADC/DAC/PWM 的引脚属性都是
`mr_ht_gpio_spec_t`），因此 **pin 编号和端口实例的命名必须长期稳定**，不要在移植
过程中重新编号。

## 7. 参考实现

片上 GPIO：

```text
mrX/arch/st/stm32h5/platform/drivers/gpio/gpio.c
```

该实现展示了 HardTree 实例化、GPIO 模式映射、EXTI 线归属检查、NVIC 使能与
ISR waitable 通知，以及 GPIO owner 的复用功能 helper。

上层扩展和软件 backend：

```text
mrX/drivers/gpio/gpio_hc165.c     # 输入扩展（compat gpio_hc165）
mrX/drivers/gpio/gpio_hc595.c     # 输出扩展（compat gpio_hc595）
```

两者用普通 GPIO 引脚拼出新的“GPIO 端口”，实现同一张 `mr_gpio_ops_t`，可用来
对照“如何在没有片上 EXTI 的情况下处理事件能力缺失”。

值得按函数名对照的实现：`gpio_pin_configure`（模式映射与 EXTI 归属检查）、
`gpio_pin_configuration`（读回寄存器反解）、`gpio_event_control`（只改 IMR1 而不重跑
`HAL_GPIO_Init`）、`gpio_irq_handle` 与 HAL 的 `HAL_GPIO_EXTI_Rising_Callback`/
`HAL_GPIO_EXTI_Falling_Callback` 强覆盖（ISR 到 waitable 的接法）。

## 8. STM32H5 能力边界（新 port 不要默认继承）

| 能力/约定           | H5 现状                                                                                           | 新 port 需要确认                             |
|---------------------|---------------------------------------------------------------------------------------------------|----------------------------------------------|
| 引脚范围            | 每个端口固定 16 个引脚，`pin >= 16` 返回 `-MR_EINVAL`                                             | 端口位宽，是否允许非连续引脚编号             |
| 复用功能            | `mr_gpio_config_t` 不携带 AF 号；`alt != DISABLE` 返回 `-MR_ENOSYS`，AF 由 GPIO owner helper 配置 | 是否需要按引脚声明 AF、由谁拥有复用配置      |
| 电平触发            | EXTI 只支持边沿，`MR_GPIO_MODE_IRQ_HIGH`/`LOW` 返回 `-MR_ENOSYS`                                  | 芯片是否支持电平触发中断                     |
| 中断线粒度          | 每条 EXTI 线一个 NVIC 向量；线跨端口全局互斥                                                      | 中断是共享向量还是每线一个，是否需要软件分发 |
| `port_read` 来源    | 直接读 `IDR` 低 16 位（HAL 无端口读接口）                                                         | 端口读寄存器与有效位宽                       |
| `port_write` 原子性 | 一次 HAL 调用同时下发 set/reset                                                                   | 是否有等价的原子置位/清除寄存器              |
| 事件 payload        | ISR 中附带触发引脚号，回调处于中断上下文                                                          | payload 格式与回调上下文                     |
| `exist_mask` 强制力 | 只影响公共层校验，backend 不据此限制寄存器访问                                                    | 是否需要 backend 自行屏蔽                    |

任何一条“H5 支持/不支持”都不能当作通用结论写进新 backend 的文档。

## 9. 移植验收清单

- `exist_mask`/`invert_mask` 与板级实际接线一致，且 `invert_mask` 只用于低有效
  电路，不用于补偿 backend 的反相错误。
- 8 种 `mode` 都有确定结果；不支持的组合返回明确错误而非静默降级。
- `pull`/`otype` 映射无越界索引（`pull` 的位域可以取到未定义的第 4 个值）。
- 输出模式在切换方向前写入初始电平。
- `pin_write` 使用原子置位/清除路径。
- `port_write` 不误写不存在的引脚，`port_read` 返回完整端口掩码。
- `pin_configuration` 返回实际硬件配置，并与 `pin_configure` 可稳定往返。
- IRQ 线归属、NVIC 使能/关闭与模式切换生命周期一致，切换离开 IRQ 模式后不再有
  硬件事件送到陈旧 waiter。
- ISR 先清 pending 再 raise；不支持的 `events` 位返回 `-MR_EINVAL`。
- 复用功能通过 GPIO owner helper 配置，不与其他外设直接改 GPIO 寄存器冲突。
- HardTree 的 `exist_mask`/`invert_mask`/`st_irq_handler_<n>` 与实际硬件一致。

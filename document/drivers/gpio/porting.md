# GPIO 驱动移植指南

本文面向平台 backend 开发者，说明如何为新的 MCU/SoC 或外设扩展芯片实现 GPIO
驱动。公共接口用法见 [GPIO 驱动](gpio.md)。

## 1. 移植边界

| 职责                                 | 公共层                              | Backend                                               |
|--------------------------------------|-------------------------------------|-------------------------------------------------------|
| `exist_mask` 校验与端口过滤          | 负责（配置/事件校验，端口掩码过滤） | 不必重复过滤                                          |
| `invert_mask` 逻辑电平换算           | 负责（读写及输出初始电平换算）      | 仅处理物理电平，切勿再次取反                          |
| 引脚模式、上下拉、输出类型与复用配置 | 透传 `mr_gpio_config_t`             | 映射到硬件寄存器并管理复用冲突                        |
| 单引脚与端口原子读写                 | 调用 `ops`                          | 保证寄存器读写原子性（如使用置位/清除寄存器）         |
| 外部中断（EXTI）与事件通知           | 透传 waiter 与事件掩码              | 管理 IRQ 资源、清除 pending 并在合适上下文唤醒 waiter |

backend 回调收到的 `pin` 参数是引脚编号（`0`~`31`），而不是位掩码；`mr_gpio_data_t`
中的 `exist_mask` 与 `invert_mask` 才是 32 位掩码，实现时注意区分。

## 2. 操作表（ops）

backend 需实现 `include/mrX/drivers/gpio/mr_gpio.h` 中定义的 `mr_gpio_ops_t`：

| 回调                | 签名要点                                                                          | 必需 | 返回值约定                                            |
|---------------------|-----------------------------------------------------------------------------------|------|-------------------------------------------------------|
| `pin_configure`     | `(const mr_driver_t *drv, mr_uint32_t pin, const mr_gpio_config_t *config)`       | 是   | 成功返回 `0`，失败返回负数错误码                      |
| `pin_configuration` | `(const mr_driver_t *drv, mr_uint32_t pin, mr_gpio_config_t *config)`             | 是   | 成功返回 `0`，失败返回负数错误码                      |
| `pin_write`         | `(const mr_driver_t *drv, mr_uint32_t pin, mr_uint8_t value)`                     | 是   | 成功返回 `0`，失败返回负数错误码                      |
| `pin_read`          | `(const mr_driver_t *drv, mr_uint32_t pin, mr_uint8_t *value)`                    | 是   | 成功返回 `0`，`*value` 为 `0` 或 `1`                  |
| `port_write`        | `(const mr_driver_t *drv, mr_uint32_t mask)`                                      | 是   | 成功返回 `0`，失败返回负数错误码                      |
| `port_read`         | `(const mr_driver_t *drv, mr_uint32_t *mask)`                                     | 是   | 成功返回 `0`，失败返回负数错误码                      |
| `event_attach`      | `(const mr_driver_t *drv, mr_uint32_t pin, mr_waiter_t *entry)`                   | 否   | 成功返回 `0`，不支持时留空（公共层返回 `-MR_ENOSYS`） |
| `event_control`     | `(const mr_driver_t *drv, mr_uint32_t pin, mr_uint32_t events, mr_bool_t enable)` | 否   | 成功返回 `0`，不支持时留空（公共层返回 `-MR_ENOSYS`） |

前 6 个基础操作在公共层通过 `MR_ASSERT` 强制非空，不可留空。所有回调首参为
`const mr_driver_t *drv`，可通过 `MR_DRIVER_PRIV(drv)` 获取硬件私有上下文，通过
`MR_DRIVER_DATA(drv)` 获取 `mr_gpio_data_t`。

## 3. 关键契约

- **物理电平约定**：`pin_write` 收到的 `value` 和 `pin_configure`（输出模式）收到的
  `config->level` 均已由公共层完成 `invert_mask` 换算，属于物理电平；`pin_read`、
  `port_read` 及 `pin_configuration` 回读时也应直接返回物理电平。
- **并发与原子性**：公共层读写无互斥锁。片上 GPIO 应优先使用硬件原子置位/清除寄存器实现
  `pin_write` 与 `port_write`；若硬件仅支持读-改-写（如部分 I2C/SPI 扩展芯片），backend
  需自行加锁或关中断保护并发访问。
- **事件与中断上下文**：`event_attach` 负责保存或挂接 `mr_waiter_t`；
  `event_control` 根据 `enable`（`MR_TRUE`/`MR_FALSE`）及 `events` 掩码（如
  `MR_GPIO_WAIT_EXTI`）启停硬件中断并清理 pending 标志。中断触发时，backend 在 ISR
  中通过等待队列唤醒 waiter，若传递 `mr_gpio_event_data_t`（携带 `pin`
  ），需注意其指针仅在同步回调期间有效。若引脚尚未配置为 `MR_GPIO_MODE_IRQ_*` 就调用
  `event_control` 使能，建议返回明确的错误码（如 `-MR_EINVAL` 或 `-MR_EPERM`）。

## 4. HardTree 安装

使用 `MR_HT_GPIO_INSTALL(_id, _priv, _ops)` 安装实例。该宏会自动读取节点上的标准可选属性：

| HardTree 属性 | 默认值        | 说明                          |
|---------------|---------------|-------------------------------|
| `exist_mask`  | `0xffffffffU` | 32 位存在掩码，标记有效引脚   |
| `invert_mask` | `0x00000000U` | 32 位反相掩码，标记低有效引脚 |

其余寄存器基址、时钟、IRQ 号、引脚复用编号等均属于平台私有属性，由具体 backend 自行解析。

## 5. 验收要点

- 验证所有支持的模式（输入/输出/模拟/中断）、上下拉及推挽/开漏配置生效，且
  `mr_gpio_configuration` 回读一致。
- 验证 `exist_mask` 越界拒绝（`-MR_EINVAL`）与 `invert_mask` 下 `write`/`read`/
  `configure`/`port_write`/`port_read` 的逻辑电平换算正确。
- 若支持 EXTI：验证 `attach` -> `enable` -> 触发回调（`mr_gpio_event_data_t`） ->
  `disable` -> 模式重配流程无中断残留或资源泄漏。
- 跑通 `test/drivers/gpio/gpio.c` 行为测试。

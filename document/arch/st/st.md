# ST 平台适配说明与规范

> 本文约束 `mrX/arch/st/` 下的芯片适配怎么分层、怎么声明资源、怎么写 backend。
> 它不替代 [平台移植层](../../platform/platform.md) 的 `mr_port_*` 契约，也不替代各驱动
> `porting.md` 的 ops 语义。片上 IP 的寄存器映射仍读对应 `porting.md`；
> 本文只规定 ST 适配层的章法，避免再按外设各写一套 HAL 胶水。

| 项       | 说明                                                                                         |
|----------|----------------------------------------------------------------------------------------------|
| 适用范围 | `mrX/arch/st/<target>/`，当前参考实现是 `stm32h5`                                            |
| 功能宏   | 无独立功能宏；平台安装后提供 `MR_USE_PLATFORM` / `MR_USE_STM32H5` 等                          |
| 参考实现 | `mrX/arch/st/stm32h5/`；组合适配范本 [HC165](../../drivers/gpio/gpio_hc165.md)、[HC595](../../drivers/gpio/gpio_hc595.md) |
| 相关文档 | [Platform](../../platform/platform.md)、[驱动框架](../../drivers/driver.md)、各驱动 `porting.md`                 |

ST 适配的目标是：HardTree 里声明了什么，适配层就只为那些实例编译、分配和占用相应资源；
外设通过已有驱动的 spec 消费引脚和通道，而不是再开一份 HAL 去抢同一块硬件。

---

## 1. 文档怎么用

- 移植新的 ST 芯片系列：先按 [Platform](../../platform/platform.md) 建好 `arch/st/<target>/` 与 port，再按本文组织 `platform/drivers/`。
- 给已有 ST 平台加一类外设：先读该外设 `porting.md`，再按第 3～6 节套文件骨架。
- 修改 `stm32h5` 现有驱动：以本文为章法，不要回到“整表预分配 + 外设自己 Init GPIO”的写法。第 8 节记录的是重写后仍保留的实现选择。

范本分工：

| 范本 | 路径 | 说明什么 |
|------|------|----------|
| 组合适配（优先对照） | `mrX/drivers/gpio/gpio_hc165.c`、`gpio_hc595.c` | 实例门控、可选资源、只引用下层 spec、按能力填 ops |
| 片上拥有者 | `mrX/arch/st/stm32h5/platform/drivers/gpio/gpio.c`、`dma/dma.c`、`tmr/tmr.c` | 一个 IP 块只由一个 backend 拥有 |
| 片上消费者 | 同目录 `uart/uart.c`、`adc/adc.c`、`dac/dac.c` | 应通过 GPIO/DMA/TMR spec 组装，而不是再管一份引脚 HAL |

HC165/HC595 不在 `arch/` 里，但它们是目前仓库里最完整的“虚拟适配层”：不碰寄存器，只消费 GPIO，再对外呈现同一张 `mr_gpio_ops_t`。ST 的 UART/ADC/DAC 属于同一类组合适配，差别只是下层除了 GPIO 还有 DMA 和定时器。

---

## 2. 分层

```
        +------------------------------------------+
        |  应用 / 模块（mr_gpio_*、mr_uart_* …）     |
        +-------------------+----------------------+
                            | 公共 API、exist/invert、生命周期
        +-------------------v----------------------+
        |  驱动公共层（mrX/drivers/<ip>/）           |
        +-------------------+----------------------+
                            | ops
        +-------------------v----------------------+
        |  组合适配（消费者）                         |
        |  UART / ADC / DAC / HC165 / HC595          |
        |  私有数据里只有 spec 与协议状态              |
        +-------------------+----------------------+
                            | mr_ht_gpio_spec_t / mr_ht_dma_spec_t / 驱动指针
        +-------------------v----------------------+
        |  片上拥有者                                 |
        |  GPIO 拥有 GPIOx+EXTI；DMA 拥有通道；        |
        |  TMR 拥有 TIMx                              |
        +-------------------+----------------------+
                            | Cube HAL / 寄存器
        +-------------------v----------------------+
        |  平台 port（clock / irq / output / assert） |
        +------------------------------------------+
```

三层职责不可混写进同一个私有结构而不加区分：

1. **平台 port**：只实现内核契约，见 [Platform](../../platform/platform.md)。不在 port 里初始化 UART/GPIO 外设。
2. **拥有者 backend**：独占一块片上 IP。GPIO 独占端口与 EXTI 线，DMA 独占通道，TMR 独占定时器。它们可以对 HAL 说话。
3. **消费者 backend**：把已有驱动组合成新设备。私有数据保存 `mr_ht_*_spec_t` 或驱动指针，通过公共 API 操作下层。HC165/HC595 是这一层的完整样本。

禁止第三层再 `HAL_GPIO_Init` / `HAL_DMA_Init` 去操作第一层已经拥有的对象。
一块引脚、一条 DMA 通道、一个定时器在同一时刻只有一个拥有者。

---

## 3. 资源规则

资源浪费通常来自四件事：没有实例就不该编译的代码仍进固件；芯片最大值被当成每个板子的实际用量；可选能力按 Kconfig 全局打开后摊到每一个实例；消费者绕过拥有者再配一次硬件。

### 3.1 谁拥有，谁引用

| 资源 | 拥有者 | 消费者如何拿到 | 禁止 |
|------|--------|----------------|------|
| GPIOx 引脚、EXTI 线 | `st_<chip>_gpio` | `mr_ht_gpio_spec_t`，属性单元 `[phandle, pin]` | 消费者 `HAL_GPIO_Init`、另写一份时钟使能去开同一端口 |
| GPDMA 通道 | `st_<chip>_gpdma` | `mr_ht_dma_spec_t`，属性单元 `[phandle, channel]` | 消费者保存 `DMA_HandleTypeDef` 或直接写 DMA 寄存器 |
| TIMx | `st_<chip>_tmr` | `MR_HT_DRIVER_FIND` / 驱动指针 | 消费者 `HAL_TIM_Base_Init` |
| USARTx / ADCx / DACx | 对应消费者自己 | HardTree 的 `st_uartx` 等 | 两个实例指向同一外设基址 |

同一引脚不能既出现在某个 GPIO 实例的 `exist_mask` 里，又被 UART/ADC/DAC 私下改 MODER。需要给外设独占的引脚，要么从 GPIO `exist_mask` 去掉，要么（目标形态）由 GPIO 配置成 AF/模拟后再交给消费者使用。

### 3.2 编译期只为存在的实例出码

每个 backend 文件在定义 `MR_HT_COMPAT` 之后必须用 `MR_HT_HAS_DRIVER()` 包住 ops、私有类型、IRQ 处理函数和安装宏。这是 HC165/HC595 已经在用的门：

```c
#define MR_HT_COMPAT                    st_stm32h5_uart
#if MR_HT_HAS_DRIVER()
/* ops、priv、IRQ、安装宏 */
#endif /* MR_HT_HAS_DRIVER() */
```

`MR_HT_DRIVER_FOREACH` 在没有实例时会展开为空，但 **FOREACH 不会删掉** 文件里那些与实例无关的全局表、弱符号 IRQ 和处理函数。GPIO 的 16 路 EXTI 处理函数、DMA 的 8×N 路通道 IRQ，只要没被 `MR_HT_HAS_DRIVER()` 包住，即使板级模板一个实例都没有，也会进当前编译单元。

`MR_USE_DRIVER_*` 只表示这类驱动参与构建；HardTree 里没有对应 compat，该 `.c` 就应当几乎是空翻译单元。

### 3.3 表的大小跟 HardTree 走，不跟芯片手册走

芯片手册上的“最多 16 条 EXTI、每台 GPDMA 8 个通道”不是每个工程的静态用量。

- GPIO：`exist_mask` 描述本实例真正存在的 pin；EXTI waitable、irq map、IRQ 处理函数只为模板声明会用到的线生成。未声明的线不占 `mr_waitable_t`，也不抢向量。
- DMA：通道数组、`DMA_HandleTypeDef` / node / queue、IRQ 处理函数按本实例声明的通道集合生成，而不是 `channels[8]` 一配到底。
- ADC/DAC：通道表来自 `st_channels` 实际元素个数，不要按手册最大值预留空槽。
- 消费者的 stream 字段：该实例有 `st_rx_dma` / `st_timer` 才生成 DMA waiter 和 transfer；Kconfig 打开 stream 不等于每个实例都有 stream。

允许为“芯片全局且天然稀疏”的资源保留一张小的全局所有权表（例如 EXTI0～15 的占用者指针），但表项指向的 waitable、NVIC 使能和向量函数仍按声明按需出现。

### 3.4 可选资源按属性存在与否展开

对照 HC165 的 CLK-INH / SER：属性不存在就填 `{0}`，运行时看到 `spec.drv == MR_NULL` 则跳过。ST 消费者同样处理可选 DMA、可选触发定时器、可选 RTS/CTS。

```c
MR_MACRO_IF(MR_HT_DRIVER_HAS_PROP(_id, clk_inh),
            (.clk_inh = MR_HT_GPIO_SPEC(_id, clk_inh)),
            (.clk_inh = {0})),
```

禁止因为工程打开了 `MR_USE_DRIVER_ADC_STREAM`，就对每一个 ADC 实例强制 `MR_HT_DMA_SPEC(_id, st_dma)` 和 `MR_HT_DRIVER_PROP(_id, st_timer)`。没有这些属性的实例应是普通轮询 ADC，stream ops 返回 `-MR_ENODEV` 或在安装时不挂 stream 回调。

### 3.5 懒初始化，不在安装时点亮整片 IP

安装宏只填充描述性字段（基址、spec、时钟入口、IRQ 号）。时钟使能、`HAL_*_Init`、NVIC、waitable 初始化发生在第一次真正用到该资源的 configure/start 路径。HC165 用 `configured` 保证宿主引脚只初始化一次；HC595 的 shadow 在值没变时跳过整轮移位。片上拥有者沿用同一原则：未配置的 DMA 通道不要 `HAL_DMA_Init`，未进入 IRQ 模式的 EXTI 线不要 `HAL_NVIC_EnableIRQ`。

---

## 4. 文件与代码骨架

### 4.1 目录

```text
mrX/arch/st/<target>/
├── install.toml
├── README.md                          # 本芯片运行期约定；章法以本文为准
├── include/platform/
│   ├── Kconfig
│   └── mr_platform.h                  # 平台 init、HAL 状态换算、芯片专有 helper
└── platform/
    ├── platform.c                     # mr_platform_init；不初始化具体外设
    ├── port/                          # clock / irq / output / assert / cache
    └── drivers/
        └── <ip>/<ip>.c                # 一类 IP 一个编译单元
```

一类 IP 一个 `.c`。不要把 GPIO 时钟表塞进 UART，也不要在 `platform.c` 里为每个外设预 Init。芯片专有、且确实被多个 backend 共享的纯函数（例如 HAL 状态换算）放 `mr_platform.h`；GPIO 端口时钟属于 GPIO 拥有者，不要再单独维持一份与 GPIO 驱动平行的时钟开关。

### 4.2 单个 backend 的固定顺序

与 `gpio_hc165.c` / `gpio_hc595.c` 对齐，ST 文件按下面顺序写，不在文件中段穿插安装宏：

1. 版权块。
2. `#include <mrX/platform/mr_platform.h>`，再按 `MR_USE_DRIVER_*` 包住其余内容。
3. 该类驱动公共头，以及本文件确实用到的 DMA/TMR 头。
4. `#define MR_HT_COMPAT st_<chip>_<ip>`。
5. `#if MR_HT_HAS_DRIVER()`。
6. ops 的前向声明，紧接着 `static const mr_<ip>_ops_t __<ip>_ops`。不支持的可选回调显式置 `MR_NULL`。
7. 私有数据类型：下层 spec、本 IP 句柄、运行状态。不要把芯片最大通道数的大数组嵌进每个实例，除非 HardTree 已声明这些通道。
8. 与芯片协议有关的 helper（模式映射表、一次转换、一次移位）。
9. ops 实现。参数检查、不支持的组合返回明确错误，不静默降级。
10. 实例安装宏 + `MR_HT_DRIVER_FOREACH`。
11. 成对 `#endif`（先 `MR_HT_HAS_DRIVER`，再 `MR_USE_DRIVER_*`）。

IRQ 处理函数只在“本实例声明了这条向量”时由安装宏生成，见 4.4。不要在 ops 之前用宏展开 16 个固定符号。

### 4.3 私有数据：描述资源，不复制硬件

拥有者保存本 IP 的 HAL 句柄或寄存器基址，以及 **本实例声明过的** 通道/引脚表。

消费者只保存引用，对照 HC595：

```c
typedef struct drv_gpio_hc595_priv {
    mr_ht_gpio_spec_t srclr;    /* optional */
    mr_ht_gpio_spec_t oe;       /* optional */
    mr_ht_gpio_spec_t ser;
    mr_ht_gpio_spec_t srclk;
    mr_ht_gpio_spec_t rclk;
    mr_uint8_t cascade_num;
    mr_uint32_t shadow;
    mr_bool_t configured;
} drv_gpio_hc595_priv_t;
```

ST UART 的目标形态是同一结构：TX/RX 是 `mr_ht_gpio_spec_t`，收发 DMA 是 `mr_ht_dma_spec_t`，外加 USART 句柄和帧配置。不要再放 `GPIO_TypeDef *` + `pin` + `alternate` 然后自己 `HAL_GPIO_Init`。

当前公共 `mr_gpio_config_t` 的 `alt` 只有 1 bit，还带不了 STM32 的 AF 编号。在 GPIO 公共层补上 AF 之前，允许消费者暂存 `tx_af` / `rx_af`，但引脚身份仍必须是 GPIO spec；这些引脚不得同时留在对应 GPIO 实例的 `exist_mask` 里。GPIO 一旦能配置 AF，消费者改为调用 `mr_ht_gpio_configure`，删掉自己的 `HAL_GPIO_Init`。

### 4.4 安装宏

安装宏只做三件事：展开本实例私有数据、按需生成该实例的 IRQ 入口、调用 `MR_HT_<IP>_INSTALL`。

- 必需属性用 `MR_HT_DRIVER_PROP` / `MR_HT_GPIO_SPEC` / `MR_HT_DMA_SPEC`。
- 可缺省的标量用 `MR_HT_DRIVER_PROP_OR`。
- 可缺省的 spec 用 `MR_HT_DRIVER_HAS_PROP`，没有则 `{0}`。
- 向量入口跟 UART/TMR 一样，由板级属性给出符号名：

```c
void MR_HT_DRIVER_PROP(_id, st_irq_handler)(void) {
    uart_irq_handle(&MR_CAT(__uart_priv_, _id));
}
```

这样只有模板声明过的实例会占用对应向量，也避免和 CubeMX 生成的空处理函数抢符号。GPIO EXTI、GPDMA 通道 IRQ 用同一思路：模板声明 `st_exti_mask` / 通道集合后，只为置位的线生成 `EXTIn_IRQHandler` / `GPDMAx_Channely_IRQHandler`。

时钟使能宏（`__HAL_RCC_GPIOA_CLK_ENABLE` 一类）可以在安装宏里包一层无参函数指针，因为它们不是普通函数。不要再另写一份按 `GPIO_TypeDef *` 分发的平行开关，除非 GPIO 拥有者自己集中维护、并且是唯一入口。

### 4.5 命名

| 对象 | 约定 | 示例 |
|------|------|------|
| compat | `st_<chip>_<ip>` | `st_stm32h5_gpio`、`st_stm32h5_gpdma` |
| 文件 | `platform/drivers/<ip>/<ip>.c` | `platform/drivers/uart/uart.c` |
| ops 表 | `__<ip>_ops` | `__gpio_ops` |
| 私有类型 | `<ip>_priv_t` | `uart_priv_t` |
| 实例对象 | `__<ip>_priv_<_id>` | `__uart_priv_0` |
| 安装宏 | `ST_HT_<IP>_INSTALL` | `ST_HT_UART_INSTALL` |
| HardTree 属性 | 芯片相关用 `st_` 前缀 | `st_clken`、`st_uartx`、`st_irqn` |
| 公共可移植属性 | 不加 `st_` | `exist_mask`、`invert_mask`、`INIT_LEVEL` |

组合驱动若放在 `mrX/drivers/`（HC165/HC595），compat 用功能名，不加 `st_`。`arch/st/` 里的片上适配一律带厂商与系列。

---

## 5. HardTree 约定

- 一个 compat 对应一类 backend 文件，文件开头的 `MR_HT_COMPAT` 必须与模板节点一致。
- 查找名是节点名，不是 compat 名。
- GPIO 引脚用 `[phandle, pin]`，DMA 通道用 `[phandle, channel]`。消费者不要改成裸寄存器地址。
- 存在性用掩码或列表表达实际用量：`exist_mask`、`st_exti_mask`、`st_channels`、`st_requests`。缺省值若会覆盖到未接线引脚，模板必须显式收窄；HC165/HC595 已经要求 `exist_mask` 与级联宽度一致，片上 GPIO 同样处理。
- 可选能力用属性有无表达，不用“打开了某 Kconfig 就默认每个节点都有 DMA”。
- `INIT_LEVEL` 遵循驱动框架；板级调试口以外的外设默认 DEFER，由应用 `setup`。
- GPIO spec 会被其他驱动长期引用，pin 编号与端口实例定义必须稳定。改编号等于改板级 ABI。

模板节点只描述这块板用到的 IP。不要为了“芯片上还有 USART3”就在模板里留空节点——空节点仍会生成 priv 和安装条目。

---

## 6. 按角色的适配要点

各 IP 的模式映射、事件位和 stream 时序以对应 `porting.md` 为准。这里只补 ST 分层带来的约束。

### 6.1 GPIO（拥有者）

- 一个实例对应一个 `GPIOx`。`exist_mask` 之外的位既不能读写，也不能借给 EXTI。
- 引脚模式、上下拉、输出类型、模拟、AF、EXTI 都从这里出去。消费者需要 AF 或模拟时，最终应走 GPIO configure，而不是第二份 HAL。
- EXTI 线在芯片上按 pin 号共享。所有权表可以是全局的，但只允许一个 GPIO 实例占用某一条线；占用冲突返回 `-MR_EBUSY`。
- `event_attach` / `event_control` 未声明 EXTI 的实例应置 `MR_NULL`，公共层会返回 `-MR_ENOSYS`。不要为从不进 IRQ 模式的端口分配 16 个 waitable。
- 不支持的触发（STM32 EXTI 没有电平触发）返回 `-MR_ENOSYS`，不要改成边沿。

### 6.2 DMA / TMR（拥有者）

- DMA 实例对应一台 GPDMA 控制器；通道集合来自模板，而不是编译期常数 8。
- 完成与错误中断为传输正确性服务，可以不依赖 waiter 订阅；`HALF` 等纯通知类中断仍按 `event_control` 开关。
- TMR 向量用 `st_irq_handler` 按实例生成。消费者需要特殊时序（例如 DAC 最后一次触发）时，通过平台头里的明确 helper 访问 **已经被自己独占的** 那个 TMR 驱动，不要再握一份 `TIM_HandleTypeDef`。

### 6.3 UART / ADC / DAC（消费者）

- 私有数据以 USART/ADC/DAC 句柄 + GPIO/DMA/TMR spec 为主。
- 普通读写与 stream 互斥规则、stop 失败保留所有权等行为写在芯片 `README.md` 和对应 `porting.md`，不要在本文重复。
- stream 启动顺序：先配置触发源（注意 STM32 定时器 UG 会提前出沿），再配 DMA，再使能外设请求，最后 start 定时器。失败按已取资源逆序停。
- 没有绑定 DMA spec 时不要编译该实例的 waiter/transfer；ops 里 stream 入口对未绑定实例返回 `-MR_ENODEV`。

### 6.4 与 HC165/HC595 相同的能力诚实

不支持的 ops 明确拒绝：写只读端口返回 `-MR_EPERM`，未实现事件置 `MR_NULL`。不要为了填满函数表去模拟硬件没有的能力。配置幂等：宿主或片上资源已初始化则跳过，但每次仍校验 pin/mode/范围。

---

## 7. 并发、IRQ 与 HAL

- 拥有者负责自己的 IRQ：清 pending、再 `mr_waitable_raise` / `mr_*_event_raise`。消费者不要再挂一份同名 `HAL_*_IRQHandler`。
- HAL 的 weak 回调可以在拥有者文件里强符号覆盖，作为进入 mrX waitable 的桥；不要每个消费者各覆盖一次。
- `HAL_*_Stop` / DMA abort 若可能等待 HAL 时基，不得在 waiter 回调（常在 IRQ 上下文）里调用。stream 错误路径只置位、停请求，把阻塞回收留在 `stream_stop`。
- 适配层不另加一把大锁去保护整个外设；与公共层一致，同一实例的并发由调用方或硬件原子寄存器保证。多步协议（ADC 切通道、UART 改帧、bit-bang）必须在文档里写明“同一实例要外部串行化”。

---

## 8. 相对当前 `stm32h5` 实现

`mrX/arch/st/stm32h5/platform/drivers/` 已按本文重写。下列是刻意保留的实现选择，不是未完成项：

| 项 | 现状 | 原因 |
|------|------|------|
| GPIO 事件回调 | 所有实例共用带 `event_attach` / `event_control` 的 `__gpio_ops` | AF helper 用 `MR_DRIVER_OPS(drv) == &__gpio_ops` 识别拥有者；未声明的 EXTI 线返回 `-MR_EPERM` |
| EXTI waitable | 芯片全局 `__gpio_irq_map[16]` 内嵌 waitable，首次 IRQ 配置时再 `mr_waitable_init` | EXTI 线是芯片全局稀疏资源；向量函数仍只由 `st_irq_handler_n` 生成 |
| DMA 通道表 | 按 `st_requests` 长度分配，而不是固定 `[8]` | 通道号仍是硬件下标；未列出的高编号通道不可用 |
| stream 字段 | Kconfig 打开 stream 时 waiter/transfer 仍在 priv 里 | 缺少 `st_dma` / `st_timer` 时 spec 为零对象，`stream_start` 返回 `-MR_ENODEV` |
| GPIO AF | 公共 `mr_gpio_config_t.alt` 仍不能带 AF 编号 | 消费者走 `mr_stm32h5_gpio_pin_alternate` |

芯片运行期语义（UART TC 与 DMA 完成的先后、ADC 先配定时器再启动、stop 失败保留 waiter）仍以 `mrX/arch/st/stm32h5/README.md` 为准。

---

## 9. 验收清单

新增或改写 `arch/st/` 下的 backend 时逐项确认：

1. 文件顺序符合第 4.2 节；`MR_HT_COMPAT` 与模板一致，且整个实现包在 `MR_HT_HAS_DRIVER()` 里。
2. 模板无该 compat 时，编译单元不残留全局 IRQ 符号、waitable 数组、按手册尺寸的通道表。
3. 拥有者不导出第二套平行 HAL 入口去配别人已经拥有的引脚或通道。
4. 消费者私有数据里的引脚/通道/定时器都是 spec 或驱动指针；可选字段在属性缺失时为零对象。
5. 静态表、waitable、IRQ 处理函数的数量能从 HardTree 直接数出来，而不是芯片最大值。
6. 不支持的模式/事件返回 `-MR_EINVAL` / `-MR_ENOSYS` / `-MR_EPERM`，与 HC165/HC595 一样不装假。
7. 安装宏只填充描述；时钟、HAL init、NVIC 出现在首次使用路径，并在重复配置时幂等。
8. 该 IP 的 ops、事件和 stream 时序仍满足对应 `porting.md`；芯片特有时序写在该 target 的 `README.md`。
9. 未把 `arch/` 源目录和安装后的 `mrX/platform/` 同时加入工程。

---

## 10. 源码导读

| 位置 | 读它做什么 |
|------|------------|
| `mrX/drivers/gpio/gpio_hc165.c` | 实例门控、可选 GPIO spec、只读能力、FOREACH 安装 |
| `mrX/drivers/gpio/gpio_hc595.c` | 同上，外加 shadow 与“未变化则跳过硬件动作” |
| `mrX/include/mrX/drivers/mr_driver.h` | `MR_HT_HAS_DRIVER`、`MR_HT_DRIVER_HAS_PROP`、`MR_HT_DRIVER_FOREACH` |
| `mrX/include/mrX/drivers/gpio/mr_gpio.h` | `MR_HT_GPIO_INSTALL`、`mr_ht_gpio_spec_t` |
| `mrX/include/mrX/drivers/dma/mr_dma.h` | `mr_ht_dma_spec_t` |
| `mrX/arch/st/stm32h5/platform/drivers/` | 当前片上实现；第 8 节说明重写后仍保留的选择 |
| `mrX/arch/st/stm32h5/README.md` | STM32H5 运行期约定 |
| `mrX/document/drivers/<ip>/porting.md` | 该类 ops 与验收，不讲 ST 分层 |

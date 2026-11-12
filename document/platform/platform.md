# 平台移植层（Platform）

> 平台移植层为内核提供与硬件/宿主相关的底层实现：时钟节拍来源、中断开关、
> 字符输出、断言处理，以及可选的缓存维护和延迟中断驱动，是 mrX 对接任意 MCU、SoC
> 或宿主环境的统一边界。

| 项       | 说明                                                                                                     |
|----------|----------------------------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_PLATFORM`（由已安装平台的 Kconfig 提供）                                                         |
| 头文件   | `<mrX/platform/mr_platform.h>`（平台侧，安装时复制，见下方说明）、`<mrX/port/mr_port_*.h>`（内核侧契约） |
| 依赖模块 | [时钟 Clock](../kernel/clock.md)、[初始化调用](../kernel/initcall.md)（`mr_platform_init` 的调用时机）   |
| 参考实现 | `mrX/arch/` 下的已有目录；安装目标为 `mrX/platform/`                                                     |

`<mrX/platform/mr_platform.h>` 不是仓库中的既存文件：它在安装平台后由适配目录的
`include/platform/` 复制到 `include/mrX/platform/`（具体规则见目标平台的
`install.toml`）。
未安装平台时该头文件不存在；若编译报"找不到平台头文件"，应先按第 3 节完成安装。

---

## 用户指南

本章先区分两种情况：

- **已有可复用平台适配**：读[第 1.1 节](#11-选择并安装已有平台)完成安装，然后返回
  [Usage](../usage.md) 的第 3 节继续接入。
- **没有可复用适配，需要对接新芯片**：读[第 1.3 节](#13-新平台的移植范围)和第 12
  节实现 port 层，
  再按[第 12.1 节](#121-适配验收清单)验收，最后同样回到 [Usage](../usage.md)
  的接入流程。

只想为已有平台补某类外设 backend 的读者不必从本文读起，可直接跳到对应驱动目录的移植文档。

### 1. 模块概述

mrX 的设计目标是"内核与硬件无关"。所有依赖具体运行环境的能力（谁产生 tick、
如何关中断、往哪里打印、断言失败怎么办）都被收敛为一组
`mr_port_*` 接口， 由移植层实现。本模块包含两个层面：

- **内核侧契约**：`mrX/include/mrX/port/` 下的 5 个头文件声明了内核期望的接口；其中
  cache 接口只在启用对应缓存宏时需要实现。
- **平台侧实现**：存放在 `mrX/arch/<vendor>/<target>/`，通过 `platform install`
  安装到 `mrX/platform/`
  后才参与编译——`platform/`
  是安装目标目录，其内容不纳入版本管理。

已有适配可作为目录结构和接口接线的参考；新平台只需在 `arch/` 下新增实现，不应修改内核来容纳板级差异。

### 1.1 选择并安装已有平台

平台适配保存在 `arch/<vendor>/<target>/`，安装目标是 `mrX/platform/` 及
`mrX/include/mrX/platform/`。安装目标是运行时真正参与编译的副本；`arch/`
只是适配仓库，不应被应用工程直接收集。

在 mrX 目录执行平台命令：

```text
python ./scripts/cli.py platform install   -i arch/<vendor>/<target>/install.toml
python ./scripts/cli.py platform status
python ./scripts/cli.py platform uninstall -i arch/<vendor>/<target>/install.toml
```

将示例路径替换为目标平台的 `install.toml`。`install` 按描述复制平台头文件和 源码，
`status` 查看当前安装状态，`uninstall`
恢复安装前的文件。执行前可加
`--dry-run` 查看计划；卸载已被修改的安装文件时才使用 `--force`。

同一输出目录同时只能安装一个平台。切换平台前先卸载旧平台；安装状态记录在 输出目录的
`.platform/` 中，安装产物和状态目录不纳入版本管理，因此每次重新
获取 mrX 后都要重新安装。平台安装完成后，再运行 Kconfig，使平台 Kconfig 选项进入配置树。

芯片运行期限制应查阅目标平台对应的 `arch/` 文档，不属于本页的通用接口契约。

### 1.2 `install.toml` 的最小结构

每个平台必须提供 `install.toml`，声明平台名称、基准目录和复制规则：

```toml
[platform]
name = "vendor,chip"

[dir]
src = "."
dst = "./../../.."

[[platform.add]]
src = "include/platform"
dst = "include/mrX/platform"

[[platform.add]]
src = "platform"
dst = "platform"
```

`src` 和 `dst` 以 `[dir]` 为基准；`dst` 需要按平台目录相对仓库根的层级回退。
`[[platform.add]]` 可重复，既可复制目录也可复制单文件。复制已有平台目录时， 同时检查
`name` 和 `dst`，避免把文件安装到错误的输出目录。

### 1.3 新平台的移植范围

只有没有可复用适配时才需要创建新平台。建议复制最接近的现有平台目录，再按
目标芯片实现以下内容：

1. `platform/port/` 中的时钟 tick、高分辨率时钟（若启用）、中断开关、输出和断言接口。开启
   `MR_USE_DCACHE` / `MR_USE_ICACHE`
   时还要实现 `mr_port_cache.h` 中对应的缓存接口。
2. `platform/platform.c` 中的 `mr_platform_init()`，完成时钟源、中断和平台输出等基础初始化。
3. 目标平台所需的板级 Kconfig、头文件和专用驱动，并在 `install.toml` 中声明安装位置。
4. 确认 tick 源按实际经过的 tick 数调用 `mr_clock_increase(ticks)`
   ，并验证中断开关支持嵌套保存与恢复。
5. 启用 `MR_USE_IRQ_DEFER` 时，实现目标平台的 defer 触发/执行接线；需要硬件外设时，再提供相应的
   HardTree 属性和平台驱动。
6. 在目标目录下写一份 `README.md`，记录本芯片的 **运行期语义**（时间单位、中断先后顺序、能力限制）。
   它与本文的分工是：本文规定 `mr_port_*` 契约和移植步骤，那份 README
   记录“本芯片具体是什么行为”。
   新平台还应自行规定分层、资源归属和文件骨架，而非将已有平台的私有约定作为公共要求。

内核只依赖 `include/mrX/port/` 中的接口契约；平台适配完成后先安装，再按
[`usage.md`](../usage.md) 的 Kconfig、HTC、纳入构建和注册段流程接入应用。按
[第 12.1 节](#121-适配验收清单)验收通过后再往下走，不要带着未验证的 port 层继续写外设
backend。
port 接口的原型、上下文和实现约束见本文后半部分。

如果工作目标只是实现 GPIO、UART、I2C 等某一类外设 backend，而不是移植整个平台，请直接进入对应驱动目录的
`porting.md`
，完整索引见[文档入口的设备驱动](../README.md#设备驱动)。

### 2. 配置与依赖

| 配置项                            | 类型   | 默认值   | 说明                                                              |
|-----------------------------------|--------|----------|-------------------------------------------------------------------|
| `MR_CFG_TICK_PER_SECOND`          | 整型   | 1000     | 每秒 tick 数，平台 tick 线程/中断按此频率驱动 `mr_clock_increase` |
| `MR_USE_PLATFORM`                 | bool   | 平台定义 | 开启平台初始化，通常由已安装平台的 Kconfig 选中                   |
| `MR_USE_CLOCK_HIRES`              | bool   | y        | 启用时移植层需实现高分辨率时钟接口                                |
| `MR_CFG_HIRES_TICK_PER_SECOND`    | 整型   | 1000000  | 高分辨率 tick 频率（≤ 1000000）                                   |
| `MR_USE_DCACHE` / `MR_USE_ICACHE` | 编译宏 | 平台定义 | 启用后要求平台提供对应 cache port                                 |

各 arch 目录带有自己的 `install.toml` 与平台
Kconfig，用于芯片系列、工具链能力和板级参数选择，具体以目标平台的说明为准。

### 3. 快速上手

选定目标环境后先安装对应平台（`mrX/platform/` 在 clone 后是空目录）：

```text
python ./scripts/cli.py platform install -i arch/<vendor>/<target>/install.toml
```

随后将安装后的平台源码接入用户工程（接入边界见 [使用指南](../usage.md)）。应用启动时调用
`mr_user_init()`；它会先经 `mr_kernel_init()` 调用 `mr_platform_init()`，再遍历用户
initcall。平台初始化完成后，tick
源开始驱动依赖时钟的内核服务。

### 4. 使用说明

**两层接口的分工**

- `mr_platform_init()`（`mrX/arch/<vendor>/<target>/platform/platform.c`
  ）：平台总初始化入口，由
  `mr_kernel_init()` 在内核初始化时调用；平台内部各子系统（中断、时钟）在此完成初始化。
- `mr_port_*` 接口：内核运行期反向调用的服务，共 5 类契约头文件；cache 一类是条件契约。

**内核侧接口契约（`mrX/include/mrX/port/`）**

| 接口                                                        | 头文件             | 职责                                                               |
|-------------------------------------------------------------|--------------------|--------------------------------------------------------------------|
| `mr_port_irq_disable` / `mr_port_irq_enable`                | `mr_port_irq.h`    | 进入/恢复临界区，保护内核共享状态                                  |
| `mr_port_clock_hires_tick` / `mr_port_clock_hires_tick_set` | `mr_port_clock.h`  | 读取/设置高分辨率 tick（`MR_USE_CLOCK_HIRES`）                     |
| `mr_port_clock_busy_wait`                                   | `mr_port_clock.h`  | 微秒级忙等待（不放弃 CPU）                                         |
| `mr_port_output`                                            | `mr_port_output.h` | 输出指定字节范围（log/printf 的最终出口）                          |
| `mr_port_assert`                                            | `mr_port_assert.h` | 记录或处理断言失败（上层宏保证后续不继续执行）                     |
| `mr_port_dcache_*`                                          | `mr_port_cache.h`  | D-cache 启停、line size、区间/全局 clean 与 invalidate（条件开启） |
| `mr_port_icache_*`                                          | `mr_port_cache.h`  | I-cache 启停、line size 与区间/全局 invalidate（条件开启）         |

**行为约定**

- `mr_port_irq_disable` 返回的 `mask` 必须成对传给 `mr_port_irq_enable`
  。实现可以保存/恢复处理器中断状态，也可以用宿主同步原语模拟，但都必须保留嵌套语义。
- `mr_port_output` 返回实际送出的字节数（`int`），负值表示输出失败。
- `mr_port_assert` 应至少留下 `file` / `line` / `expr` 可用的诊断信息，也可直接复位或停机。若它返回，
  `MR_ASSERT` /
  `MR_PANIC` 宏会立即进入死循环，不会继续执行原路径。
- cache 区间操作的 `addr` / `size` 以字节表示。具体 CPU 对 cache-line 对齐、DMA
  可达区域和指令屏障的要求由平台实现和其文档给出；不能只定义宏而留下未实现的符号。

### 5. API 参考

平台侧仅有一个统一初始化入口：

| API                | 说明                                                                      |
|--------------------|---------------------------------------------------------------------------|
| `mr_platform_init` | 平台总初始化（`mr_kernel_init()` 在启用平台时直接调用，应用无需手动调用） |

`mr_port_*` 接口由内核调用，应用一般不直接使用；仅在实现自定义移植层时才需要
关心其原型（见第 4 节表格与第 8 节）。

### 6. 完整示例

示例 1：平台 tick 源已正常工作时，运行一个周期定时器（应用无需手动推进 tick）：

```c
#include <mrX.h>

static void heartbeat(mr_timer_t *timer, void *param) {
    mr_printf("uptime = %llu ms\r\n", mr_clock_uptime());
}

static int app_init(void) {
    static mr_timer_t timer;

    mr_timer_init(&timer, heartbeat, MR_NULL,
                  MR_TICK_MSEC(1000) | MR_TIMER_PERIODIC);
    mr_timer_start(&timer);
    return 0;
}

int main(void) {
    mr_user_init();
    app_init();
    while (1) {
    }
    return 0;
}
```

示例 2：查询高分辨率时间戳。纪元和初始值由具体平台决定；下例只演示统一换算接口：

```c
#if defined(MR_USE_CLOCK_HIRES)
mr_hires_timestamp_t ts = mr_hires_tick2timestamp(mr_clock_hires_tick(), 8);
mr_printf("%u-%02u-%02u %02u:%02u:%02u\r\n", ts.year, ts.month, ts.day,
          ts.hour, ts.minute, ts.second);
#endif
```

### 7. 常见问题（FAQ）

**Q：目标板上没有 tick 中断或 tick 不动，定时器/日志时间全静止？**

tick 源由移植层负责。检查平台初始化是否真的配置了滴答定时器或宿主计时源，并按实际经过量调用
`mr_clock_increase(ticks)`。

**Q：多线程/中断下偶发数据错乱？**

内核用 `mr_port_irq_disable/enable` 保护临界区。若移植层实现不满足"嵌套安全"
（重复 disable 后第一次 enable 就真正开中断），会破坏内核临界区语义。确保实现
支持嵌套或成对保存/恢复。

**Q：printf/log 没有输出？**

依次检查：`mr_port_output` 是否把数据送到实际诊断通道；缓冲区是否过小；颜色/安全等
printf 配置项是否与终端能力匹配。

**Q：移植到新芯片最少要做哪些事？**

实现 irq、clock、output、assert 四类基础 `mr_port_*` 接口 + 1 个 `mr_platform_init`
，并提供 tick 源。若开启
D-cache/I-cache，再实现第 5 类 cache 契约；启用 IRQ defer 或平台外设时还需完成对应接线。见第
12 节。

---

## 开发者指南

### 8. 架构总览

```
        +---------------------------------+
        |  内核（clock/irq/log/printf/...）|
        +--------+------------------------+
                 | 依赖声明（只 include 头文件）
        +--------v------------------------+
        |  mrX/include/mrX/port/*.h       |   内核侧契约，无实现
        +--------+------------------------+
                 | 实现注入（安装 + 链接期决定）
        +--------v------------------------+
        |  平台实现（arch 仓库）           |
        |  mrX/arch/<vendor>/<target>/    |---< 目标 SDK / 宿主 API >---
        |  经 platform install 安装到     |
        |  mrX/platform/ 后参与编译       |
        +---------------------------------+
```

要点：

- 内核与平台之间 **只有头文件依赖**，二进制层面的绑定发生在链接期——用户 工程收集
  `mrX/{...,platform,...}/**/*.c`；安装了哪个
  arch 平台，
  `mrX/platform/` 里就是哪份实现，链接的也就是哪份实现。
- `mr_platform_init` 由 `mr_kernel_init` 在 `mr_user_init` 内部、用户 initcall
  遍历之前直接调用（`mrX/kernel/initcall.c`
  ），受 `MR_USE_PLATFORM` 保护。
- 平台目录自带 `platform.c`、`port/` 与可选平台驱动，`install.toml` 只负责把它们安装到统一位置。

### 9. 数据结构

移植层没有要求各平台共用的公开数据结构。实现通常需要维护以下私有状态，但存储形式由平台自行决定：

| 状态                  | 用途                             | 关键约束                                     |
|-----------------------|----------------------------------|----------------------------------------------|
| tick / hires 计时状态 | 记录计数器频率、累计值或时间偏移 | 换算必须与配置频率一致，并处理溢出与并发读写 |
| IRQ 临界区状态        | 保存进入临界区前的中断或锁状态   | 必须可嵌套，并按 `mask` 精确恢复             |
| 输出与 defer 状态     | 维护输出外设/队列和延迟执行源    | 在对应中断或线程启动前完成初始化             |

### 10. 核心流程

**tick 驱动**：平台先初始化周期定时器、系统滴答或宿主计时源，再在中断/线程中按实际经过量调用
`mr_clock_increase(ticks)`
。若时间源可能延迟，实现应累计落后量，避免长期丢 tick。

**hires tick**：开启高分辨率时钟后，`mr_port_clock_hires_tick` 返回值的频率必须与
`MR_CFG_HIRES_TICK_PER_SECOND` 一致。
`mr_port_clock_hires_tick_set` 设定逻辑计数值；实现可调整偏移，不要为了设值破坏底层自由运行计数器。

**临界区**：`mr_port_irq_disable` 先保存原状态再进入临界区，`mr_port_irq_enable`
使用对应 `mask` 恢复。实现不得把“恢复”简化为无条件开中断。

**输出与断言**：`mr_port_output` 把给定的字节范围送往调试串口、控制台或其他诊断通道；
`mr_port_assert` 保留断言现场，可选择返回、复位或停机。

**缓存维护**：开启 D-cache/I-cache 时，平台把统一的区间与全局操作映射到 CPU 指令或
SDK，并负责 cache-line
边界、顺序屏障和不可缓存区域的处理。

### 11. 并发与中断安全

- `mr_port_irq_disable/enable` 本身就是内核并发原语的底层，必须自身线程安全且支持嵌套；单核
  MCU
  通常保存中断状态，宿主或多核环境还需要能覆盖执行单元间并发的同步机制。
- `mr_port_clock_hires_tick` 可被多上下文调用；计数器和偏移的读写必须避免撕裂或数据竞争。
- `mr_port_output` 可能被主线程、其他线程或中断上下文调用；平台必须明确其可重入性、是否允许阻塞，以及中断中的使用边界。
- cache 维护与 DMA/外设并发时，由平台和外设驱动共同确定 clean/invalidate 时机；公共
  port 接口不替代所有权和生命周期协调。
- `mr_platform_init` 应在启动阶段串行调用；平台不应假设它会为并发重入提供额外保护。

### 12. 扩展与派生

移植到新 MCU、SoC 或宿主环境的标准步骤：

1. 建 `mrX/arch/<vendor>/<target>/` 目录，提供 `install.toml`（工具链/安装信息）与平台
   Kconfig、`mr_platform.h`。
2. 实现 `platform/port/` 基础四件套：`clock.c`（tick + hires + busy-wait）、
   `irq.c`（临界区）、`output.c`（诊断输出）、`assert.c`（失败处理）。目标开启 cache 时再增加
   `cache.c`，实现 `mr_port_cache.h`
   中对应编译宏下的全部接口。
3. 提供 `platform.c` 的 `mr_platform_init`，按依赖顺序完成计时、临界区、诊断通道和可选平台服务的初始化。
4. 按需在 `platform/drivers/` 实现平台外设驱动，对接
   [驱动框架](../drivers/driver.md)。具体某类外设的 backend
   移植不要在本章展开，应继续阅读对应驱动目录下的 `porting.md`
   ，例如 [GPIO 移植](../drivers/gpio/porting.md)、[UART 移植](../drivers/uart/porting.md)
   ；其他驱动采用相同目录结构。

接口契约不变，内核零改动。启用 `MR_USE_IRQ_DEFER` 时，还要在
`platform/irq/irq_defer.c` 完成延迟执行源的平台接线。

#### 12.1 适配验收清单

1. 先用 `platform install ... --dry-run` 确认输出根和覆盖文件，安装后用
   `platform status` 确认平台名与文件数；不要把
   `arch/` 原始目录和已安装的 `platform/` 同时加入工程。
2. 安装平台后重新生成 `mr_config.h`，确认 `MR_USE_PLATFORM`、芯片系列和板级参数与实际目标一致。
3. 确认 `mr_platform_init()` 只由 `mr_kernel_init()` 调用一次，并在启用各个中断源前建立好该中断依赖的
   tick 或 defer 状态。
4. 验证 tick 速率与 `MR_CFG_TICK_PER_SECOND` 一致，且中断或宿主线程延迟后不会长期丢
   tick。
5. 验证 `mr_port_irq_disable()` 返回的状态能被配对恢复；嵌套调用不得提前开中断，也不得破坏调用前本就关闭的状态。
6. 验证 `mr_port_output()` 对任意长度都返回实际输出字节数，并确认
   `mr_port_assert()` 能输出或保留可诊断现场；占位空实现不算通过。
7. 开启 hires 时，验证计数单调性、`set` 语义和频率换算；开启 D-cache/I-cache 时，验证
   line size、未对齐范围处理、DMA 前后的
   clean/invalidate 和必要屏障。
8. 开启 IRQ defer、HardTree 外设或平台驱动时，分别验证触发上下文、中断优先级、属性名和
   `compatible`；最后再确认
   initcall/驱动/RPC 等注册段被当前链接器保留。

### 13. 源码导读

| 位置                                             | 职责                                                                 |
|--------------------------------------------------|----------------------------------------------------------------------|
| `mrX/include/mrX/port/`                          | 内核侧契约：irq、clock、output、assert 四类基础接口与可选 cache 接口 |
| `mrX/arch/<vendor>/<target>/install.toml`        | 平台名、安装根目录和文件复制规则                                     |
| `mrX/arch/<vendor>/<target>/include/platform/`   | 平台 Kconfig 与 `mr_platform.h`，安装后位于 `include/mrX/platform/`  |
| `mrX/arch/<vendor>/<target>/platform/platform.c` | `mr_platform_init` 与平台子系统初始化顺序                            |
| `mrX/arch/<vendor>/<target>/platform/port/`      | clock、irq、output、assert 和可选 cache 契约的实现                   |
| `mrX/arch/<vendor>/<target>/platform/irq/`       | 可选的 IRQ defer 平台接线                                            |
| `mrX/arch/<vendor>/<target>/platform/drivers/`   | 可选的平台外设驱动                                                   |

### 14. 附录

**配置宏**

| 宏                             | 说明                                                        |
|--------------------------------|-------------------------------------------------------------|
| `MR_CFG_TICK_PER_SECOND`       | tick 频率，平台 tick 源的节拍基准                           |
| `MR_USE_CLOCK_HIRES`           | 高分辨率时钟开关，决定 `mr_port_clock_hires_*` 是否必须实现 |
| `MR_CFG_HIRES_TICK_PER_SECOND` | hires tick 频率，上限 1000000                               |
| `MR_USE_DCACHE`                | 要求实现 `mr_port_dcache_*`，由平台/应用配置引入            |
| `MR_USE_ICACHE`                | 要求实现 `mr_port_icache_*`，由平台/应用配置引入            |

**错误码**：`mr_port_output` 返回实际字节数或负数失败值；cache 操作返回 0 或负的
`mr_errno` 错误码，line-size 查询在无对应缓存时返回
0；irq 接口返回/接收的是待恢复 mask，不是错误码。

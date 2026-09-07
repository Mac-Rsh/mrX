# 平台移植层（Platform）

> 平台移植层为内核提供与硬件/宿主相关的底层实现：时钟节拍来源、中断开关、
> 字符输出与断言处理，是把 mrX 从 x64 宿主模拟环境搬到真实 MCU 上的对接面。

| 项       | 说明                                                                                                   |
|----------|--------------------------------------------------------------------------------------------------------|
| 功能宏   | 无（始终编译；具体 port 文件按配置条件编译）                                                           |
| 头文件   | `<mrX/platform/mr_platform.h>`（平台侧）、`<mrX/port/mr_port_*.h>`（内核侧契约）                       |
| 依赖模块 | [时钟 Clock](../kernel/clock.md)、[初始化调用](../kernel/initcall.md)（`mr_platform_init` 的调用时机） |
| 参考实现 | `mrX/platform/`（通用 x64 宿主）、`mrX/arch/win/`、`mrX/arch/wch/ch32v/`（板级）                       |

---

## 用户指南

### 1. 模块概述

mrX 的设计目标是"内核与硬件无关"。所有依赖具体运行环境的能力（谁产生 tick、 如何关中断、往哪里打印、断言失败怎么办）都被收敛为一组
`mr_port_*` 接口， 由移植层实现。本模块包含两个层面：

- **内核侧契约**：`mrX/include/mrX/port/` 下的 4 个头文件声明了内核期望的接口， 内核只依赖这些声明。
- **平台侧实现**：`mrX/platform/`（x64 宿主通用）与 `mrX/arch/<厂商>/<芯片>/`
  （板级，如 win、wch/ch32v）提供这些接口的具体实现。

仓库自带的实现可以支撑 `./build/mrX.exe` 直接运行；移植到新平台时只需替换实现， 不改动内核。

### 2. 配置与依赖

| 配置项                         | 类型 | 默认值  | 说明                                                              |
|--------------------------------|------|---------|-------------------------------------------------------------------|
| `MR_CFG_TICK_PER_SECOND`       | 整型 | 1000    | 每秒 tick 数，平台 tick 线程/中断按此频率驱动 `mr_clock_increase` |
| `MR_USE_CLOCK_HIRES`           | bool | y       | 启用时移植层需实现高分辨率时钟接口                                |
| `MR_CFG_HIRES_TICK_PER_SECOND` | 整型 | 1000000 | 高分辨率 tick 频率（≤ 1000000）                                   |

各 arch 目录（如 `mrX/arch/win/`）带有自己的 `install.toml` 与平台 Kconfig， 用于工具链选择与板级参数，具体以各 arch 目录说明为准。

### 3. 快速上手

在宿主环境使用不需要任何操作：根目录 CMake 工程已收集 `mrX/platform/**/*.c`，
`mr_user_init()` 内部的 `mr_kernel_init()` 会直接调用 `mr_platform_init()`， 随后才遍历用户 initcall，tick
线程随即启动。最小宿主程序（取自 `user/main.c` 的模式）：

```c
#include <mrX.h>

int main(void) {
    mr_user_init();

    /* Kernel is running: tick thread drives mr_clock_increase(1)
       periodically, timers/works/log timestamps are all alive */
    while (1) {
        /* Application loop */
    }
    return 0;
}
```

### 4. 使用说明

**两层接口的分工**

- `mr_platform_init()`（`mrX/platform/platform.c:12`）：平台总初始化入口，由
  `mr_kernel_init()` 在内核初始化时调用；平台内部各子系统（中断、时钟）在此完成初始化。
- `mr_port_*` 接口：内核运行期反向调用的服务，共 4 组（对应 4 个契约头文件），见下表。

**内核侧接口契约（`mrX/include/mrX/port/`）**

| 接口                                                        | 头文件             | 职责                                           |
|-------------------------------------------------------------|--------------------|------------------------------------------------|
| `mr_port_irq_disable` / `mr_port_irq_enable`                | `mr_port_irq.h`    | 关/开中断（宿主为临界区），保护内核自旋锁      |
| `mr_port_clock_hires_tick` / `mr_port_clock_hires_tick_set` | `mr_port_clock.h`  | 读取/设置高分辨率 tick（`MR_USE_CLOCK_HIRES`） |
| `mr_port_clock_busy_wait`                                   | `mr_port_clock.h`  | 微秒级忙等待（不放弃 CPU）                     |
| `mr_port_output`                                            | `mr_port_output.h` | 输出一串字符（log/printf 的最终出口）          |
| `mr_port_assert`                                            | `mr_port_assert.h` | 断言失败处理器（打印并停机）                   |

**行为约定**

- `mr_port_irq_disable` 返回的 `mask` 必须成对传给 `mr_port_irq_enable`：宿主实现 用嵌套计数支持多重关中断（
  `mrX/platform/port/irq.c:15`），目标板通常保存/恢复 PRIMASK 类状态寄存器。
- `mr_port_output` 返回实际送出的字符数（`int`），负值表示输出失败。
- `mr_port_assert` 不得返回——它是系统最后一道防线，惯例是打印后死循环或复位。

### 5. API 参考

平台侧仅有一个用户可见入口：

| API                | 说明                                                     |
|--------------------|----------------------------------------------------------|
| `mr_platform_init` | 平台总初始化（内核 initcall 自动调用，应用无需手动调用） |

`mr_port_*` 接口由内核调用，应用一般不直接使用；仅在实现自定义移植层时才需要 关心其原型（见第 4 节表格与第 8 节）。

### 6. 完整示例

示例 1：让一个周期定时器在宿主上运行（tick 由平台线程驱动，应用零干预）：

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

示例 2：查询高分辨率时间戳（宿主实现为 Unix 纪元微秒，见第 10 节）：

```c
#if defined(MR_USE_CLOCK_HIRES)
mr_hires_timestamp_t ts = mr_hires_tick2timestamp(mr_clock_hires_tick(), 8);
mr_printf("%u-%02u-%02u %02u:%02u:%02u\r\n", ts.year, ts.month, ts.day,
          ts.hour, ts.minute, ts.second);
#endif
```

### 7. 常见问题（FAQ）

**Q：目标板上没有 tick 中断或 tick 不动，定时器/日志时间全静止？**

tick 源由移植层负责。检查平台初始化是否真的配置了滴答定时器并在中断中调用
`mr_clock_increase(1)`；宿主实现对应 `mrX/platform/port/clock.c:19` 的
`tick_thread_entry`。

**Q：多线程/中断下偶发数据错乱？**

内核用 `mr_port_irq_disable/enable` 保护临界区。若移植层实现不满足"嵌套安全"
（重复 disable 后第一次 enable 就真正开中断），会破坏内核临界区语义。确保实现 支持嵌套或成对保存/恢复。

**Q：printf/log 没有输出？**

依次检查：`mr_port_output` 是否把数据送到真实外设（UART 等）；缓冲区是否过小； 颜色/安全等 printf 配置项是否与终端能力匹配。

**Q：移植到新芯片最少要做哪些事？**

实现第 4 节表格中的 4 组 `mr_port_*` 接口 + 1 个 `mr_platform_init`，并提供 tick 中断源。见第 12 节。

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
                 | 实现注入（链接期决定）
        +--------v------------------------+
        |  平台实现                        |
        |  mrX/platform/   x64 宿主通用    |---< windows.h >---
        |  mrX/arch/win/   win 板级        |
        |  mrX/arch/wch/   ch32v 板级      |---< 外设寄存器 >---
        +---------------------------------+
```

要点：

- 内核与平台之间 **只有头文件依赖**，二进制层面的绑定发生在链接期——CMake 收集
  `mrX/{...,platform,...}/**/*.c`，选了哪个 arch 目录就链接哪份实现。
- `mr_platform_init` 由 `mr_kernel_init` 在 `mr_user_init` 内部、用户 initcall 遍历 之前直接调用（
  `mrX/kernel/initcall.c:46`），受 `MR_USE_PLATFORM` 保护。平台内部再把 irq/clock 子初始化挂进去（
  `mrX/platform/platform.c:12`）。
- 板级目录（`mrX/arch/<厂商>/<芯片>/platform/`）自带 `platform.c`、`port/` 与可选 的平台驱动（如 ch32v 的 adc/gpio），
  `install.toml` 描述安装信息。

### 9. 数据结构

移植层无公开数据结构，关键是宿主实现内部的三个私有状态：

| 位置                              | 状态                                                | 说明                                                                                          |
|-----------------------------------|-----------------------------------------------------|-----------------------------------------------------------------------------------------------|
| `mrX/platform/port/irq.c:13-15`   | `__irq_lock`（CRITICAL_SECTION）+ `__irq_nest`      | 临界区与嵌套计数；`disable` 返回进入前的计数值作为 mask                                       |
| `mrX/platform/port/clock.c:16-17` | `__hires_freq` / `__hires_start` / `__hires_offset` | QPC 频率、起点与 Unix 纪元偏移（`hires_tick_set` 只改 offset，不重置计数器）                  |
| `mrX/platform/port/clock.c:19`    | `tick_thread_entry`                                 | 常驻线程：按 `MR_CFG_TICK_PER_SECOND` 自旋等待后 `mr_clock_increase(1)`，优先级 TIME_CRITICAL |

### 10. 核心流程

**tick 驱动（宿主）**：`mr_win_platform_clock_init`（`mrX/platform/port/clock.c:43`） 先初始化 QPC 高分辨率计数器，再取
`GetSystemTimeAsFileTime` 换算为 Unix 纪元微秒 并经 `mr_port_clock_hires_tick_set` 写入偏移（这就是"宿主 hires tick 为
Unix 纪元 微秒"语义的来源），最后启动 tick 线程。线程每周期自旋等待一个
`1 / MR_CFG_TICK_PER_SECOND` 后调用 `mr_clock_increase(1)`——内核侧所有基于 tick 的服务（定时器、work 等）由此被驱动。

**hires tick 计算**：`mr_port_clock_hires_tick`（`mrX/platform/port/clock.c:68`） 将 QPC 增量按
`MR_CFG_HIRES_TICK_PER_SECOND` 比例缩放，叠加 `__hires_offset`； 减法结果为负时钳位为 0，防止系统时间被回拨后出现倒流。

**关中断**：`mr_port_irq_disable`（`mrX/platform/port/irq.c:22`）进入临界区后把 当前嵌套计数作为 mask 返回，`enable`
递减计数并离开临界区——嵌套 语义由"配对保存/恢复"保证，与目标板上保存/恢复中断状态寄存器的惯用法同构。

**输出与断言**：`mr_port_output`（`mrX/platform/port/output.c:13`）直接 `printf`， 是 log/printf 模块的最底层出口；
`mr_port_assert`（`mrX/platform/port/assert.c:12`） 打印 `file:line: expr` 后 `exit(1)`。

### 11. 并发与中断安全

- `mr_port_irq_disable/enable` 本身就是内核并发原语的底层，必须自身线程安全且 支持嵌套；宿主实现用 CRITICAL_SECTION
  串行化 + 计数器嵌套。
- `mr_port_clock_hires_tick` 可被任意上下文调用：QPC 读取天然原子，offset 更新 只发生在初始化阶段。
- `mr_port_output` 会被多上下文并发调用（如 tick 线程的日志与主线程的打印）； 宿主 `printf` 由 CRT 串行化，目标板上建议在
  UART 驱动内加锁或关中断保护。
- 平台初始化流程（`mr_platform_init`）运行在单线程上下文，无并发问题。

### 12. 扩展与派生

移植到新 MCU 的标准步骤（参考 `mrX/arch/wch/ch32v/` 的现成样例）：

1. 建 `mrX/arch/<厂商>/<芯片>/` 目录，提供 `install.toml`（工具链/安装信息）与 平台 Kconfig、`mr_platform.h`。
2. 实现 `platform/port/` 四件套：`clock.c`（tick 中断 + hires + busy-wait）、
   `irq.c`（中断开关）、`output.c`（UART 输出）、`assert.c`（失败处理）。
3. 提供 `platform.c` 的 `mr_platform_init`，完成滴答定时器与串口的板级初始化。
4. 按需实现平台驱动（如 ch32v 的 `platform/drivers/{adc,gpio}/`），对接
   [驱动框架](../drivers/driver.md)。

接口契约不变，内核零改动。ch32v 的 `platform/irq/irq_defer.c` 还演示了
`MR_USE_IRQ_DEFER` 的平台侧驱动方式。

### 13. 源码导读

| 位置                                           | 职责                                                |
|------------------------------------------------|-----------------------------------------------------|
| `mrX/include/mrX/port/`                        | 内核侧契约：irq、clock、output、assert 四组接口声明 |
| `mrX/include/mrX/platform/mr_platform.h`       | 平台侧公共头（宿主包含 windows.h）                  |
| `mrX/platform/platform.c:12`                   | 宿主平台总初始化（irq + clock）                     |
| `mrX/platform/port/clock.c:19`                 | tick 线程（`mr_clock_increase` 驱动源）             |
| `mrX/platform/port/clock.c:68`                 | hires tick 读取与 Unix 纪元偏移                     |
| `mrX/platform/port/irq.c:13`                   | 临界区 + 嵌套计数实现                               |
| `mrX/platform/port/output.c:13`、`assert.c:12` | 最底层输出与断言失败处理                            |
| `mrX/arch/win/`、`mrX/arch/wch/ch32v/`         | 板级移植样例（含平台驱动）                          |

### 14. 附录

**配置宏**

| 宏                             | 说明                                                        |
|--------------------------------|-------------------------------------------------------------|
| `MR_CFG_TICK_PER_SECOND`       | tick 频率，平台 tick 源的节拍基准                           |
| `MR_USE_CLOCK_HIRES`           | 高分辨率时钟开关，决定 `mr_port_clock_hires_*` 是否必须实现 |
| `MR_CFG_HIRES_TICK_PER_SECOND` | hires tick 频率，上限 1000000                               |

**错误码**：`mr_port_output` 返回实际字符数或负数失败值；其余 port 接口无返回值 或返回嵌套 mask，不使用 `mr_errno` 错误码体系。

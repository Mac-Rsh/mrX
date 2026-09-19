# 日志（Log）

> 日志基于格式化输出提供带时间戳、带模块标签、分级可过滤的打印能力：源码中以
> `MR_LOG_xxx` 宏按级别埋点，编译期按全局级别裁剪，运行期按级别掩码过滤，
> 输出统一经由 [格式化输出 Printf](printf.md) 送往平台输出通道。

| 项       | 说明                                                                         |
|----------|------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_LOG`（默认开启，自动选中 `MR_USE_PRINTF`）                           |
| 头文件   | `<mrX/kernel/mr_log.h>`                                                      |
| 依赖模块 | [格式化输出 Printf](printf.md)（文本输出）、[时钟 Clock](clock.md)（时间戳） |
| 参考测试 | `mrX/test/kernel/log.c`                                                      |

---

## 用户指南

### 1. 模块概述

日志解决的问题是： **让散落在各模块的调试输出拥有统一的时间戳、统一的模块标签和 统一的级别开关**，且能在发布版本中零成本裁剪。

特性：

- 五个级别：`ERR` / `WRN` / `INF` / `DBG` / `TRC`，严重度递减。
- 编译期过滤：`MR_CFG_LOG_LEVEL` 之下的级别宏被整体替换为空语句，不产生任何代码。
- 运行期过滤：32 位级别掩码可动态开/关任意级别组合，无需重新编译。
- 每条日志自动附带时间戳与 `[级别/标签]` 前缀，标签由各编译单元的 `MR_LOG_TAG` 决定。
- 级别敏感的输出经 [格式化输出 Printf](printf.md)，格式语法与其一致。

不适用于：需要落盘/网络回传的结构化日志（本模块只负责输出到平台输出通道）、 对吞吐敏感的大量逐条打印（每条一次格式化 + 输出）。

### 2. 配置与依赖

| 配置项                 | 类型 | 默认值 | 说明                                                                           |
|------------------------|------|--------|--------------------------------------------------------------------------------|
| `MR_USE_LOG`           | bool | y      | 启用日志模块，自动选中 `MR_USE_PRINTF`（`mrX/include/mrX/kernel/Kconfig:110`） |
| `MR_CFG_LOG_LEVEL`     | 整型 | 4      | 编译期最高日志级别，范围 0~4（`Kconfig:119`）                                  |
| `MR_CFG_LOG_TIME_ZONE` | 整型 | 8      | 高分辨率时间戳的时区（UTC 偏移小时数），范围 -12~12（`Kconfig:133`）           |

`MR_CFG_LOG_LEVEL` 的实际代码语义由 `mr_log.h` 定义：0 = 仅 `ERR`，1 = `WRN` 及以上，2 = `INF` 及以上，3 = `DBG` 及以上，4 = 全部（含 `TRC`）。仅当编译单元定义了 `MR_LOG_TAG` 时这些级别宏才会生成调用。当前 Kconfig help 中的文字标签仍是旧定义，应以头文件常量为准。

`MR_CFG_LOG_TIME_ZONE` 仅在高分辨率时钟路径（`MR_USE_CLOCK_HIRES`）下参与换算， Kconfig 中也依赖该选项。

启用 `MR_USE_LOG` 并重新生成 `mr_config.h` 后，包含头文件即可使用：

```c
#include <mrX/kernel/mr_log.h>
```

### 3. 快速上手

在一个编译单元中定义模块标签后即可使用级别宏：

```c
#define MR_LOG_TAG                      "demo"
#include <mrX/kernel/mr_log.h>

void demo(void) {
    int value = 42;

    MR_LOG_INF("value is %d", value);
    MR_LOG_ERR("something failed: %d", -1);
}
```

典型输出（以下为未启用高分辨率时钟时的 uptime 格式；时间戳格式见第 4 节）：

```
[   1.234] [INF/demo] value is 42
[   1.235] [ERR/demo] something failed: -1
```

要点：

- `MR_LOG_TAG` 必须在包含 `mr_log.h` 之前定义，否则级别宏全部展开为空语句。
- 级别宏自带换行（`\r\n`），调用处不要再写 `\n`。

### 4. 使用说明

**级别与标签**

- 每个编译单元用一个 `MR_LOG_TAG` 字符串标识自己（如 `"timer"`、`"gpio"`）， 输出前缀为 `" [级别/标签] "`。
- 级别宏与级别的对应：`MR_LOG_ERR`(0) / `MR_LOG_WRN`(1) / `MR_LOG_INF`(2) /
  `MR_LOG_DBG`(3) / `MR_LOG_TRC`(4)。

**双重过滤**

| 过滤层 | 手段                                                    | 生效条件                             |
|--------|---------------------------------------------------------|--------------------------------------|
| 编译期 | `MR_CFG_LOG_LEVEL` 低于当前级别的宏被替换为 `((void)0)` | 该编译单元定义了 `MR_LOG_TAG`        |
| 运行期 | 32 位掩码 `mr_log_mask_set` / `mr_log_mask`             | 始终生效（直接调 `mr_log` 时也生效） |

掩码按位对应级别：`MR_LOG_MASK(level)` 生成第 `level` 位的掩码，可按位或组合。 初始掩码为 `0xffffffff`（全部开启），与
`MR_CFG_LOG_LEVEL` 无关——后者只做 编译期裁剪。

**时间戳**

- 高分辨率路径（`MR_USE_CLOCK_HIRES`）：`[年-月-日-时:分:秒:毫秒:微秒]`。`mr_log` 把 `mr_clock_hires_tick()` 直接交给 `mr_hires_tick2timestamp`，后者按“自 Unix epoch 起的 tick”解释；只有平台 hires tick 本身采用该基准时，这个结果才是真实墙钟时间。
- 普通路径：`[运行秒.毫秒]`（秒占 9 位右对齐），来自时钟模块 uptime。

**调用约束**

| 事项                                                 | 约定                                                         |
|------------------------------------------------------|--------------------------------------------------------------|
| `mr_log` 的 `level >= 32`、`fmt` 为 `MR_NULL` 或空串 | 直接忽略，无输出                                             |
| 格式语法                                             | 与 `mr_vsnprintf` 一致（见 [格式化输出 Printf](printf.md)）  |
| 输出终点                                             | 平台输出通道 `mr_port_output`，由移植层决定（串口/控制台等） |
| 时间戳与正文                                         | 分两次输出调用，中间无同步保证                               |

### 5. API 参考

级别宏（仅当定义了 `MR_LOG_TAG` 且 `MR_CFG_LOG_LEVEL` 允许时才有输出）：

| API                     | 说明                         |
|-------------------------|------------------------------|
| `MR_LOG_ERR(_fmt, ...)` | 以 `MR_LOG_LEVEL_ERR` 输出   |
| `MR_LOG_WRN(_fmt, ...)` | 以 `MR_LOG_LEVEL_WRN` 输出   |
| `MR_LOG_INF(_fmt, ...)` | 以 `MR_LOG_LEVEL_INF` 输出   |
| `MR_LOG_DBG(_fmt, ...)` | 以 `MR_LOG_LEVEL_DBG` 输出   |
| `MR_LOG_TRC(_fmt, ...)` | 以 `MR_LOG_LEVEL_TRC` 输出   |
| `MR_LOG_MASK(_level)`   | 生成级别掩码：`1U << _level` |

`MR_LOG_MASK` 本身不做范围保护，调用者应保证 `_level < 32`。

核心函数：

| API               | 说明                                   |
|-------------------|----------------------------------------|
| `mr_log`          | 按级别输出一条日志（级别宏的底层实现） |
| `mr_log_mask_set` | 设置运行期级别掩码                     |
| `mr_log_mask`     | 读取运行期级别掩码                     |

**`mr_log`**

```c
void mr_log(mr_uint32_t level, const char *fmt, ...);
```

- `level`：日志级别（0~4 有效；`>= 32` 直接忽略）。
- `fmt`：格式串，`MR_NULL` 或空串直接忽略。
- 返回：无。级别未在掩码中开启时不产生任何输出。
- 注意：该函数不做编译期裁剪，级别宏内部先经编译期判断再调用它。

**`mr_log_mask_set` / `mr_log_mask`**

```c
void mr_log_mask_set(mr_uint32_t mask);
mr_uint32_t mr_log_mask(void);
```

掩码第 `n` 位对应级别 `n`：置 1 输出，清 0 静默。常用值：

```c
mr_log_mask_set(0xffffffffU);                            /* All levels on */
mr_log_mask_set(MR_LOG_MASK(MR_LOG_LEVEL_ERR)
              | MR_LOG_MASK(MR_LOG_LEVEL_WRN));          /* ERR and WRN only */
mr_log_mask_set(0U);                                     /* All levels off */
```

### 6. 完整示例

示例 1：运行期动态调整级别（改编自 `mrX/test/kernel/log.c` 的 `mask_filter` 用例）：

```c
void level_switch_demo(void) {
    mr_uint32_t saved = mr_log_mask();

    mr_log_mask_set(0U);
    mr_log(MR_LOG_LEVEL_ERR, "silent now");            /* No output */

    mr_log_mask_set(MR_LOG_MASK(MR_LOG_LEVEL_ERR));
    mr_log(MR_LOG_LEVEL_ERR, "only err passes");       /* Output */

    mr_log_mask_set(saved);                            /* Restore */
}
```

示例 2：模块内按级别埋点（配合 `MR_LOG_TAG` 与编译期裁剪）：

```c
#define MR_LOG_TAG                      "sensor"
#include <mrX/kernel/mr_log.h>

int sensor_read(int *val) {
    if (val == MR_NULL) {
        MR_LOG_ERR("invalid parameter");
        return -MR_EINVAL;
    }
    MR_LOG_DBG("raw sample %d", *val);
    MR_LOG_INF("sensor ready");
    return 0;
}
```

更多行为参考（掩码过滤、非法级别、空格式串）见 `mrX/test/kernel/log.c`。

### 7. 常见问题（FAQ）

**Q：`MR_LOG_INF(...)` 编译后完全没有输出，连运行期掩码也开了？**

检查该编译单元是否定义了 `MR_LOG_TAG`：未定义时级别宏无条件展开为 `((void)0)`。 同时确认 `MR_CFG_LOG_LEVEL`
不低于所用级别（裁剪发生在编译期）。

**Q：`MR_CFG_LOG_LEVEL` 设为 0 后 `mr_log_mask` 仍返回 `0xffffffff`？**

正常。`MR_CFG_LOG_LEVEL` 只影响级别宏是否编译进代码；运行期掩码独立初始化为全开， 直接调用 `mr_log` 不受编译期配置约束。

**Q：时间戳怎么从 uptime 变成了日期时间？**

启用了高分辨率时钟（`MR_USE_CLOCK_HIRES`）后，日志会切换到日期时间格式，时区由 `MR_CFG_LOG_TIME_ZONE` 决定。但日期是否对应真实墙钟时间仍取决于平台提供的 hires tick 是否以 Unix epoch 为基准。

**Q：输出内容交错/乱码？**

日志本身不加锁：时间戳与正文经由 [格式化输出 Printf](printf.md) 输出，多上下文 并发输出请启用 `MR_USE_PRINTF_SAFE`
（每个格式化调用使用独立缓冲），并确保平台 输出通道 `mr_port_output` 本身线程/中断安全。

**Q：如何临时静默某个模块的日志？**

运行期掩码是全局的，无法按模块过滤；如需模块级静默，可用条件编译包裹或在输出端 按 `[级别/标签]` 前缀过滤。

---

## 开发者指南

### 8. 架构总览

日志是内核中最上层的诊断输出模块，自身不接触硬件：

```
        +---------------------------------+
        |  各模块（MR_LOG_xxx 埋点）        |
        +----------------+----------------+
                         | mr_log(level, fmt, ...)
        +----------------v----------------+
        |  日志 Log (mrX/kernel/log.c)     |----> 级别掩码过滤
        |                                 |----> 时间戳（时钟 Clock）
        +----------------+----------------+
                         | mr_vprintf(fmt, args)
        +----------------v----------------+
        |  格式化输出 Printf               |----> mr_port_output（移植层）
        +---------------------------------+
             依赖：Printf（文本输出）、Clock（时间戳）、sprintf（格式化）
```

- `MR_USE_LOG` 在 Kconfig 中 `select MR_USE_PRINTF`（`Kconfig:111`），保证底层 输出可用。
- 高分辨率路径额外依赖 `MR_USE_CLOCK_HIRES`（`log.c:17` 编译分支）。

### 9. 数据结构

模块只有一个全局状态：级别掩码 `__log_level`（`mrX/kernel/log.c:15`），
`mr_uint32_t`，初始值 `0xffffffffU`（全开）。掩码第 `n` 位对应级别 `n`，
`mr_log` 以 `(1U << level) & __log_level` 判定输出（`log.c:63`）。

级别编号由 `mr_log.h:31-35` 固定：`ERR`=0、`WRN`=1、`INF`=2、`DBG`=3、`TRC`=4；
`MR_LOG_MASK(_level)` 即 `1U << (_level)`（`mr_log.h:28`）。

级别宏的展开格式由 `__MR_LOG_FORMAT`（`mr_log.h:66`）定义：
`" [" 级别名 "/" MR_LOG_TAG "] " fmt "\r\n"`——标签是编译期字符串拼接，因此
`MR_LOG_TAG` 必须是字面量宏且先于包含头文件定义。

### 10. 核心流程

**编译期裁剪**：每个级别宏的守卫为 `defined(MR_LOG_TAG) && (MR_CFG_LOG_LEVEL >=
级别)`（如 `mr_log.h:70`）。条件不满足时宏展开为 `((void)0)`，格式串与参数一并 消失——这是发布版本去除调试日志的零开销途径。

**运行期输出**：`mr_log`（`mrX/kernel/log.c:54`）：

1. 参数检查：`level >= 32`、`fmt` 为空或空串直接返回（`log.c:58`）。
2. 掩码过滤：级别位未置 1 直接返回（`log.c:63`）。
3. `log_timestamp_print`（`log.c:18` / `log.c:37`）输出时间戳前缀：
    - 高分辨率路径：`mr_clock_hires_tick` → `mr_hires_tick2timestamp(tick,
     MR_CFG_LOG_TIME_ZONE)` → `[年-月-日-时:分:秒:毫秒:微秒]`（`log.c:29-34`）。
    - 普通路径：`mr_clock_uptime` 除以 1000 拆分为秒与毫秒， 输出 `[9 位秒.毫秒]`（`log.c:43-50`）。
4. `mr_vprintf` 可变参输出正文（`log.c:72-74`），换行已在格式串内。

**掩码读写**：`mr_log_mask_set` / `mr_log_mask` 直接赋值/读取全局变量 （`log.c:77-85`），无锁。

### 11. 并发与中断安全

- 模块内部无锁：`__log_level` 是普通 `mr_uint32_t`，`mr_log_mask_set` / `mr_log_mask` 只是直接写读，没有原子或锁语义。存在并发读写时应由调用者提供同步；常见做法是在初始化或受控配置阶段修改掩码。
- `mr_log` 本身没有阻止中断上下文调用，但能否安全这样使用取决于下游 Printf、动态内存模式以及 `mr_port_output` 的平台实现，框架没有给出统一 ISR-safe 保证。
- 时间戳与正文是两次独立输出调用，多上下文并发时可能交错。

### 12. 扩展与派生

日志无对象 clazz、无输出后端回调或 ops 表（`mr_log.h` 只声明 `mr_log` /
`mr_log_mask_set` / `mr_log_mask` 三个函数），扩展面是以下几处既有开关，且没有可 替换的后端：

- 每个编译单元定义自己的 `MR_LOG_TAG` 字面量，由 `__MR_LOG_FORMAT` 拼进前缀 （`mr_log.h:66-67`，用例
  `mrX/test/kernel/log_deep.c:16`）；不定义标签时级别宏 整体展开为空。
- 编译期用 `MR_CFG_LOG_LEVEL` 裁剪（`mr_log.h:38-40`、
  `mrX/include/mrX/kernel/Kconfig:119`），运行期用 32 位掩码 `mr_log_mask_set` /
  `mr_log_mask` 开关任意级别组合（`log.c:63`）。
- 输出终点不归本模块替换：正文经 `mr_vprintf` → `mr_port_output`（`printf.c:52`） 送出，替换点是移植层实现（
  `mrX/arch/win/platform/port/output.c:13`）；模块既无 颜色宏，也无前缀格式的覆写开关，级别常量与 `__MR_LOG_FORMAT` 均无条件定义。

### 13. 源码导读

| 位置                                 | 职责                                    |
|--------------------------------------|-----------------------------------------|
| `mrX/include/mrX/kernel/mr_log.h:28` | 掩码宏与五级定义                        |
| `mrX/include/mrX/kernel/mr_log.h:66` | 输出格式（标签拼接与换行）              |
| `mrX/include/mrX/kernel/mr_log.h:70` | 级别宏的编译期裁剪守卫                  |
| `mrX/kernel/log.c:15`                | 全局级别掩码                            |
| `mrX/kernel/log.c:18`                | 高分辨率时间戳（含时区换算）            |
| `mrX/kernel/log.c:37`                | uptime 时间戳                           |
| `mrX/kernel/log.c:54`                | `mr_log` 主流程（过滤 + 时间戳 + 输出） |
| `mrX/test/kernel/log.c`              | 掩码、非法参数、各级别输出的行为级测试  |

### 14. 附录

**配置宏**

| 宏                                      | 说明                                             |
|-----------------------------------------|--------------------------------------------------|
| `MR_USE_LOG`                            | 模块开关，默认 y，自动选中 `MR_USE_PRINTF`       |
| `MR_CFG_LOG_LEVEL`                      | 编译期最高级别，默认 4（全部），范围 0~4         |
| `MR_CFG_LOG_TIME_ZONE`                  | 高分辨率时间戳时区，默认 8（UTC+8），范围 -12~12 |
| `MR_LOG_LEVEL_ERR` ~ `MR_LOG_LEVEL_TRC` | 级别常量 0~4                                     |
| `MR_LOG_MASK(_level)`                   | 级别位掩码生成宏                                 |
| `MR_LOG_TAG`                            | 编译单元自定标签（用户侧定义，非 Kconfig 项）    |

**错误码**：本模块 API 无返回值、不产生错误码；`mr_log` 对 `level >= 32`、空指针格式串和空格式串静默忽略。时间戳路径没有错误返回通道；其绝对时间含义由底层时钟基准决定。

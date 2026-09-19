# 格式化输出（Printf）

> 格式化输出提供轻量级 `printf`：以 `mr_vsnprintf` 将格式串与参数格式化到内部
> 缓冲，再经平台输出通道 `mr_port_output` 送出，是内核日志与用户打印的统一出口。

| 项       | 说明                                                                                    |
|----------|-----------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_PRINTF`（默认开启）                                                             |
| 头文件   | `<mrX/kernel/mr_printf.h>`                                                              |
| 依赖模块 | [格式化 sprintf](../libc/sprintf.md)（`mr_vsnprintf`）、移植层输出通道 `mr_port_output` |
| 参考测试 | `mrX/test/kernel/printf.c`                                                              |

---

## 用户指南

### 1. 模块概述

格式化输出解决的问题是： **在没有完整 C 库 stdio 的环境下提供 `printf` 能力**， 并统一接管"格式化到哪里、从哪里出去"两个决策。

特性：

- `mr_printf` / `mr_vprintf` 两个入口，覆盖直接调用与可变参转发两种场景。
- 格式语法由 [格式化 sprintf](../libc/sprintf.md) 的 `mr_vsnprintf` 决定，与库内 其他格式化函数一致。
- 输出经统一的平台通道 `mr_port_output`，移植层决定落点是串口、控制台或仿真终端。
- `MR_CFG_PRINTF_BUF_SIZE` 控制格式化缓冲大小，超长输出自动截断。
- 可选 ANSI 颜色宏（`MR_USE_PRINTF_COLOR`）与独立格式化缓冲模式（`MR_USE_PRINTF_SAFE`）；后者消除静态缓冲竞争，但不替平台输出通道提供额外同步。

不适用于：写入缓冲区的格式化（请使用 [格式化 sprintf](../libc/sprintf.md) 的
`snprintf` 系列函数）、浮点格式化（能力以 `mr_vsnprintf` 实际支持为准）。

### 2. 配置与依赖

| 配置项                   | 类型 | 默认值 | 说明                                                         |
|--------------------------|------|--------|--------------------------------------------------------------|
| `MR_USE_PRINTF`          | bool | y      | 启用格式化输出（`mrX/include/mrX/kernel/Kconfig:69`）        |
| `MR_CFG_PRINTF_BUF_SIZE` | 整型 | 64     | 内部格式化缓冲字节数，范围 8~65535（`Kconfig:77`）           |
| `MR_USE_PRINTF_COLOR`    | bool | n      | 启用 ANSI 颜色宏（`Kconfig:88`）                             |
| `MR_USE_PRINTF_SAFE`     | bool | n      | 每次调用改用堆分配缓冲，避免共享静态格式化缓冲（`Kconfig:97`） |

启用 `MR_USE_PRINTF` 并重新生成 `mr_config.h` 后，包含头文件即可使用：

```c
#include <mrX/kernel/mr_printf.h>
```

缓冲取舍：`MR_CFG_PRINTF_BUF_SIZE` 决定单次输出的最大长度（超出部分被截断）， 静态模式下它同时是所有调用共享的那块内存的大小；调大以容纳更长日志行，
调小以省内存。

### 3. 快速上手

```c
#include <mrX/kernel/mr_printf.h>

void demo(void) {
    int count = 3;

    mr_printf("boot ok, count=%d, state=%s\r\n", count, "ready");
}
```

要点：

- 换行需自行携带 `\r\n`，模块不追加。
- 成功路径最终返回 `mr_port_output` 的返回值；按 port 接口约定通常是实际输出字符数。`fmt` 为 `MR_NULL` 或空串返回 `-MR_EINVAL`。

### 4. 使用说明

**输出流程**：格式化 → 截断 → 输出。格式化结果先写入大小为
`MR_CFG_PRINTF_BUF_SIZE` 的内部缓冲，实际送出的字符数不超过
`MR_CFG_PRINTF_BUF_SIZE - 1`；被截断时不报错，按截断后的长度输出。

**返回值约定**

| 情形                                  | 返回                     |
|---------------------------------------|--------------------------|
| `mr_port_output` 成功                  | 直接返回 port 的返回值；接口约定为实际输出字符数 |
| `fmt` 为 `MR_NULL` 或空串             | `-MR_EINVAL`                                    |
| 格式化失败（`mr_vsnprintf` 返回负数） | `-MR_EINVAL`                                    |
| `MR_USE_PRINTF_SAFE` 下缓冲分配失败   | `-MR_ENOMEM`                                    |
| `mr_port_output` 自身失败             | 原样透传平台返回值，不由 Printf 映射            |

**缓冲模式**

| 模式                 | 缓冲                                 | 适用                           |
|----------------------|--------------------------------------|--------------------------------|
| 默认（静态）         | 全局唯一 `static` 缓冲，所有调用共享 | 单线程、或能保证调用串行的场合 |
| `MR_USE_PRINTF_SAFE` | 每次调用 `mr_malloc`/`mr_free`       | 避免格式化缓冲互相覆盖；整体并发能力仍取决于 malloc 与 port output |

**颜色宏**（需 `MR_USE_PRINTF_COLOR`）：`MR_COLOR_RED` 等展开为 ANSI 转义序列， 未启用时展开为空，用法：

```c
mr_printf(MR_COLOR(MR_COLOR_RED, "error: %d\r\n"), code);
```

可用颜色：`MR_COLOR_BLACK` / `RED` / `GREEN` / `YELLOW` / `BLUE`，
`MR_COLOR_RESET` 复位；`MR_COLOR(_color, _str)` 自动在串尾附加重置序列。

**调用约束**

| 事项             | 约定                                                       |
|------------------|------------------------------------------------------------|
| 可否在中断中调用 | 无统一保证：默认静态缓冲不可重入，safe 模式又依赖 `mr_malloc`，两者都还受 `mr_port_output` 约束 |
| 可否在回调中调用 | 取决于该回调上下文是否允许所选缓冲策略与平台输出实现                     |
| 格式化失败的处理 | 返回负数错误码，不输出任何内容                             |

### 5. API 参考

| API                                | 说明                                   |
|------------------------------------|----------------------------------------|
| `mr_printf`                        | 可变参格式化输出                       |
| `mr_vprintf`                       | `va_list` 版本格式化输出               |
| `MR_COLOR(_color, _str)`           | 颜色包裹宏（需 `MR_USE_PRINTF_COLOR`） |
| `MR_COLOR_RESET` ~ `MR_COLOR_BLUE` | ANSI 颜色序列宏（未启用时为空）        |

**`mr_printf`**

```c
int mr_printf(const char *fmt, ...);
```

- `fmt`：格式串，不可为 `MR_NULL` 或空串。
- `...`：格式参数。
- 返回：成功/失败最终以 `mr_port_output` 返回值为准；Printf 自身的参数、格式化和分配失败见第 4 节。

**`mr_vprintf`**

```c
int mr_vprintf(const char *fmt, mr_va_list args);
```

- `fmt`：格式串，约束同上。
- `args`：由 `mr_va_start` 初始化的可变参列表，调用后由调用方 `mr_va_end` 收尾。
- 返回：同 `mr_printf`。
- 注意：`mr_printf` 内部即经由它实现；自定义带前缀的打印函数（如日志）应封装
  `mr_vprintf`。

### 6. 完整示例

示例 1：封装一个带标签的打印函数（`mr_vprintf` 的典型用法，改编自
`mrX/test/kernel/printf.c` 的 `vprintf_wrapper`）：

```c
#include <mrX/kernel/mr_printf.h>

static int tag_printf(const char *fmt, ...) {
    mr_va_list args;
    int ret;

    mr_printf("[app] ");
    mr_va_start(args, fmt);
    ret = mr_vprintf(fmt, args);
    mr_va_end(args);
    return ret;
}

void demo(void) {
    tag_printf("value=%d\n", 42);
}
```

示例 2：超长格式串的截断行为（缓冲默认 64 字节时输出被截断但不报错，改编自 测试 `vprintf_long_format` 用例）：

```c
#include <mrX/kernel/mr_printf.h>

void truncation_demo(void) {
    /* Format result exceeds MR_CFG_PRINTF_BUF_SIZE - 1: truncated */
    int ret = mr_printf(
        "this_is_a_very_long_format_string_that_exceeds_sixty_four_bytes_%s\n",
        "done");
    (void)ret;      /* Port receives at most MR_CFG_PRINTF_BUF_SIZE - 1 bytes */
}
```

更多行为参考（非法参数、基础格式化）见 `mrX/test/kernel/printf.c`。

### 7. 常见问题（FAQ）

**Q：返回的字符数比格式串展开后的完整长度短？**

传给 `mr_port_output` 的长度会被截断到最多 `MR_CFG_PRINTF_BUF_SIZE - 1`。若平台按接口约定返回实际写出长度，看到的返回值也会相应变短；Printf 本身不把截断视为错误。

**Q：`mr_printf(MR_NULL)` 会崩溃吗？**

不会，返回 `-MR_EINVAL`；空串同理。

**Q：两个线程同时打印，输出交错/出现半截内容？**

默认模式所有调用共享同一静态缓冲，并发或嵌套调用会互相覆盖。启用 `MR_USE_PRINTF_SAFE` 可消除**格式化缓冲**竞争；它不为 `mr_port_output` 加锁，也不保证 `mr_malloc` 适用于所有上下文。

**Q：为什么启用 `MR_USE_PRINTF_SAFE` 后偶发返回 `-MR_ENOMEM`？**

安全模式每次调用都要 `mr_malloc` 一块缓冲，堆耗尽即失败；调用方需检查返回值或 改用默认模式。

**Q：颜色宏在串口终端上显示乱码？**

确认 `MR_USE_PRINTF_COLOR` 已启用（未启用时宏为空，不存在乱码问题）；乱码通常 是终端不支持 ANSI 转义序列，关闭该选项即可。

**Q：格式语法支持哪些占位符？**

由 [格式化 sprintf](../libc/sprintf.md) 的 `mr_vsnprintf` 决定，与本模块无关； 未支持的占位符行为以该模块文档为准。

---

## 开发者指南

### 8. 架构总览

格式化输出是内核的诊断出口，把"格式化"与"输出"两个阶段拼接起来：

```
        +----------------------------------+
        |  调用方（应用 / 日志 Log）         |
        +-----------------+----------------+
                           | mr_printf / mr_vprintf
        +-----------------v----------------+
        |  格式化输出 Printf                 |
        |  (mrX/kernel/printf.c)           |----> mr_vsnprintf 格式化到 buf
        +-----------------+----------------+     （MR_CFG_PRINTF_BUF_SIZE）
                           | mr_port_output(buf, outsz)
        +-----------------v----------------+
        |  移植层输出通道（平台相关）         |----> 串口 / 控制台 / 仿真终端
        +----------------------------------+
             依赖：sprintf（mr_vsnprintf）、port 输出通道、
                   malloc（仅 MR_USE_PRINTF_SAFE）
```

- [日志 Log](log.md) 的时间戳与正文均经由本模块输出。
- 平台侧 `mr_port_output` 声明于 `mrX/include/mrX/port/mr_port_output.h:32`，由移植层实现（宿主模拟中对接 `stdio` 的
  `printf`，见 `mrX/arch/win/platform/port/output.c:13`）。

### 9. 数据结构

模块无核心结构体，仅两个编译期决策：

- 格式化缓冲：默认为函数内 `static char buf[MR_CFG_PRINTF_BUF_SIZE]`
  （`mrX/kernel/printf.c:23`），所有调用共享；`MR_USE_PRINTF_SAFE` 下改为每次 调用 `mr_malloc(MR_CFG_PRINTF_BUF_SIZE)` 的堆缓冲（
  `printf.c:36`）。
- 颜色宏：`MR_USE_PRINTF_COLOR` 启用时为 ANSI 转义字符串常量，未启用时全部展开 为空、`MR_COLOR(_color, _str)` 只保留
  `_str`（`mr_printf.h:30-45`）。

`MR_CFG_PRINTF_BUF_SIZE` 若未定义，源内默认 64（`printf.c:20`），与 Kconfig 默认值一致。

### 10. 核心流程

**`mr_vprintf`**（`mrX/kernel/printf.c:18`）：

1. 参数检查：`fmt` 为 `MR_NULL` 或空串返回 `-MR_EINVAL`（`printf.c:30`）。
2. 取缓冲：静态或按 `MR_USE_PRINTF_SAFE` 分配，分配失败返回 `-MR_ENOMEM`
   （`printf.c:36`）。
3. `mr_vsnprintf(buf, MR_CFG_PRINTF_BUF_SIZE, fmt, args)` 格式化（`printf.c:43`）； 返回负数视为格式化失败，转 `-MR_EINVAL`。
4. 计算 `outsz = MR_MIN(ret, buf_size - 1)`（`printf.c:45`），因此无论格式化结果长度如何，最多把缓冲可容纳的非终止字符部分交给 port。
5. `mr_port_output(buf, outsz)` 输出，并把该函数的返回值原样作为 `mr_vprintf` 的结果（`printf.c:52`）。
6. 安全模式下释放缓冲（`printf.c:55`）。

**`mr_printf`**（`printf.c:60`）：参数检查后以 `mr_va_start`/`mr_va_end` 包裹
`mr_vprintf`，纯转发逻辑。

**截断语义**：第 4 步的 `MR_MIN` 只限定传给 `mr_port_output` 的 `outsz` 上界为 `MR_CFG_PRINTF_BUF_SIZE - 1`。最终返回值来自 port；当前测试仅验证超长格式串在现有测试平台上返回正值。

### 11. 并发与中断安全

- 默认静态缓冲模式：格式化阶段非可重入——并发调用或嵌套调用会共享并覆盖同一缓冲。即使只有一个调用者，是否适合 ISR 仍取决于 `mr_port_output`。
- `MR_USE_PRINTF_SAFE` 模式：缓冲按调用独立，格式化阶段可并发；整体安全性还 取决于 `mr_malloc` 的中断可用性与
  `mr_port_output` 的实现。
- 模块本身不加锁；跨上下文的输出原子性由移植层负责。

### 12. 扩展与派生

printf 无 clazz 与派生机制（头文件里没有结构体、没有函数指针成员，只有两个函数声明与颜色宏，
`mrX/include/mrX/kernel/mr_printf.h:30-63`）。它的定制面是 **分层替换 + 编译期宏**：

- 输出终点由移植层实现的外部函数决定：`extern int mr_port_output(const char *buf, mr_size_t size)`（
  `mrX/include/mrX/port/mr_port_output.h:32`），printf 只用不定义（调用点 `mrX/kernel/printf.c:52`）。换后端就是换这个实现——宿主模拟写
  `stdout`（`mrX/arch/win/platform/port/output.c:13`），目标板空实现占位（`mrX/arch/wch/ch32v/platform/port/output.c:13`）。
- 格式化逻辑不属本模块：由 [格式化 sprintf](../libc/sprintf.md) 的 `mr_vsnprintf` 提供（`printf.c:43`），可在
  `MR_USE_SPRINTF_STD` 下整体转发标准库（`mrX/include/mrX/libc/mr_sprintf.h:29-33`），这是改变格式能力（如新增说明符）的唯一入口。
- 编译期开关：`MR_USE_PRINTF`、`MR_CFG_PRINTF_BUF_SIZE`（缺省 64，`printf.c:19-21`）、`MR_USE_PRINTF_COLOR`、
  `MR_USE_PRINTF_SAFE`（`mrX/include/mrX/kernel/Kconfig:69-105`），仅裁剪颜色与缓冲策略，不改变格式化语义；行为级验证见
  `mrX/test/kernel/printf.c:65` 的 `vprintf_long_format`。

### 13. 源码导读

| 位置                                     | 职责                                               |
|------------------------------------------|----------------------------------------------------|
| `mrX/include/mrX/kernel/mr_printf.h:30`  | 颜色宏定义（含禁用时的空展开）                     |
| `mrX/include/mrX/kernel/mr_printf.h:54`  | API 声明与 Doxygen 约定                            |
| `mrX/kernel/printf.c:18`                 | `mr_vprintf`：检查 → 取缓冲 → 格式化 → 截断 → 输出 |
| `mrX/kernel/printf.c:36`                 | 安全模式的堆缓冲分配                               |
| `mrX/kernel/printf.c:60`                 | `mr_printf`：可变参转发                            |
| `mrX/arch/win/platform/port/output.c:13` | 宿主模拟的输出通道实现示例                         |
| `mrX/test/kernel/printf.c`               | 非法参数、基础/可变参格式化、超长截断的行为级测试  |

### 14. 附录

**配置宏**

| 宏                       | 说明                                  |
|--------------------------|---------------------------------------|
| `MR_USE_PRINTF`          | 模块开关，默认 y                      |
| `MR_CFG_PRINTF_BUF_SIZE` | 格式化缓冲大小，默认 64，范围 8~65535 |
| `MR_USE_PRINTF_COLOR`    | ANSI 颜色宏开关，默认 n               |
| `MR_USE_PRINTF_SAFE`     | 堆缓冲安全模式开关，默认 n            |

**错误码**：Printf 自身产生 `-MR_EINVAL`（`fmt` 为空/空串，或 `mr_vsnprintf` 失败）和 `-MR_ENOMEM`（仅 `MR_USE_PRINTF_SAFE`，缓冲分配失败）。进入输出阶段后，`mr_port_output` 的返回值原样透传；宿主实现成功返回实际写出字节数，失败返回 `-1`。

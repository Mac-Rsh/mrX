# 格式化输出（Sprintf）

> 格式化输出模块提供轻量版 `sprintf` / `snprintf`（`mr_sprintf` 等），把整数、字符串、
> 指针等参数按格式串写入用户缓冲，是嵌入式环境下替代 C 标准库 `printf` 家族的核心
> 格式化引擎。

| 项       | 说明                                                                                                                              |
|----------|-----------------------------------------------------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_SPRINTF`（默认内置实现）；`MR_USE_SPRINTF_STD` 转发标准库；`MR_USE_SPRINTF_3RD` 当前仅为 Kconfig 预留项，代码尚无后端实现 |
| 头文件   | `<mrX/libc/mr_sprintf.h>`                                                                                                         |
| 依赖模块 | 无运行时依赖（类型来自 `mr_types.h`，见 [字符串 String](string.md) 附录；[内核格式化输出](../kernel/printf.md) 构建于本模块之上） |
| 参考测试 | `mrX/test/libc/sprintf.c`、`mrX/test/libc/sprintf_matrix.c`                                                                       |

---

## 用户指南

### 1. 模块概述

格式化输出模块解决的问题是： **在不引入完整 C 库 stdio 的目标板上，提供确定性强、 可裁剪的格式化字符串生成能力**。

特性：

- 支持 `d i u x X b o c s p` 转换说明符，含 `h hh l L ll z` 长度修饰（`L` 与
  `ll` 等价）。
- 支持标志位 `- + 空格 0 #`、宽度（数字或 `*`）与精度（`.数字` 或 `.*`）。
- 无缓冲区依赖：写入由调用者提供的缓冲，`mr_snprintf` 严格截断且保证 `\0` 结尾。
- `mr_snprintf(MR_NULL, 0, ...)` 可只统计长度、不产出输出。
- `%s` 传入 `MR_NULL` 输出 `"(null)"`，不崩溃。
- 不支持浮点转换（`f e g E G F` 参数会被消费后原样回显说明符）。

与 [内核格式化输出](../kernel/printf.md) 的分工：本模块只负责"格式串 + 参数 → 缓冲 字符串"；内核 printf
在其之上叠加设备输出与日志级别。需要在任何地方打印内容时， 最终都经由本模块完成数字到文本的转换。

### 2. 配置与依赖

| 配置项               | 类型           | 默认值 | 说明                                                           |
|----------------------|----------------|--------|----------------------------------------------------------------|
| `MR_USE_SPRINTF`     | bool（choice） | y      | 使用内置实现（`mrX/include/mrX/libc/Kconfig:102`）             |
| `MR_USE_SPRINTF_STD` | bool（choice） | n      | 宏转发标准库 `sprintf` / `snprintf` / `vsprintf` / `vsnprintf` |
| `MR_USE_SPRINTF_3RD` | bool（choice） | n      | 第三方格式化库预留选项；当前头文件/源码没有对应实现分支        |

选择 `MR_USE_SPRINTF` 并重新生成 `mr_config.h` 后，包含头文件即可使用：

```c
#include <mrX/libc/mr_sprintf.h>
```

本模块无 `MR_CFG_*` 参数，不依赖堆，全部输出写入调用者缓冲。

### 3. 快速上手

拼装一条传感器状态字符串：

```c
#include <mrX/libc/mr_sprintf.h>

void example(void) {
    char buf[64];
    int len;

    len = mr_snprintf(buf, sizeof(buf), "temp=%d.%02uV ch=%c hex=%#x",
                      25, 50, 'A', 0xFF);
    if (len < 0) {
        return;
    }
    /* buf == "temp=25.50V ch=A hex=0xff" */
}
```

要点：

- 优先使用 `mr_snprintf` 并传入 `sizeof(buf)`，超长内容会被安全截断且以 `\0` 结尾。
- 返回值为"本应写入的字符数"（不含 `\0`），可用于判断是否被截断。

### 4. 使用说明

**转换说明符一览**（均从 `mrX/libc/sprintf.c` 的格式处理分支确认）：

| 说明符         | 含义                | 备注                                                                                                              |
|----------------|---------------------|-------------------------------------------------------------------------------------------------------------------|
| `%d` / `%i`    | 有符号十进制        | 自动输出 `-` / `+` / 空号                                                                                         |
| `%u`           | 无符号十进制        |                                                                                                                   |
| `%x` / `%X`    | 十六进制小写 / 大写 | `#` 前缀输出 `0x` / `0X`                                                                                          |
| `%b`           | 二进制（扩展）      | `#` 前缀输出 `0b`，标准 C 无此说明符                                                                              |
| `%o`           | 八进制              | `#` 前缀补前导 `0`                                                                                                |
| `%c`           | 字符                | 支持宽度与 `-` 左对齐                                                                                             |
| `%s`           | 字符串              | `MR_NULL` 输出 `"(null)"`；精度限制最大字符数                                                                     |
| `%p`           | 指针                | **未显式指定宽度时**自动使用 `sizeof(void*) * 2 + 2`、`0x` 前缀与前导零；显式宽度时按普通十六进制规则，前缀需 `#` |
| `f e g E G F`  | 不支持              | 参数按浮点消费后，`%` 与说明符原样回显到输出                                                                      |
| 其他未知说明符 | 原样回显            | 输出 `%` 与该字符                                                                                                 |
| `%%`           | 原样回显            | 输出两个 `%` 字符（见 FAQ），标准 C 中输出一个                                                                    |

**标志位、宽度与精度**

| 语法            | 作用                                                                     |
|-----------------|--------------------------------------------------------------------------|
| `-`             | 左对齐（右侧补空格），优先于 `0` 填充                                    |
| `+`             | 正数也输出 `+` 号                                                        |
| 空格            | 正数输出前导空格（`+` 优先）                                             |
| `0`             | 空位补 `0`（有符号数时符号在 `0` 之前）                                  |
| `#`             | 二/十六进制加 `0b`/`0x`/`0X` 前缀，八进制加前导 `0`；数值为 0 时不加前缀 |
| 宽度数字或 `*`  | 最小输出宽度；`*` 从参数取宽度，负宽度等价于 `-` 左对齐                  |
| `.数字` 或 `.*` | 整数最小数字位数（补前导 0）；字符串最大字符数                           |

**长度修饰**：`hh`（`signed/unsigned char`）、`h`（short）、`l`（long）、
`L`（`long long`，与 `ll` 等价）、`ll`（long long）、`z`（`mr_ssize_t` / `mr_size_t`）。 无修饰时按 `int` / `unsigned int` 取参。

**返回值与截断行为**

- `mr_snprintf(buf, size, ...)`：返回不含 `\0` 的输出总长度；`size` 不足时只写入 前 `size - 1` 个字符并保证
  `buf[size - 1] == '\0'`；`size` 为 0 或 `buf` 为
  `MR_NULL` 时不写入任何内容，仅返回长度。
- `mr_sprintf(buf, ...)`：无长度上限，缓冲由调用者保证足够大。
- 内置实现中格式串为 `MR_NULL` 会立即返回 0，且不会写入输出缓冲或补 `\0`。

### 5. API 参考

| API            | 说明                                    |
|----------------|-----------------------------------------|
| `mr_snprintf`  | 按大小限制格式化输出到缓冲（推荐）      |
| `mr_sprintf`   | 无大小限制的格式化输出到缓冲            |
| `mr_vsnprintf` | 同 `mr_snprintf`，参数来自 `mr_va_list` |
| `mr_vsprintf`  | 同 `mr_sprintf`，参数来自 `mr_va_list`  |

**`mr_snprintf`**

```c
int mr_snprintf(char *buf, mr_size_t size, const char *fmt, ...);
```

- `buf`：输出缓冲。跨后端可移植的长度统计用法是 `buf = MR_NULL, size = 0`；内置实现只要发现 `buf == MR_NULL` 就会强制按
  `size = 0` 处理。
- `size`：缓冲总大小，含 `\0` 结尾占位。
- `fmt`：内置实现允许 `MR_NULL` 并直接返回 0；该路径不改写 `buf`。切换 STD 后端后应遵循标准库对格式串的前置条件，不要依赖这一扩展。
- 返回：不含 `\0` 的输出字符数；截断时为"完整输出"的长度而非实际写入数。

**`mr_sprintf`**

```c
int mr_sprintf(char *buf, const char *fmt, ...);
```

等价于 `size` 无上限的 `mr_snprintf`，缓冲必须由调用者保证足够大。

**`mr_vsnprintf` / `mr_vsprintf`**

```c
int mr_vsnprintf(char *buf, mr_size_t size, const char *fmt, mr_va_list args);
int mr_vsprintf(char *buf, const char *fmt, mr_va_list args);
```

供封装可变参数的辅助函数使用；行为分别与 `mr_snprintf` / `mr_sprintf` 一致。 调用前需 `mr_va_start`、调用后 `mr_va_end`（见第
6 节示例）。

### 6. 完整示例

示例 1：`va_list` 封装（改编自 `mrX/test/libc/sprintf.c` 的 `vsprintf` 测试）：

```c
#include <mrX/libc/mr_sprintf.h>

static int log_wrapper(char *buf, const char *fmt, ...) {
    mr_va_list args;
    int ret;

    mr_va_start(args, fmt);
    ret = mr_vsprintf(buf, fmt, args);
    mr_va_end(args);
    return ret;
}

void example(void) {
    char buf[64];

    log_wrapper(buf, "%d_%s_%x", 42, "test", 0xFF);
    /* buf == "42_test_ff" */
}
```

示例 2：先统计长度再分配精确大小的缓冲：

```c
#include <mrX/libc/mr_sprintf.h>

void exact_buffer_example(void) {
    int len;
    char buf[1];

    len = mr_snprintf(MR_NULL, 0, "val=%d", 12345);
    /* len == 10, no bytes written */

    mr_snprintf(buf, sizeof(buf), "val=%d", 12345);   /* Truncated safely */
}
```

示例 3：常用数字格式化（取材自 `mrX/test/libc/sprintf.c`）：

```c
mr_snprintf(buf, sizeof(buf), "%04d", 42);    /* "0042"   */
mr_snprintf(buf, sizeof(buf), "%-5d|", 42);   /* "42   |" */
mr_snprintf(buf, sizeof(buf), "%#x", 0xFF);   /* "0xff"   */
mr_snprintf(buf, sizeof(buf), "%#b", 10);     /* "0b1010" */
mr_snprintf(buf, sizeof(buf), "%.4d", 7);     /* "0007"   */
mr_snprintf(buf, sizeof(buf), "%10s", "test");/* "      test" */
```

更多边界行为（`%p`、`%hh` 系列极值、`*` 宽度、截断）见
`mrX/test/libc/sprintf.c` 与 `mrX/test/libc/sprintf_matrix.c`。

### 7. 常见问题（FAQ）

**Q：支持浮点（`%f`）吗？**

不支持。`f e g E G F` 的参数会被按浮点消费掉（保证后续参数不错位），但输出中 只回显 `%` 与说明符本身，不会转换数值。需要打印浮点可自行拆成整数与小数部分
（见第 3 节示例的 `%d.%02u` 手法）。

**Q：为什么 `%%` 输出了两个百分号？**

这是实现行为：`%%` 走未知说明符路径，输出 `%` 与 `%` 两个字符，与标准 C （输出一个 `%`）不同，已由 `mrX/test/libc/sprintf.c` 的
`unsigned_octal_bin`
用例固定。需要在输出中保留字面 `%` 时请知悉此差异。

**Q：`mr_snprintf` 被截断时返回值是什么？**

返回"完整格式化所需的字符数"（不含 `\0`），与标准 `snprintf` 一致；因此
`ret >= (int)size` 即表示发生了截断。

**Q：`%s` 传了 `MR_NULL` 会崩溃吗？**

不会，输出 `"(null)"`。但其他指针类型参数（如 `%d` 误传指针）无任何防护。

**Q：`%p` 的输出格式是什么样？**

当且仅当没有显式宽度时，内置实现自动设置宽度 `sizeof(void*) * 2 + 2`，并同时启用 `0x` 前缀与 `0` 填充（如 32 位平台
`0x12345678`）。一旦显式给出宽度，就不再自动设置这两个修饰；若仍需要前缀要写 `%#...p`，填充规则也按普通数字格式处理。

---

## 开发者指南

### 8. 架构总览

格式化输出是 libc 纯计算模块，无全局状态、无系统调用、无堆依赖：

```
        +------------------------------------------+
        | 内核 printf / log（设备输出、日志级别）      |
        +---------------------+--------------------+
                              | mr_vsnprintf()
        +---------------------v--------------------+
        | 格式化输出 Sprintf                         |
        |  格式串解析 -> format_number 数字装配       |
        +------------------------------------------+
             依赖：mr_types.h（va_list、定宽类型）
```

内核侧入口：`mrX/kernel/printf.c` 先准备格式化缓冲（默认静态缓冲，`MR_USE_PRINTF_SAFE`
时为动态缓冲），再调用本模块完成格式化并交给平台输出。用户代码也可直接调用本模块。

### 9. 数据结构

本模块无持久数据结构。核心是 `mr_vsnprintf` 栈上的两个游标与修饰位集合：

- `str` / `end`（`mrX/libc/sprintf.c:206`）：写指针与越界哨兵；所有写入都经过
  `if (str < end)` 判断，超出部分只累加计数不落盘，这是 `mr_snprintf` 截断 语义与 `MR_NULL` 缓冲统计长度的实现基础。
- `mod` 位集（`sprintf.c:13-20`）：`SPRINTF_MOD_ZEROPAD` / `SPRINTF_MOD_SIGN` / `SPRINTF_MOD_PLUS` /
  `SPRINTF_MOD_SPACE` / `SPRINTF_MOD_LEFT` / `SPRINTF_MOD_SPECIAL`（`#` 前缀）/ `SPRINTF_MOD_LARGE`（大写 十六进制）/
  `SPRINTF_MOD_NEGATIVE`（负数绝对值标记）。
- 数字逆序缓冲 `tmp[64]`（`sprintf.c:26`）：`format_number` 先按 base 逆序 生成数位，再正向回填，最大覆盖 64 位二进制输出。

### 10. 核心流程

**主循环**：`mr_vsnprintf`（`mrX/libc/sprintf.c:189`）逐字符扫描格式串：

1. 普通字符直接写入（受 `str < end` 保护）。
2. 遇 `%` 后依序解析：标志位（`- + 空格 0 #`，`sprintf.c:217`）、宽度 （数字或 `*`，负宽度转左对齐，`sprintf.c:233`）、精度（`.`
   数字或 `.*`，
   `sprintf.c:248`）、长度修饰（`h hh l ll z`，`sprintf.c:266`）。
3. 按转换说明符分派：`c` / `s` / `p` 各自内联处理并 `continue`；整数类 （`d i u x X b o`）设置符号与进制后落到公共取参路径。
4. 取参按 `SPRINTF_MOD_SIGN` 与长度修饰选择正确的 `mr_va_arg` 类型 （`sprintf.c:427-489`）；有符号数以"取绝对值 +
   `SPRINTF_MOD_NEGATIVE`
   "方式转换为 无符号量，避免 `INT64_MIN` 取负的未定义行为（`sprintf.c:456`）。

**数字装配**：`format_number`（`sprintf.c:22`）按序输出：符号 → 左填充 （`0` 填充时符号提前）→ `#` 前缀（`0b` / `0` / `0x` /
`0X`）→ 精度前导零 → 数位 → 右填充。所有宽度都先扣除符号与前缀占位，保证总宽恰好为指定宽度。

**`%s` / `%c` / `%p`**：`%s` 串长度受精度截断（`sprintf.c:321`），空串指针 替换为 `"(null)"`；`%c` 以宽度 1 为下限做左右填充；
`%p` 未指定宽度时启用
`SPRINTF_MOD_SPECIAL | SPRINTF_MOD_ZEROPAD` 并把宽度定为 `sizeof(void*) * 2 + 2`
（`sprintf.c:357`）。

**收尾**：`size > 0` 时在 `str` 或 `end[-1]` 处写 `\0`（`sprintf.c:497`）， 返回 `str - buf`。`buf == MR_NULL` 时以栈上哑元替代并置
`size = 0`
（`sprintf.c:201`），实现"只统计不输出"。

### 11. 并发与中断安全

- 内置实现无可变全局状态，解析状态与临时数字缓冲都在调用栈上，因此不同调用实例之间不共享格式化状态，可重入。
- 内置实现不分配堆、不加锁，执行量主要随格式串、宽度/精度和理论输出长度增长。是否适合 ISR
  还取决于调用者可接受的最坏格式化时延与栈空间预算；本模块本身不会阻塞。
- 唯一的外部约定：调用者保证输出缓冲在调用期间有效且足够大（`mr_sprintf`
  无越界保护）。

### 12. 扩展与派生

扩展面与 libc 家族其余模块一致，是 **编译期后端 choice**；本模块没有 clazz 与派生机制：

- `MR_USE_SPRINTF_STD` 时头文件把 `mr_sprintf` / `mr_snprintf` / `mr_vsprintf` / `mr_vsnprintf` 整体宏转发标准库（
  `mrX/include/mrX/libc/mr_sprintf.h:29-33`），`MR_USE_SPRINTF` 时使用内置实现（`mr_sprintf.h:34-76`，实现见
  `mrX/libc/sprintf.c:10` 起）。
- `MR_USE_SPRINTF_3RD` 已在 choice 中定义（`mrX/include/mrX/libc/Kconfig:97`），但头文件只有上述两个分支，代码中没有任何
  `MR_USE_SPRINTF_3RD` 分支——第三方格式化库的接入点当前并不存在，属于预留选项。
- 切换后端会改变可观测行为：内置实现不支持浮点转换、`%%` 输出两个百分号（`mr_sprintf.h:29-33` 转发后行为由工具链 C
  库决定），因此"换后端"不是纯粹的实现替换，会连带改变输出。模块未提供自定义说明符或输出回调注册入口，需要不同格式语义只能在调用方组合
  `mr_snprintf`。

### 13. 源码导读

| 位置                                   | 职责                                    |
|----------------------------------------|-----------------------------------------|
| `mrX/include/mrX/libc/mr_sprintf.h:29` | STD 后端宏转发与内置 API 声明           |
| `mrX/libc/sprintf.c:13`                | 修饰位（`SPRINTF_MOD_*`）定义           |
| `mrX/libc/sprintf.c:22`                | 数字装配公共路径（符号/前缀/填充/精度） |
| `mrX/libc/sprintf.c:189`               | 格式串主循环与说明符分派                |
| `mrX/libc/sprintf.c:427`               | 按长度修饰的取参表（有符号 / 无符号）   |
| `mrX/libc/sprintf.c:497`               | `\0` 收尾与返回值                       |
| `mrX/libc/sprintf.c:507`               | `mr_vsprintf`（无上限包装）             |
| `mrX/test/libc/sprintf.c`              | 说明符、标志、修饰与边界的行为级测试    |
| `mrX/test/libc/sprintf_matrix.c`       | 组合矩阵测试                            |

### 14. 附录

**配置宏**

| 宏                   | 说明                                  |
|----------------------|---------------------------------------|
| `MR_USE_SPRINTF`     | 内置实现开关（choice，默认）          |
| `MR_USE_SPRINTF_STD` | 转发标准库 sprintf/snprintf（choice） |
| `MR_USE_SPRINTF_3RD` | 第三方格式化库预留 choice；当前无实现 |

**返回值**：内置实现返回不含 `\0` 的理论输出字符数，截断时仍计完整长度；`fmt == MR_NULL` 返回 0；当前内置实现没有负数错误码路径。STD
后端则完全继承所用 C 库的返回/错误语义。

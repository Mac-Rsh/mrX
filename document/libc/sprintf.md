# 格式化输出（Sprintf）

提供轻量的 `sprintf` / `snprintf` 格式化引擎（`mr_snprintf`、`mr_sprintf`
等），无堆与系统调用依赖，用于在用户缓冲区中生成格式化字符串，也是 [内核格式化输出](../kernel/printf.md)
的底层引擎。

| 项目   | 说明                                                                                                                 |
|--------|----------------------------------------------------------------------------------------------------------------------|
| 功能宏 | `MR_USE_SPRINTF`（内置实现）；`MR_USE_SPRINTF_STD`（转发标准库）；`MR_USE_SPRINTF_3RD`（Kconfig 预留项，当前无实现） |
| 头文件 | `<mrX/libc/mr_sprintf.h>`                                                                                            |
| 依赖   | 无（仅依赖 `<mrX/libc/mr_types.h>`）                                                                                 |
| 测试   | `test/libc/sprintf.c`、`test/libc/sprintf_matrix.c`                                                                  |

## 配置

| 配置项               | 类型           | 默认值 | 说明                                                           |
|----------------------|----------------|--------|----------------------------------------------------------------|
| `MR_USE_SPRINTF`     | bool（choice） | y      | 使用内置格式化实现                                             |
| `MR_USE_SPRINTF_STD` | bool（choice） | n      | 宏转发标准库 `sprintf` / `snprintf` / `vsprintf` / `vsnprintf` |
| `MR_USE_SPRINTF_3RD` | bool（choice） | n      | 第三方格式化库预留项，当前头文件与源码无对应实现分支           |

## 快速上手

```c
#include <mrX/libc/mr_sprintf.h>

void example(void) {
    char buf[64];
    int len;

    len = mr_snprintf(buf, sizeof(buf), "temp=%d.%02uV ch=%c hex=%#x", 25, 50, 'A', 0xFF);
    if (len < 0 || len >= (int)sizeof(buf)) {
        /* 发生错误或输出被截断 */
    }
    /* buf == "temp=25.50V ch=A hex=0xff" */
}
```

## 核心机制与约束

**支持的格式语法**（内置实现）：

| 类别       | 支持项                                       | 说明                                                                         |
|------------|----------------------------------------------|------------------------------------------------------------------------------|
| 说明符     | `d` `i` `u` `x` `X` `b` `o` `c` `s` `p` `%%` | `%b` 为二进制扩展（`#` 输出 `0b`）；`%s` 传入 `MR_NULL` 输出 `"(null)"`      |
| 标志位     | `-` `+` `空格` `0` `#`                       | `-` 优先于 `0`；`+` 优先于空格；数值为 `0` 时 `#` 不输出前缀                 |
| 宽度与精度 | 数字或 `*`、`.数字` 或 `.*`                  | 负宽度等价于 `-` 左对齐；精度对整数补前导 `0`，对字符串限制最大字符数        |
| 长度修饰   | `hh` `h` `l` `L` `ll` `z`                    | `L` 与 `ll` 等价，均按 `long long` 取参；`z` 对应 `mr_ssize_t` / `mr_size_t` |

**与标准库的差异及限制**：

- **不支持浮点**：遇到 `f e g E G F` 时会按浮点消费参数（避免后续可变参数错位），但输出中只原样回显
  `%` 与说明符字符。未知说明符同样原样回显。
- **`%p` 规则**：未显式指定宽度时自动采用 `sizeof(void *) * 2 + 2` 宽度、`0x` 前缀与
  `0` 填充；一旦显式指定宽度，则退化为普通十六进制格式（需加 `#` 才输出 `0x`）。
- **截断与测长**：`mr_snprintf` 返回不含 `\0` 的完整格式化所需字符数；
  `len >= (int)size` 表示发生截断，`size > 0` 时保证 `buf[size - 1] == '\0'`。传入
  `buf = MR_NULL, size = 0` 可仅统计输出长度而不写入任何字节。
- **`fmt == MR_NULL`**：内置实现遇到 `fmt == MR_NULL` 直接返回 `0` 且不改写 `buf`
  ；切换到 `MR_USE_SPRINTF_STD` 后遵循标准库语义，不应依赖此扩展。
- **可重入性**：内置实现无全局可变状态、不分配堆、不加锁，仅使用栈上临时缓冲，可重入；
  `mr_sprintf` 无长度上限保护，调用者须保证缓冲区足够大。

## API 速查

| API                                                                             | 说明                               |
|---------------------------------------------------------------------------------|------------------------------------|
| `int mr_snprintf(char *buf, mr_size_t size, const char *fmt, ...)`              | 按缓冲区大小限制格式化输出（推荐） |
| `int mr_sprintf(char *buf, const char *fmt, ...)`                               | 无大小限制的格式化输出             |
| `int mr_vsnprintf(char *buf, mr_size_t size, const char *fmt, mr_va_list args)` | `mr_va_list` 版本的 `mr_snprintf`  |
| `int mr_vsprintf(char *buf, const char *fmt, mr_va_list args)`                  | `mr_va_list` 版本的 `mr_sprintf`   |

## 内部实现

内置实现位于 `libc/sprintf.c`，核心逻辑由 `mr_vsnprintf` 与 `format_number` 组成：

- **越界哨兵**：`mr_vsnprintf` 维护写指针 `str` 与上界 `end`，所有写入均经
  `if (str < end)` 保护，超出部分仅推进计数不写内存；`buf == MR_NULL` 时以栈上哑元替代并置
  `size = 0`。
- **数字装配**：`format_number` 使用 64 字节栈上逆序缓冲 `tmp[64]`（覆盖 64
  位二进制输出），按“符号 → 左填充 → `#` 前缀 → 精度前导零 → 数位 →
  右填充”顺序写出；有符号负数以取绝对值配合 `SPRINTF_MOD_NEGATIVE`
  标记处理，避免最小负整数取负溢出。

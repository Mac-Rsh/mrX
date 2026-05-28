# libc 格式化输出

`mr_sprintf.h` 和 `libc/sprintf.c` 提供面向内存缓冲区的格式化输出接口。启用 `MR_USE_SPRINTF_STD` 时接口映射到标准库；否则由 mrX 自实现处理整数、字符串、字符、指针等常用格式。

## 核心接口

```c
int mr_vsnprintf(char *buf, mr_size_t size, const char *fmt, mr_va_list args);
int mr_vsprintf(char *buf, const char *fmt, mr_va_list args);
int mr_snprintf(char *buf, mr_size_t size, const char *fmt, ...);
int mr_sprintf(char *buf, const char *fmt, ...);
```

`mr_vsnprintf()` 是核心实现。`mr_snprintf()` 和 `mr_sprintf()` 负责创建 `mr_va_list` 并转调。`mr_vsprintf()` 通过传入接近无上限的 size 调用 `mr_vsnprintf()`。

返回值为尝试写入的字符数，不包含结尾 `\0`。当 `size > 0` 时，输出缓冲区会以 `\0` 结尾；如果内容被截断，返回值仍表示完整格式化长度。

## 支持的格式能力

### 标志、宽度和精度

自实现支持以下格式修饰：

| 类别 | 支持项 |
|---|---|
| 标志 | `-`、`+`、空格、`0`、`#` |
| 宽度 | 十进制数字宽度、`*` 动态宽度；负动态宽度会启用左对齐。 |
| 精度 | `.数字`、`.*` 动态精度；负动态精度按 0 处理。 |
| 长度 | `l`、`ll`、`z`、`h`、`hh` |

### 转换说明符

| 说明符 | 含义 |
|---|---|
| `%c` | 字符。 |
| `%s` | 字符串，空指针输出 `"(null)"`。 |
| `%p` | 指针；默认宽度为指针字节数的 2 倍加 `0x` 前缀，并使用零填充。 |
| `%d`、`%i` | 有符号十进制。 |
| `%u` | 无符号十进制。 |
| `%x`、`%X` | 十六进制，小写或大写。 |
| `%o` | 八进制。 |
| `%b` | 二进制，mrX 扩展。 |

`#` 标志会为二进制、八进制和十六进制添加 `0b`、`0`、`0x` 或 `0X` 前缀。

## 标准库映射

启用 `MR_USE_SPRINTF_STD` 时：

```c
mr_vsnprintf -> vsnprintf
mr_vsprintf  -> vsprintf
mr_snprintf  -> snprintf
mr_sprintf   -> sprintf
```

## 使用示例

```c
#include <mrX/libc/mr_sprintf.h>

void sample_sprintf_usage(void)
{
    char buf[32];

    mr_snprintf(buf, sizeof(buf), "id=%04d hex=%#x", 7, 0x2a);
}
```

可变参数包装示例：

```c
static int sample_vformat(char *buf, mr_size_t size, const char *fmt, ...)
{
    mr_va_list args;
    int ret;

    mr_va_start(args, fmt);
    ret = mr_vsnprintf(buf, size, fmt, args);
    mr_va_end(args);
    return ret;
}
```

## 注意事项

- 自实现只在 `MR_USE_SPRINTF` 编译开关启用时参与构建。
- 自实现不支持浮点格式化。`%f`、`%e`、`%g` 及大写变体会按不支持格式处理，输出 `%` 加原说明符。
- 自实现中 `fmt == MR_NULL` 时返回 0。
- 自实现允许 `buf == MR_NULL` 且 `size == 0`，可用于计算格式化长度。
- 自实现对 `%%` 的行为与标准库不同：它会按不支持格式路径输出两个百分号字符。
- `mr_vsprintf()` 不知道真实缓冲区大小，调用方必须保证目标空间足够。
- 测试覆盖整数、字符串、宽度、精度、长度修饰、指针、二进制、前缀、截断、空字符串、空格式和空字符串参数等场景。

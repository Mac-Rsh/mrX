[**中文**](kernel_printf.md) | [**English**](kernel_printf_en.md)

---

# 内核输出

内核输出模块提供轻量级格式化输出能力。模块通过 `mr_vsnprintf()` 将格式化结果写入内部缓冲区，再调用平台层
`mr_port_output()` 输出字节流。该模块由 `MR_USE_PRINTF` 配置控制，常用于内核调试、日志输出和测试输出。

本文对应 `include/mrX/kernel/mr_printf.h`、`kernel/printf.c` 和 `test/kernel/printf.c`。

## 模块用途

`mr_printf` 模块封装了两个输出接口：`mr_printf()` 处理可变参数，`mr_vprintf()` 处理已经展开的 `mr_va_list` 参数。格式化能力来自
mrX libc 中的 `mr_vsnprintf()`，实际字符输出由端口层 `mr_port_output()` 完成，因此该模块本身不绑定具体串口、控制台或宿主输出设备。

当 `MR_USE_PRINTF_SAFE` 未启用时，`mr_vprintf()` 使用静态缓冲区；启用后，每次调用会通过 `mr_malloc()` 申请临时缓冲区并在输出后释放。缓冲区大小由
`MR_CFG_PRINTF_BUF_SIZE` 控制，源码中未定义时默认使用 64 字节。

## 核心宏和配置

### 输出开关

```c
#if defined(MR_USE_PRINTF)
```

只有启用 `MR_USE_PRINTF` 后，颜色宏和输出接口才会生效。`include/mrX/kernel/Kconfig` 中该配置默认开启。

### 缓冲区大小

```c
#define MR_CFG_PRINTF_BUF_SIZE          (64)
```

`MR_CFG_PRINTF_BUF_SIZE` 是格式化缓冲区大小。Kconfig 中该配置依赖 `MR_USE_PRINTF`，默认值为 64，取值范围为 8 到
65535。源码在配置缺失时也会使用 64 作为默认值。

### 安全输出模式

```c
#if !defined(MR_USE_PRINTF_SAFE)
    static char buf[MR_CFG_PRINTF_BUF_SIZE];
#else
    char *buf;
#endif
```

未启用 `MR_USE_PRINTF_SAFE` 时，模块使用函数内静态缓冲区，多个执行上下文同时调用时会共享同一块缓冲区。启用后，模块为每次调用分配独立缓冲区，可避免共享缓冲区竞争，但会引入堆分配开销，并可能返回
`-MR_ENOMEM`。

### 颜色宏

```c
#define MR_COLOR_RESET                  "\033[0m"
#define MR_COLOR_BLACK                  "\033[30m"
#define MR_COLOR_RED                    "\033[31m"
#define MR_COLOR_GREEN                  "\033[32m"
#define MR_COLOR_YELLOW                 "\033[33m"
#define MR_COLOR_BLUE                   "\033[34m"
#define MR_COLOR(_color, _str)          _color _str MR_COLOR_RESET
```

启用 `MR_USE_PRINTF_COLOR` 后，颜色宏展开为 ANSI 转义序列；未启用时，颜色常量为空，`MR_COLOR(_color, _str)` 直接返回 `_str`
。使用颜色输出时，实际终端或输出设备需要支持 ANSI 转义序列。

## 模块接口

### 格式化输出可变参数列表

```c
int mr_vprintf(const char *fmt, mr_va_list args);
```

`mr_vprintf()` 接收格式字符串和 `mr_va_list`，将格式化结果写入内部缓冲区，然后调用 `mr_port_output(buf, outsz)` 输出。

返回值如下：

| 返回值          | 含义                                  |
|--------------|-------------------------------------|
| `>= 0`       | `mr_port_output()` 的返回值，表示平台层输出结果。  |
| `-MR_EINVAL` | `fmt` 为空、`fmt` 是空字符串，或格式化函数返回错误。    |
| `-MR_ENOMEM` | 启用 `MR_USE_PRINTF_SAFE` 时临时缓冲区申请失败。 |

测试用例覆盖了空指针格式串、空字符串格式串、普通字符串、整数、十六进制、字符和较长格式串输出。

### 格式化输出

```c
int mr_printf(const char *fmt, ...);
```

`mr_printf()` 是常用输出接口。它检查 `fmt` 后通过 `mr_va_start()` 构造参数列表，再调用 `mr_vprintf()` 完成输出。

返回值与 `mr_vprintf()` 一致。`fmt` 为空或空字符串时直接返回 `-MR_EINVAL`。

## 使用示例

```c
#include <mrX/kernel/mr_printf.h>

void example_printf(void) {
    int ret;

    ret = mr_printf("boot step: %u\n", 1U);
    if (ret < 0) {
        return;
    }

    mr_printf(MR_COLOR(MR_COLOR_GREEN, "init ok\n"));
}
```

如果需要转发已有的可变参数列表，可以封装为 `mr_vprintf()` 调用：

```c
static int output_with_prefix(const char *fmt, ...) {
    mr_va_list args;
    int ret;

    mr_va_start(args, fmt);
    ret = mr_vprintf(fmt, args);
    mr_va_end(args);
    return ret;
}
```

## 注意事项

- `mr_printf()` 和 `mr_vprintf()` 不接受空格式串，传入 `MR_NULL` 或 `""` 会返回 `-MR_EINVAL`。
- 未启用 `MR_USE_PRINTF_SAFE` 时，内部静态缓冲区不适合多个线程或中断上下文并发输出同一条格式化数据。
- 启用 `MR_USE_PRINTF_SAFE` 后，调用路径依赖堆分配，不适合不能分配内存的上下文。
- 输出长度受 `MR_CFG_PRINTF_BUF_SIZE` 限制，超过缓冲区时实际格式化行为由 `mr_vsnprintf()` 决定。
- 该模块只负责格式化和调用端口层输出；输出设备初始化、阻塞行为和并发保护由 `mr_port_output()` 的平台实现决定。

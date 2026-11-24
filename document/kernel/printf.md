# 格式化输出（Printf）

提供轻量级 `printf` 能力：通过 `mr_vsnprintf` 将格式串展开到内部缓冲，再经平台输出通道
`mr_port_output()` 发送。

| 项目   | 说明                                                            |
|--------|-----------------------------------------------------------------|
| 功能宏 | `MR_USE_PRINTF`                                                 |
| 头文件 | `<mrX/kernel/mr_printf.h>`                                      |
| 依赖   | [格式化 sprintf](../libc/sprintf.md)、移植层 `mr_port_output()` |
| 测试   | `test/kernel/printf.c`、`test/kernel/printf_deep.c`             |

## 配置

| 配置项                   | 类型 | 默认值 | 说明                                                                |
|--------------------------|------|--------|---------------------------------------------------------------------|
| `MR_USE_PRINTF`          | bool | y      | 启用格式化输出                                                      |
| `MR_CFG_PRINTF_BUF_SIZE` | int  | 64     | 内部格式化缓冲字节数（8~65535），单次最多输出 `BUF_SIZE - 1` 字符   |
| `MR_USE_PRINTF_COLOR`    | bool | n      | 启用 ANSI 颜色转义宏；关闭时颜色宏展开为空                          |
| `MR_USE_PRINTF_SAFE`     | bool | n      | 每次调用改用 `mr_malloc()` 分配独立缓冲，避免多线程覆盖共享静态缓冲 |

## 快速上手

```c
#include <mrX/kernel/mr_printf.h>

void print_demo(int code) {
    mr_printf("boot ok, status=%s\r\n", "ready");
    mr_printf(MR_COLOR(MR_COLOR_RED, "error code: %d\r\n"), code);
}
```

## 核心机制与约束

- **截断行为**：`mr_vprintf()` 内部调用 `mr_vsnprintf()` 格式化到
  `MR_CFG_PRINTF_BUF_SIZE` 大小的缓冲中。若展开后超出
  `MR_CFG_PRINTF_BUF_SIZE - 1`，输出会被静默截断，不视为错误。
- **缓冲与线程安全**：默认模式下所有调用共享同一块 `static` 缓冲，不支持并发重入；开启
  `MR_USE_PRINTF_SAFE` 后每次调用动态申请与释放缓冲，消除格式化缓冲竞争，但整体线程/中断安全性仍取决于
  `mr_malloc()` 和 `mr_port_output()` 的实现。
- **返回值**：`fmt` 为 `MR_NULL` 或 `""`、或 `mr_vsnprintf()` 返回负数时返回
  `-MR_EINVAL`；开启 `MR_USE_PRINTF_SAFE` 且堆分配失败时返回 `-MR_ENOMEM`
  ；其余情况直接透传 `mr_port_output()` 的返回值（通常为实际输出字节数）。
- **换行与颜色**：模块不会自动追加 `\r\n`；`MR_COLOR(_color, _str)` 仅在开启
  `MR_USE_PRINTF_COLOR` 时拼接 ANSI 前后缀，否则原样展开 `_str`。

## API 速查

| API                                                    | 说明                                                      |
|--------------------------------------------------------|-----------------------------------------------------------|
| `int mr_printf(const char *fmt, ...)`                  | 格式化并输出到平台通道                                    |
| `int mr_vprintf(const char *fmt, mr_va_list args)`     | `va_list` 版本格式化输出（适合封装自定义前缀打印）        |
| `MR_COLOR(_color, _str)`                               | 用指定颜色宏包裹字符串字面量，并自动附加 `MR_COLOR_RESET` |
| `MR_COLOR_RESET`                                       | 重置终端颜色（`\033[0m`）                                 |
| `MR_COLOR_BLACK` / `RED` / `GREEN` / `YELLOW` / `BLUE` | 对应前景色 ANSI 转义串（`\033[30m` ~ `\033[34m`）         |

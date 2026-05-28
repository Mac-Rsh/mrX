# libc 格式化输入

`mr_sscanf.h` 和 `libc/sscanf.c` 提供从字符串读取格式化数据的接口。启用 `MR_USE_SSCANF_STD` 时直接映射到标准库 `sscanf()`；否则 mrX 当前实现对参数做空指针检查后调用标准库 `vsscanf()`。

## 核心接口

```c
int mr_sscanf(const char *buf, const char *fmt, ...);
```

`mr_sscanf()` 从 `buf` 中按 `fmt` 描述解析数据，并把结果写入可变参数指向的目标对象。返回值为成功赋值的字段数量；匹配失败时返回标准库 `vsscanf()` 的返回值。

## 实现方式

非 `MR_USE_SSCANF_STD` 配置下，源码实现如下行为：

1. 若 `buf == MR_NULL` 或 `fmt == MR_NULL`，返回 `-1`。
2. 使用 `va_start()` 初始化参数列表。
3. 调用 `vsscanf(buf, fmt, args)` 完成实际解析。
4. 使用 `va_end()` 结束参数列表并返回解析结果。

## 标准库映射

启用 `MR_USE_SSCANF_STD` 时：

```c
#define mr_sscanf(_b, _fmt, ...) sscanf(_b, _fmt, ##__VA_ARGS__)
```

此时空指针行为完全取决于平台标准库，不经过 mrX 的空指针检查。

## 使用示例

```c
#include <mrX/libc/mr_sscanf.h>

void sample_sscanf_usage(void)
{
    int id;
    char name[8];

    if (mr_sscanf("42 mrX", "%d %7s", &id, name) == 2) {
        /* id == 42, name == "mrX" */
    }
}
```

## 注意事项

- 当前非标准配置仍依赖 `<stdio.h>` 和 `vsscanf()`，不是完全独立的 scanf 实现。
- 返回值语义主要由平台 `vsscanf()` 决定。测试中，空输入配合 `"%d"` 返回 `-1`，格式不匹配返回 `0`。
- 调用方必须为 `%s` 等转换指定足够的目标缓冲区，建议在格式串中使用宽度限制，例如 `%7s`。
- `fmt` 为空字符串时，测试覆盖的行为为返回 `0`。

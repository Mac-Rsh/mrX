# libc 基础类型

`mr_types.h` 为 mrX 提供统一的基础类型、边界值宏、布尔值、空指针和可变参数封装。该头文件用于减少不同编译环境、标准库配置和 32/64 位架构之间的类型差异。

## 核心类型和宏

### 整数类型

当启用 `MR_USE_TYPES_STD` 时，整数类型来自 `<stdint.h>`；否则使用 C 基础类型定义固定宽度整数。

| 类型 | 含义 |
|---|---|
| `mr_int8_t`、`mr_int16_t`、`mr_int32_t`、`mr_int64_t` | 8/16/32/64 位有符号整数。 |
| `mr_uint8_t`、`mr_uint16_t`、`mr_uint32_t`、`mr_uint64_t` | 8/16/32/64 位无符号整数。 |
| `MR_INT8_MIN` 到 `MR_INT64_MAX` | 有符号整数边界值。 |
| `MR_UINT8_MAX` 到 `MR_UINT64_MAX` | 无符号整数最大值。 |

### 大小和指针类型

```c
typedef size_t mr_size_t;      /* MR_USE_TYPES_STD */
typedef mr_int64_t mr_ssize_t; /* MR_USE_ARCH_64B */
typedef void *mr_ptr_t;
```

`mr_size_t` 在标准类型模式下映射到 `size_t`，否则根据 `MR_USE_ARCH_64B` 选择 `mr_uint64_t` 或 `mr_uint32_t`。`mr_ssize_t` 始终根据架构位宽选择有符号类型。

| 宏 | 含义 |
|---|---|
| `MR_SIZE_MAX` | `mr_size_t` 最大值。 |
| `MR_SSIZE_MIN`、`MR_SSIZE_MAX` | `mr_ssize_t` 边界值。 |
| `mr_intptr_t`、`mr_uintptr_t` | 可保存指针值的有符号/无符号整数类型。 |
| `MR_NULL` | 空指针。标准类型模式下为 `NULL`，否则为 `((void *)0)`。 |

### 布尔和浮点类型

`mr_bool_t` 在 `MR_USE_TYPES_STD` 下映射为 `bool`，否则为 `int`。对应布尔常量为 `MR_FALSE` 和 `MR_TRUE`。

`mr_f32_t` 和 `mr_f64_t` 分别固定映射到 `float` 和 `double`。

### 可变参数封装

```c
typedef va_list mr_va_list;
#define mr_va_start(_a, _l) va_start(_a, _l)
#define mr_va_arg(_a, _t)   va_arg(_a, _t)
#define mr_va_end(_a)       va_end(_a)
```

可变参数直接使用 `<stdarg.h>`，并通过 `mr_` 前缀宏暴露给格式化输出等模块。

## 主要接口

该模块不提供运行时函数，主要通过 typedef 和宏为其他模块提供基础 ABI 类型。

## 使用示例

```c
#include <mrX/libc/mr_types.h>

mr_size_t copy_limit(mr_size_t requested)
{
    if (requested > 1024U) {
        return 1024U;
    }
    return requested;
}

mr_bool_t is_empty(const void *ptr)
{
    return (ptr == MR_NULL) ? MR_TRUE : MR_FALSE;
}
```

## 注意事项

- `MR_USE_TYPES_STD` 决定是否依赖 `<stdbool.h>`、`<stddef.h>` 和 `<stdint.h>`。
- 未启用标准类型模式时，固定宽度类型依赖当前编译器对 `char`、`short`、`int` 和 `long long` 的宽度约定。
- `mr_size_t` 和 `mr_ssize_t` 与 `MR_USE_ARCH_64B` 相关，跨架构保存二进制数据时不要假设它们固定为 32 位。
- `mr_va_list` 仍然是标准 `va_list`，不能跨函数重复消费同一个参数列表，除非调用方按 C 标准规则复制或重新初始化。

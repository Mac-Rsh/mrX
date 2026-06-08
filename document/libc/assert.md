[**中文**](libc_assert.md) | [**English**](libc_assert_en.md)

---

# libc 断言

`mr_assert.h` 提供运行时断言、panic 和编译期静态断言。该模块根据配置选择标准库断言、第三方断言、mrX port 层断言或空断言实现。

## 核心宏和接口

### 运行时断言

```c
#define MR_ASSERT(_expr) ...
#define MR_PANIC()       ...
```

| 配置                  | `MR_ASSERT(_expr)` 行为                                          | `MR_PANIC()` 行为                                           |
|---------------------|----------------------------------------------------------------|-----------------------------------------------------------|
| `MR_USE_ASSERT_STD` | 调用 `assert(_expr)`。                                            | 调用 `assert(0)`。                                           |
| `MR_USE_ASSERT_3RD` | 由 `mrX/3party/libc/mr_assert.h` 提供。                            | 由第三方头文件提供。                                                |
| `MR_USE_ASSERT`     | 表达式为假时调用 `mr_port_assert(__FILE__, __LINE__, #_expr)`，随后进入死循环。 | 调用 `mr_port_assert(__FILE__, __LINE__, "panic")`，随后进入死循环。 |
| 未启用上述配置             | 表达式不被求值，宏为空操作。                                                 | 直接进入死循环。                                                  |

### port 层断言回调

```c
extern void mr_port_assert(const char *file, int line, const char *expr);
```

启用 `MR_USE_ASSERT` 时，断言失败会调用 port 层实现。`file` 为源码文件，`line` 为行号，`expr` 为失败表达式字符串或
`"panic"`。

### 静态断言

```c
#define MR_STATIC_ASSERT(_name, _expr) \
    typedef char __mr_static_assert_##_name[(_expr) ? 1 : -1]
```

`MR_STATIC_ASSERT()` 在编译期检查表达式。表达式为假时数组长度为 `-1`，编译失败。

## 使用示例

```c
#include <mrX/libc/mr_assert.h>
#include <mrX/libc/mr_types.h>

MR_STATIC_ASSERT(size_is_supported, sizeof(void *) <= sizeof(mr_uint64_t));

void sample_require_pointer(void *ptr)
{
    MR_ASSERT(ptr != MR_NULL);
}
```

port 层可实现如下回调，用于输出断言位置或触发平台调试机制：

```c
#include <mrX/port/mr_port_assert.h>

void mr_port_assert(const char *file, int line, const char *expr)
{
    (void)file;
    (void)line;
    (void)expr;
}
```

## 注意事项

- 未启用断言时，`MR_ASSERT(_expr)` 展开为 `((void)0)`，表达式不会执行，因此不要在断言表达式中放置有副作用的代码。
- `MR_PANIC()` 在非标准、非第三方配置下都会进入无限循环。
- `MR_USE_ASSERT` 模式下，`mr_port_assert()` 返回后仍会进入无限循环。
- `MR_STATIC_ASSERT()` 会生成 typedef 名称，同一作用域内 `_name` 不能重复。

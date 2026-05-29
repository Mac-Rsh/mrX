[**中文**](libc_errno.md) | [**English**](libc_errno_en.md)

---

# libc 错误码

`mr_errno.h` 定义 mrX 内部使用的错误码类型和错误码宏。该模块统一屏蔽标准库、第三方 libc
和内置错误码之间的差异，供内核对象、内存管理和其他组件返回负错误码或错误原因。

## 核心类型和宏

### 错误类型

```c
typedef int mr_err_t;
```

`mr_err_t` 用于表示接口返回状态。mrX 源码中常见约定是成功返回 `0`，失败返回负错误码，例如 `-MR_EINVAL`。

### 错误码来源

| 配置                 | 行为                                            |
|--------------------|-----------------------------------------------|
| `MR_USE_ERRNO_STD` | 包含 `<errno.h>`，`MR_EPERM` 等宏映射到标准 errno。      |
| `MR_USE_ERRNO_3RD` | 包含 `mrX/3party/libc/mr_errno.h`，错误码由第三方头文件提供。 |
| 未启用上述配置            | 使用 `mr_errno.h` 内置数值。                         |

### 内置错误码

| 宏              | 内置值 | 含义      |
|----------------|----:|---------|
| `MR_EPERM`     |   1 | 操作不允许。  |
| `MR_ENOENT`    |   2 | 目标不存在。  |
| `MR_EIO`       |   5 | I/O 错误。 |
| `MR_EAGAIN`    |  11 | 资源暂不可用。 |
| `MR_ENOMEM`    |  12 | 内存不足。   |
| `MR_EBUSY`     |  16 | 资源忙。    |
| `MR_EEXIST`    |  17 | 目标已存在。  |
| `MR_EINVAL`    |  22 | 参数无效。   |
| `MR_ENOSYS`    |  40 | 功能未实现。  |
| `MR_ETIMEDOUT` | 116 | 操作超时。   |

## 主要接口

该模块不提供函数接口，只提供 `mr_err_t` 和错误码宏。错误码字符串转换由 `mr_strerror()` 提供，见 `libc_string.md`。

## 使用示例

```c
#include <mrX/libc/mr_errno.h>
#include <mrX/libc/mr_types.h>

mr_err_t sample_check(void *ptr)
{
    if (ptr == MR_NULL) {
        return -MR_EINVAL;
    }
    return 0;
}
```

## 注意事项

- 文档中的内置值只适用于未启用 `MR_USE_ERRNO_STD` 和 `MR_USE_ERRNO_3RD` 的配置。
- 使用标准 errno 时，具体数值由平台 C 库决定。
- mrX 代码通常返回负错误码；错误码宏本身为正数。
- 该头文件只声明错误码，不维护线程局部 `errno` 变量。

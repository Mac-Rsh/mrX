# 字符串（String）

提供嵌入式友好的内存操作、字符串处理与错误码描述原语（`mr_memcpy`、`mr_memset`、
`mr_strcmp`、`mr_strerror` 等），以及始终可用的 De Bruijn 位查找内联函数（
`mr_ffs32` / `mr_fls64` 等）。

| 项目   | 说明                                                                                                                                                  |
|--------|-------------------------------------------------------------------------------------------------------------------------------------------------------|
| 功能宏 | `MR_USE_STRING`（内置实现）；`MR_USE_STRING_STD`（转发标准库）；`MR_USE_STRING_3RD`（Kconfig 预留项，当前无实现）；`MR_USE_STRING_SAFE`（空指针保护） |
| 头文件 | `<mrX/libc/mr_string.h>`                                                                                                                              |
| 依赖   | `<mrX/libc/mr_compiler.h>`、`<mrX/libc/mr_types.h>`、`<mrX/libc/mr_errno.h>`                                                                          |
| 测试   | `test/libc/string.c`                                                                                                                                  |

## 配置

| 配置项               | 类型           | 默认值 | 说明                                                         |
|----------------------|----------------|--------|--------------------------------------------------------------|
| `MR_USE_STRING`      | bool（choice） | y      | 使用内置内存与字符串实现                                     |
| `MR_USE_STRING_STD`  | bool（choice） | n      | 宏转发标准库 `<string.h>` 同名函数（位查找原语仍为内置实现） |
| `MR_USE_STRING_3RD`  | bool（choice） | n      | 第三方字符串库预留项，当前头文件与源码无对应实现分支         |
| `MR_USE_STRING_SAFE` | bool           | n      | 依赖 `MR_USE_STRING`；为内置实现增加 `MR_NULL` 入口检查      |

## 快速上手

```c
#include <mrX/libc/mr_string.h>
#include <mrX/libc/mr_errno.h>

void example(void) {
    char src[16], dst[16];

    mr_strcpy(src, "hello");
    mr_memcpy(dst, src, mr_strlen(src) + 1);

    if (mr_strcmp(dst, "hello") == 0) {
        const char *msg = mr_strerror(MR_ENOMEM);   /* "Out of memory" */
        (void)msg;
    }
}
```

## 核心机制与约束

- **对齐加速与区间重叠**：`mr_memcpy`、`mr_memmove` 与 `mr_memset` 在指针按
  `sizeof(mr_uintptr_t)` 对齐匹配时按机器字整块搬运或填充，不匹配时自动退化为逐字节操作。
  `mr_memcpy` 要求源与目的区间不得重叠，重叠场景必须使用 `mr_memmove`。
- **无符号比较与边界语义**：`mr_memcmp`、`mr_strcmp` 与 `mr_strncmp` 均按无符号字节序比较（
  `0x80 > 0x7F`）。`mr_strncpy` 最多拷贝 `size` 字节，源串较短时剩余空间补 `\0`
  ，源串达到或超过 `size` 时 **不追加 `\0`**。`mr_strchr` 的搜索范围包含结尾 `\0`。
- **`mr_strerror` 取值**：内置错误表仅识别 `0` 及正的 `MR_E*` 常量（如 `MR_ENOMEM`
  ），未知码返回 `"Unknown error"`；若某 API 返回 `-MR_ENOMEM`
  ，传入前需取反。返回指针指向静态只读字符串，无需释放。
- **`MR_USE_STRING_SAFE` 空指针保护**：仅对 `MR_USE_STRING` 生效。开启后传入
  `MR_NULL` 不会解引用崩溃：拷贝/查找类（`memcpy`/`memmove`/`memset`/`memccpy`/
  `memchr`/`strcpy`/`strchr`）返回 `MR_NULL`（`strncpy` 原样返回 `dst`），长度类（
  `strlen`/`strnlen`）返回 `0`，比较类（`memcmp`/`strcmp`/`strncmp`）返回 `0`
  （注意这与“相等”返回值相同）。该模式不检查缓冲区容量或悬空指针。
- **并发与中断安全**：内置实现无全局可变状态与锁，可重入；不同缓冲区可并发操作。

## API 速查

**内存操作**：

| API                                                                   | 说明                                                               |
|-----------------------------------------------------------------------|--------------------------------------------------------------------|
| `void *mr_memcpy(void *dst, const void *src, mr_size_t size)`         | 拷贝 `size` 字节（不可重叠），返回 `dst`                           |
| `void *mr_memmove(void *dst, const void *src, mr_size_t size)`        | 拷贝 `size` 字节（支持重叠），返回 `dst`                           |
| `void *mr_memset(void *dst, int c, mr_size_t size)`                   | 将 `dst` 起 `size` 字节设为 `(unsigned char)c`，返回 `dst`         |
| `int mr_memcmp(const void *m1, const void *m2, mr_size_t size)`       | 比较 `size` 字节，返回首个差异字节的无符号差值                     |
| `void *mr_memchr(const void *s, int c, mr_size_t size)`               | 在 `size` 字节内查找字节 `c`，未找到返回 `MR_NULL`                 |
| `void *mr_memccpy(void *dst, const void *src, int c, mr_size_t size)` | 拷贝至遇到字节 `c`（含）为止，返回其后继指针，未遇到返回 `MR_NULL` |

**字符串操作**：

| API                                                              | 说明                                                  |
|------------------------------------------------------------------|-------------------------------------------------------|
| `int mr_strcmp(const char *s1, const char *s2)`                  | 比较字符串，返回首个差异字节的无符号差值              |
| `int mr_strncmp(const char *s1, const char *s2, mr_size_t size)` | 最多比较 `size` 字节                                  |
| `char *mr_strcpy(char *dst, const char *src)`                    | 拷贝含 `\0` 的字符串，返回 `dst`                      |
| `char *mr_strncpy(char *dst, const char *src, mr_size_t size)`   | 最多拷贝 `size` 字节并补 `\0`（超长不补），返回 `dst` |
| `mr_size_t mr_strlen(const char *s)`                             | 返回串长（不含 `\0`）                                 |
| `mr_size_t mr_strnlen(const char *s, mr_size_t size)`            | 返回串长，上限为 `size`                               |
| `char *mr_strchr(const char *s, int c)`                          | 查找首个匹配字符（可匹配 `\0`），未找到返回 `MR_NULL` |
| `char *mr_strerror(int errnum)`                                  | 将 `MR_E*` 错误码映射为静态英文描述字符串             |

**位查找原语**（`MR_INLINE`，定义于头文件，始终编译）：

| API                                                           | 说明                                                     |
|---------------------------------------------------------------|----------------------------------------------------------|
| `int mr_ffs32(mr_uint32_t x)` / `int mr_ffs64(mr_uint64_t x)` | 返回最低置位位的序号（`1 ~ 32/64`），入参为 `0` 返回 `0` |
| `int mr_fls32(mr_uint32_t x)` / `int mr_fls64(mr_uint64_t x)` | 返回最高置位位的序号（`1 ~ 32/64`），入参为 `0` 返回 `0` |

`include/mrX/libc/` 下还提供 `mr_assert.h`（断言与 panic）、`mr_atomic.h`（原子操作原语）、
`mr_compiler.h`（编译器属性宏）、`mr_errno.h`（`MR_E*` 错误码定义）与 `mr_types.h`
（基础类型与 `mr_va_list`）纯头文件工具，详见各头文件注释。

## 内部实现

- **字长搬运与填充**：`libc/string.c` 中 `mr_memcpy` / `mr_memmove` 检查
  `(dst_addr ^ src_addr) & (sizeof(mr_uintptr_t) - 1)` 判断对齐兼容性；
  `mr_memset` 将填充字节扩展为机器字宽模式字（`MR_USE_ARCH_64B` 下扩展至 64
  位）后分头、中、尾三段写入。
- **组合实现**：`mr_strncpy` 通过 `mr_memccpy(dst, src, '\0', size)` 拷贝并在命中后用
  `mr_memset` 补零；`mr_strchr` 通过 `mr_memchr(s, c, mr_strlen(s) + 1)` 实现。
- **De Bruijn 位查表**：`mr_ffs32` / `mr_ffs64` 通过 `x & (-x)` 分离最低置位位后乘以
  De Bruijn 常数查表；`mr_fls32` / `mr_fls64` 将最高位以下全部置 `1` 并分离最高位后复用
  `mr_ffs*`。

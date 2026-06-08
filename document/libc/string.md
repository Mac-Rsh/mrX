[**中文**](libc_string.md) | [**English**](libc_string_en.md)

---

# libc 字符串和内存工具

`mr_string.h` 和 `libc/string.c` 提供内存操作、C 字符串操作、错误码字符串和位扫描工具。启用 `MR_USE_STRING_STD`
时，多数字符串接口映射到标准库；否则由 mrX 自实现提供。位扫描函数始终以内联函数提供。

## 核心接口

### 内存操作

```c
void *mr_memccpy(void *dst, const void *src, int c, mr_size_t size);
int mr_memcmp(const void *m1, const void *m2, mr_size_t size);
void *mr_memcpy(void *dst, const void *src, mr_size_t size);
void *mr_memset(void *dst, int c, mr_size_t size);
void *mr_memchr(const void *s, int c, mr_size_t size);
```

`mr_memccpy()` 从 `src` 向 `dst` 复制，遇到字节 `c` 后停止并返回目标缓冲区中该字节后一位的指针；未遇到则返回 `MR_NULL`。
`mr_memcpy()` 在地址按 `mr_uintptr_t` 对齐时按机器字复制，否则按字节复制。`mr_memset()` 构造机器字填充值后尽量按对齐写入。

### 字符串操作

```c
int mr_strcmp(const char *s1, const char *s2);
char *mr_strcpy(char *dst, const char *src);
char *mr_strerror(int errnum);
mr_size_t mr_strlen(const char *s);
mr_size_t mr_strnlen(const char *s, mr_size_t size);
int mr_strncmp(const char *s1, const char *s2, mr_size_t size);
char *mr_strncpy(char *dst, const char *src, mr_size_t size);
char *mr_strchr(const char *s, int c);
```

`mr_strerror()` 返回内置错误码的英文描述字符串，未知错误返回 `"Unknown error"`。`mr_strncpy()` 基于 `mr_memccpy()`
实现，若源字符串在限制长度内结束，则用 `\0` 填充剩余空间。

### 位扫描

```c
int mr_ffs32(mr_uint32_t x);
int mr_ffs64(mr_uint64_t x);
int mr_fls32(mr_uint32_t x);
int mr_fls64(mr_uint64_t x);
```

`mr_ffs32()` 和 `mr_ffs64()` 返回最低有效置位的 1 基下标；输入为 0 时返回 0。`mr_fls32()` 和 `mr_fls64()` 返回最高有效置位的
1 基下标；输入为 0 时返回 0。

## 标准库映射

启用 `MR_USE_STRING_STD` 时，`mr_memcmp()`、`mr_memcpy()`、`mr_memset()`、`mr_memchr()`、`mr_strcmp()`、`mr_strcpy()`、
`mr_strerror()`、`mr_strlen()`、`mr_strnlen()`、`mr_strncmp()`、`mr_strncpy()` 和 `mr_strchr()` 直接映射到标准库同名函数。
`mr_memccpy()` 映射到 `memccpy()`。

## 使用示例

```c
#include <mrX/libc/mr_string.h>

void sample_string_usage(void)
{
    char buf[16];

    mr_memset(buf, 0, sizeof(buf));
    mr_strncpy(buf, "abc", sizeof(buf));

    if (mr_strchr(buf, 'b') != MR_NULL) {
        (void)mr_strlen(buf);
    }
}
```

```c
int bit_index(mr_uint32_t flags)
{
    return mr_ffs32(flags);
}
```

## 注意事项

- 自实现版本只在 `MR_USE_STRING` 编译开关启用时参与构建。
- 启用 `MR_USE_STRING_SAFE` 时，部分接口会对空指针返回安全值，例如 `MR_NULL`、`0` 或比较结果 `0`；未启用时传入空指针属于未定义行为。
- `mr_memcpy()` 不处理源和目标区域重叠，不能替代 `memmove()`。
- `mr_strcpy()` 不检查目标缓冲区大小，调用方必须保证空间足够。
- `mr_strchr()` 通过 `mr_strlen(s) + 1` 限定搜索范围，因此空指针行为取决于 `mr_strlen()` 和 `mr_memchr()` 的安全配置。
- 测试覆盖内存复制/填充/比较/搜索、字符串复制/比较/长度、错误字符串和 32/64 位 ffs/fls 基本边界。

# string - 字符串操作

## 1. 模块概述

### 1.1 功能介绍

string 模块提供字符串和内存操作函数，类似于标准 C 库的 `<string.h>`。它包含内存操作函数（memcpy、memcmp、memset 等）、字符串操作函数（strcmp、strcpy、strlen 等）以及位操作函数（ffs、fls）。

该模块支持两种配置模式：STD 模式（使用标准库）和自定义模式（使用内置实现）。

### 1.2 主要特性

- **内存操作**：复制、比较、填充、查找
- **字符串操作**：复制、比较、长度、查找
- **位操作**：查找首/末置位（使用 DeBruijn 算法）
- **标准兼容**：接口与标准 C 库一致

### 1.3 适用场景

string 模块适用于以下场景：

- **字符串处理**：复制、比较、拼接字符串
- **内存操作**：复制、填充内存块
- **位操作**：查找位位置、计算最高位
- **裸机环境**：没有标准库的嵌入式系统

---

## 2. 使用指南

### 2.1 内存操作函数

#### mr_memcpy() - 内存复制

```c
void *mr_memcpy(void *dst, const void *src, mr_size_t size);
```

**功能**：从 src 复制 size 字节到 dst

**参数**：

- `dst`：目标地址
- `src`：源地址
- `size`：复制字节数

**返回值**：返回 dst

**示例**：

```c
#include <mrX/libc/mr_string.h>

mr_uint8_t src[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
mr_uint8_t dst[10];

mr_memcpy(dst, src, sizeof(src));
/* dst 现在包含 {0, 1, 2, 3, 4, 5, 6, 7, 8, 9} */
```

**注意事项**：

- 内存区域不能重叠
- 如果内存重叠，应使用 `mr_memccpy` 或手动处理

#### mr_memccpy() - 条件内存复制

```c
void *mr_memccpy(void *dst, const void *src, int c, mr_size_t size);
```

**功能**：从 src 复制字节到 dst，直到遇到字节 c 或复制了 size 字节

**参数**：

- `dst`：目标地址
- `src`：源地址
- `c`：停止字符
- `size`：最大复制字节数

**返回值**：

- 找到 c：返回 dst 中 c 之后的指针
- 未找到 c：返回 MR_NULL

**示例**：

```c
char src[] = "hello:world";
char dst[20];

void *result = mr_memccpy(dst, src, ':', sizeof(src));
if (result != MR_NULL) {
    printf("Found ':' at offset %td\n", (char *)result - dst);
}
/* 输出: Found ':' at offset 5 */
```

#### mr_memcmp() - 内存比较

```c
int mr_memcmp(const void *m1, const void *m2, mr_size_t size);
```

**功能**：比较两个内存区域

**参数**：

- `m1`：第一个内存区域
- `m2`：第二个内存区域
- `size`：比较字节数

**返回值**：

- `< 0`：m1 < m2
- `= 0`：m1 == m2
- `> 0`：m1 > m2

**示例**：

```c
mr_uint8_t a[4] = {1, 2, 3, 4};
mr_uint8_t b[4] = {1, 2, 3, 5};
mr_uint8_t c[4] = {1, 2, 3, 4};

int cmp_ab = mr_memcmp(a, b, 4);  /* < 0 */
int cmp_ac = mr_memcmp(a, c, 4);  /* = 0 */
int cmp_ba = mr_memcmp(b, a, 4);  /* > 0 */
```

#### mr_memset() - 内存填充

```c
void *mr_memset(void *dst, int c, mr_size_t size);
```

**功能**：将 dst 的前 size 字节设置为 c

**参数**：

- `dst`：目标地址
- `c`：填充值（转换为 unsigned char）
- `size`：填充字节数

**返回值**：返回 dst

**示例**：

```c
/* 清零缓冲区 */
mr_uint8_t buffer[100];
mr_memset(buffer, 0, sizeof(buffer));

/* 填充特定值 */
mr_memset(buffer, 0xFF, sizeof(buffer));

/* 初始化结构体 */
typedef struct {
    char name[32];
    int id;
} person_t;

person_t p;
mr_memset(&p, 0, sizeof(p));
```

#### mr_memchr() - 内存查找

```c
void *mr_memchr(const void *s, int c, mr_size_t size);
```

**功能**：在 s 的前 size 字节中查找 c

**参数**：

- `s`：要搜索的内存区域
- `c`：要查找的字节
- `size`：搜索大小

**返回值**：

- 找到：返回指向 c 的指针
- 未找到：返回 MR_NULL

**示例**：

```c
char data[] = "hello:world";

char *colon = mr_memchr(data, ':', sizeof(data));
if (colon != MR_NULL) {
    printf("Found ':' at position %td\n", colon - data);
}
```

### 2.2 字符串操作函数

#### mr_strcmp() - 字符串比较

```c
int mr_strcmp(const char *s1, const char *s2);
```

**功能**：比较两个字符串

**参数**：

- `s1`：第一个字符串
- `s2`：第二个字符串

**返回值**：

- `< 0`：s1 < s2
- `= 0`：s1 == s2
- `> 0`：s1 > s2

**示例**：

```c
char *str1 = "apple";
char *str2 = "banana";
char *str3 = "apple";

int cmp_12 = mr_strcmp(str1, str2);  /* < 0 */
int cmp_13 = mr_strcmp(str1, str3);  /* = 0 */
int cmp_21 = mr_strcmp(str2, str1);  /* > 0 */
```

#### mr_strcpy() - 字符串复制

```c
char *mr_strcpy(char *dst, const char *src);
```

**功能**：将 src 复制到 dst（包括终止空字符）

**参数**：

- `dst`：目标缓冲区
- `src`：源字符串

**返回值**：返回 dst

**示例**：

```c
char src[] = "hello";
char dst[20];

mr_strcpy(dst, src);
printf("%s\n", dst);  /* 输出: hello */
```

**注意事项**：

- dst 必须有足够空间
- 缓冲区溢出风险
- 推荐使用 `mr_strncpy` 代替

#### mr_strlen() - 字符串长度

```c
mr_size_t mr_strlen(const char *s);
```

**功能**：返回字符串的长度（不包括终止空字符）

**参数**：

- `s`：字符串

**返回值**：字符串长度

**示例**：

```c
char *str = "hello";
mr_size_t len = mr_strlen(str);
printf("Length: %u\n", len);  /* 输出: Length: 5 */
```

#### mr_strnlen() - 受限字符串长度

```c
mr_size_t mr_strnlen(const char *s, mr_size_t size);
```

**功能**：返回字符串长度，最多检查 size 字节

**参数**：

- `s`：字符串
- `size`：最大检查字节数

**返回值**：字符串长度（不超过 size）

**示例**：

```c
char buffer[10] = "hello";
mr_size_t len = mr_strnlen(buffer, sizeof(buffer));
printf("Length: %u\n", len);  /* 输出: Length: 5 */
```

**安全优势**：避免遍历没有终止空字符的缓冲区。

#### mr_strncmp() - 受限字符串比较

```c
int mr_strncmp(const char *s1, const char *s2, mr_size_t size);
```

**功能**：比较两个字符串的前 size 个字符

**参数**：

- `s1`：第一个字符串
- `s2`：第二个字符串
- `size`：最大比较字符数

**返回值**：

- `< 0`：s1 < s2
- `= 0`：s1 == s2（前 size 个字符）
- `> 0`：s1 > s2

**示例**：

```c
char *s1 = "hello world";
char *s2 = "hello there";

int cmp = mr_strncmp(s1, s2, 5);
printf("Comparison: %d\n", cmp);  /* 输出: Comparison: 0 */
```

#### mr_strncpy() - 受限字符串复制

```c
char *mr_strncpy(char *dst, const char *src, mr_size_t size);
```

**功能**：从 src 复制最多 size 个字符到 dst

**参数**：

- `dst`：目标缓冲区
- `src`：源字符串
- `size`：最大复制字符数

**返回值**：返回 dst

**示例**：

```c
char src[] = "hello world";
char dst[6];

mr_strncpy(dst, src, sizeof(dst) - 1);
dst[sizeof(dst) - 1] = '\0';  /* 确保终止 */
printf("%s\n", dst);  /* 输出: hello */
```

**注意事项**：

- 如果 strlen(src) >= size，dst 不会自动添加终止空字符
- 需要手动添加终止空字符

#### mr_strchr() - 字符查找

```c
char *mr_strchr(const char *s, int c);
```

**功能**：在字符串中查找字符 c

**参数**：

- `s`：字符串
- `c`：要查找的字符

**返回值**：

- 找到：返回指向 c 的指针
- 未找到：返回 MR_NULL

**示例**：

```c
char *str = "hello:world";
char *colon = mr_strchr(str, ':');
if (colon != MR_NULL) {
    printf("Found ':' at position %td\n", colon - str);
    printf("After ':': %s\n", colon + 1);
}
```

#### mr_strerror() - 错误信息

```c
char *mr_strerror(int errnum);
```

**功能**：返回错误码的描述字符串

**参数**：

- `errnum`：错误码

**返回值**：错误描述字符串

**示例**：

```c
mr_err_t ret = some_function();
if (ret != 0) {
    printf("Error: %s\n", mr_strerror(-ret));
}
```

### 2.3 位操作函数

#### mr_ffs32() - 查找首置位（32位）

```c
int mr_ffs32(mr_uint32_t x);
```

**功能**：查找最低位的 1 的位置（从 1 开始）

**参数**：

- `x`：要搜索的值

**返回值**：

- `0`：x 为 0
- `1-32`：首置位的位置（1 是最低位）

**示例**：

```c
int pos;

pos = mr_ffs32(0x00000001);  /* 返回 1 */
pos = mr_ffs32(0x00000010);  /* 返回 5 */
pos = mr_ffs32(0x80000000);  /* 返回 32 */
pos = mr_ffs32(0x00000000);  /* 返回 0 */
```

**应用场景**：

- 查找最低位设置的中断号
- 确定是否为 2 的幂（`ffs32(x) == ffs32(~x + 1)`）
- 快速计算 trailing zeros

#### mr_ffs64() - 查找首置位（64位）

```c
int mr_ffs64(mr_uint64_t x);
```

**功能**：查找 64 位值中最低位的 1 的位置

**示例**：

```c
int pos;

pos = mr_ffs64(0x0000000000000001ULL);  /* 返回 1 */
pos = mr_ffs64(0x0000000100000000ULL);  /* 返回 33 */
pos = mr_ffs64(0x8000000000000000ULL);  /* 返回 64 */
```

#### mr_fls32() - 查找末置位（32位）

```c
int mr_fls32(mr_uint32_t x);
```

**功能**：查找最高位的 1 的位置（从 1 开始）

**参数**：

- `x`：要搜索的值

**返回值**：

- `0`：x 为 0
- `1-32`：末置位的位置

**示例**：

```c
int pos;

pos = mr_fls32(0x00000001);  /* 返回 1 */
pos = mr_fls32(0x00000010);  /* 返回 5 */
pos = mr_fls32(0x80000000);  /* 返回 32 */
pos = mr_fls32(0x00000000);  /* 返回 0 */
```

**应用场景**：

- 计算需要的字节数（`fls32(x) + 7) / 8`）
- 确定数值大小
- 位掩码操作

#### mr_fls64() - 查找末置位（64位）

```c
int mr_fls64(mr_uint64_t x);
```

**功能**：查找 64 位值中最高位的 1 的位置

**示例**：

```c
int pos;

pos = mr_fls64(0x0000000000000001ULL);  /* 返回 1 */
pos = mr_fls64(0x0000000100000000ULL);  /* 返回 33 */
pos = mr_fls64(0x8000000000000000ULL);  /* 返回 64 */
```

#### DeBruijn 算法原理

mr_ffs32 使用 DeBruijn 序列实现 O(1) 查找：

```c
MR_INLINE int mr_ffs32(mr_uint32_t x) {
    const int debruijn32[32] = {
        1,  2,  29, 3,  30, 15, 25, 4,  31, 23, 21, 16, 26, 18, 5,  9,
        32, 28, 14, 24, 22, 20, 17, 8,  27, 13, 19, 7,  12, 6,  11, 10,
    };

    if (x == 0) {
        return 0;
    }

    return debruijn32[((x & (-x)) * 0x077cb531U) >> 27];
}
```

**工作原理**：

1. `x & (-x)`：提取最低位的 1（创建只有一个位设置的值）
2. 乘以魔数 `0x077cb531`
3. 右移 27 位，得到查找索引
4. 在 DeBruijn 表中查找结果

**复杂度**：O(1)，比循环查找快得多。

### 2.4 配置模式

#### STD 模式（标准库模式）

```c
#define MR_USE_STRING_STD
```

**特点**：

- 直接使用标准库的 `<string.h>`
- 所有函数映射到标准函数

```c
#if defined(MR_USE_STRING_STD)
#define mr_memcpy(_d, _s, _sz)          memcpy(_d, _s, _sz)
#define mr_memcmp(_m1, _m2, _sz)        memcmp(_m1, _m2, _sz)
/* ... */
#endif
```

#### 自定义模式

**特点**：

- 使用内置实现
- 适合裸机环境

### 2.5 典型应用案例

#### 案例 1：安全的字符串复制

```c
#include <mrX/libc/mr_string.h>

mr_err_t safe_copy(char *dst, mr_size_t dst_size, const char *src) {
    mr_size_t src_len = mr_strnlen(src, dst_size);

    if (src_len >= dst_size) {
        return -MR_EINVAL;  /* 目标缓冲区太小 */
    }

    mr_strcpy(dst, src);
    return 0;
}

/* 使用 */
char buffer[16];
mr_err_t ret = safe_copy(buffer, sizeof(buffer), "hello");
```

#### 案例 2：位图操作

```c
#include <mrX/libc/mr_string.h>

#define BITMAP_SIZE  4

typedef struct {
    mr_uint32_t bits[BITMAP_SIZE];
} bitmap_t;

void bitmap_set(bitmap_t *bmp, int bit) {
    int index = bit / 32;
    int offset = bit % 32;
    bmp->bits[index] |= (1U << offset);
}

void bitmap_clear(bitmap_t *bmp, int bit) {
    int index = bit / 32;
    int offset = bit % 32;
    bmp->bits[index] &= ~(1U << offset);
}

int bitmap_find_first_set(bitmap_t *bmp) {
    for (int i = 0; i < BITMAP_SIZE; i++) {
        if (bmp->bits[i]) {
            int bit = mr_ffs32(bmp->bits[i]);
            return i * 32 + bit - 1;
        }
    }
    return -1;  /* 全部为 0 */
}

int bitmap_find_last_set(bitmap_t *bmp) {
    for (int i = BITMAP_SIZE - 1; i >= 0; i--) {
        if (bmp->bits[i]) {
            int bit = mr_fls32(bmp->bits[i]);
            return i * 32 + bit - 1;
        }
    }
    return -1;  /* 全部为 0 */
}
```

#### 案例 3：命令解析

```c
#include <mrX/libc/mr_string.h>

void parse_command(const char *cmd) {
    const char *space = mr_strchr(cmd, ' ');
    char command[16];
    char args[32];

    if (space == MR_NULL) {
        /* 没有参数 */
        mr_strncpy(command, cmd, sizeof(command) - 1);
        args[0] = '\0';
    } else {
        /* 分离命令和参数 */
        mr_size_t cmd_len = space - cmd;
        if (cmd_len >= sizeof(command)) {
            cmd_len = sizeof(command) - 1;
        }
        mr_memcpy(command, cmd, cmd_len);
        command[cmd_len] = '\0';

        mr_strncpy(args, space + 1, sizeof(args) - 1);
    }

    printf("Command: %s\n", command);
    printf("Args: %s\n", args);
}
```

#### 案例 4：快速计算 2 的幂

```c
#include <mrX/libc/mr_string.h>

/* 检查是否为 2 的幂 */
mr_bool_t is_power_of_two(mr_uint32_t x) {
    return (x > 0) && ((x & (x - 1)) == 0);
}

/* 计算以 2 为底的对数（向上取整） */
int log2_ceil(mr_uint32_t x) {
    int pos = mr_fls32(x);
    /* 如果 x 不是 2 的幂，需要 +1 */
    if (x & (x - 1)) {
        return pos;
    }
    return pos - 1;
}

/* 计算需要的字节数 */
mr_size_t bytes_needed(mr_uint32_t bits) {
    if (bits == 0) {
        return 0;
    }
    return (mr_fls32(bits) + 7) / 8;
}
```

---

## 3. 设计思路

### 3.1 设计目标

string 模块的设计目标：

1. **标准兼容**：接口与标准 C 库一致
2. **高效实现**：优化的位操作算法
3. **可移植性**：不依赖平台特定功能
4. **安全性**：提供受限版本的字符串函数

### 3.2 DeBruijn 算法

#### 为什么使用 DeBruijn 序列？

传统方法（循环查找）：

```c
/* O(n) 时间复杂度 */
int ffs32_slow(mr_uint32_t x) {
    if (x == 0) return 0;
    int pos = 1;
    while (!(x & 1)) {
        x >>= 1;
        pos++;
    }
    return pos;
}
```

DeBruijn 方法（O(1) 时间复杂度）：

```c
/* O(1) 时间复杂度 */
int ffs32_fast(mr_uint32_t x) {
    return debruijn32[((x & (-x)) * 0x077cb531U) >> 27];
}
```

#### 算法原理

1. **提取最低位**：`x & (-x)` 得到只有最低位 1 的值
2. **乘法散列**：乘以特定魔数，将唯一的位模式映射到唯一位置
3. **查表**：通过高位索引查找表得到结果

#### fls 的实现

```c
int mr_fls32(mr_uint32_t x) {
    if (x == 0) {
        return 0;
    }

    /* 将所有低位设置为 1 */
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;

    /* 只保留最高位 */
    x -= x >> 1;

    /* 使用 ffs 查找 */
    return mr_ffs32(x);
}
```

### 3.3 设计权衡

#### 为什么提供受限版本？

| 函数 | 非受限 | 受限 |
|------|--------|------|
| 复制 | `strcpy` | `strncpy` |
| 比较 | `strcmp` | `strncmp` |
| 长度 | `strlen` | `strnlen` |

**受限版本的优势**：

- 防止缓冲区溢出
- 限制最大操作长度
- 更安全的使用

**注意事项**：

- `strncpy` 不保证终止空字符
- 需要手动处理字符串终止

---

## 4. 参考资料

### 4.1 相关模块

string 模块被广泛使用：

- **sprintf**：格式化输出使用字符串操作
- **sscanf**：格式化输入使用字符串操作
- **所有模块**：字符串处理是基本需求

### 4.2 算法参考

- **DeBruijn 序列**：用于位查找的高效算法
- **字符串算法**：KMP、Boyer-Moore 等字符串搜索算法
- **内存操作**：memcpy 优化技术（SIMD、对齐等）

### 4.3 延伸阅读

- **C 标准库**：string.h 的完整规范
- **缓冲区溢出**：理解和防止缓冲区溢出
- **字符串处理**：安全字符串处理最佳实践
- **位操作技巧**：高效的位操作算法

# sprintf - 格式化输出

## 1. 模块概述

### 1.1 功能介绍

sprintf 模块提供格式化字符串输出功能，将格式化的数据写入缓冲区。它支持标准的 printf 风格格式说明符，类似于标准 C 库的 `sprintf` 系列函数。

该模块支持两种配置模式：STD 模式（使用标准库）和自定义模式（使用内置实现）。

### 1.2 主要特性

- **格式化输出**：支持 printf 风格的格式说明符
- **安全版本**：`snprintf` 限制输出大小，防止缓冲区溢出
- **变参支持**：支持可变参数列表
- **标准兼容**：接口与标准 C 库一致

### 1.3 适用场景

sprintf 模块适用于以下场景：

- **日志输出**：格式化日志消息
- **调试信息**：生成调试字符串
- **数据转换**：将数值转换为字符串
- **消息构建**：构造协议消息

---

## 2. 使用指南

### 2.1 接口说明

#### mr_snprintf() - 安全格式化输出

```c
int mr_snprintf(char *buf, mr_size_t size, const char *fmt, ...);
```

**功能**：格式化数据并写入缓冲区，限制最大写入字符数

**参数**：

- `buf`：目标缓冲区
- `size`：缓冲区大小
- `fmt`：格式字符串
- `...`：可变参数

**返回值**：

- 成功：返回写入的字符数（不包括终止空字符）
- 失败：返回负值

**特点**：

- 保证缓冲区不会溢出
- 当 size 为 0 时，只计算需要的字符数
- 总是添加终止空字符（如果 size > 0）

**示例**：

```c
#include <mrX/libc/mr_sprintf.h>

char buffer[64];
int len;

len = mr_snprintf(buffer, sizeof(buffer), "Value: %d", 42);
printf("Written: %d, Content: %s\n", len, buffer);
/* 输出: Written: 9, Content: Value: 42 */

/* 缓冲区不足的情况 */
len = mr_snprintf(buffer, 8, "Value: %d", 42);
printf("Written: %d, Content: %s\n", len, buffer);
/* 输出: Written: 9, Content: Value:  */
/* 注意：内容被截断，但返回值是完整的长度 */
```

#### mr_sprintf() - 格式化输出

```c
int mr_sprintf(char *buf, const char *fmt, ...);
```

**功能**：格式化数据并写入缓冲区（无大小限制）

**参数**：

- `buf`：目标缓冲区
- `fmt`：格式字符串
- `...`：可变参数

**返回值**：

- 成功：返回写入的字符数
- 失败：返回负值

**警告**：此函数不检查缓冲区大小，存在缓冲区溢出风险！

**示例**：

```c
char buffer[64];

mr_sprintf(buffer, "Value: %d, String: %s", 42, "hello");
printf("%s\n", buffer);
```

**建议**：优先使用 `mr_snprintf` 而非 `mr_sprintf`。

#### mr_vsnprintf() - 可变参数安全格式化

```c
int mr_vsnprintf(char *buf, mr_size_t size, const char *fmt, mr_va_list args);
```

**功能**：与 `mr_snprintf` 相同，但接受 `va_list` 参数

**参数**：

- `buf`：目标缓冲区
- `size`：缓冲区大小
- `fmt`：格式字符串
- `args`：可变参数列表

**返回值**：

- 成功：返回写入的字符数
- 失败：返回负值

**示例**：

```c
#include <mrX/libc/mr_sprintf.h>

void log_message(const char *level, const char *fmt, ...) {
    char buffer[128];
    mr_va_list args;
    int total_len;

    /* 写入日志级别 */
    int len = mr_snprintf(buffer, sizeof(buffer), "[%s] ", level);

    /* 添加格式化消息 */
    mr_va_start(args, fmt);
    total_len = len + mr_vsnprintf(buffer + len, sizeof(buffer) - len, fmt, args);
    mr_va_end(args);

    printf("%s\n", buffer);
}

/* 使用 */
log_message("INFO", "Temperature: %d C", 25);
```

#### mr_vsprintf() - 可变参数格式化

```c
int mr_vsprintf(char *buf, const char *fmt, mr_va_list args);
```

**功能**：与 `mr_sprintf` 相同，但接受 `va_list` 参数

**警告**：此函数不检查缓冲区大小！

### 2.2 格式说明符

sprintf 模块支持常用的 printf 格式说明符：

#### 整数格式

| 说明符 | 类型 | 示例 | 输出 |
|--------|------|------|------|
| `%d` | 有符号十进制 | `printf("%d", -42)` | `-42` |
| `%u` | 无符号十进制 | `printf("%u", 42)` | `42` |
| `%x` | 无符号十六进制（小写） | `printf("%x", 255)` | `ff` |
| `%X` | 无符号十六进制（大写） | `printf("%X", 255)` | `FF` |
| `%o` | 无符号八进制 | `printf("%o", 8)` | `10` |

#### 字符格式

| 说明符 | 类型 | 示例 | 输出 |
|--------|------|------|------|
| `%c` | 字符 | `printf("%c", 'A')` | `A` |
| `%s` | 字符串 | `printf("%s", "hello")` | `hello` |

#### 指针格式

| 说明符 | 类型 | 示例 | 输出 |
|--------|------|------|------|
| `%p` | 指针 | `printf("%p", ptr)` | `0x12345678` |

#### 宽度和精度

```c
/* 宽度 */
mr_snprintf(buf, size, "%5d", 42);      /* "   42" (右对齐，宽度5) */
mr_snprintf(buf, size, "%-5d", 42);     /* "42   " (左对齐，宽度5) */

/* 前导零 */
mr_snprintf(buf, size, "%05d", 42);     /* "00042" */

/* 精度（字符串） */
mr_snprintf(buf, size, "%.3s", "hello"); /* "hel" */

/* 精度（整数） */
mr_snprintf(buf, size, "%.3d", 42);     /* "042" */
```

#### 长度修饰符

```c
/* 短整型 */
mr_snprintf(buf, size, "%hd", (short)42);

/* 长整型 */
mr_snprintf(buf, size, "%ld", (long)42);

/* 长长整型 */
mr_snprintf(buf, size, "%lld", (long long)42);

/* size_t */
mr_snprintf(buf, size, "%zu", (size_t)42);

/* ptrdiff_t */
mr_snprintf(buf, size, "%td", (ptrdiff_t)42);
```

### 2.3 配置模式

#### STD 模式（标准库模式）

```c
#define MR_USE_SPRINTF_STD
```

**特点**：

- 直接使用标准库的 `<stdio.h>`
- 所有函数映射到标准函数

```c
#if defined(MR_USE_SPRINTF_STD)
#define mr_vsnprintf(_b, _s, _f, _a)    vsnprintf(_b, _s, _f, _a)
#define mr_vsprintf(_b, _f, _a)         vsprintf(_b, _f, _a)
#define mr_snprintf(_b, _s, _f, ...)    snprintf(_b, _s, _f, ##__VA_ARGS__)
#define mr_sprintf(_b, _f, ...)         sprintf(_b, _f, ##__VA_ARGS__)
#endif
```

#### 自定义模式

**特点**：

- 使用内置实现
- 支持常用格式说明符
- 适合裸机环境

### 2.4 典型应用案例

#### 案例 1：构建日志消息

```c
#include <mrX/libc/mr_sprintf.h>

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
} log_level_t;

void log_write(log_level_t level, const char *fmt, ...) {
    char buffer[256];
    mr_va_list args;
    const char *level_str;

    switch (level) {
        case LOG_DEBUG: level_str = "DEBUG"; break;
        case LOG_INFO:  level_str = "INFO "; break;
        case LOG_WARN:  level_str = "WARN "; break;
        case LOG_ERROR: level_str = "ERROR"; break;
        default:        level_str = "UNKNOWN"; break;
    }

    /* 格式化消息 */
    mr_va_start(args, fmt);
    mr_vsnprintf(buffer, sizeof(buffer), fmt, args);
    mr_va_end(args);

    /* 输出 */
    printf("[%s] %s\n", level_str, buffer);
}

/* 使用 */
log_write(LOG_INFO, "Temperature: %d C, Humidity: %d %%", 25, 60);
```

#### 案例 2：数据转十六进制字符串

```c
#include <mrX/libc/mr_sprintf.h>

char *bytes_to_hex(const mr_uint8_t *data, mr_size_t len, char *str, mr_size_t size) {
    mr_size_t pos = 0;

    for (mr_size_t i = 0; i < len && pos + 3 < size; i++) {
        pos += mr_snprintf(str + pos, size - pos, "%02x ", data[i]);
    }

    if (pos > 0) {
        str[pos - 1] = '\0';  /* 移除最后一个空格 */
    } else {
        str[0] = '\0';
    }

    return str;
}

/* 使用 */
mr_uint8_t mac_addr[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
char mac_str[18];

bytes_to_hex(mac_addr, 6, mac_str, sizeof(mac_str));
printf("MAC: %s\n", mac_str);
/* 输出: MAC: 00:11:22:33:44:55 (需要调整格式) */
```

#### 案例 3：状态报告

```c
#include <mrX/libc/mr_sprintf.h>

typedef struct {
    mr_uint32_t uptime;
    mr_uint32_t free_mem;
    mr_uint32_t total_mem;
    mr_uint32_t tasks;
    mr_uint32_t cpu_usage;
} system_status_t;

void print_status(const system_status_t *status) {
    char buffer[256];

    mr_snprintf(buffer, sizeof(buffer),
                "System Status:\n"
                "  Uptime: %u s\n"
                "  Memory: %u / %u KB\n"
                "  Tasks: %u\n"
                "  CPU: %u%%\n",
                status->uptime,
                (status->total_mem - status->free_mem) / 1024,
                status->total_mem / 1024,
                status->tasks,
                status->cpu_usage);

    printf("%s\n", buffer);
}
```

#### 案例 4：表格输出

```c
#include <mrX/libc/mr_sprintf.h>

void print_table_header(void) {
    printf("+------+--------+-------+\n");
    printf("| ID   | Name   | Value |\n");
    printf("+------+--------+-------+\n");
}

void print_table_row(int id, const char *name, int value) {
    char line[64];

    mr_snprintf(line, sizeof(line), "| %-4d | %-6s | %5d |", id, name, value);
    printf("%s\n", line);
}

void print_table_footer(void) {
    printf("+------+--------+-------+\n");
}

/* 使用 */
print_table_header();
print_table_row(1, "Temp", 25);
print_table_row(2, "Humid", 60);
print_table_footer();
```

#### 案例 5：安全的字符串构建

```c
#include <mrX/libc/mr_sprintf.h>

typedef struct {
    char data[256];
    mr_size_t pos;
    mr_size_t size;
} str_builder_t;

void str_builder_init(str_builder_t *b, char *buffer, mr_size_t size) {
    b->data = buffer;
    b->size = size;
    b->pos = 0;
    buffer[0] = '\0';
}

mr_err_t str_builder_append(str_builder_t *b, const char *fmt, ...) {
    mr_va_list args;
    int len;

    if (b->pos >= b->size) {
        return -MR_ENOMEM;
    }

    mr_va_start(args, fmt);
    len = mr_vsnprintf(b->data + b->pos, b->size - b->pos, fmt, args);
    mr_va_end(args);

    if (len < 0) {
        return -MR_EIO;
    }

    b->pos += len;
    return 0;
}

/* 使用 */
char buffer[256];
str_builder_t builder;

str_builder_init(&builder, buffer, sizeof(buffer));
str_builder_append(&builder, "Request: %s ", "GET");
str_builder_append(&builder, "path=%s", "/api/data");

printf("%s\n", buffer);
```

---

## 3. 设计思路

### 3.1 设计目标

sprintf 模块的设计目标：

1. **标准兼容**：与 printf 接口一致
2. **安全性**：提供 snprintf 防止缓冲区溢出
3. **可移植性**：不依赖复杂的标准库
4. **轻量级**：支持常用格式说明符

### 3.2 格式化处理流程

```
格式字符串解析
    ↓
扫描格式说明符 (%)
    ↓
读取参数
    ↓
格式化转换
    ↓
写入缓冲区
```

### 3.3 设计权衡

#### 为什么优先使用 snprintf？

| 特性 | sprintf | snprintf |
|------|---------|----------|
| 缓冲区检查 | 无 | 有 |
| 安全性 | 危险 | 安全 |
| 性能 | 稍快 | 稍慢 |
| 推荐 | 否 | 是 |

**示例风险**：

```c
/* 危险！可能缓冲区溢出 */
char small[8];
mr_sprintf(small, "Value: %d", 12345678);

/* 安全 */
char small[8];
mr_snprintf(small, sizeof(small), "Value: %d", 12345678);
```

#### 为什么需要 v* 版本？

1. **封装函数**：在自定义函数中使用可变参数
2. **参数转发**：将参数转发给其他函数
3. **日志系统**：实现分层日志记录

---

## 4. 参考资料

### 4.1 相关模块

sprintf 模块与以下模块相关：

- **printf**：最终输出函数
- **sscanf**：格式化输入（反向操作）
- **string**：字符串操作函数

### 4.2 相关标准

- **ISO C**：stdio.h 中 printf 系列函数的规范
- **POSIX**：扩展的格式说明符

### 4.3 延伸阅读

- **Printf 格式说明符**：完整的格式说明符参考
- **缓冲区安全**：防止缓冲区溢出的最佳实践
- **可变参数**：va_list 和可变参数函数的使用

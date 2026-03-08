# printf - 格式化输出

## 1. 模块概述

### 1.1 功能介绍

printf 模块实现了一个轻量级的格式化输出系统。它提供类似标准 C 库 printf 的功能，将格式化字符串输出到系统控制台或指定设备。

printf 模块的核心功能：

- **格式化输出**：支持 printf 风格的格式化字符串
- **可变参数**：使用 va_list 处理可变参数
- **移植接口**：通过 `mr_port_output` 实现平台特定的输出
- **ANSI 颜色**：可选支持终端颜色输出
- **安全模式**：可选使用动态分配替代静态缓冲区

### 1.2 主要特性

- **分层设计**：printf → sprintf → 输出端口，职责清晰
- **缓冲机制**：使用固定大小缓冲区（默认 64 字节）分批输出
- **可选安全模式**：使用 `MR_USE_PRINTF_SAFE` 启用动态分配
- **颜色支持**：通过 `MR_USE_PRINTF_COLOR` 启用 ANSI 颜色
- **可配置缓冲区**：通过 `MR_CFG_PRINTF_BUF_SIZE` 调整缓冲区大小

### 1.3 适用场景

printf 模块适用于以下场景：

- **系统调试**：输出调试信息和系统状态
- **日志输出**：作为 log 模块的底层输出
- **用户交互**：向用户显示信息
- **开发测试**：快速验证代码逻辑

**不适用场景**：

- **高频输出**：每次调用有缓冲开销，不适合极高频输出
- **二进制数据**：只适合文本输出，二进制数据应使用专用接口
- **大文本输出**：受缓冲区大小限制，大文本会分多次输出

### 1.4 使用注意事项

⚠️ **重要警告**：

1. **必须实现 mr_port_output**：使用前必须实现平台特定的输出函数

2. **缓冲区大小**：默认 64 字节，大格式化结果会被截断（但 snprintf 保证不溢出）

3. **非线程安全**：使用静态缓冲区时，多线程调用需要外部同步

4. **颜色支持**：ANSI 颜色需要终端支持，裸机串口通常不支持

5. **返回值**：成功返回字符数，失败返回负错误码

---

## 2. 使用指南

### 2.1 核心接口说明

#### 格式化输出函数

##### mr_printf()

```c
int mr_printf(const char *fmt, ...);
```

**功能**：输出格式化字符串到系统输出设备

**参数**：

- `fmt`：printf 风格的格式化字符串
- `...`：可变参数

**返回值**：

- `>= 0`：成功，返回输出的字符数
- `< 0`：失败，返回错误码（`-MR_EINVAL`、`-MR_ENOMEM`）

**示例**：

```c
#include <mrX/kernel/mr_printf.h>

void example(void) {
    int count = mr_printf("Hello, World!\n");
    mr_printf("Temperature: %d.%d C\n", 25, 4);
    mr_printf("Status: %s\n", "OK");
}
```

##### mr_vprintf()

```c
int mr_vprintf(const char *fmt, mr_va_list args);
```

**功能**：输出格式化字符串（va_list 版本）

**参数**：

- `fmt`：printf 风格的格式化字符串
- `args`：可变参数列表

**返回值**：

- `>= 0`：成功，返回输出的字符数
- `< 0`：失败，返回错误码

**用途**：用于包装 printf 函数，实现自定义日志函数

**示例**：

```c
#include <mrX/kernel/mr_printf.h>

void my_log(const char *fmt, ...) {
    mr_va_list args;

    mr_va_start(args, fmt);
    mr_vprintf(fmt, args);
    mr_va_end(args);
}

/* 调用 */
my_log("Error: %d\n", error_code);
```

#### ANSI 颜色宏（可选）

##### MR_COLOR_RED / GREEN / YELLOW / BLUE / BLACK

```c
#define MR_COLOR_RED          "\033[31m"
#define MR_COLOR_GREEN        "\033[32m"
#define MR_COLOR_YELLOW       "\033[33m"
#define MR_COLOR_BLUE         "\033[34m"
#define MR_COLOR_BLACK        "\033[30m"
#define MR_COLOR_RESET        "\033[0m"
```

**功能**：ANSI 转义序列，用于终端颜色控制

**使用限制**：

- 需要定义 `MR_USE_PRINTF_COLOR`
- 需要终端支持 ANSI 转义序列
- 裸机串口通常不支持

##### MR_COLOR()

```c
#define MR_COLOR(_color, _str)    _color _str MR_COLOR_RESET
```

**功能**：为字符串添加颜色并自动重置

**参数**：

- `_color`：颜色宏（如 `MR_COLOR_RED`）
- `_str`：字符串

**示例**：

```c
#if defined(MR_USE_PRINTF_COLOR)
    mr_printf(MR_COLOR(MR_COLOR_RED, "Error: ") "%s\n", error_msg);
    mr_printf(MR_COLOR(MR_COLOR_GREEN, "Success: ") "Operation completed\n");
    mr_printf(MR_COLOR(MR_COLOR_YELLOW, "Warning: ") "%s\n", warning_msg);
#endif
```

**未定义颜色时**：

```c
/* 未定义 MR_USE_PRINTF_COLOR 时，宏展开为原字符串 */
mr_printf(MR_COLOR(MR_COLOR_RED, "Error"));  /* 展开为 mr_printf("Error") */
```

### 2.2 格式化字符串说明

printf 模块使用 sprintf 模块进行格式化，支持的格式说明符取决于 sprintf 的实现。

**常用格式说明符**：

| 说明符  | 类型       | 示例                         | 输出           |
|------|----------|----------------------------|--------------|
| `%d` | 有符号整数    | `mr_printf("%d", -42)`     | `-42`        |
| `%u` | 无符号整数    | `mr_printf("%u", 42)`      | `42`         |
| `%x` | 十六进制（小写） | `mr_printf("%x", 255)`     | `ff`         |
| `%X` | 十六进制（大写） | `mr_printf("%X", 255)`     | `FF`         |
| `%p` | 指针地址     | `mr_printf("%p", ptr)`     | `0x1234abcd` |
| `%s` | 字符串      | `mr_printf("%s", "Hello")` | `Hello`      |
| `%c` | 字符       | `mr_printf("%c", 'A')`     | `A`          |
| `%%` | 百分号      | `mr_printf("100%%")`       | `100%`       |

**宽度与精度**：

```c
mr_printf("[%5d]", 42);      /* [   42] 右对齐，宽度 5 */
mr_printf("[%-5d]", 42);     /* [42   ] 左对齐，宽度 5 */
mr_printf("[%05d]", 42);     /* [00042] 补零 */
mr_printf("%.2f", 3.14159);  /* 3.14 精度 */
```

### 2.3 典型应用案例

#### 案例 1：基础用法 - 系统状态输出

```c
#include <mrX/kernel/mr_printf.h>

void system_status_print(void) {
    mr_printf("=== System Status ===\n");
    mr_printf("CPU Load:   %d%%\n", cpu_load);
    mr_printf("Memory:     %u / %u bytes\n", used_mem, total_mem);
    mr_printf("Uptime:     %llu seconds\n", uptime);
    mr_printf("====================\n");
}
```

#### 案例 2：错误信息输出

```c
#include <mrX/kernel/mr_printf.h>

void error_report(int error_code, const char *file, int line) {
#if defined(MR_USE_PRINTF_COLOR)
    mr_printf(MR_COLOR(MR_COLOR_RED, "[ERROR] "));
#else
    mr_printf("[ERROR] ");
#endif
    mr_printf("Code %d at %s:%d\n", error_code, file, line);
}

/* 使用宏简化 */
#define ERROR_LOG(_code, _msg) \
    mr_printf("[ERROR] %s: %s (code: %d)\n", __func__, _msg, _code)
```

#### 案例 3：调试输出 - 十六进制转储

```c
#include <mrX/kernel/mr_printf.h>

void hex_dump(const uint8_t *data, size_t len) {
    size_t i;

    for (i = 0; i < len; i++) {
        mr_printf("%02X ", data[i]);
        if ((i & 0x0F) == 0x0F) {
            mr_printf("\n");
        }
    }
    mr_printf("\n");
}

/* 输出示例 */
/* 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 */
/* 11 12 13 14 ... */
```

#### 案例 4：自定义日志函数

```c
#include <mrX/kernel/mr_printf.h>

/* 自定义日志头 */
void my_log(const char *level, const char *tag, const char *fmt, ...) {
    mr_va_list args;

    /* 输出日志头 */
    mr_printf("[%s][%s] ", level, tag);

    /* 输出格式化内容 */
    mr_va_start(args, fmt);
    mr_vprintf(fmt, args);
    mr_va_end(args);

    /* 换行 */
    mr_printf("\n");
}

/* 使用 */
#define LOG_ERROR(tag, ...)   my_log("ERROR", tag, __VA_ARGS__)
#define LOG_INFO(tag, ...)    my_log("INFO", tag, __VA_ARGS__)

/* 调用 */
LOG_INFO("Main", "System initialized, version %s", "1.0.0");
LOG_ERROR("UART", "Failed to send: %d", error);
```

#### 案例 5：表格化输出

```c
#include <mrX/kernel/mr_printf.h>

void task_list_print(void) {
    mr_printf("+----+----------+-------+-------+\n");
    mr_printf("| ID | Name     | Stack | Prio  |\n");
    mr_printf("+----+----------+-------+-------+\n");

    for (int i = 0; i < task_count; i++) {
        mr_printf("| %2d | %-8s | %5u | %5u |\n",
                  tasks[i].id,
                  tasks[i].name,
                  tasks[i].stack_size,
                  tasks[i].priority);
    }

    mr_printf("+----+----------+-------+-------+\n");
}

/* 输出：
+----+----------+-------+-------+
| ID | Name     | Stack | Prio  |
+----+----------+-------+-------+
|  0 | Idle     |   256 |     0 |
|  1 | Main     |  1024 |    10 |
|  2 | UART     |   512 |     5 |
+----+----------+-------+-------+
*/
```

#### 案例 6：进度条显示

```c
#include <mrX/kernel/mr_printf.h>

void progress_bar(int current, int total) {
    int percent = (current * 100) / total;
    int bars = (current * 40) / total;
    int i;

    mr_printf("\r[");  /* \r 回到行首 */

    for (i = 0; i < bars; i++) {
        mr_printf("=");
    }
    for (i = bars; i < 40; i++) {
        mr_printf(" ");
    }

    mr_printf("] %d%% (%d/%d)", percent, current, total);
}

/* 输出：
[====================                    ] 50% (500/1000)
*/
```

#### 案例 7：正确使用示例与常见陷阱

```c
#include <mrX/kernel/mr_printf.h>

/* ✓ 正确：检查返回值 */
static int safe_print(const char *fmt, ...) {
    mr_va_list args;
    int ret;

    mr_va_start(args, fmt);
    ret = mr_vprintf(fmt, args);
    mr_va_end(args);

    if (ret < 0) {
        /* 处理错误 */
        return -1;
    }
    return ret;
}

/* ✓ 正确：使用指针格式化 */
static void print_pointer(void) {
    void *ptr = (void *)0x12345678;
    mr_printf("Pointer: %p\n", ptr);  /* 输出: Pointer: 0x12345678 */
}

/* ✓ 正确：字符串安全 */
static void print_string_safe(const char *str) {
    if (str) {
        mr_printf("String: %s\n", str);
    } else {
        mr_printf("String: (null)\n");
    }
}

/* ✗ 错误：格式化字符串和参数不匹配 */
static void wrong_format_mismatch(void) {
    int value = 42;
    /* 错误：%s 期望字符串，传入了整数 */
    /* mr_printf("Value: %s\n", value); */  /* 可能崩溃 */
}

/* ✓ 正确：类型匹配 */
static void correct_format_match(void) {
    int value = 42;
    mr_printf("Value: %d\n", value);  /* 正确 */
}

/* ✗ 错误：格式化字符串非字面量（安全风险） */
static void wrong_format_non_literal(const char *user_input) {
    /* 危险：用户可能包含 %n 等危险格式 */
    /* mr_printf(user_input); */
}

/* ✓ 正确：使用 %s 输出用户输入 */
static void correct_format_literal(const char *user_input) {
    mr_printf("%s", user_input);
}

/* ✓ 正确：处理缓冲区限制 */
static void handle_large_output(void) {
    /* 默认缓冲区 64 字节，长字符串会被分批输出 */
    /* sprintf 保证不会溢出，但长字符串需要多次调用 */
    mr_printf("This is a very long string that will be output "
              "in multiple chunks depending on buffer size\n");
}

/* ✗ 错误：在中断中使用 printf（可能重入） */
void irq_handler(void) {
    /* 错误：中断处理程序不应调用可能阻塞的函数 */
    /* mr_printf("IRQ occurred\n"); */
}

/* ✓ 正确：设置标志位，延后输出 */
void irq_handler_safe(void) {
    irq_flag = 1;  /* 设置标志 */
}

/* 在主循环中处理 */
void main_loop(void) {
    if (irq_flag) {
        mr_printf("IRQ was handled\n");
        irq_flag = 0;
    }
}

/* ✓ 正确：条件颜色使用 */
static void conditional_color(int error) {
#if defined(MR_USE_PRINTF_COLOR)
    if (error) {
        mr_printf(MR_COLOR(MR_COLOR_RED, "Failed\n"));
    } else {
        mr_printf(MR_COLOR(MR_COLOR_GREEN, "Success\n"));
    }
#else
    mr_printf(error ? "Failed\n" : "Success\n");
#endif
}

/* ✓ 正确：宏定义日志 */
#define LOG(fmt, ...) \
    mr_printf("[LOG][%s:%d] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)

static void use_log_macro(void) {
    LOG("Value: %d", 42);
    /* 输出: [LOG:function_name:123] Value: 42 */
}
```

---

## 3. 设计思路

### 3.1 设计目标

printf 模块的设计目标：

1. **轻量级**：最小化代码和内存开销
2. **可移植性**：通过移植接口适配不同平台
3. **可配置**：缓冲区大小、安全模式、颜色支持均可配置
4. **分层设计**：依赖 sprintf 进行格式化，职责单一
5. **兼容性**：提供类似标准库的接口

### 3.2 分层架构

```
┌─────────────────────────────────────┐
│         应用层（用户代码）            │
│  mr_printf("Value: %d", value);     │
└─────────────────┬───────────────────┘
                  │
┌─────────────────▼───────────────────┐
│       printf 模块（格式化调用）        │
│   解析可变参数 → 调用 vsnprintf      │
└─────────────────┬───────────────────┘
                  │
┌─────────────────▼───────────────────┐
│       sprintf 模块（字符串格式化）      │
│   将格式化结果写入缓冲区              │
└─────────────────┬───────────────────┘
                  │
┌─────────────────▼───────────────────┐
│   port_output（平台输出接口）         │
│   平台实现：UART、SWD、JTAG 等       │
└─────────────────────────────────────┘
```

### 3.3 缓冲机制

printf 模块使用固定大小的缓冲区来减少平台输出接口的调用次数。

**默认模式**（不安全但高效）：

```c
int mr_vprintf(const char *fmt, mr_va_list args) {
    static char buf[64];  /* 静态缓冲区 */

    /* 格式化到缓冲区 */
    ret = mr_vsnprintf(buf, sizeof(buf), fmt, args);

    /* 输出 */
    mr_port_output(buf, ret);

    return ret;
}
```

**安全模式**（使用动态分配）：

```c
#if defined(MR_USE_PRINTF_SAFE)
int mr_vprintf(const char *fmt, mr_va_list args) {
    char *buf;

    /* 动态分配缓冲区 */
    buf = mr_malloc(MR_CFG_PRINTF_BUF_SIZE);
    if (!buf) {
        return -MR_ENOMEM;
    }

    /* 格式化到缓冲区 */
    ret = mr_vsnprintf(buf, MR_CFG_PRINTF_BUF_SIZE, fmt, args);

    /* 输出 */
    mr_port_output(buf, ret);

    /* 释放缓冲区 */
    mr_free(buf);
    return ret;
}
#endif
```

**缓冲区大小配置**：

```c
/* 在配置中定义缓冲区大小 */
#define MR_CFG_PRINTF_BUF_SIZE    128  /* 默认为 64 */
```

### 3.4 ANSI 颜色实现

ANSI 颜色通过转义序列实现，只在定义 `MR_USE_PRINTF_COLOR` 时生效。

**实现原理**：

```c
#if defined(MR_USE_PRINTF_COLOR)
    #define MR_COLOR_RESET    "\033[0m"
    #define MR_COLOR_RED      "\033[31m"
    #define MR_COLOR(_c, _s)  _c _s MR_COLOR_RESET
#else
    #define MR_COLOR_RESET
    #define MR_COLOR_RED
    #define MR_COLOR(_c, _s)  _s  /* 不支持颜色时返回原字符串 */
#endif
```

**使用方式**：

```c
/* 有颜色支持 */
mr_printf(MR_COLOR(MR_COLOR_RED, "Error: "));
/* 展开为: mr_printf("\033[31m" "Error: " "\033[0m"); */

/* 无颜色支持 */
mr_printf(MR_COLOR(MR_COLOR_RED, "Error: "));
/* 展开为: mr_printf("Error: "); */
```

### 3.5 与标准 printf 的对比

| 特性   | mr_printf  | 标准 printf |
|------|------------|-----------|
| 输出目标 | 平台移植接口     | stdout    |
| 格式化  | 依赖 sprintf | 内置        |
| 缓冲   | 固定缓冲区      | 通常缓冲      |
| 颜色支持 | 可选         | 无         |
| 线程安全 | 否          | 取决于实现     |
| 错误返回 | 错误码        | 负数        |

### 3.6 移植接口

printf 模块需要实现一个平台特定的输出函数：

```c
/**
 * @brief 平台输出函数（需要平台实现）
 * @param buf 要输出的数据
 * @param size 数据大小
 * @return 成功输出的字节数，失败返回负值
 */
int mr_port_output(const char *buf, mr_size_t size);
```

**UART 移植示例**：

```c
int mr_port_output(const char *buf, mr_size_t size) {
    mr_size_t i;

    for (i = 0; i < size; i++) {
        UART1->DR = buf[i];  /* 写入 UART 数据寄存器 */
        while (!(UART1->SR & UART_SR_TXE)) {
            /* 等待发送完成 */
        }
    }
    return (int)size;
}
```

**调试器移植示例**（通过 ITM / SWO）：

```c
int mr_port_output(const char *buf, mr_size_t size) {
    /* 通过 ITM 发送到调试器 */
    for (mr_size_t i = 0; i < size; i++) {
        ITM_SendChar(buf[i]);
    }
    return (int)size;
}
```

---

## 4. 参考资料

### 4.1 相关模块

- **sprintf**：字符串格式化模块，printf 的底层依赖
- **log**：日志模块，使用 printf 作为输出
- **port_output**：平台输出接口，需要平台实现

### 4.2 设计参考

- **C 标准库 printf**：ISO C 标准定义的格式化输出函数
- **ANSI 转义序列**：终端控制代码标准
- **嵌入式 printf 实现**：各种嵌入式系统的 printf 实现

### 4.3 延伸阅读

- **格式化字符串漏洞**：格式化字符串安全问题
- **printf 性能优化**：减少 printf 开销的方法
- **嵌入式调试技术**：使用 printf 进行嵌入式调试

# log - 日志系统

## 1. 模块概述

### 1.1 功能介绍

log 模块实现了一个轻量级的分级日志系统。它允许系统在运行时输出带有时间戳和日志级别的调试信息，同时支持编译时和运行时的日志过滤。

日志系统的核心功能：

- **分级输出**：支持 5 个日志级别（ERR、WRN、INF、DBG、TRC）
- **时间戳**：自动为每条日志添加时间戳（支持高分辨率时间或运行时间）
- **标签过滤**：通过 `MR_LOG_TAG` 标签和位图掩码实现灵活的日志过滤
- **编译优化**：未启用的日志级别在编译时被完全移除
- **格式化输出**：支持 printf 风格的格式化字符串

### 1.2 主要特性

- **零开销过滤**：未启用的日志级别在编译时展开为空语句
- **运行时控制**：可通过掩码动态启用/禁用特定级别的日志
- **自动时间戳**：每条日志自动附加高精度时间戳或运行时间
- **标签支持**：通过 `MR_LOG_TAG` 区分不同模块的日志
- **模块化设计**：依赖 printf 和 clock 模块，可选依赖高分辨率时钟

### 1.3 适用场景

log 模块适用于以下场景：

- **开发调试**：输出详细的调试信息跟踪程序执行
- **错误诊断**：记录系统运行中的错误和警告
- **状态监控**：输出关键状态变化信息
- **性能分析**：通过时间戳分析代码执行时序
- **生产环境**：通过配置只输出错误和警告信息

**不适用场景**：

- **高频数据记录**：日志系统不适合高频数据采集（考虑使用专用数据记录模块）
- **大容量存储**：日志直接输出，不提供本地缓存和存储功能
- **复杂过滤**：只支持级别过滤，不支持模块或内容过滤

### 1.4 使用注意事项

⚠️ **重要警告**：

1. **MR_LOG_TAG 必须在 include 之前定义**：必须先 `#define MR_LOG_TAG`，再 `#include <mrX/kernel/mr_log.h>`，否则所有日志宏展开为空语句
   `((void)0)`

2. **级别控制范围**：编译时配置 `MR_CFG_LOG_LEVEL` 控制哪些级别可用（默认为 TRC）

3. **时间戳依赖**：日志系统依赖 clock 模块，确保时钟已初始化

4. **输出依赖**：日志最终通过 printf 模块输出，需要正确配置输出端口

5. **可重入性**：日志函数不是线程安全的，在多线程环境中需要外部同步

---

## 2. 使用指南

### 2.1 核心数据结构

#### 日志级别

```c
#define MR_LOG_LEVEL_ERR    (0)    /* 错误级别 */
#define MR_LOG_LEVEL_WRN    (1)    /* 警告级别 */
#define MR_LOG_LEVEL_INF    (2)    /* 信息级别 */
#define MR_LOG_LEVEL_DBG    (3)    /* 调试级别 */
#define MR_LOG_LEVEL_TRC    (4)    /* 跟踪级别 */
```

#### 日志级别掩码

```c
#define MR_LOG_MASK(_level)     (1U << (_level))
```

**掩码示例**：

```c
/* 只启用 ERR 和 WRN */
mr_log_mask_set(MR_LOG_MASK(MR_LOG_LEVEL_ERR) | MR_LOG_MASK(MR_LOG_LEVEL_WRN));

/* 启用所有级别 */
mr_log_mask_set(0xFFFFFFFFU);

/* 只启用 INF */
mr_log_mask_set(MR_LOG_MASK(MR_LOG_LEVEL_INF));
```

### 2.2 主要接口说明

#### 日志输出宏

##### MR_LOG_TRC / MR_LOG_DBG / MR_LOG_INF / MR_LOG_WRN / MR_LOG_ERR

```c
#define MR_LOG_TRC(_fmt, ...)
#define MR_LOG_DBG(_fmt, ...)
#define MR_LOG_INF(_fmt, ...)
#define MR_LOG_WRN(_fmt, ...)
#define MR_LOG_ERR(_fmt, ...)
```

**功能**：输出对应级别的日志

**参数**：

- `_fmt`：printf 风格的格式化字符串
- `...`：可变参数

**输出格式**：

```
[时间戳] [级别/标签] 格式化内容\n
```

**编译控制**：

- 必须在 include 之前定义 `MR_LOG_TAG`
- 级别必须小于等于 `MR_CFG_LOG_LEVEL`
- 否则宏展开为 `((void)0)`

**示例**：

```c
#define MR_LOG_TAG    "MyApp"
#include <mrX/kernel/mr_log.h>

void function(void) {
    MR_LOG_INF("System initialized");
    MR_LOG_DBG("Value: %d", 42);
    MR_LOG_ERR("Failed to open file: %s", filename);
}
```

**输出效果**（高分辨率时钟模式）：

```
[2024-09-06-14:30:25:123:456] [INF/MyApp] System initialized
[2024-09-06-14:30:25:127:789] [DBG/MyApp] Value: 42
[2024-09-06-14:30:25:200:100] [ERR/MyApp] Failed to open file: config.txt
```

**输出效果**（运行时间模式）：

```
[      0.123] [INF/MyApp] System initialized
[      0.127] [DBG/MyApp] Value: 42
[      0.200] [ERR/MyApp] Failed to open file: config.txt
```

#### 日志输出函数

##### mr_log()

```c
void mr_log(mr_uint32_t level, const char *fmt, ...);
```

**功能**：输出指定级别的日志（底层函数）

**参数**：

- `level`：日志级别（0-4）
- `fmt`：格式化字符串
- `...`：可变参数

**注意事项**：

- 不包含级别标签和换行符，需手动添加
- 直接输出，不受宏级别过滤控制
- 仍受运行时掩码控制

**示例**：

```c
/* 等价于 MR_LOG_INF("Value: %d", 42) */
mr_log(MR_LOG_LEVEL_INF, " [INF/MyApp] Value: %d\n", 42);
```

#### 运行时控制接口

##### mr_log_mask_set()

```c
void mr_log_mask_set(mr_uint32_t mask);
```

**功能**：设置日志级别掩码

**参数**：

- `mask`：32 位掩码，每一位对应一个级别

**示例**：

```c
/* 只启用 ERR */
mr_log_mask_set(MR_LOG_MASK(MR_LOG_LEVEL_ERR));

/* 启用 ERR, WRN, INF */
mr_log_mask_set(MR_LOG_MASK(MR_LOG_LEVEL_ERR) |
                MR_LOG_MASK(MR_LOG_LEVEL_WRN) |
                MR_LOG_MASK(MR_LOG_LEVEL_INF));
```

##### mr_log_mask()

```c
mr_uint32_t mr_log_mask(void);
```

**功能**：获取当前日志级别掩码

**返回值**：当前的 32 位掩码

### 2.3 典型应用案例

#### 案例 1：基础用法 - 模块日志输出

```c
/* 必须先定义标签，再包含头文件，否则日志宏无效 */
#define MR_LOG_TAG    "GPIO"
#include <mrX/kernel/mr_log.h>

void gpio_init(void) {
    MR_LOG_INF("GPIO module initialized");
}

void gpio_set(int pin, int value) {
    MR_LOG_DBG("Set pin %d to %d", pin, value);

    /* ... 实际操作 ... */
}

void gpio_error_handler(int error) {
    MR_LOG_ERR("GPIO error occurred: %d", error);
}
```

#### 案例 2：运行时动态控制日志级别

```c
#define MR_LOG_TAG    "Main"
#include <mrX/kernel/mr_log.h>

/* 日志级别配置 */
typedef enum {
    LOG_LEVEL_SILENT,    /* 静默 */
    LOG_LEVEL_ERROR,     /* 仅错误 */
    LOG_LEVEL_WARN,      /* 错误 + 警告 */
    LOG_LEVEL_INFO,      /* 错误 + 警告 + 信息 */
    LOG_LEVEL_DEBUG,     /* 错误 + 警告 + 信息 + 调试 */
    LOG_LEVEL_TRACE,     /* 全部 */
} log_level_config_t;

/* 设置日志级别 */
void log_level_set(log_level_config_t level) {
    switch (level) {
        case LOG_LEVEL_SILENT:
            mr_log_mask_set(0);
            break;
        case LOG_LEVEL_ERROR:
            mr_log_mask_set(MR_LOG_MASK(MR_LOG_LEVEL_ERR));
            break;
        case LOG_LEVEL_WARN:
            mr_log_mask_set(MR_LOG_MASK(MR_LOG_LEVEL_ERR) |
                            MR_LOG_MASK(MR_LOG_LEVEL_WRN));
            break;
        case LOG_LEVEL_INFO:
            mr_log_mask_set(MR_LOG_MASK(MR_LOG_LEVEL_ERR) |
                            MR_LOG_MASK(MR_LOG_LEVEL_WRN) |
                            MR_LOG_MASK(MR_LOG_LEVEL_INF));
            break;
        case LOG_LEVEL_DEBUG:
            mr_log_mask_set(MR_LOG_MASK(MR_LOG_LEVEL_ERR) |
                            MR_LOG_MASK(MR_LOG_LEVEL_WRN) |
                            MR_LOG_MASK(MR_LOG_LEVEL_INF) |
                            MR_LOG_MASK(MR_LOG_LEVEL_DBG));
            break;
        case LOG_LEVEL_TRACE:
            mr_log_mask_set(0xFFFFFFFFU);
            break;
    }
}

/* 使用示例 */
void app_main(void) {
    /* 开发阶段：启用全部日志 */
    log_level_set(LOG_LEVEL_TRACE);

    MR_LOG_INF("Application started in debug mode");

    /* ... 运行中 ... */

    /* 生产阶段：只输出错误和警告 */
    log_level_set(LOG_LEVEL_WARN);
}
```

#### 案例 3：条件日志输出

```c
#define MR_LOG_TAG    "UART"
#include <mrX/kernel/mr_log.h>

/* 带检查的日志输出 */
mr_err_t uart_send(const uint8_t *data, size_t len) {
    if (!data) {
        MR_LOG_ERR("Invalid data pointer");
        return -MR_EINVAL;
    }

    if (len == 0) {
        MR_LOG_WRN("Zero length send request");
        return -MR_EINVAL;
    }

    MR_LOG_DBG("Sending %u bytes", (unsigned int)len);

    /* ... 实际发送操作 ... */

    MR_LOG_INF("Sent %u bytes successfully", (unsigned int)len);
    return 0;
}
```

#### 案例 4：函数跟踪日志

```c
#define MR_LOG_TAG    "StateMachine"
#include <mrX/kernel/mr_log.h>

/* 状态机跟踪 */
typedef enum {
    STATE_IDLE,
    STATE_RUNNING,
    STATE_PAUSED,
    STATE_ERROR,
} state_t;

void state_transition(state_t new_state) {
    state_t old_state = current_state;

    MR_LOG_TRC("State transition: %d -> %d", old_state, new_state);

    current_state = new_state;

    if (new_state == STATE_ERROR) {
        MR_LOG_ERR("State machine entered error state from %d", old_state);
    }
}

/* 函数入口/出口跟踪 */
void critical_function(void) {
    MR_LOG_TRC("Enter critical_function");

    /* ... 函数实现 ... */

    MR_LOG_TRC("Exit critical_function");
}
```

#### 案例 5：编译时级别配置

```c
/* 在配置文件或命令行定义 */

/* 方式 1：全部启用（开发调试） */
#define MR_CFG_LOG_LEVEL    MR_LOG_LEVEL_TRC

/* 方式 2：启用到 INF 级别（常规发布） */
#define MR_CFG_LOG_LEVEL    MR_LOG_LEVEL_INF

/* 方式 3：只启用错误（生产环境） */
#define MR_CFG_LOG_LEVEL    MR_LOG_LEVEL_ERR

/* 方式 4：完全禁用（空间受限） */
/* 不定义 MR_USE_LOG */
```

**代码中的使用**：

```c
#define MR_LOG_TAG    "App"
#include <mrX/kernel/mr_log.h>

void application(void) {
    /* 这行只在 MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_INF 时编译进代码 */
    MR_LOG_INF("Application starting");

    /* 这行只在 MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_DBG 时编译进代码 */
    MR_LOG_DBG("Debug info: %d", value);

    /* 如果 MR_CFG_LOG_LEVEL = MR_LOG_LEVEL_INF，
       MR_LOG_DBG 被编译为 ((void)0)，不产生任何代码 */
}
```

#### 案例 6：多模块日志管理

```c
/* 模块 A */
#define MR_LOG_TAG    "ModuleA"
#include <mrX/kernel/mr_log.h>

void module_a_init(void) {
    MR_LOG_INF("Module A initialized");
}

/* 模块 B */
/* 注意：每个模块需要单独定义标签 */
#undef MR_LOG_TAG
#define MR_LOG_TAG    "ModuleB"
#include <mrX/kernel/mr_log.h>

void module_b_init(void) {
    MR_LOG_INF("Module B initialized");
}

/* 主程序 */
#define MR_LOG_TAG    "Main"
#include <mrX/kernel/mr_log.h>

int main(void) {
    MR_LOG_INF("System boot");

    module_a_init();  /* 输出: [timestamp] [INF/ModuleA] Module A initialized */
    module_b_init();  /* 输出: [timestamp] [INF/ModuleB] Module B initialized */

    MR_LOG_INF("System ready");

    return 0;
}
```

#### 案例 7：正确使用示例与常见陷阱

```c
/* ✓ 正确：先定义标签，再包含头文件 */
#define MR_LOG_TAG    "Correct"
#include <mrX/kernel/mr_log.h>

static void correct_usage(void) {
    int value = 42;

    /* ✓ 正确：使用正确的格式化字符串 */
    MR_LOG_INF("Value is %d", value);

    /* ✓ 正确：字符串字面量 */
    MR_LOG_DBG("Debug message");

    /* ✓ 正确：多个参数 */
    MR_LOG_DBG("Coords: x=%d, y=%d", x, y);
}

/* ✗ 错误：未定义标签，include 在 define 之前 */
#include <mrX/kernel/mr_log.h>
/* #define MR_LOG_TAG    "Missing" */  /* 如果在这里定义，已经太晚了！ */

static void wrong_no_tag(void) {
    /* 所有日志宏都被展开为 ((void)0)，不会产生任何输出 */
    MR_LOG_INF("This will never be printed");
}

/* ✓ 正确：在不同文件中使用不同标签 */
/* file1.c */
#define MR_LOG_TAG    "File1"
#include <mrX/kernel/mr_log.h>

void file1_function(void) {
    MR_LOG_INF("Message from file1");
}

/* file2.c */
#define MR_LOG_TAG    "File2"
#include <mrX/kernel/mr_log.h>

void file2_function(void) {
    MR_LOG_INF("Message from file2");
}

/* ✓ 正确：使用条件编译 */
static void safe_log(int *ptr) {
    if (ptr) {
        MR_LOG_DBG("Pointer value: %p", (void *)ptr);
    } else {
        MR_LOG_ERR("Null pointer detected");
    }
}

/* ✗ 错误：在中断中大量使用日志 */
void irq_handler(void) {
    /* 错误：中断处理程序不应输出大量日志 */
    /* MR_LOG_DBG("IRQ occurred at line %d in function %s", __LINE__, __func__); */

    /* 正确：只记录关键错误 */
    /* MR_LOG_ERR("Critical IRQ error"); */
}

/* ✓ 正确：处理格式化字符串 */
static void format_example(void) {
    int count = 10;
    const char *name = "Test";

    /* 正确：字符串和变量组合 */
    MR_LOG_INF("Processing %s with count=%d", name, count);

    /* 正确：使用修饰符 */
    MR_LOG_DBG("Value: 0x%08X", (unsigned int)address);
}

/* ✗ 错误：在循环中频繁输出日志 */
static void wrong_loop_logging(void) {
    for (int i = 0; i < 10000; i++) {
        /* 错误：每次循环都输出日志，严重影响性能 */
        /* MR_LOG_DBG("Processing item %d", i); */
        process_item(i);
    }

    /* 正确：批量处理时只输出摘要 */
    MR_LOG_INF("Processed %d items", 10000);
}

/* ✓ 正确：使用运行时掩码控制 */
static void runtime_control_example(void) {
    /* 调试阶段 */
    mr_log_mask_set(0xFFFFFFFFU);  /* 启用全部 */
    MR_LOG_DBG("Debug mode enabled");

    /* 发现问题后，减少日志输出 */
    mr_log_mask_set(MR_LOG_MASK(MR_LOG_LEVEL_ERR) |
                    MR_LOG_MASK(MR_LOG_LEVEL_WRN));

    /* 这行不会输出，因为 DBG 被禁用 */
    MR_LOG_DBG("This won't be printed");

    /* 这行会输出 */
    MR_LOG_ERR("This will be printed");
}
```

---

## 3. 设计思路

### 3.1 设计目标

log 模块的设计目标：

1. **轻量级**：最小的代码开销，适合资源受限的嵌入式系统
2. **零开销抽象**：未启用的日志在编译时完全移除
3. **运行时灵活**：支持动态控制日志输出级别
4. **模块化**：可选依赖，可与其他模块灵活组合
5. **简单易用**：类似标准库的接口，降低学习成本

### 3.2 编译时过滤机制

日志宏使用两级过滤：

**第一级：宏定义检查**

```c
#if defined(MR_LOG_TAG) && (MR_CFG_LOG_LEVEL >= MR_LOG_LEVEL_DBG)
#define MR_LOG_DBG(_fmt, ...) \
    mr_log(MR_LOG_LEVEL_DBG, __MR_LOG_FORMAT("DBG", _fmt), ##__VA_ARGS__)
#else
#define MR_LOG_DBG(_fmt, ...)           ((void)0)
#endif
```

- 如果未定义 `MR_LOG_TAG`，宏展开为 `((void)0)`
- 如果级别不够，宏展开为 `((void)0)`
- 完全不产生代码，零内存和 CPU 开销

**第二级：运行时掩码检查**

```c
void mr_log(mr_uint32_t level, const char *fmt, ...) {
    /* 检查级别是否被掩码允许 */
    if (!((1U << level) & __log_level)) {
        return;  /* 直接返回，不输出 */
    }

    /* ... 实际输出 ... */
}
```

- 通过位掩码快速判断是否输出
- 32 位掩码支持最多 32 个级别（当前使用 5 个）

### 3.3 时间戳设计

日志系统提供两种时间戳模式：

**高分辨率时间模式**（定义 `MR_USE_CLOCK_HIRES`）

```c
[2024-09-06-14:30:25:123:456] [INF/Tag] Message
```

- 格式：`年-月-日-时:分:秒:毫秒:微秒`
- 需要配置时区（默认 UTC+8）
- 适合需要绝对时间的应用

**运行时间模式**（默认）

```c
[      0.123] [INF/Tag] Message
```

- 格式：`秒.毫秒`（从系统启动开始计时）
- 不依赖外部时间源
- 适合相对时间分析

### 3.4 日志级别设计

五个日志级别的语义：

| 级别 | 名称  | 用途               | 示例                       |
|----|-----|------------------|--------------------------|
| 0  | ERR | 错误，系统无法继续执行的错误   | "Flash write failed"     |
| 1  | WRN | 警告，异常但可恢复的情况     | "Buffer nearly full"     |
| 2  | INF | 信息，正常但重要的状态变化    | "System initialized"     |
| 3  | DBG | 调试，用于诊断问题的详细信息   | "Register value: 0x1234" |
| 4  | TRC | 跟踪，函数入口/出口等细粒度信息 | "Enter function_foo"     |

**级别选择建议**：

- 生产环境：`MR_CFG_LOG_LEVEL = MR_LOG_LEVEL_INF`（默认启用 ERR、WRN、INF）
- 调试版本：`MR_CFG_LOG_LEVEL = MR_LOG_LEVEL_DBG` 或 `MR_LOG_LEVEL_TRC`
- 空间受限：`MR_CFG_LOG_LEVEL = MR_LOG_LEVEL_ERR`

### 3.5 与其他日志系统的对比

#### vs printf

| 特性   | log 模块 | printf |
|------|--------|--------|
| 级别过滤 | 是      | 否      |
| 时间戳  | 自动     | 手动     |
| 编译优化 | 是      | 否      |
| 标签支持 | 是      | 否      |

#### vs 串口直接输出

| 特性  | log 模块 | 串口直接输出 |
|-----|--------|--------|
| 格式化 | 统一格式   | 手动格式   |
| 时间戳 | 自动     | 手动添加   |
| 过滤  | 支持     | 不支持    |
| 灵活性 | 一般     | 高      |

#### vs 复杂日志框架（如 log4c）

| 特性   | log 模块 | 复杂框架     |
|------|--------|----------|
| 代码大小 | 极小     | 较大       |
| 功能   | 基础     | 丰富       |
| 过滤能力 | 级别     | 级别/模块/内容 |
| 输出目标 | 单一     | 多目标      |
| 适用场景 | 嵌入式    | 通用应用     |

### 3.6 依赖关系

```
log 模块依赖：
├── printf 模块（格式化输出）
│   └── sprintf 模块
│       └── string 模块
└── clock 模块（时间戳）
    └── [可选] hires_clock（高分辨率时间）
```

**移植注意事项**：

1. 必须实现 `mr_vprintf()` 函数
2. 必须实现 `mr_clock_uptime()` 函数
3. 如需高分辨率时间，实现 `mr_clock_hires_tick()` 和 `mr_hires_tick2timestamp()`

---

## 4. 参考资料

### 4.1 相关模块

- **printf**：格式化输出模块
- **clock**：时钟和时间管理模块
- **sprintf**：字符串格式化模块

### 4.2 设计参考

- **Android logcat**：Android 系统的日志工具
- **Linux printk**：Linux 内核的打印函数
- **RTOS 日志**：FreeRTOS、ThreadX 等 RTOS 的日志机制

### 4.3 延伸阅读

- **日志级别最佳实践**：如何合理设置和使用日志级别
- **嵌入式调试技巧**：使用日志进行嵌入式系统调试
- **性能分析**：通过时间戳分析代码执行性能

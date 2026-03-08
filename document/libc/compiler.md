# compiler - 编译器抽象

## 1. 模块概述

### 1.1 功能介绍

compiler 模块提供跨编译器的宏定义抽象，屏蔽不同编译器之间的语法差异。它定义了一套统一的编译器属性宏，使得开发者可以使用相同的代码在不同编译器（GCC、Clang、ARMCC）上编译。

该模块主要用于定义代码段属性、对齐属性、函数属性等编译器特定功能，是 mrX 框架实现链接段排序、初始化系统等高级特性的基础。

### 1.2 主要特性

- **多编译器支持**：支持 GCC、Clang、ARMCC（ARM Compiler）
- **统一接口**：提供统一的宏定义，隐藏编译器差异
- **链接段控制**：支持将代码/数据放入指定段
- **内存对齐**：支持指定变量/结构体的对齐方式
- **零运行时开销**：所有宏在编译期展开

### 1.3 适用场景

compiler 模块适用于以下场景：

- **分级初始化**：通过链接段实现模块的分级初始化
- **驱动注册**：将驱动信息放入特定链接段
- **内存布局**：精确控制变量/函数的内存位置
- **编译器兼容**：编写跨编译器的代码

---

## 2. 使用指南

### 2.1 宏说明

#### MR_ALIGN() - 内存对齐

```c
#define MR_ALIGN(_x)                    __attribute__((aligned(_x)))
```

**功能**：指定变量或结构体的对齐字节数

**参数**：

- `_x`：对齐字节数（必须是 2 的幂，如 4、8、16）

**示例**：

```c
/* 8 字节对齐的结构体 */
struct MR_ALIGN(8) dma_buffer_t {
    mr_uint32_t src;
    mr_uint32_t dst;
    mr_uint8_t  data[256];
};

/* 16 字节对齐的数组 */
mr_uint8_t MR_ALIGN(16) cache_line[64];

/* 对齐的变量 */
mr_uint32_t MR_ALIGN(4) aligned_value = 0;
```

**注意事项**：

- 对齐值必须是 2 的幂（1、2、4、8、16、32...）
- 对齐值应不大于默认对齐值才能保证性能
- 过大的对齐可能导致内存浪费

#### MR_SECTION() - 段属性

```c
#define MR_SECTION(_x)                  __attribute__((section(_x)))
```

**功能**：将变量或函数放入指定的链接段

**参数**：

- `_x`：段名称字符串

**示例**：

```c
/* 将函数放入 .text.fast 段 */
void MR_SECTION(".text.fast") fast_function(void) {
    /* 高频执行的代码 */
}

/* 将变量放入 .data.config 段 */
const mr_uint32_t MR_SECTION(".data.config") device_id = 0x12345678;

/* 将初始化函数放入特定段 */
void MR_SECTION("mr_init.2") my_module_init(void) {
    /* 初始化代码 */
}
```

**应用场景**：

- 分级初始化：不同优先级的初始化函数放入不同段
- 驱动注册：将驱动描述符放入特定段
- 代码优化：将热路径代码放入快速执行段

#### MR_SECTION_ALIGN() - 段对齐

```c
#define MR_SECTION_ALIGN(_x)            MR_ALIGN(MR_ALIGN_SIZE) MR_SECTION(_x)
```

**功能**：将变量或函数放入指定段，并按默认对齐大小对齐

**参数**：

- `_x`：段名称字符串

**示例**：

```c
/* 放入段并按指针大小对齐 */
const driver_entry_t MR_SECTION_ALIGN("mr_driver.3") my_driver_entry = {
    .name = "my_driver",
    .init = my_driver_init,
};
```

**实现原理**：

- `MR_ALIGN_SIZE` 默认为 `sizeof(void *)`（32 位系统为 4，64 位系统为 8）
- 等价于先对齐再放入段

#### MR_UNUSED - 未使用警告抑制

```c
#define MR_UNUSED                       __attribute__((unused))
```

**功能**：抑制变量或函数未使用的编译警告

**示例**：

```c
/* 标记可能未使用的参数 */
void process_data(mr_uint8_t *data MR_UNUSED) {
    /* 某些条件下 data 不使用 */
}

/* 标记可能未使用的函数 */
static MR_UNUSED void debug_helper(void) {
    /* 调试辅助函数，仅调试版本使用 */
}

/* 标记可能未使用的变量 */
static const mr_uint8_t MR_UNUSED table[16] = { /* ... */ };
```

#### MR_USED - 强制保留符号

```c
#define MR_USED                         __attribute__((used))
```

**功能**：强制编译器保留变量或函数，即使看似未使用

**示例**：

```c
/* 强制保留变量（用于链接段收集） */
MR_USED const initcall_entry_t __my_init_entry = {
    .func = my_init,
};

/* 强制保留函数 */
MR_USED void placeholder_function(void) {
    /* 函数可能被间接引用 */
}
```

**应用场景**：

- 链接段收集：确保段中的变量不被优化掉
- 中断向量表：保留中断处理函数
- 调试符号：保留调试信息

#### MR_WEAK - 弱符号

```c
#define MR_WEAK                         __attribute__((weak))
```

**功能**：将函数或变量声明为弱符号，允许被强符号覆盖

**示例**：

```c
/* 定义弱函数 - 可被用户定义覆盖 */
void MR_WEAK mr_port_putc(char ch) {
    /* 默认实现：空操作 */
}

/* 用户可以提供自己的实现 */
void mr_port_putc(char ch) {
    /* 用户实现：实际输出字符 */
    uart_send(ch);
}

/* 弱变量 */
mr_uint32_t MR_WEAK system_clock_freq = 16000000;  /* 默认 16MHz */

/* 用户可以覆盖 */
mr_uint32_t system_clock_freq = 72000000;  /* 实际 72MHz */
```

**应用场景**：

- 平台移植：提供默认实现，平台可覆盖
- 配置参数：提供默认值，用户可自定义
- Hook 函数：允许用户注入自定义逻辑

#### MR_INLINE - 内联函数

```c
#define MR_INLINE                       static __inline
```

**功能**：定义内联函数

**示例**：

```c
/* 定义内联函数 */
MR_INLINE mr_uint32_t read_reg32(mr_uintptr_t addr) {
    return *(volatile mr_uint32_t *)addr;
}

MR_INLINE void write_reg32(mr_uintptr_t addr, mr_uint32_t value) {
    *(volatile mr_uint32_t *)addr = value;
}

/* 使用 */
int main(void) {
    write_reg32(0x40000000, 0x12345678);
    mr_uint32_t value = read_reg32(0x40000000);
    return 0;
}
```

**注意事项**：

- `static` 确保函数具有内部链接，避免符号冲突
- `__inline` 建议编译器内联展开
- 最终是否内联由编译器决定

#### MR_ALIGN_SIZE - 默认对齐大小

```c
#define MR_ALIGN_SIZE                   (sizeof(void *))
```

**功能**：定义默认的对齐字节数（指针大小）

**示例**：

```c
/* 获取当前架构的默认对齐值 */
mr_size_t alignment = MR_ALIGN_SIZE;  /* 32位: 4, 64位: 8 */
```

#### MR_IGNORE() - 忽略变量警告

```c
#define MR_IGNORE(_x)                   (void)(_x)
```

**功能**：显式忽略变量值，抑制未使用警告

**示例**：

```c
/* 忽略函数返回值 */
int read_value(void) {
    return 42;
}

void example(void) {
    MR_IGNORE(read_value());  /* 不关心返回值 */
}

/* 忽略参数 */
void callback(int event, void *arg) {
    MR_IGNORE(arg);  /* 不使用 arg 参数 */
    if (event == 1) {
        /* 处理事件 */
    }
}
```

### 2.2 支持的编译器

compiler 模块支持以下编译器：

| 编译器 | 宏定义 | 说明 |
|--------|--------|------|
| GCC | `__GNUC__` | GNU C Compiler |
| Clang | `__clang__` | LLVM Clang |
| ARMCC | `__ARMCC_VERSION` | ARM Compiler 5/6 |

**不支持其他编译器时**：

```c
#else
#error "Compiler not supported"
#endif
```

### 2.3 典型应用案例

#### 案例 1：分级初始化

```c
#include <mrX/libc/mr_compiler.h>

/* 定义初始化函数条目 */
typedef struct {
    void (*func)(void);
} initcall_entry_t;

/* 定义初始化函数 */
void module_a_init(void) {
    /* 模块 A 初始化 */
}

void module_b_init(void) {
    /* 模块 B 初始化 */
}

/* 将初始化条目放入不同段（按字典序排序） */
MR_USED const initcall_entry_t MR_SECTION_ALIGN("mr_init.2.0") __module_a_init = {
    .func = module_a_init,
};

MR_USED const initcall_entry_t MR_SECTION_ALIGN("mr_init.2.1") __module_b_init = {
    .func = module_b_init,
};

/* 链接器会按段名排序，先执行 module_a_init，再执行 module_b_init */
```

#### 案例 2：驱动注册

```c
#include <mrX/libc/mr_compiler.h>

typedef struct {
    const char *name;
    void *data;
} driver_desc_t;

/* 驱动初始化函数 */
static void gpio_driver_init(void) {
    /* GPIO 驱动初始化 */
}

/* 将驱动描述符放入驱动段 */
MR_USED const driver_desc_t MR_SECTION_ALIGN("mr_driver.3") __gpio_driver = {
    .name = "gpio",
    .data = NULL,
};
```

#### 案例 3：内存对齐的 DMA 缓冲区

```c
#include <mrX/libc/mr_compiler.h>

/* 32 字节对齐的 DMA 缓冲区 */
mr_uint8_t MR_ALIGN(32) dma_tx_buffer[256];
mr_uint8_t MR_ALIGN(32) dma_rx_buffer[256];

/* 对齐的结构体 */
struct MR_ALIGN(16) aligned_struct {
    mr_uint64_t a;
    mr_uint64_t b;
};
```

#### 案例 4：弱符号实现平台接口

```c
#include <mrX/libc/mr_compiler.h>

/* 框架提供默认实现（弱符号） */
void MR_WEAK mr_platform_delay_us(mr_uint32_t us) {
    /* 默认空实现 */
}

/* 平台提供实际实现（强符号） */
void mr_platform_delay_us(mr_uint32_t us) {
    /* 使用硬件定时器实现精确延时 */
    timer_delay(us);
}

/* 链接时优先使用强符号 */
```

#### 案例 5：条件编译和函数属性

```c
#include <mrX/libc/mr_compiler.h>

/* 可能未使用的参数 */
static int process_item(int value, int options MR_UNUSED) {
    return value * 2;
}

/* 可能未使用的调试函数 */
#if defined(DEBUG)
static void dump_buffer(const void *buffer, mr_size_t size) {
    /* 转储缓冲区内容 */
}
#else
static void dump_buffer(const void *buffer MR_UNUSED,
                        mr_size_t size MR_UNUSED) {
    /* 空实现 */
}
#endif
```

---

## 3. 设计思路

### 3.1 设计目标

compiler 模块的设计目标：

1. **编译器无关性**：屏蔽不同编译器的语法差异
2. **最小依赖**：只依赖编译器内置功能
3. **零开销**：所有宏在编译期展开
4. **类型安全**：利用编译器类型检查
5. **可维护性**：统一的接口便于添加新编译器支持

### 3.2 实现原理

#### 编译器检测

通过预定义宏检测编译器类型：

```c
#if defined(__GNUC__) || defined(__clang__) || defined(__ARMCC_VERSION)
    /* GCC, Clang, ARMCC 都支持 __attribute__ */
    #define MR_ALIGN(_x)    __attribute__((aligned(_x)))
    /* ... */
#else
    #error "Compiler not supported"
#endif
```

#### 属性宏的实现

| 功能 | GCC/Clang/ARMCC 语法 |
|------|-------------------|
| 对齐 | `__attribute__((aligned(n)))` |
| 段 | `__attribute__((section("name")))` |
| 未使用 | `__attribute__((unused))` |
| 强制保留 | `__attribute__((used))` |
| 弱符号 | `__attribute__((weak))` |

#### 内联函数的实现

```c
#define MR_INLINE    static __inline
```

- `static`：内部链接，避免符号冲突
- `__inline`：建议内联（C99 标准）

### 3.3 设计权衡

#### 为什么使用宏而不是函数？

1. **编译期展开**：宏在预处理阶段展开，无运行时开销
2. **编译器特定**：许多功能是编译器特定的，不能用标准 C 实现
3. **灵活应用**：可以用于变量、函数、类型等多种场景

#### 为什么强制指定编译器？

1. **可预测性**：明确支持的编译器，避免未知行为
2. **错误提示**：使用不支持的编译器时给出明确错误
3. **质量保证**：只在经过测试的编译器上保证正确性

#### 为什么 MR_INLINE 使用 static？

1. **避免符号冲突**：多个文件中同名 inline 函数不会冲突
2. **启用内联**：编译器更愿意内联 static 函数
3. **符合惯例**：这是定义工具内联函数的标准做法

---

## 4. 参考资料

### 4.1 相关模块

- **initcall**：分级初始化机制，依赖 MR_SECTION
- **driver**：驱动框架，依赖 MR_SECTION 和 MR_SECTION_ALIGN
- **types**：基础类型，可能使用 MR_ALIGN

### 4.2 编译器文档

- **GCC 文档**：[Using GNU C Attribute](https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html)
- **Clang 文档**：[Language Extensions](https://clang.llvm.org/docs/LanguageExtensions.html)
- **ARMCC 文档**：[ARM Compiler Attributes](https://developer.arm.com/documentation/101070/0009)

### 4.3 延伸阅读

- **链接脚本**：理解链接段的作用和链接器脚本
- **内存对齐**：理解对齐对性能的影响
- **弱符号**：理解弱符号在库设计中的作用
- **内联函数**：理解 inline 关键字和编译器优化

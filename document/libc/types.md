# types - 基础类型

## 1. 模块概述

### 1.1 功能介绍

types 模块是 mrX 框架中最基础的模块，提供跨平台的基础数据类型定义。它为不同编译器、不同架构的嵌入式系统提供了统一的数据类型抽象，确保代码的可移植性和一致性。

types 模块定义了所有其他模块依赖的基础类型，包括整数类型、大小类型、布尔类型、指针类型和浮点类型等。通过统一的类型命名和定义，开发者可以在不同平台间移植代码而无需修改类型相关的代码。

### 1.2 主要特性

- **跨平台兼容**：支持 STD 模式（标准库）和自定义模式
- **统一命名规范**：所有类型以 `mr_` 前缀，避免命名冲突
- **完整的类型覆盖**：整数、布尔、指针、浮点、变参列表
- **编译时类型检查**：提供 MIN/MAX 宏定义
- **架构适配**：自动适配 32/64 位架构

### 1.3 适用场景

types 模块适用于以下场景：

- **嵌入式系统**：需要统一类型定义的资源受限平台
- **跨平台开发**：需要在不同架构间移植代码的项目
- **驱动开发**：需要精确控制数据大小的底层代码
- **裸机/RTOS**：兼容裸机和 RTOS 环境

---

## 2. 使用指南

### 2.1 类型定义说明

#### 有符号整数类型

```c
typedef signed char                     mr_int8_t;
typedef short                           mr_int16_t;
typedef int                             mr_int32_t;
typedef long long                       mr_int64_t;
```

**类型说明**：

| 类型 | 存储大小 | 最小值 | 最大值 |
|------|---------|--------|--------|
| `mr_int8_t` | 1 字节 | `MR_INT8_MIN` (-128) | `MR_INT8_MAX` (127) |
| `mr_int16_t` | 2 字节 | `MR_INT16_MIN` (-32768) | `MR_INT16_MAX` (32767) |
| `mr_int32_t` | 4 字节 | `MR_INT32_MIN` (-2147483648) | `MR_INT32_MAX` (2147483647) |
| `mr_int64_t` | 8 字节 | `MR_INT64_MIN` (-9223372036854775808) | `MR_INT64_MAX` (9223372036854775807) |

**使用示例**：

```c
mr_int8_t   temp = 25;       /* 温度值 */
mr_int16_t  voltage = 3300;  /* 电压值 (mV) */
mr_int32_t  counter = 0;     /* 计数器 */
mr_int64_t  timestamp = 0;   /* 时间戳 */
```

#### 无符号整数类型

```c
typedef unsigned char                   mr_uint8_t;
typedef unsigned short                  mr_uint16_t;
typedef unsigned int                    mr_uint32_t;
typedef unsigned long long              mr_uint64_t;
```

**类型说明**：

| 类型 | 存储大小 | 最大值 |
|------|---------|--------|
| `mr_uint8_t` | 1 字节 | `MR_UINT8_MAX` (255) |
| `mr_uint16_t` | 2 字节 | `MR_UINT16_MAX` (65535) |
| `mr_uint32_t` | 4 字节 | `MR_UINT32_MAX` (4294967295) |
| `mr_uint64_t` | 8 字节 | `MR_UINT64_MAX` (18446744073709551615) |

**使用示例**：

```c
mr_uint8_t  status = 0;       /* 状态寄存器 */
mr_uint16_t length = 512;     /* 数据长度 */
mr_uint32_t address = 0x0800; /* 内存地址 */
mr_uint64_t id = 0;           /* 唯一标识符 */
```

#### 大小类型

```c
#if defined(MR_USE_ARCH_64B)
typedef mr_uint64_t                     mr_size_t;
typedef mr_int64_t                      mr_ssize_t;
#else
typedef mr_uint32_t                     mr_size_t;
typedef mr_int32_t                      mr_ssize_t;
#endif
```

**类型说明**：

- `mr_size_t`：无符号大小类型，用于表示内存大小、数组长度等
- `mr_ssize_t`：有符号大小类型，用于表示可能为负的大小

| 架构 | `mr_size_t` | `mr_ssize_t` |
|------|------------|-------------|
| 32 位 | 32 位无符号整数 | 32 位有符号整数 |
| 64 位 | 64 位无符号整数 | 64 位有符号整数 |

**使用示例**：

```c
mr_size_t  buffer_size = 1024;  /* 缓冲区大小 */
mr_ssize_t bytes_read = -1;     /* 读取字节数（-1 表示错误） */
```

#### 布尔类型

```c
typedef int                             mr_bool_t;
#define MR_FALSE                        ((mr_bool_t)0)
#define MR_TRUE                         ((mr_bool_t)1)
```

**类型说明**：

- `mr_bool_t`：布尔类型
- `MR_FALSE`：假值（0）
- `MR_TRUE`：真值（1）

**使用示例**：

```c
mr_bool_t is_ready = MR_FALSE;
mr_bool_t is_valid = MR_TRUE;

if (is_valid) {
    /* 执行操作 */
}
```

#### 指针类型

```c
typedef void *                          mr_ptr_t;
typedef mr_ssize_t                      mr_intptr_t;
typedef mr_size_t                       mr_uintptr_t;
#define MR_NULL                         ((void *)0)
```

**类型说明**：

- `mr_ptr_t`：通用指针类型（void *）
- `mr_intptr_t`：有符号整数类型，可安全存储指针值
- `mr_uintptr_t`：无符号整数类型，可安全存储指针值
- `MR_NULL`：空指针常量

**使用示例**：

```c
mr_ptr_t     ptr = MR_NULL;       /* 通用指针 */
mr_intptr_t  addr = (mr_intptr_t)ptr;  /* 指针转整数 */
mr_uintptr_t uaddr = (mr_uintptr_t)ptr; /* 指针转无符号整数 */

if (ptr == MR_NULL) {
    /* 指针为空 */
}
```

#### 浮点类型

```c
typedef float                           mr_f32_t;
typedef double                          mr_f64_t;
```

**类型说明**：

- `mr_f32_t`：32 位单精度浮点数（通常 6-7 位有效数字）
- `mr_f64_t`：64 位双精度浮点数（通常 15-16 位有效数字）

**使用示例**：

```c
mr_f32_t  temperature = 25.5f;  /* 温度值 */
mr_f64_t  pi = 3.14159265359;   /* 圆周率 */
```

#### 变参列表类型

```c
typedef va_list                         mr_va_list;
#define mr_va_start(_a, _l)             va_start(_a, _l)
#define mr_va_arg(_a, _t)               va_arg(_a, _t)
#define mr_va_end(_a)                   va_end(_a)
```

**类型说明**：

- `mr_va_list`：变参列表类型（直接使用标准库的 va_list）
- `mr_va_start`：初始化变参列表
- `mr_va_arg`：获取下一个变参
- `mr_va_end`：清理变参列表

**使用示例**：

```c
int my_printf(const char *fmt, ...) {
    mr_va_list args;
    int ret;

    mr_va_start(args, fmt);
    ret = mr_vsnprintf(buffer, sizeof(buffer), fmt, args);
    mr_va_end(args);

    return ret;
}
```

### 2.2 配置模式

types 模块支持两种配置模式：

#### STD 模式（标准库模式）

```c
#define MR_USE_TYPES_STD
```

**特点**：

- 直接使用标准库的 `<stdint.h>`、`<stddef.h>`、`<stdbool.h>`
- 类型定义映射到标准类型
- 适合有标准库支持的平台

#### 自定义模式

**特点**：

- 使用编译器内置类型定义
- 不依赖标准库
- 适合裸机环境

### 2.3 典型应用案例

#### 案例 1：数据结构定义

```c
#include <mrX/libc/mr_types.h>

typedef struct {
    mr_uint8_t  id;          /* 设备 ID */
    mr_uint16_t vendor_id;   /* 供应商 ID */
    mr_uint32_t serial_num;  /* 序列号 */
    mr_bool_t   is_active;   /* 激活状态 */
} device_info_t;

device_info_t dev = {
    .id = 1,
    .vendor_id = 0x1234,
    .serial_num = 0x5678,
    .is_active = MR_TRUE,
};
```

#### 案例 2：跨平台内存操作

```c
#include <mrX/libc/mr_types.h>

void process_buffer(mr_uint8_t *buffer, mr_size_t size) {
    for (mr_size_t i = 0; i < size; i++) {
        buffer[i] = (mr_uint8_t)(buffer[i] ^ 0xFF);  /* 按位取反 */
    }
}

int main(void) {
    mr_uint8_t data[256];
    process_buffer(data, sizeof(data));
    return 0;
}
```

#### 案例 3：寄存器操作

```c
#include <mrX/libc/mr_types.h>

#define REG_BASE    ((mr_uintptr_t)0x40000000)
#define REG_OFFSET  0x04

volatile mr_uint32_t *get_register(mr_uintptr_t offset) {
    return (volatile mr_uint32_t *)(REG_BASE + offset);
}

int main(void) {
    volatile mr_uint32_t *reg = get_register(REG_OFFSET);
    mr_uint32_t value = *reg;
    *reg = 0x12345678;
    return 0;
}
```

#### 案例 4：数组边界检查

```c
#include <mrX/libc/mr_types.h>

#define ARRAY_SIZE  100

mr_bool_t is_index_valid(mr_size_t index) {
    return (index < ARRAY_SIZE) ? MR_TRUE : MR_FALSE;
}

int main(void) {
    mr_int32_t array[ARRAY_SIZE];
    mr_size_t index = 50;

    if (is_index_valid(index)) {
        array[index] = 123;
    }

    return 0;
}
```

---

## 3. 设计思路

### 3.1 设计目标

types 模块的设计目标：

1. **可移植性**：提供跨平台的统一类型定义
2. **明确性**：类型名称清晰表达其大小和用途
3. **一致性**：统一的命名规范（mr_ 前缀）
4. **灵活性**：支持多种配置模式
5. **最小依赖**：在自定义模式下不依赖标准库

### 3.2 设计权衡

#### 为什么使用 mr_ 前缀？

1. **避免命名冲突**：与标准库类型（如 `int32_t`）区分
2. **明确归属**：清楚表明这是 mrX 框架的类型
3. **便于搜索**：代码中容易找到框架相关的类型使用

#### 为什么同时支持 STD 和自定义模式？

1. **灵活性**：适应不同的编译环境和平台
2. **渐进式迁移**：可以从有标准库的环境逐步迁移到裸机环境
3. **代码复用**：可以复用现有使用标准库的代码

#### size_t 根据架构自适应的原因

1. **性能优化**：指针大小与机器字长相同可以提高效率
2. **内存效率**：32 位系统使用 32 位 size_t 节省内存
3. **地址表示**：size_t 需要能够表示任意对象的大小

### 3.3 实现原理

#### 类型定义的实现

在 STD 模式下，类型直接映射到标准库类型：

```c
#if defined(MR_USE_TYPES_STD)
typedef int8_t                          mr_int8_t;
typedef uint8_t                         mr_uint8_t;
/* ... */
#endif
```

在自定义模式下，使用编译器内置类型：

```c
#else
typedef signed char                     mr_int8_t;
typedef unsigned char                   mr_uint8_t;
/* ... */
#endif
```

#### 常量定义的实现

MIN/MAX 常量使用宏定义，确保编译期计算：

```c
#define MR_INT8_MIN                     (-128)
#define MR_INT8_MAX                     (127)
#define MR_UINT8_MAX                    (255U)
```

---

## 4. 参考资料

### 4.1 相关模块

types 模块是所有其他模块的基础，几乎所有模块都依赖它：

- **compiler**：编译器抽象，可能使用 types 模块
- **errno**：错误码，使用 `mr_err_t`（基于 `int`）
- **malloc**：内存管理，使用 `mr_size_t`
- **string**：字符串操作，使用多种基础类型

### 4.2 相关标准

- **C99 标准**：`<stdint.h>` 定义了定宽整数类型
- **POSIX**：定义了 `size_t` 等类型
- **编译器文档**：各编译器的内置类型说明

### 4.3 延伸阅读

- **数据类型对齐**：理解数据在内存中的对齐规则
- **字节序**：大端序与小端序的区别
- **类型转换**：隐式与显式类型转换的风险

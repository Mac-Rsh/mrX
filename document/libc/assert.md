# assert - 断言

## 1. 模块概述

### 1.1 功能介绍

assert 模块提供运行时断言和静态断言功能，用于调试和验证代码的正确性。运行时断言在条件不满足时触发系统恐慌并停止执行，而静态断言在编译期验证常量表达式的真假。

该模块支持四种配置模式：STD 模式（标准 assert.h）、3RD 模式（第三方库）、PORT 模式（移植层实现）和空模式（完全禁用断言），为不同场景提供了灵活的选择。

### 1.2 主要特性

- **运行时断言**：验证运行时条件，失败时触发恐慌
- **静态断言**：编译期验证常量表达式
- **多模式支持**：STD、3RD、PORT、空模式
- **零开销**：禁用模式下无任何运行时开销

### 1.3 适用场景

assert 模块适用于以下场景：

- **参数验证**：验证函数参数的有效性
- **不变量检查**：验证程序状态的不变量
- **编译期检查**：验证类型大小、对齐等常量属性
- **调试辅助**：在开发阶段发现逻辑错误

---

## 2. 使用指南

### 2.1 运行时断言

#### MR_ASSERT() - 运行时断言

```c
#define MR_ASSERT(_expr)                assert(_expr)
```

**功能**：验证表达式，失败时触发恐慌

**参数**：

- `_expr`：要验证的表达式

**行为**：

- 表达式为真：继续执行
- 表达式为假：触发恐慌并进入死循环

**示例**：

```c
#include <mrX/libc/mr_assert.h>

/* 参数验证 */
void buffer_write(mr_uint8_t *buffer, mr_size_t size, mr_uint8_t data) {
    MR_ASSERT(buffer != MR_NULL);  /* 缓冲区指针不能为空 */
    MR_ASSERT(size > 0);           /* 大小必须大于 0 */

    buffer[0] = data;
}

/* 不变量检查 */
typedef struct {
    mr_uint32_t count;
    mr_uint8_t  data[100];
} queue_t;

void queue_push(queue_t *q, mr_uint8_t data) {
    MR_ASSERT(q->count < 100);     /* 队列不能溢出 */

    q->data[q->count++] = data;
}

/* 状态验证 */
enum {
    STATE_IDLE = 0,
    STATE_BUSY,
    STATE_ERROR,
};

void state_machine(int event) {
    static int state = STATE_IDLE;

    MR_ASSERT(state >= STATE_IDLE && state <= STATE_ERROR);

    /* 状态机处理... */
}
```

#### MR_PANIC() - 系统恐慌

```c
#define MR_PANIC()                    assert(0)
```

**功能**：无条件触发系统恐慌

**示例**：

```c
#include <mrX/libc/mr_assert.h>

/* 不应该执行的代码路径 */
void switch_example(int value) {
    switch (value) {
        case 0:
            /* 处理情况 0 */
            break;
        case 1:
            /* 处理情况 1 */
            break;
        default:
            /* 不应该到达这里 */
            MR_PANIC();
    }
}

/* 致命错误处理 */
mr_err_t critical_check(void) {
    if (critical_hardware_error()) {
        printf("Critical hardware error detected!\n");
        MR_PANIC();  /* 系统无法继续运行 */
    }
    return 0;
}
```

### 2.2 静态断言

#### MR_STATIC_ASSERT() - 编译期断言

```c
#define MR_STATIC_ASSERT(_name, _expr) \
    typedef char __mr_static_assert_##_name[(_expr) ? 1 : -1]
```

**功能**：在编译期验证常量表达式

**参数**：

- `_name`：断言的唯一名称
- `_expr`：常量表达式

**行为**：

- 表达式为真：编译通过
- 表达式为假：编译失败（负数组大小）

**示例**：

```c
#include <mrX/libc/mr_assert.h>

/* 验证类型大小 */
MR_STATIC_ASSERT(size_of_int, sizeof(int) == 4);
MR_STATIC_ASSERT(size_of_ptr, sizeof(void *) >= 4);

/* 验证结构体成员偏移 */
typedef struct {
    mr_uint8_t  a;
    mr_uint32_t b;
} test_struct_t;

MR_STATIC_ASSERT(offset_of_b, offsetof(test_struct_t, b) == 4);

/* 验证常量值 */
#define MAX_DEVICES    16
#define MIN_DEVICES    1

MR_STATIC_ASSERT(max_devices_valid, MAX_DEVICES >= MIN_DEVICES);
MR_STATIC_ASSERT(max_devices_power_of_2, (MAX_DEVICES & (MAX_DEVICES - 1)) == 0);

/* 验证枚举值 */
enum {
    COLOR_RED = 0,
    COLOR_GREEN,
    COLOR_BLUE,
    COLOR_COUNT
};

MR_STATIC_ASSERT(color_count_valid, COLOR_COUNT == 3);
```

**常见用途**：

```c
/* 1. 类型大小检查 */
MR_STATIC_ASSERT(int_32bit, sizeof(int) == 4);

/* 2. 结构体打包检查 */
MR_STATIC_ASSERT(pack_check, sizeof(small_struct) <= 8);

/* 3. 指针大小检查 */
MR_STATIC_ASSERT(ptr_size, sizeof(void *) == sizeof(mr_size_t));

/* 4. 数组大小检查 */
#define BUFFER_SIZE  1024
extern mr_uint8_t buffer[BUFFER_SIZE];
MR_STATIC_ASSERT(buffer_size, sizeof(buffer) == BUFFER_SIZE);

/* 5. 对齐检查 */
struct MR_ALIGN(8) aligned_struct {
    mr_uint32_t a;
    mr_uint32_t b;
};
MR_STATIC_ASSERT(alignment_check, __alignof__(aligned_struct) == 8);
```

### 2.3 配置模式

#### STD 模式（标准库模式）

```c
#define MR_USE_ASSERT_STD
```

**特点**：

- 使用标准库的 `<assert.h>`
- `NDEBUG` 宏控制断言行为
- 适合有标准库的平台

**定义**：

```c
#if defined(MR_USE_ASSERT_STD)
#define MR_ASSERT(_expr)                assert(_expr)
#define MR_PANIC()                      assert(0)
#endif
```

**注意**：定义 `NDEBUG` 会禁用标准断言。

#### 3RD 模式（第三方库模式）

```c
#define MR_USE_ASSERT_3RD
```

**特点**：

- 使用第三方库的断言实现
- 适配特定的 RTOS 或库
- 定义在 `mrX/3party/libc/mr_assert.h`

**定义**：

```c
#elif defined(MR_USE_ASSERT_3RD)
/* In 'mrX/3party/libc/mr_assert.h' */
#endif
```

#### PORT 模式（移植层模式）

```c
#define MR_USE_ASSERT
```

**特点**：

- 使用移植层实现的断言
- 调用 `mr_port_assert()` 函数
- 可自定义断言行为

**定义**：

```c
#elif defined(MR_USE_ASSERT)
#define MR_ASSERT(_expr)                                                       \
    do {                                                                       \
        if (!(_expr)) {                                                        \
            mr_port_assert(__FILE__, __LINE__, #_expr);                        \
            while (1) {                                                        \
            }                                                                  \
        }                                                                      \
    } while (0)
#define MR_PANIC()                                                             \
    do {                                                                       \
        mr_port_assert(__FILE__, __LINE__, "panic");                           \
        while (1) {                                                            \
        }                                                                      \
    } while (0)
#endif
```

**移植层接口**：

```c
/* port/mr_port_assert.h */
void mr_port_assert(const char *file, int line, const char *expr);
```

**移植层实现示例**：

```c
#include <stdio.h>

void mr_port_assert(const char *file, int line, const char *expr) {
    printf("ASSERTION FAILED: %s at %s:%d\n", expr, file, line);
    /* 可以添加更多处理，如：
     * - 打印调用栈
     * - 写入日志
     * - 触发调试器断点
     */
}
```

#### 空模式（完全禁用）

**特点**：

- 完全禁用断言
- 无任何运行时开销
- 适合生产环境

**定义**：

```c
#else
#define MR_ASSERT(_expr)                ((void)0)
#define MR_PANIC()                      \
    do {                                \
        while (1) {                     \
        }                               \
    } while (0)
#endif
```

**注意**：空模式下 `MR_PANIC()` 仍然会进入死循环。

### 2.4 典型应用案例

#### 案例 1：函数参数验证

```c
#include <mrX/libc/mr_assert.h>

mr_err_t list_add_tail(list_t *list, list_node_t *node) {
    /* 参数验证 */
    MR_ASSERT(list != MR_NULL);
    MR_ASSERT(node != MR_NULL);
    MR_ASSERT(!list_contains(list, node));  /* 节点不应已在链表中 */

    /* 添加节点... */
    return 0;
}
```

#### 案例 2：数组边界检查

```c
#include <mrX/libc/mr_assert.h>

#define ARRAY_SIZE  100

mr_int32_t array_get(mr_int32_t *array, mr_size_t index) {
    MR_ASSERT(index < ARRAY_SIZE);

    return array[index];
}

/* 编译期检查 */
MR_STATIC_ASSERT(array_size_valid, ARRAY_SIZE > 0);
```

#### 案例 3：状态机验证

```c
#include <mrX/libc/mr_assert.h>

typedef enum {
    STATE_IDLE,
    STATE_CONNECTING,
    STATE_CONNECTED,
    STATE_DISCONNECTING,
} state_t;

void transition_to(state_t *current, state_t next) {
    MR_ASSERT(current != MR_NULL);

    /* 验证状态转换合法性 */
    switch (*current) {
        case STATE_IDLE:
            MR_ASSERT(next == STATE_CONNECTING);
            break;
        case STATE_CONNECTING:
            MR_ASSERT(next == STATE_CONNECTED || next == STATE_IDLE);
            break;
        case STATE_CONNECTED:
            MR_ASSERT(next == STATE_DISCONNECTING);
            break;
        case STATE_DISCONNECTING:
            MR_ASSERT(next == STATE_IDLE);
            break;
        default:
            MR_PANIC();  /* 无效状态 */
    }

    *current = next;
}
```

#### 案例 4：编译期类型检查

```c
#include <mrX/libc/mr_assert.h>

/* 验证位域大小 */
typedef struct {
    unsigned int mode : 2;
    unsigned int speed : 4;
    unsigned int flags : 10;
} config_t;

MR_STATIC_ASSERT(config_size, sizeof(config_t) == 2);

/* 验证联合体大小 */
typedef union {
    mr_uint32_t as_u32;
    mr_int32_t  as_i32;
    mr_float_t  as_float;
} value_t;

MR_STATIC_ASSERT(value_size, sizeof(value_t) == 4);

/* 验证枚举大小 */
enum { VALUE_MIN = 0, VALUE_MAX = 255 };

MR_STATIC_ASSERT(enum_fits_in_uint8, VALUE_MAX <= MR_UINT8_MAX);
```

#### 案例 5：与调试系统集成

```c
#include <mrX/libc/mr_assert.h>

/* 调试模式使用断言，发布模式返回错误码 */
#if defined(DEBUG)
#define CHECK_PARAM(_expr)   MR_ASSERT(_expr)
#else
#define CHECK_PARAM(_expr)                                           \
    do {                                                             \
        if (!(_expr)) {                                              \
            return -MR_EINVAL;                                       \
        }                                                            \
    } while (0)
#endif

mr_err_t device_config(device_t *dev, const config_t *cfg) {
    CHECK_PARAM(dev != MR_NULL);
    CHECK_PARAM(cfg != MR_NULL);
    CHECK_PARAM(cfg->frequency > 0);

    /* 配置设备... */
    return 0;
}
```

---

## 3. 设计思路

### 3.1 设计目标

assert 模块的设计目标：

1. **调试支持**：帮助发现逻辑错误和不变量违反
2. **编译期检查**：通过静态断言验证常量属性
3. **零开销**：生产环境可完全禁用
4. **灵活性**：支持多种实现方式

### 3.2 运行时断言的实现

#### do-while(0) 包装

```c
#define MR_ASSERT(_expr)                                                       \
    do {                                                                       \
        if (!(_expr)) {                                                        \
            mr_port_assert(__FILE__, __LINE__, #_expr);                        \
            while (1) {                                                        \
            }                                                                  \
        }                                                                      \
    } while (0)
```

**为什么要用 do-while(0)？**

1. **语句安全**：可以在任何地方使用，包括 if-else 语句中
2. **分号安全**：总是需要分号结尾
3. **扩展安全**：可以安全地用于宏定义中

**示例**：

```c
if (condition)
    MR_ASSERT(x > 0);  /* 正确 */
else
    MR_ASSERT(y > 0);  /* 正确 */

/* 如果没有 do-while(0)，上面的代码会出问题 */
```

#### 字符串化运算符

```c
#define MR_ASSERT(_expr)   mr_port_assert(__FILE__, __LINE__, #_expr)
```

`#_expr` 将表达式转换为字符串，便于调试输出：

```c
MR_ASSERT(ptr != MR_NULL);
/* 展开为: mr_port_assert(__FILE__, __LINE__, "ptr != MR_NULL"); */
```

### 3.3 静态断言的实现

#### 负数组大小技巧

```c
#define MR_STATIC_ASSERT(_name, _expr) \
    typedef char __mr_static_assert_##_name[(_expr) ? 1 : -1]
```

**工作原理**：

1. 表达式为真：`char name[1]` - 合法的数组定义
2. 表达式为假：`char name[-1]` - 非法数组定义，编译失败

**为什么需要 _name 参数？**

1. **唯一性**：每个断言需要唯一的类型名
2. **可读性**：错误消息中显示断言名称

**错误消息示例**：

```c
test.c:10: error: size of array '__mr_static_assert_size_check' is negative
```

#### C11 _Static_assert 对比

C11 标准提供了 `_Static_assert` 关键字：

```c
/* C11 风格 */
_Static_assert(sizeof(int) == 4, "int must be 32-bit");

/* mrX 风格（兼容 C99） */
MR_STATIC_ASSERT(int_32bit, sizeof(int) == 4);
```

mrX 使用数组技巧是为了兼容 C99 标准。

### 3.4 设计权衡

#### 为什么 MR_PANIC() 在空模式下仍死循环？

1. **安全考虑**：恐慌是严重错误，应该停止系统
2. **调试价值**：死循环便于调试器定位问题
3. **可预测性**：行为一致，便于理解

#### 何时使用断言 vs 错误码？

| 场景 | 使用 | 示例 |
|------|------|------|
| 内部逻辑错误 | 断言 | 指针不应为空 |
| 外部输入验证 | 错误码 | 用户输入检查 |
| 不变量违反 | 断言 | 链表循环检测 |
| 资源不可用 | 错误码 | 内存分配失败 |

**示例**：

```c
/* 使用断言：内部不变量 */
void list_remove(list_t *list, list_node_t *node) {
    MR_ASSERT(list_contains(list, node));  /* 节点必须在链表中 */
    /* ... */
}

/* 使用错误码：外部条件 */
mr_err_t file_open(const char *path, file_t **file_out) {
    if (path == MR_NULL) {
        return -MR_EINVAL;  /* 用户提供的参数 */
    }
    /* ... */
}
```

---

## 4. 参考资料

### 4.1 相关模块

assert 模块被广泛用于验证：

- **malloc**：验证内存管理的不变量
- **driver**：验证驱动状态
- **object**：验证对象引用计数
- **所有模块**：参数验证和不变量检查

### 4.2 相关标准

- **ISO C**：`assert.h` 宏的定义
- **C11**：`_Static_assert` 关键字
- **POSIX**：断言的使用约定

### 4.3 延伸阅读

- **防御性编程**：如何使用断言提高代码质量
- **契约式编程**：Design by Contract 理念
- **断言最佳实践**：何时使用断言，何时使用错误处理
- **编译期计算**：利用静态断言进行类型检查

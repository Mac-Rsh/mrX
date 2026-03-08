# service - 通用工具宏集合

## 1. 模块概述

### 1.1 功能介绍

service 模块（也称为通用工具宏集合）提供了一系列常用的工具宏和内联函数，用于增强 C
语言的表达能力和代码可读性。这些工具宏涵盖了结构体操作、数组操作、数学运算、对齐操作、宏编程技巧等多个方面。

虽然文件名是 `mr_service.h`，但它实际上是一个**通用工具库**，类似于 Linux 内核的 `kernel.h` 或 GLib 的通用工具函数。这些工具宏在
mrX 框架中被广泛使用，特别是在驱动框架、内核对象层等需要高度抽象和可配置性的场景。

### 1.2 主要特性

- **零运行时开销**：大部分操作在编译期完成，无运行时性能损失
- **类型安全**：利用编译器类型检查，减少运行时错误
- **增强表达能力**：提供宏编程技巧，实现复杂的编译期逻辑
- **跨平台兼容**：处理不同平台的对齐、字节序等问题
- **高度可移植**：纯标准 C 代码，无平台特定依赖

### 1.3 适用场景

service 模块的宏和函数适用于以下场景：

- **内核代码**：需要高效的工具宏
- **驱动开发**：结构体布局、设备注册等
- **宏编程**：条件编译、可变参数处理
- **内存管理**：对齐计算、大小计算
- **数据校验**：奇偶校验等

---

## 2. 使用指南

### 2.1 结构体操作宏

#### MR_CONTAINER_OF()

```c
#define MR_CONTAINER_OF(_ptr, _type, _member) \
    ((void *)((char *)(_ptr) - (char *)((void *)&(((_type *)0)->_member))))
```

**功能**：根据结构体成员指针获取包含该成员的结构体指针

**参数**：

- `_ptr`：成员指针
- `_type`：结构体类型
- `_member`：成员在结构体中的名称

**返回值**：包含该成员的结构体指针

**示例**：

```c
typedef struct {
    int id;
    char name[32];
    mr_list_t link;    /* 链表节点 */
} student_t;

void print_student_info(mr_list_t *node) {
    /* 从链表节点指针获取 student_t 结构体指针 */
    student_t *stu = MR_CONTAINER_OF(node, student_t, link);
    printf("Student ID: %d, Name: %s\n", stu->id, stu->name);
}
```

**实现原理**：

1. 计算成员在结构体中的偏移量：`&(((_type *)0)->_member)`
2. 将成员指针转换为字符指针：`(char *)(_ptr)`
3. 减去偏移量得到结构体起始地址

**注意事项**：

- `_ptr` 必须指向有效成员
- `_type` 和 `_member` 必须匹配
- 这是内核风格的经典宏，广泛用于链表、对象系统等

#### MR_ARRAY_SIZE()

```c
#define MR_ARRAY_SIZE(_array) (sizeof(_array) / sizeof((_array)[0]))
```

**功能**：计算数组的元素个数

**参数**：

- `_array`：数组（不是指针）

**返回值**：数组元素个数

**示例**：

```c
int numbers[10];
char *strings[5];

printf("numbers size: %d\n", MR_ARRAY_SIZE(numbers));  /* 输出: 10 */
printf("strings size: %d\n", MR_ARRAY_SIZE(strings));  /* 输出: 5 */

/* 错误用法：不能用于指针 */
int *ptr = numbers;
printf("wrong: %d\n", MR_ARRAY_SIZE(ptr));  /* 编译错误或错误结果！*/
```

**注意事项**：

- 不能用于函数参数（数组退化为指针）
- 在编译期计算，无运行时开销

### 2.2 数学运算宏

#### MR_MIN() / MR_MAX()

```c
#define MR_MIN(_a, _b) (((_a) < (_b)) ? (_a) : (_b))
#define MR_MAX(_a, _b) (((_a) > (_b)) ? (_a) : (_b))
```

**功能**：计算两个值的最小值或最大值

**参数**：

- `_a, _b`：两个值（支持任意可比较类型）

**返回值**：较小或较大的值

**示例**：

```c
int a = 10, b = 20;
printf("MIN: %d\n", MR_MIN(a, b));  /* 输出: 10 */
printf("MAX: %d\n", MR_MAX(a, b));  /* 输出: 20 */

/* 也可以用于浮点数 */
float x = 1.5f, y = 2.3f;
printf("MIN: %.1f\n", MR_MIN(x, y));  /* 输出: 1.5 */
```

**注意事项**：

- 参数会被求值两次，如果参数有副作用会出问题：
  ```c
  MR_MIN(i++, j++);  /* 错误！i 或 j 会递增两次 */
  ```

#### MR_ROUND_DIV()

```c
#define MR_ROUND_DIV(_a, _b) \
    (((_a) + ((((_a) ^ (_b)) >= 0) ? (_b) : -(_b)) / 2) / (_b))
```

**功能**：整数除法，带四舍五入

**参数**：

- `_a`：被除数
- `_b`：除数

**返回值**：四舍五入后的商

**示例**：

```c
printf("%d\n", MR_ROUND_DIV(7, 3));   /* 输出: 2 (7/3=2.33→2) */
printf("%d\n", MR_ROUND_DIV(8, 3));   /* 输出: 3 (8/3=2.67→3) */
printf("%d\n", MR_ROUND_DIV(-7, 3));  /* 输出: -2 */
printf("%d\n", MR_ROUND_DIV(-8, 3));  /* 输出: -3 */
```

**实现原理**：

- 通过添加除数的一半（考虑符号）实现四舍五入
- 处理了正负数的不同情况

### 2.3 对齐操作宏

#### MR_ALIGN_UP()

```c
#define MR_ALIGN_UP(_x, _ali) (((_x) + (_ali) - 1) & ~((_ali) - 1))
```

**功能**：向上对齐到指定边界

**参数**：

- `_x`：要对齐的值
- `_ali`：对齐边界（必须是 2 的幂）

**返回值**：对齐后的值

**示例**：

```c
printf("%d\n", MR_ALIGN_UP(0, 8));   /* 输出: 0 */
printf("%d\n", MR_ALIGN_UP(5, 8));   /* 输出: 8 */
printf("%d\n", MR_ALIGN_UP(8, 8));   /* 输出: 8 */
printf("%d\n", MR_ALIGN_UP(13, 8));  /* 输出: 16 */

/* 用于内存对齐 */
size_t size = 123;
size_t aligned_size = MR_ALIGN_UP(size, 8);  /* 对齐到 8 字节 */
```

**实现原理**：

- 假设 `_ali` 是 2 的幂（如 8 = 0b1000）
- `~((_ali) - 1)` 清除低位的对齐位
- `(_x) + (_ali) - 1` 确保向上对齐

#### MR_ALIGN_DOWN()

```c
#define MR_ALIGN_DOWN(_x, _ali) ((_x) & ~((_ali) - 1))
```

**功能**：向下对齐到指定边界

**参数**：

- `_x`：要对齐的值
- `_ali`：对齐边界（必须是 2 的幂）

**返回值**：对齐后的值

**示例**：

```c
printf("%d\n", MR_ALIGN_DOWN(0, 8));   /* 输出: 0 */
printf("%d\n", MR_ALIGN_DOWN(5, 8));   /* 输出: 0 */
printf("%d\n", MR_ALIGN_DOWN(8, 8));   /* 输出: 8 */
printf("%d\n", MR_ALIGN_DOWN(13, 8));  /* 输出: 8 */
```

**注意事项**：

- 对齐边界必须是 2 的幂（2、4、8、16、32、64...）
- 常用于内存分配、DMA 缓冲区对齐等场景

### 2.4 创建局部变量宏

#### MR_MAKE_LOCAL()

```c
#define MR_MAKE_LOCAL(_type, ...) (&((_type){__VA_ARGS__}))
```

**功能**：创建并初始化一个局部变量，返回其指针

**参数**：

- `_type`：变量类型
- `__VA_ARGS__`：初始化值（可变参数）

**返回值**：指向该局部变量的指针

**示例**：

```c
/* 创建结构体 */
typedef struct {
    int x;
    int y;
} point_t;

point_t *p = MR_MAKE_LOCAL(point_t, 10, 20);
printf("Point: (%d, %d)\n", p->x, p->y);  /* 输出: (10, 20) */

/* 创建基本类型 */
int *num = MR_MAKE_LOCAL(int, 42);
printf("Number: %d\n", *num);  /* 输出: 42 */

/* 创建数组（C99 复合字面量） */
int *arr = MR_MAKE_LOCAL(int[3], 1, 2, 3);
printf("Array[0]: %d\n", arr[0]);  /* 输出: 1 */
```

**实现原理**：

- 使用 C99 的**复合字面量**（Compound Literals）
- `(_type){__VA_ARGS__}` 创建一个临时对象
- `&(...)` 获取其地址

**注意事项**：

- 临时对象的生命周期与当前块相同
- 不要返回指向该对象的指针到外部作用域
- 适用于传递临时参数给函数

### 2.5 可变参数宏

#### MR_VA_ARGS_NUM()

```c
#define MR_VA_ARGS_NUM(...) \
    __MR_VA_ARGS_NUM(, ##__VA_ARGS__, 63, 62, 61, ..., 2, 1, 0)
```

**功能**：计算可变参数的数量（最多 63 个）

**参数**：

- `__VA_ARGS__`：可变参数列表

**返回值**：参数个数

**示例**：

```c
printf("%d\n", MR_VA_ARGS_NUM());              /* 输出: 0 */
printf("%d\n", MR_VA_ARGS_NUM(a));             /* 输出: 1 */
printf("%d\n", MR_VA_ARGS_NUM(a, b));          /* 输出: 2 */
printf("%d\n", MR_VA_ARGS_NUM(a, b, c));       /* 输出: 3 */
```

**实现原理**：

- 使用宏递归和参数计数技巧
- `##__VA_ARGS__` 处理空参数的情况
- 从 63 递减到 0，通过参数位置匹配获取计数

#### MR_CAT()

```c
#define MR_CAT(...) \
    __MR_CAT(__MR_CAT_, MR_VA_ARGS_NUM(__VA_ARGS__))(__VA_ARGS__)
```

**功能**：连接多个标识符（Token Pasting）

**参数**：

- `__VA_ARGS__`：要连接的标识符

**返回值**：连接后的标识符

**示例**：

```c
/* 基本用法 */
#define PREFIX my_func_
#define NAME test

/* MR_CAT(PREFIX, NAME) 展开为 my_func_test */
void MR_CAT(PREFIX, NAME)(void) {
    printf("Function: my_func_test\n");
}

/* 多个参数 */
#define VAR(x, y) MR_CAT(x, _, y)
int VAR(counter, 1) = 10;  /* 展开为 counter_1 */
int VAR(counter, 2) = 20;  /* 展开为 counter_2 */
```

**实现原理**：

- 先计算参数数量
- 根据数量调用不同层级的宏
- 使用 `##` 运算符连接标识符

#### MR_CAT_SEP()

```c
#define MR_CAT_SEP(_s, ...) \
    MR_CAT(__MR_CAT_SEP_, MR_VA_ARGS_NUM(__VA_ARGS__))(_s, ##__VA_ARGS__)
```

**功能**：用分隔符连接多个标识符

**参数**：

- `_s`：分隔符
- `__VA_ARGS__`：要连接的标识符

**示例**：

```c
/* 使用下划线连接 */
MR_CAT_SEP(_, foo, bar, baz)  /* 展开为 foo_bar_baz */

/* 使用自定义分隔符 */
#define SEP _
MR_CAT_SEP(SEP, var, 1, 2, 3)  /* 展开为 var_1_2_3 */
```

### 2.6 字符串操作宏

#### MR_STR()

```c
#define MR_STR(_s) __MR_STR_IMPL(_s)
#define __MR_STR_IMPL(_s) #_s
```

**功能**：将宏转换为字符串（Stringification）

**参数**：

- `_s`：宏或值

**返回值**：字符串字面量

**示例**：

```c
#define VERSION 1.0.0
#define NAME mrX

printf("Version: " MR_STR(VERSION) "\n");  /* 输出: Version: 1.0.0 */
printf("Name: " MR_STR(NAME) "\n");        /* 输出: Name: mrX */

/* 用于调试信息 */
int line = __LINE__;
printf("Code at line: " MR_STR(__LINE__) "\n");  /* 输出行号 */
```

**实现原理**：

- 使用 `#` 预处理运算符将参数转换为字符串
- 双层宏确保参数先展开再字符串化

### 2.7 宏条件编译

#### MR_MACRO_IF()

```c
#define MR_MACRO_IF(_c, _if, _else) \
    __MR_MACRO_IF(MR_CAT(__MR_MACRO_IF_TRUE, _c), _if, _else)
```

**功能**：在宏展开时进行条件选择

**参数**：

- `_c`：条件（必须是一个值为 0 或 1 的宏）
- `_if`：条件为真时的代码
- `_else`：条件为假时的代码

**返回值**：选中的代码

**示例**：

```c
#define DEBUG 1

/* MR_MACRO_IF(DEBUG, debug_code, release_code) */
int value = MR_MACRO_IF(DEBUG, 10, 20);
/* 如果 DEBUG 定义为 1，value = 10 */
/* 如果 DEBUG 定义为 0，value = 20 */

/* 复杂示例 */
#define MR_MACRO_IF(DEBUG, \
    printf("Debug mode\n"), \
    printf("Release mode\n"))
```

**实现原理**：

- 通过符号拼接实现条件分支
- `_c` 为 1 时，`__MR_MACRO_IF_TRUE1` 被定义为 `__CONDTRUE1,`
- 逗号创造了额外参数，选中第一个分支
- 使用 `MR_DE_BRACKET` 去除保护性括号

#### MR_MACRO_IS_USED()

```c
#define MR_MACRO_IS_USED(_m) MR_MACRO_IF(_m, (1), (0))
```

**功能**：检查宏是否被定义且值为 1

**参数**：

- `_m`：宏名称

**返回值**：

- `1`：宏被定义且值为 1
- `0`：宏未定义或值不为 1

**示例**：

```c
#define USE_FEATURE_A 1
#define USE_FEATURE_B 0

#if MR_MACRO_IS_USED(USE_FEATURE_A)
    /* 编译这段代码 */
#endif

#if MR_MACRO_IS_USED(USE_FEATURE_B)
    /* 不编译这段代码 */
#endif
```

### 2.8 其他实用宏

#### MR_DE_BRACKET()

```c
#define MR_DE_BRACKET(...) __VA_ARGS__
```

**功能**：去除括号，展开参数

**示例**：

```c
/* 用于 MR_MACRO_IF 中去除保护性括号 */
MR_DE_BRACKET (code)  /* 展开为 code */
```

### 2.9 奇偶校验函数

#### mr_parity_even() / mr_parity_odd()

```c
mr_bool_t mr_parity_even(const void *src, mr_size_t size);
mr_bool_t mr_parity_odd(const void *src, mr_size_t size);
```

**功能**：计算数据的奇偶校验

**参数**：

- `src`：数据指针
- `size`：数据长度

**返回值**：

- `mr_parity_even`：偶数个 1 返回 `MR_TRUE`
- `mr_parity_odd`：奇数个 1 返回 `MR_TRUE`

**示例**：

```c
mr_uint8_t data1 = 0b01010101;  /* 4 个 1，偶数 */
mr_uint8_t data2 = 0b01010100;  /* 3 个 1，奇数 */

printf("Even: %d\n", mr_parity_even(&data1, 1));  /* 输出: 1 (true) */
printf("Odd: %d\n", mr_parity_odd(&data2, 1));    /* 输出: 1 (true) */
```

**实现原理**：

- 使用查表法计算半字节（4 位）的奇偶性
- 累加所有半字节的奇偶性
- 通过最低位判断最终奇偶性

---

## 3. 典型应用案例

### 案例 1：链表遍历与容器获取

```c
#include <mrX/kernel/mr_list.h>
#include <mrX/kernel/mr_service.h>

typedef struct {
    int id;
    char name[32];
    mr_list_t link;
} device_t;

void print_devices(mr_list_t *device_list) {
    mr_list_t *pos;

    MR_LIST_FOR_EACH(pos, device_list) {
        /* 从链表节点获取 device_t 结构体 */
        device_t *dev = MR_CONTAINER_OF(pos, device_t, link);
        printf("Device %d: %s\n", dev->id, dev->name);
    }
}
```

### 案例 2：内存对齐分配

```c
#include <mrX/kernel/mr_service.h>
#include <mrX/libc/mr_malloc.h>

#define DMA_ALIGNMENT 256

void *allocate_dma_buffer(size_t size) {
    /* 向上对齐到 DMA 边界 */
    size_t aligned_size = MR_ALIGN_UP(size, DMA_ALIGNMENT);
    void *ptr = mr_malloc(aligned_size);

    printf("Requested: %zu, Aligned: %zu\n", size, aligned_size);
    return ptr;
}

/* 使用示例 */
void example(void) {
    void *buf = allocate_dma_buffer(1000);
    /* 实际分配 1024 字节（256 的倍数） */
    mr_free(buf);
}
```

### 案例 3：宏定义的动态函数生成

```c
#include <mrX/kernel/mr_service.h>

/* 定义设备初始化函数 */
#define DEFINE_DEVICE_INIT(_name, _type) \
    void MR_CAT(device_init_, _name)(_type *dev) { \
        dev->magic = 0x1234; \
        dev->state = 0; \
        printf("Device " #_name " initialized\n"); \
    }

/* 生成多个设备初始化函数 */
typedef struct { int magic; int state; } uart_dev_t;
typedef struct { int magic; int state; } spi_dev_t;

DEFINE_DEVICE_INIT(uart, uart_dev_t)
DEFINE_DEVICE_INIT(spi, spi_dev_t)

/* 使用生成的函数 */
void example(void) {
    uart_dev_t uart;
    spi_dev_t spi;

    device_init_uart(&uart);  /* 调用生成的函数 */
    device_init_spi(&spi);
}
```

### 案例 4：条件编译和特性选择

```c
#include <mrX/kernel/mr_service.h>

#define CONFIG_FEATURE_A 1
#define CONFIG_FEATURE_B 0

/* 根据配置选择不同实现 */
#define MY_FUNCTION_IMPL \
    MR_MACRO_IF(CONFIG_FEATURE_A, \
        feature_a_implementation, \
        feature_b_implementation)

void feature_a_implementation(void) {
    printf("Using feature A\n");
}

void feature_b_implementation(void) {
    printf("Using feature B\n");
}

void my_function(void) {
    MY_FUNCTION_IMPL();
}
```

---

## 4. 设计思路

### 4.1 设计目标

service 模块的设计目标：

1. **零开销抽象**：所有操作在编译期完成，无运行时成本
2. **增强 C 语言能力**：提供 C++ 风格的模板元编程能力
3. **类型安全**：利用编译器类型检查
4. **高度可复用**：通用工具，适用于各种场景
5. **可移植性**：纯标准 C 代码

### 4.2 关键设计决策

#### 为什么使用宏而不是函数？

**宏的优势**：

1. **类型通用**：一个宏可以用于多种类型
2. **编译期计算**：常量折叠、大小计算在编译期完成
3. **无调用开销**：内联展开，无函数调用成本
4. **符号操作**：可以进行符号连接、字符串化等操作

**函数的限制**：

1. 类型固定
2. 运行时执行
3. 有调用开销
4. 不能操作符号

#### MR_CONTAINER_OF 的巧妙设计

```c
#define MR_CONTAINER_OF(ptr, type, member) \
    ((void *)((char *)(ptr) - (char *)&(((type *)0)->member)))
```

**关键技巧**：

1. 使用 `(type *)0` 访问空地址的成员（不实际解引用）
2. 通过 `&` 获取成员的偏移量
3. 转换为 `char *` 进行字节级指针运算
4. 减去偏移量得到结构体起始地址

#### 宏递归的实现

`MR_CAT` 和 `MR_VA_ARGS_NUM` 使用了宏递归技巧：

```c
/* 根据参数数量调用不同层级的宏 */
#define MR_CAT(...) \
    __MR_CAT(__MR_CAT_, MR_VA_ARGS_NUM(__VA_ARGS__))(__VA_ARGS__)

/* 每个层级处理不同数量的参数 */
#define __MR_CAT_2(_a, ...) __MR_CAT(_a, __MR_CAT_1(__VA_ARGS__))
#define __MR_CAT_3(_a, ...) __MR_CAT(_a, __MR_CAT_2(__VA_ARGS__))
/* ... */
```

**工作原理**：

1. 先计算参数数量（`MR_VA_ARGS_NUM`）
2. 拼接对应层级的宏名（如 `__MR_CAT_3`）
3. 调用对应层级的实现

---

## 5. 参考资料

### 5.1 设计参考

- **Linux kernel.h**：Linux 内核的通用工具宏
- **GLib**：GNOME 的通用工具库

### 5.2 延伸阅读

- **C 预处理器技巧**：宏的高级用法
- **复合字面量**：C99 的特性
- **Token Pasting**：符号连接运算符 `##`
- **宏递归**：如何用宏实现递归逻辑
- **内联函数 vs 宏**：两者的权衡

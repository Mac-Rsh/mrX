# check - 检查宏

## 1. 模块概述

### 1.1 功能介绍

check 模块提供单元测试中的检查宏（Test Check Macros），用于在测试用例中进行条件验证。它提供了一组宏用于比较不同类型的值，并根据验证结果更新测试状态。

该模块是 mrX 测试框架的基础组件，与 mr_test.h 配合使用完成单元测试。

### 1.2 主要特性

- **类型支持**：支持整数、无符号整数、指针、浮点数、双精度浮点数
- **比较操作**：支持相等、不等、小于、小于等于、大于、大于等于
- **浮点数近似**：支持浮点数的近似比较（NEAR）
- **两种语义**：EXPECT（失败继续）和 ASSERT（失败返回）

### 1.3 适用场景

check 模块适用于以下场景：

- **单元测试**：验证函数返回值
- **边界测试**：检查边界条件
- **状态验证**：验证状态机状态
- **数值计算**：验证计算结果

---

## 2. 使用指南

### 2.1 核心机制

#### 测试状态变量

```c
extern volatile mr_uint32_t __mr_test_status;
```

**说明**：全局测试状态变量

**魔数**：

```c
#define __MR_TEST_MAGIC_PASS            (0x70617373U)
```

- `0x70617373` = "pass" 的 ASCII 码（小端序）
- 测试通过时保持此魔数
- 测试失败时设置为 `~__MR_TEST_MAGIC_PASS`

### 2.2 EXPECT 宏 - 失败继续

EXPECT 宏在断言失败时记录错误，但继续执行后续代码。

#### 相等比较

```c
void MR_TEST_EXPECT_EQ_INT(_a, _b);    /* 有符号整数相等 */
void MR_TEST_EXPECT_EQ_UINT(_a, _b);   /* 无符号整数相等 */
void MR_TEST_EXPECT_EQ_PTR(_a, _b);    /* 指针相等 */
void MR_TEST_EXPECT_EQ_FLOAT(_a, _b);   /* 浮点数近似相等 (误差 <= 1e-4) */
void MR_TEST_EXPECT_EQ_DOUBLE(_a, _b);  /* 双精度近似相等 (误差 <= 1e-8) */
```

**示例**：

```c
#include <mrX/test/mr_test.h>

void test_addition(void) {
    int result = add(2, 3);

    /* 验证结果 */
    MR_TEST_EXPECT_EQ_INT(result, 5);
    MR_TEST_EXPECT_EQ_INT(add(0, 0), 0);
    MR_TEST_EXPECT_EQ_INT(add(-1, 1), 0);

    /* 所有断言都会执行，即使前面有失败 */
}
```

#### 不等比较

```c
void MR_TEST_EXPECT_NE_INT(_a, _b);
void MR_TEST_EXPECT_NE_UINT(_a, _b);
void MR_TEST_EXPECT_NE_PTR(_a, _b);
void MR_TEST_EXPECT_NE_FLOAT(_a, _b);
void MR_TEST_EXPECT_NE_DOUBLE(_a, _b);
```

**示例**：

```c
void test_division(void) {
    int result = divide(10, 3);

    /* 验证不等于某个值 */
    MR_TEST_EXPECT_NE_INT(result, 4);
    MR_TEST_EXPECT_NE_PTR(result, MR_NULL);
}
```

#### 小于/小于等于比较

```c
void MR_TEST_EXPECT_LT_INT(_a, _b);
void MR_TEST_EXPECT_LT_UINT(_a, _b);
void MR_TEST_EXPECT_LT_PTR(_a, _b);
void MR_TEST_EXPECT_LT_FLOAT(_a, _b);
void MR_TEST_EXPECT_LT_DOUBLE(_a, _b);

void MR_TEST_EXPECT_LE_INT(_a, _b);
void MR_TEST_EXPECT_LE_UINT(_a, _b);
void MR_TEST_EXPECT_LE_PTR(_a, _b);
void MR_TEST_EXPECT_LE_FLOAT(_a, _b);
void MR_TEST_EXPECT_LE_DOUBLE(_a, _b);
```

**示例**：

```c
void test_bounds(void) {
    int value = get_value();

    /* 验证范围 */
    MR_TEST_EXPECT_GE_INT(value, 0);   /* value >= 0 */
    MR_TEST_EXPECT_LT_INT(value, 100); /* value < 100 */
}
```

#### 大于/大于等于比较

```c
void MR_TEST_EXPECT_GT_INT(_a, _b);
void MR_TEST_EXPECT_GT_UINT(_a, _b);
void MR_TEST_EXPECT_GT_PTR(_a, _b);
void MR_TEST_EXPECT_GT_FLOAT(_a, _b);
void MR_TEST_EXPECT_GT_DOUBLE(_a, _b);

void MR_TEST_EXPECT_GE_INT(_a, _b);
void MR_TEST_EXPECT_GE_UINT(_a, _b);
void MR_TEST_EXPECT_GE_PTR(_a, _b);
void MR_TEST_EXPECT_GE_FLOAT(_a, _b);
void MR_TEST_EXPECT_GE_DOUBLE(_a, _b);
```

#### 近似比较

```c
void MR_TEST_EXPECT_NEAR(_a, _b, _epsilon);
```

**功能**：验证 `|_a - _b| <= _epsilon`

**参数**：

- `_a`：第一个值（浮点类型）
- `_b`：第二个值（浮点类型）
- `_epsilon`：允许的误差范围

**示例**：

```c
void test_float_calculation(void) {
    float result = calculate_pi();

    /* π 的近似值，允许 0.001 的误差 */
    MR_TEST_EXPECT_NEAR(result, 3.14159f, 0.001f);
}
```

### 2.3 ASSERT 宏 - 失败返回

ASSERT 宏在断言失败时记录错误并立即从测试函数返回。

**注意**：ASSERT 宏只能用于返回 `void` 的测试函数。

#### 相等比较

```c
void MR_TEST_ASSERT_EQ_INT(_a, _b);
void MR_TEST_ASSERT_EQ_UINT(_a, _b);
void MR_TEST_ASSERT_EQ_PTR(_a, _b);
void MR_TEST_ASSERT_EQ_FLOAT(_a, _b);
void MR_TEST_ASSERT_EQ_DOUBLE(_a, _b);
```

**示例**：

```c
void test_memory_allocation(void) {
    void *ptr = mr_malloc(1024);

    /* 如果分配失败，立即返回，不执行后续代码 */
    MR_TEST_ASSERT_EQ_PTR(ptr, MR_NULL);

    /* 只有分配成功时才会执行到这里 */
    MR_TEST_ASSERT_NE_PTR(ptr, MR_NULL);

    /* 使用内存... */
    ((mr_uint8_t *)ptr)[0] = 0x42;

    mr_free(ptr);
}
```

#### 不等、比较、近似比较

```c
/* 不等 */
void MR_TEST_ASSERT_NE_INT(_a, _b);
void MR_TEST_ASSERT_NE_UINT(_a, _b);
void MR_TEST_ASSERT_NE_PTR(_a, _b);
void MR_TEST_ASSERT_NE_FLOAT(_a, _b);
void MR_TEST_ASSERT_NE_DOUBLE(_a, _b);

/* 小于 */
void MR_TEST_ASSERT_LT_INT(_a, _b);
void MR_TEST_ASSERT_LT_UINT(_a, _b);
void MR_TEST_ASSERT_LT_PTR(_a, _b);
void MR_TEST_ASSERT_LT_FLOAT(_a, _b);
void MR_TEST_ASSERT_LT_DOUBLE(_a, _b);

/* 小于等于 */
void MR_TEST_ASSERT_LE_INT(_a, _b);
void MR_TEST_ASSERT_LE_UINT(_a, _b);
void MR_TEST_ASSERT_LE_PTR(_a, _b);
void MR_TEST_ASSERT_LE_FLOAT(_a, _b);
void MR_TEST_ASSERT_LE_DOUBLE(_a, _b);

/* 大于 */
void MR_TEST_ASSERT_GT_INT(_a, _b);
void MR_TEST_ASSERT_GT_UINT(_a, _b);
void MR_TEST_ASSERT_GT_PTR(_a, _b);
void MR_TEST_ASSERT_GT_FLOAT(_a, _b);
void MR_TEST_ASSERT_GT_DOUBLE(_a, _b);

/* 大于等于 */
void MR_TEST_ASSERT_GE_INT(_a, _b);
void MR_TEST_ASSERT_GE_UINT(_a, _b);
void MR_TEST_ASSERT_GE_PTR(_a, _b);
void MR_TEST_ASSERT_GE_FLOAT(_a, _b);
void MR_TEST_ASSERT_GE_DOUBLE(_a, _b);

/* 近似 */
void MR_TEST_ASSERT_NEAR(_a, _b, _epsilon);
```

### 2.4 配置模式

check 模块通过 `MR_USE_TEST` 配置开关控制：

```c
#if defined(MR_USE_TEST)
    /* 测试代码启用 */
#else
    /* 测试代码不编译 */
#endif
```

### 2.5 典型应用案例

#### 案例 1：基本功能测试

```c
#include <mrX/test/mr_test.h>
#include <mrX/libc/mr_string.h>

void test_strlen(void) {
    /* 空字符串 */
    MR_TEST_EXPECT_EQ_INT(mr_strlen(""), 0);

    /* 普通字符串 */
    MR_TEST_EXPECT_EQ_INT(mr_strlen("hello"), 5);

    /* 长字符串 */
    MR_TEST_EXPECT_EQ_INT(mr_strlen("hello, world!"), 13);
}

void test_strcmp(void) {
    /* 相等 */
    MR_TEST_EXPECT_EQ_INT(mr_strcmp("abc", "abc"), 0);

    /* 小于 */
    MR_TEST_EXPECT_LT_INT(mr_strcmp("abc", "abd"), 0);

    /* 大于 */
    MR_TEST_EXPECT_GT_INT(mr_strcmp("abd", "abc"), 0);
}
```

#### 案例 2：边界条件测试

```c
void test_array_bounds(void) {
    int array[10];

    /* 测试边界 */
    for (int i = 0; i < 10; i++) {
        array[i] = i;
    }

    MR_TEST_EXPECT_EQ_INT(array[0], 0);
    MR_TEST_EXPECT_EQ_INT(array[9], 9);
}

void test_overflow(void) {
    mr_uint8_t value = 255;

    /* 溢出测试 */
    MR_TEST_EXPECT_EQ_UINT(value + 1, 0);
}
```

#### 案例 3：指针测试

```c
void test_memory_operations(void) {
    void *ptr1 = mr_malloc(100);
    void *ptr2 = mr_malloc(100);

    /* 分配成功 */
    MR_TEST_ASSERT_NE_PTR(ptr1, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(ptr2, MR_NULL);

    /* 指针不同 */
    MR_TEST_EXPECT_NE_PTR(ptr1, ptr2);

    /* 释放后再使用是危险的，用 ASSERT 确保不会继续 */
    mr_free(ptr1);
    mr_free(ptr2);
}

void test_null_handling(void) {
    void *result = allocate_optional();

    /* 结果可能为 NULL */
    if (result != MR_NULL) {
        MR_TEST_EXPECT_EQ_PTR(result, expected_pointer);
    }
}
```

#### 案例 4：浮点数测试

```c
void test_math_functions(void) {
    float sin_pi = mr_sin(MR_PI);
    float cos_pi = mr_cos(MR_PI);

    /* 浮点数使用近似比较 */
    MR_TEST_EXPECT_NEAR(sin_pi, 0.0f, 1e-4f);
    MR_TEST_EXPECT_NEAR(cos_pi, -1.0f, 1e-4f);
}

void test_temperature_conversion(void) {
    float celsius = 25.0f;
    float fahrenheit = celsius_to_fahrenheit(celsius);

    /* F = C * 9/5 + 32 = 25 * 1.8 + 32 = 77 */
    MR_TEST_EXPECT_NEAR(fahrenheit, 77.0f, 0.01f);
}
```

#### 案例 5：使用 ASSERT 进行前置条件检查

```c
void test_device_operation(void) {
    device_t *dev = device_acquire();

    /* 确保设备获取成功，否则无法继续 */
    MR_TEST_ASSERT_NE_PTR(dev, MR_NULL);

    /* 前置条件满足，继续测试 */
    MR_TEST_EXPECT_EQ_INT(device_init(dev), 0);

    mr_int32_t value = device_read(dev);
    MR_TEST_EXPECT_GE_INT(value, 0);

    device_release(dev);
}
```

---

## 3. 设计思路

### 3.1 设计目标

check 模块的设计目标：

1. **简洁性**：宏定义简单易用
2. **类型安全**：不同类型使用不同宏
3. **可移植性**：使用标准 printf 进行输出
4. **灵活性**：提供 EXPECT 和 ASSERT 两种语义

### 3.2 实现原理

#### 检查宏展开

```c
#define __MR_TEST_CHECK(_a, _cond, _b, _type, _fmt)                            \
    do {                                                                       \
        _type __a = (_type)(_a);                                               \
        _type __b = (_type)(_b);                                               \
        if (!(__a _cond __b)) {                                                \
            __MR_TEST_PRINTF(MR_STR(_a) " " MR_STR(_cond) " " MR_STR(_b),      \
                             MR_STR(_a), __a, MR_STR(_b), __b, _fmt);          \
            __mr_test_status = ~__MR_TEST_MAGIC_PASS;                          \
        }                                                                      \
    } while (0)
```

**工作流程**：

1. 将参数转换为指定类型
2. 执行比较操作
3. 如果比较失败：
   - 打印失败信息（文件名、行号、表达式、实际值）
   - 设置测试状态为失败

#### 浮点数近似比较

```c
#define __MR_TEST_CHECK_NEAR(_a, _b, _e, _type, _fmt)                          \
    do {                                                                       \
        _type __a = (_type)(_a);                                               \
        _type __b = (_type)(_b);                                               \
        _type __e = (_type)(_e);                                               \
        _type __c = (__a > __b) ? (__a - __b) : (__b - __a);                   \
        if (!(__c <= __e)) {                                                   \
            __MR_TEST_PRINTF(                                                  \
                "| " MR_STR(_a) " - " MR_STR(_b) " | <= " MR_STR(_e),          \
                MR_STR(_a), __a, MR_STR(_b), __b, _fmt);                       \
            __mr_test_status = ~__MR_TEST_MAGIC_PASS;                          \
        }                                                                      \
    } while (0)
```

**原理**：计算 `|a - b|`，检查是否小于等于误差 `e`

#### EXPECT vs ASSERT

```c
#define __MR_TEST_EXPECT(_check)        _check

#define __MR_TEST_ASSERT(_check)                                               \
    do {                                                                       \
        mr_uint32_t __s = __mr_test_status;                                    \
        __mr_test_status = __MR_TEST_MAGIC_PASS;                               \
        _check;                                                                \
        if (__mr_test_status == (~__MR_TEST_MAGIC_PASS)) {                     \
            return;                                                            \
        }                                                                      \
        __mr_test_status = __s;                                                \
    } while (0)
```

**区别**：

| 特性 | EXPECT | ASSERT |
|------|--------|--------|
| 失败行为 | 继续执行 | 立即返回 |
| 状态处理 | 直接修改状态 | 保存/恢复状态 |
| 适用场景 | 收集多个验证 | 前置条件检查 |

### 3.3 设计权衡

#### 为什么需要两种断言？

**EXPECT 模式**：
- 适合收集多个验证结果
- 一次性报告所有失败
- 适合独立的验证点

**ASSERT 模式**：
- 防止在错误状态下继续执行
- 避免级联失败
- 适合前置条件检查

#### 为什么浮点数使用近似比较？

浮点数由于精度问题，直接比较相等不可靠：

```c
/* 危险！ */
MR_TEST_EXPECT_EQ_FLOAT(0.1f + 0.2f, 0.3f);  /* 可能失败 */

/* 安全 */
MR_TEST_EXPECT_NEAR(0.1f + 0.2f, 0.3f, 1e-6f);
```

#### 宏展开的问题

**do-while(0) 包装**：

```c
#define MACRO(x) do { something(x); } while(0)
```

**作用**：

1. 使宏可以像函数一样使用（加分号）
2. 防止 if-else 悬挂问题

```c
/* 没有保护的情况 */
if (condition)
    MR_TEST_EXPECT_EQ_INT(a, b);  /* 有问题 */
else
    other_func();

/* 使用 do-while(0) 保护 */
if (condition)
    MR_TEST_EXPECT_EQ_INT(a, b);  /* 正常 */
else
    other_func();
```

---

## 4. 参考资料

### 4.1 相关模块

check 模块与以下模块相关：

- **test**：测试框架，提供测试用例管理
- **printf**：输出函数，用于打印失败信息
- **service**：通用工具，提供 MR_STR、MR_CAT 等宏

### 4.2 测试模式

- **单元测试**：测试单个函数
- **集成测试**：测试模块间交互
- **回归测试**：确保修改不引入新问题

### 4.3 延伸阅读

- **测试驱动开发 (TDD)**：先写测试，再写代码
- **测试覆盖率**：衡量测试完整性
- **Mock 和 Stub**：隔离外部依赖的测试技术

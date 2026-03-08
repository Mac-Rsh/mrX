# test - 测试框架

## 1. 模块概述

### 1.1 功能介绍

test 模块提供轻量级的单元测试框架，支持测试用例的定义、导出和自动执行。它基于链接段机制实现测试用例的自动发现和运行，无需手动注册每个测试用例。

该框架包含完整的检查宏系统（参见 check.md），支持多种类型的条件验证。

### 1.2 主要特性

- **自动发现**：基于链接段自动收集测试用例
- **分级组织**：支持测试套件（suite）和测试用例（testcase）两级组织
- **选择性运行**：可运行全部、指定套件或单个测试用例
- **灵活断言**：提供 EXPECT 和 ASSERT 两种断言语义
- **类型丰富**：支持整数、指针、浮点数等多种类型的比较

### 1.3 适用场景

test 模块适用于以下场景：

- **单元测试**：测试单个函数的功能
- **回归测试**：确保代码修改不破坏现有功能
- **持续集成**：自动化测试流程
- **驱动开发**：TDD（测试驱动开发）

---

## 2. 使用指南

### 2.1 核心数据结构

#### mr_testcase_entry_t - 测试用例函数类型

```c
typedef void (mr_testcase_entry)(void);
```

**说明**：测试用例函数的类型定义

**特点**：

- 无参数
- 无返回值
- 通过检查宏报告结果

#### mr_testcase_t - 测试用例结构

```c
typedef struct mr_testcase {
    const char *suite;           /* 测试套件名称 */
    const char *name;            /* 测试用例名称 */
    mr_testcase_entry *entry;    /* 测试函数指针 */
} mr_testcase_t;
```

**成员说明**：

- `suite`：测试套件名称，用于组织相关测试
- `name`：测试用例名称，描述具体测试内容
- `entry`：指向测试函数的指针

### 2.2 测试用例导出

#### MR_TEST_EXPORT() - 导出测试用例

```c
#define MR_TEST_EXPORT(_suite, _name, _entry)
```

**功能**：将测试用例导出到特定的链接段

**参数**：

- `_suite`：测试套件名称（字符串字面量）
- `_name`：测试用例名称（字符串字面量）
- `_entry`：测试函数名

**实现原理**：

```c
#define __MR_TEST_EXPORT(_suite, _name, _entry, _level)                        \
    MR_USED const mr_testcase_t MR_CAT(__mr_testcase_, _suite, _, _name)       \
        MR_SECTION_ALIGN("mr_testcase." _level)                                \
        = {.suite = MR_STR(_suite), .name = MR_STR(_name), .entry = (_entry)}
```

**示例**：

```c
#include <mrX/test/mr_test.h>

/* 测试函数定义 */
void test_string_length(void) {
    MR_TEST_EXPECT_EQ_INT(mr_strlen("hello"), 5);
    MR_TEST_EXPECT_EQ_INT(mr_strlen(""), 0);
}

/* 导出测试用例 */
MR_TEST_EXPORT(string, length, test_string_length);
```

**链接段布局**：

```
内存布局
    ↓
.mr_testcase.1 段
    ├── __mr_testcase_string_length
    ├── __mr_testcase_string_compare
    ├── __mr_testcase_memory_malloc
    └── ...
```

### 2.3 测试运行

#### mr_testcase_run() - 运行单个测试用例

```c
void mr_testcase_run(const char *suite, const char *name);
```

**功能**：运行指定套件和名称的测试用例

**参数**：

- `suite`：测试套件名称
- `name`：测试用例名称

**示例**：

```c
/* 运行 string 套件中的 length 测试 */
mr_testcase_run("string", "length");
```

#### mr_testsuite_run() - 运行测试套件

```c
void mr_testsuite_run(const char *suite);
```

**功能**：运行指定套件中的所有测试用例

**参数**：

- `suite`：测试套件名称

**示例**：

```c
/* 运行 string 套件中的所有测试 */
mr_testsuite_run("string");
```

#### mr_test_run() - 运行所有测试

```c
mr_bool_t mr_test_run(void);
```

**功能**：运行所有导出的测试用例

**返回值**：

- `MR_TRUE`：所有测试通过
- `MR_FALSE`：有测试失败

**示例**：

```c
int main(void) {
    /* 初始化系统 */
    system_init();

    /* 运行所有测试 */
    if (mr_test_run()) {
        printf("All tests passed!\n");
    } else {
        printf("Some tests failed!\n");
    }

    return 0;
}
```

### 2.4 配置模式

test 模块通过 `MR_USE_TEST` 配置开关控制：

```c
#if defined(MR_USE_TEST)
    /* 测试框架启用 */
#else
    /* 测试框架不编译 */
#endif
```

**配置方式**：

在 Kconfig 中启用：
```
MR_USE_TEST=y
```

### 2.5 典型应用案例

#### 案例 1：基础测试套件

```c
#include <mrX/test/mr_test.h>
#include <mrX/libc/mr_string.h>

/* ========== 测试函数 ========== */

void test_strlen_empty(void) {
    MR_TEST_EXPECT_EQ_INT(mr_strlen(""), 0);
}

void test_strlen_normal(void) {
    MR_TEST_EXPECT_EQ_INT(mr_strlen("hello"), 5);
    MR_TEST_EXPECT_EQ_INT(mr_strlen("world"), 5);
}

void test_strcmp_equal(void) {
    MR_TEST_EXPECT_EQ_INT(mr_strcmp("abc", "abc"), 0);
}

void test_strcmp_not_equal(void) {
    MR_TEST_EXPECT_LT_INT(mr_strcmp("abc", "abd"), 0);
    MR_TEST_EXPECT_GT_INT(mr_strcmp("xyz", "abc"), 0);
}

/* ========== 测试导出 ========== */

MR_TEST_EXPORT(string, strlen_empty, test_strlen_empty);
MR_TEST_EXPORT(string, strlen_normal, test_strlen_normal);
MR_TEST_EXPORT(string, strcmp_equal, test_strcmp_equal);
MR_TEST_EXPORT(string, strcmp_not_equal, test_strcmp_not_equal);
```

#### 案例 2：内存管理测试

```c
#include <mrX/test/mr_test.h>
#include <mrX/libc/mr_malloc.h>

void test_malloc_free(void) {
    void *ptr = mr_malloc(1024);

    MR_TEST_ASSERT_NE_PTR(ptr, MR_NULL);

    mr_free(ptr);
}

void test_calloc_zero(void) {
    mr_uint32_t *ptr = mr_calloc(10, sizeof(mr_uint32_t));

    MR_TEST_ASSERT_NE_PTR(ptr, MR_NULL);

    /* 验证清零 */
    for (int i = 0; i < 10; i++) {
        MR_TEST_EXPECT_EQ_UINT(ptr[i], 0);
    }

    mr_free(ptr);
}

void test_realloc(void) {
    mr_uint8_t *ptr = mr_malloc(100);

    MR_TEST_ASSERT_NE_PTR(ptr, MR_NULL);

    ptr[0] = 0x42;
    ptr[99] = 0xFF;

    /* 重新分配 */
    mr_uint8_t *new_ptr = mr_realloc(ptr, 200);

    MR_TEST_ASSERT_NE_PTR(new_ptr, MR_NULL);

    /* 验证数据保留 */
    MR_TEST_EXPECT_EQ_UINT(new_ptr[0], 0x42);
    MR_TEST_EXPECT_EQ_UINT(new_ptr[99], 0xFF);

    mr_free(new_ptr);
}

MR_TEST_EXPORT(memory, malloc_free, test_malloc_free);
MR_TEST_EXPORT(memory, calloc_zero, test_calloc_zero);
MR_TEST_EXPORT(memory, realloc, test_realloc);
```

#### 案例 3：链表测试

```c
#include <mrX/test/mr_test.h>
#include <mrX/kernel/mr_list.h>

typedef struct {
    int value;
    mr_list_t node;
} test_node_t;

void test_list_init(void) {
    mr_list_t list;

    mr_list_init(&list);

    MR_TEST_EXPECT_EQ_INT(mr_list_is_empty(&list), MR_TRUE);
}

void test_list_add_remove(void) {
    mr_list_t list;
    test_node_t node1 = {.value = 1};
    test_node_t node2 = {.value = 2};

    mr_list_init(&list);

    /* 添加节点 */
    mr_list_add(&list, &node1.node);
    MR_TEST_EXPECT_EQ_INT(mr_list_is_empty(&list), MR_FALSE);

    mr_list_add(&list, &node2.node);

    /* 移除节点 */
    mr_list_del(&node1.node);
    MR_TEST_EXPECT_EQ_INT(list.next == &node2.node, MR_TRUE);

    mr_list_del(&node2.node);
    MR_TEST_EXPECT_EQ_INT(mr_list_is_empty(&list), MR_TRUE);
}

MR_TEST_EXPORT(list, init, test_list_init);
MR_TEST_EXPORT(list, add_remove, test_list_add_remove);
```

#### 案例 4：FIFO 测试

```c
#include <mrX/test/mr_test.h>
#include <mrX/kernel/mr_fifo.h>

#define FIFO_SIZE  16

void test_fifo_init(void) {
    mr_uint8_t buffer[FIFO_SIZE];
    mr_fifo_t fifo;

    mr_fifo_init(&fifo, buffer, FIFO_SIZE);

    MR_TEST_EXPECT_EQ_INT(mr_fifo_is_empty(&fifo), MR_TRUE);
    MR_TEST_EXPECT_EQ_INT(mr_fifo_is_full(&fifo), MR_FALSE);
}

void test_fifo_push_pop(void) {
    mr_uint8_t buffer[FIFO_SIZE];
    mr_fifo_t fifo;
    mr_uint8_t data_out;

    mr_fifo_init(&fifo, buffer, FIFO_SIZE);

    /* 压入数据 */
    MR_TEST_EXPECT_EQ_INT(mr_fifo_push(&fifo, 0x42), 1);
    MR_TEST_EXPECT_EQ_INT(mr_fifo_len(&fifo), 1);

    MR_TEST_EXPECT_EQ_INT(mr_fifo_push(&fifo, 0x55), 1);
    MR_TEST_EXPECT_EQ_INT(mr_fifo_len(&fifo), 2);

    /* 弹出数据 */
    MR_TEST_EXPECT_EQ_INT(mr_fifo_pop(&fifo, &data_out), 1);
    MR_TEST_EXPECT_EQ_UINT(data_out, 0x42);
    MR_TEST_EXPECT_EQ_INT(mr_fifo_len(&fifo), 1);

    MR_TEST_EXPECT_EQ_INT(mr_fifo_pop(&fifo, &data_out), 1);
    MR_TEST_EXPECT_EQ_UINT(data_out, 0x55);
    MR_TEST_EXPECT_EQ_INT(mr_fifo_is_empty(&fifo), MR_TRUE);
}

MR_TEST_EXPORT(fifo, init, test_fifo_init);
MR_TEST_EXPORT(fifo, push_pop, test_fifo_push_pop);
```

#### 案例 5：完整测试主程序

```c
#include <mrX/test/mr_test.h>
#include <mrX/kernel/mr_printf.h>

int main(void) {
    int failed = 0;

    mr_printf("========================================\n");
    mr_printf("       mrX Framework Test Suite        \n");
    mr_printf("========================================\n\n");

    /* 运行所有测试 */
    if (!mr_test_run()) {
        failed = 1;
        mr_printf("\n!!! TESTS FAILED !!!\n");
    } else {
        mr_printf("\n*** ALL TESTS PASSED ***\n");
    }

    mr_printf("========================================\n");

    return failed;
}
```

#### 案例 6：选择运行特定测试

```c
int main(void) {
    /* 只运行 string 套件的测试 */
    mr_printf("Running string tests...\n");
    mr_testsuite_run("string");

    /* 或者运行单个测试 */
    mr_printf("Running memory.malloc_free...\n");
    mr_testcase_run("memory", "malloc_free");

    return 0;
}
```

---

## 3. 设计思路

### 3.1 设计目标

test 模块的设计目标：

1. **轻量级**：最小化代码占用，适合资源受限系统
2. **易用性**：简单的宏定义，快速上手
3. **自动化**：基于链接段自动发现测试用例
4. **可移植性**：使用标准 C 实现

### 3.2 实现原理

#### 链接段机制

测试框架利用链接器段功能自动收集测试用例：

```
+------------------+     +------------------+     +------------------+
| __mr_testcase_   |     | __mr_testcase_   |     | __mr_testcase_   |
| string_length    |     | string_compare   |     | memory_malloc    |
+------------------+     +------------------+     +------------------+
         |                        |                        |
         +------------------------+------------------------+
                                    |
                          .mr_testcase.1 段
```

**链接脚本配置**（示例）：

```ld
SECTIONS {
    .mr_testcase : {
        __mr_testcase_start = .;
        KEEP(*(.mr_testcase.*))
        __mr_testcase_end = .;
    }
}
```

**自动发现**：

```c
void mr_test_run(void) {
    mr_testcase_t *test;

    /* 遍历段中的所有测试用例 */
    for (test = __mr_testcase_start; test < __mr_testcase_end; test++) {
        mr_printf("Running: %s.%s\n", test->suite, test->name);
        test->entry();
    }
}
```

#### 测试状态管理

```c
extern volatile mr_uint32_t __mr_test_status;
```

**状态转换**：

```
MR_TEST_MAGIC_PASS (0x70617373)
         ↓ 检查失败
~MR_TEST_MAGIC_PASS
         ↓
测试标记为失败
```

#### 测试用例执行流程

```
开始测试
    ↓
重置测试状态 = MR_TEST_MAGIC_PASS
    ↓
调用测试函数
    ↓
检查失败？
    ├─ 是 → 设置状态 = ~MR_TEST_MAGIC_PASS
    └─ 否 → 保持状态
    ↓
返回测试结果
```

### 3.3 设计权衡

#### 为什么使用链接段？

| 方式 | 优点 | 缺点 |
|------|------|------|
| 链接段 | 自动收集、无需注册、零开销 | 需要链接脚本支持 |
| 手动注册 | 简单、无特殊要求 | 容易遗漏、代码冗余 |
| 宏注册 | 自动化 | 需要构造数组 |

**结论**：链接段提供了最佳的自动化体验

#### 测试命名规范

建议的命名规范：

```c
/* 测试函数：test_<模块>_<功能> */
void test_string_length(void);
void test_memory_allocation(void);

/* 测试套件：使用模块名 */
MR_TEST_EXPORT(string, length, test_string_length);
MR_TEST_EXPORT(memory, allocation, test_memory_allocation);
```

#### 测试用例独立性

每个测试用例应该：

1. **独立运行**：不依赖其他测试的执行顺序
2. **可重复**：多次运行结果一致
3. **自包含**：自己准备和清理资源

```c
/* 好的做法 */
void test_with_setup_teardown(void) {
    /* 准备 */
    resource_t *res = resource_create();
    MR_TEST_ASSERT_NE_PTR(res, MR_NULL);

    /* 测试 */
    MR_TEST_EXPECT_EQ_INT(resource_operation(res), 0);

    /* 清理 */
    resource_destroy(res);
}

/* 不好的做法：依赖全局状态 */
resource_t *g_res;  /* 共享状态 */

void test_a(void) {
    g_res = resource_create();  /* 污染全局状态 */
}

void test_b(void) {
    /* 依赖 test_a 的执行顺序 */
    MR_TEST_EXPECT_NE_PTR(g_res, MR_NULL);
}
```

---

## 4. 参考资料

### 4.1 相关模块

test 模块与以下模块相关：

- **check**：检查宏系统，提供断言功能
- **printf**：输出函数，用于打印测试信息
- **compiler**：编译器抽象，提供段属性宏
- **service**：通用工具，提供 MR_STR、MR_CAT 等宏

### 4.2 测试组织

推荐的测试目录结构：

```
tests/
├── test_string.c      /* 字符串测试 */
├── test_memory.c      /* 内存测试 */
├── test_list.c        /* 链表测试 */
├── test_fifo.c        /* FIFO 测试 */
└── main.c             /* 测试主程序 */
```

### 4.3 延伸阅读

- **单元测试最佳实践**：如何编写有效的单元测试
- **测试覆盖率**：衡量测试完整性的指标
- **TDD（测试驱动开发）**：先写测试再写代码的开发方法
- **持续集成（CI）**：自动化测试和构建流程

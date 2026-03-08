# malloc - 内存管理

## 1. 模块概述

### 1.1 功能介绍

malloc 模块提供动态内存分配和管理功能，为资源受限的嵌入式系统提供轻量级的堆内存管理器。它支持标准的内存分配接口（malloc、free、calloc、realloc），并提供内存区域添加和堆信息查询等扩展功能。

该模块支持两种配置模式：STD 模式（使用标准库）和自定义模式（使用内置内存管理器）。

### 1.2 主要特性

- **标准接口**：提供 malloc、free、calloc、realloc 标准接口
- **多内存区域**：支持添加多个内存区域作为堆空间
- **内存统计**：提供堆信息查询（总量、使用量、碎片率等）
- **魔数保护**：使用魔数检测堆块损坏
- **配置灵活**：可配置堆大小和内存区域

### 1.3 适用场景

malloc 模块适用于以下场景：

- **动态缓冲区**：需要运行时分配的缓冲区
- **数据结构**：链表、树等动态数据结构
- **消息传递**：动态分配的消息缓冲区
- **资源受限**：RAM 有限的嵌入式系统

---

## 2. 使用指南

### 2.1 核心数据结构

#### mr_heap_block_t - 堆块

```c
#if defined(MR_USE_MALLOC)
typedef struct mr_heap_block {
    volatile mr_uint32_t magic;    /* 魔数 */
    struct mr_heap_block *next;    /* 下一个堆块 */
    mr_size_t size;                /* 堗块大小 */
} mr_heap_block_t;
```

**成员说明**：

- `magic`：魔数（`MR_HEAP_MAGIC = 0x6d725800`），用于检测堆块损坏
- `next`：指向下一个堆块的指针（用于空闲链表管理）
- `size`：堆块大小（包含头部信息）

**布局**：

```
+------------------+------------------+------------------+
| heap_block_t     |  用户数据区域     |  (可能的对齐填充)  |
| (16字节)         |  (size - 16)     |                   |
+------------------+------------------+------------------+
   ^                                        ^
   |                                        |
   ptr (返回给用户)                    堆块结束
```

#### mr_heap_region_t - 堆区域

```c
typedef struct mr_heap_region {
    void *address;    /* 区域起始地址 */
    mr_size_t size;   /* 区域大小 */
} mr_heap_region_t;
```

**用途**：定义一个内存区域作为堆空间

#### mr_heap_info_t - 堆信息

```c
typedef struct mr_heap_info {
    mr_size_t total_bytes;    /* 总字节数 */
    mr_size_t used_bytes;     /* 已用字节数 */
    mr_size_t free_bytes;     /* 空闲字节数 */
    mr_size_t fail_count;     /* 分配失败次数 */
    mr_size_t frag_num;       /* 碎片数量 */
    mr_size_t max_free_bytes; /* 最大空闲块 */
    mr_size_t disp_kb_ratio;  /* 已用比例 (‰) */
    mr_size_t frag_ratio;     /* 碎片率 (‰) */
} mr_heap_info_t;
```

### 2.2 主要接口说明

#### mr_malloc() - 分配内存

```c
void *mr_malloc(mr_size_t size);
```

**功能**：分配指定大小的内存块

**参数**：

- `size`：要分配的字节数

**返回值**：

- 成功：返回指向内存块的指针
- 失败：返回 `MR_NULL`

**示例**：

```c
#include <mrX/libc/mr_malloc.h>

void *buffer = mr_malloc(1024);
if (buffer == MR_NULL) {
    printf("Memory allocation failed\n");
    return;
}

/* 使用内存 */
mr_uint8_t *buf = (mr_uint8_t *)buffer;
buf[0] = 0x12;

/* 释放内存 */
mr_free(buffer);
```

#### mr_free() - 释放内存

```c
void mr_free(void *mem);
```

**功能**：释放之前分配的内存块

**参数**：

- `mem`：要释放的内存块指针

**示例**：

```c
void *ptr = mr_malloc(100);
if (ptr != MR_NULL) {
    /* 使用内存 */
    /* ... */

    /* 释放内存 */
    mr_free(ptr);
    ptr = MR_NULL;  /* 避免悬空指针 */
}

/* 释放 MR_NULL 是安全的 */
mr_free(MR_NULL);  /* 什么都不做 */
```

**注意事项**：

- 只能释放 `mr_malloc` 等函数分配的内存
- 不要重复释放同一块内存
- 释放 `MR_NULL` 是安全的

#### mr_calloc() - 分配并清零内存

```c
void *mr_calloc(mr_size_t num, mr_size_t size);
```

**功能**：分配 num 个 size 大小的内存块，并清零

**参数**：

- `num`：元素个数
- `size`：每个元素的大小

**返回值**：

- 成功：返回指向清零内存的指针
- 失败：返回 `MR_NULL`

**示例**：

```c
/* 分配 100 个 int 的数组，并清零 */
int *array = mr_calloc(100, sizeof(int));
if (array != MR_NULL) {
    /* 数组已初始化为 0 */
    for (int i = 0; i < 100; i++) {
        printf("array[%d] = %d\n", i, array[i]);
    }
    mr_free(array);
}
```

#### mr_realloc() - 重新分配内存

```c
void *mr_realloc(void *mem, mr_size_t size);
```

**功能**：重新分配内存块大小

**参数**：

- `mem`：原内存块指针（可为 MR_NULL）
- `size`：新的大小

**返回值**：

- 成功：返回指向新内存的指针（可能与原指针相同或不同）
- 失败：返回 `MR_NULL`，原内存块保持不变

**示例**：

```c
/* 初始分配 */
mr_uint8_t *buffer = mr_malloc(256);
if (buffer == MR_NULL) {
    return;
}

/* 使用缓冲区 */
/* ... */

/* 扩大缓冲区 */
mr_uint8_t *new_buffer = mr_realloc(buffer, 512);
if (new_buffer == MR_NULL) {
    /* 扩展失败，原缓冲区仍可用 */
    mr_free(buffer);
    return;
}

buffer = new_buffer;
/* 继续使用更大的缓冲区 */
```

**注意事项**：

- 如果 `mem` 为 `MR_NULL`，相当于 `mr_malloc(size)`
- 如果 `size` 为 0，相当于 `mr_free(mem)`
- 原内容会被保留（截断或扩展）

#### mr_malloc_usable_size() - 获取可用大小

```c
mr_size_t mr_malloc_usable_size(void *mem);
```

**功能**：获取内存块的实际可用大小

**参数**：

- `mem`：内存块指针

**返回值**：

- 内存块的实际可用字节数

**示例**：

```c
void *ptr = mr_malloc(100);
mr_size_t usable = mr_malloc_usable_size(ptr);

printf("Requested: 100 bytes, Usable: %u bytes\n", usable);
/* 可能输出: Requested: 100 bytes, Usable: 112 bytes */
```

#### mr_heap_region_add() - 添加内存区域

```c
void mr_heap_region_add(const mr_heap_region_t *regions);
```

**功能**：添加内存区域到堆管理器

**参数**：

- `regions`：内存区域数组，必须以 `MR_NULL` address 结尾

**示例**：

```c
/* 定义两个内存区域 */
extern mr_uint8_t __heap_start__;
extern mr_uint8_t __heap_end__;
static mr_uint8_t extra_heap[4096];

static const mr_heap_region_t heap_regions[] = {
    { .address = &__heap_start__, .size = &__heap_end__ - &__heap_start__ },
    { .address = extra_heap,      .size = sizeof(extra_heap) },
    { .address = MR_NULL },       /* 结尾标记 */
};

/* 添加内存区域 */
mr_heap_region_add(heap_regions);
```

#### mr_heap_information() - 获取堆信息

```c
mr_heap_info_t mr_heap_information(mr_bool_t lazy);
```

**功能**：获取堆的使用信息

**参数**：

- `lazy`：是否使用懒惰模式（true 不更新耗时的 max_free_bytes）

**返回值**：

- 堆信息结构体

**示例**：

```c
/* 获取完整堆信息 */
mr_heap_info_t info = mr_heap_information(MR_FALSE);

printf("Heap Information:\n");
printf("  Total:     %u bytes\n", info.total_bytes);
printf("  Used:      %u bytes\n", info.used_bytes);
printf("  Free:      %u bytes\n", info.free_bytes);
printf("  Max Free:  %u bytes\n", info.max_free_bytes);
printf("  Failures:  %u\n", info.fail_count);
printf("  Fragments: %u\n", info.frag_num);
printf("  Usage:     %u‰\n", info.disp_kb_ratio);
printf("  Fragment:  %u‰\n", info.frag_ratio);
```

### 2.3 配置模式

#### STD 模式（标准库模式）

```c
#define MR_USE_MALLOC_STD
```

**特点**：

- 直接使用标准库的 `malloc`、`free` 等
- 适合有标准库支持的平台

**定义**：

```c
#if defined(MR_USE_MALLOC_STD)
#define mr_malloc(_sz)                  malloc(_sz)
#define mr_free(_m)                     free(_m)
#define mr_calloc(_n, _sz)              calloc(_n, _sz)
#define mr_realloc(_m, _sz)             realloc(_m, _sz)
#endif
```

#### 自定义模式

```c
#define MR_USE_MALLOC
```

**特点**：

- 使用内置内存管理器
- 支持多内存区域
- 提供内存统计功能

### 2.4 典型应用案例

#### 案例 1：动态数组

```c
#include <mrX/libc/mr_malloc.h>

typedef struct {
    int *data;
    mr_size_t size;
    mr_size_t capacity;
} dynamic_array_t;

dynamic_array_t *array_create(mr_size_t initial_capacity) {
    dynamic_array_t *array = mr_malloc(sizeof(dynamic_array_t));
    if (array == MR_NULL) {
        return MR_NULL;
    }

    array->data = mr_malloc(initial_capacity * sizeof(int));
    if (array->data == MR_NULL) {
        mr_free(array);
        return MR_NULL;
    }

    array->size = 0;
    array->capacity = initial_capacity;
    return array;
}

mr_err_t array_push(dynamic_array_t *array, int value) {
    if (array->size >= array->capacity) {
        /* 扩容 */
        mr_size_t new_capacity = array->capacity * 2;
        int *new_data = mr_realloc(array->data,
                                   new_capacity * sizeof(int));
        if (new_data == MR_NULL) {
            return -MR_ENOMEM;
        }
        array->data = new_data;
        array->capacity = new_capacity;
    }

    array->data[array->size++] = value;
    return 0;
}

void array_destroy(dynamic_array_t *array) {
    if (array != MR_NULL) {
        mr_free(array->data);
        mr_free(array);
    }
}
```

#### 案例 2：字符串缓冲区

```c
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>

typedef struct {
    char *data;
    mr_size_t length;
    mr_size_t capacity;
} string_buffer_t;

string_buffer_t *string_buffer_create(mr_size_t initial_capacity) {
    string_buffer_t *buf = mr_malloc(sizeof(string_buffer_t));
    if (buf == MR_NULL) {
        return MR_NULL;
    }

    buf->data = mr_malloc(initial_capacity);
    if (buf->data == MR_NULL) {
        mr_free(buf);
        return MR_NULL;
    }

    buf->data[0] = '\0';
    buf->length = 0;
    buf->capacity = initial_capacity;
    return buf;
}

mr_err_t string_buffer_append(string_buffer_t *buf, const char *str) {
    mr_size_t str_len = mr_strlen(str);
    mr_size_t needed = buf->length + str_len + 1;

    if (needed > buf->capacity) {
        char *new_data = mr_realloc(buf->data, needed);
        if (new_data == MR_NULL) {
            return -MR_ENOMEM;
        }
        buf->data = new_data;
        buf->capacity = needed;
    }

    mr_strcpy(buf->data + buf->length, str);
    buf->length += str_len;
    return 0;
}

void string_buffer_destroy(string_buffer_t *buf) {
    if (buf != MR_NULL) {
        mr_free(buf->data);
        mr_free(buf);
    }
}
```

#### 案例 3：内存池预分配

```c
#include <mrX/libc/mr_malloc.h>

#define POOL_BLOCK_SIZE  256
#define POOL_BLOCK_COUNT 10

typedef struct {
    void *blocks[POOL_BLOCK_COUNT];
    mr_size_t count;
} memory_pool_t;

memory_pool_t *memory_pool_create(void) {
    memory_pool_t *pool = mr_calloc(1, sizeof(memory_pool_t));
    if (pool == MR_NULL) {
        return MR_NULL;
    }

    /* 预分配所有块 */
    for (mr_size_t i = 0; i < POOL_BLOCK_COUNT; i++) {
        pool->blocks[i] = mr_malloc(POOL_BLOCK_SIZE);
        if (pool->blocks[i] == MR_NULL) {
            /* 分配失败，清理已分配的 */
            memory_pool_destroy(pool);
            return MR_NULL;
        }
        pool->count++;
    }

    return pool;
}

void *memory_pool_alloc(memory_pool_t *pool) {
    if (pool->count > 0) {
        return pool->blocks[--pool->count];
    }
    return MR_NULL;
}

void memory_pool_destroy(memory_pool_t *pool) {
    if (pool != MR_NULL) {
        for (mr_size_t i = 0; i < pool->count; i++) {
            mr_free(pool->blocks[i]);
        }
        mr_free(pool);
    }
}
```

#### 案例 4：内存监控

```c
#include <mrX/libc/mr_malloc.h>

void print_heap_status(void) {
    mr_heap_info_t info = mr_heap_information(MR_FALSE);

    printf("=== Heap Status ===\n");
    printf("Total:  %6u bytes\n", info.total_bytes);
    printf("Used:   %6u bytes (%u%%)\n",
           info.used_bytes,
           (info.used_bytes * 100) / info.total_bytes);
    printf("Free:   %6u bytes\n", info.free_bytes);
    printf("Max:    %6u bytes\n", info.max_free_bytes);
    printf("Frag:   %u blocks\n", info.frag_num);
    printf("Fails:  %u\n", info.fail_count);

    /* 检查内存是否紧张 */
    if (info.free_bytes < (info.total_bytes / 10)) {
        printf("WARNING: Low memory!\n");
    }
}

void *monitored_malloc(mr_size_t size) {
    void *ptr = mr_malloc(size);
    if (ptr == MR_NULL) {
        mr_heap_info_t info = mr_heap_information(MR_TRUE);
        printf("Allocation failed: %u bytes\n", size);
        printf("Available: %u bytes\n", info.free_bytes);
    }
    return ptr;
}
```

---

## 3. 设计思路

### 3.1 设计目标

malloc 模块的设计目标：

1. **轻量级**：适合资源受限的嵌入式系统
2. **标准兼容**：提供标准 malloc 接口
3. **可扩展**：支持多内存区域
4. **可调试**：提供内存统计和错误检测

### 3.2 内存管理策略

#### 堆块结构

```
+------------+------------+------------+-----+
| Block 0    | Block 1    | Block 2    | ... |
| (Used)     | (Free)     | (Used)     |     |
+------------+------------+------------+-----+
```

每个堆块包含：

1. **头部**：`mr_heap_block_t` 结构（魔数、指针、大小）
2. **数据区**：用户实际使用的内存

#### 魔数保护

```c
#define MR_HEAP_MAGIC   (0x6d725800U)
```

- 分配时设置魔数
- 释放时验证魔数
- 魔数损坏表示内存被破坏

#### 空闲链表

- 使用简单的单向链表管理空闲块
- 首次适应算法查找合适的空闲块
- 合并相邻的空闲块减少碎片

### 3.3 设计权衡

#### 为什么使用内置 malloc？

| 特点 | 内置 malloc | 标准 malloc |
|------|------------|-------------|
| 代码大小 | 较小 | 较大 |
| 性能 | 可定制 | 优化良好 |
| 控制 | 完全控制 | 依赖实现 |
| 可移植性 | 需要移植 | 标准兼容 |

#### 内存碎片问题

**内部碎片**：

- 分配的块比请求的大（对齐、最小块大小）
- 难以完全避免

**外部碎片**：

- 空闲块分散在内存各处
- 通过合并相邻空闲块缓解

**缓解策略**：

1. 合并相邻空闲块
2. 使用固定大小的内存池
3. 预分配常用大小的块

---

## 4. 参考资料

### 4.1 相关模块

malloc 模块被以下模块使用：

- **object**：动态创建对象
- **string**：动态字符串操作
- **list**：动态链表节点
- **fifo**：动态 FIFO 缓冲区

### 4.2 内存管理算法

- **首次适应** (First Fit)：找到第一个足够大的空闲块
- **最佳适应** (Best Fit)：找到最小的足够大的空闲块
- **伙伴系统** (Buddy System)：按 2 的幂管理内存块

### 4.3 延伸阅读

- **内存对齐**：理解内存对齐对性能的影响
- **内存碎片**：了解内部碎片和外部碎片
- **内存泄漏检测**：如何检测和调试内存泄漏
- **堆溢出**：理解堆溢出的原理和防护

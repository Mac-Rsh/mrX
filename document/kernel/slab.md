# slab - 固定大小内存池

## 1. 模块概述

### 1.1 功能介绍

slab 模块实现了一个固定大小的内存池分配器。它使用位图（bitmap）来管理内存块的使用状态，提供高效的内存分配和释放操作，专为资源受限的嵌入式系统设计。

slab 模块的核心功能：

- **固定大小分配**：所有内存块大小相同，避免内存碎片
- **位图管理**：使用位图记录内存块使用状态，每个 bit 代表一个内存块
- **无锁并发**：使用原子操作（CAS）实现线程安全，无需自旋锁
- **静态分配**：使用用户提供的缓冲区和位图，不进行动态内存分配
- **索引转换**：支持指针与索引的双向转换

### 1.2 主要特性

- **零内存碎片**：固定大小分配，不会产生外部碎片
- **高效分配**：使用硬件指令查找空闲位，速度随位图数量线性增长
- **线程安全**：使用原子操作，支持多线程和中断上下文
- **内存可预测**：所有内存预先分配，占用固定空间
- **无需初始化**：使用 `MR_SLAB_INIT` 静态初始化即可
- **边界检查**：释放时会验证内存指针的有效性

### 1.3 适用场景

slab 模块适用于以下场景：

- **频繁分配/释放**：固定大小对象的频繁创建和销毁
- **实时性要求高**：需要确定性的分配时间
- **内存受限**：无法使用动态内存分配或 malloc
- **多线程环境**：需要线程安全的内存分配
- **对象池**：实现对象池模式，复用对象减少分配开销

**典型应用**：

- **网络数据包缓冲区**：固定大小的数据包缓冲池
- **任务控制块**：任务或线程的 TCB 池
- **消息队列节点**：固定大小的消息节点池
- **设备描述符**：文件描述符、socket 等结构体池

**不适用场景**：

- **变长分配**：需要分配不同大小的内存块
- **大量小对象**：位图开销相对较大
- **单次分配**：分配后很少释放的场景

### 1.4 使用注意事项

⚠️ **重要警告**：

1. **容量限制**：slab 容量在初始化时确定，资源耗尽时分配失败返回 `NULL`

2. **固定大小**：所有内存块大小必须相同

3. **位图大小**：使用 `MR_SLAB_MAP_SIZE(num)` 计算所需位图大小

4. **内存对齐**：缓冲区不需要特殊对齐，但考虑性能建议对齐

5. **释放检查**：释放无效指针会被忽略，不会导致错误

---

## 2. 使用指南

### 2.1 核心数据结构

#### mr_slab_t

```c
typedef struct mr_slab {
    mr_atomic_t *map;     /* 位图数组 */
    mr_uint32_t size;     /* 单个内存块大小 */
    mr_uint8_t *buf;      /* 内存缓冲区 */
    mr_uint32_t num;      /* 内存块数量 */
} mr_slab_t;
```

**字段说明**：

- `map`：指向位图数组的指针（由用户提供），每个 bit 代表一个内存块的使用状态（0=空闲，1=占用）
- `size`：每个内存块的大小（字节）
- `buf`：指向内存缓冲区的指针（由用户提供）
- `num`：内存块的总数量

**内存布局**：

```
slab 内存布局 (num = 16, size = 64):

位图 map (4 个 uint32_t):
+--------+--------+--------+--------+
|11111111|11111111|00000000|00000000|
+--------+--------+--------+--------+
  ^                         ^
  |                         |
  前8个占用                后8个空闲

缓冲区 buf (16 * 64 = 1024 字节):
+------+------+------+------+--...--+------+
| blk0 | blk1 | blk2 | blk3 | ...  | blk15|
+------+------+------+------+--+---+------+
  ^                                                ^
  |                                                |
  buf + 0*64                                      buf + 15*64
```

**位图映射**：

```
内存块索引 = (map索引 << 5) + bit位置
例如：索引 10 = (0 << 5) + 10 → map[0] 的 bit 10
     索引 35 = (1 << 5) + 3  → map[1] 的 bit 3
```

### 2.2 主要接口说明

#### 初始化接口

##### MR_SLAB_INIT()

```c
#define MR_SLAB_INIT(_buf, _size, _num)
```

**功能**：静态初始化 slab

**参数**：

- `_buf`：内存缓冲区
- `_size`：单个内存块大小
- `_num`：内存块数量

**注意**：此宏不初始化位图，位图必须在全局作用域声明并初始化为 0

**示例**：

```c
/* 定义缓冲区和位图 */
static mr_uint8_t slab_buf[16 * 64];  /* 16 个 64 字节的块 */
static mr_atomic_t slab_map[MR_SLAB_MAP_SIZE(16)];  /* 位图，自动初始化为 0 */

/* 静态初始化 slab */
static mr_slab_t my_slab = MR_SLAB_INIT(slab_buf, 64, 16);
```

##### mr_slab_init()

```c
MR_INLINE mr_uint32_t mr_slab_init(mr_slab_t *slab,
                                   void *map,
                                   mr_uint32_t map_size,
                                   void *buf,
                                   mr_uint32_t size,
                                   mr_uint32_t num);
```

**功能**：动态初始化 slab

**参数**：

- `slab`：slab 对象指针
- `map`：位图数组
- `map_size`：位图大小（字节）
- `buf`：内存缓冲区
- `size`：单个内存块大小
- `num`：内存块数量

**返回值**：实际支持的内存块数量

**示例**：

```c
mr_uint8_t slab_buf[32 * 128];
mr_atomic_t slab_map[4];  /* 4 * 32 = 128 bits */
mr_slab_t slab;

/* 初始化 slab */
mr_uint32_t actual_num = mr_slab_init(&slab,
                                       slab_map,
                                       sizeof(slab_map),
                                       slab_buf,
                                       128,
                                       100);
/* actual_num = 100 (受 num 参数限制) */
```

#### MR_SLAB_MAP_SIZE()

```c
#define MR_SLAB_MAP_SIZE(_num)
```

**功能**：计算给定数量内存块所需的位图大小（字节）

**参数**：

- `_num`：内存块数量

**返回值**：位图大小（字节）

**计算公式**：

```c
(((num + 31) >> 5) << 2)
/* 等价于: ((num + 31) / 32) * 4 */
```

**示例**：

```c
/* 16 个块需要 1 个 uint32_t (4 字节) */
MR_SLAB_MAP_SIZE(16)   = 4

/* 32 个块需要 1 个 uint32_t (4 字节) */
MR_SLAB_MAP_SIZE(32)   = 4

/* 33 个块需要 2 个 uint32_t (8 字节) */
MR_SLAB_MAP_SIZE(33)   = 8

/* 100 个块需要 4 个 uint32_t (16 字节) */
MR_SLAB_MAP_SIZE(100)  = 16
```

#### 分配接口

##### mr_slab_alloc()

```c
void *mr_slab_alloc(mr_slab_t *slab);
```

**功能**：从 slab 中分配一个内存块

**参数**：

- `slab`：slab 对象指针

**返回值**：

- 成功：返回分配的内存块指针
- 失败：返回 `NULL`（slab 已满）

**实现原理**：

```c
/* 1. 遍历位图数组，查找空闲 bit */
for (i = 0; i < SLAB_MAP_NUM(slab->num); i++) {
    /* 2. 使用原子操作获取当前位图值 */
    last = mr_atomic_load(&slab->map[i]);

    do {
        /* 3. 查找第一个空闲 bit (find first set) */
        bit = mr_ffs32(~((mr_uint32_t)last));
        if (bit == 0) {
            break;  /* 当前 uint32_t 无空闲 bit */
        }

        /* 4. 尝试原子地标记为占用 */
        mask = (mr_uint32_t)last | (1U << (bit - 1));
    } while (!mr_atomic_compare_exchange(&slab->map[i], &last, mask));

    /* 5. 如果成功分配，返回对应的内存块 */
    if (bit != 0) {
        off = (i << 5) + (bit - 1);
        if (off < slab->num) {
            return slab->buf + (off * slab->size);
        }
    }
}
return NULL;
```

**注意事项**：

- 线程安全：使用原子 CAS 操作，支持多线程并发分配
- 首次适配：从低索引到高索引查找，优先分配前面的块
- 可能失败：slab 满时返回 NULL

#### 释放接口

##### mr_slab_free()

```c
void mr_slab_free(mr_slab_t *slab, void *mem);
```

**功能**：释放内存块回 slab

**参数**：

- `slab`：slab 对象指针
- `mem`：要释放的内存块指针

**实现原理**：

```c
/* 1. 验证指针是否在有效范围内 */
if ((mem >= slab->buf + (slab->num * slab->size)) ||
    (mem < slab->buf)) {
    return;  /* 无效指针，直接返回 */
}

/* 2. 计算内存块的索引 */
off = ((mr_uint8_t *)mem - slab->buf) / slab->size;

/* 3. 计算位图索引和 bit 位置 */
bit = off & 0x1f;       /* bit 位置 (0-31) */
i = off >> 5;           /* uint32_t 索引 */

/* 4. 原子地清除对应的 bit */
mr_atomic_fetch_and(&slab->map[i], ~(1U << bit));
```

**注意事项**：

- 线程安全：使用原子操作
- 容错处理：无效指针会被忽略
- 重复释放：安全，不会造成错误

#### 索引转换接口

##### mr_slab_ptr2index()

```c
MR_INLINE int mr_slab_ptr2index(mr_slab_t *slab, void *mem);
```

**功能**：将内存块指针转换为索引

**参数**：

- `slab`：slab 对象指针
- `mem`：内存块指针

**返回值**：

- 成功：返回内存块索引（0 ~ num-1）
- 失败：返回 -1（指针无效）

**示例**：

```c
void *ptr = mr_slab_alloc(&slab);
int idx = mr_slab_ptr2index(&slab, ptr);
if (idx >= 0) {
    printf("Allocated block index: %d\n", idx);
}
```

##### mr_slab_index2ptr()

```c
MR_INLINE void *mr_slab_index2ptr(mr_slab_t *slab, int index);
```

**功能**：将索引转换为内存块指针

**参数**：

- `slab`：slab 对象指针
- `index`：内存块索引

**返回值**：

- 成功：返回内存块指针
- 失败：返回 `NULL`（索引无效）

**示例**：

```c
/* 遍历所有内存块 */
for (int i = 0; i < slab.num; i++) {
    void *ptr = mr_slab_index2ptr(&slab, i);
    if (ptr != NULL) {
        /* 处理内存块 */
        process_block(ptr);
    }
}
```

### 2.3 典型应用案例

#### 案例 1：基础用法 - 网络数据包缓冲池

```c
#include <mrX/kernel/mr_slab.h>

/* 配置 */
#define PACKET_SIZE    1536  /* 最大以太网帧 */
#define PACKET_COUNT   32    /* 缓冲区数量 */

/* 静态定义 */
static mr_uint8_t packet_buf[PACKET_SIZE * PACKET_COUNT];
static mr_atomic_t packet_map[MR_SLAB_MAP_SIZE(PACKET_COUNT)];
static mr_slab_t packet_pool = MR_SLAB_INIT(packet_buf, PACKET_SIZE, PACKET_COUNT);

/* 分配数据包缓冲区 */
void *alloc_packet_buffer(void) {
    void *buf = mr_slab_alloc(&packet_pool);
    if (buf == NULL) {
        printf("Packet pool exhausted!\n");
    }
    return buf;
}

/* 释放数据包缓冲区 */
void free_packet_buffer(void *buf) {
    mr_slab_free(&packet_pool, buf);
}

/* 使用示例 */
void handle_network_data(void) {
    void *pkt = alloc_packet_buffer();
    if (pkt != NULL) {
        /* 接收数据到缓冲区 */
        receive_ethernet_frame(pkt, PACKET_SIZE);

        /* 处理数据 */
        process_packet(pkt);

        /* 释放缓冲区 */
        free_packet_buffer(pkt);
    }
}
```

#### 案例 2：任务控制块池

```c
#include <mrX/kernel/mr_slab.h>

/* 任务控制块 */
typedef struct task {
    mr_uint8_t stack[1024];
    void (*entry)(void *);
    void *arg;
    /* ... 其他字段 ... */
} task_t;

#define MAX_TASKS  16

static mr_uint8_t task_buf[sizeof(task_t) * MAX_TASKS];
static mr_atomic_t task_map[MR_SLAB_MAP_SIZE(MAX_TASKS)];
static mr_slab_t task_pool = MR_SLAB_INIT(task_buf, sizeof(task_t), MAX_TASKS);

/* 创建任务 */
task_t *task_create(void (*entry)(void *), void *arg) {
    task_t *task = (task_t *)mr_slab_alloc(&task_pool);
    if (task != NULL) {
        task->entry = entry;
        task->arg = arg;
        /* 初始化栈等... */
    }
    return task;
}

/* 销毁任务 */
void task_destroy(task_t *task) {
    if (task != NULL) {
        mr_slab_free(&task_pool, task);
    }
}
```

#### 案例 3：多线程安全使用

```c
#include <mrX/kernel/mr_slab.h>

#define MSG_SIZE   128
#define MSG_COUNT  64

static mr_uint8_t msg_buf[MSG_SIZE * MSG_COUNT];
static mr_atomic_t msg_map[MR_SLAB_MAP_SIZE(MSG_COUNT)];
static mr_slab_t msg_pool = MR_SLAB_INIT(msg_buf, MSG_SIZE, MSG_COUNT);

/* 生产者线程 */
void producer_thread(void) {
    while (1) {
        /* 分配消息 */
        void *msg = mr_slab_alloc(&msg_pool);
        if (msg != NULL) {
            /* 填充消息 */
            prepare_message(msg, MSG_SIZE);

            /* 发送到队列 */
            message_queue_push(msg);
        } else {
            /* 池满，等待或重试 */
            mr_sleep(10);
        }
    }
}

/* 消费者线程 */
void consumer_thread(void) {
    while (1) {
        /* 从队列获取消息 */
        void *msg = message_queue_pop();
        if (msg != NULL) {
            /* 处理消息 */
            process_message(msg, MSG_SIZE);

            /* 释放消息 */
            mr_slab_free(&msg_pool, msg);
        }
    }
}
```

#### 案例 4：对象池模式 - 文件描述符

```c
#include <mrX/kernel/mr_slab.h>

/* 文件描述符 */
typedef struct file {
    int fd;
    const char *path;
    int flags;
    /* ... 其他字段 ... */
} file_t;

#define MAX_FILES  32

static mr_uint8_t file_buf[sizeof(file_t) * MAX_FILES];
static mr_atomic_t file_map[MR_SLAB_MAP_SIZE(MAX_FILES)];
static mr_slab_t file_pool = MR_SLAB_INIT(file_buf, sizeof(file_t), MAX_FILES);

/* 打开文件 */
file_t *file_open(const char *path, int flags) {
    file_t *file = (file_t *)mr_slab_alloc(&file_pool);
    if (file != NULL) {
        file->path = path;
        file->flags = flags;
        /* 实际打开操作... */
    }
    return file;
}

/* 关闭文件 */
void file_close(file_t *file) {
    if (file != NULL) {
        /* 实际关闭操作... */
        mr_slab_free(&file_pool, file);
    }
}

/* 获取 fd 对应的 file 结构 */
file_t *file_get(int fd) {
    file_t *file = mr_slab_index2ptr(&file_pool, fd);
    return file;
}
```

#### 案例 5：正确使用示例与常见陷阱

```c
#include <mrX/kernel/mr_slab.h>

#define OBJ_SIZE  64
#define OBJ_NUM   16

static mr_uint8_t obj_buf[OBJ_SIZE * OBJ_NUM];
static mr_atomic_t obj_map[MR_SLAB_MAP_SIZE(OBJ_NUM)];
static mr_slab_t obj_pool = MR_SLAB_INIT(obj_buf, OBJ_SIZE, OBJ_NUM);

/* ✓ 正确：检查返回值 */
static void correct_alloc_check(void) {
    void *obj = mr_slab_alloc(&obj_pool);
    if (obj == NULL) {
        printf("Allocation failed!\n");
        /* 处理失败情况 */
        return;
    }
    /* 使用对象 */
    use_object(obj);
}

/* ✗ 错误：未检查返回值 */
static void wrong_no_check(void) {
    void *obj = mr_slab_alloc(&obj_pool);
    /* 错误：obj 可能为 NULL */
    use_object(obj);  /* 可能崩溃 */
}

/* ✓ 正确：使用 MR_SLAB_MAP_SIZE 计算位图大小 */
static mr_atomic_t correct_map[MR_SLAB_MAP_SIZE(100)];
static mr_slab_t correct_pool;

void correct_init(void) {
    mr_uint8_t buf[256 * 100];
    mr_slab_init(&correct_pool,
                 correct_map,
                 sizeof(correct_map),
                 buf,
                 256,
                 100);
}

/* ✗ 错误：位图大小不足 */
static mr_atomic_t wrong_map[1];  /* 只有 32 个 bit */
static mr_slab_t wrong_pool;

void wrong_init(void) {
    mr_uint8_t buf[256 * 100];
    mr_slab_init(&wrong_pool,
                 wrong_map,
                 sizeof(wrong_map),
                 buf,
                 256,
                 100);  /* 错误：位图只能支持 32 个块 */
}

/* ✓ 正确：成对分配和释放 */
static void correct_pair(void) {
    void *obj = mr_slab_alloc(&obj_pool);
    if (obj != NULL) {
        /* 使用对象 */
        process(obj);
        /* 释放对象 */
        mr_slab_free(&obj_pool, obj);
    }
}

/* ✗ 错误：内存泄漏 */
static void wrong_leak(void) {
    void *obj = mr_slab_alloc(&obj_pool);
    if (obj != NULL) {
        process(obj);
        /* 错误：忘记释放 */
    }
}

/* ✓ 正确：重复使用同一块内存 */
static void correct_reuse(void) {
    void *obj = mr_slab_alloc(&obj_pool);
    if (obj != NULL) {
        use_object(obj);
        mr_slab_free(&obj_pool, obj);
    }

    /* 再次分配，可能获得同一块内存 */
    obj = mr_slab_alloc(&obj_pool);
    if (obj != NULL) {
        use_object(obj);
        mr_slab_free(&obj_pool, obj);
    }
}

/* ✓ 正确：指针与索引转换 */
static void correct_index_convert(void) {
    void *obj = mr_slab_alloc(&obj_pool);
    if (obj != NULL) {
        /* 指针转索引 */
        int idx = mr_slab_ptr2index(&obj_pool, obj);
        printf("Object index: %d\n", idx);

        /* 索引转指针 */
        void *obj2 = mr_slab_index2ptr(&obj_pool, idx);
        /* obj2 == obj */

        mr_slab_free(&obj_pool, obj);
    }
}

/* ✓ 正确：遍历所有对象（调试用） */
static void correct_traverse(void) {
    for (int i = 0; i < obj_pool.num; i++) {
        void *obj = mr_slab_index2ptr(&obj_pool, i);
        if (obj != NULL) {
            /* 检查对象状态 */
            inspect_object(obj);
        }
    }
}

/* ✓ 正确：处理分配失败 */
static void correct_handle_full(void) {
    void *objs[OBJ_NUM];
    int count = 0;

    /* 分配所有对象 */
    for (int i = 0; i < OBJ_NUM; i++) {
        objs[i] = mr_slab_alloc(&obj_pool);
        if (objs[i] != NULL) {
            count++;
        } else {
            break;  /* 池已满 */
        }
    }

    /* 尝试再次分配，会失败 */
    void *extra = mr_slab_alloc(&obj_pool);
    if (extra == NULL) {
        printf("Pool is full (%d objects)\n", count);
    }

    /* 释放所有对象 */
    for (int i = 0; i < count; i++) {
        mr_slab_free(&obj_pool, objs[i]);
    }
}

/* ✓ 正确：静态初始化 */
static mr_uint8_t static_buf[32 * 64];
static mr_atomic_t static_map[MR_SLAB_MAP_SIZE(32)];
static mr_slab_t static_slab = MR_SLAB_INIT(static_buf, 64, 32);
/* 注意：static_map 需要在全局作用域声明，自动初始化为 0 */
```

---

## 3. 设计思路

### 3.1 设计目标

slab 模块的设计目标：

1. **零碎片**：固定大小分配，避免内存碎片问题
2. **确定性**：分配时间随位图数量线性增长，具有可预测的上界
3. **线程安全**：使用原子操作，无需锁
4. **静态分配**：所有内存预先分配，无运行时分配
5. **简单高效**：实现简单，性能优异

### 3.2 为什么使用位图

**位图的优点**：

1. **空间效率高**：每个 bit 代表一个块，管理开销极小
    - 1000 个块只需要 125 字节的位图
    - 比链表指针开销小得多

2. **查找速度快**：
    - 使用 `mr_ffs32()` 指令快速查找空闲 bit
    - 硬件级别的查找效率

3. **缓存友好**：位图通常在缓存行内，访问速度快

**位图 vs 链表**：

| 特性   | 位图          | 链表              |
|------|-------------|-----------------|
| 管理开销 | 1 bit/块     | 1 指针/块 (4/8 字节) |
| 查找速度 | 快（随位图数线性增长） | O(n) 或 O(1)     |
| 分配顺序 | 首次适配        | 通常 LIFO         |
| 碎片处理 | 无碎片         | 可能碎片            |

### 3.3 无锁并发设计

slab 模块使用原子操作实现无锁并发：

**分配过程（CAS 循环）**：

```c
do {
    /* 1. 读取当前位图值 */
    last = mr_atomic_load(&slab->map[i]);

    /* 2. 查找空闲 bit */
    bit = mr_ffs32(~((mr_uint32_t)last));
    if (bit == 0) {
        break;  /* 无空闲 bit */
    }

    /* 3. 准备新值（设置 bit） */
    mask = last | (1U << (bit - 1));

    /* 4. CAS 操作：如果 map[i] == last，则设置为 mask */
} while (!mr_atomic_compare_exchange(&slab->map[i], &last, mask));
```

**为什么无锁**：

1. **CAS 原子性**：`compare_exchange` 保证原子地完成"检查并设置"
2. **自动重试**：如果其他线程修改了位图，自动重试
3. **无死锁**：没有锁的获取和释放，不会死锁

**释放过程（fetch_and）**：

```c
/* 原子地清除指定 bit */
mr_atomic_fetch_and(&slab->map[i], ~(1U << bit));
```

### 3.4 固定大小的权衡

**固定大小的优点**：

- **无外部碎片**：所有块大小相同，完美利用内存
- **分配简单**：无需搜索合适大小的块
- **可预测性**：内存占用确定，适合实时系统

**固定大小的缺点**：

- **灵活性差**：只能分配固定大小的对象
- **可能浪费**：如果对象大小差异大，会浪费内存

**解决方法**：

```c
/* 创建多个不同大小的 slab */
#define SMALL_SIZE  64
#define SMALL_NUM   128

#define MEDIUM_SIZE 256
#define MEDIUM_NUM  64

#define LARGE_SIZE  1024
#define LARGE_NUM   32

static mr_slab_t small_slab;   /* 小对象 */
static mr_slab_t medium_slab;  /* 中等对象 */
static mr_slab_t large_slab;   /* 大对象 */

/* 根据大小选择合适的 slab */
void *alloc_variable_size(size_t size) {
    if (size <= SMALL_SIZE) {
        return mr_slab_alloc(&small_slab);
    } else if (size <= MEDIUM_SIZE) {
        return mr_slab_alloc(&medium_slab);
    } else if (size <= LARGE_SIZE) {
        return mr_slab_alloc(&large_slab);
    }
    return NULL;
}
```

### 3.5 与其他机制的对比

#### vs malloc/free

| 特性   | slab  | malloc |
|------|-------|--------|
| 内存碎片 | 无     | 有      |
| 分配时间 | 快（线性） | 不确定    |
| 线程安全 | 原子操作  | 通常用锁   |
| 内存来源 | 静态分配  | 动态分配   |
| 适用场景 | 实时系统  | 通用应用   |

#### vs 内存池（链表实现）

| 特性   | slab（位图） | 内存池（链表） |
|------|----------|---------|
| 管理开销 | 极小       | 较大（指针）  |
| 分配速度 | 极快       | 快       |
| 释放速度 | 极快       | 快       |
| 内存布局 | 连续       | 离散      |
| 缓存友好 | 好        | 一般      |

#### vs 对象池

| 特性    | slab       | 对象池   |
|-------|------------|-------|
| 对象类型  | 无类型（字节缓冲区） | 有类型   |
| 构造/析构 | 无支持        | 可支持   |
| 类型安全  | 需要强制转换     | 编译时检查 |

### 3.6 slab 模块的应用场景

#### 网络协议栈

```c
/* 数据包缓冲池 */
#define SKB_SIZE  2048
#define SKB_COUNT 64

static mr_slab_t skb_pool;

/* 接收数据包 */
void net_receive(void) {
    /* 分配 skb */
    void *skb = mr_slab_alloc(&skb_pool);
    if (skb != NULL) {
        /* DMA 传输到缓冲区 */
        eth_dma_read(skb, SKB_SIZE);

        /* 处理数据包 */
        net_process(skb);

        /* 释放 skb */
        mr_slab_free(&skb_pool, skb);
    }
}
```

#### 任务调度器

```c
/* 任务控制块池 */
#define TCB_SIZE  sizeof(task_t)
#define TCB_COUNT 32

static mr_slab_t tcb_pool;

/* 创建任务 */
task_t *task_create(void (*entry)(void *)) {
    task_t *tcb = (task_t *)mr_slab_alloc(&tcb_pool);
    if (tcb != NULL) {
        tcb->entry = entry;
        tcb->state = TASK_READY;
        /* ... */
    }
    return tcb;
}
```

#### 消息队列

```c
/* 消息节点池 */
typedef struct msg_node {
    struct msg_node *next;
    void *data;
} msg_node_t;

#define MSG_NODE_COUNT 128

static mr_slab_t msg_node_pool;

/* 发送消息 */
void message_send(void *data) {
    msg_node_t *node = (msg_node_t *)mr_slab_alloc(&msg_node_pool);
    if (node != NULL) {
        node->data = data;
        queue_push(&msg_queue, node);
    }
}

/* 接收消息 */
void *message_receive(void) {
    msg_node_t *node = queue_pop(&msg_queue);
    if (node != NULL) {
        void *data = node->data;
        mr_slab_free(&msg_node_pool, node);
        return data;
    }
    return NULL;
}
```

---

## 4. 参考资料

### 4.1 相关模块

- **atomic**：原子操作模块，提供 CAS 和 fetch_and 操作
- **spinlock**：如果需要用锁保护其他状态，可配合使用

### 4.2 设计参考

- **Slab 分配器**：Linux 内核的 slab 分配器（更复杂，支持多缓存）
- **对象池模式**：软件设计模式中的对象池
- **内存池**：嵌入式系统中常用的内存管理技术

### 4.3 延伸阅读

- **Buddy 分配器**：另一种内存分配算法
- **TLSF 分配器**：Two-Level Segregated Fit，用于嵌入式系统
- **内存碎片**：内存管理中的碎片问题及其解决方法

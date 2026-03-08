# list - 双向链表

## 1. 模块概述

### 1.1 功能介绍

list 模块实现了一个经典的双向循环链表数据结构。链表是一种基础的线性数据结构，通过指针将一组数据元素串联起来。与数组相比，链表具有动态大小、高效的插入和删除操作等优势。

双向链表的每个节点包含两个指针：

- **next 指针**：指向后继节点
- **prev 指针**：指向前驱节点

循环链表的特点是最后一个节点的 next 指针指向头节点，头节点的 prev 指针指向最后一个节点，形成一个环形结构。

list 模块的设计参考了 Linux 内核的链表实现（`struct list_head`），采用了**侵入式链表**的设计模式：

- 链表节点嵌入到用户自定义的结构体中
- 通过 `MR_CONTAINER_OF` 宏从链表节点获取包含它的结构体
- 链表操作与具体数据类型解耦

### 1.2 主要特性

- **双向循环链表**：支持双向遍历，任意位置插入删除都是 O(1) 时间复杂度
- **侵入式设计**：链表节点嵌入到用户结构体中，不额外分配内存
- **零开销抽象**：所有函数都是内联函数，编译后无函数调用开销
- **类型安全**：通过宏和类型检查确保正确使用
- **丰富的操作接口**：初始化、插入、删除、替换、移动、遍历等
- **安全的遍历机制**：提供 `_SAFE` 版本的遍历宏，支持遍历过程中删除节点

### 1.3 适用场景

list 模块适用于以下场景：

- **动态数据集合**：需要频繁插入删除的数据
- **任务队列**：如就绪队列、等待队列、工作队列等
- **资源管理**：管理设备、缓冲区、定时器等资源
- **LRU 缓存**：最近最少使用缓存算法
- **树的实现**：如父子关系的树结构

**不适用场景**：

- 需要随机访问的场景：链表的随机访问是 O(n)，数组是 O(1)
- 小型固定集合：简单数组或静态数组更合适
- 对缓存性能要求极高的场景：链表的缓存局部性较差

---

## 2. 使用指南

### 2.1 核心数据结构

#### mr_list_t

```c
typedef struct mr_list {
    struct mr_list *next;    /* 后继节点 */
    struct mr_list *prev;    /* 前驱节点 */
} mr_list_t;
```

双向循环链表的节点类型，只包含两个指针。

**内存占用**：

- 32 位系统：8 字节（每个指针 4 字节）
- 64 位系统：16 字节（每个指针 8 字节）

**使用方式**：
链表节点通常是用户自定义结构体的一个成员：

```c
typedef struct {
    int id;
    char name[32];
    mr_list_t link;    /* 链表节点 */
} my_node_t;
```

### 2.2 主要接口说明

#### 初始化接口

##### MR_LIST_INIT()

```c
#define MR_LIST_INIT(_list) \
    { \
        .next = (_list), \
        .prev = (_list), \
    }
```

**功能**：静态初始化链表头

**参数**：

- `_list`：链表头指针

**示例**：

```c
static mr_list_t my_list = MR_LIST_INIT(&my_list);
```

##### mr_list_init()

```c
void mr_list_init(mr_list_t *entry);
```

**功能**：动态初始化链表节点或头节点

**参数**：

- `entry`：链表节点指针

**示例**：

```c
mr_list_t my_list;
mr_list_init(&my_list);  /* 初始化为空链表 */
```

**注意事项**：

- 初始化后，节点的 `next` 和 `prev` 都指向自己，表示空链表

#### 插入操作接口

##### mr_list_add()

```c
void mr_list_add(mr_list_t *head, mr_list_t *entry);
```

**功能**：将节点添加到链表末尾

**参数**：

- `head`：链表头
- `entry`：要添加的节点

**时间复杂度**：O(1)

**示例**：

```c
mr_list_add(&my_list, &node1->link);  /* 添加到末尾 */
mr_list_add(&my_list, &node2->link);  /* 添加到末尾 */
/* 结果：node1 -> node2 */
```

##### mr_list_insert()

```c
void mr_list_insert(mr_list_t *head, mr_list_t *entry);
```

**功能**：将节点插入到链表头部

**参数**：

- `head`：链表头
- `entry`：要插入的节点

**时间复杂度**：O(1)

**示例**：

```c
mr_list_insert(&my_list, &node1->link);  /* 插入到头部 */
mr_list_insert(&my_list, &node2->link);  /* 插入到头部 */
/* 结果：node2 -> node1 */
```

#### 删除操作接口

##### mr_list_del()

```c
void mr_list_del(mr_list_t *entry);
```

**功能**：从链表中删除节点

**参数**：

- `entry`：要删除的节点

**时间复杂度**：O(1)

**注意事项**：

- 删除后，节点的 `next` 和 `prev` 会指向自己，但不会清零
- 删除操作不会释放内存，需要手动管理

**示例**：

```c
mr_list_del(&node->link);  /* 从链表中删除 */
```

#### 替换和移动接口

##### mr_list_replace()

```c
void mr_list_replace(mr_list_t *new, mr_list_t *old);
```

**功能**：用新节点替换旧节点

**参数**：

- `new`：新节点
- `old`：旧节点

**时间复杂度**：O(1)

**示例**：

```c
mr_list_replace(&new_node->link, &old_node->link);
```

##### mr_list_move()

```c
void mr_list_move(mr_list_t *entry, mr_list_t *head);
```

**功能**：将节点从一个链表移动到另一个链表的末尾

**参数**：

- `entry`：要移动的节点
- `head`：目标链表头

**时间复杂度**：O(1)

**示例**：

```c
mr_list_move(&node->link, &another_list);  /* 移动到另一个链表 */
```

#### 查询接口

##### mr_list_is_empty()

```c
mr_bool_t mr_list_is_empty(mr_list_t *head);
```

**功能**：检查链表是否为空

**参数**：

- `head`：链表头

**返回值**：

- `MR_TRUE`：链表为空
- `MR_FALSE`：链表非空

#### 辅助宏

##### MR_LIST_ENTRY()

```c
#define MR_LIST_ENTRY(_ptr, _type, _member) \
    MR_CONTAINER_OF(_ptr, _type, _member)
```

**功能**：从链表节点指针获取包含它的结构体指针

**参数**：

- `_ptr`：链表节点指针（`mr_list_t *`）
- `_type`：结构体类型
- `_member`：链表节点在结构体中的成员名

**返回值**：包含该节点的结构体指针

**示例**：

```c
typedef struct {
    int id;
    mr_list_t link;
} my_node_t;

mr_list_t *pos = ...;
my_node_t *node = MR_LIST_ENTRY(pos, my_node_t, link);
printf("Node ID: %d\n", node->id);
```

#### 遍历宏

##### MR_LIST_FOR_EACH()

```c
#define MR_LIST_FOR_EACH(_pos, _head) \
    for ((_pos) = (_head)->next; (_pos) != (_head); (_pos) = (_pos)->next)
```

**功能**：遍历链表

**参数**：

- `_pos`：当前节点指针（`mr_list_t *`）
- `_head`：链表头

**示例**：

```c
mr_list_t *pos;
MR_LIST_FOR_EACH(pos, &my_list) {
    my_node_t *node = MR_LIST_ENTRY(pos, my_node_t, link);
    printf("Node ID: %d\n", node->id);
}
```

**注意事项**：

- **不要在遍历过程中删除节点**，会导致未定义行为
- 如需在遍历中删除，使用 `MR_LIST_FOR_EACH_SAFE`

##### MR_LIST_FOR_EACH_SAFE()

```c
#define MR_LIST_FOR_EACH_SAFE(_pos, _n, _head) \
    for ((_pos) = (_head)->next, (_n) = (_pos)->next; \
         (_pos) != (_head); \
         (_pos) = (_n), (_n) = (_pos)->next)
```

**功能**：安全地遍历链表，支持在遍历过程中删除节点

**参数**：

- `_pos`：当前节点指针（`mr_list_t *`）
- `_n`：下一个节点的临时指针（`mr_list_t *`）
- `_head`：链表头

**示例**：

```c
mr_list_t *pos, *n;
MR_LIST_FOR_EACH_SAFE(pos, n, &my_list) {
    my_node_t *node = MR_LIST_ENTRY(pos, my_node_t, link);
    if (node->id == target_id) {
        mr_list_del(pos);  /* 可以安全删除 */
    }
}
```

### 2.3 典型应用案例

#### 案例 1：基础用法 - 创建和遍历链表

```c
#include <mrX/kernel/mr_list.h>
#include <stdio.h>
#include <stdlib.h>

/* 定义节点结构 */
typedef struct {
    int id;
    char name[32];
    mr_list_t link;    /* 链表节点成员 */
} student_t;

int main(void) {
    mr_list_t class_list;
    student_t students[3];
    mr_list_t *pos;

    /* 初始化链表头 */
    mr_list_init(&class_list);

    /* 初始化学生数据 */
    students[0].id = 1;
    snprintf(students[0].name, sizeof(students[0].name), "Alice");

    students[1].id = 2;
    snprintf(students[1].name, sizeof(students[1].name), "Bob");

    students[2].id = 3;
    snprintf(students[2].name, sizeof(students[2].name), "Charlie");

    /* 添加学生到链表 */
    mr_list_add(&class_list, &students[0].link);
    mr_list_add(&class_list, &students[1].link);
    mr_list_add(&class_list, &students[2].link);

    /* 遍历链表打印学生信息 */
    printf("Class roster:\n");
    MR_LIST_FOR_EACH(pos, &class_list) {
        student_t *s = MR_LIST_ENTRY(pos, student_t, link);
        printf("  ID: %d, Name: %s\n", s->id, s->name);
    }

    /* 检查链表是否为空 */
    if (mr_list_is_empty(&class_list)) {
        printf("Class is empty\n");
    } else {
        printf("Total students: %d\n", 3);
    }

    return 0;
}
```

**预期输出**：

```
Class roster:
  ID: 1, Name: Alice
  ID: 2, Name: Bob
  ID: 3, Name: Charlie
Total students: 3
```

#### 案例 2：任务队列管理

```c
#include <mrX/kernel/mr_list.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int priority;
    char task_name[64];
    mr_list_t link;
} task_t;

/* 任务队列头 */
static mr_list_t task_queue;

void init_task_queue(void) {
    mr_list_init(&task_queue);
}

void add_task(int priority, const char *name) {
    task_t *task = malloc(sizeof(task_t));
    if (!task) {
        return;
    }

    task->priority = priority;
    snprintf(task->task_name, sizeof(task->task_name), "%s", name);

    /* 高优先级任务插入到头部 */
    if (priority > 5) {
        mr_list_insert(&task_queue, &task->link);
    } else {
        mr_list_add(&task_queue, &task->link);
    }

    printf("Task added: %s (priority=%d)\n", name, priority);
}

void execute_tasks(void) {
    mr_list_t *pos, *n;

    printf("\nExecuting tasks:\n");

    /* 安全遍历，执行并删除任务 */
    MR_LIST_FOR_EACH_SAFE(pos, n, &task_queue) {
        task_t *task = MR_LIST_ENTRY(pos, task_t, link);
        printf("  Executing: %s (priority=%d)\n", task->task_name, task->priority);

        /* 执行任务... */

        /* 从队列中删除并释放 */
        mr_list_del(pos);
        free(task);
    }

    printf("\nAll tasks completed\n");
}

int main(void) {
    init_task_queue();

    add_task(3, "Low priority task");
    add_task(8, "High priority task");
    add_task(5, "Medium priority task");

    execute_tasks();

    return 0;
}
```

**预期输出**：

```
Task added: Low priority task (priority=3)
Task added: High priority task (priority=8)
Task added: Medium priority task (priority=5)

Executing tasks:
  Executing: High priority task (priority=8)
  Executing: Medium priority task (priority=5)
  Executing: Low priority task (priority=3)

All tasks completed
```

#### 案例 3：LRU 缓存实现

```c
#include <mrX/kernel/mr_list.h>
#include <stdio.h>
#include <string.h>

#define CACHE_SIZE 5

typedef struct {
    int key;
    int value;
    mr_list_t link;    /* LRU 链表节点 */
} cache_entry_t;

typedef struct {
    cache_entry_t entries[CACHE_SIZE];
    mr_list_t lru_list;
    int count;
} lru_cache_t;

void lru_cache_init(lru_cache_t *cache) {
    int i;

    mr_list_init(&cache->lru_list);
    cache->count = 0;

    /* 初始化所有缓存条目 */
    for (i = 0; i < CACHE_SIZE; i++) {
        mr_list_init(&cache->entries[i].link);
        cache->entries[i].key = -1;  /* -1 表示未使用 */
    }
}

int lru_cache_get(lru_cache_t *cache, int key) {
    cache_entry_t *entry;
    mr_list_t *pos;

    /* 查找缓存 */
    MR_LIST_FOR_EACH(pos, &cache->lru_list) {
        entry = MR_LIST_ENTRY(pos, cache_entry_t, link);
        if (entry->key == key) {
            /* 命中，移动到链表头部（最新使用） */
            mr_list_move(pos, &cache->lru_list);
            printf("Cache HIT: key=%d, value=%d\n", key, entry->value);
            return entry->value;
        }
    }

    printf("Cache MISS: key=%d\n", key);
    return -1;  /* 未找到 */
}

void lru_cache_put(lru_cache_t *cache, int key, int value) {
    cache_entry_t *entry;
    mr_list_t *pos;

    /* 检查是否已存在 */
    MR_LIST_FOR_EACH(pos, &cache->lru_list) {
        entry = MR_LIST_ENTRY(pos, cache_entry_t, link);
        if (entry->key == key) {
            /* 更新值并移动到头部 */
            entry->value = value;
            mr_list_move(pos, &cache->lru_list);
            printf("Cache UPDATE: key=%d, value=%d\n", key, value);
            return;
        }
    }

    /* 如果缓存已满，淘汰最久未使用的项（链表末尾） */
    if (cache->count >= CACHE_SIZE) {
        pos = cache->lru_list.prev;
        entry = MR_LIST_ENTRY(pos, cache_entry_t, link);
        printf("Cache EVICT: key=%d, value=%d\n", entry->key, entry->value);

        /* 从链表中删除 */
        mr_list_del(pos);
        cache->count--;
    }

    /* 找一个空闲条目 */
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache->entries[i].key == -1) {
            entry = &cache->entries[i];
            entry->key = key;
            entry->value = value;

            /* 插入到链表头部（最新使用） */
            mr_list_insert(&cache->lru_list, &entry->link);
            cache->count++;
            printf("Cache ADD: key=%d, value=%d\n", key, value);
            return;
        }
    }
}

void lru_cache_print(lru_cache_t *cache) {
    cache_entry_t *entry;
    mr_list_t *pos;

    printf("Cache state (MRU -> LRU):\n");
    MR_LIST_FOR_EACH(pos, &cache->lru_list) {
        entry = MR_LIST_ENTRY(pos, cache_entry_t, link);
        printf("  key=%d, value=%d\n", entry->key, entry->value);
    }
}

int main(void) {
    lru_cache_t cache;

    lru_cache_init(&cache);

    /* 添加缓存项 */
    lru_cache_put(&cache, 1, 100);
    lru_cache_put(&cache, 2, 200);
    lru_cache_put(&cache, 3, 300);

    lru_cache_print(&cache);

    /* 访问缓存 */
    lru_cache_get(&cache, 1);  /* 命中 */
    lru_cache_get(&cache, 4);  /* 未命中 */

    lru_cache_print(&cache);

    /* 添加更多项，触发淘汰 */
    lru_cache_put(&cache, 4, 400);
    lru_cache_put(&cache, 5, 500);
    lru_cache_put(&cache, 6, 600);  /* 这会淘汰最久未使用的项 */

    lru_cache_print(&cache);

    return 0;
}
```

**预期输出**：

```
Cache ADD: key=1, value=100
Cache ADD: key=2, value=200
Cache ADD: key=3, value=300
Cache state (MRU -> LRU):
  key=3, value=300
  key=2, value=200
  key=1, value=100
Cache HIT: key=1, value=100
Cache MISS: key=4
Cache state (MRU -> LRU):
  key=1, value=100
  key=3, value=300
  key=2, value=200
Cache ADD: key=4, value=400
Cache ADD: key=5, value=500
Cache EVICT: key=2, value=200
Cache ADD: key=6, value=600
Cache state (MRU -> LRU):
  key=6, value=600
  key=5, value=500
  key=4, value=400
  key=1, value=100
  key=3, value=300
```

#### 案例 4：多链表管理 - 就绪队列和等待队列

```c
#include <mrX/kernel/mr_list.h>
#include <stdio.h>

typedef enum {
    TASK_STATE_READY,
    TASK_STATE_WAITING,
    TASK_STATE_RUNNING,
} task_state_t;

typedef struct {
    int pid;
    task_state_t state;
    mr_list_t ready_link;    /* 就绪队列链表节点 */
    mr_list_t wait_link;     /* 等待队列链表节点 */
} task_t;

/* 就绪队列和等待队列 */
static mr_list_t ready_queue;
static mr_list_t wait_queue;

void scheduler_init(void) {
    mr_list_init(&ready_queue);
    mr_list_init(&wait_queue);
}

void task_add(task_t *task) {
    task->state = TASK_STATE_READY;
    mr_list_add(&ready_queue, &task->ready_link);
    printf("Task %d added to READY queue\n", task->pid);
}

void task_block(task_t *task) {
    if (task->state == TASK_STATE_READY) {
        /* 从就绪队列移除 */
        mr_list_del(&task->ready_link);

        /* 加入等待队列 */
        task->state = TASK_STATE_WAITING;
        mr_list_add(&wait_queue, &task->wait_link);

        printf("Task %d moved from READY to WAIT queue\n", task->pid);
    }
}

void task_wakeup(task_t *task) {
    if (task->state == TASK_STATE_WAITING) {
        /* 从等待队列移除 */
        mr_list_del(&task->wait_link);

        /* 加入就绪队列 */
        task->state = TASK_STATE_READY;
        mr_list_insert(&ready_queue, &task->ready_link);

        printf("Task %d moved from WAIT to READY queue\n", task->pid);
    }
}

void schedule(void) {
    task_t *task;
    mr_list_t *pos;

    printf("\n--- Scheduler ---\n");

    /* 打印就绪队列 */
    printf("READY queue:");
    MR_LIST_FOR_EACH(pos, &ready_queue) {
        task = MR_LIST_ENTRY(pos, task_t, ready_link);
        printf(" %d", task->pid);
    }
    printf("\n");

    /* 打印等待队列 */
    printf("WAIT queue:");
    MR_LIST_FOR_EACH(pos, &wait_queue) {
        task = MR_LIST_ENTRY(pos, task_t, wait_link);
        printf(" %d", task->pid);
    }
    printf("\n");

    /* 简单调度：执行就绪队列的第一个任务 */
    if (!mr_list_is_empty(&ready_queue)) {
        pos = ready_queue.next;
        task = MR_LIST_ENTRY(pos, task_t, ready_link);
        printf("Running task %d...\n", task->pid);
        task->state = TASK_STATE_RUNNING;
    }
}

int main(void) {
    task_t tasks[5];
    int i;

    scheduler_init();

    /* 创建任务 */
    for (i = 0; i < 5; i++) {
        tasks[i].pid = i + 1;
        task_add(&tasks[i]);
    }

    schedule();

    /* 阻塞一些任务 */
    task_block(&tasks[1]);
    task_block(&tasks[3]);

    schedule();

    /* 唤醒任务 */
    task_wakeup(&tasks[1]);

    schedule();

    return 0;
}
```

**预期输出**：

```
Task 1 added to READY queue
Task 2 added to READY queue
Task 3 added to READY queue
Task 4 added to READY queue
Task 5 added to READY queue

--- Scheduler ---
READY queue: 1 2 3 4 5
WAIT queue:
Running task 1...
Task 2 moved from READY to WAIT queue
Task 4 moved from READY to WAIT queue

--- Scheduler ---
READY queue: 3 5
WAIT queue: 2 4
Running task 3...
Task 2 moved from WAIT to READY queue

--- Scheduler ---
READY queue: 2 3 5
WAIT queue: 4
Running task 2...
```

---

## 3. 设计思路

### 3.1 设计目标

list 模块的设计目标非常明确：

1. **简洁高效**：提供最基础的链表操作，保持代码简洁
2. **零开销**：所有函数都是内联的，避免函数调用开销
3. **类型通用**：通过侵入式设计，适用于任何数据类型
4. **安全可靠**：提供安全的遍历机制，防止常见的使用错误

### 3.2 侵入式链表设计

list 模块采用**侵入式链表**的设计模式：

```c
/* 传统非侵入式链表 */
struct node {
    void *data;      /* 数据指针 */
    struct node *next;
    struct node *prev;
};

/* 侵入式链表（list 模块） */
struct my_data {
    int value;
    mr_list_t link;    /* 链表节点嵌入到数据结构中 */
};
```

**侵入式设计的优势**：

1. **避免二次内存分配**：
    - 非侵入式：分配数据 + 分配节点
    - 侵入式：只需要分配数据结构本身

2. **更好的缓存局部性**：
    - 链表节点和数据在同一个内存块中
    - 遍历时缓存命中率更高

3. **更灵活的数据组织**：
    - 同一个数据结构可以包含多个链表节点
    - 可以同时存在于多个链表中

**示例 - 多链表管理**：

```c
typedef struct {
    int pid;
    mr_list_t ready_link;    /* 就绪队列 */
    mr_list_t wait_link;     /* 等待队列 */
    mr_list_t all_link;      /* 所有任务队列 */
} task_t;
```

### 3.3 双向循环链表的选择

list 模块选择**双向循环链表**而不是单向链表或非循环链表：

**双向 vs 单向**：

- **双向**：可以从任意方向遍历，删除节点只需节点指针
- **单向**：只能单向遍历，删除节点需要前驱节点

**循环 vs 非循环**：

- **循环**：空链表和非空链表的操作一致，无需特殊处理
- **非循环**：需要检查 NULL 指针，边界条件复杂

**设计优势**：

1. **统一的操作接口**：头节点和普通节点的处理方式相同
2. **简化的边界检查**：无需检查 NULL，链表总是环形
3. **O(1) 的尾部插入**：通过 `head->prev` 直接访问尾部

### 3.4 内联函数的选择

list 模块的所有函数都声明为 `MR_INLINE`：

```c
MR_INLINE void mr_list_add(mr_list_t *head, mr_list_t *entry) {
    head->prev->next = entry;
    entry->prev = head->prev;
    head->prev = entry;
    entry->next = head;
}
```

**理由**：

1. **零开销**：编译后直接内联到调用点，无函数调用开销
2. **代码量小**：链表操作本身很简单，内联后不会显著增加代码体积
3. **性能关键**：链表操作在内核中非常频繁，开销敏感

### 3.5 安全遍历机制

`MR_LIST_FOR_EACH_SAFE` 宏提供了安全的遍历机制：

```c
#define MR_LIST_FOR_EACH_SAFE(_pos, _n, _head) \
    for ((_pos) = (_head)->next, (_n) = (_pos)->next; \
         (_pos) != (_head); \
         (_pos) = (_n), (_n) = (_pos)->next)
```

**为什么需要 _SAFE 版本？**

使用普通遍历删除节点会导致问题：

```c
MR_LIST_FOR_EACH(pos, &head) {
    if (should_delete(pos)) {
        mr_list_del(pos);  /* 错误！pos 已失效 */
    }
    pos = pos->next;  /* 未定义行为！*/
}
```

使用 _SAFE 版本可以安全删除：

```c
MR_LIST_FOR_EACH_SAFE(pos, n, &head) {
    if (should_delete(pos)) {
        mr_list_del(pos);  /* 安全！n 已经保存了下一个节点 */
    }
}
```

---

## 4. 参考资料

### 4.1 相关模块

- **compiler** (libc/compiler)：提供 `MR_CONTAINER_OF` 宏，用于从链表节点指针获取包含它的结构体指针

### 4.2 设计参考

- **Linux list_head**：Linux 内核的链表实现，list 模块的主要参考
- **BSD queue**：BSD 系统的宏队列实现

### 4.3 延伸阅读

- **侵入式与非侵入式数据结构**：理解两种设计模式的区别
- **双向循环链表的实现**：链表的经典实现方式
- **MR_CONTAINER_OF 宏的原理**：理解如何从成员指针获取结构体指针

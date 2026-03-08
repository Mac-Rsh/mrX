# spinlock - 自旋锁

## 1. 模块概述

### 1.1 功能介绍

spinlock 模块实现了自旋锁（Spinlock）同步原语。自旋锁是一种简单的锁机制，用于保护多线程或多核环境下的共享资源。

当线程尝试获取已被占用的自旋锁时，它不会让出 CPU 进入睡眠，而是在一个循环中反复检查锁是否可用（即"自旋"
）。一旦锁被释放，等待的线程可以立即获取锁。

自旋锁的核心特点：

- **忙等待**：等待锁的线程会持续占用 CPU，直到获取锁
- **无上下文切换**：不会触发线程调度和上下文切换
- **低延迟**：适合锁持有时间很短的场景

### 1.2 主要特性

- **基于原子操作**：使用 CAS（Compare-And-Swap）指令实现，保证多核安全
- **零开销抽象**：所有函数都是内联函数，无函数调用开销
- **中断安全版本**：提供 `irqsave/irqrestore` 版本，用于中断上下文
- **轻量级**：只有一个原子变量，内存占用极小
- **可移植**：通过底层原子操作接口实现，支持多种平台

### 1.3 适用场景

spinlock 模块适用于以下场景：

- **短时间锁定**：锁持有时间非常短（微秒级）
- **多核系统**：多核 CPU 并发访问共享资源
- **中断上下文**：中断处理程序中不能睡眠，必须使用自旋锁
- **内核数据结构**：保护链表、哈希表等内核数据结构

**不适用场景**：

- **长时间锁定**：会导致其他等待线程空转，浪费 CPU
- **单核系统**：单核系统上自旋锁效率低下，应该使用互斥锁
- **可能睡眠的代码**：持有自旋锁时不能睡眠，否则可能导致死锁

### 1.4 使用注意事项

⚠️ **重要警告**：

1. **持有自旋锁时不能睡眠**：持有自旋锁的代码不能调用可能睡眠的函数，否则可能导致系统死锁或性能严重下降

2. **锁持有时间要短**：自旋锁适合保护临界区很小的代码，临界区执行时间应尽量短

3. **禁止递归锁**：不要在持有自旋锁的情况下再次获取同一个锁，会导致死锁

4. **中断上下文**：在中断处理程序中获取自旋锁时，必须使用 `irqsave/irqrestore` 版本

---

## 2. 使用指南

### 2.1 核心数据结构

#### mr_spinlock_t

```c
typedef mr_atomic_t mr_spinlock_t;
```

自旋锁类型，本质上是一个原子整数：

- **0**：锁未被占用（解锁状态）
- **1**：锁已被占用（加锁状态）

**内存占用**：

- 通常为 4 字节（32 位系统）

### 2.2 主要接口说明

#### 初始化接口

##### MR_SPINLOCK_INIT()

```c
#define MR_SPINLOCK_INIT() MR_ATOMIC_INIT(0)
```

**功能**：静态初始化自旋锁

**示例**：

```c
static mr_spinlock_t my_lock = MR_SPINLOCK_INIT();
```

##### mr_spinlock_init()

```c
void mr_spinlock_init(mr_spinlock_t *lock);
```

**功能**：动态初始化自旋锁

**参数**：

- `lock`：自旋锁指针

**示例**：

```c
mr_spinlock_t lock;
mr_spinlock_init(&lock);
```

#### 基础加锁/解锁接口

##### mr_spinlock_lock()

```c
void mr_spinlock_lock(mr_spinlock_t *lock);
```

**功能**：获取自旋锁（如果锁已被占用，则自旋等待）

**参数**：

- `lock`：自旋锁指针

**实现原理**：

- 使用 CAS 指令原子地将锁从 0 设置为 1
- 如果锁已经是 1，则循环重试

**注意事项**：

- 可能会永久阻塞（如果锁永远不会被释放）
- 调用此函数前必须确保不会发生中断抢占（如果需要，使用 `irqsave` 版本）

##### mr_spinlock_unlock()

```c
void mr_spinlock_unlock(mr_spinlock_t *lock);
```

**功能**：释放自旋锁

**参数**：

- `lock`：自旋锁指针

**实现原理**：

- 原子地将锁设置为 0

##### mr_spinlock_is_locked()

```c
mr_bool_t mr_spinlock_is_locked(mr_spinlock_t *lock);
```

**功能**：检查自旋锁是否已被占用

**参数**：

- `lock`：自旋锁指针

**返回值**：

- `MR_TRUE`：锁已被占用
- `MR_FALSE`：锁未被占用

**使用场景**：

- 调试和断言
- 检查锁的状态

#### 中断安全接口

##### mr_spinlock_lock_irqsave()

```c
int mr_spinlock_lock_irqsave(mr_spinlock_t *lock);
```

**功能**：禁用中断并获取自旋锁

**参数**：

- `lock`：自旋锁指针

**返回值**：中断掩码（用于后续恢复中断状态）

**使用场景**：

- 中断处理程序中获取锁
- 防止中断处理程序与当前线程竞争锁

**注意事项**：

- 返回的中断掩码必须保存，用于后续恢复

##### mr_spinlock_unlock_irqrestore()

```c
void mr_spinlock_unlock_irqrestore(mr_spinlock_t *lock, int mask);
```

**功能**：释放自旋锁并恢复中断状态

**参数**：

- `lock`：自旋锁指针
- `mask`：中断掩码（由 `lock_irqsave` 返回）

**注意事项**：

- 必须与 `lock_irqsave` 配对使用
- mask 必须是之前 `lock_irqsave` 返回的值

### 2.3 典型应用案例

#### 案例 1：基础用法 - 保护共享计数器

```c
#include <mrX/kernel/mr_spinlock.h>
#include <stdio.h>

/* 共享数据 */
static int shared_counter = 0;
static mr_spinlock_t counter_lock = MR_SPINLOCK_INIT();

void increment_counter(void) {
    /* 获取锁 */
    mr_spinlock_lock(&counter_lock);

    /* 临界区：访问共享数据 */
    shared_counter++;
    printf("Counter: %d\n", shared_counter);

    /* 释放锁 */
    mr_spinlock_unlock(&counter_lock);
}

int get_counter(void) {
    int value;

    mr_spinlock_lock(&counter_lock);
    value = shared_counter;
    mr_spinlock_unlock(&counter_lock);

    return value;
}
```

#### 案例 2：保护链表操作

```c
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/kernel/mr_list.h>

typedef struct {
    int id;
    mr_list_t link;
} data_item_t;

/* 全局链表和锁 */
static mr_list_t data_list;
static mr_spinlock_t list_lock = MR_SPINLOCK_INIT();

void init_data_list(void) {
    mr_list_init(&data_list);
}

/* 添加数据项到链表 */
void add_data_item(data_item_t *item) {
    mr_spinlock_lock(&list_lock);

    /* 临界区：修改链表 */
    mr_list_add(&data_list, &item->link);

    mr_spinlock_unlock(&list_lock);
}

/* 查找并删除数据项 */
data_item_t *find_and_remove(int id) {
    mr_list_t *pos;
    data_item_t *item = NULL;
    data_item_t *result;

    mr_spinlock_lock(&list_lock);

    /* 临界区：遍历并修改链表 */
    MR_LIST_FOR_EACH(pos, &data_list) {
        item = MR_LIST_ENTRY(pos, data_item_t, link);
        if (item->id == id) {
            mr_list_del(pos);
            result = item;
            goto out;
        }
    }
    result = NULL;

out:
    mr_spinlock_unlock(&list_lock);
    return result;
}
```

#### 案例 3：中断上下文使用 irqsave/irqrestore

```c
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/port/mr_port_irq.h>

/* 共享数据 */
static volatile int device_status = 0;
static mr_spinlock_t status_lock = MR_SPINLOCK_INIT();

/* 中断处理程序 */
void irq_handler(void) {
    int mask;

    /* 禁用中断并获取锁 */
    mask = mr_spinlock_lock_irqsave(&status_lock);

    /* 临界区：访问共享数据（此时中断已禁用） */
    device_status++;

    /* 释放锁并恢复中断 */
    mr_spinlock_unlock_irqrestore(&status_lock, mask);
}

/* 线程函数 */
void thread_func(void) {
    int mask;

    /* 禁用中断并获取锁 */
    mask = mr_spinlock_lock_irqsave(&status_lock);

    /* 临界区：访问共享数据 */
    device_status = 100;

    /* 释放锁并恢复中断 */
    mr_spinlock_unlock_irqrestore(&status_lock, mask);
}
```

#### 案例 4：读者-写者锁模式

```c
#include <mrX/kernel/mr_spinlock.h>

/* 简单的读优先锁实现 */
typedef struct {
    int readers;          /* 当前读者数量 */
    mr_spinlock_t lock;   /* 保护 readers 字段 */
} rw_lock_t;

void rw_lock_init(rw_lock_t *rw) {
    rw->readers = 0;
    mr_spinlock_init(&rw->lock);
}

/* 获取读锁（多个读者可以同时持有） */
void rw_lock_read(rw_lock_t *rw) {
    mr_spinlock_lock(&rw->lock);
    rw->readers++;
    mr_spinlock_unlock(&rw->lock);
}

/* 释放读锁 */
void rw_unlock_read(rw_lock_t *rw) {
    mr_spinlock_lock(&rw->lock);
    rw->readers--;
    mr_spinlock_unlock(&rw->lock);
}

/* 获取写锁（独占访问） */
void rw_lock_write(rw_lock_t *rw) {
    /* 等待所有读者完成 */
    while (1) {
        mr_spinlock_lock(&rw->lock);
        if (rw->readers == 0) {
            /* 没有读者，获取写锁 */
            break;
        }
        mr_spinlock_unlock(&rw->lock);
        /* 自旋等待 */
    }
}

/* 释放写锁 */
void rw_unlock_write(rw_lock_t *rw) {
    mr_spinlock_unlock(&rw->lock);
}

/* 使用示例 */
static rw_lock_t data_lock;
static int shared_data = 0;

/* 读者线程 */
void reader_thread(void) {
    rw_lock_read(&data_lock);
    printf("Read: %d\n", shared_data);
    rw_unlock_read(&data_lock);
}

/* 写者线程 */
void writer_thread(void) {
    rw_lock_write(&data_lock);
    shared_data++;
    printf("Write: %d\n", shared_data);
    rw_unlock_write(&data_lock);
}
```

#### 案例 5：正确使用示例与常见陷阱

```c
#include <mrX/kernel/mr_spinlock.h>
#include <stdio.h>

static mr_spinlock_t my_lock = MR_SPINLOCK_INIT();
static int shared_data = 0;

/* ✓ 正确：临界区很短 */
void correct_usage_1(void) {
    mr_spinlock_lock(&my_lock);
    shared_data++;  /* 只有一条简单指令 */
    mr_spinlock_unlock(&my_lock);
}

/* ✓ 正确：提前退出也要释放锁 */
void correct_usage_2(void) {
    mr_spinlock_lock(&my_lock);

    if (shared_data > 100) {
        mr_spinlock_unlock(&my_lock);  /* 提前释放锁 */
        return;
    }

    shared_data++;
    mr_spinlock_unlock(&my_lock);
}

/* ✗ 错误：临界区太大，包含 IO 操作 */
void wrong_usage_1(void) {
    mr_spinlock_lock(&my_lock);

    /* 错误：包含可能耗时的操作 */
    printf("This is bad!\n");  /* printf 可能很慢 */

    /* 错误：包含循环 */
    for (int i = 0; i < 1000000; i++) {
        /* 长时间计算 */
    }

    mr_spinlock_unlock(&my_lock);
}

/* ✗ 错误：持有锁时睡眠 */
void wrong_usage_2(void) {
    mr_spinlock_lock(&my_lock);

    /* 错误：不能在持有锁时睡眠 */
    // sleep(1);  /* 这会导致严重问题！ */

    mr_spinlock_unlock(&my_lock);
}

/* ✗ 错误：递归获取锁（死锁） */
void wrong_usage_3(void) {
    mr_spinlock_lock(&my_lock);

    /* 错误：再次获取同一个锁（死锁！） */
    // mr_spinlock_lock(&my_lock);  /* 永远阻塞 */

    mr_spinlock_unlock(&my_lock);
}

/* ✓ 正确：使用 goto 处理错误和资源清理 */
void correct_usage_3(int *result) {
    mr_spinlock_lock(&my_lock);

    if (shared_data < 0) {
        mr_spinlock_unlock(&my_lock);
        *result = -1;
        return;
    }

    if (shared_data > 1000) {
        mr_spinlock_unlock(&my_lock);
        *result = -2;
        return;
    }

    shared_data++;
    *result = 0;

    mr_spinlock_unlock(&my_lock);
}

/* ✓ 更好的方式：使用统一出口 */
void correct_usage_4(int *result) {
    mr_spinlock_lock(&my_lock);

    if (shared_data < 0) {
        *result = -1;
        goto out;
    }

    if (shared_data > 1000) {
        *result = -2;
        goto out;
    }

    shared_data++;
    *result = 0;

out:
    mr_spinlock_unlock(&my_lock);
}
```

---

## 3. 设计思路

### 3.1 设计目标

spinlock 模块的设计目标：

1. **简单高效**：提供最基本的加锁/解锁功能，保持接口简洁
2. **零开销**：所有函数都是内联的，编译后无函数调用开销
3. **中断安全**：提供中断安全的版本，用于中断上下文
4. **可移植**：基于底层原子操作接口，支持多种平台

### 3.2 自旋锁的实现原理

spinlock 模块使用 CAS（Compare-And-Swap）指令实现自旋锁：

```c
void mr_spinlock_lock(mr_spinlock_t *lock) {
    mr_atomic_t unlock;

    do {
        unlock = 0;
        /* 尝试将 lock 从 0 设置为 1 */
        unlock = mr_atomic_compare_exchange(lock, &unlock, 1);
    } while (!unlock);  /* 如果返回 0，说明成功加锁 */
}
```

**工作原理**：

1. `mr_atomic_compare_exchange(lock, &unlock, 1)` 原子地执行：
    - 如果 `*lock == unlock`（即 `*lock == 0`），则将 `*lock` 设置为 `1`，返回原始值 `0`
    - 如果 `*lock != unlock`（即 `*lock == 1`，锁已被占用），则不修改，返回当前值 `1`

2. 返回值为 `0` 表示成功加锁，循环结束
3. 返回值为非 `0` 表示锁已被占用，继续循环（自旋）

**为什么不会"饿死"？**

- 自旋锁假设锁持有时间很短
- 在多核系统上，持有锁的 CPU 很快会释放锁
- 等待的线程会持续检查，一旦锁释放就能立即获取

### 3.3 中断安全版本的必要性

在中断处理程序中使用自旋锁需要特别小心：

**问题场景**：

```c
/* 线程 A */
void thread_a(void) {
    mr_spinlock_lock(&lock);  /* 获取锁 */
    /* 临界区 */
    mr_spinlock_unlock(&lock);
}

/* 中断处理程序 */
void irq_handler(void) {
    mr_spinlock_lock(&lock);  /* 尝试获取锁 */
    /* 如果中断发生在线程 A 持有锁时，这里会死锁！ */
}
```

**死锁原因**：

1. 线程 A 持有锁
2. 中断发生，中断处理程序抢占线程 A
3. 中断处理程序尝试获取同一个锁
4. 因为线程 A 持有锁，中断处理程序永远无法获取锁
5. 线程 A 无法运行（被中断抢占），无法释放锁
6. **死锁！**

**解决方案**：

```c
void thread_a(void) {
    int mask;

    /* 禁用中断并获取锁 */
    mask = mr_spinlock_lock_irqsave(&lock);

    /* 临界区（此时中断被禁用，不会被抢占） */

    /* 释放锁并恢复中断 */
    mr_spinlock_unlock_irqrestore(&lock, mask);
}
```

**工作原理**：

- `lock_irqsave` 先禁用中断，再获取锁
- 在持有锁期间不会发生中断抢占
- 不会出现中断处理程序尝试获取同一个锁的情况

### 3.4 自旋锁 vs 互斥锁

| 特性         | 自旋锁       | 互斥锁      |
|------------|-----------|----------|
| **等待方式**   | 忙等待（自旋）   | 睡眠等待     |
| **CPU 开销** | 持续占用 CPU  | 让出 CPU   |
| **适用场景**   | 短时间锁定     | 长时间锁定    |
| **上下文切换**  | 无         | 有（调度开销）  |
| **延迟**     | 极低        | 较高       |
| **单核效率**   | 差（浪费 CPU） | 好        |
| **多核效率**   | 好（临界区很短时） | 好        |
| **中断上下文**  | 可用        | 不可用（会睡眠） |

**选择建议**：

- **内核代码**：优先使用自旋锁（临界区很短）
- **驱动程序**：根据临界区大小选择
- **用户空间**：通常使用互斥锁（临界区可能较长）

### 3.5 为什么所有函数都是内联的

spinlock 模块的所有函数都声明为 `MR_INLINE`：

**理由**：

1. **性能关键**：锁操作非常频繁，函数调用开销不可忽视
2. **代码简单**：锁操作的代码非常简单，内联后不会显著增加代码体积
3. **优化机会**：内联后编译器可以更好地优化上下文

**对比**：

```c
/* 非内联版本 */
void lock(mr_spinlock_t *lock) {
    /* 函数调用开销：保存寄存器、跳转、返回 */
    ...
}

/* 内联版本 */
MR_INLINE void lock(mr_spinlock_t *lock) {
    /* 直接展开到调用点，无额外开销 */
    ...
}
```

---

## 4. 参考资料

### 4.1 设计参考

- **Linux spinlock**：Linux 内核的自旋锁实现
- **FreeBSD sxlock**：FreeBSD 的睡眠锁和自旋锁

### 4.2 延伸阅读

- **CAS 指令的原理**：理解 Compare-And-Swap 的工作机制
- **内存屏障（Memory Barrier）**：理解多核环境下的内存一致性
- **锁的优化技术**：Ticket Lock、MCS Lock 等高级自旋锁
- **死锁的预防和检测**：理解死锁的产生条件和避免方法

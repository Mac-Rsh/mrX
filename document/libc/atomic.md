# atomic - 原子操作

## 1. 模块概述

### 1.1 功能介绍

atomic 模块提供原子操作支持，确保对共享变量的访问和修改在多任务环境中的原子性。它提供了一组原子操作接口，包括加载、存储、交换、比较交换以及各种运算操作。

该模块支持两种配置模式：3RD 模式（使用第三方原子库）和自定义模式（基于中断屏蔽实现）。

### 1.2 主要特性

- **原子性保证**：所有操作都是原子的，不会被中断
- **无锁算法**：基于中断屏蔽实现，适用于单核系统
- **CAS 支持**：提供比较交换（Compare-And-Swap）操作
- **运算操作**：支持加、减、与、或、异或等运算

### 1.3 适用场景

atomic 模块适用于以下场景：

- **引用计数**：对象的引用计数管理
- **状态标志**：多任务间的状态同步
- **无锁队列**：实现无锁数据结构
- **中断安全**：在中断和主循环间共享数据

---

## 2. 使用指南

### 2.1 数据类型

#### mr_atomic_t - 原子类型

```c
typedef mr_intptr_t                     mr_atomic_t;
```

**说明**：

- 原子类型本质上是指针大小的有符号整数
- 32 位系统：32 位整数
- 64 位系统：64 位整数

#### MR_ATOMIC_INIT - 初始化宏

```c
#define MR_ATOMIC_INIT(_val)            (_val)
```

**功能**：定义原子变量的初始值

**示例**：

```c
mr_atomic_t counter = MR_ATOMIC_INIT(0);
```

### 2.2 基础操作

#### mr_atomic_init() - 初始化

```c
MR_INLINE void mr_atomic_init(volatile mr_atomic_t *atomic, mr_atomic_t val);
```

**功能**：初始化原子变量

**参数**：

- `atomic`：原子变量指针
- `val`：初始值

**示例**：

```c
mr_atomic_t counter;
mr_atomic_init(&counter, 0);
```

#### mr_atomic_load() - 加载

```c
MR_INLINE mr_atomic_t mr_atomic_load(volatile mr_atomic_t *atomic);
```

**功能**：原子地读取原子变量的值

**参数**：

- `atomic`：原子变量指针

**返回值**：原子变量的当前值

**实现原理**：

```c
MR_INLINE mr_atomic_t mr_atomic_load(volatile mr_atomic_t *atomic) {
    mr_atomic_t tmp;
    int mask;

    /* 禁用中断 */
    mask = mr_port_irq_disable();

    /* 读取原子值 */
    tmp = *atomic;

    /* 恢复中断 */
    mr_port_irq_enable(mask);
    return tmp;
}
```

#### mr_atomic_store() - 存储

```c
MR_INLINE void mr_atomic_store(volatile mr_atomic_t *atomic, mr_atomic_t val);
```

**功能**：原子地写入值到原子变量

**参数**：

- `atomic`：原子变量指针
- `val`：要存储的值

**示例**：

```c
mr_atomic_t counter;

mr_atomic_store(&counter, 42);
mr_atomic_t value = mr_atomic_load(&counter);
/* value = 42 */
```

### 2.3 交换操作

#### mr_atomic_exchange() - 交换

```c
MR_INLINE mr_atomic_t mr_atomic_exchange(volatile mr_atomic_t *atomic,
                                         mr_atomic_t val);
```

**功能**：原子地将新值写入变量，并返回旧值

**参数**：

- `atomic`：原子变量指针
- `val`：新的值

**返回值**：交换前的旧值

**示例**：

```c
mr_atomic_t counter = MR_ATOMIC_INIT(10);

mr_atomic_t old = mr_atomic_exchange(&counter, 20);
printf("Old value: %d, New value: %d\n", old, mr_atomic_load(&counter));
/* 输出: Old value: 10, New value: 20 */
```

#### mr_atomic_compare_exchange() - 比较交换（CAS）

```c
MR_INLINE mr_bool_t mr_atomic_compare_exchange(volatile mr_atomic_t *atomic,
                                               mr_atomic_t *val,
                                               mr_atomic_t des);
```

**功能**：比较并交换（Compare-And-Swap）

**参数**：

- `atomic`：原子变量指针
- `val`：指向期望值的指针（输入/输出）
- `des`：期望的新值

**返回值**：

- `MR_TRUE`：交换成功（atomic == *val）
- `MR_FALSE`：交换失败（*val 更新为当前值）

**示例**：

```c
mr_atomic_t counter = MR_ATOMIC_INIT(10);
mr_atomic_t expected = 10;
mr_bool_t success;

/* 尝试将 10 替换为 20 */
success = mr_atomic_compare_exchange(&counter, &expected, 20);
if (success) {
    printf("CAS succeeded: counter is now 20\n");
} else {
    printf("CAS failed: counter was %d, not %d\n", expected, 10);
}
```

**应用场景**：

1. **无锁栈**
2. **无锁队列**
3. **引用计数**
4. **状态机**

### 2.4 运算操作

#### mr_atomic_fetch_add() - 加法

```c
MR_INLINE mr_atomic_t mr_atomic_fetch_add(volatile mr_atomic_t *atomic,
                                          mr_atomic_t val);
```

**功能**：原子地加法，返回操作前的值

**参数**：

- `atomic`：原子变量指针
- `val`：要加的值

**返回值**：操作前的旧值

**示例**：

```c
mr_atomic_t counter = MR_ATOMIC_INIT(10);

mr_atomic_t old = mr_atomic_fetch_add(&counter, 5);
printf("Old: %d, New: %d\n", old, mr_atomic_load(&counter));
/* 输出: Old: 10, New: 15 */
```

#### mr_atomic_fetch_sub() - 减法

```c
MR_INLINE mr_atomic_t mr_atomic_fetch_sub(volatile mr_atomic_t *atomic,
                                          mr_atomic_t val);
```

**功能**：原子地减法，返回操作前的值

**示例**：

```c
mr_atomic_t counter = MR_ATOMIC_INIT(10);

mr_atomic_t old = mr_atomic_fetch_sub(&counter, 3);
printf("Old: %d, New: %d\n", old, mr_atomic_load(&counter));
/* 输出: Old: 10, New: 7 */
```

#### mr_atomic_fetch_and() - 与运算

```c
MR_INLINE mr_atomic_t mr_atomic_fetch_and(volatile mr_atomic_t *atomic,
                                          mr_atomic_t val);
```

**功能**：原子地按位与，返回操作前的值

**示例**：

```c
mr_atomic_t flags = MR_ATOMIC_INIT(0xFF);

mr_atomic_fetch_and(&flags, 0x0F);
printf("Flags: 0x%X\n", mr_atomic_load(&flags));
/* 输出: Flags: 0xF */
```

#### mr_atomic_fetch_or() - 或运算

```c
MR_INLINE mr_atomic_t mr_atomic_fetch_or(volatile mr_atomic_t *atomic,
                                         mr_atomic_t val);
```

**功能**：原子地按位或，返回操作前的值

**示例**：

```c
mr_atomic_t flags = MR_ATOMIC_INIT(0x01);

mr_atomic_fetch_or(&flags, 0x10);
printf("Flags: 0x%X\n", mr_atomic_load(&flags));
/* 输出: Flags: 0x11 */
```

#### mr_atomic_fetch_xor() - 异或运算

```c
MR_INLINE mr_atomic_t mr_atomic_fetch_xor(volatile mr_atomic_t *atomic,
                                          mr_atomic_t val);
```

**功能**：原子地按位异或，返回操作前的值

**示例**：

```c
mr_atomic_t value = MR_ATOMIC_INIT(0xFF);

mr_atomic_fetch_xor(&value, 0xFF);
printf("Value: 0x%X\n", mr_atomic_load(&value));
/* 输出: Value: 0x0 */
```

### 2.5 配置模式

#### 3RD 模式（第三方库模式）

```c
#define MR_USE_ATOMIC_3RD
```

**特点**：

- 使用第三方原子库实现
- 可能使用硬件原子指令
- 适合支持原子指令的平台

**定义**：

```c
#if defined(MR_USE_ATOMIC_3RD)
/* In 'mrX/3party/libc/mr_atomic.h' */
#endif
```

#### 自定义模式（基于中断屏蔽）

**特点**：

- 基于中断屏蔽实现
- 适用于单核嵌入式系统
- 不依赖硬件原子指令

**实现原理**：

```c
#define ATOMIC_OP(_op, _atomic, _val) \
    do { \
        int mask; \
        mask = mr_port_irq_disable();  /* 禁用中断 */ \
        _op;                           /* 执行操作 */ \
        mr_port_irq_enable(mask);      /* 恢复中断 */ \
    } while (0)
```

### 2.6 典型应用案例

#### 案例 1：引用计数

```c
#include <mrX/libc/mr_atomic.h>

typedef struct {
    mr_atomic_t ref_count;
    /* ... 其他成员 ... */
} ref_object_t;

void ref_object_get(ref_object_t *obj) {
    mr_atomic_fetch_add(&obj->ref_count, 1);
}

void ref_object_put(ref_object_t *obj) {
    if (mr_atomic_fetch_sub(&obj->ref_count, 1) == 1) {
        /* 引用计数归零，释放对象 */
        mr_free(obj);
    }
}

ref_object_t *ref_object_create(void) {
    ref_object_t *obj = mr_malloc(sizeof(ref_object_t));
    if (obj != MR_NULL) {
        mr_atomic_init(&obj->ref_count, 1);
    }
    return obj;
}
```

#### 案例 2：状态标志

```c
#include <mrX/libc/mr_atomic.h>

#define STATE_IDLE     0
#define STATE_BUSY     1
#define STATE_ERROR    2

typedef struct {
    mr_atomic_t state;
} device_t;

mr_bool_t device_try_acquire(device_t *dev) {
    mr_atomic_t expected = STATE_IDLE;

    /* CAS：尝试从 IDLE 转换到 BUSY */
    if (mr_atomic_compare_exchange(&dev->state, &expected, STATE_BUSY)) {
        return MR_TRUE;  /* 成功获取 */
    }
    return MR_FALSE;  /* 设备忙或错误状态 */
}

void device_release(device_t *dev) {
    mr_atomic_store(&dev->state, STATE_IDLE);
}

/* 中断处理函数 */
void irq_handler(void) {
    device_t *dev = get_device();

    /* 设置错误状态 */
    mr_atomic_store(&dev->state, STATE_ERROR);
}
```

#### 案例 3：无锁环形缓冲区索引

```c
#include <mrX/libc/mr_atomic.h>

#define BUFFER_SIZE  256

typedef struct {
    mr_uint8_t data[BUFFER_SIZE];
    mr_atomic_t read_pos;
    mr_atomic_t write_pos;
} ring_buffer_t;

mr_bool_t ring_buffer_write(ring_buffer_t *rb, mr_uint8_t data) {
    mr_atomic_t read_pos = mr_atomic_load(&rb->read_pos);
    mr_atomic_t write_pos = mr_atomic_load(&rb->write_pos);
    mr_atomic_t next_pos = (write_pos + 1) % BUFFER_SIZE;

    /* 检查缓冲区是否满 */
    if (next_pos == read_pos) {
        return MR_FALSE;  /* 缓冲区满 */
    }

    /* 写入数据 */
    rb->data[write_pos] = data;
    mr_atomic_store(&rb->write_pos, next_pos);
    return MR_TRUE;
}

mr_bool_t ring_buffer_read(ring_buffer_t *rb, mr_uint8_t *data) {
    mr_atomic_t read_pos = mr_atomic_load(&rb->read_pos);
    mr_atomic_t write_pos = mr_atomic_load(&rb->write_pos);

    /* 检查缓冲区是否空 */
    if (read_pos == write_pos) {
        return MR_FALSE;  /* 缓冲区空 */
    }

    /* 读取数据 */
    *data = rb->data[read_pos];
    mr_atomic_t next_pos = (read_pos + 1) % BUFFER_SIZE;
    mr_atomic_store(&rb->read_pos, next_pos);
    return MR_TRUE;
}
```

#### 案例 4：位标志操作

```c
#include <mrX/libc/mr_atomic.h>

#define FLAG_READY   (1U << 0)
#define FLAG_ERROR   (1U << 1)
#define FLAG_BUSY    (1U << 2)

typedef struct {
    mr_atomic_t flags;
} status_t;

void set_flag(status_t *status, mr_uint32_t flag) {
    mr_atomic_fetch_or(&status->flags, flag);
}

void clear_flag(status_t *status, mr_uint32_t flag) {
    mr_atomic_fetch_and(&status->flags, ~flag);
}

mr_bool_t is_flag_set(status_t *status, mr_uint32_t flag) {
    return (mr_atomic_load(&status->flags) & flag) ? MR_TRUE : MR_FALSE;
}

/* 使用 */
status_t status;
mr_atomic_init(&status.flags, 0);

set_flag(&status, FLAG_BUSY);
/* ... 执行操作 ... */
clear_flag(&status, FLAG_BUSY);
```

#### 案例 5：自旋锁

```c
#include <mrX/libc/mr_atomic.h>

typedef struct {
    mr_atomic_t locked;
} spinlock_t;

void spinlock_init(spinlock_t *lock) {
    mr_atomic_init(&lock->locked, 0);
}

void spinlock_lock(spinlock_t *lock) {
    mr_atomic_t expected = 0;

    /* 自旋等待获取锁 */
    while (!mr_atomic_compare_exchange(&lock->locked, &expected, 1)) {
        expected = 0;  /* 重置期望值 */
    }
}

void spinlock_unlock(spinlock_t *lock) {
    mr_atomic_store(&lock->locked, 0);
}
```

---

## 3. 设计思路

### 3.1 设计目标

atomic 模块的设计目标：

1. **原子性**：保证操作的原子性
2. **中断安全**：在中断和主循环间安全共享数据
3. **无锁**：基于中断屏蔽，无需互斥锁
4. **可移植性**：不依赖特定硬件指令

### 3.2 实现原理

#### 中断屏蔽实现

```c
MR_INLINE mr_atomic_t mr_atomic_load(volatile mr_atomic_t *atomic) {
    mr_atomic_t tmp;
    int mask;

    /* 禁用中断，保存旧的屏蔽状态 */
    mask = mr_port_irq_disable();

    /* 临界区：读取原子值 */
    tmp = *atomic;

    /* 恢复中断，恢复之前的屏蔽状态 */
    mr_port_irq_enable(mask);

    return tmp;
}
```

**关键点**：

1. `mr_port_irq_disable()` 返回中断屏蔽状态
2. `mr_port_irq_enable(mask)` 恢复之前的状态
3. 支持中断嵌套

#### 为什么可以保证原子性？

1. **单核系统**：只有一个执行单元
2. **中断屏蔽**：禁止任务切换
3. **临界区保护**：操作期间不会被中断

**多核系统**：此实现不适用，需要使用硬件原子指令（如 ARM 的 LDREX/STREX）。

### 3.3 设计权衡

#### 原子操作 vs 互斥锁

| 特性 | 原子操作 | 互斥锁 |
|------|---------|--------|
| 开销 | 低 | 高 |
| 中断安全 | 是 | 否（需特殊处理） |
| 复杂度 | 简单 | 复杂 |
| 死锁 | 无 | 可能 |
| 适用场景 | 单变量 | 复杂临界区 |

#### 为什么基于中断屏蔽？

**优势**：

1. **简单**：实现简单，易于理解
2. **可靠**：不依赖复杂的硬件指令
3. **通用**：适用于大多数嵌入式架构

**劣势**：

1. **延迟**：禁用中断会增加中断延迟
2. **单核限制**：不适用于多核系统
3. **性能**：频繁使用可能影响系统响应

**使用建议**：

- 临界区要短
- 不要在原子操作中调用耗时函数
- 适合保护简单的变量操作

#### 中断嵌套支持

```c
int mask = mr_port_irq_disable();
/* 临界区 */
mr_port_irq_enable(mask);
```

**工作原理**：

1. `mr_port_irq_disable()` 返回之前的屏蔽状态
2. 嵌套调用时，每次保存自己的状态
3. 恢复时按相反顺序恢复

**示例**：

```c
/* 外层调用 */
void outer_function(void) {
    int mask1 = mr_port_irq_disable();  /* 假设返回 0 */

    /* 内层调用 */
    void inner_function(void) {
        int mask2 = mr_port_irq_disable();  /* 返回 1 */
        /* ... */
        mr_port_irq_enable(mask2);  /* 恢复到状态 1 */
    }

    inner_function();

    mr_port_irq_enable(mask1);  /* 恢复到状态 0，重新启用中断 */
}
```

---

## 4. 参考资料

### 4.1 相关模块

atomic 模块被以下模块使用：

- **ref**：引用计数模块
- **object**：对象系统的引用计数
- **driver**：驱动状态管理
- **spinlock**：自旋锁实现

### 4.2 算法参考

- **CAS (Compare-And-Swap)**：无锁算法的基础
- **LL/SC (Load-Linked/Store-Conditional)**：另一种原子操作原语
- **无锁数据结构**：基于原子操作的无锁队列、栈等

### 4.3 延伸阅读

- **内存屏障**：理解内存顺序和可见性
- **无锁编程**：设计和实现无锁算法
- **ARM 原子指令**：LDREX/STREX 指令的使用
- **x86 原子指令**：LOCK 前缀和 CMPXCHG 指令

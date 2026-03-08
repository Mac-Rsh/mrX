# wait - 等待机制

## 1. 模块概述

### 1.1 功能介绍

wait 模块实现了一个事件等待和通知机制，类似于 POSIX 的 poll/select 或 Linux 的
epoll。它允许一个或多个等待者（waiter）等待某个可等待对象（waitable）的状态变化，当条件满足时触发回调。

等待机制的核心功能：

- **事件注册**：waiters 可以注册到 waitable，等待特定事件
- **事件通知**：当 waitable 状态变化时，通知所有注册的 waiters
- **触发模式**：支持电平触发和边沿触发
- **多事件类型**：支持 IN（可读）、OUT（可写）、ERR（错误）事件
- **并发安全**：使用自旋锁保护，支持多线程和中断上下文

### 1.2 主要特性

- **双角色设计**：waitable（被等待对象）和 waiter（等待者）
- **灵活的事件检查**：通过 check 函数自定义事件检测逻辑
- **电平/边沿触发**：支持两种触发模式
- **generation 机制**：防止嵌套检查时的竞争条件
- **oplist 保护**：使用临时链表保护内存安全
- **对象集成**：继承自 mr_object_t，支持引用计数
- **零配置**：不需要复杂的初始化流程

### 1.3 适用场景

wait 模块适用于以下场景：

- **设备状态通知**：设备状态变化时通知多个监听者
- **跨模块事件**：不同模块间的事件通知机制
- **异步操作完成**：异步操作完成时通知等待者
- **资源可用性**：资源（如缓冲区）可用时通知多个消费者
- **一对多通知**：一个事件源通知多个监听器

**不适用场景**：

- **简单的互斥同步**：应该使用信号量或互斥锁
- **单次事件**：应该使用简单的标志位或条件变量

### 1.4 使用注意事项

⚠️ **重要警告**：

1. **回调函数限制**：waiter 回调函数中不能执行耗时操作，会阻塞其他 waiters

2. **避免重入**：不要在回调函数中调用 `mr_waitable_complete`，可能导致死锁

3. **事件类型匹配**：waiter 等待的事件类型必须与 waitable 产生的事件类型匹配

4. **内存管理**：删除 waitable 前必须先取消所有 waiters

5. **generation 检查**：check 函数中不要调用会触发新的 complete 的操作

---

## 2. 使用指南

### 2.1 核心数据结构

#### mr_waitable_t

```c
typedef struct mr_waitable {
    mr_object_t parent;    /* 父类：对象系统 */
    mr_uint64_t last;      /* 上次的事件状态（用于边沿触发） */
    mr_atomic_t gen;       /* generation 计数器（防止并发检查竞争） */
    mr_list_t list;        /* waiter 链表头 */
    mr_ptr_t check;        /* 事件检查函数 */
    mr_ptr_t param;        /* check 函数的参数 */
} mr_waitable_t;
```

**字段说明**：

- `last`：上次的事件状态，用于计算边沿触发（`event_edge = event & ~last`）
- `gen`：generation 计数器，每次 check 时递增，用于防止并发检查时的竞争
- `list`：所有等待此 waitable 的 waiter 链表
- `check`：事件检查函数指针
- `param`：传递给 check 函数的参数

#### mr_waiter_t

```c
typedef struct mr_waiter {
    mr_object_t parent;    /* 父类：对象系统 */
    mr_uint64_t event;     /* 等待的事件类型（IN|OUT|ERR|EDGE） */
    mr_list_t list;        /* 链表节点（挂在 waitable 的 list 上） */
    mr_ptr_t entry;        /* 回调函数 */
    mr_ptr_t param;        /* 回调函数参数 */
} mr_waiter_t;
```

**字段说明**：

- `event`：等待的事件类型（可以是多个事件的组合）
- `list`：链表节点，当 waiter 处于等待状态时挂在 waitable 的链表上
- `entry`：事件触发时的回调函数
- `param`：传递给回调函数的参数

#### 事件类型定义

```c
#define MR_WAIT_IN       (1ULL << 60)  /* 可读事件 */
#define MR_WAIT_OUT      (1ULL << 61)  /* 可写事件 */
#define MR_WAIT_ERR      (1ULL << 62)  /* 错误事件 */
#define MR_WAIT_EDGE     (1ULL << 63)  /* 边沿触发标志 */
```

**事件掩码**：

```c
#define MR_WAIT_EVENT_MASK (~(MR_WAIT_IN | MR_WAIT_OUT | MR_WAIT_ERR | MR_WAIT_EDGE))
```

用户自定义事件应使用 `MR_WAIT_EVENT_MASK` 之外的位（0 ~ 59 位）。

#### 回调函数类型

```c
/* waitable 的事件检查函数 */
typedef mr_uint64_t (*mr_waitable_check_t)(mr_waitable_t *waitable, void *param);

/* waiter 的回调函数 */
typedef void (*mr_waiter_entry_t)(mr_waiter_t *waiter, void *param, mr_uint64_t event);
```

### 2.2 主要接口说明

#### waitable 接口

##### mr_waitable_init()

```c
mr_err_t mr_waitable_init(mr_waitable_t *able,
                          mr_waitable_check_t *check,
                          void *param);
```

**功能**：初始化 waitable

**参数**：

- `able`：waitable 对象
- `check`：事件检查函数（返回当前的事件状态）
- `param`：传递给 check 函数的参数

**返回值**：

- `0`：成功
- 负数：错误码

##### mr_waitable_create()

```c
mr_waitable_t *mr_waitable_create(mr_waitable_check_t *check, void *param);
```

**功能**：创建动态 waitable

**返回值**：

- 非 `NULL`：waitable 指针
- `NULL`：创建失败

##### mr_waitable_complete()

```c
mr_err_t mr_waitable_complete(mr_waitable_t *able);
```

**功能**：完成 waitable，触发检查和通知

**工作流程**：

1. 调用 `check` 函数获取当前事件状态
2. 遍历所有 waiters，触发匹配的回调

**注意**：如果没有 waiter 在等待，此函数会快速返回（优化）

##### mr_waitable_raise()

```c
mr_err_t mr_waitable_raise(mr_waitable_t *able, mr_uint64_t event);
```

**功能**：手动触发 waitable 事件

**参数**：

- `event`：要触发的事件（IN/OUT/ERR）

**区别**：`complete` 会调用 check 函数获取事件，`raise` 直接使用传入的事件

##### mr_waitable_cancel_all()

```c
mr_err_t mr_waitable_cancel_all(mr_waitable_t *able);
```

**功能**：取消所有等待此 waitable 的 waiters

#### waiter 接口

##### mr_waiter_init()

```c
mr_err_t mr_waiter_init(mr_waiter_t *wait,
                        mr_waiter_entry_t *entry,
                        void *param,
                        mr_uint64_t event);
```

**功能**：初始化 waiter

**参数**：

- `wait`：waiter 对象
- `entry`：回调函数
- `param`：回调函数参数
- `event`：等待的事件类型（如 `MR_WAIT_IN`、`MR_WAIT_OUT | MR_WAIT_EDGE` 等）

**返回值**：

- `0`：成功
- 负数：错误码

##### mr_waiter_create()

```c
mr_waiter_t *mr_waiter_create(mr_waiter_entry_t *entry,
                              void *param,
                              mr_uint64_t event);
```

**功能**：创建动态 waiter

**返回值**：

- 非 `NULL`：waiter 指针
- `NULL`：创建失败

##### mr_waiter_wait()

```c
mr_err_t mr_waiter_wait(mr_waiter_t *wait, mr_waitable_t *able);
```

**功能**：将 waiter 注册到 waitable，开始等待

**工作流程**：

1. 如果 waiter 已经在等待，返回 `-MR_EBUSY`
2. 将 waiter 添加到 waitable 的等待链表
3. 如果是边沿触发，直接返回
4. 如果是电平触发，立即检查当前状态，如果条件满足则调用回调

**返回值**：

- `0`：成功
- `-MR_EBUSY`：waiter 已经在等待

##### mr_waiter_cancel()

```c
mr_err_t mr_waiter_cancel(mr_waiter_t *wait);
```

**功能**：取消 waiter 的等待

**实现原理**：从 waitable 的等待链表中移除

##### mr_waiter_event_set()

```c
mr_err_t mr_waiter_event_set(mr_waiter_t *wait, mr_uint64_t event);
```

**功能**：修改 waiter 等待的事件类型

**注意**：如果 waiter 正在等待，会先取消等待，然后修改事件类型

##### MR_WAITER_IS_WAITING()

```c
#define MR_WAITER_IS_WAITING(_waiter)
```

**功能**：检查 waiter 是否正在等待

**返回值**：

- `MR_TRUE`：正在等待
- `MR_FALSE`：未在等待

**实现原理**：检查 waiter 的 list 节点是否在链表中

### 2.3 典型应用案例

#### 案例 1：基础用法 - I/O 设备可读事件

```c
#include <mrX/kernel/mr_wait.h>

/* UART 设备 */
typedef struct {
    mr_waitable_t waitable;
    volatile uint8_t *rx_buffer;
    size_t rx_count;
} uart_device_t;

/* 检查 UART 是否可读 */
static mr_uint64_t uart_check(mr_waitable_t *waitable, void *param) {
    uart_device_t *uart = (uart_device_t *)param;

    if (uart->rx_count > 0) {
        return MR_WAIT_IN;  /* 有数据可读 */
    }

    return 0;  /* 无事件 */
}

/* waiter 回调 */
static void uart_rx_callback(mr_waiter_t *waiter, void *param, mr_uint64_t event) {
    uart_device_t *uart = (uart_device_t *)param;

    if (event & MR_WAIT_IN) {
        /* 处理接收数据 */
        process_uart_data(uart->rx_buffer, uart->rx_count);
        uart->rx_count = 0;  /* 清空缓冲区 */
    }
}

/* 使用示例 */
void uart_example(void) {
    uart_device_t uart;
    mr_waiter_t waiter;
    mr_err_t ret;

    /* 初始化 waitable */
    mr_waitable_init(&uart.waitable, uart_check, &uart);

    /* 初始化 waiter */
    mr_waiter_init(&waiter, uart_rx_callback, &uart, MR_WAIT_IN);

    /* 开始等待 */
    ret = mr_waiter_wait(&waiter, &uart.waitable);
    if (ret != 0) {
        printf("Failed to wait: %d\n", ret);
    }

    /* 当 UART 接收到数据时，触发中断处理程序 */
    void uart_irq_handler(void) {
        /* 读取数据到缓冲区 */
        uart.rx_buffer[uart.rx_count++] = read_uart_reg();
        /* ... */

        /* 完成 waitable，触发 waiters */
        mr_waitable_complete(&uart.waitable);
    }
}
```

#### 案例 2：边沿触发 vs 电平触发

```c
#include <mrX/kernel/mr_wait.h>

typedef struct {
    mr_waitable_t waitable;
    volatile int flag;
} device_t;

static mr_uint64_t device_check(mr_waitable_t *waitable, void *param) {
    device_t *dev = (device_t *)param;

    if (dev->flag) {
        return MR_WAIT_IN;
    }

    return 0;
}

static void edge_callback(mr_waiter_t *waiter, void *param, mr_uint64_t event) {
    printf("Edge triggered: event=%llx\n", event);
}

static void level_callback(mr_waiter_t *waiter, void *param, mr_uint64_t event) {
    printf("Level triggered: event=%llx\n", event);
}

void edge_vs_level_example(void) {
    device_t dev;
    mr_waiter_t edge_waiter, level_waiter;

    dev.flag = 0;
    mr_waitable_init(&dev.waitable, device_check, &dev);

    /* 边沿触发 waiter */
    mr_waiter_init(&edge_waiter, edge_callback, &dev,
                   MR_WAIT_IN | MR_WAIT_EDGE);
    mr_waiter_wait(&edge_waiter, &dev.waitable);

    /* 电平触发 waiter */
    mr_waiter_init(&level_waiter, level_callback, &dev, MR_WAIT_IN);
    mr_waiter_wait(&level_waiter, &dev.waitable);

    /* 第一次置位 flag */
    dev.flag = 1;
    mr_waitable_complete(&dev.waitable);
    /* 输出：
       Edge triggered: event=0x1000000000000000
       Level triggered: event=0x1000000000000000
    */

    /* 第二次调用 complete（flag 仍为 1） */
    mr_waitable_complete(&dev.waitable);
    /* 输出：
       Level triggered: event=0x1000000000000000
       （边沿触发不会再次触发，因为没有变化）
    */
}
```

#### 案例 3：多个 waiter 等待同一个 waitable

```c
#include <mrX/kernel/mr_wait.h>

typedef struct {
    mr_waitable_t waitable;
    int data_ready;
} shared_resource_t;

static mr_uint64_t resource_check(mr_waitable_t *waitable, void *param) {
    shared_resource_t *res = (shared_resource_t *)param;

    if (res->data_ready) {
        return MR_WAIT_IN;
    }

    return 0;
}

/* 多个等待者的回调 */
static void consumer1_callback(mr_waiter_t *waiter, void *param, mr_uint64_t event) {
    printf("Consumer 1: data ready\n");
    /* 处理数据 */
}

static void consumer2_callback(mr_waiter_t *waiter, void *param, mr_uint64_t event) {
    printf("Consumer 2: data ready\n");
    /* 处理数据 */
}

static void consumer3_callback(mr_waiter_t *waiter, void *param, mr_uint64_t event) {
    printf("Consumer 3: data ready\n");
    /* 处理数据 */
}

void multiple_waiters_example(void) {
    shared_resource_t resource;
    mr_waiter_t waiters[3];

    resource.data_ready = 0;
    mr_waitable_init(&resource.waitable, resource_check, &resource);

    /* 注册多个 waiters */
    mr_waiter_init(&waiters[0], consumer1_callback, &resource, MR_WAIT_IN);
    mr_waiter_wait(&waiters[0], &resource.waitable);

    mr_waiter_init(&waiters[1], consumer2_callback, &resource, MR_WAIT_IN);
    mr_waiter_wait(&waiters[1], &resource.waitable);

    mr_waiter_init(&waiters[2], consumer3_callback, &resource, MR_WAIT_IN);
    mr_waiter_wait(&waiters[2], &resource.waitable);

    /* 数据就绪，触发所有 waiters */
    resource.data_ready = 1;
    mr_waitable_complete(&resource.waitable);
    /* 输出：
       Consumer 1: data ready
       Consumer 2: data ready
       Consumer 3: data ready
    */
}
```

#### 案例 4：多事件类型 - IN/OUT/ERR

```c
#include <mrX/kernel/mr_wait.h>

typedef struct {
    mr_waitable_t waitable;
    int can_read;
    int can_write;
    int has_error;
} io_device_t;

static mr_uint64_t io_check(mr_waitable_t *waitable, void *param) {
    io_device_t *dev = (io_device_t *)param;
    mr_uint64_t event = 0;

    if (dev->can_read) {
        event |= MR_WAIT_IN;
    }

    if (dev->can_write) {
        event |= MR_WAIT_OUT;
    }

    if (dev->has_error) {
        event |= MR_WAIT_ERR;
    }

    return event;
}

/* IN 事件回调 */
static void on_readable(mr_waiter_t *waiter, void *param, mr_uint64_t event) {
    if (event & MR_WAIT_IN) {
        printf("Device is readable\n");
        read_data();
    }
}

/* OUT 事件回调 */
static void on_writable(mr_waiter_t *waiter, void *param, mr_uint64_t event) {
    if (event & MR_WAIT_OUT) {
        printf("Device is writable\n");
        write_data();
    }
}

/* ERR 事件回调 */
static void on_error(mr_waiter_t *waiter, void *param, mr_uint64_t event) {
    if (event & MR_WAIT_ERR) {
        printf("Device error!\n");
        handle_error();
    }
}

/* 多事件 waiter */
static void on_any_event(mr_waiter_t *waiter, void *param, mr_uint64_t event) {
    printf("Event: 0x%llx\n", event);

    if (event & MR_WAIT_IN) {
        /* 处理可读 */
    }

    if (event & MR_WAIT_OUT) {
        /* 处理可写 */
    }

    if (event & MR_WAIT_ERR) {
        /* 处理错误 */
    }
}

void multi_event_example(void) {
    io_device_t dev;
    mr_waiter_t in_waiter, out_waiter, err_waiter, any_waiter;

    dev.can_read = 0;
    dev.can_write = 0;
    dev.has_error = 0;

    mr_waitable_init(&dev.waitable, io_check, &dev);

    /* 分别注册不同事件的 waiters */
    mr_waiter_init(&in_waiter, on_readable, &dev, MR_WAIT_IN);
    mr_waiter_wait(&in_waiter, &dev.waitable);

    mr_waiter_init(&out_waiter, on_writable, &dev, MR_WAIT_OUT);
    mr_waiter_wait(&out_waiter, &dev.waitable);

    mr_waiter_init(&err_waiter, on_error, &dev, MR_WAIT_ERR);
    mr_waiter_wait(&err_waiter, &dev.waitable);

    /* 注册等待所有事件的 waiter */
    mr_waiter_init(&any_waiter, on_any_event, &dev,
                   MR_WAIT_IN | MR_WAIT_OUT | MR_WAIT_ERR);
    mr_waiter_wait(&any_waiter, &dev.waitable);

    /* 模拟状态变化 */
    dev.can_read = 1;
    mr_waitable_complete(&dev.waitable);
    /* 触发：in_waiter, any_waiter */

    dev.can_write = 1;
    mr_waitable_complete(&dev.waitable);
    /* 触发：out_waiter, any_waiter */

    dev.has_error = 1;
    mr_waitable_complete(&dev.waitable);
    /* 触发：err_waiter, any_waiter */
}
```

#### 案例 5：动态修改 waiter 事件类型

```c
#include <mrX/kernel/mr_wait.h>

typedef struct {
    mr_waitable_t waitable;
    int state;
} device_t;

static mr_uint64_t device_check(mr_waitable_t *waitable, void *param) {
    device_t *dev = (device_t *)param;

    if (dev->state == 1) {
        return MR_WAIT_IN;
    } else if (dev->state == 2) {
        return MR_WAIT_OUT;
    }

    return 0;
}

static void device_callback(mr_waiter_t *waiter, void *param, mr_uint64_t event) {
    device_t *dev = (device_t *)param;

    if (event & MR_WAIT_IN) {
        printf("State 1: IN event\n");
        /* 处理完后等待 OUT 事件 */
        mr_waiter_event_set(waiter, MR_WAIT_OUT);
        mr_waiter_wait(waiter, &dev->waitable);
    } else if (event & MR_WAIT_OUT) {
        printf("State 2: OUT event\n");
        /* 处理完后等待 IN 事件 */
        mr_waiter_event_set(waiter, MR_WAIT_IN);
        mr_waiter_wait(waiter, &dev->waitable);
    }
}

void dynamic_event_change_example(void) {
    device_t dev;
    mr_waiter_t waiter;

    dev.state = 0;
    mr_waitable_init(&dev.waitable, device_check, &dev);

    /* 初始等待 IN 事件 */
    mr_waiter_init(&waiter, device_callback, &dev, MR_WAIT_IN);
    mr_waiter_wait(&waiter, &dev.waitable);

    /* 状态变化 1 */
    dev.state = 1;
    mr_waitable_complete(&dev.waitable);
    /* 输出：State 1: IN event
       waiter 自动切换到等待 OUT 事件
    */

    /* 状态变化 2 */
    dev.state = 2;
    mr_waitable_complete(&dev.waitable);
    /* 输出：State 2: OUT event
       waiter 自动切换到等待 IN 事件
    */
}
```

---

## 3. 设计思路

### 3.1 设计目标

wait 模块的设计目标：

1. **灵活的事件模型**：支持多种事件类型和触发模式
2. **高效的通知机制**：避免不必要的检查和回调
3. **线程安全**：支持多线程和中断上下文
4. **内存安全**：防止并发访问导致的内存问题
5. **可扩展性**：用户可以自定义事件类型

### 3.2 generation 机制：防止并发检查竞争

**问题场景**：

```c
/* check 函数中可能触发新的 complete */
static mr_uint64_t check(mr_waitable_t *w, void *p) {
    if (some_condition) {
        /* 这里会触发新的 complete，导致嵌套 */
        mr_waitable_complete(w);
    }
    return current_event;
}
```

**解决方案**：generation 计数器

```c
static mr_uint64_t waitable_check(mr_waitable_t *able) {
    mr_waitable_check_t *check;
    mr_uint64_t event;
    mr_atomic_t gen;

    /* 获取当前 generation 并递增 */
    gen = mr_atomic_fetch_add(&able->gen, 1);

    /* 调用 check 函数 */
    event = check(able, able->param);

    /* 检查 generation 是否被修改 */
    if (mr_atomic_load(&able->gen) != (gen + 1)) {
        /* 有新的 check 发生，忽略这个旧的结果 */
        return 0;
    }

    return event;
}
```

**工作原理**：

1. 每个 check 操作开始时获取当前 gen 并递增
2. 如果 check 期间有新的 check，gen 会再次递增
3. 第一个 check 检测到 gen 变化，知道自己不是最新的，返回 0
4. 只有最新的 check 结果才有效

**为什么需要这个机制？**

- 防止嵌套调用时使用过时的检查结果
- 保证总是使用最新的事件状态
- 避免重复触发 waiter

### 3.3 oplist 机制：保护链表内存安全

**问题场景**：

```c
/* 遍历 waitable 的 waiter 链表 */
for (waiter in waitable.list) {
    /* 调用 waiter 回调 */
    waiter->entry(waiter, param, event);

    /* 回调中可能删除 waiter */
    /* 此时链表被修改，继续遍历会访问非法内存 */
}
```

**解决方案**：临时链表（oplist）

```c
static void waitable_handle(mr_waitable_t *able, mr_uint64_t event) {
    mr_list_t oplist;
    mr_waiter_t *waiter;

    /* 初始化临时链表 */
    mr_list_init(&oplist);

    lock();

    /* 将等待链表替换到临时链表 */
    mr_list_replace(&oplist, &able->list);

    /* 遍历临时链表 */
    while (!mr_list_is_empty(&oplist)) {
        waiter = get_first(&oplist);

        /* 移回原链表（此时在原链表中） */
        mr_list_move(&waiter->list, &able->list);

        /* 检查是否触发 */
        if (!(waiter->event & event)) {
            continue;
        }

        unlock();

        /* 调用回调 */
        waiter->entry(waiter, param, event);

        lock();

        /* 如果 waiter 在回调中被删除，它已经不在链表中 */
        /* 继续处理下一个 */
    }

    unlock();
}
```

**工作原理**：

1. 将 waitable 的 waiter 链表复制到 oplist
2. 遍历 oplist，将每个 waiter 移回原链表
3. 调用 waiter 回调
4. 如果回调中删除了 waiter，它已从原链表移除
5. 继续处理 oplist 中的下一个 waiter

**为什么需要先移回原链表？**

- 保证 waiter 始终在某个链表中（oplist 或 waitable.list）
- 防止回调中删除 waiter 时内存访问越界
- 类似于 timer 模块的设计

### 3.4 边沿触发 vs 电平触发

**电平触发（Level-Triggered）**：

```c
/* 只要条件满足就触发 */
event_level = event;

if (waiter->event & event_level) {
    /* 触发回调 */
}
```

**特点**：

- 只要事件状态匹配，每次 `complete` 都会触发
- 适合状态持续的场景（如缓冲区有数据）
- 类似于 POSIX poll 的默认行为

**边沿触发（Edge-Triggered）**：

```c
/* 只有状态变化时触发 */
event_edge = event & ~able->last;

if (waiter->event & MR_WAIT_EDGE) {
    /* 使用边沿事件 */
    if (waiter->event & event_edge) {
        /* 触发回调 */
    }
}

/* 更新 last */
able->last = event;
```

**特点**：

- 只有状态从"无"变"有"时才触发
- 关注变化而非状态
- 类似于 Linux epoll 的 EPOLLET

**示例对比**：

```c
/* 场景：flag 从 0 变为 1，然后保持 1 */

dev.flag = 0;
/* 注册电平触发 waiter */
mr_waiter_wait(&level_waiter, &dev.waitable);  /* event=MR_WAIT_IN */

/* 注册边沿触发 waiter */
mr_waiter_wait(&edge_waiter, &dev.waitable);   /* event=MR_WAIT_IN|MR_WAIT_EDGE */

/* flag 变为 1 */
dev.flag = 1;
mr_waitable_complete(&dev.waitable);
/* 电平触发：触发
   边沿触发：触发（0 -> 1 的变化）
*/

/* flag 保持 1，再次调用 complete */
mr_waitable_complete(&dev.waitable);
/* 电平触发：再次触发（条件仍满足）
   边沿触发：不触发（没有变化，一直是 1）
*/
```

### 3.5 waiter_wait 中的立即检查

**问题**：电平触发模式下，如果注册 waiter 时事件已经满足，不应该遗漏。

**解决方案**：注册后立即检查

```c
mr_err_t mr_waiter_wait(mr_waiter_t *wait, mr_waitable_t *able) {
    lock();

    /* 添加到等待链表 */
    mr_list_add(&able->list, &wait->list);

    /* 边沿触发直接返回（等待状态变化） */
    if (wait->event & MR_WAIT_EDGE) {
        unlock();
        return 0;
    }

    unlock();

    /* 电平触发：立即检查当前状态 */
    event = waitable_check(able);

    if ((event & WAIT_TYPE_MASK) == 0) {
        return 0;  /* 无事件，继续等待 */
    }

    if (!(wait->event & event)) {
        return 0;  /* 事件不匹配，继续等待 */
    }

    /* 条件满足，立即调用回调 */
    wait->entry(wait, wait->param, event);
    return 0;
}
```

**为什么边沿触发不需要立即检查？**

- 边沿触发关心的是"变化"
- 如果已经满足，说明已经"变化"过了
- 只需要等待下一次变化

**为什么电平触发需要立即检查？**

- 电平触发关心的是"状态"
- 如果已经满足，应该立即通知
- 避免遗漏已就绪的事件

### 3.6 事件类型设计

**高位保留位**：

```c
MR_WAIT_IN   = (1ULL << 60)  /* 第 60 位 */
MR_WAIT_OUT  = (1ULL << 61)  /* 第 61 位 */
MR_WAIT_ERR  = (1ULL << 62)  /* 第 62 位 */
MR_WAIT_EDGE = (1ULL << 63)  /* 第 63 位 */
```

**用户自定义事件**：

```c
/* 用户可以使用 0 ~ 59 位自定义事件 */
#define MY_EVENT_READY  (1ULL << 0)
#define MY_EVENT_DONE   (1ULL << 1)

static mr_uint64_t my_check(mr_waitable_t *w, void *p) {
    if (is_ready()) {
        return MR_WAIT_IN | MY_EVENT_READY;
    }

    if (is_done()) {
        return MR_WAIT_OUT | MY_EVENT_DONE;
    }

    return 0;
}
```

**事件掩码**：

```c
#define MR_WAIT_EVENT_MASK (~(MR_WAIT_IN | MR_WAIT_OUT | MR_WAIT_ERR | MR_WAIT_EDGE))
```

用于提取用户自定义事件：

```c
static void callback(mr_waiter_t *waiter, void *param, mr_uint64_t event) {
    /* 检查标准事件 */
    if (event & MR_WAIT_IN) {
        /* 处理可读 */
    }

    /* 检查自定义事件 */
    mr_uint64_t custom_event = event & MR_WAIT_EVENT_MASK;
    if (custom_event & MY_EVENT_READY) {
        /* 处理自定义事件 */
    }
}
```

### 3.7 与其他机制的对比

#### vs 观察者模式

| 特性   | wait 模块        | 经典观察者模式 |
|------|----------------|---------|
| 编程模型 | 回调驱动           | 接口驱动    |
| 内存分配 | 静态             | 通常动态    |
| 事件类型 | 多种（IN/OUT/ERR） | 通常单一    |
| 触发模式 | 电平+边沿          | 仅边沿     |
| 适用场景 | 嵌入式系统          | 通用应用    |

wait 模块是观察者模式的一种实现，但针对嵌入式场景进行了优化。

#### vs 信号量/条件变量

| 特性   | wait 模块  | 信号量/条件变量 |
|------|----------|----------|
| 用途   | 事件通知     | 资源同步/互斥  |
| 事件类型 | 多种       | 单一       |
| 触发模式 | 电平+边沿    | 边沿       |
| 阻塞特性 | 非阻塞（回调）  | 可阻塞      |
| 适用场景 | I/O、设备状态 | 线程同步     |

#### vs hook 模块

| 特性   | wait 模块 | hook 模块   |
|------|---------|-----------|
| 触发方式 | 状态检查    | 主动调用      |
| 事件类型 | 带类型的位标志 | 无类型       |
| 触发模式 | 电平+边沿   | 仅边沿（每次调用） |
| 使用场景 | 状态变化通知  | 回调链       |

---

## 4. 参考资料

### 4.1 设计参考

- **观察者模式**：软件设计模式中的观察者模式（多个观察者监听同一个主题）
- **Linux kernel notification chain**：Linux 内核的通知链机制
- **信号槽机制**：Qt 框架的信号与槽机制

### 4.2 延伸阅读

- **观察者模式**：设计模式中的观察者模式详解
- **边沿触发 vs 电平触发**：两种触发模式的深入理解
- **事件驱动架构**：基于事件的软件架构模式
- **发布-订阅模式**：一种消息传递模式，与观察者模式类似

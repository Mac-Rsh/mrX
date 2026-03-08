# workqueue - 工作队列

## 1. 模块概述

### 1.1 功能介绍

workqueue 模块实现了一个异步任务执行机制。它允许将工作（work）提交到队列中延迟执行，类似于 Linux 内核的 workqueue 或线程池。

工作队列的核心功能：

- **异步执行**：将工作提交到队列，稍后执行
- **延迟执行**：支持延迟指定 tick 数后执行
- **资源优化**：多个延迟工作共享一个 timer，降低系统开销
- **hook 机制**：支持 wakeup/suspend 回调，可与操作系统/信号量集成
- **多队列支持**：可以创建多个 workqueue，实现优先级或 CPU 隔离

### 1.2 主要特性

- **timer 集成**：每个 workqueue 内置一个 timer，高效管理延迟工作
- **oplist 保护**：使用临时链表保护内存安全
- **work 绑定**：work 可以绑定到 workqueue，支持重复调度
- **depth 机制**：保证 hook 的 wakeup/suspend 正确调用
- **线程安全**：使用自旋锁保护，支持多核环境
- **对象集成**：继承自 mr_object_t，支持引用计数
- **灵活调度**：支持立即执行和延迟执行两种模式

### 1.3 适用场景

workqueue 模块适用于以下场景：

- **中断延迟处理**：中断上下文中将耗时工作推迟到进程上下文
- **异步任务**：不需要立即执行的耗时操作
- **批量处理**：将多个小任务合并处理，提高效率
- **资源清理**：延迟释放资源，避免在不恰当的时机释放
- **多优先级任务**：不同优先级的任务分配到不同的 workqueue

**不适用场景**：

- **硬实时任务**：workqueue 的执行时机不确定
- **极短延迟**：延迟时间小于 workqueue 执行精度时会导致延迟不准

### 1.4 使用注意事项

⚠️ **重要警告**：

1. **work 绑定**：work 可以绑定到 workqueue 支持重复调度，也可以直接使用 `mr_workqueue_work` 等接口指定 workqueue（会自动绑定）

2. **执行时机不确定**：work 的执行时机取决于 `mr_workqueue_execute` 的调用

3. **不能重复调度**：正在运行的 work 不能再次调度（返回 `-MR_EBUSY`）

4. **延迟上限**：延迟 tick 必须小于 `MR_TICK_MAX / 2`

5. **删除前清理**：删除 workqueue 前会自动取消所有 work，但建议手动清理

---

## 2. 使用指南

### 2.1 核心数据结构

#### mr_workqueue_t

```c
typedef struct mr_workqueue {
    mr_object_t parent;      /* 父类：对象系统 */
    mr_atomic_t depth;       /* 深度计数器（用于 hook 机制） */
    mr_atomic_t dying;       /* 删除标志 */
    mr_timer_t timer;        /* 内置 timer：处理延迟工作 */
    mr_list_t tlist;         /* 延迟工作链表（按超时时间排序） */
    mr_list_t list;          /* 待执行工作链表 */
    const mr_workqueue_hook_t *hook;  /* hook 回调 */
} mr_workqueue_t;
```

**字段说明**：

- `depth`：深度计数器
    - 初始值为 `0`（空闲状态）
    - 有 work 加入时递增（触发 wakeup）
    - 执行完 work 后设为 `1`（触发 suspend）
- `timer`：周期定时器，用于处理延迟工作
- `tlist`：延迟工作链表，按超时时间排序（最近的在头部）
- `list`：待执行工作链表（立即执行或已到期的延迟工作）

#### mr_work_t

```c
typedef struct mr_work {
    mr_object_t parent;      /* 父类：对象系统 */
    mr_list_t list;          /* 链表节点（挂在 workqueue 上） */
    mr_ptr_t queue;          /* 绑定的 workqueue */
    mr_tick_t tick;          /* 超时时刻（延迟工作） */
    mr_ptr_t entry;          /* 工作函数 */
    mr_ptr_t param;          /* 工作函数参数 */
} mr_work_t;
```

**字段说明**：

- `list`：链表节点，当 work 被调度时挂在 workqueue 的 list 或 tlist 上
- `queue`：绑定的 workqueue，用于重复调度
- `tick`：超时时刻的绝对时钟数（延迟工作使用）
- `entry`：工作函数指针
- `param`：工作函数参数

#### mr_workqueue_hook_t

```c
typedef struct mr_workqueue_hook {
    void (*suspend)(struct mr_workqueue *queue, void *param);
    void (*wakeup)(struct mr_workqueue *queue, void *param);
    void *param;
} mr_workqueue_hook_t;
```

**hook 回调**：

- `suspend`：当 workqueue 变为空闲时调用（depth 从 1 变为 0）
- `wakeup`：当有 work 加入时调用（depth 从 0 变为 1）
- `param`：传递给 hook 函数的参数

**典型应用**：使用信号量（初始值为 0）作为 hook

#### 工作函数类型

```c
typedef void (*mr_work_entry_t)(struct mr_work *work, void *param);
```

工作函数类型：

- `work`：work 对象指针
- `param`：工作函数参数

### 2.2 主要接口说明

#### workqueue 接口

##### mr_workqueue_init()

```c
mr_err_t mr_workqueue_init(mr_workqueue_t *queue);
```

**功能**：初始化 workqueue

**参数**：

- `queue`：workqueue 对象

**返回值**：

- `0`：成功
- 负数：错误码

**初始化内容**：

- 初始化内置 timer
- 初始化链表和原子变量
- 初始化对象系统

##### mr_workqueue_create()

```c
mr_workqueue_t *mr_workqueue_create(void);
```

**功能**：创建动态 workqueue

**返回值**：

- 非 `NULL`：workqueue 指针
- `NULL`：创建失败

##### mr_workqueue_del()

```c
mr_err_t mr_workqueue_del(mr_workqueue_t *queue);
```

**功能**：删除 workqueue

**参数**：

- `queue`：workqueue 对象

**实现原理**：

1. 设置 `dying` 标志，阻止新的 work 加入
2. 删除内置 timer
3. 取消所有 work（从 list 和 tlist 移除）

##### mr_workqueue_execute()

```c
mr_err_t mr_workqueue_execute(mr_workqueue_t *queue);
```

**功能**：执行 workqueue 中的所有工作

**工作流程**：

1. 如果 list 为空且无 suspend hook，快速返回
2. 使用 oplist 保护链表
3. 遍历所有 work，调用其工作函数
4. 执行完后触发 suspend（如果需要）

**注意**：此函数需要在进程上下文中定期调用

##### mr_workqueue_work()

```c
mr_err_t mr_workqueue_work(mr_workqueue_t *queue, mr_work_t *work);
```

**功能**：将 work 添加到 workqueue 立即执行

**参数**：

- `queue`：workqueue 对象
- `work`：work 对象

**返回值**：

- `0`：成功
- `-MR_ENOENT`：workqueue 正在删除（dying 标志）
- `-MR_EBUSY`：work 已经在运行

**工作流程**：

1. 将 work 添加到 list
2. 触发 wakeup（如果需要）

##### mr_workqueue_delayed_work()

```c
mr_err_t mr_workqueue_delayed_work(mr_workqueue_t *queue,
                                   mr_work_t *work,
                                   mr_tick_t tick);
```

**功能**：将 work 延迟添加到 workqueue

**参数**：

- `queue`：workqueue 对象
- `work`：work 对象
- `tick`：延迟 tick 数

**返回值**：

- `0`：成功
- `-MR_ENOENT`：workqueue 正在删除
- `-MR_EBUSY`：work 已经在运行

**工作流程**：

1. 计算超时时刻：`work->tick = 当前时钟 + tick`
2. 将 work 插入 tlist（按超时时间排序）
3. 如果是第一个延迟 work，设置并启动 timer
4. trigger wakeup（如果需要）

#### work 接口

##### mr_work_init()

```c
mr_err_t mr_work_init(mr_work_t *work, mr_work_entry_t *entry, void *param);
```

**功能**：初始化 work

**参数**：

- `work`：work 对象
- `entry`：工作函数
- `param`：工作函数参数

**返回值**：

- `0`：成功
- 负数：错误码

##### mr_work_create()

```c
mr_work_t *mr_work_create(mr_work_entry_t *entry, void *param);
```

**功能**：创建动态 work

**返回值**：

- 非 `NULL`：work 指针
- `NULL`：创建失败

##### mr_work_bind()

```c
mr_err_t mr_work_bind(mr_work_t *work, mr_workqueue_t *queue);
```

**功能**：将 work 绑定到 workqueue

**参数**：

- `work`：work 对象
- `queue`：workqueue 对象

**用途**：绑定后可以重复调度，无需每次指定 workqueue

##### mr_work_schedule()

```c
mr_err_t mr_work_schedule(mr_work_t *work);
```

**功能**：调度 work 到绑定的 workqueue 立即执行

**返回值**：

- `0`：成功
- `-MR_ENOENT`：work 未绑定或 workqueue 已删除
- `-MR_EBUSY`：work 已经在运行

##### mr_work_delayed_schedule()

```c
mr_err_t mr_work_delayed_schedule(mr_work_t *work, mr_tick_t tick);
```

**功能**：调度 work 延迟执行

**参数**：

- `work`：work 对象
- `tick`：延迟 tick 数

**返回值**：

- `0`：成功
- `-MR_ENOENT`：work 未绑定或 workqueue 已删除
- `-MR_EBUSY`：work 已经在运行

##### mr_work_cancel()

```c
mr_err_t mr_work_cancel(mr_work_t *work);
```

**功能**：取消 work（从 workqueue 移除）

##### MR_WORK_IS_RUNNING()

```c
#define MR_WORK_IS_RUNNING(_work)
```

**功能**：检查 work 是否正在运行（已调度未执行）

**返回值**：

- `MR_TRUE`：正在运行
- `MR_FALSE`：未运行

### 2.3 典型应用案例

#### 案例 1：基础用法 - 立即执行工作

```c
#include <mrX/kernel/mr_workqueue.h>

/* 工作函数 */
static void my_work_entry(mr_work_t *work, void *param) {
    printf("Work executed, param: %p\n", param);

    /* 执行任务 */
    do_something();
}

void basic_usage_example(void) {
    mr_workqueue_t queue;
    mr_work_t work;
    mr_err_t ret;

    /* 初始化 workqueue */
    mr_workqueue_init(&queue);

    /* 初始化 work */
    mr_work_init(&work, my_work_entry, (void *)0x1234);

    /* 绑定 work 到 workqueue */
    mr_work_bind(&work, &queue);

    /* 调度 work 立即执行 */
    mr_work_schedule(&work);

    /* 在某个时刻执行 workqueue 中的 work */
    /* 通常在主循环或线程中 */
    mr_workqueue_execute(&queue);

    /* 清理 */
    mr_work_del(&work);
    mr_workqueue_del(&queue);
}
```

#### 案例 2：延迟执行 - 中断延迟处理

```c
#include <mrX/kernel/mr_workqueue.h>

static mr_workqueue_t g_defer_queue;

/* 延迟处理函数（进程上下文） */
static void irq_defer_work(mr_work_t *work, void *param) {
    device_t *dev = (device_t *)param;

    /* 耗时操作：处理中断数据 */
    process_interrupt_data(dev);

    /* 可能的耗时操作 */
    send_response(dev);
    update_statistics(dev);
}

/* 中断处理程序（中断上下文） */
void irq_handler(void) {
    device_t *dev = get_device();

    /* 快速操作：读取数据到缓冲区 */
    read_data_to_buffer(dev);

    /* 清除硬件中断标志 */
    clear_hardware_interrupt(dev);

    /* 调度延迟工作 */
    mr_work_delayed_schedule(&dev->defer_work, 0);  /* 立即加入队列 */
}

/* 初始化 */
void device_init(device_t *dev) {
    /* 初始化延迟 workqueue */
    mr_workqueue_init(&g_defer_queue);

    /* 初始化并绑定延迟 work */
    mr_work_init(&dev->defer_work, irq_defer_work, dev);
    mr_work_bind(&dev->defer_work, &g_defer_queue);
}

/* 主循环 */
void main_loop(void) {
    while (1) {
        /* 执行 workqueue 中的 work */
        mr_workqueue_execute(&g_defer_queue);

        /* 其他任务 */
        /* ... */
    }
}
```

#### 案例 3：配合 RTOS 实现多优先级 workqueue

```c
#include <mrX/kernel/mr_workqueue.h>
#include <rtos.h>  /* 假设使用 FreeRTOS 或其他 RTOS */

/* 定义三个优先级的 workqueue */
static mr_workqueue_t high_priority_queue;
static mr_workqueue_t normal_priority_queue;
static mr_workqueue_t low_priority_queue;

/* 高优先级工作 */
static void high_priority_work(mr_work_t *work, void *param) {
    /* 处理紧急任务 */
    handle_urgent_task();
}

/* 普通优先级工作 */
static void normal_priority_work(mr_work_t *work, void *param) {
    /* 处理普通任务 */
    handle_normal_task();
}

/* 低优先级工作 */
static void low_priority_work(mr_work_t *work, void *param) {
    /* 处理后台任务 */
    handle_background_task();
}

/* RTOS 任务：高优先级线程 */
void high_priority_thread(void *param) {
    while (1) {
        /* 等待信号量或条件变量 */
        mr_sem_wait(&high_sem);

        /* 执行高优先级 workqueue */
        mr_workqueue_execute(&high_priority_queue);
    }
}

/* RTOS 任务：普通优先级线程 */
void normal_priority_thread(void *param) {
    while (1) {
        /* 等待信号量或条件变量 */
        mr_sem_wait(&normal_sem);

        /* 执行普通优先级 workqueue */
        mr_workqueue_execute(&normal_priority_queue);
    }
}

/* RTOS 任务：低优先级线程 */
void low_priority_thread(void *param) {
    while (1) {
        /* 等待信号量或条件变量 */
        mr_sem_wait(&low_sem);

        /* 执行低优先级 workqueue */
        mr_workqueue_execute(&low_priority_queue);
    }
}

/* hook：wakeup 时释放信号量 */
static void workqueue_wakeup(mr_workqueue_t *queue, void *param) {
    mr_sem_t *sem = (mr_sem_t *)param;
    mr_sem_post(sem);  /* 唤醒等待的线程 */
}

/* hook：suspend 时无需操作（信号量等待会自动阻塞） */
static void workqueue_suspend(mr_workqueue_t *queue, void *param) {
    /* 无需操作 */
}

void multi_priority_with_rtos_example(void) {
    mr_workqueue_hook_t hook;

    /* 初始化三个 workqueue */
    mr_workqueue_init(&high_priority_queue);
    mr_workqueue_init(&normal_priority_queue);
    mr_workqueue_init(&low_priority_queue);

    /* 初始化信号量 */
    mr_sem_init(&high_sem, 0);
    mr_sem_init(&normal_sem, 0);
    mr_sem_init(&low_sem, 0);

    /* 设置 high_priority_queue 的 hook */
    hook.suspend = workqueue_suspend;
    hook.wakeup = workqueue_wakeup;
    hook.param = &high_sem;
    mr_workqueue_hook_set(&high_priority_queue, &hook);

    /* 设置 normal_priority_queue 的 hook */
    hook.param = &normal_sem;
    mr_workqueue_hook_set(&normal_priority_queue, &hook);

    /* 设置 low_priority_queue 的 hook */
    hook.param = &low_sem;
    mr_workqueue_hook_set(&low_priority_queue, &hook);

    /* 创建 RTOS 任务（不同优先级） */
    rtos_thread_create(high_priority_thread, NULL, PRIORITY_HIGH);
    rtos_thread_create(normal_priority_thread, NULL, PRIORITY_NORMAL);
    rtos_thread_create(low_priority_thread, NULL, PRIORITY_LOW);

    /* 创建并绑定不同优先级的 work */
    mr_work_t *high_work1 = mr_work_create(high_priority_work, NULL);
    mr_work_bind(high_work1, &high_priority_queue);

    mr_work_t *normal_work1 = mr_work_create(normal_priority_work, NULL);
    mr_work_bind(normal_work1, &normal_priority_queue);

    mr_work_t *low_work1 = mr_work_create(low_priority_work, NULL);
    mr_work_bind(low_work1, &low_priority_queue);

    /* 调度 work（会自动唤醒对应的线程） */
    mr_work_schedule(high_work1);   /* 唤醒高优先级线程 */
    mr_work_schedule(normal_work1); /* 唤醒普通优先级线程 */
    mr_work_schedule(low_work1);    /* 唤醒低优先级线程 */
}
```

#### 案例 4：真正的延迟执行 - 定时任务

```c
#include <mrX/kernel/mr_workqueue.h>

static mr_workqueue_t g_timer_queue;

/* 定时任务 */
static void timeout_task(mr_work_t *work, void *param) {
    printf("Timeout task executed\n");

    /* 执行定时任务 */
    /* ... */
}

void delayed_schedule_example(void) {
    mr_work_t work;
    mr_err_t ret;

    mr_workqueue_init(&g_timer_queue);
    mr_work_init(&work, timeout_task, NULL);
    mr_work_bind(&work, &g_timer_queue);

    /* 延迟 1000 tick 后执行 */
    mr_work_delayed_schedule(&work, 1000);

    /* 主循环 */
    while (1) {
        /* 执行 workqueue */
        mr_workqueue_execute(&g_timer_queue);

        /* 其他任务或睡眠 */
        /* ... */
    }
}
```

#### 案例 5：与信号量集成 - hook 机制

```c
#include <mrX/kernel/mr_workqueue.h>
#include <mrX/kernel/mr_semaphore.h>  /* 假设有信号量模块 */

static mr_workqueue_t g_workqueue;
static mr_semaphore_t g_sem;

/* suspend：workqueue 变为空闲时调用 */
static void workqueue_suspend(mr_workqueue_t *queue, void *param) {
    /* 尝试获取信号量（会阻塞或返回失败） */
    mr_semaphore_wait(&g_sem);
}

/* wakeup：有 work 加入时调用 */
static void workqueue_wakeup(mr_workqueue_t *queue, void *param) {
    /* 释放信号量，唤醒等待的线程 */
    mr_semaphore_post(&g_sem);
}

/* hook 定义 */
static const mr_workqueue_hook_t g_hook = {
    .suspend = workqueue_suspend,
    .wakeup = workqueue_wakeup,
    .param = NULL,
};

void init_hook_workqueue(void) {
    /* 初始化信号量（初始值为 0） */
    mr_semaphore_init(&g_sem, 0);

    /* 初始化 workqueue */
    mr_workqueue_init(&g_workqueue);

    /* 设置 hook */
    mr_workqueue_hook_set(&g_workqueue, &g_hook);
}

/* 工作线程 */
void work_thread(void) {
    while (1) {
        /* 等待信号量（有 work 时被唤醒） */
        mr_semaphore_wait(&g_sem);

        /* 执行 workqueue */
        mr_workqueue_execute(&g_workqueue);
    }
}
```

#### 案例 6：低功耗管理 - hook 机制的应用

##### 方案一：简单的低功耗（单 workqueue + tickless）

```c
#include <mrX/kernel/mr_workqueue.h>
#include <mrX/kernel/mr_clock.h>

static mr_workqueue_t g_workqueue;
static volatile int g_need_wakeup = 0;

/* suspend：workqueue 变为空闲时，配置 MCU 低功耗模式 */
static void lowpower_suspend(mr_workqueue_t *queue, void *param) {
    /* 获取 tickless 时间 */
    mr_tick_t tickless = mr_clock_tickless();

    if (tickless > 0) {
        /* 配置 MCU 在指定时间后唤醒 */
        configure_low_power_mode(tickless);
    } else {
        /* 无待处理任务，配置外部触发唤醒 */
        configure_low_power_mode_external_wakeup();
    }
}

/* wakeup：有 work 加入时，唤醒 MCU */
static void lowpower_wakeup(mr_workqueue_t *queue, void *param) {
    /* 唤醒 MCU */
    wakeup_from_low_power();
}

static const mr_workqueue_hook_t g_lowpower_hook = {
    .suspend = lowpower_suspend,
    .wakeup = lowpower_wakeup,
    .param = NULL,
};

void simple_lowpower_init(void) {
    /* 初始化 workqueue */
    mr_workqueue_init(&g_workqueue);

    /* 设置低功耗 hook */
    mr_workqueue_hook_set(&g_workqueue, &g_lowpower_hook);
}

/* 主循环 */
void main_loop(void) {
    while (1) {
        /* 执行 workqueue */
        mr_workqueue_execute(&g_workqueue);

        /* 如果没有 work，suspend hook 会配置低功耗模式 */
        /* 下一次 work 调度时，wakeup hook 会唤醒 MCU */
    }
}
```

##### 方案二：复杂的低功耗（多 workqueue + 能耗调节）

```c
#include <mrX/kernel/mr_workqueue.h>

/* 定义不同能耗级别的 workqueue */
static mr_workqueue_t g_critical_queue;  /* 关键任务：高功耗 */
static mr_workqueue_t g_normal_queue;    /* 普通任务：中等功耗 */
static mr_workqueue_t g_background_queue; /* 后台任务：低功耗 */

static volatile enum {
    POWER_HIGH,    /* 关键任务运行中 */
    POWER_NORMAL,  /* 普通任务运行中 */
    POWER_LOW,     /* 后台任务运行中或空闲 */
} g_power_state = POWER_NORMAL;

/* 关键任务 suspend：任务完成，可能降低功耗 */
static void critical_suspend(mr_workqueue_t *queue, void *param) {
    if (mr_list_is_empty(&g_normal_queue.list) &&
        mr_list_is_empty(&g_background_queue.list)) {
        /* 其他 workqueue 也空闲，进入低功耗 */
        g_power_state = POWER_LOW;
        configure_power_mode(POWER_LOW);
    } else {
        /* 还有普通或后台任务，降低到中等功耗 */
        g_power_state = POWER_NORMAL;
        configure_power_mode(POWER_NORMAL);
    }
}

/* 关键任务 wakeup：有新任务，提高功耗 */
static void critical_wakeup(mr_workqueue_t *queue, void *param) {
    g_power_state = POWER_HIGH;
    configure_power_mode(POWER_HIGH);
}

/* 普通任务 suspend */
static void normal_suspend(mr_workqueue_t *queue, void *param) {
    if (mr_list_is_empty(&g_background_queue.list)) {
        /* 后台任务空闲，降低功耗 */
        g_power_state = POWER_LOW;
        configure_power_mode(POWER_LOW);
    }
}

/* 普通任务 wakeup */
static void normal_wakeup(mr_workqueue_t *queue, void *param) {
    if (!mr_list_is_empty(&g_critical_queue.list)) {
        /* 关键任务还在，保持高功耗 */
        return;
    }

    g_power_state = POWER_NORMAL;
    configure_power_mode(POWER_NORMAL);
}

/* 后台任务 suspend */
static void background_suspend(mr_workqueue_t *queue, void *param) {
    /* 所有 workqueue 空闲 */
    g_power_state = POWER_LOW;
    configure_power_mode(POWER_LOW);
}

/* 后台任务 wakeup */
static void background_wakeup(mr_workqueue_t *queue, void *param) {
    if (!mr_list_is_empty(&g_critical_queue.list)) {
        /* 关键任务还在，保持高功耗 */
        return;
    }

    if (!mr_list_is_empty(&g_normal_queue.list)) {
        /* 普通任务还在，保持中等功耗 */
        g_power_state = POWER_NORMAL;
        return;
    }

    /* 只有后台任务，使用低功耗 */
    g_power_state = POWER_LOW;
    configure_power_mode(POWER_LOW);
}

/* hook 定义 */
static const mr_workqueue_hook_t g_critical_hook = {
    .suspend = critical_suspend,
    .wakeup = critical_wakeup,
    .param = NULL,
};

static const mr_workqueue_hook_t g_normal_hook = {
    .suspend = normal_suspend,
    .wakeup = normal_wakeup,
    .param = NULL,
};

static const mr_workqueue_hook_t g_background_hook = {
    .suspend = background_suspend,
    .wakeup = background_wakeup,
    .param = NULL,
};

void complex_lowpower_init(void) {
    /* 初始化三个 workqueue */
    mr_workqueue_init(&g_critical_queue);
    mr_workqueue_init(&g_normal_queue);
    mr_workqueue_init(&g_background_queue);

    /* 设置各自的 hook */
    mr_workqueue_hook_set(&g_critical_queue, &g_critical_hook);
    mr_workqueue_hook_set(&g_normal_queue, &g_normal_hook);
    mr_workqueue_hook_set(&g_background_queue, &g_background_hook);
}

/* 不同能耗级别的 work */
mr_work_t *critical_work = mr_work_create(critical_task, NULL);
mr_work_bind(critical_work, &g_critical_queue);

mr_work_t *normal_work = mr_work_create(normal_task, NULL);
mr_work_bind(normal_work, &g_normal_queue);

mr_work_t *background_work = mr_work_create(background_task, NULL);
mr_work_bind(background_work, &g_background_queue);

/* 调度 work 会自动调节功耗 */
mr_work_schedule(critical_work);  /* 切换到高功耗 */
mr_work_schedule(normal_work);    /* 切换到中等功耗 */
mr_work_schedule(background_work); /* 切换到低功耗 */
```

#### 案例 7：正确使用示例与常见陷阱

```c
#include <mrX/kernel/mr_workqueue.h>

/* ✓ 正确：使用绑定方式重复调度 */
static void correct_usage_1(void) {
    mr_workqueue_t queue;
    mr_work_t work;

    mr_workqueue_init(&queue);
    mr_work_init(&work, work_entry, NULL);
    mr_work_bind(&work, &queue);  /* 绑定 */

    /* 可以重复调度，无需指定 workqueue */
    mr_work_schedule(&work);
    mr_work_schedule(&work);
}

/* ✓ 正确：使用直接指定 workqueue 的方式 */
static void correct_usage_2(void) {
    mr_workqueue_t queue;
    mr_work_t work;

    mr_workqueue_init(&queue);
    mr_work_init(&work, work_entry, NULL);

    /* 直接指定 workqueue，会自动绑定 */
    mr_workqueue_work(&queue, &work);  /* 第一次调度，自动绑定 */
    mr_work_schedule(&work);         /* 之后可以重复调度 */
}

/* ✗ 错误：未绑定就调度 */
static void wrong_usage_1(void) {
    mr_work_t work;

    mr_work_init(&work, work_entry, NULL);
    /* 错误：未绑定 */
    // mr_work_schedule(&work);  /* 返回 -MR_ENOENT */
}

/* ✓ 正确：检查返回值 */
static void correct_usage_2(void) {
    mr_work_t work;
    mr_err_t ret;

    /* ... */

    ret = mr_work_schedule(&work);
    if (ret == -MR_EBUSY) {
        /* work 已经在运行 */
        printf("Work is already running\n");
    }
}

/* ✓ 正确：使用 hook 实现按需执行 */
static void correct_hook_usage(void) {
    /* 初始化 workqueue 和 hook */
    init_hook_workqueue();

    /* 调度 work */
    mr_work_schedule(&work1);

    /* 工作线程会被自动唤醒 */
}

/* ✗ 错误：在 work 中调度自己（死循环） */
static void recursive_work_entry(mr_work_t *work, void *param) {
    /* 处理任务 */
    do_task();

    /* 错误：再次调度自己 */
    // mr_work_schedule(work);  /* 死循环！ */
}

/* ✓ 正确：在 work 中重新调度自己（需要条件） */
static void conditional_recursive_entry(mr_work_t *work, void *param) {
    /* 处理任务 */
    if (need_retry()) {
        /* 有条件地重新调度 */
        mr_work_schedule(work);
    }
}

/* ✓ 正确：删除前取消 work */
static void correct_cleanup(void) {
    mr_work_t work;

    /* ... */

    /* 取消 work */
    mr_work_cancel(&work);

    /* 删除 work */
    mr_work_del(&work);
}

/* ✓ 正确：检查 work 是否在运行 */
static void correct_check_running(void) {
    if (MR_WORK_IS_RUNNING(&work)) {
        /* work 正在运行 */
        printf("Work is running\n");
    } else {
        /* work 未运行 */
        mr_work_schedule(&work);
    }
}
```

---

## 3. 设计思路

### 3.1 设计目标

workqueue 模块的设计目标：

1. **异步执行**：将工作推迟执行，避免在中断上下文中处理耗时操作
2. **资源优化**：多个延迟工作共享一个 timer，降低系统开销
3. **灵活调度**：支持立即执行和延迟执行
4. **可扩展**：支持多个 workqueue，实现优先级或 CPU 隔离
5. **系统集成**：通过 hook 机制与操作系统或信号量集成

### 3.2 timer 集成：节省资源的关键

**问题**：如果为每个延迟 work 创建一个 timer：

- 大量 timer 会增加全局 timer 链表的长度
- 插入 timer 的复杂度增加（O(n)）
- 每个 work 都需要独立的 timer 资源

**解决方案**：每个 workqueue 内置一个 timer

```c
typedef struct mr_workqueue {
    mr_timer_t timer;    /* 内置 timer */
    mr_list_t tlist;     /* 延迟 work 链表（按超时排序） */
    /* ... */
} mr_workqueue_t;
```

**工作原理**：

1. 延迟 work 添加到 tlist（按超时时间排序）
2. 设置 timer 为最近的超时时间
3. timer 到期时，将到期的 work 移到 list（立即执行链表）
4. 重新设置 timer 为下一个最近的超时时间

**优势**：

- 节省 timer 资源
- 降低全局 timer 链表压力
- 提高插入效率（O(log n) 降到 O(1)）
- 分散 timer 压力到各个 workqueue

### 3.3 depth 机制：保证 hook 正确调用

**问题场景**：

```c
/* Thread A */
mr_workqueue_work(&queue, &work1);  /* depth: 0 -> 1, 触发 wakeup */

/* Thread B */
mr_workqueue_execute(&queue);        /* depth: 1 -> 1, 执行 work */

/* Thread A */
mr_workqueue_work(&queue, &work2);  /* depth: 1 -> 2, 不触发 wakeup */

/* Thread B */
mr_workqueue_execute(&queue);        /* 执行完后 depth 设为 1 */
                                     /* 如果再执行，depth: 1 -> 0, 触发 suspend */
```

如果简单地在 `execute` 结束时调用 suspend，可能导致：

- 还有 work 在队列中就 suspend
- 或者在没有 work 时多次 suspend

**解决方案**：depth 计数器

```c
static void queue_wakeup(mr_workqueue_t *queue) {
    mr_atomic_t last;

    /* depth 加 1 */
    last = mr_atomic_fetch_add(&queue->depth, 1);

    if (last != 0) {
        /* depth 不是 0，说明已经唤醒过了 */
        return;
    }

    /* depth 从 0 变为 1，触发 wakeup */
    if (queue->hook && queue->hook->wakeup) {
        queue->hook->wakeup(queue, queue->hook->param);
    }
}
```

```c
static void queue_suspend(mr_workqueue_t *queue) {
    mr_atomic_t last;

    /* depth 减 1 */
    last = mr_atomic_fetch_sub(&queue->depth, 1);

    if (last != 1) {
        /* depth 不是 1，不需要 suspend */
        return;
    }

    /* depth 从 1 变为 0，触发 suspend */
    if (queue->hook && queue->hook->suspend) {
        queue->hook->suspend(queue, queue->hook->param);
    }
}
```

**在 execute 中使用**：

```c
mr_err_t mr_workqueue_execute(mr_workqueue_t *queue) {
    /* ... 执行所有 work ... */

    /* 将 depth 设为 1（重置状态） */
    mr_atomic_store(&queue->depth, 1);

    /* 尝试 suspend */
    queue_suspend(queue);

    return 0;
}
```

**为什么这样可以？**

- `queue_wakeup`：每次加 work 时 depth 加 1，只有从 0 变为 1 时才唤醒
- `queue_suspend`：执行完 work 后 depth 设为 1，只有从 1 变为 0 时才挂起
- 保证：
    - 有 work 时一定处于唤醒状态
    - 无 work 且执行完后才会挂起
    - 不会在还有 work 时挂起

### 3.4 延迟工作的排序算法

**tlist 按超时时间排序**：

```c
/* 插入延迟 work */
MR_LIST_FOR_EACH(l, &queue->tlist) {
    w = MR_LIST_ENTRY(l, mr_work_t, list);

    /* 比较超时时间（溢出安全） */
    if ((work->tick - w->tick) < (MR_TICK_MAX / 2)) {
        continue;  /* work 的超时时间更晚，继续遍历 */
    }

    /* 找到位置，插入在 w 之前 */
    mr_list_move(&work->list, &w->list);
    return;
}

/* 插入到链表尾部（最晚超时） */
mr_list_move(&work->list, &queue->tlist);
```

**timer 到期时处理**：

```c
static void queue_timer_entry(mr_timer_t *timer, void *param) {
    mr_workqueue_t *queue = (mr_workqueue_t *)param;
    mr_tick_t tickless, tick;
    mr_work_t *work;

    tick = mr_clock_tick();

    /* 检查到期的 work */
    while (!mr_list_is_empty(&queue->tlist)) {
        work = MR_LIST_ENTRY(queue->tlist.next, mr_work_t, list);

        /* 检查是否超时（溢出安全比较） */
        if ((tick - work->tick) >= (MR_TICK_MAX / 2)) {
            /* 未超时，计算距离下次超时的时间 */
            tickless = work->tick - tick;
            break;
        }

        /* 已超时，移到立即执行链表 */
        mr_list_move(&work->list, &queue->list);
    }

    /* 设置 timer 为下一个超时时间 */
    if (tickless != 0) {
        mr_timer_tick_set(&queue->timer, tickless | MR_TIMER_PERIODIC);
        mr_timer_start(&queue->timer);
    } else if (mr_list_is_empty(&queue->tlist)) {
        /* 没有延迟 work，停止 timer */
        mr_timer_stop(&queue->timer);
    }

    /* 触发 wakeup */
    queue_wakeup(queue);
}
```

**为什么需要第一个 work 的特殊处理**：

```c
/* 在添加延迟 work 时 */
if (&work->list == queue->tlist.next) {
    /* work 是第一个，需要设置 timer */
    tick = work->tick - mr_clock_tick();
    mr_timer_tick_set(&queue->timer, tick | MR_TIMER_PERIODIC);
    mr_timer_start(&queue->timer);
}
```

- 只有第一个 work 的超时时间最重要
- timer 会在第一个 work 到期时触发
- 到期时检查所有 work，移动已到期的
- 然后重新设置 timer 为下一个超时时间

### 3.5 oplist 机制：保护链表内存安全

和 timer 模块一样，workqueue 也使用 oplist 保护链表：

```c
mr_err_t mr_workqueue_execute(mr_workqueue_t *queue) {
    mr_list_t oplist;
    mr_work_t *work;
    int mask;

    /* 初始化临时链表 */
    mr_list_init(&oplist);

    lock();

    /* 将待执行链表替换到临时链表 */
    mr_list_replace(&oplist, &queue->list);

    /* 遍历临时链表 */
    while (!mr_list_is_empty(&oplist)) {
        work = MR_LIST_ENTRY(oplist.next, mr_work_t, list);

        /* 从临时链表移除 */
        mr_list_del(&work->list);

        unlock();

        /* 执行 work */
        work->entry(work, work->param);

        lock();

        /* 如果 work 在执行期间被取消，它已经不在任何链表中 */
        /* 继续处理下一个 */
    }

    unlock();

    /* 尝试 suspend */
    queue_suspend(queue);

    return 0;
}
```

**保护原理**：

1. 将 queue->list 替换到 oplist
2. queue->list 变为空，不会影响后续的 work 添加
3. 从 oplist 移除 work 后执行
4. 如果 work 在执行期间被取消，只是从 oplist 移除
5. 不会访问已释放的内存

### 3.6 work 绑定：实现多优先级和多 CPU 支持

**为什么需要绑定？**

```c
/* 未绑定的方式：每次都需要指定 workqueue */
mr_workqueue_work(&high_priority_queue, &work1);
mr_workqueue_work(&normal_priority_queue, &work2);
```

**绑定后的方式**：

```c
/* 绑定 */
mr_work_bind(&work1, &high_priority_queue);
mr_work_bind(&work2, &normal_priority_queue);

/* 调度（无需指定 workqueue） */
mr_work_schedule(&work1);  /* 自动调度到 high_priority_queue */
mr_work_schedule(&work2);  /* 自动调度到 normal_priority_queue */
```

**多优先级实现**：

```c
/* 定义不同优先级的 workqueue */
static mr_workqueue_t high_priority_queue;
static mr_workqueue_t normal_priority_queue;
static mr_workqueue_t low_priority_queue;

/* 高优先级 work */
mr_work_t *high_work = mr_work_create(high_priority_entry, NULL);
mr_work_bind(high_work, &high_priority_queue);

/* 普通优先级 work */
mr_work_t *normal_work = mr_work_create(normal_priority_entry, NULL);
mr_work_bind(normal_work, &normal_priority_queue);

/* 主循环：按优先级执行 */
while (1) {
    mr_workqueue_execute(&high_priority_queue);    /* 先执行高优先级 */
    mr_workqueue_execute(&normal_priority_queue);   /* 再执行普通优先级 */
    mr_workqueue_execute(&low_priority_queue);     /* 最后执行低优先级 */
}
```

**多 CPU 支持**：

```c
/* 为每个 CPU 创建一个 workqueue */
static mr_workqueue_t cpu0_queue;
static mr_workqueue_t cpu1_queue;

/* CPU 0 的 work */
mr_work_t *cpu0_work = mr_work_create(cpu0_handler, NULL);
mr_work_bind(cpu0_work, &cpu0_queue);

/* CPU 1 的 work */
mr_work_t *cpu1_work = mr_work_create(cpu1_handler, NULL);
mr_work_bind(cpu1_work, &cpu1_queue);

/* CPU 0 的线程 */
void cpu0_thread(void) {
    while (1) {
        mr_workqueue_execute(&cpu0_queue);  /* 只执行 CPU 0 的 work */
    }
}

/* CPU 1 的线程 */
void cpu1_thread(void) {
    while (1) {
        mr_workqueue_execute(&cpu1_queue);  /* 只执行 CPU 1 的 work */
    }
}
```

### 3.7 为什么使用周期定时器

timer 使用周期模式（`MR_TIMER_PERIODIC`），但每次到期后会重新设置间隔：

```c
/* 设置周期定时器 */
mr_timer_tick_set(&queue->timer, tickless | MR_TIMER_PERIODIC);
mr_timer_start(&queue->timer);

/* timer 到期时 */
tickless = work->tick - tick;  /* 下次超时时间 */
mr_timer_tick_set(&queue->timer, tickless | MR_TIMER_PERIODIC);
```

**为什么这样设计？**

1. **灵活性**：每次超时后根据下一个延迟 work 动态调整间隔
2. **tickless 支持**：返回值用于 clock 模块的 tickless 优化
3. **自动重启**：周期模式保证 timer 会持续触发
4. **节省资源**：没有延迟 work 时停止 timer

### 3.8 与其他机制的对比

#### vs timer 模块

| 特性    | workqueue          | timer       |
|-------|--------------------|-------------|
| 延迟执行  | 支持                 | 支持          |
| 多任务共享 | 一个 timer 管理多个 work | 每个 timer 独立 |
| 执行上下文 | 进程上下文（execute）     | 中断上下文       |
| 资源开销  | 低（共享 timer）        | 高（每个独立）     |
| 适用场景  | 耗时操作               | 短任务         |

#### vs 中断延迟处理（irq defer）

| 特性   | workqueue        | irq defer         |
|------|------------------|-------------------|
| 实现方式 | 独立模块             | 依赖 irq 模块         |
| 灵活性  | 高（可多个 workqueue） | 低（全局 defer queue） |
| 优先级  | 可自定义             | 固定                |

---

## 4. 参考资料

### 4.1 设计参考

- **Linux workqueue**：Linux 内核的工作队列机制
- **kthread worker**：Linux 内核的内核线程工作者
- **Windows ThreadPool**：Windows 的线程池机制

### 4.2 延伸阅读

- **生产者-消费者模式**：一种并发设计模式
- **线程池**：复用线程执行多个任务的设计模式
- **任务调度**：操作系统中的任务调度算法
- **异步 I/O**：非阻塞的 I/O 操作模式

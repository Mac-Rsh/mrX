# timer - 软件定时器

## 1. 模块概述

### 1.1 功能介绍

timer 模块实现了软件定时器（Software Timer）功能。软件定时器是一种基于系统时钟滴答的定时机制，允许用户在指定的时间后执行回调函数。

软件定时器的核心特点：

- **基于系统时钟**：依赖 clock 模块的时钟滴答作为时间基准
- **回调驱动**：定时器到期时调用用户注册的回调函数
- **单次/周期**：支持一次性定时器和周期性定时器
- **动态管理**：可以在运行时启动、停止、修改定时器
- **线程安全**：使用自旋锁保护，支持多线程环境

### 1.2 主要特性

- **双模式支持**：通过 `MR_TIMER_PERIODIC` 标志控制单次或周期模式
- **高效管理**：使用有序链表管理定时器，按超时时间排序
- **溢出安全**：使用特殊的比较算法处理时钟溢出
- **内存安全**：巧妙的设计防止访问已释放的定时器内存
- **对象集成**：继承自 mr_object_t，支持引用计数和生命周期管理
- **零开销抽象**：内联函数无调用开销
- **中断安全**：使用 irqsave/irqrestore 保护临界区

### 1.3 适用场景

timer 模块适用于以下场景：

- **延迟执行**：在指定时间后执行某项任务
- **周期性任务**：定期执行心跳检测、状态更新等
- **超时检测**：为阻塞操作设置超时时间
- **任务调度**：实现简单的任务调度器
- **去抖动**：延迟处理频繁触发的事件

**不适用场景**：

- **硬实时要求**：软件定时器受系统调度影响，无法保证精确的硬实时
- **微秒级定时**：精度受限于时钟滴答周期
- **高频定时**：大量高频定时器会增加系统开销

### 1.4 使用注意事项

⚠️ **重要警告**：

1. **回调函数限制**：定时器回调函数中不能执行耗时操作，否则会影响其他定时器的处理

2. **内存安全**：删除定时器后不要再访问其内存，即使回调可能正在执行

3. **周期定时器**：周期定时器会自动重启，如需停止必须显式调用 `mr_timer_stop()`

4. **时间精度**：定时器精度受限于 `MR_CFG_TICK_PER_SECOND` 配置

---

## 2. 使用指南

### 2.1 核心数据结构

#### mr_timer_t

```c
typedef struct mr_timer {
    mr_object_t parent;    /* 父类：对象系统 */
    mr_tick_t itick;       /* 初始定时时钟数（包含周期标志） */
    mr_tick_t ttick;       /* 目标超时时刻的绝对时钟数 */
    mr_list_t list;        /* 链表节点：用于插入定时器管理链表 */
    mr_ptr_t entry;        /* 定时器回调函数指针 */
    mr_ptr_t param;        /* 回调函数参数 */
} mr_timer_t;
```

**字段说明**：

- `itick`：用户设置的定时时长，最高位用于标志是否为周期定时器
- `ttick`：定时器应该触发的绝对时刻（时钟滴答数）
- `list`：链表节点，定时器运行时被插入全局定时器链表

#### mr_timer_entry_t

```c
typedef void (*mr_timer_entry_t)(mr_timer_t *timer, void *param);
```

定时器回调函数类型：

- `timer`：定时器对象指针
- `param`：用户自定义参数

### 2.2 主要接口说明

#### 初始化接口

##### MR_TIMER_INIT()

```c
#define MR_TIMER_INIT(_timer, _entry, _param, _tick)
```

**功能**：静态初始化定时器

**参数**：

- `_timer`：定时器变量地址
- `_entry`：回调函数
- `_param`：回调函数参数
- `_tick`：定时时长（时钟滴答数），周期定时器使用 `tick | MR_TIMER_PERIODIC`

**示例**：

```c
static void my_timer_entry(mr_timer_t *timer, void *param);

/* 单次定时器：1000 ticks */
static mr_timer_t timer = MR_TIMER_INIT(&timer, my_timer_entry, NULL, 1000);

/* 周期定时器：100 ticks */
static mr_timer_t periodic_timer = MR_TIMER_INIT(&periodic_timer,
                                                 my_timer_entry,
                                                 NULL,
                                                 100 | MR_TIMER_PERIODIC);
```

##### mr_timer_init()

```c
mr_err_t mr_timer_init(mr_timer_t *timer,
                       mr_timer_entry_t *entry,
                       void *param,
                       mr_tick_t tick);
```

**功能**：动态初始化定时器

**参数**：

- `timer`：定时器指针
- `entry`：回调函数
- `param`：回调函数参数
- `tick`：定时时长（时钟滴答数）

**返回值**：

- `0`：成功
- 负数：错误码

##### mr_timer_create()

```c
mr_timer_t *mr_timer_create(mr_timer_entry_t *entry,
                            void *param,
                            mr_tick_t tick);
```

**功能**：创建并初始化动态定时器

**参数**：

- `entry`：回调函数
- `param`：回调函数参数
- `tick`：定时时长（时钟滴答数）

**返回值**：

- 非 `NULL`：定时器指针
- `NULL`：创建失败（内存不足）

**注意**：动态创建的定时器需要使用 `mr_timer_del()` 释放

#### 控制接口

##### mr_timer_start()

```c
mr_err_t mr_timer_start(mr_timer_t *timer);
```

**功能**：启动定时器

**参数**：

- `timer`：定时器指针

**返回值**：

- `0`：成功
- 负数：错误码

**实现原理**：

- 计算绝对超时时刻：`ttick = 当前时钟 + itick`
- 将定时器插入全局定时器链表（按超时时刻排序）

##### mr_timer_stop()

```c
mr_err_t mr_timer_stop(mr_timer_t *timer);
```

**功能**：停止定时器

**参数**：

- `timer`：定时器指针

**返回值**：

- `0`：成功
- 负数：错误码

**实现原理**：

- 从全局定时器链表中移除定时器
- 如果回调正在执行，等待其完成

##### mr_timer_tick_set()

```c
mr_err_t mr_timer_tick_set(mr_timer_t *timer, mr_tick_t tick);
```

**功能**：修改定时器定时时长

**参数**：

- `timer`：定时器指针
- `tick`：新的定时时长

**返回值**：

- `0`：成功
- 负数：错误码

**注意**：

- 如果定时器正在运行，会先停止它
- 需要重新启动定时器才能生效

#### 查询接口

##### MR_TIMER_IS_TIMING()

```c
#define MR_TIMER_IS_TIMING(_timer)
```

**功能**：检查定时器是否正在计时

**返回值**：

- `MR_TRUE`：定时器正在运行
- `MR_FALSE`：定时器未运行

**实现原理**：

- 检查定时器的 `list` 节点是否在链表中
- 空链表表示定时器未启动

##### mr_timer_tick()

```c
mr_tick_t mr_timer_tick(mr_timer_t *timer);
```

**功能**：获取定时器的定时时长

**返回值**：

- 定时时长（时钟滴答数）

#### 生命周期管理

##### mr_timer_get()

```c
mr_timer_t *mr_timer_get(mr_timer_t *timer);
```

**功能**：增加定时器引用计数

##### mr_timer_put()

```c
mr_bool_t mr_timer_put(mr_timer_t *timer);
```

**功能**：减少定时器引用计数，如果计数归零则释放资源

**返回值**：

- `MR_TRUE`：对象生命周期结束
- `MR_FALSE`：对象仍在使用

##### mr_timer_del()

```c
mr_err_t mr_timer_del(mr_timer_t *timer);
```

**功能**：删除定时器

**参数**：

- `timer`：定时器指针

**返回值**：

- `0`：成功
- 负数：错误码

**实现原理**：

- 从链表移除定时器
- 释放对象资源（动态分配的定时器会释放内存）

### 2.3 典型应用案例

#### 案例 1：基础用法 - 延迟执行

```c
#include <mrX/kernel/mr_timer.h>

static volatile int task_done = 0;
static mr_timer_t delay_timer;

/* 延迟任务回调 */
static void delayed_task_entry(mr_timer_t *timer, void *param) {
    printf("Delayed task executed\n");
    task_done = 1;
}

/* 启动延迟任务 */
void start_delayed_task(void) {
    task_done = 0;

    /* 初始化并启动定时器：1000 ticks 后执行 */
    mr_timer_init(&delay_timer, delayed_task_entry, NULL, 1000);
    mr_timer_start(&delay_timer);
}
```

#### 案例 2：周期定时器 - 心跳检测

```c
#include <mrX/kernel/mr_timer.h>

static mr_timer_t heartbeat_timer;
static int heartbeat_count = 0;

/* 心跳回调 */
static void heartbeat_entry(mr_timer_t *timer, void *param) {
    heartbeat_count++;

    /* 发送心跳信号 */
    send_heartbeat_signal();

    printf("Heartbeat: %d\n", heartbeat_count);
}

/* 启动心跳 */
void start_heartbeat(void) {
    /* 每 1000 ticks 发送一次心跳 */
    mr_timer_init(&heartbeat_timer,
                  heartbeat_entry,
                  NULL,
                  1000 | MR_TIMER_PERIODIC);
    mr_timer_start(&heartbeat_timer);
}

/* 停止心跳 */
void stop_heartbeat(void) {
    mr_timer_stop(&heartbeat_timer);
    mr_timer_del(&heartbeat_timer);
}
```

#### 案例 3：动态创建定时器 - 多任务超时管理

```c
#include <mrX/kernel/mr_timer.h>

typedef struct {
    int task_id;
    mr_timer_t *timer;
    int timeout;
} task_context_t;

/* 超时回调 */
static void task_timeout_entry(mr_timer_t *timer, void *param) {
    task_context_t *ctx = (task_context_t *)param;

    printf("Task %d timeout!\n", ctx->task_id);

    /* 处理超时 */
    handle_task_timeout(ctx->task_id);

    /* 删除定时器 */
    mr_timer_del(timer);
    free(ctx);
}

/* 为任务创建超时定时器 */
void create_task_timeout(int task_id, int timeout_ticks) {
    task_context_t *ctx = malloc(sizeof(task_context_t));

    ctx->task_id = task_id;
    ctx->timeout = timeout_ticks;

    /* 创建动态定时器 */
    ctx->timer = mr_timer_create(task_timeout_entry, ctx, timeout_ticks);
    if (ctx->timer) {
        mr_timer_start(ctx->timer);
    }
}

/* 任务完成时取消超时 */
void task_complete(int task_id) {
    /* 查找并删除对应的定时器 */
    /* ... */
}
```

#### 案例 4：在回调中操作定时器

```c
#include <mrX/kernel/mr_timer.h>

static mr_timer_t adaptive_timer;
static int current_interval = 100;

/* 自适应定时器回调 */
static void adaptive_timer_entry(mr_timer_t *timer, void *param) {
    static int count = 0;

    count++;

    /* 执行任务 */
    do_periodic_task();

    /* 根据条件调整定时器间隔 */
    if (need_slow_down()) {
        current_interval = 200;  /* 减慢频率 */
    } else if (need_speed_up()) {
        current_interval = 50;   /* 加快频率 */
    }

    /* 修改定时器间隔并重启 */
    mr_timer_tick_set(timer, current_interval | MR_TIMER_PERIODIC);
    mr_timer_start(timer);
}

/* 启动自适应定时器 */
void start_adaptive_timer(void) {
    mr_timer_init(&adaptive_timer,
                  adaptive_timer_entry,
                  NULL,
                  current_interval | MR_TIMER_PERIODIC);
    mr_timer_start(&adaptive_timer);
}
```

#### 案例 5：正确使用示例与常见陷阱

```c
#include <mrX/kernel/mr_timer.h>

/* ✓ 正确：回调函数简短快速 */
static void good_entry_1(mr_timer_t *timer, void *param) {
    /* 只做简单操作 */
    flag = 1;
}

/* ✓ 正确：周期定时器显式停止 */
static mr_timer_t periodic_timer;

static void periodic_entry(mr_timer_t *timer, void *param) {
    static int count = 0;

    count++;
    if (count >= 10) {
        /* 停止周期定时器 */
        mr_timer_stop(timer);
    }
}

void start_periodic(void) {
    mr_timer_init(&periodic_timer, periodic_entry, NULL, 100 | MR_TIMER_PERIODIC);
    mr_timer_start(&periodic_timer);
}

/* ✗ 错误：回调函数执行耗时操作 */
static void bad_entry_1(mr_timer_t *timer, void *param) {
    /* 错误：耗时操作会阻塞其他定时器 */
    // sleep(1);
    // heavy_computation();
}

/* ✓ 正确：删除定时器后不访问其内存 */
static mr_timer_t *dynamic_timer;

static void cleanup_entry(mr_timer_t *timer, void *param) {
    /* 正确：只设置标志，不访问 timer */
    cleanup_done = 1;
}

void cleanup_example(void) {
    dynamic_timer = mr_timer_create(cleanup_entry, NULL, 100);
    mr_timer_start(dynamic_timer);

    /* 删除定时器 */
    mr_timer_del(dynamic_timer);
    dynamic_timer = NULL;  /* 不再访问 */
}

/* ✗ 错误：删除定时器后仍访问 */
static void bad_cleanup(void) {
    mr_timer_t *timer = mr_timer_create(...);
    mr_timer_start(timer);
    mr_timer_del(timer);

    /* 错误：访问已删除的定时器 */
    // mr_timer_stop(timer);  /* 危险！ */
}

/* ✓ 正确：使用引用计数管理共享定时器 */
void share_timer(mr_timer_t *timer) {
    /* 增加引用 */
    mr_timer_get(timer);

    /* 使用定时器 */
    /* ... */

    /* 减少引用 */
    if (mr_timer_put(timer)) {
        /* 定时器已被删除 */
    }
}
```

---

## 3. 设计思路

### 3.1 设计目标

timer 模块的设计目标：

1. **简洁高效**：提供基本的软件定时器功能，接口简单易用
2. **安全可靠**：处理时钟溢出、防止内存访问越界、支持并发访问
3. **灵活性强**：支持单次和周期模式、静态和动态分配
4. **可移植性**：基于 clock 和 object 模块，跨平台兼容

### 3.2 定时器管理的实现原理

timer 模块使用**有序链表**管理所有活动定时器：

```c
/* 全局定时器链表（按超时时刻排序） */
static mr_list_t __timer_list = MR_LIST_INIT(&__timer_list);
```

**插入定时器**（`timer_insert`）：

1. 计算绝对超时时刻：`ttick = 当前时钟 + itick`
2. 遍历链表，找到合适的插入位置（保持有序）
3. 将定时器插入链表

**超时检查**（`timer_timeout_check`）：

1. 获取当前时钟 `tick`
2. 检查链表第一个定时器是否超时
3. 如果超时，从链表移除并执行回调
4. 重复直到没有超时的定时器

**为什么使用有序链表？**

- 只需要检查链表头部的定时器
- 不需要遍历整个链表
- 插入操作复杂度 O(n)，但 n 通常很小

### 3.3 溢出安全的比较

由于时钟滴答会溢出（例如 `mr_tick_t` 是 32 位无符号整数），必须使用特殊的比较算法：

```c
/* 检查是否超时 */
if ((tick - timer->ttick) >= (MR_TICK_MAX / 2)) {
    /* 未超时 */
} else {
    /* 已超时 */
}
```

**原理**：

- 无符号整数减法会自动处理溢出
- `MR_TICK_MAX / 2` 是阈值，超过这个值说明发生了回绕
- 例如：`tick = 100, timer->ttick = 0xFFFFFFFF`
    - `tick - timer->ttick = 100 - 0xFFFFFFFF = 101`（考虑溢出）
    - `101 < MR_TICK_MAX / 2`，说明 `tick` 在 `ttick` 之后（已超时）

### 3.4 内存安全设计：oplist 机制

timer 模块最巧妙的设计是使用临时链表 `oplist` 防止内存访问越界：

**问题场景**：

```c
/* 传统设计的问题 */
void timer_timeout_check(void) {
    mr_timer_t *timer = get_first_timer();

    /* 从链表移除 */
    list_del(&timer->list);

    /* 释放锁 */
    unlock();

    /* 执行回调（此时定时器可能被删除）*/
    timer->entry(timer, timer->param);  /* 危险！timer 可能已被释放 */

    /* 重新获取锁 */
    lock();
}
```

**timer 模块的解决方案**：

```c
static mr_tick_t timer_timeout_check(void) {
    mr_list_t oplist;  /* 临时操作链表 */

    mr_list_init(&oplist);
    lock();

    while (!mr_list_is_empty(&__timer_list)) {
        timer = get_first_timer();

        if (!is_timeout(timer)) {
            break;
        }

        /* 移到 oplist，而不是真正删除 */
        mr_list_move(&timer->list, &oplist);

        /* 保存回调和参数 */
        entry = timer->entry;
        param = timer->param;

        unlock();

        /* 执行回调 */
        entry(timer, param);

        lock();

        /* 检查定时器是否仍在 oplist */
        if (!mr_list_is_empty(&oplist)) {
            /* 定时器未被删除，可以安全访问 */
            if (timer->itick & MR_TIMER_PERIODIC) {
                /* 周期定时器，重新插入 */
                timer_insert(timer);
            }
        } else {
            /* 定时器已被删除，不访问其内存 */
        }
    }

    unlock();
}
```

**工作原理**：

1. **移到 oplist**：将超时的定时器从 `__timer_list` 移到 `oplist`
    - 此时 `__timer_list` 不包含这个定时器
    - 其他线程可以正常访问 `__timer_list`

2. **释放锁**：释放锁后执行回调
    - 允许回调函数中操作定时器（启动、停止、删除）

3. **检查 oplist**：回调执行后检查定时器是否还在 `oplist`
    - 如果仍在 `oplist`：说明定时器未被删除，可以安全访问
    - 如果不在 `oplist`：说明定时器已被其他代码删除，不能访问其内存

**单读单写模型**：

- 每个定时器只能在一个地方被操作
- `timer_timeout_check` 通过 `oplist` 操作正在超时的定时器
- 其他代码通过 `__timer_list` 操作其他定时器
- 两者不会冲突

**删除定时器的安全性**：

```c
mr_err_t mr_timer_del(mr_timer_t *timer) {
    lock();

    /* 从链表移除（可能在 __timer_list 或 oplist） */
    mr_list_del(&timer->list);

    unlock();

    /* 删除对象（如果回调正在执行，等待其完成）*/
    mr_object_del((mr_object_t *)timer);
}
```

### 3.5 周期定时器的实现

周期定时器通过 `itick` 的最高位标志：

```c
#define MR_TIMER_PERIODIC (1U << 31)
```

**为什么使用最高位？**

这是一个巧妙的设计，利用了溢出安全比较的特性：

1. **溢出安全比较的阈值**：`MR_TICK_MAX / 2`
2. **牺牲一位**：为了实现溢出安全比较，实际可用的定时范围是 `0 ~ MR_TICK_MAX/2 - 1`，牺牲了最高位
3. **复用最高位**：既然最高位在时间比较中不被使用，就可以用它来标志"周期模式"

例如，32 位 `mr_tick_t`：

- 最大值：`0xFFFFFFFF` (4294967295)
- 溢出安全阈值：`0x7FFFFFFF` (2147483647)
- 可用定时范围：`0 ~ 2147483646`
- 周期标志：`0x80000000` (2147483648)

这样既不影响溢出安全的比较，又实现了周期/单次模式的统一存储。

**启动定时器时**：

```c
/* 清除最高位，获取实际定时时长 */
tick = timer->itick & ~MR_TIMER_PERIODIC;
timer->ttick = mr_clock_tick() + tick;
```

**回调执行后**：

```c
/* 检查是否为周期定时器 */
if (timer->itick & MR_TIMER_PERIODIC) {
    /* 重新插入定时器 */
    timer_insert(timer);
}
```

### 3.6 定时器与时钟钩子的集成

timer 模块通过 clock 模块的钩子机制实现：

```c
void mr_kernel_timer_init(void) {
    /* 注册超时检查函数到时钟钩子 */
    mr_clock_hook_add(timer_timeout_check);
}
```

**工作流程**：

1. 每次时钟滴答，clock 模块调用所有注册的钩子函数
2. `timer_timeout_check` 被调用
3. 检查是否有定时器超时
4. 如果有，执行回调并返回距离下次超时的 tick 数
5. clock 模块利用返回值实现 tickless 机制

**返回值的作用**：

```c
static mr_tick_t timer_timeout_check(void) {
    /* ... */
    for (tless = 0, tick = mr_clock_tick(); !mr_list_is_empty(&__timer_list);) {
        timer = get_first_timer();

        if (!is_timeout(timer)) {
            /* 计算距离下次超时的 tick 数 */
            tless = timer->ttick - tick;
            break;
        }

        /* 处理超时定时器 */
        /* ... */
    }

    return tless;  /* 返回给 clock 模块用于低功耗优化 */
}
```

### 3.7 Tickless 机制的实现

Tickless（无滴答）是一种低功耗优化技术，系统在没有任务需要处理时进入低功耗模式，而不是持续产生时钟中断。

**传统定时方式的问题**：

```
传统方式（非 tickless）：
|---|---|---|---|---|---|---|---|---|
 ↑   ↑   ↑                   ↑
 每个时钟中断都唤醒 CPU，即使无事可做
```

**Tickless 定时方式**：

```
Tickless 方式：
|---------------------------|
    ↑（最近定时器超时时刻）
    只在需要时唤醒 CPU
```

**timer 模块如何支持 tickless**：

1. **计算最近超时时间**：`timer_timeout_check` 返回距离最近定时器超时的 tick 数
2. **通知硬件**：clock 模块将返回值传递给硬件定时器
3. **进入低功耗**：系统进入低功耗模式，硬件定时器倒计时
4. **精确唤醒**：硬件定时器到期时唤醒 CPU

**代码示例**：

```c
/* timer_timeout_check 返回值 */
static mr_tick_t timer_timeout_check(void) {
    mr_tick_t tless = 0;  /* 默认值：0 表示没有活动定时器 */

    /* 遍历定时器链表 */
    while (!mr_list_is_empty(&__timer_list)) {
        timer = get_first_timer();

        if (!is_timeout(timer)) {
            /* 计算距离超时还有多少 tick */
            tless = timer->ttick - tick;
            break;
        }

        /* 处理超时定时器 */
        /* ... */
    }

    return tless;  /* 返回给 clock 模块 */
}
```

**clock 模块如何使用返回值**（简化示例）：

```c
void clock_irq_handler(void) {
    /* 调用所有钩子 */
    MR_LIST_FOR_EACH_SAFE(hook, &clock_hooks) {
        tless = hook->func();  /* 例如 timer_timeout_check */
        if (tless < min_sleep) {
            min_sleep = tless;  /* 记录最小的睡眠时间 */
        }
    }

    /* 配置硬件定时器 */
    if (min_sleep > 0) {
        hw_timer_set_next(min_sleep);  /* 设置下次唤醒时间 */
    } else {
        hw_timer_set_default();  /* 没有定时器，使用默认间隔 */
    }
}
```

**低功耗优化效果**：

| 场景              | 传统方式     | Tickless 方式 |
|-----------------|----------|-------------|
| 定时器间隔 1000 tick | 1000 次中断 | 1 次中断       |
| CPU 唤醒次数        | 频繁       | 按需唤醒        |
| 功耗              | 高        | 低           |

**注意事项**：

1. **精度损失**：低功耗模式下可能有轻微的精度损失
2. **硬件支持**：需要硬件定时器支持可配置的周期
3. **多个钩子协调**：如果有多个钩子，选择最小的返回值

### 3.8 为什么使用自旋锁

timer 模块使用自旋锁而非互斥锁：

1. **临界区短**：锁操作的临界区非常短（链表插入/删除）
2. **中断上下文**：`timer_timeout_check` 可能在中断上下文调用
3. **实时性**：自旋锁延迟低，适合实时系统

```c
/* 使用 irqsave/irqrestore 版本 */
mask = mr_spinlock_lock_irqsave(&__timer_lock);
/* 临界区 */
mr_spinlock_unlock_irqrestore(&__timer_lock, mask);
```

---

## 4. 参考资料

### 4.1 设计参考

- **Linux kernel timer**：Linux 内核的定时器实现
- **Zephyr Timer API**：Zephyr RTOS 的定时器子系统
- **FreeRTOS Software Timers**：FreeRTOS 的软件定时器

### 4.2 延伸阅读

- **定时器轮（Timer Wheel）**：更高效的定时器管理算法
- **时间堆（Timing Heap）**：基于堆的定时器管理
- **tickless 无滴答内核**：低功耗系统的时钟管理
- **级联定时器（Cascade Timer）**：硬件定时器的级联使用

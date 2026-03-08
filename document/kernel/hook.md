# hook - 钩子系统

## 1. 模块概述

### 1.1 功能介绍

hook 模块实现了一个轻量级的回调函数管理器。它允许系统在特定事件发生时通知多个已注册的回调函数，这是一种观察者模式的实现。

钩子系统的核心功能：

- **回调注册**：动态添加回调函数到钩子
- **批量调用**：遍历并调用所有已注册的回调函数
- **线程安全**：使用自旋锁保护，支持多线程环境
- **固定容量**：预分配数组，无需动态内存分配

### 1.2 主要特性

- **零开销抽象**：所有操作都是内联或简单的数组操作
- **静态分配**：使用用户提供的数组，不进行动态内存分配
- **线程安全**：使用自旋锁保护，支持中断上下文
- **类型安全**：通过宏提供类型安全的遍历接口
- **简单高效**：只有三个接口（init、add、遍历宏）
- **无删除功能**：简化设计，不支持删除单个回调

### 1.3 适用场景

hook 模块适用于以下场景：

- **事件通知**：系统事件发生时通知多个监听器
- **回调扩展**：允许模块注册回调以扩展功能
- **时钟钩子**：clock 模块使用 hook 管理时钟回调
- **日志钩子**：多个日志输出目标的统一管理
- **调试钩子**：调试事件发生时触发多个调试操作

**不适用场景**：

- **需要动态删除回调**：hook 不支持删除单个回调
- **需要回调优先级**：hook 按注册顺序调用，无优先级
- **大量回调**：固定容量，不适合大量回调场景

### 1.4 使用注意事项

⚠️ **重要警告**：

1. **容量限制**：hook 容量在初始化时确定，超过会返回错误

2. **无删除功能**：一旦添加的回调无法单独删除

3. **回调执行顺序**：按添加顺序执行（FIFO）

4. **回调中的限制**：回调函数中不能添加新的回调（可能导致死锁）

5. **类型一致性**：使用 `MR_HOOK_EACH` 时确保类型转换正确

---

## 2. 使用指南

### 2.1 核心数据结构

#### mr_hook_t

```c
typedef struct mr_hook {
    mr_ptr_t *entry;     /* 回调函数指针数组 */
    mr_uint16_t size;    /* 数组容量 */
    mr_uint16_t next;    /* 下一个空闲位置索引 */
} mr_hook_t;
```

**字段说明**：

- `entry`：指向回调函数指针数组的指针（由用户提供）
- `size`：数组的最大容量（在初始化时确定）
- `next`：下一个可用的数组索引（0 ~ size-1）

**内存布局**：

```
entry 数组（size = 4）:
+-------+-------+-------+-------+
| func0 | func1 | func2 |  NULL |
+-------+-------+-------+-------+
  ^                    ^
  |                    |
  next = 3             size = 4
```

### 2.2 主要接口说明

#### 初始化接口

##### MR_HOOK_INIT()

```c
#define MR_HOOK_INIT(_entry, _size)
```

**功能**：静态初始化钩子

**参数**：

- `_entry`：回调函数指针数组
- `_size`：数组容量

**示例**：

```c
static void (*callbacks[4])(void);
static mr_hook_t my_hook = MR_HOOK_INIT(callbacks, 4);
```

##### mr_hook_init()

```c
MR_INLINE void mr_hook_init(mr_hook_t *hook, mr_ptr_t *entry, mr_uint16_t size);
```

**功能**：动态初始化钩子

**参数**：

- `hook`：钩子对象指针
- `entry`：回调函数指针数组
- `size`：数组容量

**示例**：

```c
void (*callbacks[8])(void);
mr_hook_t hook;

/* 初始化钩子，容量为 8 */
mr_hook_init(&hook, callbacks, 8);
```

#### 注册接口

##### mr_hook_add()

```c
mr_err_t mr_hook_add(mr_hook_t *hook, mr_ptr_t entry);
```

**功能**：添加回调函数到钩子

**参数**：

- `hook`：钩子对象指针
- `entry`：回调函数指针

**返回值**：

- `0`：成功
- `-MR_ENOMEM`：钩子已满，无法添加

**实现原理**：

```c
if (hook->next < hook->size) {
    hook->entry[hook->next++] = entry;
    return 0;
} else {
    return -MR_ENOMEM;
}
```

**注意事项**：

- 线程安全：使用自旋锁保护
- 不检查重复：可以多次添加同一个回调

#### 遍历接口

##### MR_HOOK_EACH()

```c
#define MR_HOOK_EACH(_i, _entry, _type, _hook)
```

**功能**：遍历钩子中的所有回调函数

**参数**：

- `_i`：循环索引变量（必须已声明）
- `_entry`：当前回调函数指针（必须已声明）
- `_type`：回调函数的类型
- `_hook`：钩子对象指针

**示例**：

```c
typedef void (*callback_t)(int);

void call_all_callbacks(mr_hook_t *hook, int value) {
    int i;
    callback_t fn;

    /* 遍历所有回调 */
    MR_HOOK_EACH(i, fn, callback_t, hook) {
        fn(value);  /* 调用回调函数 */
    }
}
```

**宏展开后的效果**：

```c
for (i = 0, fn = (callback_t *)hook->entry[i];
     i < hook->next;
     i++, fn = (callback_t *)hook->entry[i]) {
    fn(value);
}
```

### 2.3 典型应用案例

#### 案例 1：基础用法 - 事件通知系统

```c
#include <mrX/kernel/mr_hook.h>

/* 回调函数类型 */
typedef void (*event_callback_t)(const char *event, void *data);

/* 钩子和回调数组 */
static event_callback_t g_event_callbacks[8];
static mr_hook_t g_event_hook;

/* 初始化事件系统 */
void event_system_init(void) {
    mr_hook_init(&g_event_hook,
                 (mr_ptr_t *)g_event_callbacks,
                 8);
}

/* 注册事件监听器 */
mr_err_t register_event_listener(event_callback_t callback) {
    return mr_hook_add(&g_event_hook, (mr_ptr_t)callback);
}

/* 触发事件 */
void trigger_event(const char *event_name, void *event_data) {
    int i;
    event_callback_t callback;

    /* 通知所有监听器 */
    MR_HOOK_EACH(i, callback, event_callback_t, &g_event_hook) {
        callback(event_name, event_data);
    }
}

/* 使用示例 */
static void on_button_click(const char *event, void *data) {
    printf("Button clicked: %s\n", event);
}

static void on_led_toggle(const char *event, void *data) {
    printf("LED toggled: %s\n", event);
}

void app_init(void) {
    event_system_init();

    /* 注册监听器 */
    register_event_listener(on_button_click);
    register_event_listener(on_led_toggle);

    /* 触发事件 */
    trigger_event("user_action", NULL);
}
```

#### 案例 2：系统状态变化通知

```c
#include <mrX/kernel/mr_hook.h>

/* 回调函数类型 */
typedef void (*state_change_callback_t)(int old_state, int new_state);

/* 钩子 */
static state_change_callback_t g_state_callbacks[4];
static mr_hook_t g_state_hook = MR_HOOK_INIT(
    (mr_ptr_t *)g_state_callbacks,
    4
);

/* 全局状态 */
static int g_system_state = 0;

/* 注册状态变化监听器 */
mr_err_t register_state_monitor(state_change_callback_t callback) {
    return mr_hook_add(&g_state_hook, (mr_ptr_t)callback);
}

/* 改变系统状态 */
void set_system_state(int new_state) {
    int i;
    state_change_callback_t callback;
    int old_state = g_system_state;

    if (old_state == new_state) {
        return;  /* 状态未变化 */
    }

    g_system_state = new_state;

    /* 通知所有监听器 */
    MR_HOOK_EACH(i, callback, state_change_callback_t, &g_state_hook) {
        callback(old_state, new_state);
    }
}

/* 监听器示例 */
static void log_state_change(int old_state, int new_state) {
    printf("State: %d -> %d\n", old_state, new_state);
}

static void update_led_on_state_change(int old_state, int new_state) {
    if (new_state == 1) {
        led_on();
    } else {
        led_off();
    }
}
```

#### 案例 3：调试钩子 - 多个调试输出目标

```c
#include <mrX/kernel/mr_hook.h>

/* 调试回调类型 */
typedef void (*debug_output_fn)(const char *message);

/* 调试钩子 */
static debug_output_fn g_debug_outputs[4];
static mr_hook_t g_debug_hook = MR_HOOK_INIT(
    (mr_ptr_t *)g_debug_outputs,
    4
);

/* 注册调试输出 */
mr_err_t add_debug_output(debug_output_fn output) {
    return mr_hook_add(&g_debug_hook, (mr_ptr_t)output);
}

/* 调试输出函数 */
void debug_print(const char *fmt, ...) {
    char buffer[256];
    int i;
    debug_output_fn output;
    va_list args;

    /* 格式化消息 */
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    /* 输出到所有注册的目标 */
    MR_HOOK_EACH(i, output, debug_output_fn, &g_debug_hook) {
        output(buffer);
    }
}

/* 调试输出目标 */
static void debug_to_uart(const char *msg) {
    uart_puts(UART1, msg);
}

static void debug_to_file(const char *msg) {
    file_write(log_file, msg, strlen(msg));
}

static void debug_to_ram_buffer(const char *msg) {
    strncat(debug_buffer, msg, DEBUG_BUFFER_SIZE);
}

/* 初始化 */
void debug_init(void) {
    add_debug_output(debug_to_uart);
    add_debug_output(debug_to_file);
    add_debug_output(debug_to_ram_buffer);
}
```

#### 案例 4：定时器回调 - 统一的心跳管理

```c
#include <mrX/kernel/mr_hook.h>

/* 心跳回调类型 */
typedef void (*heartbeat_callback_t)(mr_tick_t tick);

/* 心跳钩子 */
static heartbeat_callback_t g_heartbeat_callbacks[16];
static mr_hook_t g_heartbeat_hook;

/* 初始化心跳系统 */
void heartbeat_init(void) {
    mr_hook_init(&g_heartbeat_hook,
                 (mr_ptr_t *)g_heartbeat_callbacks,
                 16);
}

/* 注册心跳回调 */
mr_err_t register_heartbeat(heartbeat_callback_t callback) {
    return mr_hook_add(&g_heartbeat_hook, (mr_ptr_t)callback);
}

/* 时钟中断处理程序 */
void timer_irq_handler(void) {
    static mr_tick_t tick_count = 0;
    int i;
    heartbeat_callback_t callback;

    tick_count++;

    /* 调用所有心跳回调 */
    MR_HOOK_EACH(i, callback, heartbeat_callback_t, &g_heartbeat_hook) {
        callback(tick_count);
    }
}

/* 心跳回调示例 */
static void task1_heartbeat(mr_tick_t tick) {
    if (tick % 10 == 0) {
        task1_update();
    }
}

static void task2_heartbeat(mr_tick_t tick) {
    if (tick % 50 == 0) {
        task2_update();
    }
}

static void watchdog_heartbeat(mr_tick_t tick) {
    watchdog_refresh();
}
```

#### 案例 5：正确使用示例与常见陷阱

```c
#include <mrX/kernel/mr_hook.h>

typedef void (*callback_t)(void);

/* ✓ 正确：检查返回值 */
static mr_err_t register_with_check(callback_t cb) {
    mr_err_t ret = mr_hook_add(&g_hook, (mr_ptr_t)cb);
    if (ret != 0) {
        printf("Failed to register callback: %d\n", ret);
        return ret;
    }
    return 0;
}

/* ✓ 正确：遍历时使用局部变量 */
static void call_all_correct(mr_hook_t *hook) {
    int i;
    callback_t fn;

    MR_HOOK_EACH(i, fn, callback_t, hook) {
        fn();  /* 调用回调 */
    }
}

/* ✗ 错误：遍历时添加回调（可能死锁） */
static void wrong_add_during_iterate(mr_hook_t *hook) {
    int i;
    callback_t fn;

    MR_HOOK_EACH(i, fn, callback_t, hook) {
        fn();
        /* 错误：在遍历时添加回调 */
        // mr_hook_add(hook, (mr_ptr_t)new_callback);  /* 死锁！ */
    }
}

/* ✓ 正确：使用 MR_HOOK_INIT 静态初始化 */
static callback_t g_callbacks[4];
static mr_hook_t g_hook = MR_HOOK_INIT((mr_ptr_t *)g_callbacks, 4);

/* ✗ 错误：类型不匹配 */
static void wrong_type_usage(mr_hook_t *hook) {
    int i;
    void *fn;  /* 错误类型 */

    /* 错误：fn 应该是函数指针类型 */
    // MR_HOOK_EACH(i, fn, callback_t, hook);
}

/* ✓ 正确：类型匹配 */
static void correct_type_usage(mr_hook_t *hook) {
    int i;
    callback_t fn;  /* 正确类型 */

    MR_HOOK_EACH(i, fn, callback_t, hook) {
        fn();
    }
}

/* ✓ 正确：容量足够 */
#define MAX_CALLBACKS 8
static callback_t g_callbacks[MAX_CALLBACKS];
static mr_hook_t g_hook;

void correct_capacity_init(void) {
    mr_hook_init(&g_hook, (mr_ptr_t *)g_callbacks, MAX_CALLBACKS);
}

/* ✗ 错误：容量不足 */
static callback_t g_small_callbacks[2];
static mr_hook_t g_small_hook;

void wrong_capacity_init(void) {
    mr_hook_init(&g_small_hook, (mr_ptr_t *)g_small_callbacks, 2);

    /* 注册超过 2 个回调会失败 */
    mr_hook_add(&g_small_hook, (mr_ptr_t)cb1);
    mr_hook_add(&g_small_hook, (mr_ptr_t)cb2);
    mr_err_t ret = mr_hook_add(&g_small_hook, (mr_ptr_t)cb3);
    /* ret = -MR_ENOMEM */
}

/* ✓ 正确：回调函数签名一致 */
typedef void (*my_callback_t)(int value);

static void my_callback(int value) {
    printf("Value: %d\n", value);
}

static void register_correct(void) {
    my_callback_t callbacks[4];
    mr_hook_t hook;
    int i;
    my_callback_t fn;

    mr_hook_init(&hook, (mr_ptr_t *)callbacks, 4);
    mr_hook_add(&hook, (mr_ptr_t)my_callback);

    /* 正确：类型匹配 */
    MR_HOOK_EACH(i, fn, my_callback_t, &hook) {
        fn(42);  /* 传递参数 */
    }
}
```

---

## 3. 设计思路

### 3.1 设计目标

hook 模块的设计目标：

1. **极简设计**：只提供最基本的功能，保持接口简洁
2. **零动态分配**：使用用户提供的数组，不进行内存分配
3. **线程安全**：使用自旋锁保护，支持多线程和中断
4. **类型安全**：通过宏提供类型安全的接口
5. **高效执行**：数组存储，遍历速度快

### 3.2 为什么不支持删除

hook 模块不支持删除单个回调函数，原因：

1. **简化设计**：删除功能会增加复杂度（需要处理数组空洞、移动元素等）
2. **避免重入**：删除操作可能在回调执行时发生，导致复杂的同步问题
3. **典型使用场景**：大多数情况下回调在系统初始化时注册，运行时不删除

**如果需要删除**：

- 重新初始化整个 hook（清空所有回调）
- 使用标志位，在回调函数中检查是否应该执行

### 3.3 使用自旋锁的原因

hook 模块使用自旋锁而非互斥锁：

1. **临界区极短**：添加回调只是简单的数组赋值操作
2. **中断上下文**：可能在中断处理程序中调用 `mr_hook_add`
3. **延迟低**：自旋锁延迟低，适合快速操作

```c
mr_err_t mr_hook_add(mr_hook_t *hook, mr_ptr_t entry) {
    int mask;

    /* 获取锁（禁用中断） */
    mask = mr_spinlock_lock_irqsave(&__hook_lock);

    /* 临界区：只执行几条指令 */
    if (hook->next < hook->size) {
        hook->entry[hook->next++] = entry;
        ret = 0;
    } else {
        ret = -MR_ENOMEM;
    }

    /* 释放锁（恢复中断） */
    mr_spinlock_unlock_irqrestore(&__hook_lock, mask);
    return ret;
}
```

### 3.4 固定容量的权衡

**固定容量的优点**：

- 无动态内存分配
- 内存占用可预测
- 实现简单，无内存碎片

**固定容量的缺点**：

- 容量需要预先估计
- 超过容量会失败（返回 `-MR_ENOMEM`）

**如何选择合适的容量**：

- 统计实际需要的回调数量
- 留出少量余量
- 对于大多数系统，4-8 个回调足够

### 3.5 与其他机制的对比

#### vs 函数指针数组

| 特性   | hook 模块 | 函数指针数组   |
|------|---------|----------|
| 线程安全 | 是       | 否        |
| 添加接口 | 统一      | 手动管理     |
| 遍历宏  | 类型安全    | 需要手动类型转换 |

#### vs 观察者模式

| 特性   | hook 模块 | 完整观察者模式   |
|------|---------|-----------|
| 复杂度  | 极简      | 较复杂       |
| 动态管理 | 添加      | 添加/删除/优先级 |
| 内存分配 | 静态      | 通常动态      |
| 适用场景 | 轻量级通知   | 复杂事件系统    |

#### vs 链表

| 特性   | hook 模块 | 链表       |
|------|---------|----------|
| 内存布局 | 连续数组    | 离散节点     |
| 遍历性能 | 好（缓存友好） | 一般       |
| 容量限制 | 有       | 无（受限于内存） |
| 删除操作 | 不支持     | 支持       |

### 3.6 hook 模块的应用场景

#### 时钟钩子（clock 模块）

```c
/* clock 模块使用 hook 管理时钟回调 */
typedef mr_tick_t (*clock_hook_fn_t)(void);

static clock_hook_fn_t g_clock_hooks[MR_CFG_CLOCK_HOOK_SIZE];
static mr_hook_t g_clock_hook;

/* 注册时钟钩子 */
mr_err_t mr_clock_hook_add(clock_hook_fn_t hook) {
    return mr_hook_add(&g_clock_hook, (mr_ptr_t)hook);
}

/* 时钟中断处理程序 */
void timer_irq_handler(void) {
    int i;
    clock_hook_fn_t hook;

    /* 调用所有时钟钩子 */
    MR_HOOK_EACH(i, hook, clock_hook_fn_t, &g_clock_hook) {
        hook();  /* 返回下次唤醒需要的 tick 数 */
    }
}
```

#### 日志钩子

```c
/* 多个日志输出目标 */
typedef void (*log_output_fn)(int level, const char *msg);

static log_output_fn g_log_outputs[4];
static mr_hook_t g_log_hook;

void log_message(int level, const char *fmt, ...) {
    char buffer[256];
    int i;
    log_output_fn output;
    va_list args;

    vsnprintf(buffer, sizeof(buffer), fmt, args);

    /* 输出到所有目标 */
    MR_HOOK_EACH(i, output, log_output_fn, &g_log_hook) {
        output(level, buffer);
    }
}
```

---

## 4. 参考资料

### 4.1 相关模块

- **spinlock**：自旋锁模块，提供并发访问保护
- **clock**：时钟模块，使用 hook 管理时钟回调

### 4.2 设计参考

- **观察者模式**：软件设计模式中的观察者模式
- **回调函数**：C 语言中的回调机制
- **Linux notifier chain**：Linux 内核的通知链机制

### 4.3 延伸阅读

- **事件驱动架构**：基于事件的软件架构模式
- **发布-订阅模式**：一种消息传递模式
- **信号与槽**：Qt 框架中的信号槽机制

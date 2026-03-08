# irq - 中断管理

## 1. 模块概述

### 1.1 功能介绍

irq 模块实现了中断请求（Interrupt Request）管理系统。它为上层提供了统一的中断注册、控制和处理接口，屏蔽了底层硬件的差异。

中断管理的核心功能：

- **中断注册**：为指定的中断号注册处理函数
- **中断控制**：使能、禁用中断，设置优先级和触发类型
- **共享中断**：多个处理函数共享同一个中断号
- **延迟处理**：将耗时操作推迟到进程上下文执行
- **嵌套控制**：支持嵌套的中断使能/禁用调用

### 1.2 主要特性

- **硬件抽象**：通过 `mr_irq_ops_t` 操作接口适配不同硬件平台
- **共享中断支持**：多个设备可以共享同一个中断号
- **延迟处理机制**：可选的 defer 机制，将耗时操作推迟到 workqueue
- **线程安全**：使用自旋锁保护，支持多核环境
- **引用计数**：继承自 mr_object_t，支持生命周期管理
- **灵活配置**：支持边沿触发（上升沿、下降沿、双边沿）和电平触发（高电平、低电平）
- **dying 标志**：防止删除时有新的操作

### 1.3 适用场景

irq 模块适用于以下场景：

- **驱动程序开发**：为设备驱动注册中断处理函数
- **外部中断**：处理 GPIO 外部中断、串口中断等
- **定时器中断**：处理硬件定时器中断
- **共享中断线**：多个设备共享同一中断线（如 PCI 设备）

**不适用场景**：

- **异常处理**：CPU 异常（如缺页、非法指令）不属于 irq 模块管理范围
- **软中断**：软中断和 tasklet 应使用其他机制

### 1.4 使用注意事项

⚠️ **重要警告**：

1. **中断上下文限制**：中断处理函数在中断上下文中执行，不能睡眠或阻塞

2. **快速执行**：中断处理函数应尽快执行完成，耗时操作应使用 defer 机制

3. **共享中断要求**：共享中断的所有 handler 必须使用相同的 flags（触发类型）

4. **资源清理**：删除设备或驱动时必须释放注册的中断

5. **优先级设置**：优先级数值越小，优先级越高（0：最高，255：最低）

---

## 2. 使用指南

### 2.1 核心数据结构

#### mr_irq_t

```c
typedef struct mr_irq {
    mr_object_t parent;        /* 父类：对象系统 */
    mr_atomic_t dying;         /* 删除标志：防止删除时有新操作 */
    mr_uint32_t istart;        /* 管理的中断号起始值 */
    mr_uint32_t iend;          /* 管理的中断号结束值 */
    const mr_irq_ops_t *ops;   /* 硬件操作接口 */
    mr_ptr_t priv;             /* 私有数据 */
} mr_irq_t;
```

中断控制器对象，管理一个连续范围的中断号。

#### mr_irq_ops_t

```c
typedef struct mr_irq_ops {
    void (*priority_set)(mr_uint32_t irq, mr_uint8_t priority, void *priv);
    void (*type_set)(mr_uint32_t irq, mr_uint8_t type, void *priv);
    void (*enable)(mr_uint32_t irq, void *priv);
    void (*unmask)(mr_uint32_t irq, void *priv);
    void (*disable)(mr_uint32_t irq, void *priv);
    void (*mask)(mr_uint32_t irq, void *priv);
} mr_irq_ops_t;
```

硬件操作接口，由移植层实现：

- `priority_set`：设置中断优先级
- `type_set`：设置中断触发类型
- `enable`：使能中断（在硬件层面启用）
- `unmask`：解除屏蔽（允许中断触发）
- `disable`：禁用中断（在硬件层面禁用）
- `mask`：屏蔽中断（临时禁止触发）

#### mr_irq_desc_t

```c
typedef struct mr_irq_desc {
    mr_irq_action_t *action;   /* 中断处理函数链表 */
    mr_uint16_t priority;      /* 中断优先级 */
    mr_uint16_t flags;         /* 中断标志（触发类型等） */
    mr_atomic_t depth;         /* 嵌套深度计数器（1=禁用，0=使能） */
    mr_irq_t *irq;             /* 所属的中断控制器 */
} mr_irq_desc_t;
```

中断描述符，描述一个中断号的状态。

**depth 的含义**：

- 初始值为 `1`（禁用状态）
- 每次 `enable` 减 1
- 每次 `disable` 加 1
- 只有从 `1` 变为 `0` 时才真正使能中断
- 支持嵌套调用

#### mr_irq_action_t

```c
typedef struct mr_irq_action {
    struct mr_irq_action *next; /* 链表下一个节点 */
    mr_uint32_t pending: 1;     /* 是否正在执行 */
    mr_uint32_t irq: 31;        /* 中断号 */
    mr_ptr_t entry;             /* 中断处理函数 */
    mr_ptr_t owner;             /* 所有者（用于匹配释放） */
#if defined(MR_USE_IRQ_DEFER)
    mr_work_t defer;            /* 延迟工作项 */
    mr_ptr_t defer_entry;       /* 延迟处理函数 */
#endif
} mr_irq_action_t;
```

中断动作，表示一个注册的中断处理函数。

**pending 的作用**：

- 标记 handler 正在执行
- 如果在执行期间调用 `free`，会将 `owner` 设为 `NULL`
- 等 handler 执行完后才真正释放内存
- 防止执行期间被释放导致内存访问越界

#### mr_irq_entry_t

```c
typedef mr_irq_return_t (*mr_irq_entry_t)(mr_uint32_t irq, void *owner);
```

中断处理函数类型：

- `irq`：中断号
- `owner`：所有者（注册时传入的 owner）
- 返回值：`mr_irq_return_t`

#### mr_irq_return_t

```c
typedef enum mr_irq_return {
    MR_IRQ_NONE,       /* 中断不是由本设备产生的（共享中断场景） */
    MR_IRQ_HANDLED,    /* 中断已处理 */
    MR_IRQ_DEFERRED,   /* 延迟处理（需要调用 defer_entry） */
} mr_irq_return_t;
```

中断返回值类型。

### 2.2 主要接口说明

#### 初始化接口

##### mr_irq_init()

```c
mr_err_t mr_irq_init(mr_irq_t *irq,
                     mr_uint32_t irq_start,
                     mr_uint32_t irq_end,
                     const mr_irq_ops_t *ops,
                     void *priv);
```

**功能**：初始化中断控制器

**参数**：

- `irq`：中断控制器对象
- `irq_start`：管理的起始中断号
- `irq_end`：管理的结束中断号
- `ops`：硬件操作接口
- `priv`：私有数据（传递给 ops 函数）

**返回值**：

- `0`：成功
- `-MR_EEXIST`：中断号范围已被占用

**示例**：

```c
static mr_irq_t g_irq_ctrl;

/* 初始化中断控制器，管理中断号 0-31 */
mr_err_t ret = mr_irq_init(&g_irq_ctrl, 0, 31, &platform_irq_ops, NULL);
```

##### mr_irq_create()

```c
mr_irq_t *mr_irq_create(mr_uint32_t irq_start,
                        mr_uint32_t irq_end,
                        const mr_irq_ops_t *ops,
                        void *priv);
```

**功能**：创建动态中断控制器

**返回值**：

- 非 `NULL`：中断控制器指针
- `NULL`：创建失败

#### 中断注册/注销

##### mr_irq_request()

```c
mr_err_t mr_irq_request(mr_uint32_t irq,
                        mr_irq_entry_t *entry,
                        void *owner,
                        mr_uint16_t flags);
```

**功能**：注册中断处理函数

**参数**：

- `irq`：中断号
- `entry`：中断处理函数
- `owner`：所有者（释放时用于匹配）
- `flags`：中断标志（触发类型、共享标志等）

**返回值**：

- `0`：成功
- `-MR_EEXIST`：该 owner 已注册过此中断
- `-MR_EBUSY`：中断已被占用且未设置共享标志
- `-MR_EINVAL`：共享中断的 flags 不一致

**中断标志**：

```c
#define MR_IRQ_EDGE_RISING    (0x01U)  /* 上升沿触发 */
#define MR_IRQ_EDGE_FALLING   (0x02U)  /* 下降沿触发 */
#define MR_IRQ_EDGE_BOTH      (0x03U)  /* 双边沿触发 */
#define MR_IRQ_LEVEL_HIGH     (0x04U)  /* 高电平触发 */
#define MR_IRQ_LEVEL_LOW      (0x08U)  /* 低电平触发 */
#define MR_IRQ_SHARED         (0x10U)  /* 共享中断标志 */
```

##### mr_irq_defer_request()

```c
mr_err_t mr_irq_defer_request(mr_uint32_t irq,
                              mr_irq_entry_t *entry,
                              void *owner,
                              mr_uint16_t flags,
                              mr_irq_entry_t *defer_entry);
```

**功能**：注册支持延迟处理的中断处理函数

**参数**：

- `irq`：中断号
- `entry`：中断处理函数（在中断上下文执行）
- `owner`：所有者
- `flags`：中断标志
- `defer_entry`：延迟处理函数（在进程上下文执行）

**工作流程**：

1. 中断发生时，调用 `entry`（中断上下文）
2. 如果 `entry` 返回 `MR_IRQ_DEFERRED`，将 `defer_entry` 加入 workqueue
3. 在进程上下文执行 `defer_entry`

##### mr_irq_free()

```c
mr_err_t mr_irq_free(mr_uint32_t irq, void *owner);
```

**功能**：释放中断处理函数

**参数**：

- `irq`：中断号
- `owner`：所有者

**返回值**：

- `0`：成功
- `-MR_ENOENT`：中断不存在或 owner 不匹配

#### 中断控制

##### mr_irq_enable()

```c
mr_err_t mr_irq_enable(mr_uint32_t irq);
```

**功能**：使能中断（嵌套 depth 减 1）

**注意**：只有当 depth 从 1 变为 0 时才真正使能

##### mr_irq_disable()

```c
mr_err_t mr_irq_disable(mr_uint32_t irq);
```

**功能**：禁用中断（嵌套 depth 加 1）

##### mr_irq_priority_set()

```c
mr_err_t mr_irq_priority_set(mr_uint32_t irq, mr_uint8_t priority);
```

**功能**：设置中断优先级

**参数**：

- `irq`：中断号
- `priority`：优先级（0：最高，255：最低）

##### mr_irq_priority()

```c
int mr_irq_priority(mr_uint32_t irq);
```

**功能**：获取中断优先级

**返回值**：

- `>= 0`：优先级值
- `< 0`：错误码

##### mr_irq_type()

```c
int mr_irq_type(mr_uint32_t irq);
```

**功能**：获取中断触发类型

**返回值**：

- 触发类型标志
- `< 0`：错误码

#### 中断处理

##### mr_irq_handle()

```c
mr_irq_return_t mr_irq_handle(mr_uint32_t irq);
```

**功能**：处理中断（由硬件中断处理程序调用）

**工作流程**：

1. 获取自旋锁
2. 遍历该中断的所有 handler
3. 标记 handler 为 pending
4. 释放锁，调用 handler
5. 重新获取锁，清除 pending
6. 如果 handler 返回 `MR_IRQ_HANDLED`，停止遍历
7. 如果 handler 返回 `MR_IRQ_DEFERRED`，加入 defer workqueue
8. 解除中断屏蔽

**注意**：此函数通常由移植层的中断服务程序（ISR）调用

##### mr_irq_defer_execute()

```c
void mr_irq_defer_execute(void);
```

**功能**：执行延迟的中断处理

**注意**：需要在进程上下文中定期调用此函数

#### 生命周期管理

##### mr_irq_get()

```c
mr_irq_t *mr_irq_get(mr_irq_t *irq);
```

**功能**：增加中断控制器引用计数

##### mr_irq_put()

```c
mr_bool_t mr_irq_put(mr_irq_t *irq);
```

**功能**：减少中断控制器引用计数

##### mr_irq_del()

```c
mr_err_t mr_irq_del(mr_irq_t *irq);
```

**功能**：删除中断控制器

**实现原理**：

1. 设置 `dying` 标志
2. 禁用并释放所有中断号
3. 释放对象资源

### 2.3 典型应用案例

#### 案例 1：基础用法 - GPIO 外部中断

```c
#include <mrX/kernel/mr_irq.h>

/* 中断处理函数 */
static mr_irq_return_t button_isr(mr_uint32_t irq, void *owner) {
    /* 快速处理：读取状态，清除中断标志 */
    int button_state = read_button_state();

    /* 设置标志，让主循环处理 */
    button_pressed = 1;
    last_button_state = button_state;

    return MR_IRQ_HANDLED;
}

/* 初始化 */
void app_init(void) {
    mr_err_t ret;

    /* 注册中断：中断号 10，上升沿触发 */
    ret = mr_irq_request(10, button_isr, &button_device, MR_IRQ_EDGE_RISING);
    if (ret != 0) {
        printf("Failed to request IRQ: %d\n", ret);
    }
}

/* 清理 */
void app_cleanup(void) {
    /* 释放中断 */
    mr_irq_free(10, &button_device);
}
```

#### 案例 2：共享中断 - 多设备共享同一中断线

```c
#include <mrX/kernel/mr_irq.h>

/* 设备 A 的中断处理 */
static mr_irq_return_t device_a_isr(mr_uint32_t irq, void *owner) {
    device_t *dev = (device_t *)owner;

    /* 检查是否是本设备产生的中断 */
    if (!device_a_check_interrupt(dev)) {
        return MR_IRQ_NONE;  /* 不是本设备，继续传递 */
    }

    /* 处理中断 */
    device_a_handle_interrupt(dev);
    return MR_IRQ_HANDLED;
}

/* 设备 B 的中断处理 */
static mr_irq_return_t device_b_isr(mr_uint32_t irq, void *owner) {
    device_t *dev = (device_t *)owner;

    if (!device_b_check_interrupt(dev)) {
        return MR_IRQ_NONE;
    }

    device_b_handle_interrupt(dev);
    return MR_IRQ_HANDLED;
}

/* 初始化共享中断 */
void init_shared_irq_devices(void) {
    /* 注册第一个设备（注意：第一个可以不设置 MR_IRQ_SHARED） */
    mr_irq_request(15, device_a_isr, &device_a, MR_IRQ_LEVEL_HIGH | MR_IRQ_SHARED);

    /* 注册第二个设备（必须设置 MR_IRQ_SHARED，且 flags 必须相同） */
    mr_irq_request(15, device_b_isr, &device_b, MR_IRQ_LEVEL_HIGH | MR_IRQ_SHARED);
}
```

#### 案例 3：延迟处理 - 将耗时操作推迟到进程上下文

```c
#include <mrX/kernel/mr_irq.h>

/* 延迟处理函数（进程上下文） */
static void network_defer_handler(mr_uint32_t irq, void *owner) {
    network_device_t *dev = (network_device_t *)owner;

    /* 耗时操作：处理网络数据包 */
    process_network_packets(dev);

    /* 可能的耗时操作 */
    send_response(dev);
    update_statistics(dev);
}

/* 中断处理函数（中断上下文） */
static mr_irq_return_t network_isr(mr_uint32_t irq, void *owner) {
    network_device_t *dev = (network_device_t *)owner;

    /* 快速操作：读取数据到缓冲区 */
    read_data_to_buffer(dev);

    /* 清除硬件中断标志 */
    clear_hardware_interrupt(dev);

    /* 返回 DEFERRED，将耗时操作推迟 */
    return MR_IRQ_DEFERRED;
}

/* 初始化 */
void init_network_device(void) {
    /* 注册支持延迟处理的中断 */
    mr_irq_defer_request(20,
                        network_isr,
                        &network_dev,
                        MR_IRQ_EDGE_RISING,
                        network_defer_handler);
}

/* 主循环 */
void main_loop(void) {
    while (1) {
        /* 处理延迟的中断 */
        mr_irq_defer_execute();

        /* 其他任务 */
        /* ... */
    }
}
```

#### 案例 4：嵌套控制 - 使能/禁用嵌套调用

```c
#include <mrX/kernel/mr_irq.h>

void critical_section_1(void) {
    /* 禁用中断 */
    mr_irq_disable(10);

    /* 临界区操作 */
    critical_operation_1();

    /* 重新使能中断 */
    mr_irq_enable(10);
}

void critical_section_2(void) {
    mr_irq_disable(10);

    /* 调用另一个也需要禁用中断的函数 */
    critical_section_1();  /* depth: 1 -> 2 -> 1 */

    critical_operation_2();

    mr_irq_enable(10);     /* depth: 1 -> 0，此时才真正使能 */
}

/* 正确的配对调用 */
void correct_nesting(void) {
    mr_irq_disable(10);  /* depth: 1 -> 2 */
    mr_irq_disable(10);  /* depth: 2 -> 3 */
    mr_irq_enable(10);   /* depth: 3 -> 2 */
    mr_irq_enable(10);   /* depth: 2 -> 1 */
    /* 此时中断仍然禁用 */
    mr_irq_enable(10);   /* depth: 1 -> 0，真正使能 */
}
```

#### 案例 5：移植层实现 - 实现 mr_irq_ops

```c
#include <mrX/kernel/mr_irq.h>
#include <mrX/port/mr_port_irq.h>

/* 平台特定的中断控制实现 */
static void platform_irq_priority_set(mr_uint32_t irq, mr_uint8_t priority, void *priv) {
    /* 调用芯片厂商提供的库函数 */
    NVIC_SetPriority(irq, priority);
}

static void platform_irq_type_set(mr_uint32_t irq, mr_uint8_t type, void *priv) {
    GPIO_TypeDef *port = get_gpio_port(irq);
    uint16_t pin = get_gpio_pin(irq);

    switch (type) {
        case MR_IRQ_EDGE_RISING:
            GPIO_SetTriggerConfig(port, pin, GPIO_TRIGGER_RISING);
            break;
        case MR_IRQ_EDGE_FALLING:
            GPIO_SetTriggerConfig(port, pin, GPIO_TRIGGER_FALLING);
            break;
        case MR_IRQ_EDGE_BOTH:
            GPIO_SetTriggerConfig(port, pin, GPIO_TRIGGER_BOTH);
            break;
        case MR_IRQ_LEVEL_HIGH:
            GPIO_SetTriggerConfig(port, pin, GPIO_TRIGGER_HIGH_LEVEL);
            break;
        case MR_IRQ_LEVEL_LOW:
            GPIO_SetTriggerConfig(port, pin, GPIO_TRIGGER_LOW_LEVEL);
            break;
    }
}

static void platform_irq_enable(mr_uint32_t irq, void *priv) {
    NVIC_EnableIRQ(irq);
}

static void platform_irq_unmask(mr_uint32_t irq, void *priv) {
    /* 如果需要，解除屏蔽 */
}

static void platform_irq_disable(mr_uint32_t irq, void *priv) {
    NVIC_DisableIRQ(irq);
}

static void platform_irq_mask(mr_uint32_t irq, void *priv) {
    /* 临时屏蔽中断 */
}

/* 中断操作接口 */
static const mr_irq_ops_t platform_irq_ops = {
    .priority_set = platform_irq_priority_set,
    .type_set = platform_irq_type_set,
    .enable = platform_irq_enable,
    .unmask = platform_irq_unmask,
    .disable = platform_irq_disable,
    .mask = platform_irq_mask,
};

/* 硬件中断服务程序（由中断向量表调用） */
void EXTI0_IRQHandler(void) {
    /* 调用 irq 模块的中断处理函数 */
    mr_irq_handle(EXTI0_IRQn);
}

/* 初始化中断控制器 */
void platform_irq_init(void) {
    /* 初始化中断控制器，管理中断号 0-31 */
    mr_irq_init(&g_platform_irq, 0, 31, &platform_irq_ops, NULL);
}
```

#### 案例 6：正确使用示例与常见陷阱

```c
#include <mrX/kernel/mr_irq.h>

/* ✓ 正确：中断处理函数快速执行 */
static mr_irq_return_t good_isr_1(mr_uint32_t irq, void *owner) {
    /* 只做最必要的操作 */
    volatile uint32_t *status_reg = (volatile uint32_t *)0x40000000;
    uint32_t status = *status_reg;

    /* 清除中断标志 */
    *status_reg = status;

    /* 设置标志 */
    data_ready = 1;

    return MR_IRQ_HANDLED;
}

/* ✓ 正确：共享中断返回 MR_IRQ_NONE */
static mr_irq_return_t good_shared_isr(mr_uint32_t irq, void *owner) {
    device_t *dev = (device_t *)owner;

    /* 检查是否是本设备 */
    if (!(dev->regs->status & INT_STATUS)) {
        return MR_IRQ_NONE;  /* 不是本设备，继续传递 */
    }

    /* 处理中断 */
    dev->regs->status = 0;  /* 清除标志 */
    handle_interrupt(dev);

    return MR_IRQ_HANDLED;
}

/* ✗ 错误：中断处理函数中使用睡眠 */
static mr_irq_return_t bad_isr_1(mr_uint32_t irq, void *owner) {
    /* 错误：不能在中断上下文中睡眠 */
    // sleep(1);
    // mr_semaphore_wait(sem);

    return MR_IRQ_HANDLED;
}

/* ✗ 错误：中断处理函数耗时过长 */
static mr_irq_return_t bad_isr_2(mr_uint32_t irq, void *owner) {
    /* 错误：耗时操作会阻塞其他中断 */
    for (int i = 0; i < 1000000; i++) {
        /* 长时间计算 */
    }

    return MR_IRQ_HANDLED;
}

/* ✓ 正确：使用 defer 机制处理耗时操作 */
static mr_irq_return_t good_isr_defer(mr_uint32_t irq, void *owner) {
    /* 快速操作 */
    read_hardware_registers();

    /* 返回 DEFERRED */
    return MR_IRQ_DEFERRED;
}

static void good_defer_handler(mr_uint32_t irq, void *owner) {
    /* 在进程上下文中执行耗时操作 */
    process_data();
}

/* ✓ 正确：清理时释放中断 */
void device_cleanup(device_t *dev) {
    /* 释放中断 */
    mr_irq_free(dev->irq_num, dev);

    /* 其他清理 */
    /* ... */
}

/* ✗ 错误：忘记释放中断 */
void bad_cleanup(device_t *dev) {
    /* 忘记释放中断，会导致内存泄漏 */
    // mr_irq_free(dev->irq_num, dev);
}

/* ✓ 正确：使能/禁用配对调用 */
void correct_enable_disable(void) {
    mr_irq_disable(10);
    /* 临界区 */
    critical_operation();
    mr_irq_enable(10);
}

/* ✗ 错误：使能/禁用不配对 */
void bad_enable_disable(void) {
    mr_irq_disable(10);
    mr_irq_disable(10);  /* depth: 1 -> 2 */
    /* 错误：只调用了一次 enable */
    mr_irq_enable(10);   /* depth: 2 -> 1，中断仍然禁用！ */
}
```

---

## 3. 设计思路

### 3.1 设计目标

irq 模块的设计目标：

1. **硬件抽象**：提供统一的中断管理接口，屏蔽硬件差异
2. **灵活配置**：支持多种触发类型和共享中断
3. **延迟处理**：可选的 defer 机制，将耗时操作推迟到进程上下文
4. **线程安全**：支持多核环境，保护共享数据
5. **嵌套控制**：支持嵌套的中断使能/禁用调用
6. **资源安全**：防止删除时有新的操作，防止执行期间内存被释放

### 3.2 中断描述符表设计

irq 模块使用全局中断描述符表管理所有中断：

```c
#define MR_CFG_IRQ_TABLE_SIZE (32)
static mr_irq_desc_t __irq_table[MR_CFG_IRQ_TABLE_SIZE];
```

**设计特点**：

- 数组索引对应中断号
- 快速查找：O(1) 时间复杂度
- 每个 `mr_irq_t` 管理一个连续的中断号范围

**irq 控制器与描述符的关系**：

```
mr_irq_t (istart=0, iend=15)
    |
    +---> __irq_table[0] (irq = mr_irq_t)
    +---> __irq_table[1] (irq = mr_irq_t)
    ...
    +---> __irq_table[15] (irq = mr_irq_t)
```

### 3.3 dying 标志的作用

当删除 `mr_irq_t` 时，设置 `dying` 标志：

```c
mr_err_t mr_irq_del(mr_irq_t *irq) {
    /* 标记为 dying */
    mr_atomic_store(&irq->dying, MR_TRUE);

    /* 删除对象 */
    mr_object_del((mr_object_t *)irq);
    return 0;
}
```

**作用**：防止新的操作

```c
static mr_err_t irq_request(mr_uint32_t irq, ...) {
    /* 检查是否正在删除 */
    if (mr_atomic_load(&desc->irq->dying)) {
        return -MR_ENOENT;
    }

    /* 继续请求 */
    /* ... */
}
```

### 3.4 depth 嵌套计数器

**为什么需要 depth？**

支持嵌套的中断使能/禁用调用：

```c
void function_a(void) {
    mr_irq_disable(10);  /* depth: 1 -> 2 */
    /* 临界区 */
    function_b();        /* 也调用 mr_irq_disable */
    mr_irq_enable(10);   /* depth: 2 -> 1 */
}

void function_b(void) {
    mr_irq_disable(10);  /* depth: 2 -> 3 */
    /* 临界区 */
    mr_irq_enable(10);   /* depth: 3 -> 2 */
}
```

**实现原理**：

```c
static void irq_depth_enable(mr_uint32_t irq, mr_irq_desc_t *desc) {
    mr_atomic_t last;

    /* 减 1 */
    last = mr_atomic_fetch_sub(&desc->depth, 1);

    if (last != 1) {
        /* 不是从 1 变到 0，不需要真正使能 */
        return;
    }

    /* 从 1 变到 0，真正使能中断 */
    desc->irq->ops->unmask(irq, desc->irq->priv);
    desc->irq->ops->enable(irq, desc->irq->priv);
}
```

**初始值为什么是 1？**

- `1` 表示禁用状态（depth = 1）
- 第一次 `enable`：`1 -> 0`，真正使能
- 之后 `disable`：`0 -> 1`，禁用

### 3.5 pending 标志与内存安全

**问题场景**：

```c
/* 中断正在执行 */
void irq_handler(void) {
    /* ... */
    action->entry(irq, action->owner);  /* 正在执行 */
    /* ... */
}

/* 同时，另一个线程尝试释放 */
void free_interrupt(void) {
    mr_irq_free(irq, owner);  /* 会释放 action 内存 */
}
```

**解决方案**：pending 标志

```c
mr_irq_return_t mr_irq_handle(mr_uint32_t irq) {
    /* ... */
    for (action = desc->action; action; action = action->next) {
        /* 标记为 pending */
        action->pending = MR_TRUE;

        /* 保存 entry 和 owner */
        entry = action->entry;
        owner = action->owner;

        /* 释放锁 */
        unlock();

        /* 执行 handler */
        ret = entry(irq, owner);

        /* 重新获取锁 */
        lock();

        /* 清除 pending */
        action->pending = MR_FALSE;

        /* 检查是否正在被释放 */
        if (!action->owner) {
            /* owner 已被设为 NULL，说明正在释放 */
            /* 从链表移除并释放 action */
            free_action(action);
        }
    }
}
```

**释放时的处理**：

```c
mr_err_t mr_irq_free(mr_uint32_t irq, void *owner) {
    /* ... */
    for (a = &desc->action; *a; a = &(*a)->next) {
        if (owner != (*a)->owner) {
            continue;
        }

        /* 检查是否正在执行 */
        if ((*a)->pending) {
            /* 标记 owner 为 NULL，延迟释放 */
            (*a)->owner = MR_NULL;
            return 0;
        }

        /* 没有正在执行，直接释放 */
        action = *a;
        *a = action->next;
        free_action(action);
        break;
    }
}
```

### 3.6 共享中断的设计

**为什么共享中断的 flags 必须相同？**

因为只有一个物理中断线，硬件配置（触发类型）只能有一个：

```c
/* 第一个 handler */
mr_irq_request(15, handler_a, &dev_a, MR_IRQ_LEVEL_HIGH);

/* 第二个 handler 必须使用相同的 flags */
mr_irq_request(15, handler_b, &dev_b, MR_IRQ_LEVEL_HIGH | MR_IRQ_SHARED);
```

**检查逻辑**：

```c
static mr_err_t irq_request(mr_uint32_t irq, mr_uint16_t flags, ...) {
    if (!desc->action) {
        /* 第一个 handler，设置类型 */
        irq_type_set(irq, desc, flags & IRQ_FLAG_TYPE_MASK);
        desc->flags = flags;
    } else {
        /* 非第一个 handler，检查是否支持共享 */
        if (!(desc->flags & MR_IRQ_SHARED)) {
            return -MR_EBUSY;  /* 不支持共享 */
        }

        /* 检查 flags 是否一致 */
        if (desc->flags != flags) {
            return -MR_EINVAL;  /* flags 不一致 */
        }
    }
}
```

**共享中断的处理流程**：

```c
mr_irq_return_t mr_irq_handle(mr_uint32_t irq) {
    mr_irq_return_t ret;

    /* 遍历所有 handler */
    for (action = desc->action; action; action = action->next) {
        ret = action->entry(irq, action->owner);

        if (ret == MR_IRQ_HANDLED) {
            break;  /* 已处理，停止遍历 */
        }

        /* MR_IRQ_NONE：继续传递给下一个 */
    }
}
```

### 3.7 defer 延迟处理机制

**为什么需要 defer？**

中断上下文限制：

- 不能睡眠
- 不能阻塞
- 应快速执行

有些操作需要在这些限制之外进行。

**defer 的工作流程**：

```
硬件中断
    |
    v
ISR (中断上下文)
    |
    +---> entry() 返回 MR_IRQ_DEFERRED
    |
    v
加入 defer workqueue
    |
    v
进程上下文
    |
    +---> defer_entry() 执行耗时操作
```

**实现原理**：

```c
#if defined(MR_USE_IRQ_DEFER)
typedef struct mr_irq_action {
    /* ... */
    mr_work_t defer;        /* 工作项 */
    mr_ptr_t defer_entry;   /* 延迟处理函数 */
} mr_irq_action_t;
```

**中断处理时**：

```c
if (ret == MR_IRQ_DEFERRED) {
    if (action->defer_entry) {
        /* 加入 workqueue */
        mr_workqueue_work(&__irq_defer_queue, &action->defer);
    }
}
```

**workqueue 回调**：

```c
static void irq_action_defer_entry(mr_work_t *work, void *param) {
    mr_irq_action_t *action = (mr_irq_action_t *)param;

    /* 检查 action 是否已被释放 */
    if (!action->owner) {
        return;
    }

    /* 调用延迟处理函数 */
    action->defer_entry(action->irq, action->owner);
}
```

### 3.8 mask vs disable

**mask（屏蔽）**：

- 临时禁止中断触发
- 不改变中断的使能状态
- 可以通过 unmask 恢复

**disable（禁用）**：

- 彻底禁用中断
- 通常用于关闭中断源

**典型使用场景**：

```c
/* 中断处理开始 */
void irq_handler(void) {
    /* mask：防止中断嵌套 */
    mask(irq);

    /* 处理中断 */
    /* ... */

    /* unmask：恢复中断 */
    unmask(irq);
}

/* 设备关闭 */
void device_shutdown(void) {
    /* disable：彻底禁用中断 */
    disable(irq);
}
```

---

## 4. 参考资料

### 4.1 设计参考

- **Linux IRQ subsystem**：Linux 内核的中断子系统
- **Zephyr Interrupt Management**：Zephyr RTOS 的中断管理
- **FreeRTOS Interrupts**：FreeRTOS 的中断管理机制

### 4.2 延伸阅读

- **中断上下文 vs 进程上下文**：理解不同上下文的限制
- **中断嵌套**：高优先级中断抢占低优先级中断
- **中断亲和性**：多核系统中中断的 CPU 绑定
- **中断线程化**：将中断处理程序运行在内核线程中

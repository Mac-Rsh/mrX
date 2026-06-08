[**中文**](kernel_irq.md) | [**English**](kernel_irq_en.md)

---

# 中断管理

中断管理模块在 `MR_USE_IRQ` 启用时编译，用于注册中断控制器对象、维护中断描述符表、绑定中断处理函数，并在中断触发时分发处理函数。模块通过
`mr_irq_ops_t` 抽象平台或驱动提供的中断控制操作，通过全局自旋锁保护描述符表。

该模块支持共享中断和可选的延迟处理中断。延迟处理能力由 `MR_USE_IRQ_DEFER` 控制，延迟处理唤醒/挂起回调由
`MR_USE_IRQ_DEFER_HOOK` 控制。

## 核心数据结构和宏

### 中断触发标志

```c
#define MR_IRQ_EDGE_RISING  (0x01U)
#define MR_IRQ_EDGE_FALLING (0x02U)
#define MR_IRQ_EDGE_BOTH    (0x03U)
#define MR_IRQ_LEVEL_HIGH   (0x04U)
#define MR_IRQ_LEVEL_LOW    (0x08U)
#define MR_IRQ_SHARED       (0x10U)
```

低 4 位表示触发类型，内部使用 `IRQ_FLAG_TYPE_MASK` 提取；`MR_IRQ_SHARED` 表示允许多个 action 共享同一个 IRQ。共享中断要求后续注册的
flags 与已有 flags 完全一致。

### 中断返回值

```c
typedef enum mr_irq_return {
    MR_IRQ_NONE,
    MR_IRQ_HANDLED,
    MR_IRQ_DEFERRED,
} mr_irq_return_t;
```

`MR_IRQ_NONE` 表示当前处理函数未处理该中断，分发逻辑继续尝试后续 action。`MR_IRQ_HANDLED` 表示中断已处理，分发停止。
`MR_IRQ_DEFERRED` 表示需要延迟处理；未启用延迟处理或 action 未配置延迟入口时，当前实现会将其视为已处理。

### 中断控制操作

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

`mr_irq_ops_t` 由具体平台或中断控制器驱动实现。模块在首次 request 时设置触发类型和默认优先级，在 enable/disable 和 handle
路径中调用 enable、disable、mask、unmask。

### 中断对象

```c
typedef struct mr_irq {
    mr_object_t parent;
    mr_atomic_t dying;
    mr_uint32_t istart;
    mr_uint32_t iend;
    const mr_irq_ops_t *ops;
    mr_ptr_t priv;
} mr_irq_t;
```

`parent` 提供对象生命周期管理。`dying` 用于阻止删除中的对象继续被 request、free、enable、disable、priority_set 或 handle 使用。
`istart` 和 `iend` 描述该中断控制器管理的 IRQ 号范围。`ops` 和 `priv` 保存底层控制器操作和私有数据。

### action 和描述符

```c
typedef struct mr_irq_action {
    struct mr_irq_action *next;
    mr_uint32_t pending: 1;
    mr_uint32_t irq: 31;
    mr_ptr_t entry;
    mr_ptr_t owner;
#if defined(MR_USE_IRQ_DEFER)
    mr_work_t defer;
    mr_ptr_t defer_entry;
#endif
} mr_irq_action_t;

typedef struct mr_irq_desc {
    mr_irq_action_t *action;
    mr_uint16_t priority;
    mr_uint16_t flags;
    mr_atomic_t depth;
    mr_irq_t *irq;
} mr_irq_desc_t;
```

每个 IRQ 号对应一个描述符。`action` 是处理函数链表，`priority` 和 `flags` 保存当前配置，`depth` 是禁用深度，`irq` 指向所属中断对象。
`pending` 表示 action 正在处理；如果此时调用 `mr_irq_free()`，实现会先把 `owner` 置空，待处理路径返回后再释放 action。

全局描述符表大小由 `MR_CFG_IRQ_TABLE_SIZE` 控制，默认值为 32。

### 平台中断接口

```c
extern int mr_port_irq_disable(void);
extern void mr_port_irq_enable(int mask);
```

平台层接口位于 `include/mrX/port/mr_port_irq.h`，供自旋锁的 irqsave/irqrestore 路径使用。平台实现需要返回可用于恢复的 mask
或嵌套层级，并支持嵌套调用。

## 主要接口

### 初始化和创建中断对象

```c
mr_err_t mr_irq_init(mr_irq_t *irq, mr_uint32_t irq_start, mr_uint32_t irq_end,
                     const mr_irq_ops_t *ops, void *priv);
mr_irq_t *mr_irq_create(mr_uint32_t irq_start, mr_uint32_t irq_end,
                        const mr_irq_ops_t *ops, void *priv);
```

`mr_irq_init()` 初始化静态中断对象，`mr_irq_create()` 分配并初始化动态中断对象。二者都会把 `[irq_start, irq_end]`
范围插入全局描述符表，初始化每个描述符的禁用深度为 1，并保存 `ops` 和 `priv`。

如果范围内已有描述符绑定了中断对象，初始化返回 `-MR_EEXIST`；动态创建失败时返回 `MR_NULL`。参数范围、对象状态和 `ops`
为空由断言检查。

### 删除和引用管理

```c
mr_err_t mr_irq_del(mr_irq_t *irq);
mr_irq_t *mr_irq_get(mr_irq_t *irq);
mr_bool_t mr_irq_put(mr_irq_t *irq);
```

`mr_irq_del()` 将对象标记为 dying，并释放对象引用。最后一个引用释放时，模块会移除描述符表中的范围、释放所有 action、禁用并
mask 对应 IRQ。动态对象还会释放 `mr_irq_t` 内存。

`mr_irq_get()` 和 `mr_irq_put()` 是对象引用接口的薄封装。`mr_irq_handle()` 在分发期间会临时获取中断对象引用，避免处理过程中对象生命周期提前结束。

### 请求和释放中断

```c
mr_err_t mr_irq_request(mr_uint32_t irq, mr_irq_entry_t *entry,
                        void *owner, mr_uint16_t flags);
mr_err_t mr_irq_free(mr_uint32_t irq, void *owner);
```

`mr_irq_request()` 为指定 IRQ 添加 action。第一个 action 会设置触发类型、默认优先级 0 和描述符 flags，并将禁用深度从 1 降到
0，从而调用 `unmask()` 和 `enable()`。后续 action 只有在已有 flags 包含 `MR_IRQ_SHARED` 且新 flags 与旧 flags
完全一致时才允许加入。相同 owner 不能重复注册。

`mr_irq_free()` 按 owner 删除 action。删除最后一个 action 后会增加禁用深度，并在深度从 0 变为 1 时调用 `disable()` 和
`mask()`。如果 action 正在处理，函数先清空 owner 并返回，实际释放由 handle 路径完成。

### 启用、禁用和查询配置

```c
mr_err_t mr_irq_enable(mr_uint32_t irq);
mr_err_t mr_irq_disable(mr_uint32_t irq);
int mr_irq_type(mr_uint32_t irq);
mr_err_t mr_irq_priority_set(mr_uint32_t irq, mr_uint8_t priority);
int mr_irq_priority(mr_uint32_t irq);
```

`mr_irq_enable()` 和 `mr_irq_disable()` 操作描述符的禁用深度。只有深度从 1 降到 0 时才真正 unmask/enable，只有深度从 0 升到
1 时才真正 disable/mask。

`mr_irq_type()` 返回描述符 flags 中的触发类型。`mr_irq_priority_set()` 调用底层 `priority_set()` 并更新描述符优先级。
`mr_irq_priority()` 返回保存的优先级。不存在的 IRQ 返回 `-MR_ENOENT`。

### 分发中断

```c
mr_irq_return_t mr_irq_handle(mr_uint32_t irq);
```

`mr_irq_handle()` 在全局锁保护下查找描述符，获取中断对象引用，并先调用底层 `mask()`。随后按 action
链表顺序调用处理函数；调用处理函数前会释放全局锁，处理函数返回后重新加锁。

返回 `MR_IRQ_HANDLED` 时停止分发。返回 `MR_IRQ_DEFERRED` 时，如果启用了 `MR_USE_IRQ_DEFER` 且该 action 有延迟入口，则把延迟
work 加入内部 workqueue；否则把结果改为 `MR_IRQ_HANDLED`。分发结束后，如果禁用深度为 0，则调用 `unmask()`。

### 延迟处理中断

```c
mr_err_t mr_irq_defer_request(mr_uint32_t irq, mr_irq_entry_t *entry,
                              void *owner, mr_uint16_t flags,
                              mr_irq_entry_t *defer_entry);
void mr_irq_defer_execute(void);
mr_err_t mr_irq_defer_hook_set(const mr_irq_defer_hook_t *hook);
```

这些接口只在对应配置启用时可用。`mr_irq_defer_request()` 注册普通入口和延迟入口。普通入口返回 `MR_IRQ_DEFERRED` 后，延迟入口由内部
workqueue 执行。`mr_irq_defer_execute()` 执行该 workqueue，注释说明用户通常不需要直接调用。`mr_irq_defer_hook_set()` 设置
workqueue 的 suspend/wakeup 钩子，测试中使用该钩子验证延迟队列状态变化。

`mr_kernel_irq_init()` 在内核初始化阶段初始化延迟 workqueue，并在启用 hook 时设置 workqueue hook。

## 使用示例

```c
static void hw_priority_set(mr_uint32_t irq, mr_uint8_t priority, void *priv) {}
static void hw_type_set(mr_uint32_t irq, mr_uint8_t type, void *priv) {}
static void hw_enable(mr_uint32_t irq, void *priv) {}
static void hw_unmask(mr_uint32_t irq, void *priv) {}
static void hw_disable(mr_uint32_t irq, void *priv) {}
static void hw_mask(mr_uint32_t irq, void *priv) {}

static const mr_irq_ops_t ops = {
    .priority_set = hw_priority_set,
    .type_set = hw_type_set,
    .enable = hw_enable,
    .unmask = hw_unmask,
    .disable = hw_disable,
    .mask = hw_mask,
};

static mr_irq_return_t button_irq(mr_uint32_t irq, void *owner) {
    return MR_IRQ_HANDLED;
}

void example(void) {
    static mr_irq_t irqctl;

    mr_irq_init(&irqctl, 0, 3, &ops, MR_NULL);
    mr_irq_request(0, button_irq, &irqctl, MR_IRQ_EDGE_RISING);

    /* 平台中断入口中调用。 */
    mr_irq_handle(0);

    mr_irq_free(0, &irqctl);
    mr_irq_del(&irqctl);
}
```

测试 `test/kernel/irq.c` 覆盖了初始化/删除、动态创建、request/free、优先级设置和查询、触发类型查询、enable/disable、handle、共享中断、错误路径，以及延迟处理中断的
request、free-before-execute 和 del-before-execute 场景。

## 注意事项

- 该模块仅在 `MR_USE_IRQ` 定义时可用。
- `mr_irq_init()` 和 `mr_irq_create()` 要求 IRQ 范围不重叠，且范围必须小于 `MR_CFG_IRQ_TABLE_SIZE`。
- request 时 `owner` 既用于释放匹配，也用于重复注册检测；同一 IRQ 下相同 owner 不能重复注册。
- 共享中断必须从第一个 action 开始就设置 `MR_IRQ_SHARED`，否则后续共享注册会返回忙。
- 中断处理函数在全局锁释放后执行，但 action 会标记 pending；处理函数中调用 `mr_irq_free()` 是测试覆盖的场景。
- 删除中断对象会清理其 IRQ 范围内所有 action，待执行的延迟 action 会因 owner 被清空而跳过。
- 底层 `mr_irq_ops_t` 的所有函数指针在当前实现中被直接调用，驱动需要提供完整实现。

[**中文**](kernel_wait.md) | [**English**](kernel_wait_en.md)

---

# 等待事件

Wait 模块用于把“可等待对象”的事件状态和“等待者”的回调连接起来。`mr_waitable_t` 表示能够产生事件的对象，`mr_waiter_t`
表示等待指定事件并在触发时执行回调的对象。模块支持输入、输出、错误三类事件，以及边沿触发和电平触发两种等待方式。

Wait 对象使用 `mr_object_t` 管理生命周期，并通过全局自旋锁保护等待链表的插入、删除和遍历。

## 核心数据结构和宏

### 事件标志

```c
#define MR_WAIT_IN   (1ULL << 60)
#define MR_WAIT_OUT  (1ULL << 61)
#define MR_WAIT_ERR  (1ULL << 62)
#define MR_WAIT_EDGE (1ULL << 63)
#define MR_WAIT_EVENT_MASK (~(MR_WAIT_IN | MR_WAIT_OUT | MR_WAIT_ERR | MR_WAIT_EDGE))
```

`MR_WAIT_IN`、`MR_WAIT_OUT` 和 `MR_WAIT_ERR` 表示事件类型。`MR_WAIT_EDGE` 表示等待者使用边沿触发；未设置该标志时为电平触发。低位可携带模块自定义事件数据，
`MR_WAIT_EVENT_MASK` 可用于取出这部分数据。

### 可等待对象

```c
typedef struct mr_waitable {
    mr_object_t parent;
    mr_uint64_t last;
    mr_atomic_t gen;
    mr_list_t list;
    mr_ptr_t check;
    mr_ptr_t param;
} mr_waitable_t;
```

字段含义如下：

| 字段       | 含义                                  |
|----------|-------------------------------------|
| `parent` | 对象控制块。                              |
| `last`   | 上一次处理的事件，用于计算边沿事件。                  |
| `gen`    | 检查代数，用于丢弃并发嵌套检查中的旧结果。               |
| `list`   | 等待该对象的 `mr_waiter_t` 链表头。           |
| `check`  | 事件检查函数，类型为 `mr_waitable_check_t *`。 |
| `param`  | 传给检查函数的参数。                          |

```c
typedef mr_uint64_t(mr_waitable_check_t)(mr_waitable_t *waitable, void *param);
```

检查函数返回当前事件集合。若检查函数为空，内部检查结果为 0。

### 等待者

```c
typedef struct mr_waiter {
    mr_object_t parent;
    mr_uint64_t event;
    mr_list_t list;
    mr_ptr_t entry;
    mr_ptr_t param;
} mr_waiter_t;
```

字段含义如下：

| 字段       | 含义                              |
|----------|---------------------------------|
| `parent` | 对象控制块。                          |
| `event`  | 等待的事件类型和触发方式。                   |
| `list`   | 等待链表节点；空链表表示当前未等待。              |
| `entry`  | 触发回调，类型为 `mr_waiter_entry_t *`。 |
| `param`  | 传给触发回调的参数。                      |

```c
typedef void(mr_waiter_entry_t)(mr_waiter_t *waiter, void *param,
                                mr_uint64_t event);
```

## 模块接口

### 可等待对象初始化和创建

```c
mr_err_t mr_waitable_init(mr_waitable_t *able,
                          mr_waitable_check_t *check, void *param);
mr_waitable_t *mr_waitable_create(mr_waitable_check_t *check, void *param);
```

`mr_waitable_init()` 初始化调用者提供的静态对象。`mr_waitable_create()` 从堆中创建动态对象。`check` 可以为 `MR_NULL`，但此时
`mr_waitable_complete()` 不会产生事件。

### 可等待对象删除和引用管理

```c
mr_err_t mr_waitable_del(mr_waitable_t *able);
mr_waitable_t *mr_waitable_get(mr_waitable_t *able);
mr_bool_t mr_waitable_put(mr_waitable_t *able);
```

`mr_waitable_del()` 会取消该对象上的所有等待者，再释放对象引用。`mr_waitable_get()` 和 `mr_waitable_put()` 是引用计数封装。

### 完成、主动触发和取消等待

```c
mr_err_t mr_waitable_complete(mr_waitable_t *able);
mr_err_t mr_waitable_raise(mr_waitable_t *able, mr_uint64_t event);
mr_err_t mr_waitable_cancel_all(mr_waitable_t *able);
```

`mr_waitable_complete()` 在存在等待者时调用 `check` 获取当前事件，并触发匹配的等待者。若等待者链表为空，该函数直接返回，不调用
`check`。

`mr_waitable_raise()` 不调用 `check`，而是直接用传入的 `event` 触发等待者。

`mr_waitable_cancel_all()` 从等待链表中移除所有等待者，不调用等待者回调。

### 等待者初始化和创建

```c
mr_err_t mr_waiter_init(mr_waiter_t *wait, mr_waiter_entry_t *entry,
                        void *param, mr_uint64_t event);
mr_waiter_t *mr_waiter_create(mr_waiter_entry_t *entry, void *param,
                              mr_uint64_t event);
```

`mr_waiter_init()` 初始化静态等待者。`mr_waiter_create()` 创建动态等待者。两者都要求 `entry != MR_NULL`。

### 等待、取消和事件配置

```c
mr_err_t mr_waiter_wait(mr_waiter_t *wait, mr_waitable_t *able);
mr_err_t mr_waiter_cancel(mr_waiter_t *wait);
mr_err_t mr_waiter_event_set(mr_waiter_t *wait, mr_uint64_t event);
mr_uint64_t mr_waiter_event(mr_waiter_t *wait);
#define MR_WAITER_IS_WAITING(_waiter) (!mr_list_is_empty(&(_waiter)->list))
```

`mr_waiter_wait()` 将等待者加入可等待对象的等待链表。若等待者已经在等待中，返回 `-MR_EBUSY`。

电平触发等待者加入链表后会立即执行一次 `check`，若当前事件匹配，会立即调用等待者回调。边沿触发等待者加入时不检查历史状态，只等待后续事件变化或主动触发。

`mr_waiter_cancel()` 从等待链表移除等待者。`mr_waiter_event_set()` 会先取消当前等待，再更新等待事件。`mr_waiter_event()`
返回当前等待事件。

## 使用示例

```c
#include <mrX/kernel/mr_wait.h>

static mr_uint64_t sample_check(mr_waitable_t *able, void *param)
{
    (void)able;
    return (*(int *)param != 0) ? MR_WAIT_IN : 0;
}

static void sample_waiter_entry(mr_waiter_t *waiter, void *param,
                                mr_uint64_t event)
{
    (void)waiter;
    (void)param;
    (void)event;
}

void sample_wait_usage(void)
{
    int ready = 0;
    mr_waitable_t able;
    mr_waiter_t waiter;

    mr_waitable_init(&able, sample_check, &ready);
    mr_waiter_init(&waiter, sample_waiter_entry, MR_NULL, MR_WAIT_IN);

    mr_waiter_wait(&waiter, &able);
    ready = 1;
    mr_waitable_complete(&able);

    mr_waiter_del(&waiter);
    mr_waitable_del(&able);
}
```

该示例中，等待者等待 `MR_WAIT_IN`。当 `ready` 被置为非零后，`mr_waitable_complete()` 通过 `sample_check()` 获取事件并触发回调。

## 注意事项

- Wait 模块只有在 `MR_USE_WAIT` 启用时可用。
- 等待者回调在内部等待锁释放后调用，但回调中修改等待关系仍需考虑对象生命周期。
- 边沿事件通过 `event & ~able->last` 计算；电平事件直接使用当前事件。
- `mr_waitable_complete()` 在没有等待者时不会调用检查函数。
- `mr_waiter_event_set()` 会取消当前等待关系，调用者需要重新等待。
- 删除可等待对象不会删除等待者对象本身，只会把等待者从等待链表移除。

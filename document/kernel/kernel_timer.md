[**中文**](kernel_timer.md) | [**English**](kernel_timer_en.md)

---

# 定时器

Timer 模块用于在内核时钟 tick 到达指定时间后执行回调。定时器对象以内嵌 `mr_object_t` 管理生命周期，以链表节点加入全局定时列表，并由
`mr_kernel_timer_init()` 注册到时钟钩子中进行超时检查。

定时器支持一次性触发和周期触发。一次性定时器触发后不会自动重新加入定时列表；周期定时器触发回调后会按配置 tick 重新启动。

## 核心数据结构和宏

### 定时器控制块

```c
typedef struct mr_timer {
    mr_object_t parent;
    mr_tick_t itick;
    mr_tick_t ttick;
    mr_list_t list;
    mr_ptr_t entry;
    mr_ptr_t param;
} mr_timer_t;
```

字段含义如下：

| 字段       | 含义                                 |
|----------|------------------------------------|
| `parent` | 对象控制块，用于引用计数和静态/动态释放。              |
| `itick`  | 初始定时时长。最高位可携带 `MR_TIMER_PERIODIC`。 |
| `ttick`  | 绝对超时 tick，由启动时的当前 tick 加定时时长得到。    |
| `list`   | 全局定时链表中的节点；空链表表示未处于计时状态。           |
| `entry`  | 超时回调，类型为 `mr_timer_entry_t *`。     |
| `param`  | 传给超时回调的用户参数。                       |

### 周期标志

```c
#define MR_TIMER_PERIODIC (1U << 31)
```

`MR_TIMER_PERIODIC` 用于标记周期定时器。模块在计算超时时间时会使用 `itick & ~MR_TIMER_PERIODIC` 得到实际 tick 数。

### 初始化宏和回调类型

```c
typedef void(mr_timer_entry_t)(mr_timer_t *timer, void *param);

#define MR_TIMER_INIT(_timer, _entry, _param, _tick) \
    MR_TIMER_DERIVE_INIT((_timer), MR_CLAZZES_STATIK(timer), (_entry), \
                         (_param), (_tick))
```

`MR_TIMER_DERIVE_INIT()` 可用于派生定时器类，`MR_TIMER_INIT()` 使用定时器静态类初始化普通定时器。

## 模块接口

### 初始化和创建

```c
mr_err_t mr_timer_init(mr_timer_t *timer, mr_timer_entry_t *entry,
                       void *param, mr_tick_t tick);
mr_timer_t *mr_timer_create(mr_timer_entry_t *entry, void *param,
                            mr_tick_t tick);
```

`mr_timer_init()` 初始化调用者提供的静态定时器对象。`mr_timer_create()` 从堆中分配 `mr_timer_t` 并初始化为动态对象。两者都要求
`entry != MR_NULL` 且 `tick > 0`。

初始化成功后，定时器对象引用计数为 1，链表节点为空，尚未开始计时。

### 删除和引用管理

```c
mr_err_t mr_timer_del(mr_timer_t *timer);
mr_timer_t *mr_timer_get(mr_timer_t *timer);
mr_bool_t mr_timer_put(mr_timer_t *timer);
```

`mr_timer_del()` 先从定时列表移除定时器，再释放对象引用。动态创建的定时器在最后一个引用释放时会释放对象内存；静态初始化的定时器只结束对象生命周期。

`mr_timer_get()` 和 `mr_timer_put()` 是对象引用计数接口的定时器封装。

### 启动、停止和状态判断

```c
mr_err_t mr_timer_start(mr_timer_t *timer);
mr_err_t mr_timer_stop(mr_timer_t *timer);
#define MR_TIMER_IS_TIMING(_timer) (!mr_list_is_empty(&(_timer)->list))
```

`mr_timer_start()` 按当前时钟 tick 和 `itick` 计算超时时间，并将定时器插入全局定时链表。若定时器已经在链表中，启动操作会通过链表移动语义重新定位，相当于重启。

`mr_timer_stop()` 从定时链表中移除定时器。`MR_TIMER_IS_TIMING()` 通过链表是否为空判断定时器是否正在计时。

### 设置和读取 tick

```c
mr_err_t mr_timer_tick_set(mr_timer_t *timer, mr_tick_t tick);
mr_tick_t mr_timer_tick(mr_timer_t *timer);
```

`mr_timer_tick_set()` 会先从定时链表移除定时器，再更新 `itick`。如果定时器正在运行，调用该接口会停止当前计时，调用者需要重新
`mr_timer_start()`。

`mr_timer_tick()` 返回当前 `itick`，包括可能存在的 `MR_TIMER_PERIODIC` 标志。

### 内核定时器初始化

```c
void mr_kernel_timer_init(void);
```

`mr_kernel_timer_init()` 将内部超时检查函数注册到时钟钩子。时钟推进后，超时检查会取出到期定时器，在释放定时器锁后调用回调；周期定时器回调完成后会重新插入定时链表。

## 使用示例

```c
#include <mrX/kernel/mr_timer.h>

static void sample_timer_entry(mr_timer_t *timer, void *param)
{
    (void)timer;
    (void)param;
}

void sample_timer_usage(void)
{
    mr_timer_t timer;

    if (mr_timer_init(&timer, sample_timer_entry, MR_NULL,
                      10 | MR_TIMER_PERIODIC) != 0) {
        return;
    }

    mr_timer_start(&timer);
    mr_timer_stop(&timer);
    mr_timer_del(&timer);
}
```

该示例创建一个静态周期定时器。定时器启动后将在每 10 个 tick 触发一次回调；停止后删除对象。

## 注意事项

- Timer 模块只有在 `MR_USE_TIMER` 启用时可用。
- `tick` 必须大于 0；周期标志占用最高位，实际定时时长由 `tick & ~MR_TIMER_PERIODIC` 得到。
- 超时回调在内部定时器锁释放后执行，回调中可以操作定时器接口，但仍需避免对象生命周期竞争。
- `mr_timer_tick_set()` 会停止正在运行的定时器。
- 删除定时器会先停止计时，再释放对象引用；动态定时器删除后不能继续访问原指针。

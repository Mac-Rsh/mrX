# 等待队列（Wait）

基于“事件源（`mr_waitable_t`）+ 等待者（`mr_waiter_t`）”的同步回调事件分发机制，支持
32 位事件位图匹配及电平/边沿触发。

| 项目   | 说明                                            |
|--------|-------------------------------------------------|
| 功能宏 | `MR_USE_WAIT`                                   |
| 头文件 | `<mrX/kernel/mr_wait.h>`                        |
| 依赖   | [对象系统](object.md)                           |
| 测试   | `test/kernel/wait.c`、`test/kernel/wait_deep.c` |

## 配置

| 配置项        | 类型 | 默认值 | 说明             |
|---------------|------|--------|------------------|
| `MR_USE_WAIT` | bool | y      | 启用等待队列模块 |

## 快速上手

```c
#include <mrX/kernel/mr_wait.h>

static void on_event(mr_waiter_t *waiter, void *param,
                     mr_wait_event_t event, const void *data) {
    int *cnt = (int *)param;
    (*cnt)++;
}

void wait_demo(void) {
    static mr_waitable_t able;
    static mr_waiter_t waiter;
    int count = 0;
    const int payload = 0x1234;

    mr_waitable_init(&able);
    mr_waiter_init(&waiter, on_event, &count, MR_WAIT_IN);
    mr_waiter_wait(&waiter, &able);

    /* 同步触发：on_event 在当前上下文执行 */
    mr_waitable_raise(&able, MR_WAIT_IN, &payload);

    mr_waiter_del(&waiter);
    mr_waitable_del(&able);
}
```

## 核心机制与约束

- **同步分发与持续订阅**：`mr_waitable_raise()` 在调用者当前上下文（线程或
  ISR）中同步执行所有匹配的回调；它不会阻塞等待，也不会切换线程。waiter
  触发后仍保留在等待队列中，直到显式调用 `mr_waiter_cancel()` / `mr_waiter_del()`
  或 `mr_waitable_cancel_all()`。
- **事件位图与电平/边沿触发**：`mr_wait_event_t` 为 32 位掩码：`MR_WAIT_IN`
  (`1U<<28`)、`MR_WAIT_OUT` (`1U<<29`)、`MR_WAIT_ERR` (`1U<<30`)、`MR_WAIT_EDGE`
  (`1U<<31`)，低 28 位（`MR_WAIT_EVENT_MASK`）供用户自定义。`raise` 入口会先清除
  `MR_WAIT_EDGE`：
    - **电平触发**（未设 `MR_WAIT_EDGE`）：只要 `waiter->event & event != 0` 即触发。
    - **边沿触发**（设置 `MR_WAIT_EDGE`）：仅当 `event & (~waiter->last)` 与
      `waiter->event` 有交集时触发，每次 `raise` 均更新 `waiter->last = event`
      （发起全 0 事件可复位边沿基准，`mr_waiter_wait()` 也会清零 `last`）。
    - 回调收到的 `event` 参数是计算后的事件集合（电平模式为本次完整事件，边沿模式为
      `event & ~last`），而非与 waiter 关注掩码相与后的子集。
- **回调内安全操作与载荷语义**：分发期间通过游标链表支持回调内取消/删除任意
  waiter、重新 `wait` 或递归 `raise`；但删除正在分发中的 `waitable`
  前需由调用方保证其内存不被提前释放。`data` 指针由所有回调共享，应视为只读且仅在
  `mr_waitable_raise()` 返回前有效。
- **生命周期**：`mr_waiter_wait()` 仅建立链表关系， **不会自动增加** `waitable` 或
  `waiter` 的对象引用计数，调用方需保证两者在挂接期间存活。对已挂接的 waiter 再次调用
  `mr_waiter_wait()` 返回 `-MR_EBUSY`；调用 `mr_waiter_event_set()`
  会修改关注位并自动取消当前等待。

## API 速查

### 事件源（`mr_waitable_t`）

| API                                                                  | 说明                                      |
|----------------------------------------------------------------------|-------------------------------------------|
| `MR_WAITABLE_INIT(_able)` / `MR_WAITABLE_DERIVE_INIT(_able, _clazz)` | 静态初始化 waitable                       |
| `mr_err_t mr_waitable_init(mr_waitable_t *able)`                     | 运行时初始化静态 waitable                 |
| `mr_waitable_t *mr_waitable_create(void)`                            | 堆上动态创建 waitable，失败返回 `MR_NULL` |
| `mr_err_t mr_waitable_del(mr_waitable_t *able)`                      | 取消全部 waiter 并归还对象引用            |
| `mr_err_t mr_waitable_raise(able, event, data)`                      | 分发事件，同步执行匹配回调                |
| `mr_err_t mr_waitable_cancel_all(mr_waitable_t *able)`               | 取消该事件源上全部 waiter                 |
| `mr_waitable_get(able)` / `mr_waitable_put(able)`                    | 增减对象引用计数                          |

### 等待者（`mr_waiter_t`）

| API                                                               | 说明                                                   |
|-------------------------------------------------------------------|--------------------------------------------------------|
| `MR_WAITER_INIT(_wait, _entry, _param, _event)`                   | 静态初始化 waiter（另有 `MR_WAITER_DERIVE_INIT`）      |
| `mr_err_t mr_waiter_init(wait, entry, param, event)`              | 运行时初始化静态 waiter（`entry` 不可为 `MR_NULL`）    |
| `mr_waiter_t *mr_waiter_create(entry, param, event)`              | 堆上动态创建 waiter，失败返回 `MR_NULL`                |
| `mr_err_t mr_waiter_del(mr_waiter_t *wait)`                       | 从队列摘除并归还对象引用                               |
| `mr_err_t mr_waiter_wait(mr_waiter_t *wait, mr_waitable_t *able)` | 挂接到 `able` 并清零边沿历史；已在等待返回 `-MR_EBUSY` |
| `mr_err_t mr_waiter_cancel(mr_waiter_t *wait)`                    | 从 `able` 摘除（空闲时安全无操作）                     |
| `mr_err_t mr_waiter_event_set(wait, event)`                       | 修改关注事件位并取消当前等待                           |
| `mr_wait_event_t mr_waiter_event(mr_waiter_t *wait)`              | 读取当前关注事件位                                     |
| `MR_WAITER_IS_WAITING(_waiter)`                                   | 判断是否已挂接在等待队列中                             |
| `mr_waiter_get(wait)` / `mr_waiter_put(wait)`                     | 增减对象引用计数                                       |

## 内部实现

`kernel/wait.c` 使用全局自旋锁 `__wait_lock`（`mr_spinlock_lock_irqsave`
）保护队列操作，在执行用户回调前释放锁，回调返回后重新加锁。`waitable_handle()`
在栈上分配 `wait_cursor_t` 节点挂入 `able->cursors` 链表；当任意路径调用
`waiter_remove_locked()` 移除节点时，会同步将所有指向被移除节点的游标
`cursor->next` 推进到下一节点，从而在不复制队列的前提下支持并发摘除与递归
`raise`。

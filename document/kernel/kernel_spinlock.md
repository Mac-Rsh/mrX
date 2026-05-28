# 自旋锁

自旋锁模块提供基于原子变量的忙等互斥原语，并提供带中断保存和恢复的锁操作。该模块用于保护短临界区，尤其适合内核全局表、对象状态等不能在临界区内睡眠的路径。

自旋锁本身只保存锁状态。带中断版本通过 `mr_port_irq_disable()` 和 `mr_port_irq_enable()` 调用平台层接口，因此平台需要提供可嵌套的中断屏蔽语义。

## 核心数据结构和宏

### 锁类型

```c
typedef mr_atomic_t mr_spinlock_t;
```

`mr_spinlock_t` 是原子变量。值为 0 表示未加锁，非 0 表示已加锁。当前实现加锁时写入 1，解锁时写回 0。

### 静态初始化

```c
#define MR_SPINLOCK_INIT() MR_ATOMIC_INIT(0)
```

`MR_SPINLOCK_INIT()` 用于静态定义未加锁状态的自旋锁。

## 主要接口

### 初始化自旋锁

```c
void mr_spinlock_init(mr_spinlock_t *lock);
```

`mr_spinlock_init()` 将锁状态设置为 0。该函数不会检查锁当前是否已被持有，调用者必须保证不会在持锁状态下重新初始化。

### 加锁和解锁

```c
void mr_spinlock_lock(mr_spinlock_t *lock);
void mr_spinlock_unlock(mr_spinlock_t *lock);
```

`mr_spinlock_lock()` 在循环中尝试把锁状态从 0 改为 1，直到比较交换成功。该函数不会让出 CPU，也没有超时逻辑。

`mr_spinlock_unlock()` 将锁状态写回 0。当前实现不检查调用者是否持有该锁。

### 关中断加锁和恢复中断解锁

```c
int mr_spinlock_lock_irqsave(mr_spinlock_t *lock);
void mr_spinlock_unlock_irqrestore(mr_spinlock_t *lock, int mask);
```

`mr_spinlock_lock_irqsave()` 先调用 `mr_port_irq_disable()` 获取当前中断 mask 或嵌套层级，再获取自旋锁，并返回该 mask。

`mr_spinlock_unlock_irqrestore()` 先释放自旋锁，再调用 `mr_port_irq_enable(mask)` 恢复中断状态。`mask` 必须来自配对的 `mr_spinlock_lock_irqsave()` 调用。

### 查询锁状态

```c
mr_bool_t mr_spinlock_is_locked(mr_spinlock_t *lock);
```

`mr_spinlock_is_locked()` 读取原子锁值，非 0 返回 `MR_TRUE`，否则返回 `MR_FALSE`。该接口只用于观察瞬时状态，不能作为无锁同步条件。

## 使用示例

```c
static mr_spinlock_t list_lock = MR_SPINLOCK_INIT();

void update_shared_state(void) {
    int mask;

    mask = mr_spinlock_lock_irqsave(&list_lock);
    /* 修改共享状态，临界区内不能执行可能睡眠的操作。 */
    mr_spinlock_unlock_irqrestore(&list_lock, mask);
}
```

测试 `test/kernel/spinlock.c` 覆盖了静态初始化、普通加锁解锁、带中断保存和恢复的加锁解锁，以及 `mr_spinlock_is_locked()` 的状态变化。

## 注意事项

- `mr_spinlock_lock()` 是忙等循环，临界区应保持很短。
- 当前实现不可重入，同一执行上下文重复加同一把锁会自旋等待自身释放。
- 带中断版本依赖 `mr_port_irq_disable()` 和 `mr_port_irq_enable()` 的平台实现，并要求平台支持嵌套恢复。
- 解锁接口不会校验所有权，错误解锁会直接把锁状态置 0。
- `mr_spinlock_is_locked()` 的返回值可能在读取后立即失效，不能替代实际加锁。

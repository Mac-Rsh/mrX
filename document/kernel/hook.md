# 钩子（Hook）

提供固定容量、带锁追加的通用指针表，常用于实现“单一事件点挂接多个回调”的分发链（如[时钟](clock.md)
的每 tick 钩子）。

| 项目   | 说明                                                           |
|--------|----------------------------------------------------------------|
| 功能宏 | 无（始终编译）                                                 |
| 头文件 | `<mrX/kernel/mr_hook.h>`                                       |
| 依赖   | [内核基础工具（Foundation）](foundation.md)（`mr_spinlock.h`） |
| 测试   | `test/kernel/hook.c`、`test/kernel/hook_deep.c`                |

## 快速上手

```c
#include <mrX/kernel/mr_hook.h>

typedef void(my_hook_fn_t)(void);

static int sum;
static void add_one(void) { sum += 1; }
static void add_two(void) { sum += 2; }

void hook_demo(void) {
    mr_ptr_t buf[4];
    mr_hook_t hook = MR_HOOK_INIT(buf, 4);
    my_hook_fn_t *entry;
    mr_uint32_t i;

    mr_hook_add(&hook, (mr_ptr_t)add_one);
    mr_hook_add(&hook, (mr_ptr_t)add_two);

    MR_HOOK_EACH(i, entry, my_hook_fn_t, &hook) {
        entry(); /* 按注册顺序执行，循环结束后 sum == 3 */
    }
}
```

## 核心机制与约束

- **零分配与固定容量**：存储区 `mr_ptr_t` 数组由调用者提供，容器容量在初始化时固定（允许容量为
  `0` 且 `entry == MR_NULL` 的占位构造，此时所有追加均返回 `-MR_ENOMEM`）。
- **只增不删、不去重**：模块仅提供 `mr_hook_add()`
  追加到表尾，不提供删除接口，也不对重复指针去重；若需运行时禁用某条回调，应在回调内部自行维护使能标志。
- **并发与遍历限制**：`mr_hook_add()` 内部通过全局自旋锁
  `mr_spinlock_lock_irqsave()` 串行化所有实例的追加操作，但 `MR_HOOK_EACH()` 遍历时
  **不加锁**。因此应在初始化阶段完成注册，避免与运行期遍历并发。
- **参数校验**：`mr_hook_add()` 会通过 `MR_ASSERT` 校验 `hook` 非空、`entry` 非空以及（
  `size == 0` 或数组非空）。

## API 速查

| API / 宏                                                                | 说明                                                            |
|-------------------------------------------------------------------------|-----------------------------------------------------------------|
| `MR_HOOK_INIT(_entry, _size)`                                           | 静态初始化 `mr_hook_t` 容器                                     |
| `void mr_hook_init(mr_hook_t *hook, mr_ptr_t *entry, mr_uint16_t size)` | 运行期初始化容器，绑定容量为 `size` 的 `mr_ptr_t` 数组          |
| `mr_err_t mr_hook_add(mr_hook_t *hook, mr_ptr_t entry)`                 | 锁内追加条目到表尾；成功返回 `0`，表满返回 `-MR_ENOMEM`         |
| `MR_HOOK_EACH(_i, _entry, _type, _hook)`                                | 按注册顺序展开 `for` 循环，将各条目按 `_type *` 赋值给 `_entry` |

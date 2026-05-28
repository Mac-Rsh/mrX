# 钩子管理

钩子模块提供固定容量的回调入口表管理能力。模块负责初始化钩子表、向表中追加入口，并提供遍历宏供调用者按注册顺序执行回调。它适用于系统 tick、工作队列、驱动事件等需要维护一组回调函数的场景。

本文对应 `include/mrX/kernel/mr_hook.h`、`kernel/hook.c` 和 `test/kernel/hook.c`。

## 模块用途

`mr_hook` 模块只管理入口指针数组，不定义回调函数的具体签名，也不主动调用回调。调用者负责准备存储数组、指定容量、将函数或对象入口转换为 `mr_ptr_t` 后注册，并在遍历时转换回实际类型。

`mr_hook_add()` 使用模块内部的全局自旋锁保护追加操作，避免多个执行上下文同时修改 `next` 和入口数组。遍历宏本身不加锁，是否允许注册和遍历并发由具体使用模块决定。

## 核心数据结构和宏

### 钩子控制块

```c
typedef struct mr_hook {
    mr_ptr_t *entry;
    mr_uint16_t size;
    mr_uint16_t next;
} mr_hook_t;
```

字段含义如下：

| 字段 | 含义 |
|-----|------|
| `entry` | 入口数组首地址，数组元素类型为 `mr_ptr_t`。 |
| `size` | 入口数组容量，单位是入口数量。 |
| `next` | 下一个可写入位置，同时表示已注册入口数量。 |

### 静态初始化宏

```c
#define MR_HOOK_INIT(_entry, _size)                                            \
    {.entry = (_entry), .size = (_size), .next = 0}
```

`MR_HOOK_INIT()` 用于静态初始化 `mr_hook_t`。初始化后，`next` 为 0，表示尚未注册任何入口。

### 遍历宏

```c
#define MR_HOOK_EACH(_i, _entry, _type, _hook)                                 \
    for ((_i) = 0, (_entry) = (_type *)(_hook)->entry[(_i)];                   \
         (_i) < (_hook)->next;                                                 \
         (_i)++, (_entry) = (_type *)(_hook)->entry[(_i)])
```

`MR_HOOK_EACH()` 按索引从 0 遍历到 `next - 1`，并将每个 `mr_ptr_t` 入口转换为 `_type *`。如果入口本身是函数指针，调用者需要传入匹配的函数类型。

## 模块接口

### 初始化钩子

```c
MR_INLINE void mr_hook_init(mr_hook_t *hook, mr_ptr_t *entry,
                            mr_uint16_t size);
```

`mr_hook_init()` 初始化钩子控制块，设置入口数组、容量和 `next`。该函数不返回错误码，也不做空指针检查；调用者必须传入有效的 `hook`，并保证 `entry` 与 `size` 的组合有效。

测试用例覆盖了 `MR_HOOK_INIT()` 和 `mr_hook_init()` 初始化后 `size`、`next` 字段的结果。

### 添加入口

```c
mr_err_t mr_hook_add(mr_hook_t *hook, mr_ptr_t entry);
```

`mr_hook_add()` 将入口追加到钩子表末尾。函数会在内部获取全局自旋锁并保存中断状态，写入完成后释放锁并恢复中断状态。

返回值如下：

| 返回值 | 含义 |
|-------|------|
| `0` | 添加成功，`next` 增加 1。 |
| `-MR_ENOMEM` | `next >= size`，入口表已满。 |

源码使用 `MR_ASSERT()` 检查以下条件：`hook != MR_NULL`、`entry != MR_NULL`、以及 `hook->entry != MR_NULL` 或 `hook->size == 0`。这些断言失败时的行为取决于断言配置。

## 使用示例

```c
#include <mrX/kernel/mr_hook.h>

typedef void (*event_hook_t)(int event);

static mr_ptr_t event_entries[4];
static mr_hook_t event_hook = MR_HOOK_INIT(event_entries, 4);

static void on_event(int event) {
    (void)event;
}

void example_hook_register(void) {
    mr_hook_add(&event_hook, (mr_ptr_t)on_event);
}

void example_hook_emit(int event) {
    mr_uint16_t i;
    event_hook_t cb;

    MR_HOOK_EACH(i, cb, event_hook_t, &event_hook) {
        cb(event);
    }
}
```

## 注意事项

- 钩子表容量固定，模块不会自动扩容；容量不足时 `mr_hook_add()` 返回 `-MR_ENOMEM`。
- `mr_hook_init()` 不清空入口数组内容，只将 `next` 置为 0；旧数组内容不会被遍历到。
- `mr_hook_add()` 只保护追加过程；`MR_HOOK_EACH()` 遍历时没有加锁。
- `MR_HOOK_EACH()` 会在循环初始化阶段读取 `entry[0]`，因此即使 `next == 0`，也要求 `entry` 指向有效数组，除非调用者避免对空钩子执行遍历宏。
- 模块存储的是 `mr_ptr_t`，函数指针和数据指针转换需要与目标平台 ABI 保持一致。

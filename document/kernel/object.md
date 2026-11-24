# 对象系统（Object）

为内核组件提供统一的类型描述（`clazz`）、引用计数、单继承派生与双通道（静态/动态）生命周期管理。

| 项目   | 说明                                                |
|--------|-----------------------------------------------------|
| 功能宏 | 无，始终编译                                        |
| 头文件 | `<mrX/kernel/mr_object.h>`                          |
| 依赖   | 无                                                  |
| 测试   | `test/kernel/object.c`、`test/kernel/object_deep.c` |

## 配置

| 配置项                  | 类型 | 默认值 | 说明                                                         |
|-------------------------|------|--------|--------------------------------------------------------------|
| `MR_USE_REF_LONG_COUNT` | bool | n      | 启用 16 位引用计数（默认 8 位）；并发引用可能超过 255 时开启 |

## 快速上手

```c
#include <mrX/kernel/mr_object.h>

static void demo_statik_release(mr_object_t *obj) {
    /* 静态对象：仅做反初始化，不释放内存 */
}

static void demo_dynamic_release(mr_object_t *obj) {
    /* 动态对象：反初始化并释放堆内存 */
}

/* 注册名为 demo 的双通道 clazzes，无父类 */
MR_CLAZZES_DEFINE(demo, demo_statik_release, demo_dynamic_release, MR_NULL);

void demo_usage(void) {
    /* 静态初始化，初始引用计数为 1 */
    mr_object_t obj = MR_OBJECT_INIT(MR_CLAZZES_STATIK(demo));

    /* 其他持有者获取引用 */
    mr_object_t *held = mr_object_get(&obj);

    /* 所有者归还初始引用；此时 held 仍有效 */
    mr_object_del(&obj);

    /* 最后一个引用归还，计数归零并触发 demo_statik_release */
    if (held != MR_NULL) {
        mr_object_put(held);
    }
}
```

## 核心机制与约束

- **双通道类型（`clazzes`）**：每种对象通过 `MR_CLAZZES_DEFINE` 同时定义 `statik` 与
  `dynamic` 两个 `mr_clazz_t`，分别绑定静态与动态释放回调。`mr_object_del()`
  本质上是执行一次 `mr_object_put()` 归还初始引用，仅当引用计数归零时才按当前
  `clazz` 触发对应释放路径。
- **继承与释放顺序**：通过 `super` 指针构成单继承链。引用计数归零时，沿着继承链
  **先调用父 `clazz` 的 `release`，再调用当前 `clazz` 的 `release`**
  （与析构顺序相反，子类释放前父类成员仍完整）。
- **对象派生**：`mr_object_derive()` 允许将已初始化对象切换为当前 `clazz` 的子
  `clazz`；若目标 `clazz` 不是当前类型的派生类，返回 `-MR_EPERM`。
- **释放上下文与重入**：`release` 在最后一次 `mr_object_put()` 的调用者上下文中同步执行。计数归零时
  `magic_ref` 已被标记为无效，此后 `mr_object_get()` 返回 `MR_NULL`，再次
  `mr_object_put()` 直接返回 `MR_TRUE` 且不会重复释放。
- **重复初始化**：`mr_object_init()` 会直接重置 `clazz` 与引用计数为
  1，不检查是否仍有外部引用；仅在确认无其他持有者时才可复用静态对象。

## API 速查

### 类型定义与注册宏

| API                                                       | 说明                                                         |
|-----------------------------------------------------------|--------------------------------------------------------------|
| `MR_CLAZZ_INIT(_name, _release, _super)`                  | 静态初始化单个 `mr_clazz_t`                                  |
| `mr_clazz_init(clazz, name, release, super)`              | 运行时初始化单个 `mr_clazz_t`                                |
| `MR_CLAZZES_INIT(_name, _statik_rel, _dyn_rel, _super)`   | 静态初始化一对 `mr_clazzes_t`                                |
| `MR_CLAZZES_DEFINE(_name, _statik_rel, _dyn_rel, _super)` | 定义名为 `__mr_clazzes_<_name>` 的 `const mr_clazzes_t` 实例 |
| `MR_CLAZZES_EXPORT(_name)` / `MR_CLAZZES_IMPORT(_name)`   | 声明外部 `clazzes`（两者等价）                               |
| `MR_CLAZZES_FIND(_name)`                                  | 获取 `mr_clazzes_t *` 指针                                   |
| `MR_CLAZZES_STATIK(_name)` / `MR_CLAZZES_DYNAMIC(_name)`  | 获取静态 / 动态通道 `const mr_clazz_t *`                     |

### 对象生命周期与类型检查

| API                                                                    | 说明                                                                            |
|------------------------------------------------------------------------|---------------------------------------------------------------------------------|
| `MR_OBJECT_INIT(_clazz)`                                               | 静态初始化 `mr_object_t`（引用计数置 1）                                        |
| `mr_err_t mr_object_init(mr_object_t *obj, const mr_clazz_t *clazz)`   | 运行时初始化对象；`obj == MR_NULL` 返回 `-MR_EINVAL`（允许 `clazz == MR_NULL`） |
| `mr_err_t mr_object_derive(mr_object_t *obj, const mr_clazz_t *clazz)` | 派生到子 `clazz`；未初始化或空参返回 `-MR_EINVAL`，非子类返回 `-MR_EPERM`       |
| `void mr_object_del(mr_object_t *obj)`                                 | 归还一次引用（对 `MR_NULL` 或未初始化对象安全无操作）                           |
| `mr_object_t *mr_object_get(mr_object_t *obj)`                         | 引用计数加一；对象无效或已释放则返回 `MR_NULL`                                  |
| `mr_bool_t mr_object_put(mr_object_t *obj)`                            | 引用计数减一；归零触发释放或对象无效时返回 `MR_TRUE`，否则返回 `MR_FALSE`       |
| `MR_OBJECT_IS_INITED(_obj)`                                            | 判断对象是否已初始化且未进入释放终点                                            |
| `MR_OBJECT_CLAZZ(_obj)`                                                | 读取对象当前 `const mr_clazz_t *`                                               |
| `mr_bool_t mr_clazz_is_kind_of(clazz, target)`                         | 判断 `clazz` 是否为 `target` 或其派生类                                         |
| `MR_OBJECT_IS_CLAZZES(_obj, _name)`                                    | 判断对象是否属于 `_name` 对应的静态/动态通道或其派生类                          |

## 内部实现

`mr_object_t` 仅包含 `mr_ref_t magic_ref`（含魔数与引用计数）和
`const mr_clazz_t *clazz`，通常作为外层内核结构体的首成员嵌入。核心实现位于
`kernel/object.c`：引用计数通过 `mr_ref_get()` / `mr_ref_put()` 保证原子性；计数归零时由
`object_ref_release()` 经 `MR_CONTAINER_OF` 还原对象指针，并调用
`object_clazz_release()` 递归遍历 `clazz->super` 执行释放链。

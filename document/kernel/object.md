# 对象系统（Object）

> 对象系统是 mrX 全框架的类型与生命周期基座：为任意内核对象提供编译期/运行期类型标注
> （clazz）、引用计数与统一的双通道释放（静态/动态），是定时器、工作队列、驱动等一切
> 内核组件的共同基类。

| 项       | 说明                                                       |
|----------|------------------------------------------------------------|
| 功能宏   | 无（始终编译；`MR_USE_REF_LONG_COUNT` 可切换引用计数位宽） |
| 头文件   | `<mrX/kernel/mr_object.h>`                                 |
| 依赖模块 | 无（构建于 libc 原子操作 `mr_ref.h` 与断言之上）           |
| 参考测试 | `mrX/test/kernel/object.c`                                 |

---

## 用户指南

### 1. 模块概述

mrX 的内核组件（如 [定时器](timer.md)、[工作队列](work.md)、[驱动](../drivers/driver.md)）都需要 回答三个问题：
**这个对象是什么类型？**（类型检查） **还有谁在用它？**（引用计数） **用完之后怎么收尾？**（静态反初始化 / 动态释放内存）。对象系统用极小的代价
（两个指针字）回答这三个问题：

- 每个"对象"首部嵌一个 `mr_object_t` 基类，携带一个 clazz（类型描述符）与一个原子引用计数。
- 每种类型通过 `MR_CLAZZES_DEFINE` 注册一对 clazz：静态通道与动态通道，删除对象时按通道收尾。
- 引用计数归零时自动沿 clazz 继承链调用释放函数，派生类型无需重复实现生命周期代码。

适用场景：编写新的内核组件（给组件加类型检查与生命周期）、跨模块持有组件指针 （get/put 防止悬空）、组件派生扩展。

不适用于：普通数据结构（不需要类型/生命周期的场合直接用结构体即可，不要为它们 发明 clazz）。

### 2. 配置与依赖

| 配置项                  | 类型 | 默认值 | 说明                                                                                                |
|-------------------------|------|--------|-----------------------------------------------------------------------------------------------------|
| `MR_USE_REF_LONG_COUNT` | bool | n      | 启用 16 位引用计数（默认 8 位）。引用计数可能超过 255 时启用（`mrX/include/mrX/kernel/Kconfig:16`） |

对象系统无模块开关，始终参与编译。包含头文件即可使用：

```c
#include <mrX/kernel/mr_object.h>
```

该头文件自动引入 `mr_service.h`（`MR_CONTAINER_OF` 等）、`mr_ref.h`（引用计数）与 错误码定义。

### 3. 快速上手

定义一个可被类型检查、可被引用计数的对象类型并走完生命周期：

```c
#include <mrX/kernel/mr_object.h>

/* Release handlers: called when the reference count drops to zero */
static void my_obj_statik_release(mr_object_t *obj) {
    /* Static object: nothing to free, storage belongs to the caller */
}

static void my_obj_dynamic_release(mr_object_t *obj) {
    /* Dynamic object: free the heap block here if any */
}

/* Define clazzes: name, static release, dynamic release, super clazz */
MR_CLAZZES_DEFINE(my_obj, my_obj_statik_release, my_obj_dynamic_release,
                  MR_NULL);

void my_obj_example(void) {
    /* Static initialization with MR_OBJECT_INIT */
    mr_object_t obj = MR_OBJECT_INIT(MR_CLAZZES_STATIK(my_obj));

    /* (Runtime initialization: mr_object_init(&obj, clazz), equivalent) */

    /* Reference management: initial reference is held by the owner */
    if (mr_object_get(&obj) != MR_NULL) {
        /* Someone else holds the object */
        mr_object_put(&obj);
    }

    /* Delete: drops the initial reference, release handler runs */
    mr_object_del(&obj);
}
```

要点：

- 引用计数初始化为 1：执行 `init`（或持有对象的组件）天然持有一次引用。
- `mr_object_del` 就是"归还初始引用"，不是强制的销毁：只有计数归零才触发释放。
- 静态/动态两种方式使用不同的 clazz，从而在删除时走不同的释放通道。

### 4. 使用说明

**生命周期**

- 静态方式：调用者分配存储（全局/栈上/嵌入其他结构体），用 `MR_OBJECT_INIT` 或
  `mr_object_init` 以 `MR_CLAZZES_STATIK(name)` 初始化；用完调用 `mr_object_del`， 静态释放通道执行收尾，存储仍归调用者，可重新
  `init` 复用。
- 动态方式：组件内部 `mr_malloc` 分配存储并以 `MR_CLAZZES_DYNAMIC(name)` 初始化； 计数归零时动态释放通道执行收尾并归还内存，此后指针不可再用。

**引用计数约定**

| 操作                        | 行为                                                                            |
|-----------------------------|---------------------------------------------------------------------------------|
| `mr_object_get`             | 计数加一并返回对象指针；对象未初始化或已到达生命周期终点时返回 `MR_NULL`        |
| `mr_object_put`             | 计数减一；归零时执行释放链并返回 `MR_TRUE`（生命周期终点），否则返回 `MR_FALSE` |
| 对未初始化/已删除对象 `put` | 安全，直接返回 `MR_TRUE`                                                        |
| 对未初始化/已删除对象 `get` | 安全，返回 `MR_NULL`                                                            |
| 删除后再次 `get` / `put`    | 安全：`get` 得 `MR_NULL`，`put` 得 `MR_TRUE`                                    |
| 对已初始化对象重复 `init`   | 允许，重新初始化（计数重置为 1）                                                |

**类型检查**：拿到一个对象指针后，先用 `MR_OBJECT_IS_CLAZZES(obj, name)` 确认类型， 再做后续操作。内核各组件的 API
内部都执行该检查，直接传入错误类型对象在断言开启时 会被 `MR_ASSERT` 拦截。

**释放函数上下文**：释放函数在最后一次 `put` 的调用者上下文中同步执行，其中不得 依赖"对象仍然可用"的假设（此时对象已被标记为生命周期终点）。

### 5. API 参考

clazz 定义与注册：

| API                                 | 说明                                   |
|-------------------------------------|----------------------------------------|
| `MR_CLAZZES_DEFINE`                 | 宏：定义一对 clazz（静态/动态通道）    |
| `MR_CLAZZ_INIT` / `MR_CLAZZES_INIT` | 宏：以初始化器形式构造 clazz / clazzes |
| `mr_clazz_init`                     | 运行时初始化单个 clazz                 |

初始化与销毁：

| API                | 说明                                          |
|--------------------|-----------------------------------------------|
| `MR_OBJECT_INIT`   | 静态初始化宏（编译期以指定 clazz 初始化对象） |
| `mr_object_init`   | 运行时初始化对象                              |
| `mr_object_derive` | 将已初始化对象派生到子 clazz                  |
| `mr_object_del`    | 删除对象（归还初始引用）                      |

引用管理：

| API             | 说明                                   |
|-----------------|----------------------------------------|
| `mr_object_get` | 引用计数加一，返回对象指针或 `MR_NULL` |
| `mr_object_put` | 引用计数减一，返回是否到达生命周期终点 |

类型与状态查询：

| API                    | 说明                                      |
|------------------------|-------------------------------------------|
| `MR_OBJECT_IS_INITED`  | 宏：对象是否已初始化                      |
| `MR_OBJECT_CLAZZ`      | 宏：读取对象当前 clazz                    |
| `MR_OBJECT_IS_CLAZZES` | 宏：对象是否属于指定 clazzes（含继承链）  |
| `mr_clazz_is_kind_of`  | 某clazz 是否为目标 clazz 的子孙（含自身） |

**`mr_object_init`**

```c
mr_err_t mr_object_init(mr_object_t *obj, const mr_clazz_t *clazz);
```

- `obj`：对象基类指针（通常由组件内部把外层结构体首成员转型传入）。
- `clazz`：clazz 指针，通常取 `MR_CLAZZES_STATIK(name)` 或 `MR_CLAZZES_DYNAMIC(name)`； 传 `MR_NULL` 允许（对象可初始化但无类型）。
- 返回：成功 `0`；`obj` 为 `MR_NULL` 返回 `-MR_EINVAL`。
- 注意：重复 `init` 会重置引用计数为 1。

**`mr_object_derive`**

```c
mr_err_t mr_object_derive(mr_object_t *obj, const mr_clazz_t *clazz);
```

将对象的 clazz 换成其子 clazz（新 clazz 的继承链必须包含当前 clazz）。

- 返回：成功 `0`；参数为空或对象未初始化返回 `-MR_EINVAL`；`clazz` 不是当前 clazz 的子 clazz 返回 `-MR_EPERM`。

**`mr_object_del`**

```c
void mr_object_del(mr_object_t *obj);
```

对未初始化对象或 `MR_NULL` 安全（直接返回）。等价于对初始引用执行一次 `put`。

**`mr_object_get` / `mr_object_put`**

```c
mr_object_t *mr_object_get(mr_object_t *obj);
mr_bool_t mr_object_put(mr_object_t *obj);
```

`get` 返回对象指针（计数已加一），未初始化或生命周期已终点返回 `MR_NULL`。
`put` 返回 `MR_TRUE` 表示本次归零、生命周期结束（释放链已执行），`MR_FALSE` 表示 仍有引用。

**`MR_OBJECT_IS_CLAZZES`**

```c
#define MR_OBJECT_IS_CLAZZES(_obj, _name)                                      \
    (mr_clazz_is_kind_of(MR_OBJECT_CLAZZ(_obj), MR_CLAZZES_STATIK(_name))      \
     || mr_clazz_is_kind_of(MR_OBJECT_CLAZZ(_obj), MR_CLAZZES_DYNAMIC(_name)))
```

`_name` 为 `MR_CLAZZES_DEFINE` 使用的 clazzes 名（不加引号）。对象属于该 clazzes 的静态或动态通道、或其派生 clazz 时返回
`MR_TRUE`。

### 6. 完整示例

示例 1：组件封装——以静态/动态双通道初始化并统一删除（改编自 `mrX/test/kernel/object.c:89-104`）：

```c
static void demo_statik_release(mr_object_t *obj) { /* nothing to free */ }
static void demo_dynamic_release(mr_object_t *obj) { /* free heap part */ }
MR_CLAZZES_DEFINE(demo, demo_statik_release, demo_dynamic_release, MR_NULL);

void demo_example(void) {
    mr_object_t obj;
    mr_object_t *held;

    mr_object_init(&obj, MR_CLAZZES_STATIK(demo));
    held = mr_object_get(&obj);         /* Another module takes a reference */
    mr_object_del(&obj);                /* Owner drops initial reference */
    /* held is still valid until mr_object_put(held) drops the last one */
    if (held != MR_NULL) {
        mr_object_put(held);            /* Reference count reaches zero here */
    }
}
```

示例 2：类型检查守卫（改编自 `mrX/test/kernel/object.c:256-269`）：

```c
void typecheck_example(mr_object_t *unknown) {
    if (!MR_OBJECT_IS_INITED(unknown)) {
        return;
    }
    if (MR_OBJECT_IS_CLAZZES(unknown, demo)) {
        /* Safe to treat as demo object */
    }
}
```

示例 3：派生到子类型后再删除，父类与子类释放函数都会被调用 （改编自 `mrX/test/kernel/object.c:400-421`）：

```c
MR_CLAZZES_DEFINE(demo_sub, demo_sub_statik_release, demo_sub_dynamic_release,
                  MR_CLAZZES_STATIK(demo));

void derive_example(void) {
    mr_object_t obj;

    mr_object_init(&obj, MR_CLAZZES_STATIK(demo));
    mr_object_derive(&obj, MR_CLAZZES_DYNAMIC(demo_sub));
    mr_object_del(&obj);
    /* Release chain: demo_statik_release -> demo_sub_dynamic_release */
}
```

更多边界行为（重复 init、删除后 get/put、无关 clazz 派生被拒等）见
`mrX/test/kernel/object.c`。

### 7. 常见问题（FAQ）

**Q：`mr_object_del` 之后内存被释放了吗？**

取决于对象初始化时使用的 clazz：静态 clazz 的释放函数不释放内存（存储归调用者， 可复用）；动态 clazz 的释放函数负责归还内存（如内核组件在
`create` 路径上分配的堆块）。 对象系统本身不调用 `mr_free`，收尾动作完全由该类型的释放函数定义。

**Q：`get` 返回了 `MR_NULL` 是什么意思？**

对象未初始化，或引用计数已归零（生命周期终点）。此时不要再使用该指针。

**Q：`put` 返回 `MR_TRUE` 后还能继续用对象吗？**

不能。`MR_TRUE` 表示生命周期已终点、释放链已执行。对静态对象需要重新 `init` 才能 复用。

**Q：初始引用是谁的？**

是执行 `init`（或创建该对象的组件）的"所有者"。所有者用 `mr_object_del` 归还； 其他持有者在持有期间各自 `get`/`put` 配对。

**Q：为什么派生被拒绝并返回 `-MR_EPERM`？**

`mr_object_derive` 只允许沿继承链"向下"派生：新 clazz 的 `super` 链上必须出现对象 当前的 clazz。把对象派生到无继承关系的
clazz（包括同类型的另一个通道）会被拒绝。

**Q：引用计数最大是多少？**

默认 8 位计数，上限 255；启用 `MR_USE_REF_LONG_COUNT` 后为 16 位，上限 65535。

---

## 开发者指南

### 8. 架构总览

对象系统是内核最底层的服务之一，位于所有内核组件之下：

```
        +------------------------------------------+
        |  内核组件层：Timer / Work / Driver / ...   |
        |  以 mr_object_t 为首成员，持有各自 clazzes |
        +---------------------+--------------------+
                              | mr_object_init / get / put / del
        +---------------------v--------------------+
        |  对象系统 Object                          |
        |  clazz 继承链 + 引用计数 + 双通道释放      |
        +---------------------+--------------------+
                              | mr_ref_get / mr_ref_put
        +---------------------v--------------------+
        |  引用计数 Ref（mrX/include/mrX/libc 层面   |
        |  的原子 CAS 实现，见 mr_ref.h）            |
        +------------------------------------------+
```

对外提供的内部接口即 `mr_object.h` 全部内容；组件层的典型接法参见
`mrX/kernel/timer.c:17`（timer clazzes 注册）与 `mrX/kernel/timer.c:33`（init 时嵌入 对象基类）。

### 9. 数据结构

**clazz 与 clazzes**（`mrX/include/mrX/kernel/mr_object.h:31`、`mr_object.h:38`）：

```c
typedef struct mr_clazz {
    const char *name;                   /* Clazz name */
    mr_object_release_t *release;       /* Release handler */
    const struct mr_clazz *super;       /* Super clazz (NULL for root) */
} mr_clazz_t;

typedef struct mr_clazzes {
    const mr_clazz_t statik;            /* Clazz for static objects */
    const mr_clazz_t dynamic;           /* Clazz for dynamic objects */
} mr_clazzes_t;
```

每个类型注册一对 clazz：`statik` 与 `dynamic` 共享类型名与继承链，仅 `release`
不同——这是"静态/动态双释放通道"的载体。`super` 构成继承链，类型检查与释放链都 沿它递归。

**对象基类**（`mr_object.h:44`）：

```c
typedef struct mr_object {
    mr_ref_t magic_ref;                 /* Atomic ref: magic + count */
    const mr_clazz_t *clazz;            /* Current clazz */
} mr_object_t;
```

| 字段        | 说明                                                                                                                      |
|-------------|---------------------------------------------------------------------------------------------------------------------------|
| `magic_ref` | 引用计数，高位为 magic（`0x6d72` 前缀）、低位为计数，见 `mrX/include/mrX/kernel/mr_ref.h:26-34`；magic 兼作"已初始化"标记 |
| `clazz`     | 当前 clazz；`mr_object_derive` 修改它即完成"运行期换型"                                                                   |

**关键宏的编码方式**：

- `MR_CLAZZES_DEFINE(name, ...)`（`mr_object.h:107`）定义符号
  `__mr_clazzes_<name>`；`MR_CLAZZES_EXPORT/IMPORT(name)`（`mr_object.h:116`、
  `mr_object.h:124`）就是对该符号的 `extern` 声明（两者同义，IMPORT 仅为语义别名）；
  `MR_CLAZZES_FIND/STATIK/DYNAMIC(name)`（`mr_object.h:132`、`mr_object.h:140`、
  `mr_object.h:148`）按名取符号及其中通道。
- `MR_OBJECT_INIT(_clazz)`（`mr_object.h:155`）以 `MR_REF_INIT()`（计数 1）和 clazz 指针填充对象，可在定义处完成初始化。

### 10. 核心流程

**初始化**：`mr_object_init`（`mrX/kernel/object.c:11`）仅记录 clazz 并
`mr_ref_init`（计数置 1），不做任何分配，因此允许对已初始化对象重复调用（重置语义）。

**派生**：`mr_object_derive`（`object.c:23`）先检查参数与初始化状态，再以
`mr_clazz_is_kind_of(clazz, 当前 clazz)`（`mr_object.h:203`，沿 `super` 链线性查找） 确认新 clazz 是当前 clazz 的子孙，通过后仅改写
`obj->clazz`。同类两个通道互不为 子孙，因此跨通道派生返回 `-MR_EPERM`（测试 `derive_not_subclass`，
`mrX/test/kernel/object.c:368`）。

**引用计数**：`mr_object_get/put` 薄封装 `mr_ref_get/mr_ref_put`：

- `mr_ref_get`（`mr_ref.h:83`）：CAS 循环加一；发现未初始化（magic 不符）或计数为 0 即返回失败——这保证"删除后 get
  不会复活对象"。
- `mr_ref_put`（`mr_ref.h:109`）：`fetch_sub` 减一；若减前计数为 1，先用
  `fetch_and(~MR_REF_MAGIC)` 清除 magic（`mr_ref.h:117`）把对象标记为生命周期终点， 然后才调用释放回调。 **先标记后释放**
  的顺序保证释放过程中并发的 `get` 一律失败。

**释放链**：计数归零后 `object_ref_release`（`object.c:74`）以
`MR_CONTAINER_OF` 取回对象，调用 `object_clazz_release`（`object.c:59`）：从对象当前 clazz 沿 `super` 链
**先递归父类、再执行本类**的 release（`release` 为 `MR_NULL` 的 clazz 跳过）。因此派生对象的删除会依次归还父类到子类的全部资源（测试
`derive_del_release`，`mrX/test/kernel/object.c:400`）。

**删除**：`mr_object_del`（`object.c:39`）= 一次 `put`。组件层在它之外负责摘链等 自身清理，如 timer 的两个 release（
`mrX/kernel/timer.c:187-200`）：静态通道仅摘链， 动态通道额外 `mr_free`。

### 11. 并发与中断安全

- 引用计数的增减是原子操作（`mr_ref_t` 为 `volatile mr_atomic_t`），`get`/`put`
  可在任意上下文（含中断）调用，无需外部加锁。
- 对象系统自身无锁：clazz 指针的改写（`derive`）与初始化不由锁保护，同一对象的
  `init`/`derive` 与并发 `get/put` 的交由使用者按组件约定安排（内核组件均在初始化 阶段完成 derive，之后再不修改 clazz）。
- 释放链在最后一次 `put` 的调用者上下文中同步递归执行；若释放函数可能睡眠，则
  `put` 也不可在中断上下文调用（内核组件的释放函数均为无阻塞的摘链/释放内存）。

### 12. 扩展与派生

基于对象系统扩展一个新组件类型的标准步骤（以 timer 为完整范例）：

1. 组件结构体首成员放置自身对象基类（`mr_object_t` 或以其为首的兼容布局）。
2. `MR_CLAZZES_DEFINE(timer, statik_release, dynamic_release, super)` 注册 clazzes； 需要继承已有类型时把 `super` 指向父类型（如
   `MR_CLAZZES_STATIK(父类型名)`）。
3. 初始化路径调用 `mr_object_init` 传入对应通道 clazz；`create` 路径用动态通道。
4. API 入口用 `MR_OBJECT_IS_INITED` + `MR_OBJECT_IS_CLAZZES` 做类型守卫。
5. 释放函数中完成组件自身清理（静态通道不释放内存，动态通道释放）。

运行期派生：对已初始化对象调用 `mr_object_derive` 切换到子 clazz，即可在不动 初始化代码的前提下让对象携带子类型语义，参考
`mrX/test/kernel/object.c:312-331`。

### 13. 源码导读

| 位置                                     | 职责                                              |
|------------------------------------------|---------------------------------------------------|
| `mrX/include/mrX/kernel/mr_object.h:31`  | `mr_clazz_t` / `mr_clazzes_t`：类型描述符与双通道 |
| `mrX/include/mrX/kernel/mr_object.h:44`  | `mr_object_t` 对象基类                            |
| `mrX/include/mrX/kernel/mr_object.h:107` | `MR_CLAZZES_DEFINE` 等 clazzes 宏族               |
| `mrX/include/mrX/kernel/mr_ref.h:83`     | 引用计数 get（CAS 循环）                          |
| `mrX/include/mrX/kernel/mr_ref.h:109`    | 引用计数 put（先标记后释放）                      |
| `mrX/kernel/object.c:11`                 | 对象初始化                                        |
| `mrX/kernel/object.c:23`                 | 运行期派生（子 clazz 检查）                       |
| `mrX/kernel/object.c:59`                 | 释放链递归（super 先、本类后）                    |
| `mrX/kernel/object.c:84`                 | put 入口                                          |
| `mrX/kernel/timer.c:17`                  | 组件层 clazzes 注册范例                           |
| `mrX/test/kernel/object.c`               | 全部对象行为测试（init/derive/get/put/释放链）    |

### 14. 附录

**配置宏**

| 宏                                                                 | 说明                                                                                                                 |
|--------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------------|
| `MR_USE_REF_LONG_COUNT`                                            | 引用计数位宽开关：默认 8 位计数（`MR_REF_MAGIC` 为 `0x6d725800`），启用后 16 位（`0x6d720000`），见 `mr_ref.h:24-34` |
| `MR_REF_MAGIC` / `MR_REF_MAGIC_MASK`                               | 引用计数 magic 与掩码，兼作初始化标记                                                                                |
| `MR_OBJECT_IS_INITED` / `MR_OBJECT_CLAZZ` / `MR_OBJECT_IS_CLAZZES` | 对象状态与类型检查宏                                                                                                 |

**错误码**

| 返回值       | 产生点                                                               |
|--------------|----------------------------------------------------------------------|
| `-MR_EINVAL` | `mr_object_init` 传入空对象；`mr_object_derive` 空参数或对象未初始化 |
| `-MR_EPERM`  | `mr_object_derive` 目标 clazz 不是当前 clazz 的子孙                  |
| `MR_NULL`    | `mr_object_get` 对象未初始化或生命周期已终点                         |

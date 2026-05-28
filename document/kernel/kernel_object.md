# 对象管理

mrX 内核使用对象控制块统一管理内核资源的生命周期。每个内核对象都以内嵌的 `mr_object_t` 作为基础对象，基础对象记录引用计数和类描述符。调用者通过
`mr_object_get()` 获取引用，通过 `mr_object_put()` 或 `mr_object_del()` 释放引用；当最后一个引用被释放时，内核会结束对象生命周期并调用对应的类释放函数。

本文说明对象控制块、类描述符、引用规则和常用接口的使用方式。

## 核心概念

### 对象控制块

对象控制块用于描述一个内核对象：

```c
typedef struct mr_object {
    mr_ref_t magic_ref;
    const mr_clazz_t *clazz;
} mr_object_t;
```

字段含义如下：

| 字段          | 含义                                                           |
|-------------|--------------------------------------------------------------|
| `magic_ref` | 引用计数和生命周期标记。它由 `mr_ref_t` 管理，初始化后计数为 1；最后一个引用释放后，生命周期标记会被清除。 |
| `clazz`     | 当前对象使用的类描述符。对象释放时会沿该类的继承链调用释放函数。                             |

对象可以动态初始化，也可以使用 `MR_OBJECT_INIT()` 做静态初始化：

```c
#define MR_OBJECT_INIT(_clazz)                                                 \
    {                                                                          \
        .magic_ref = MR_REF_INIT(),                                            \
        .clazz = (_clazz),                                                     \
    }
```

### 类描述符

类描述符描述对象类型、释放函数和父类：

```c
typedef struct mr_clazz {
    const char *name;
    mr_object_release_t *release;
    const struct mr_clazz *super;
} mr_clazz_t;
```

字段含义如下：

| 字段        | 含义                                      |
|-----------|-----------------------------------------|
| `name`    | 类名称，主要用于标识对象类型。                         |
| `release` | 对象生命周期结束时调用的释放函数，可以为 `MR_NULL`。         |
| `super`   | 父类描述符，用于建立继承关系和释放函数调用链。无父类时为 `MR_NULL`。 |

可以使用 `MR_CLAZZ_INIT()` 静态初始化类描述符，也可以使用 `mr_clazz_init()` 在运行时初始化类描述符。

### 类族

类族用于同时描述同一种对象的静态对象类和动态对象类：

```c
typedef struct mr_clazzes {
    const mr_clazz_t statik;
    const mr_clazz_t dynamic;
} mr_clazzes_t;
```

`statik` 表示静态对象使用的类描述符，`dynamic` 表示动态对象使用的类描述符。字段名 `statik` 是为了避开 C/C++ 的 `static`
关键字。

通常使用下面的宏定义和访问类族：

```c
MR_CLAZZES_DEFINE(timer, timer_statik_release, timer_dynamic_release, MR_NULL);

MR_CLAZZES_STATIK(timer);
MR_CLAZZES_DYNAMIC(timer);
```

静态类和动态类可以使用不同的释放函数。静态释放函数通常只回收对象内部资源，不释放对象本身占用的内存；动态释放函数通常在回收内部资源后，还会释放对象本身占用的内存。

这个区分用于表达一个重要原则：谁申请内存，谁释放内存。如果一个对象是动态创建的，那么只有实际执行动态创建的那一层类负责释放整块对象内存；它的父类只负责按静态对象方式释放自己管理的内部资源。

## 生命周期规则

对象生命周期遵循如下规则：

1. 对象必须先初始化，再传给其他对象接口。
2. 初始化成功后，对象初始引用计数为 1。
3. 每次成功调用 `mr_object_get()` 都会增加一个引用，调用者必须用一次 `mr_object_put()` 或 `mr_object_del()` 释放该引用。
4. `mr_object_put()` 释放最后一个引用时返回 `MR_TRUE`，并触发类释放函数调用链。
5. `mr_object_put()` 释放非最后一个引用时返回 `MR_FALSE`，对象仍然有效。
6. 最后一个引用释放后，`magic_ref` 的生命周期标记被清除，后续 `mr_object_get()` 会失败。
7. `mr_object_put(MR_NULL)`、`mr_object_put()` 作用于未初始化对象或已结束生命周期的对象时返回 `MR_TRUE`，表示调用方不需要继续持有该对象。
8. `mr_object_del()` 是忽略返回值的便捷接口，适用于不关心对象是否在本次调用后结束生命周期的场景。

引用计数增减由 `mr_ref_t` 的原子操作完成，但对象的类指针更新、释放函数内容以及对象自身资源并不会自动加锁。若对象可能被多个执行上下文同时派生、释放或访问内部资源，调用者需要使用对象所属模块的锁来保护这些操作。

## 类继承

`super` 用于建立类继承链。`mr_clazz_is_kind_of(clazz, target)` 会沿 `clazz->super` 向上查找；如果链上存在 `target`，则认为
`clazz` 是 `target` 的同类或子类。

继承关系本质上由类描述符指针串起来。例如下面的类族定义表示 `child` 继承自 `base`：

```c
MR_CLAZZES_DEFINE(base, base_statik_release, base_dynamic_release, MR_NULL);
MR_CLAZZES_DEFINE(child, child_statik_release, child_dynamic_release,
                  MR_CLAZZES_STATIK(base));
```

此时静态类链如下：

```text
child.statik -> base.statik -> MR_NULL
```

需要注意，`MR_CLAZZES_DEFINE()` 的第四个参数是父类描述符，并且会同时作为 `statik` 和 `dynamic` 两个类描述符的父类。上例中
`child` 的父类是 `base.statik`，因此 `child.statik` 和 `child.dynamic` 都会继承到 `base.statik`。

这种写法是有意为之：子类对象的内存通常是一整块连续内存，父类部分不是单独 `create`
出来的，而是在子类对象内存中调用父类的初始化逻辑完成构造。因此释放时，父类应按静态对象释放，只清理父类拥有的内部资源；只有最外层、实际申请整块对象内存的动态子类释放函数才释放对象内存本身。

如果需要让静态类和动态类分别继承不同的父类，就不能直接使用 `MR_CLAZZES_DEFINE()` 一次性定义，需要分别构造对应的
`mr_clazz_t`。但在常规对象继承中，动态子类继承静态父类更符合“父类只初始化、不分配整块对象内存”的模型。

### 类型判定

`mr_clazz_is_kind_of(clazz, target)` 用于判断一个类是否属于目标类：

```c
mr_clazz_is_kind_of(MR_CLAZZES_STATIK(child), MR_CLAZZES_STATIK(base));
```

上面的调用返回 `MR_TRUE`，因为 `child.statik` 的父类链上存在 `base.statik`。

判定规则如下：

| 调用                                   | 结果         | 原因          |
|--------------------------------------|------------|-------------|
| `mr_clazz_is_kind_of(base, base)`    | `MR_TRUE`  | 类与自身匹配。     |
| `mr_clazz_is_kind_of(child, base)`   | `MR_TRUE`  | 子类链上存在父类。   |
| `mr_clazz_is_kind_of(base, child)`   | `MR_FALSE` | 父类不会反向匹配子类。 |
| `mr_clazz_is_kind_of(MR_NULL, base)` | `MR_FALSE` | 空类没有继承链。    |

`MR_OBJECT_IS_CLAZZES(obj, name)` 是对象级类型判定宏。它会把对象当前类分别和 `name` 的静态类、动态类比较，只要命中其中一个类族分支就返回
`MR_TRUE`。

### 对象派生

`mr_object_derive()` 只能把对象从当前类派生到当前类的子类：

```c
mr_object_init(&obj, MR_CLAZZES_STATIK(base));
mr_object_derive(&obj, MR_CLAZZES_STATIK(child));
```

派生成功后，对象的 `clazz` 会从原来的类描述符更新为目标类描述符。引用计数不会变化，对象内存也不会被重新分配。

派生方向必须满足：目标类是当前类的同类或子类。也就是说，下面的派生是合法的：

```c
mr_object_derive(&obj, MR_CLAZZES_STATIK(base));
mr_object_derive(&obj, MR_CLAZZES_STATIK(child));
```

下面的派生是不合法的：

```c
mr_object_derive(&obj, MR_CLAZZES_STATIK(unrelated));
mr_object_derive(&obj, MR_CLAZZES_STATIK(parent_of_current));
```

非法派生会返回 `-MR_EPERM`。对象为空、目标类为空或对象未初始化时，返回 `-MR_EINVAL`。

派生接口适合表达“对象初始化先完成公共部分，再推进到更具体的类型”。它不适合在对象已经公开给其他线程后随意改变类型；如果必须这样做，调用者需要保证没有并发访问或使用外部锁保护。

从内存模型看，派生并不会创建新的父类对象或子类对象，也不会移动对象内存。它只是把对象控制块中的 `clazz`
更新为更具体的类描述符。真正的父类初始化应发生在子类构造流程内部，并且通常是调用父类的 `init` 路径，而不是父类的 `create`
路径。

### 释放顺序

对象释放时，释放函数按父类到子类的顺序调用。也就是说，如果对象当前类为 `child`，且 `child->super` 为 `base`，释放顺序为：

1. `base->release(obj)`
2. `child->release(obj)`

释放函数可以为 `MR_NULL`，此时该级类不会执行释放回调，但继承链上的其他释放函数仍会继续执行。

这个顺序来自当前实现中的递归释放逻辑：先递归释放 `clazz->super`，再调用当前 `clazz->release`
。因此，如果某个子类依赖父类释放函数先完成公共资源回收，这个顺序是安全的；如果子类释放函数需要使用父类资源，就不能假设父类资源在子类释放时仍然可用。

在动态子类对象释放时，继承链中的父类通常是静态类释放函数，最后才调用子类自己的动态释放函数。例如
`child.dynamic -> base.statik -> MR_NULL` 的释放链会先执行 `base` 的静态释放，再执行 `child`
的动态释放。这样可以保证父类不会误释放子类申请的整块对象内存，最终由 `child` 的动态释放函数完成整块内存释放。

设计释放函数时建议遵循如下约定：

- 父类释放函数只处理父类拥有的公共资源。
- 子类释放函数只处理子类新增的资源。
- 父类作为 `super` 使用时，应优先使用静态类描述符。
- 动态释放函数只应出现在实际申请整块对象内存的那一层。
- 子类释放函数不要依赖父类释放函数已经释放后的资源状态。
- 不要在释放函数中再次调用 `mr_object_put()` 释放同一个对象引用。

## 对象管理接口

### 初始化对象

```c
mr_err_t mr_object_init(mr_object_t *obj, const mr_clazz_t *clazz);
```

`mr_object_init()` 将类描述符保存到对象控制块，并把引用计数初始化为 1。

返回值如下：

| 返回值          | 含义                          |
|--------------|-----------------------------|
| `0`          | 初始化成功。                      |
| `-MR_EINVAL` | `obj` 为 `MR_NULL`，或对象已经初始化。 |

`clazz` 可以为 `MR_NULL`。这种对象仍然可以使用引用计数管理生命周期，但最后一个引用释放时不会调用类释放函数。

### 派生对象

```c
mr_err_t mr_object_derive(mr_object_t *obj, const mr_clazz_t *clazz);
```

`mr_object_derive()` 将对象的当前类更新为指定子类。该接口会先检查对象是否已经初始化，再检查目标类是否为当前类的同类或子类。

返回值如下：

| 返回值          | 含义                                              |
|--------------|-------------------------------------------------|
| `0`          | 派生成功。                                           |
| `-MR_EINVAL` | `obj` 为 `MR_NULL`、`clazz` 为 `MR_NULL`，或对象尚未初始化。 |
| `-MR_EPERM`  | `clazz` 不是对象当前类的同类或子类。                          |

派生通常用于基础对象先完成通用初始化，再由更具体的模块把对象类型推进到子类。不要在对象已经可能被其他执行上下文释放时再修改其类描述符。

### 获取引用

```c
mr_object_t *mr_object_get(mr_object_t *obj);
```

`mr_object_get()` 尝试增加对象引用计数。

返回值如下：

| 返回值       | 含义                                    |
|-----------|---------------------------------------|
| 对象指针      | 获取引用成功。                               |
| `MR_NULL` | `obj` 为 `MR_NULL`、对象未初始化，或对象生命周期已经结束。 |

调用者只有在返回非 `MR_NULL` 时才拥有新增引用，并且必须匹配一次 `mr_object_put()` 或 `mr_object_del()`。

### 释放引用

```c
mr_bool_t mr_object_put(mr_object_t *obj);
```

`mr_object_put()` 释放对象的一个引用。

返回值如下：

| 返回值        | 含义                                     |
|------------|----------------------------------------|
| `MR_TRUE`  | 对象生命周期已经在本次调用中结束，或传入对象为空、未初始化、已结束生命周期。 |
| `MR_FALSE` | 只释放了一个非最后引用，对象仍有其他引用存在。                |

最后一个引用释放时，`mr_object_put()` 会清除生命周期标记，并调用对象当前类的释放链。

### 便捷删除

```c
void mr_object_del(mr_object_t *obj);
```

`mr_object_del()` 用于释放一个引用并忽略生命周期是否结束。其行为等价于：

```c
(void)mr_object_put(obj);
```

如果 `obj` 为 `MR_NULL`，或对象未初始化，该函数直接返回。

## 使用示例

### 基本引用计数

下面的示例演示对象初始化、获取引用和释放引用的基本过程：

```c
#include <mrX/kernel/mr_object.h>

static void sample_release(mr_object_t *obj)
{
    (void)obj;
}

static const mr_clazz_t sample_clazz =
    MR_CLAZZ_INIT("sample", sample_release, MR_NULL);

void sample_object_usage(void)
{
    mr_object_t obj;

    if (mr_object_init(&obj, &sample_clazz) != 0) {
        return;
    }

    if (mr_object_get(&obj) != MR_NULL) {
        mr_object_put(&obj);
    }

    mr_object_del(&obj);
}
```

在该示例中，`obj` 是一个静态分配的对象。初始化成功后引用计数为 1；`mr_object_get()` 成功后引用计数增加到 2；随后
`mr_object_put()` 释放新增引用，`mr_object_del()` 释放初始化时持有的引用并触发 `sample_release()`。

### 类族与派生

下面的示例演示如何使用类族和派生关系：

```c
#include <mrX/kernel/mr_object.h>

static void base_release(mr_object_t *obj)
{
    (void)obj;
}

static void child_release(mr_object_t *obj)
{
    (void)obj;
}

MR_CLAZZES_DEFINE(base, base_release, base_release, MR_NULL);
MR_CLAZZES_DEFINE(child, child_release, child_release,
                  MR_CLAZZES_STATIK(base));

void sample_derive_usage(void)
{
    mr_object_t obj;

    if (mr_object_init(&obj, MR_CLAZZES_STATIK(base)) != 0) {
        return;
    }

    if (mr_object_derive(&obj, MR_CLAZZES_STATIK(child)) != 0) {
        mr_object_del(&obj);
        return;
    }

    mr_object_del(&obj);
}
```

对象先以 `base` 类初始化，再派生为 `child` 类。最后释放对象时，释放链会先调用 `base_release()`，再调用 `child_release()`。

## 使用建议

- 对象创建者应明确谁持有初始化引用，并规定何时释放该引用。
- 获取引用失败时，不要继续访问对象内部资源。
- 释放函数应只处理对象生命周期结束时需要回收的资源，不应再次释放同一个对象引用。
- 静态对象和动态对象建议使用类族区分释放行为，避免静态对象释放路径误释放内存。
- 派生应发生在对象对外发布前，或在外部同步保护下进行。

# object - 对象系统

## 1. 模块概述

### 1.1 功能介绍

object 模块是一个在 C 语言中实现的轻量级面向对象基础设施。它提供了一套完整的对象生命周期管理机制，包括对象的创建、初始化、引用计数管理和自动释放。

在 C 语言中实现面向对象编程是一个挑战，因为 C 语言没有内置的类、继承、多态等特性。object 模块通过巧妙的设计，在 C
语言中实现了以下面向对象的核心特性：

- **封装**：通过结构体将数据和方法组织在一起
- **继承**：通过结构体嵌套实现类型继承
- **多态**：通过函数指针实现虚函数机制
- **RTTI（运行时类型识别）**：通过 clazz 指针实现类型信息查询
- **自动内存管理**：基于引用计数自动管理对象生命周期

object 模块的设计理念是"最小化但完整"：它只提供最核心的对象管理功能，保持轻量和高效，同时为上层模块构建更复杂的面向对象系统打下坚实基础。

### 1.2 主要特性

- **C 语言实现的面向对象**：不依赖 C++ 编译器，纯 C 语言实现
- **基于引用计数的生命周期管理**：自动管理对象生命周期，防止内存泄漏
- **类型安全**：通过 clazz 机制实现类型检查和 RTTI
- **支持静态和动态分配**：同一类型可以支持栈上分配和堆上分配两种方式
- **继承支持**：通过结构体嵌套实现类型继承
- **跨编译单元类型检查**：通过编译期宏实现类型的公开和导入
- **零开销抽象**：虚函数调用和类型检查的开销极小

### 1.3 适用场景

object 模块是 mrX 框架的核心基础设施，适用于以下场景：

- **设备驱动**：为各种设备提供统一的对象接口
- **内核对象**：定时器、工作队列、IRQ 等内核组件的生命周期管理
- **服务管理**：系统服务的注册和查找
- **资源管理**：文件、缓冲区、网络连接等资源的自动管理

**不适用场景**：

- 简单数据结构：如链表节点、哈希表条目等，不需要完整的对象系统
- 性能极度敏感的代码：虽然 object 模块开销很小，但仍有一定成本

---

## 2. 使用指南

### 2.1 核心数据结构

#### mr_object_t

```c
typedef struct mr_object {
    mr_ref_t magic_ref;        /* 引用计数 */
    const mr_clazz_t *clazz;   /* 类型信息 */
} mr_object_t;
```

所有对象的基础结构，包含两个成员：

- `magic_ref`：引用计数，用于生命周期管理（继承自 ref 模块）
- `clazz`：指向对象的类型信息（clazz）

**设计要点**：

- 只有 8 字节（32 位系统），非常轻量
- 通过将 `mr_object_t` 作为自定义结构体的第一个成员，可以实现"继承"

**使用技巧**：
在实际使用中，推荐直接将派生类指针转换为 `mr_object_t *`，而不是显式访问 `.parent` 成员：

```c
/* 推荐的写法 - 体现继承关系 */
mr_object_init((mr_object_t *)&dev, MR_CLAZZES_STATIK(MR_CLAZZES_FIND(my_device)));
mr_object_del((mr_object_t *)&dev);

/* 不推荐的写法 - 虽然也可以工作 */
mr_object_init(&dev.parent, MR_CLAZZES_STATIK(MR_CLAZZES_FIND(my_device)));
mr_object_del(&dev.parent);
```

第一种写法更能体现面向对象的"继承"语义，代码也更简洁。由于 C 语言保证结构体第一个成员的地址与结构体本身相同，这两种写法在底层是等价的。

#### mr_clazz_t

```c
typedef struct mr_clazz {
    const char *name;                     /* 类型名称 */
    mr_object_release_t *release;         /* 释放函数 */
} mr_clazz_t;
```

类型信息结构，描述一个对象类型：

**成员说明**：

- `name`：类型的名称，用于调试和日志
- `release`：对象的释放函数，当引用计数归零时被调用

#### mr_clazzes_t

```c
typedef struct mr_clazzes {
    const mr_clazz_t statik;    /* 静态分配对象的类型信息 */
    const mr_clazz_t dynamic;   /* 动态分配对象的类型信息 */
} mr_clazzes_t;
```

类型信息对，包含静态和动态两种分配方式的类型信息：

**为什么需要两个 clazz？**

- **statik**：用于栈上或静态分配的对象，释放时只执行清理操作，不释放内存
- **dynamic**：用于堆上分配（malloc）的对象，释放时会调用 free() 释放内存

### 2.2 主要接口说明

#### 类型定义接口

##### MR_CLAZZES_DEFINE()

```c
#define MR_CLAZZES_DEFINE(_name, _statik_release, _dynamic_release)
```

**功能**：定义一个对象的类型信息对

**参数**：

- `_name`：类型名称（标识符）
- `_statik_release`：静态对象的释放函数
- `_dynamic_release`：动态对象的释放函数

**示例**：

```c
static void my_object_statik_release(mr_object_t *obj) {
    /* 只做清理，不释放内存 */
    printf("Static object released\n");
}

static void my_object_dynamic_release(mr_object_t *obj) {
    my_object_t *my_obj = (my_object_t *)obj;
    /* 清理资源 */
    printf("Dynamic object released\n");
    /* 释放内存 */
    free(my_obj);
}

MR_CLAZZES_DEFINE(my_object, my_object_statik_release, my_object_dynamic_release);
```

**说明**：

- 这个宏会定义一个名为 `__mr_clazzes_##_name` 的全局变量
- 通过 `MR_CLAZZES_FIND(_name)` 可以找到这个类型信息

#### 类型查询接口

##### MR_CLAZZES_FIND()

```c
#define MR_CLAZZES_FIND(_name) (&MR_CAT(__mr_clazzes_, _name))
```

**功能**：查找对象的类型信息对

**参数**：

- `_name`：类型名称

**返回值**：指向 `mr_clazzes_t` 的指针

##### MR_CLAZZES_STATIK()

```c
#define MR_CLAZZES_STATIK(_clazzes) (&(_clazzes)->statik)
```

**功能**：从类型信息对中获取静态类型信息

##### MR_CLAZZES_DYNAMIC()

```c
#define MR_CLAZZES_DYNAMIC(_clazzes) (&(_clazzes)->dynamic)
```

**功能**：从类型信息对中获取动态类型信息

##### MR_OBJECT_IS_CLAZZES()

```c
#define MR_OBJECT_IS_CLAZZES(_obj, _name) \
    ((MR_OBJECT_CLAZZ(_obj) == MR_CLAZZES_STATIK(MR_CLAZZES_FIND(_name))) \
     || (MR_OBJECT_CLAZZ(_obj) == MR_CLAZZES_DYNAMIC(MR_CLAZZES_FIND(_name))))
```

**功能**：检查对象是否为指定类型

**参数**：

- `_obj`：对象指针
- `_name`：类型名称

**返回值**：

- `MR_TRUE`：对象是该类型
- `MR_FALSE`：对象不是该类型

**使用场景**：

- 类型安全检查
- 防止类型混淆
- 调试和日志

#### 对象初始化接口

##### MR_OBJECT_INIT()

```c
#define MR_OBJECT_INIT(_clazz) \
    { \
        .magic_ref = MR_REF_INIT(), \
        .clazz = (_clazz), \
    }
```

**功能**：静态初始化对象

**参数**：

- `_clazz`：指向类型信息的指针

**示例**：

```c
static my_object_t obj = MR_OBJECT_INIT(MR_CLAZZES_STATIK(MR_CLAZZES_FIND(my_object)));
```

##### mr_object_init()

```c
mr_err_t mr_object_init(mr_object_t *obj, const mr_clazz_t *clazz);
```

**功能**：动态初始化对象

**参数**：

- `obj`：指向对象的指针
- `clazz`：指向类型信息的指针

**返回值**：

- `0`：成功
- `-MR_EINVAL`：参数错误（obj 为 NULL 或对象已初始化）

**注意事项**：

- 不能重复初始化同一个对象
- 初始化前对象必须是未初始化状态

#### 对象生命周期管理接口

##### mr_object_get()

```c
mr_object_t *mr_object_get(mr_object_t *obj);
```

**功能**：获取对象的引用（增加引用计数）

**参数**：

- `obj`：对象指针

**返回值**：

- 成功：返回对象指针
- 失败（对象已释放或未初始化）：返回 `MR_NULL`

**使用规则**：

- **每次成功调用 `mr_object_get` 后，都必须配套调用一次 `mr_object_put`**
- 失败时不需要调用 `mr_object_put`

##### mr_object_put()

```c
mr_bool_t mr_object_put(mr_object_t *obj);
```

**功能**：释放对象的引用（减少引用计数）

**参数**：

- `obj`：对象指针

**返回值**：

- `MR_TRUE`：引用计数归零，对象已被释放
- `MR_FALSE`：引用计数仍大于 0，对象仍存活

**注意事项**：

- 必须与之前的 `mr_object_get` 配对使用
- 引用计数归零时，会自动调用对象的 release 函数
- release 函数中不应执行耗时操作或可能阻塞的调用

##### mr_object_del()

```c
void mr_object_del(mr_object_t *obj);
```

**功能**：删除对象（释放对象的初始引用）

**参数**：

- `obj`：对象指针

**说明**：

- 等价于调用 `mr_object_put(obj)`
- 通常用于释放对象创建时持有的初始引用

#### 状态查询接口

##### MR_OBJECT_IS_INITED()

```c
#define MR_OBJECT_IS_INITED(_obj) \
    (MR_REF_IS_INITED(((mr_object_t *)(_obj))->magic_ref))
```

**功能**：检查对象是否已初始化

**参数**：

- `_obj`：对象指针

**返回值**：

- `MR_TRUE`：已初始化
- `MR_FALSE`：未初始化或已释放

##### MR_OBJECT_CLAZZ()

```c
#define MR_OBJECT_CLAZZ(_obj) (((mr_object_t *)(_obj))->clazz)
```

**功能**：获取对象的类型信息

**参数**：

- `_obj`：对象指针

**返回值**：指向 `mr_clazz_t` 的指针

### 2.3 典型应用案例

#### 案例 1：定义和使用自定义对象

```c
#include <mrX/kernel/mr_object.h>
#include <mrX/libc/mr_string.h>
#include <stdio.h>
#include <stdlib.h>

/* 定义自定义对象类型 */
typedef struct {
    mr_object_t parent;      /* 父类对象（必须放在第一个成员） */
    int value;
    char name[32];
} my_device_t;

/* 释放函数 - 静态对象 */
static void my_device_statik_release(mr_object_t *obj) {
    my_device_t *dev = (my_device_t *)obj;
    printf("Static device released: %s\n", dev->name);
    /* 只做清理，不释放内存 */
}

/* 释放函数 - 动态对象 */
static void my_device_dynamic_release(mr_object_t *obj) {
    my_device_t *dev = (my_device_t *)obj;
    printf("Dynamic device released: %s\n", dev->name);
    /* 清理资源 */
    /* 释放内存 */
    free(dev);
}

/* 定义类型信息 */
MR_CLAZZES_DEFINE(my_device, my_device_statik_release, my_device_dynamic_release);

/* 使用静态对象 */
void example_static_object(void) {
    my_device_t dev;

    /* 初始化对象 */
    mr_object_init((mr_object_t *)&dev, MR_CLAZZES_STATIK(MR_CLAZZES_FIND(my_device)));
    dev.value = 100;
    snprintf(dev.name, sizeof(dev.name), "dev0");

    printf("Device created: %s, value=%d\n", dev.name, dev.value);

    /* 使用对象... */

    /* 删除对象 */
    mr_object_del((mr_object_t *)&dev);
    printf("Device deleted\n");
}

/* 使用动态对象 */
void example_dynamic_object(void) {
    my_device_t *dev = malloc(sizeof(my_device_t));
    if (!dev) {
        return;
    }

    /* 初始化对象 */
    mr_object_init((mr_object_t *)dev, MR_CLAZZES_DYNAMIC(MR_CLAZZES_FIND(my_device)));
    dev->value = 200;
    snprintf(dev->name, sizeof(dev->name), "dev1");

    printf("Device created: %s, value=%d\n", dev->name, dev->value);

    /* 使用对象... */

    /* 删除对象（会自动调用 free） */
    mr_object_del((mr_object_t *)dev);
    printf("Device deleted (memory freed)\n");
}
```

**预期输出**：

```
Device created: dev0, value=100
Static device released: dev0
Device deleted
Device created: dev1, value=200
Dynamic device released: dev1
Device deleted (memory freed)
```

#### 案例 2：对象共享与引用计数

```c
#include <mrX/kernel/mr_object.h>
#include <stdio.h>

typedef struct {
    mr_object_t parent;
    int data;
} shared_resource_t;

static void shared_resource_statik_release(mr_object_t *obj) {
    shared_resource_t *res = (shared_resource_t *)obj;
    printf("Resource released, data=%d\n", res->data);
}

MR_CLAZZES_DEFINE(shared_resource, shared_resource_statik_release, NULL);

void example_shared_resource(void) {
    shared_resource_t resource;

    /* 初始化资源（引用计数 = 1） */
    mr_object_init((mr_object_t *)&resource, MR_CLAZZES_STATIK(MR_CLAZZES_FIND(shared_resource)));
    resource.data = 42;
    printf("Resource initialized, count=1\n");

    /* 模块 A 获取引用 */
    mr_object_t *ref_a = mr_object_get((mr_object_t *)&resource);
    if (ref_a) {
        printf("Module A acquired, count=2\n");
    }

    /* 模块 B 获取引用 */
    mr_object_t *ref_b = mr_object_get((mr_object_t *)&resource);
    if (ref_b) {
        printf("Module B acquired, count=3\n");
    }

    /* 模块 A 释放引用 */
    if (ref_a) {
        mr_object_put(ref_a);
        printf("Module A released, count=2\n");
    }

    /* 模块 B 释放引用 */
    if (ref_b) {
        mr_object_put(ref_b);
        printf("Module B released, count=1\n");
    }

    /* 主模块释放最后的引用（触发释放） */
    mr_object_del((mr_object_t *)&resource);
    printf("Main module released, count=0\n");
}
```

**预期输出**：

```
Resource initialized, count=1
Module A acquired, count=2
Module B acquired, count=3
Module A released, count=2
Module B released, count=1
Resource released, data=42
Main module released, count=0
```

#### 案例 3：类型检查和 RTTI

```c
#include <mrX/kernel/mr_object.h>
#include <stdio.h>

typedef struct {
    mr_object_t parent;
    int id;
} device_a_t;

typedef struct {
    mr_object_t parent;
    int id;
} device_b_t;

static void device_a_statik_release(mr_object_t *obj) {
    printf("Device A released\n");
}

static void device_b_statik_release(mr_object_t *obj) {
    printf("Device B released\n");
}

MR_CLAZZES_DEFINE(device_a, device_a_statik_release, NULL);
MR_CLAZZES_DEFINE(device_b, device_b_statik_release, NULL);

/* 通用设备处理函数 */
void process_device(mr_object_t *obj) {
    /* 类型检查 */
    if (MR_OBJECT_IS_CLAZZES(obj, device_a)) {
        device_a_t *dev = (device_a_t *)obj;
        printf("Processing Device A, id=%d\n", dev->id);
    } else if (MR_OBJECT_IS_CLAZZES(obj, device_b)) {
        device_b_t *dev = (device_b_t *)obj;
        printf("Processing Device B, id=%d\n", dev->id);
    } else {
        printf("Unknown device type\n");
    }
}

void example_type_check(void) {
    device_a_t dev_a;
    device_b_t dev_b;

    mr_object_init((mr_object_t *)&dev_a, MR_CLAZZES_STATIK(MR_CLAZZES_FIND(device_a)));
    dev_a.id = 1;

    mr_object_init((mr_object_t *)&dev_b, MR_CLAZZES_STATIK(MR_CLAZZES_FIND(device_b)));
    dev_b.id = 2;

    /* 通过父类指针处理不同类型的设备 */
    process_device((mr_object_t *)&dev_a);
    process_device((mr_object_t *)&dev_b);

    mr_object_del((mr_object_t *)&dev_a);
    mr_object_del((mr_object_t *)&dev_b);
}
```

**预期输出**：

```
Processing Device A, id=1
Processing Device B, id=2
Device A released
Device B released
```

#### 案例 4：跨模块类型导出与导入

```c
/* module_a.h */
#ifndef MODULE_A_H
#define MODULE_A_H

#include <mrX/kernel/mr_object.h>

/* 导出类型信息 */
MR_CLAZZES_EXPORT(module_a_object);

/* 创建模块 A 对象的函数 */
mr_object_t *module_a_create_object(void);

#endif /* MODULE_A_H */

/* ========== */

/* module_a.c */
#include "module_a.h"
#include <stdlib.h>

typedef struct {
    mr_object_t parent;
    int data;
} module_a_object_t;

static void module_a_object_statik_release(mr_object_t *obj) {
    printf("Module A object released\n");
}

static void module_a_object_dynamic_release(mr_object_t *obj) {
    module_a_object_t *obj_a = (module_a_object_t *)obj;
    printf("Module A object released (dynamic)\n");
    free(obj_a);
}

/* 定义类型信息 */
MR_CLAZZES_DEFINE(module_a_object,
                  module_a_object_statik_release,
                  module_a_object_dynamic_release);

mr_object_t *module_a_create_object(void) {
    module_a_object_t *obj = malloc(sizeof(module_a_object_t));
    if (!obj) {
        return NULL;
    }

    mr_object_init((mr_object_t *)obj,
                   MR_CLAZZES_DYNAMIC(MR_CLAZZES_FIND(module_a_object)));
    obj->data = 100;

    return (mr_object_t *)obj;
}

/* ========== */

/* main.c */
#include <mrX/kernel/mr_object.h>
#include "module_a.h"
#include <stdio.h>

int main(void) {
    mr_object_t *obj = module_a_create_object();
    if (!obj) {
        return -1;
    }

    /* 类型检查（跨编译单元） */
    if (MR_OBJECT_IS_CLAZZES(obj, module_a_object)) {
        printf("Object is from module A\n");
    }

    mr_object_del(obj);
    return 0;
}
```

**说明**：

- `MR_CLAZZES_EXPORT` 在头文件中声明类型信息
- `MR_CLAZZES_DEFINE` 在源文件中定义类型信息
- `MR_CLAZZES_IMPORT`（等同于 `MR_CLAZZES_EXPORT`）在其他模块中使用
- 所有工作都在编译期完成，没有运行时开销

---

## 3. 设计思路

### 3.1 设计目标

object 模块的设计目标包括：

1. **在 C 语言中实现面向对象**：不依赖 C++ 编译器，保持 C 语言的简洁性和可移植性
2. **轻量级**：最小化运行时开销，适合资源受限的嵌入式系统
3. **类型安全**：提供类型检查机制，防止类型混淆和误用
4. **自动内存管理**：通过引用计数自动管理对象生命周期
5. **支持多种分配方式**：同时支持栈分配、静态分配和堆分配
6. **跨编译单元类型检查**：通过编译期宏实现类型的公开和导入

### 3.2 架构设计

#### C 语言实现面向对象的技术

object 模块通过以下技术在 C 语言中实现面向对象：

##### 1. 通过结构体嵌套实现继承

```c
typedef struct {
    mr_object_t parent;    /* 父类 */
    int value;             /* 派生类特有成员 */
} derived_object_t;
```

**原理**：

- C 语言标准规定：结构体的第一个成员的地址与结构体本身的地址相同
- 因此可以将 `derived_object_t *` 安全地转换为 `mr_object_t *`
- 这实现了"继承"关系

**示例**：

```c
derived_object_t derived;
mr_object_t *parent = (mr_object_t *)&derived;  /* 向上转型 */
derived_object_t *derived = (derived_object_t *)parent;  /* 向下转型 */
```

##### 2. 通过函数指针实现多态

```c
typedef struct mr_clazz {
    const char *name;
    mr_object_release_t *release;  /* 虚函数 */
} mr_clazz_t;
```

**原理**：

- 每个类型都有自己的 `release` 函数
- 通过 `clazz->release(obj)` 实现动态调用
- 不同类型的对象会调用不同的 `release` 函数

**优势**：

- 运行时多态，同一接口不同实现
- 开销极小（一次函数指针调用）

##### 3. 通过 clazz 指针实现 RTTI

```c
typedef struct mr_object {
    mr_ref_t magic_ref;
    const mr_clazz_t *clazz;  /* 类型信息 */
} mr_object_t;
```

**功能**：

- 每个对象都携带类型信息
- 可以在运行时查询对象的类型
- 支持 `MR_OBJECT_IS_CLAZZES` 等类型检查

#### 为什么叫 clazz 而不是 class？

这个命名选择有几个考虑：

1. **避免关键字冲突**：`class` 是 C++ 的关键字，在 C/C++ 混合编程时可能造成问题
2. **变量命名友好**：`clazz` 可以作为变量名（如 `my_clazz`），而 `class` 不行
3. **代码趣味性**：`class` 和 `clazz` 发音相近，但写法更有趣
4. **独特性**：在代码中一眼就能识别这是 mrX 框架的类型系统

#### static 和 dynamic clazz 的设计

每个对象类型需要定义两个 clazz：`statik` 和 `dynamic`。

**设计理由**：

1. **支持不同的内存分配方式**：
    - **statik**：用于栈上或静态分配的对象
    - **dynamic**：用于堆上分配（malloc）的对象

2. **不同的释放行为**：
    - `statik` 的 release 函数：只执行清理操作（如关闭文件、释放锁）
    - `dynamic` 的 release 函数：先执行清理操作，再调用 `free()` 释放内存

3. **类型安全和错误预防**：
    - 防止对栈上对象调用 `free()`
    - 防止对堆上对象忘记调用 `free()`

**示例**：

```c
/* 栈上分配 */
my_device_t dev;
mr_object_init((mr_object_t *)&dev, MR_CLAZZES_STATIK(MR_CLAZZES_FIND(my_device)));
/* ... 使用对象 ... */
mr_object_del((mr_object_t *)&dev);  /* 只调用 release 函数，不释放内存 */

/* 堆上分配 */
my_device_t *dev = malloc(sizeof(my_device_t));
mr_object_init((mr_object_t *)dev, MR_CLAZZES_DYNAMIC(MR_CLAZZES_FIND(my_device)));
/* ... 使用对象 ... */
mr_object_del((mr_object_t *)dev);  /* 调用 release 函数，然后 free(dev) */
```

#### 跨编译单元类型检查

object 模块提供了一套完整的类型导出和导入机制：

```c
/* 定义类型（在 .c 文件中） */
MR_CLAZZES_DEFINE(my_type, statik_release, dynamic_release);

/* 导出类型（在 .h 文件中） */
MR_CLAZZES_EXPORT(my_type);

/* 导入类型（在其他 .c 文件中） */
MR_CLAZZES_IMPORT(my_type);

/* 查找类型 */
const mr_clazzes_t *clazzes = MR_CLAZZES_FIND(my_type);
```

**设计理念**：

- **编译期完成**：所有工作都在编译期通过宏展开完成
- **零运行时开销**：不需要运行时查找或注册
- **类型安全**：编译器会检查类型的一致性

**实现原理**：

- `MR_CLAZZES_DEFINE` 定义了一个具有特定名称的全局变量：`__mr_clazzes_##name`
- `MR_CLAZZES_FIND` 通过名称拼接找到这个全局变量
- `MR_CLAZZES_EXPORT/IMPORT` 是 `extern` 声明，用于跨编译单元访问

### 3.3 实现要点

#### 对象初始化的防御性检查

```c
mr_err_t mr_object_init(mr_object_t *obj, const mr_clazz_t *clazz) {
    if ((!obj) || MR_OBJECT_IS_INITED(obj)) {
        return -MR_EINVAL;  /* 防止重复初始化 */
    }
    obj->clazz = clazz;
    mr_ref_init(&obj->magic_ref);
    return 0;
}
```

**设计理由**：

1. **防止重复初始化**：重复初始化会导致引用计数错误
2. **对象池复用**：如果需要复用对象，必须先调用 `mr_object_del` 释放旧对象
3. **状态一致性**：确保对象始终处于确定的状态

#### release 函数的调用机制

```c
static void object_ref_release(mr_ref_t *ref) {
    const mr_clazz_t *clazz;
    mr_object_t *obj;

    /* 通过 ref 指针找到对象 */
    obj = MR_CONTAINER_OF(ref, mr_object_t, magic_ref);
    clazz = obj->clazz;

    /* 调用类型的 release 函数 */
    if (clazz && clazz->release) {
        clazz->release(obj);
    }
}
```

**设计要点**：

1. **通过 `MR_CONTAINER_OF` 找到对象**：从 `mr_ref_t *` 反向获取 `mr_object_t *`
2. **通过 clazz 调用正确的 release 函数**：实现多态
3. **空指针检查**：确保安全

**使用注意事项**：

- release 函数在引用计数归零时**同步调用**
- release 函数中不应执行耗时操作或可能阻塞的调用
- 如果需要异步释放，可以结合工作队列使用

#### 引用计数的继承

object 模块完全基于 ref 模块实现，继承了 ref 模块的所有特性：

- **线程安全**：所有操作都是原子操作
- **魔法数字保护**：防止对未初始化或已释放对象的误操作
- **溢出检测**：帮助发现循环引用等严重问题

### 3.4 设计权衡

#### 面向对象的实现方式选择

**方案对比**：

| 方案        | 优点         | 缺点                    |
|-----------|------------|-----------------------|
| 使用 C++    | 完整的面向对象支持  | 增加依赖、代码体积大、某些嵌入式平台不支持 |
| object 模块 | 轻量、纯 C、可移植 | 需要手动管理、功能有限           |

**选择 object 模块的理由**：

- mrX 面向资源受限的嵌入式系统
- 需要完全可控的代码体积和运行时开销
- 需要与现有的 C 代码无缝集成

#### 类型系统的复杂度

object 模块提供了 RTTI 功能，但不是完整的类型系统：

**提供的功能**：

- 类型信息查询（`MR_OBJECT_CLAZZ`）
- 类型相等性检查（`MR_OBJECT_IS_CLAZZES`）
- 类型继承（通过结构体嵌套）

**未提供的功能**：

- 类型转换检查（向下转型需要手动操作）
- 多重继承（不支持）
- 虚函数表（只有 release 函数是虚函数）

**设计理念**：

- 提供"够用"的类型系统
- 保持简单和高效
- 避免过度设计

#### 内存占用

每个对象的开销：

- 32 位系统：8 字节（mr_ref_t 4 字节 + clazz 指针 4 字节）
- 64 位系统：16 字节（mr_ref_t 4 字节 + clazz 指针 8 字节）

**权衡**：

- 开销很小，大多数场景可以接受
- 换来了自动内存管理和类型安全
- 符合"零开销抽象"的原则

---

## 4. 参考资料

### 4.1 相关模块

- **ref**：引用计数系统，object 模块基于 ref 实现生命周期管理
- **compiler** (libc/compiler)：提供 `MR_CONTAINER_OF` 宏，用于从 mr_object_t 指针获取派生类结构体指针

### 4.2 设计参考

- **Linux kobject**：Linux 内核的设备模型，类似的面向对象设计
- **BSD object**：BSD 系统的对象系统
- **GObject**：GLib 的对象系统，更完整的 C 语言面向对象实现

### 4.3 延伸阅读

- **C 语言中的面向对象编程**：理解如何在 C 中实现 OOP
- **结构体布局和内存对齐**：理解为什么第一个成员可以安全转型
- **函数指针和回调**：理解虚函数的实现机制
- **引用计数与垃圾回收**：理解自动内存管理的不同方案

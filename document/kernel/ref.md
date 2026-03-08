# ref - 引用计数系统

## 1. 模块概述

### 1.1 功能介绍

引用计数（Reference Counting）是一种自动管理资源生命周期的技术。ref 模块提供了一个轻量级、线程安全的引用计数实现，主要用于追踪资源被引用的次数，当引用计数归零时自动释放资源。

在嵌入式系统中，很多资源（如对象、设备、缓冲区等）需要被多个模块或线程共享访问。如果没有统一的生命周期管理机制，很容易出现：

- **内存泄漏**：资源使用完毕后忘记释放
- **悬空指针**：资源已被释放但仍有指针指向它
- **重复释放**：同一资源被释放多次导致系统崩溃

ref 模块通过引用计数机制解决了这些问题。每个资源维护一个计数器：

- 当资源被引用时（`mr_ref_get`），计数器加 1
- 当资源使用完毕时（`mr_ref_put`），计数器减 1
- 当计数器归零时，说明没有任何地方再使用该资源，自动调用释放函数回收资源

### 1.2 主要特性

- **线程安全**：基于原子操作实现，支持多线程和多中断环境
- **轻量级设计**：仅使用一个原子变量存储引用计数和状态信息
- **魔法数字保护**：通过特定的魔法数字标识资源状态，防止对未初始化或已释放资源的误操作
- **溢出检测**：引用计数溢出时会触发断言，帮助开发者发现循环引用等严重问题
- **灵活的配置**：支持 8 位和 16 位两种引用计数模式

### 1.3 适用场景

ref 模块适用于以下场景：

- **共享资源的生命周期管理**：多个模块或线程需要访问同一资源
- **对象系统**：作为面向对象框架的基础设施，管理对象的引用和释放
- **设备驱动**：管理设备资源的共享访问
- **缓冲区管理**：追踪缓冲区的使用者数量

**不适用场景**：

- 单一所有者的资源：简单场景下直接使用 malloc/free 即可
- 需要复杂所有权语义的场景：如 RAII、unique_ptr 等概念

---

## 2. 使用指南

### 2.1 核心数据结构

#### mr_ref_t

```c
typedef volatile mr_atomic_t mr_ref_t;
```

`mr_ref_t` 是引用计数的核心类型，本质上是一个原子整数。它通过特殊的编码方式同时存储了：

- **魔法数字**：高 16 位（或 24 位），用于标识资源是否已初始化
- **引用计数**：低 8 位或 16 位，记录当前被引用的次数

**编码格式**：

- 8 位计数模式（默认）：`0x6d725800 | count`（0x6d7258 = "mrX" 的 ASCII 编码）
- 16 位计数模式：`0x6d720000 | count`（0x6d72 = "mr" 的 ASCII 编码）

#### mr_ref_release_t

```c
typedef void(mr_ref_release_t)(mr_ref_t *ref);
```

释放函数的回调类型。当引用计数归零时，ref 模块会自动调用此函数来执行资源清理工作。

**参数说明**：

- `ref`：指向引用计数的指针，通常通过 `MR_CONTAINER_OF` 宏获取包含它的结构体

### 2.2 主要接口说明

#### 初始化接口

##### MR_REF_INIT()

```c
#define MR_REF_INIT() MR_ATOMIC_INIT(MR_REF_MAGIC | 0x0001U)
```

**功能**：静态初始化引用计数

**示例**：

```c
static mr_ref_t ref = MR_REF_INIT();
```

**注意事项**：

- 只能用于静态变量或全局变量
- 初始化后引用计数为 1，表示资源已创建并存活

##### mr_ref_init()

```c
void mr_ref_init(mr_ref_t *ref);
```

**功能**：动态初始化引用计数

**参数**：

- `ref`：指向引用计数变量的指针

**示例**：

```c
mr_ref_t ref;
mr_ref_init(&ref);
```

**注意事项**：

- 必须在使用其他 ref 函数之前调用
- 初始化后引用计数为 1

#### 状态查询接口

##### MR_REF_IS_INITED()

```c
#define MR_REF_IS_INITED(_ref) (((_ref) & MR_REF_MAGIC_MASK) == MR_REF_MAGIC)
```

**功能**：检查引用计数是否已初始化

**参数**：

- `_ref`：引用计数变量的值

**返回值**：

- `MR_TRUE`：已初始化
- `MR_FALSE`：未初始化或已释放

**使用场景**：

- 调试时查看引用是否初始化
- 防止对未初始化资源的误操作

##### MR_REF_COUNT()

```c
#define MR_REF_COUNT(_ref) ((_ref) & (~MR_REF_MAGIC))
```

**功能**：获取当前引用计数值

**参数**：

- `_ref`：引用计数变量的值

**返回值**：

- 当前的引用计数（不含魔法数字部分）

**使用场景**：

- 调试时查看引用计数
- 日志输出

#### 引用操作接口

##### mr_ref_get()

```c
mr_bool_t mr_ref_get(mr_ref_t *ref);
```

**功能**：增加引用计数（获取资源的引用）

**参数**：

- `ref`：指向引用计数变量的指针

**返回值**：

- `MR_TRUE`：成功增加引用计数
- `MR_FALSE`：资源已释放或引用计数为 0，无法获取

**实现细节**：

- 使用 CAS（Compare-And-Swap）原子操作
- 如果资源未初始化或已释放（魔法数字不匹配），直接返回失败
- 如果引用计数已归零，返回失败
- 否则将引用计数加 1

**注意事项**：

- **每次成功调用 `mr_ref_get` 后，都必须配套调用一次 `mr_ref_put`**
- 失败时不增加引用计数，也不需要调用 `mr_ref_put`

##### mr_ref_put()

```c
mr_bool_t mr_ref_put(mr_ref_t *ref, mr_ref_release_t *release);
```

**功能**：减少引用计数（释放资源的引用）

**参数**：

- `ref`：指向引用计数变量的指针
- `release`：释放函数指针，当引用计数归零时被调用

**返回值**：

- `MR_TRUE`：引用计数归零，资源已被释放
- `MR_FALSE`：引用计数仍大于 0，资源仍存活

**实现细节**：

- 使用原子减法操作
- 当引用计数从 1 减到 0 时：
    1. 清除魔法数字（盖上"死亡印章"）
    2. 调用 `release` 回调函数
    3. 返回 `MR_TRUE`
- 之后再次调用会因魔法数字不匹配而直接返回 `MR_FALSE`

**注意事项**：

- 必须与之前的 `mr_ref_get` 配对使用
- `release` 可以为 `MR_NULL`，此时不调用任何函数
- 释放后的资源不应再被访问

### 2.3 典型应用案例

#### 案例 1：基础用法 - 简单资源管理

```c
#include <mrX/kernel/mr_ref.h>

/* 定义资源结构 */
typedef struct {
    mr_ref_t ref;
    int data;
} my_resource_t;

/* 释放函数 */
static void my_resource_release(mr_ref_t *ref) {
    my_resource_t *res = MR_CONTAINER_OF(ref, my_resource_t, ref);

    /* 执行清理工作 */
    printf("Releasing resource, data=%d\n", res->data);

    /* 如果有动态分配的内存，在这里释放 */
    // free(res);
}

/* 使用示例 */
void example_basic_usage(void) {
    my_resource_t res;

    /* 1. 初始化资源（引用计数 = 1） */
    mr_ref_init(&res.ref);
    res.data = 42;

    printf("Initial count: %d\n", MR_REF_COUNT(res.ref));  // 输出: 1

    /* 2. 获取引用（引用计数 + 1） */
    if (mr_ref_get(&res.ref)) {
        printf("After get: %d\n", MR_REF_COUNT(res.ref));  // 输出: 2

        /* 使用资源... */
        printf("Resource data: %d\n", res.data);  // 输出: 42

        /* 3. 释放引用（引用计数 - 1） */
        mr_ref_put(&res.ref, my_resource_release);
        printf("After put: %d\n", MR_REF_COUNT(res.ref));  // 输出: 1
    }

    /* 4. 最后释放（引用计数归零，调用释放函数） */
    if (mr_ref_put(&res.ref, my_resource_release)) {
        printf("Resource released\n");  // 输出: Resource released
        printf("Final count: %d\n", MR_REF_COUNT(res.ref));  // 输出: 0
    }
}
```

**预期输出**：

```
Initial count: 1
After get: 2
Resource data: 42
After put: 1
Releasing resource, data=42
Resource released
Final count: 0
```

#### 案例 2：多线程共享资源

```c
#include <mrX/kernel/mr_ref.h>

/* 共享缓冲区 */
typedef struct {
    mr_ref_t ref;
    char buffer[256];
    size_t size;
} shared_buffer_t;

/* 释放函数 */
static void shared_buffer_release(mr_ref_t *ref) {
    shared_buffer_t *buf = MR_CONTAINER_OF(ref, shared_buffer_t, ref);
    printf("Buffer freed, size=%zu\n", buf->size);
    // free(buf);  /* 如果是动态分配 */
}

/* 线程 1：生产者 */
void *thread_producer(void *arg) {
    shared_buffer_t *buf = (shared_buffer_t *)arg;

    /* 生产者已经持有一个引用（初始化时的引用） */
    snprintf(buf->buffer, sizeof(buf->buffer), "Hello from producer");
    buf->size = strlen(buf->buffer);
    printf("Producer: wrote data\n");

    /* 完成后释放引用 */
    mr_ref_put(&buf->ref, shared_buffer_release);
    return NULL;
}

/* 线程 2：消费者 */
void *thread_consumer(void *arg) {
    shared_buffer_t *buf = (shared_buffer_t *)arg;

    /* 尝试获取引用 */
    if (!mr_ref_get(&buf->ref)) {
        printf("Consumer: buffer already released\n");
        return NULL;
    }

    /* 读取数据 */
    printf("Consumer: read data: %s\n", buf->buffer);

    /* 完成后释放引用 */
    mr_ref_put(&buf->ref, shared_buffer_release);
    return NULL;
}

/* 主函数 */
void example_multithread(void) {
    shared_buffer_t *buf = malloc(sizeof(shared_buffer_t));
    mr_ref_init(&buf->ref);
    buf->size = 0;

    /* 启动两个线程 */
    pthread_t producer, consumer;
    pthread_create(&producer, NULL, thread_producer, buf);
    pthread_create(&consumer, NULL, thread_consumer, buf);

    /* 等待线程完成 */
    pthread_join(producer, NULL);
    pthread_join(consumer, NULL);

    /* 此时引用计数应该归零，缓冲区已被释放 */
    printf("All threads finished\n");
}
```

#### 案例 3：防止重复释放和悬空指针

```c
#include <mrX/kernel/mr_ref.h>

typedef struct {
    mr_ref_t ref;
    int value;
} safe_object_t;

static void safe_object_release(mr_ref_t *ref) {
    safe_object_t *obj = MR_CONTAINER_OF(ref, safe_object_t, ref);
    printf("Object released, value=%d\n", obj->value);
}

void example_safety(void) {
    safe_object_t obj;
    mr_ref_init(&obj.ref);
    obj.value = 100;

    /* 第一次 put：成功释放 */
    printf("First put:\n");
    if (mr_ref_put(&obj.ref, safe_object_release)) {
        printf("  -> Released\n");
    }

    /* 第二次 put：安全地返回失败 */
    printf("Second put:\n");
    if (!mr_ref_put(&obj.ref, safe_object_release)) {
        printf("  -> Already released, safe\n");
    }

    /* 尝试 get：失败（因为已被释放） */
    printf("Try get after release:\n");
    if (!mr_ref_get(&obj.ref)) {
        printf("  -> Get failed (expected)\n");
    }

    /* 检查状态 */
    printf("Is inited: %d\n", MR_REF_IS_INITED(obj.ref));  // 0
    printf("Count: %d\n", MR_REF_COUNT(obj.ref));  // 0
}
```

**预期输出**：

```
First put:
Object released, value=100
  -> Released
Second put:
  -> Already released, safe
Try get after release:
  -> Get failed (expected)
Is inited: 0
Count: 0
```

#### 案例 4：检测循环引用（溢出）

```c
#include <mrX/kernel/mr_ref.h>

typedef struct {
    mr_ref_t ref;
    struct object *peer;  /* 指向另一个对象 */
} object;

static void object_release(mr_ref_t *ref) {
    object *obj = MR_CONTAINER_OF(ref, object, ref);
    printf("Object released\n");
}

void example_circular_reference(void) {
    object a, b;

    mr_ref_init(&a.ref);
    mr_ref_init(&b.ref);
    a.peer = &b;
    b.peer = &a;

    /* 创建循环引用：a 引用 b，b 引用 a */
    mr_ref_get(&b.ref);  /* a 持有 b 的引用 */
    mr_ref_get(&a.ref);  /* b 持有 a 的引用 */

    /* 尝试释放：永远不会释放成功！ */
    printf("Trying to release a...\n");
    mr_ref_put(&a.ref, object_release);  // 计数从 2 降到 1

    printf("Trying to release b...\n");
    mr_ref_put(&b.ref, object_release);  // 计数从 2 降到 1

    /* 此时 a 和 b 的引用计数都是 1，永远不会归零 */

    /* 在测试模式下，如果继续 get 会触发溢出断言 */
    /* for (int i = 0; i < 300; i++) { */
    /*     mr_ref_get(&a.ref);  /\* 触发断言：检测到溢出 *\/ */
    /* } */
}
```

**说明**：

- 这个例子展示了循环引用的危害
- 在实际应用中，需要使用弱引用或其他机制打破循环
- ref 模块的溢出检测可以帮助开发者及早发现这类问题

---

## 3. 设计思路

### 3.1 设计目标

ref 模块的设计目标包括：

1. **轻量级**：嵌入式系统资源有限，引用计数机制本身不能占用太多资源
2. **线程安全**：必须支持多线程和多中断环境下的并发访问
3. **安全性**：防止对未初始化或已释放资源的误操作
4. **可调试性**：提供错误检测机制（如溢出检测）帮助开发者发现问题
5. **零开销**：在大多数场景下，原子操作的开销应该是可接受的

### 3.2 架构设计

#### 单一原子变量设计

ref 模块最核心的设计决策是**只用一个原子变量同时存储状态和计数**。

**传统方法**：

```c
struct ref {
    atomic_t count;      /* 引用计数 */
    bool initialized;    /* 是否已初始化 */
    bool released;       /* 是否已释放 */
};
```

**ref 模块的方法**：

```c
typedef atomic_t mr_ref_t;  /* 一个变量搞定一切 */
```

**优势**：

- **内存占用小**：只需 4 字节（32 位系统）
- **操作高效**：一次原子操作即可完成状态检查和计数更新
- **缓存友好**：单个变量不会出现缓存一致性问题

**代价**：

- 引用计数的最大值受限（8 位模式最大 255，16 位模式最大 65535）
- 但实际上，正常的引用计数深度很少会超过 10

#### 魔法数字编码

魔法数字采用 ASCII 编码：

- 8 位模式：`0x6d725800`（"mrX" = 0x6D 0x72 0x58）
- 16 位模式：`0x6d720000`（"mr" = 0x6D 0x72）

**设计理由**：

1. **易于识别**：在调试器中查看内存时，可以立即识别出这是 mrX 的引用计数
2. **防误操作**：未初始化的变量通常为 0，不太可能正好等于魔法数字
3. **死亡标识**：清除魔法数字相当于给对象盖上"死亡印章"，方便调试和追溯

#### 初始化计数为 1

为什么初始化后引用计数是 1 而不是 0？

**核心思想**：**对象创建出来就存活了，自然应该持有 1 个引用。**

- 初始化时计数为 1：表示对象的创建者持有一个引用
- 归零意味着对象死亡：没有任何地方再需要这个对象了
- 这种语义与 C++ 的 `shared_ptr` 一致

**如果初始化为 0 会有什么问题？**

```c
/* 假设初始化为 0 */
mr_ref_init(&obj.ref);  /* count = 0 */

/* 创建者需要显式 get */
mr_ref_get(&obj.ref);  /* count = 1 */

/* 使用完毕 */
mr_ref_put(&obj.ref, release);  /* count = 0，释放 */

/* 如果忘记 get，对象会被立即释放！*/
```

初始化为 1 避免了这个陷阱。

#### 死亡印章机制

当引用计数归零时，ref 模块会**清除魔法数字**：

```c
/* 标记为已释放 */
mr_atomic_fetch_and(ref, ~MR_REF_MAGIC);
```

**作用**：

1. **防止重复释放**：之后再调用 `mr_ref_put` 会因魔法数字不匹配而直接返回失败
2. **防止误用**：`mr_ref_get` 也会失败，防止获取已释放对象的引用
3. **调试追溯**：在调试器中看到魔法数字被清除，可以立即判断对象已释放

### 3.3 实现要点

#### mr_ref_get 的 CAS 实现

```c
MR_INLINE mr_bool_t mr_ref_get(mr_ref_t *ref) {
    mr_atomic_t last;

    last = mr_atomic_load(ref);  /* 读取当前值 */

    do {
        /* 检查状态 */
        if ((!MR_REF_IS_INITED(last)) || (MR_REF_COUNT(last) == 0)) {
            return MR_FALSE;  /* 未初始化或已释放 */
        }

        /* CAS 尝试增加计数 */
    } while (!mr_atomic_compare_exchange(ref, &last, last + 1));

    return MR_TRUE;
}
```

**为什么需要 CAS？**

假设使用简单的"读取-修改-写入"：

```c
/* 错误的实现 */
int count = MR_REF_COUNT(*ref);
count++;
*ref = MR_REF_MAGIC | count;  /* 竞争条件！*/
```

多线程环境下会出现竞争条件：

- 线程 A 读取 count = 5
- 线程 B 读取 count = 5
- 线程 A 写入 count = 6
- 线程 B 写入 count = 6（应该是 7！）

CAS 操作保证了原子性：

- 只有当 `*ref` 的值确实等于 `last` 时，才会更新为 `last + 1`
- 如果中间被其他线程修改，CAS 会失败并重新尝试

#### mr_ref_put 的释放时序

```c
MR_INLINE mr_bool_t mr_ref_put(mr_ref_t *ref, mr_ref_release_t *release) {
    mr_atomic_t last;

    last = mr_atomic_fetch_sub(ref, 1);  /* 原子减法 */

    /* 检查是否从 1 减到 0 */
    if (MR_REF_IS_INITED(last) && (MR_REF_COUNT(last) == 1)
        && (release != MR_NULL)) {
        /* 先清除魔法数字 */
        mr_atomic_fetch_and(ref, ~MR_REF_MAGIC);

        /* 再调用释放函数 */
        release(ref);
        return MR_TRUE;
    }

    return MR_FALSE;
}
```

**关键点**：

1. 使用 `fetch_sub` 获取减少前的值，可以判断是否从 1 减到 0
2. 先清除魔法数字（盖上死亡印章），再调用释放函数
3. 这样在释放函数执行期间，其他线程无法再获取引用

### 3.4 设计权衡

#### 8 位 vs 16 位引用计数

**8 位模式（默认）**：

- 最大引用计数：255
- 魔法数字：`0x6d725800`（"mrX"）
- 适用场景：大多数应用

**16 位模式**（定义 `MR_USE_REF_LONG_COUNT`）：

- 最大引用计数：65535
- 魔法数字：`0x6d720000`（"mr"）
- 适用场景：特殊情况下的深层引用

**权衡**：

- 8 位已经足够：正常情况下引用计数深度很少超过 10
- 16 位提供更大安全裕度：但牺牲了 1 字节的内存
- 默认选择 8 位：体现了嵌入式系统"够用就好"的设计哲学

#### 溢出检测的价值

引用计数溢出几乎总是意味着**循环引用**：

```
A -> B -> C -> A
```

每个对象的引用计数永远无法归零，导致内存泄漏。

ref 模块通过断言检测溢出：

```c
MR_ASSERT(last != MR_REF_OVERFLOW);
```

**价值**：

- 开发阶段就能发现严重的 BUG
- 避免生产环境中的内存泄漏

**代价**：

- 需要 1 个额外的魔法数字值（`MR_REF_OVERFLOW`）
- 但这是完全值得的

#### 内联函数的选择

ref 模块的核心函数都声明为 `MR_INLINE`：

```c
MR_INLINE void mr_ref_init(mr_ref_t *ref) { ... }
MR_INLINE mr_bool_t mr_ref_get(mr_ref_t *ref) { ... }
MR_INLINE mr_bool_t mr_ref_put(mr_ref_t *ref, mr_ref_release_t *release) { ... }
```

**理由**：

- 引用计数操作非常频繁，函数调用的开销不可忽视
- 内联后编译器可以优化掉不必要的指令
- 代码量很小，内联不会显著增加代码体积

---

## 4. 参考资料

### 4.1 相关模块

- **object**：对象系统模块，基于 ref 实现了完整的面向对象生命周期管理
- **atomic**：原子操作模块，提供 ref 模块所需的底层原子操作支持

### 4.2 参考实现

- **Linux kref**：Linux 内核的引用计数机制，设计理念类似
- **BSD uref**：BSD 系统的引用计数实现

### 4.3 延伸阅读

- **引用计数与垃圾回收**：理解引用计数的优缺点
- **循环引用的解决方案**：弱引用、观察者模式等
- **智能指针**：C++ 的 `shared_ptr` 和 `weak_ptr` 设计思想

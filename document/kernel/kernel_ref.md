# 引用计数

引用计数模块提供轻量级生命周期标记和原子引用计数能力。`mr_ref_t` 将固定 magic 值和计数值保存在同一个原子变量中，初始化后引用计数为 1；调用者通过 `mr_ref_get()` 增加引用，通过 `mr_ref_put()` 释放引用，并在最后一个引用释放时触发释放回调。

该模块只管理引用计数本身，不管理被引用对象的内存布局。对象管理模块中的 `mr_object_t` 以内嵌 `mr_ref_t` 的方式复用该能力。

## 核心数据结构和宏

### 引用计数类型

```c
typedef volatile mr_atomic_t mr_ref_t;
typedef void(mr_ref_release_t)(mr_ref_t *ref);
```

`mr_ref_t` 是原子变量类型，用于保存 magic 和引用计数。`mr_ref_release_t` 是最后一个引用释放时调用的回调类型，回调参数为当前引用计数对象。

### magic 和计数位

```c
#if defined(MR_USE_REF_LONG_COUNT)
#define MR_REF_MAGIC      (0x6d720000U)
#define MR_REF_MAGIC_MASK (0xffff0000U)
#define MR_REF_OVERFLOW   (0x6d730000U)
#else
#define MR_REF_MAGIC      (0x6d725800U)
#define MR_REF_MAGIC_MASK (0xffffff00U)
#define MR_REF_OVERFLOW   (0x6d725900U)
#endif
```

未定义 `MR_USE_REF_LONG_COUNT` 时，引用计数使用低 8 位，magic 使用高 24 位；定义 `MR_USE_REF_LONG_COUNT` 时，引用计数使用低 16 位，magic 使用高 16 位。`MR_REF_OVERFLOW` 用于在 `mr_ref_get()` 检测到溢出标记时触发断言。

### 初始化和读取宏

```c
#define MR_REF_INIT() MR_ATOMIC_INIT(MR_REF_MAGIC | 0x0001U)
#define MR_REF_IS_INITED(_ref) (((_ref) & MR_REF_MAGIC_MASK) == MR_REF_MAGIC)
#define MR_REF_COUNT(_ref) ((_ref) & (~MR_REF_MAGIC))
```

`MR_REF_INIT()` 用于静态初始化，初始计数为 1。`MR_REF_IS_INITED()` 判断 magic 是否有效。`MR_REF_COUNT()` 读取低位计数值。

## 主要接口

### 初始化引用计数

```c
void mr_ref_init(mr_ref_t *ref);
```

`mr_ref_init()` 将 `ref` 设置为 `MR_REF_MAGIC | 1`，赋予一次有效生命周期。该函数不检查旧值，重复初始化会覆盖当前计数状态。

### 获取引用

```c
mr_bool_t mr_ref_get(mr_ref_t *ref);
```

`mr_ref_get()` 先读取当前值，如果 magic 无效或计数为 0，则返回 `MR_FALSE`，表示对象生命周期已经结束或尚未初始化。否则通过 `mr_atomic_compare_exchange()` 将计数加 1，成功后返回 `MR_TRUE`。

当当前值等于 `MR_REF_OVERFLOW` 时，函数中的断言会失败。该行为用于暴露计数溢出，而不是返回普通错误码。

### 释放引用

```c
mr_bool_t mr_ref_put(mr_ref_t *ref, mr_ref_release_t *release);
```

`mr_ref_put()` 使用 `mr_atomic_fetch_sub()` 将计数减 1。如果递减前 magic 有效、计数为 1，且 `release` 非空，则先清除 magic 标记，再调用 `release(ref)`，并返回 `MR_TRUE`。其他情况下返回 `MR_FALSE`。

需要注意，当前实现只有在 `release != MR_NULL` 时才会执行“最后一个引用释放”的分支；如果最后一次 `put` 传入空释放函数，函数不会清除 magic，也不会返回 `MR_TRUE`。

## 使用示例

```c
static void my_release(mr_ref_t *ref) {
    /* 这里释放包含 ref 的对象资源，或标记对象已经释放。 */
}

void example(void) {
    mr_ref_t ref;

    mr_ref_init(&ref);

    if (mr_ref_get(&ref)) {
        /* 使用受 ref 保护的对象。 */
        mr_ref_put(&ref, my_release);
    }

    if (mr_ref_put(&ref, my_release)) {
        /* 最后一个引用已经释放，my_release 已被调用。 */
    }
}
```

测试 `test/kernel/ref.c` 覆盖了静态初始化、动态初始化、连续获取引用、最后一次释放调用回调，以及生命周期结束后 `mr_ref_get()` 返回失败。

## 注意事项

- `mr_ref_t` 只保证引用计数操作使用原子读改写，不自动保护对象其他字段。
- `mr_ref_put()` 不检查计数下溢；对已经释放或未初始化的引用继续 `put` 会继续修改原子值。
- `release` 回调在 magic 被清除后调用，回调内不应再假设 `MR_REF_IS_INITED()` 为真。
- 引用计数位宽由 `MR_USE_REF_LONG_COUNT` 决定，默认低 8 位计数适合小引用数对象。
- 静态初始化使用 `MR_REF_INIT()`，运行时初始化使用 `mr_ref_init()`，二者都会产生初始计数 1。

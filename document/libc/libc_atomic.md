# libc 原子操作

`mr_atomic.h` 提供 mrX 内部使用的整数原子操作。默认实现通过关闭中断保护读改写临界区；启用 `MR_USE_ATOMIC_3RD` 时，具体类型和接口由第三方头文件提供。

## 核心类型和宏

```c
typedef mr_intptr_t mr_atomic_t;
#define MR_ATOMIC_INIT(_val) (_val)
```

默认实现中，`mr_atomic_t` 为可保存指针宽度整数的有符号类型。静态初始化可使用 `MR_ATOMIC_INIT()`。

所有默认原子函数都以内联函数形式定义，并在访问期间调用：

```c
int mask = mr_port_irq_disable();
...
mr_port_irq_enable(mask);
```

## 主要接口

### 初始化和读写

```c
void mr_atomic_init(volatile mr_atomic_t *atomic, mr_atomic_t val);
mr_atomic_t mr_atomic_load(volatile mr_atomic_t *atomic);
void mr_atomic_store(volatile mr_atomic_t *atomic, mr_atomic_t val);
```

`mr_atomic_init()` 直接写入初值。`mr_atomic_load()` 和 `mr_atomic_store()` 在关中断临界区中读取或写入原子变量。

### 交换

```c
mr_atomic_t mr_atomic_exchange(volatile mr_atomic_t *atomic, mr_atomic_t val);
```

该接口将 `atomic` 更新为 `val`，并返回更新前的旧值。

### 比较交换

```c
mr_bool_t mr_atomic_compare_exchange(volatile mr_atomic_t *atomic,
                                     mr_atomic_t *val,
                                     mr_atomic_t des);
```

当 `*atomic == *val` 时写入 `des` 并返回 `MR_TRUE`。否则把当前 `*atomic` 写回 `*val`，返回 `MR_FALSE`。

### fetch 类操作

```c
mr_atomic_t mr_atomic_fetch_add(volatile mr_atomic_t *atomic, mr_atomic_t val);
mr_atomic_t mr_atomic_fetch_sub(volatile mr_atomic_t *atomic, mr_atomic_t val);
mr_atomic_t mr_atomic_fetch_and(volatile mr_atomic_t *atomic, mr_atomic_t val);
mr_atomic_t mr_atomic_fetch_or(volatile mr_atomic_t *atomic, mr_atomic_t val);
mr_atomic_t mr_atomic_fetch_xor(volatile mr_atomic_t *atomic, mr_atomic_t val);
```

这些接口返回修改前的旧值，然后分别执行加、减、按位与、按位或、按位异或。

## 使用示例

```c
#include <mrX/libc/mr_atomic.h>

static volatile mr_atomic_t counter = MR_ATOMIC_INIT(0);

void sample_atomic_usage(void)
{
    mr_atomic_t expect;

    mr_atomic_fetch_add(&counter, 1);

    expect = 1;
    if (mr_atomic_compare_exchange(&counter, &expect, 10) == MR_FALSE) {
        /* expect 已被更新为 counter 当前值。 */
    }
}
```

测试用例覆盖静态初始化、load/store、exchange、compare-exchange 成功和失败路径，以及 fetch-add/sub/and/or/xor 的旧值返回语义。

## 注意事项

- 默认实现依赖 `mr_port_irq_disable()` 和 `mr_port_irq_enable()`，适合通过关中断保护单核或特定移植层临界区的场景。
- `mr_atomic_init()` 本身不关闭中断，通常应在对象发布前调用。
- 默认实现没有显式内存序参数，也不等价于 C11 atomic 的完整跨核内存模型。
- 参数指针必须有效；默认实现不做空指针检查。

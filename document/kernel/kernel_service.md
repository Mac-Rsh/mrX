[**中文**](kernel_service.md) | [**English**](kernel_service_en.md)

---

# 服务宏和基础工具

Service 模块提供内核中通用的宏和小型内联工具函数，包括容器地址换算、数组长度计算、最值和对齐、可变参数宏辅助、临时复合字面量构造，以及奇偶校验计算。该模块不维护运行时状态，也没有对应的
`.c` 实现文件；接口全部在 `mr_service.h` 中以宏或 `MR_INLINE` 函数形式提供。

## 核心宏和函数

### 容器和数组工具

```c
#define MR_CONTAINER_OF(_ptr, _type, _member) \
    ((void *)((char *)(_ptr) - (char *)((void *)&(((_type *)0)->_member))))

#define MR_ARRAY_SIZE(_array) (sizeof(_array) / sizeof((_array)[0]))
```

`MR_CONTAINER_OF()` 根据结构体成员指针计算外层结构体地址。测试用例使用结构体成员 `b` 的地址恢复包含它的结构体对象。

`MR_ARRAY_SIZE()` 返回 C 数组元素个数。该宏应只用于真实数组，不应用于已退化为指针的函数参数。

### 最值和对齐

```c
#define MR_MIN(_a, _b) (((_a) < (_b)) ? (_a) : (_b))
#define MR_MAX(_a, _b) (((_a) > (_b)) ? (_a) : (_b))
#define MR_ALIGN_UP(_x, _ali)   (((_x) + (_ali) - 1) & ~((_ali) - 1))
#define MR_ALIGN_DOWN(_x, _ali) ((_x) & ~((_ali) - 1))
```

`MR_MIN()` 和 `MR_MAX()` 返回两个表达式的较小值或较大值。`MR_ALIGN_UP()` 向上对齐，`MR_ALIGN_DOWN()` 向下对齐。

当前对齐宏使用位掩码计算，要求 `_ali` 适合作为二进制对齐值使用，通常应为 2 的幂。

### 四舍五入除法

```c
#define MR_ROUND_DIV(_a, _b) \
    (((_a) + ((((_a) ^ (_b)) >= 0) ? (_b) : -(_b)) / 2) / (_b))
```

`MR_ROUND_DIV()` 执行整数除法并按除数符号方向补偿半个除数，用于得到四舍五入后的商。

### 临时对象构造

```c
#define MR_MAKE_LOCAL(_type, ...) (&((_type){__VA_ARGS__}))
```

`MR_MAKE_LOCAL()` 使用复合字面量创建当前作用域内的临时对象，并返回对象地址。测试用例覆盖了结构体和整数临时对象。

### 可变参数和宏拼接工具

```c
#define MR_VA_ARGS_NUM(...) /* 返回参数数量，范围 0-63 */
#define MR_STR(_s)          /* 将参数转为字符串 */
#define MR_CAT(...)         /* 拼接多个 token */
#define MR_CAT_SEP(_s, ...) /* 使用分隔前缀拼接多个 token */
#define MR_DE_BRACKET(...)  __VA_ARGS__
#define MR_MACRO_IF(_c, _if, _else) /* 条件展开 */
#define MR_MACRO_IS_USED(_m)        /* 判断宏是否按约定定义为 1 */
```

这些宏主要服务于其他内核头文件中的条件展开、名称生成和配置判断。`MR_MACRO_IF()` 要求 `_if` 和 `_else`
参数以额外括号传入，以避免表达式中逗号破坏宏实参列表；内部通过 `MR_DE_BRACKET` 去除保护括号。

### 奇偶校验

```c
MR_INLINE mr_bool_t mr_parity_even(const void *src, mr_size_t size);
MR_INLINE mr_bool_t mr_parity_odd(const void *src, mr_size_t size);
```

`mr_parity_even()` 统计 `src` 指向的 `size` 字节数据中置位 bit 的数量，置位数量为偶数时返回 `MR_TRUE`。`mr_parity_odd()`
返回偶校验结果的取反，置位数量为奇数时返回 `MR_TRUE`。

测试用例对 16 位整数范围内的多个值计算 bit 奇偶性，并与这两个接口的返回值比较。

## 使用示例

### 通过成员指针获取容器

```c
#include <mrX/kernel/mr_service.h>

typedef struct sample_node {
    int id;
    char state;
} sample_node_t;

void sample_container_usage(void)
{
    sample_node_t node = {1, 'r'};
    char *state = &node.state;
    sample_node_t *owner;

    owner = MR_CONTAINER_OF(state, sample_node_t, state);
    (void)owner;
}
```

### 对齐和校验

```c
#include <mrX/kernel/mr_service.h>

void sample_service_usage(void)
{
    mr_uint32_t value = 0x55U;
    mr_uint32_t aligned;
    mr_bool_t even;

    aligned = MR_ALIGN_UP(13U, 8U);
    even = mr_parity_even(&value, sizeof(value));

    (void)aligned;
    (void)even;
}
```

## 注意事项

- `MR_MIN()`、`MR_MAX()` 等宏可能多次求值实参；不要传入带副作用的表达式，例如 `i++`。
- `MR_ARRAY_SIZE()` 只能用于编译期可见的数组对象，不能用于指针。
- `MR_ALIGN_UP()` 和 `MR_ALIGN_DOWN()` 的 `_ali` 通常应为 2 的幂，否则位掩码对齐结果不具备通用除法对齐语义。
- `MR_MAKE_LOCAL()` 返回的是当前作用域内复合字面量的地址，不应在对象生命周期结束后保存或异步使用。
- `MR_CONTAINER_OF()` 依赖成员确实属于指定容器类型；传入不匹配的指针会得到无效地址。

[**中文**](kernel_listify.md) | [**English**](kernel_listify_en.md)

---

# 宏序列展开

`mr_listify` 模块提供预处理期的重复展开能力。调用者给定展开次数、分隔符、回调宏和附加参数后，`MR_LISTIFY()` 会生成按索引递增的一组宏调用。

该模块主要服务于静态初始化和重复声明场景。例如 `mr_list` 的链表数组静态初始化通过 `MR_LISTIFY()` 为每个数组下标生成一个
`MR_LIST_INIT()` 初始化项。

## 核心宏

### 普通序列展开

```c
#define MR_LISTIFY(_n, _s, _f, ...) \
    MR_CAT(__MR_LISTIFY_, _n)(_s, _f, ##__VA_ARGS__)
```

`MR_LISTIFY()` 的参数含义如下：

| 参数    | 含义                                        |
|-------|-------------------------------------------|
| `_n`  | 展开次数，生成索引 `0` 到 `_n - 1`。                 |
| `_s`  | 每个展开项之间的分隔符，使用时通过 `MR_DE_BRACKET _s` 去括号。 |
| `_f`  | 被调用的宏，调用形式为 `_f(index, ...)`。             |
| `...` | 传给 `_f` 的附加参数。                            |

头文件中定义了 `__MR_LISTIFY_0` 到 `__MR_LISTIFY_4096`。`__MR_LISTIFY_0` 不生成内容，`__MR_LISTIFY_1` 生成 `_f(0, ...)`
，后续版本以前一个版本为基础追加一项。

### 嵌套序列展开

```c
#define MR_LISTIFY_NEST(_n, _s, _f, ...) \
    MR_CAT(__MR_LISTIFY_NEST_, _n)(_s, _f, ##__VA_ARGS__)
```

`MR_LISTIFY_NEST()` 使用与 `MR_LISTIFY()` 相同的参数形式，但其内部宏名为 `__MR_LISTIFY_NEST_x`，当前头文件定义到
`__MR_LISTIFY_NEST_255`。它用于需要与普通 `MR_LISTIFY()` 分开递归层级的宏展开场景。

## 主要接口

### 生成普通展开序列

```c
MR_LISTIFY(_n, _s, _f, ...)
```

当 `_n` 为 `3`，分隔符为 `(, )`，回调宏为 `ITEM` 时，展开结果等价于：

```c
ITEM(0, ...), ITEM(1, ...), ITEM(2, ...)
```

如果 `_n` 为 `0`，不会生成任何项。

### 生成嵌套展开序列

```c
MR_LISTIFY_NEST(_n, _s, _f, ...)
```

该接口同样按 `0` 到 `_n - 1` 的顺序调用 `_f(index, ...)`，但使用独立的 `__MR_LISTIFY_NEST_x`
宏族，适合在宏回调内部再次使用普通序列展开时降低命名冲突风险。

## 使用示例

`mr_list` 中链表数组初始化使用 `MR_LISTIFY()` 生成多个初始化项：

```c
#define MR_LISTS_INIT(_list, _list_num) \
    {MR_LISTIFY(_list_num, (, ), __MR_LIST_ARRAY_INIT_IMPL, _list)}

#define __MR_LIST_ARRAY_INIT_IMPL(_i, _list) \
    MR_LIST_INIT(&(_list)[_i])
```

对于包含 3 个元素的链表数组，生成结果等价于：

```c
{
    MR_LIST_INIT(&list[0]),
    MR_LIST_INIT(&list[1]),
    MR_LIST_INIT(&list[2])
}
```

下面是一个独立的声明生成示例：

```c
#define DECL_BUF(_i, _prefix) static int _prefix##_buf##_i

MR_LISTIFY(3, (;), DECL_BUF, sample);
```

该宏调用会按顺序生成 3 个声明项，并用分号分隔。

## 注意事项

- `_n` 必须对应头文件中已经定义的宏编号；普通 `MR_LISTIFY()` 当前定义到 `4096`，嵌套 `MR_LISTIFY_NEST()` 当前定义到 `255`。
- `_s` 通常以带括号形式传入，例如 `(, )` 或 `(;)`，由实现中的 `MR_DE_BRACKET` 去掉外层括号。
- `_f` 必须能接受索引作为第一个参数，并能接受调用方传入的附加参数。
- 展开数量越大，预处理和编译负担越高；静态初始化数组时应避免不必要的大规模展开。
- 该模块只做预处理期代码生成，不产生运行时数据结构或运行时代码逻辑。

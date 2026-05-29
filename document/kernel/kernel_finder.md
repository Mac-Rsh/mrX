[**中文**](kernel_finder.md) | [**English**](kernel_finder_en.md)

---

# Finder 查找树

`mr_finder` 模块在启用 `MR_USE_FINDER` 时提供基于名称的层级查找树。每个 finder
是一个内核对象，带有名称、父节点指针、兄弟链表节点和子节点链表头。模块支持按相对路径查找、按路径自动创建中间节点，以及把已有节点添加到路径末端。

Finder 使用全局自旋锁保护树结构修改和路径查找，使用 `mr_object` 管理引用计数和静态/动态对象释放路径。

## 核心数据结构和宏

### 名称描述

```c
typedef struct mr_finder_name {
    const char *str;
    mr_uint32_t hash;
    mr_uint32_t len;
} mr_finder_name_t;
```

`str` 指向名称字符串，`hash` 保存名称哈希值，`len` 保存名称长度且不包含结尾的 `\0`。查找时先比较哈希和长度，再比较字符串内容以处理哈希冲突。

### Finder 控制块

```c
MR_CLAZZES_EXPORT(finder);
typedef struct mr_finder {
    mr_object_t parent;
#if !defined(MR_CFG_FINDER_INAME_LEN)
#define MR_CFG_FINDER_INAME_LEN        (4)
#endif
    char iname[MR_CFG_FINDER_INAME_LEN];
    mr_finder_name_t name;
    struct mr_finder *upper;
    mr_list_t sibling, lower;
} mr_finder_t;
```

字段含义如下：

| 字段        | 含义                                                     |
|-----------|--------------------------------------------------------|
| `parent`  | 内嵌对象控制块，用于引用计数和释放回调。                                   |
| `iname`   | 短名称内联存储区，默认长度为 `4` 字节，可由 `MR_CFG_FINDER_INAME_LEN` 配置。 |
| `name`    | 名称字符串、哈希值和长度。名称较短时指向 `iname`，较长时指向动态分配内存。              |
| `upper`   | 父 finder 指针，未加入树时为 `MR_NULL`。                          |
| `sibling` | 挂入父节点 `lower` 链表的兄弟节点。                                 |
| `lower`   | 子 finder 链表头。                                          |

### 名称和状态宏

```c
#define MR_FINDER_NAME(_finder)         ((_finder)->name.str)
#define MR_FINDER_NAME_LEN(_finder)     ((_finder)->name.len)
#define MR_FINDER_IS_ADDED(_finder)     (!mr_list_is_empty(&(_finder)->sibling))
```

`MR_FINDER_NAME()` 返回名称字符串，`MR_FINDER_NAME_LEN()` 返回名称长度。`MR_FINDER_IS_ADDED()` 通过 `sibling` 链表节点判断
finder 是否已经挂入父节点。

## 模块接口

### 初始化和创建

```c
mr_err_t mr_finder_init(mr_finder_t *finder, const char *fmt, ...);
mr_finder_t *mr_finder_create(const char *fmt, ...);
mr_err_t mr_finder_vinit(mr_finder_t *finder, const char *fmt, mr_va_list args);
mr_finder_t *mr_finder_vcreate(const char *fmt, mr_va_list args);
```

`mr_finder_init()` 初始化调用者提供的静态 finder，并使用静态类释放路径。`mr_finder_create()` 动态分配 finder，并使用动态类释放路径。
`vinit` 和 `vcreate` 是对应的 `mr_va_list` 版本。

名称通过格式化字符串生成，名称不能包含 `/`。头文件注释要求名称不能为 empty、`.` 或 `..`；当前实现通过 `MR_ASSERT` 检查 `fmt`
非空，并在名称包含 `/` 时返回 `-MR_EINVAL`。

### 删除和引用

```c
mr_err_t mr_finder_del(mr_finder_t *finder);
MR_INLINE mr_finder_t *mr_finder_get(mr_finder_t *finder);
MR_INLINE mr_bool_t mr_finder_put(mr_finder_t *finder);
```

`mr_finder_del()` 会先从父节点移除 finder，再释放对象引用。静态 finder 释放名称资源但不释放控制块本身；动态 finder
还会释放控制块内存。`mr_finder_get()` 和 `mr_finder_put()` 是对象引用接口的 finder 类型封装。

### 添加和移除

```c
mr_err_t mr_finder_add(mr_finder_t *finder, mr_finder_t *upper);
mr_err_t mr_finder_remove(mr_finder_t *finder);
```

`mr_finder_add()` 将 `finder` 加入 `upper` 的子节点链表，并持有 `upper` 的一个引用。返回值如下：

| 返回值          | 含义                  |
|--------------|---------------------|
| `0`          | 添加成功。               |
| `-MR_EINVAL` | 获取父节点引用失败。          |
| `-MR_EPERM`  | 检测到直接递归关系。          |
| `-MR_EBUSY`  | `finder` 已经加入其他父节点。 |
| `-MR_EEXIST` | `upper` 下已经存在同名子节点。 |

`mr_finder_remove()` 从父节点移除 finder，并释放添加时持有的父节点引用。当前实现中，对未加入父节点的 finder 调用该接口也返回
`0`。

### 路径查找

```c
mr_finder_t *mr_finder_lookup(mr_finder_t *start, mr_finder_t *limit,
                              const char *fmt, ...);
mr_finder_t *mr_finder_vlookup(mr_finder_t *start, mr_finder_t *limit,
                               const char *fmt, mr_va_list args);
```

路径相对于 `start` 解析。多个 `/` 会被跳过，`.` 表示当前节点，`..` 表示父节点；当当前节点等于 `limit` 或没有父节点时，`..`
不再继续向上。查找成功返回 finder 指针，失败返回 `MR_NULL`。头文件提示：如果调用者需要长期持有返回结果，应调用
`mr_finder_get()` 获取引用。

### 创建路径和添加节点

```c
mr_finder_t *mr_finder_mkdir(mr_finder_t *start, mr_finder_t *limit,
                             const char *fmt, ...);
mr_finder_t *mr_finder_vmkdir(mr_finder_t *start, mr_finder_t *limit,
                              const char *fmt, mr_va_list args);

mr_err_t mr_finder_mkdir_add(mr_finder_t *start, mr_finder_t *limit,
                             mr_finder_t *finder, const char *fmt, ...);
mr_err_t mr_finder_vmkdir_add(mr_finder_t *start, mr_finder_t *limit,
                              mr_finder_t *finder, const char *fmt,
                              mr_va_list args);
```

`mr_finder_mkdir()` 按路径逐级查找，不存在的中间节点会通过 `mr_finder_create()` 动态创建并加入树中，最终返回路径末端
finder。`mr_finder_mkdir_add()` 先确保路径存在，再把调用者提供的 `finder` 添加到路径末端节点下。

## 使用示例

### 添加、查找和移除

```c
#include <mrX/kernel/mr_finder.h>

void sample_finder_usage(void)
{
    mr_finder_t root;
    mr_finder_t child;
    mr_finder_t *found;

    if (mr_finder_init(&root, "root") != 0) {
        return;
    }
    if (mr_finder_init(&child, "child") != 0) {
        mr_finder_del(&root);
        return;
    }

    if (mr_finder_add(&child, &root) == 0) {
        found = mr_finder_lookup(&root, MR_NULL, "child");
        (void)found;
        mr_finder_remove(&child);
    }

    mr_finder_del(&child);
    mr_finder_del(&root);
}
```

### 创建路径并添加叶子节点

```c
void sample_finder_mkdir_add(void)
{
    mr_finder_t root;
    mr_finder_t file;

    if (mr_finder_init(&root, "root") != 0) {
        return;
    }
    if (mr_finder_init(&file, "myfile") != 0) {
        mr_finder_del(&root);
        return;
    }

    if (mr_finder_mkdir_add(&root, MR_NULL, &file, "dir1/dir2") == 0) {
        (void)mr_finder_lookup(&root, MR_NULL, "dir1/dir2/myfile");
        mr_finder_remove(&file);
    }

    mr_finder_del(&file);
    mr_finder_del(&root);
}
```

## 注意事项

- 该模块只有在定义 `MR_USE_FINDER` 时才提供数据结构和接口实现。
- `mr_finder_lookup()` 返回的指针不自动增加引用；需要跨锁外长期持有时应调用 `mr_finder_get()`。
- `mr_finder_add()` 成功后会持有父节点引用，`mr_finder_remove()` 或对象释放路径会释放该引用。
- 当前递归保护只检查直接父子互相添加的情况，不是完整的祖先链环检测。
- `mr_finder_mkdir()` 创建的中间节点是动态 finder，随对象引用释放路径回收。
- Finder 树结构由模块内部全局自旋锁保护，但 finder 对象外部生命周期和返回指针持有规则仍需调用者遵守引用计数约定。

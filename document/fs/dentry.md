[**中文**](fs_dentry.md) | [**English**](fs_dentry_en.md)

---

# 目录项

Dentry 模块维护路径命名空间里的目录项树。每个 `mr_dentry_t` 是一个路径分量：它有名字、父节点、子节点链表，以及可选的绑定 inode。路径查找先走这棵缓存树；缓存未命中再向文件系统要 inode，并 instantiate 成新的 dentry。

Dentry 以内嵌 `mr_object_t` 管理引用。应用通常只通过 [文件接口](file.md) 间接触达它。文件系统实现和 VFS 内部代码直接使用本模块。

## 核心数据结构

### 目录项控制块

```c
typedef struct mr_dentry {
    mr_object_t parent;
    char iname[MR_CFG_FS_DENTRY_INLINE_NAME_LEN];
    mr_dentry_name_t name;
    struct mr_dentry *mounted;
    struct mr_dentry *upper;
    mr_list_t sibling, lower;
    const mr_dentry_ops_t *ops;
    struct mr_inode *inode;
    mr_uint8_t state;
} mr_dentry_t;
```

| 字段 | 含义 |
|------|------|
| `parent` | 对象控制块。初始引用计数为 1。 |
| `iname` | 短名内联缓冲。长度小于 `MR_CFG_FS_DENTRY_INLINE_NAME_LEN` 的名字存在这里，更长的名字在堆上。这不是文件名上限。 |
| `name` | 名字视图：`str`、`hash`、`len`（不含 `\0`）。`str` 可能指向 `iname` 或堆缓冲。 |
| `mounted` | 盖在本 dentry 上的被挂文件系统根。查找时会进入它。 |
| `upper` | 父目录 dentry。全局 ROOT 的 `upper` 为 `MR_NULL`。 |
| `sibling` | 挂在父目录 `lower` 上的兄弟链表节点。空链表表示尚未加入树。 |
| `lower` | 子 dentry 链表头。 |
| `ops` | 本目录下名字的 hash/compare。`MR_NULL` 使用默认 djb2-xor。 |
| `inode` | 绑定的 inode。dentry 自己持有一份 inode 引用。 |
| `state` | 内部状态：ACTIVE、MOVING、UNLINKING、DEAD。 |

`MR_DENTRY_IS_ADDED(dentry)` 用 `sibling` 是否为空判断节点是否已挂到父目录下。

### 名字

```c
typedef struct mr_dentry_name {
    const char *str;
    mr_uint32_t hash;
    mr_uint32_t len;
} mr_dentry_name_t;

#define MR_DENTRY_HASH_SEED  (5381u)
#define MR_DENTRY_NAME(d)    ((d)->name.str)
#define MR_DENTRY_NAME_LEN(d) ((d)->name.len)
```

默认哈希是 djb2-xor，种子 `MR_DENTRY_HASH_SEED`。比较默认按长度和字节相等。父目录可以提供自定义 `mr_dentry_ops_t`，查找时用**父目录**的 ops 计算哈希，以便和已有子节点一致。

名字必须是单个分量，不能包含 `/`（全局 ROOT 的 `"/"` 除外）。空名字、`"."`、`".."` 不能作为 dentry 名字创建。

### 目录项操作

```c
typedef struct mr_dentry_ops {
    mr_uint32_t (*hash)(const char *name, mr_uint32_t len);
    mr_bool_t (*compare)(const char *a, const char *b, mr_uint32_t len);
} mr_dentry_ops_t;
```

两个函数都可以为 `MR_NULL`，此时使用默认实现。自定义文件系统若需要大小写不敏感或特殊编码，在 super-block 的 `dops` 里提供，挂载时赋给被挂根 dentry。

## 全局 ROOT

```c
#define MR_DENTRY_ROOT_INIT(_dentry)
```

全局 ROOT 是静态 dentry，名字 `"/"`，`name.len` 为 0，`upper` 为空。`mr_fs_nsp_root()` 返回它。`MR_DENTRY_ROOT_INIT()` 只适用于这个 ROOT，不要用来初始化普通 dentry。

## 生命周期

1. `mr_dentry_init()` / `mr_dentry_create()` 创建后引用计数为 1，尚未加入树。
2. `mr_dentry_add()` 把它挂到父目录的 `lower` 下。同名已存在返回 `-MR_EEXIST`。
3. `mr_dentry_instantiate()` 绑定 inode；dentry 取得自己的 inode 引用，调用者原引用不变。
4. `mr_dentry_get()` / `mr_dentry_put()` 增减引用。
5. `mr_dentry_unlink()` 先把状态设为 UNLINKING，调用父 inode 的 `unlink`（若有），再从树中摘下、把状态设为 DEAD，并 `mr_object_del()` 释放创建引用。
6. 最后一个引用释放时，释放函数从父链表摘下（若还在）、`mr_inode_put()` 绑定的 inode，并释放堆上的长名字。动态 dentry 还会 `mr_free()` 自身。

内部状态用于把「通知文件系统」和「提交到树」分开：

| 状态 | 含义 |
|------|------|
| ACTIVE | 正常节点，可查找、可改名、可删除。 |
| MOVING | 已占用目标名字，尚未提交改名。 |
| UNLINKING | 已通知或即将通知文件系统 unlink，尚未提交。 |
| DEAD | 已从树中摘下，只等引用归零。 |

对 MOVING/UNLINKING/DEAD 节点再 `add`/`unlink`/`instantiate` 会失败。

## 模块接口

### 创建和删除

```c
mr_err_t mr_dentry_init(mr_dentry_t *dentry, const mr_dentry_ops_t *ops,
                        const char *fmt, ...);
mr_dentry_t *mr_dentry_create(const mr_dentry_ops_t *ops, const char *fmt, ...);
mr_err_t mr_dentry_del(mr_dentry_t *dentry);
```

名字通过 `fmt` 格式化，规则与 `mr_snprintf()` 相同。`mr_dentry_init()` 初始化调用者提供的静态对象；`mr_dentry_create()` 从堆分配动态对象。

`mr_dentry_del()` 释放创建时的那一份引用。正在 MOVING 或 UNLINKING 时返回 `-MR_EBUSY`。已经 DEAD 的对象再删一次仍然会 `put`。

### 加入和移出树

```c
mr_err_t mr_dentry_add(mr_dentry_t *dentry, mr_dentry_t *upper);
mr_err_t mr_dentry_remove(mr_dentry_t *dentry);
```

`mr_dentry_add()` 只改命名空间树，不调用文件系统。已在树上返回 `-MR_EBUSY`。父目录或自身不是 ACTIVE 返回 `-MR_ENOENT`。

`mr_dentry_remove()` 从父目录摘下，不调用文件系统 `unlink`，也不把状态标为 DEAD。它用于尚未绑定文件系统节点的纯命名空间调整。已 DEAD 或没有父目录时返回 `-MR_ENOENT`。

### 移动和重命名

```c
mr_err_t mr_dentry_move(mr_dentry_t *dentry, mr_dentry_t *upper,
                        const char *name);
mr_err_t mr_dentry_rename(mr_dentry_t *dentry, mr_dentry_t *upper,
                          const char *name);
```

两者都要求 dentry 已在树上，`name` 是不含 `/` 的单分量。不能把节点移进自己的子树，返回 `-MR_EINVAL`。目标名字已被占用返回 `-MR_EEXIST`。

实现上先在目标父目录挂一个占位 dentry 占用新名字，源节点进入 MOVING，再提交或取消：

- `mr_dentry_move()` 只改树，不调用文件系统。
- `mr_dentry_rename()` 在提交前调用旧父 inode 的 `rename`。回调失败则取消占位，树恢复原状。

### 删除文件系统节点

```c
mr_err_t mr_dentry_unlink(mr_dentry_t *dentry);
```

要求节点 ACTIVE、已在树上、已绑定 inode。目录且 `lower` 非空时返回 `-MR_ENOTEMPTY`。若父 inode 提供 `unlink`，先调用它；失败则恢复 ACTIVE。成功后减少 `inode->nlink`，从树中摘下，状态改为 DEAD，并释放创建引用。

路径层的 `mr_fs_unlink()` / `mr_fs_rmdir()` 在这之前还会拒绝挂载点和 ROOT。

### 子节点和空目录

```c
mr_dentry_t *mr_dentry_child_get(mr_dentry_t *parent, mr_size_t index);
mr_bool_t mr_dentry_is_empty(mr_dentry_t *dentry);
```

`mr_dentry_child_get()` 按 0 起始下标取子节点，返回的 dentry 带一份引用，用完必须 `mr_dentry_put()`。越界返回 `MR_NULL`。默认 `readdir` 就用这个接口。

`mr_dentry_is_empty()` 看 `lower` 是否为空。它只反映缓存树，不向文件系统询问未实例化的磁盘内容。

### 查找

```c
mr_err_t mr_dentry_lookup(mr_dentry_t *start, mr_dentry_t *limit,
                          const char *path, mr_dentry_t **result);
```

`path` 相对 `start`。`limit` 是 `".."` 不能越过的节点，通常是命名空间 ROOT 或 `MR_AT_IN_ROOT` 的目录句柄。成功时 `*result` 持有一份引用。

查找规则：

1. 从 `start` 开始，若存在 `mounted`，先进入被挂根。
2. 跳过多余的 `/`。`"."` 留在原地。`".."` 走到 `upper`，已经位于 `limit` 或没有 `upper` 时保持原地。
3. 其他分量先在子链表里按哈希匹配。
4. 未命中则调用父 inode 的 `lookup`，把返回的 inode instantiate 成子 dentry 后再继续。
5. 每个分量解析后再次跟随 `mounted`。

`lookup` 回调在命名空间锁外执行。文件系统必须返回新的 inode 引用；VFS 会把它绑到 dentry 上。

### 绑定 inode

```c
mr_err_t mr_dentry_instantiate(mr_dentry_t *dentry, mr_inode_t *inode);
mr_err_t mr_dentry_uninstantiate(mr_dentry_t *dentry);
```

`instantiate` 要求 dentry ACTIVE 且尚未绑定 inode，否则 `-MR_EBUSY` / `-MR_ENOENT`。dentry 会再 `mr_inode_get()` 一次。`uninstantiate` 放下这份引用并把 `inode` 置空，不影响其他引用。

### 引用

```c
mr_dentry_t *mr_dentry_get(mr_dentry_t *dentry);
mr_bool_t mr_dentry_put(mr_dentry_t *dentry);
```

封装 `mr_object_get()` / `mr_object_put()`。查找、`child_get`、打开文件拿到的 dentry 都必须配对 `put`。

## 使用示例

下面演示纯命名空间下创建、挂上父目录、绑定 inode 再摘掉。真实文件系统通常由 `mr_fs_mkdir()` / `open` 走完这些步骤。

```c
#include <mrX/fs/mr_dentry.h>
#include <mrX/fs/mr_inode.h>

void sample_dentry_bind(mr_dentry_t *parent, mr_inode_t *inode)
{
    mr_dentry_t *child;

    child = mr_dentry_create(parent->ops, "%s", "leaf");
    if (!child) {
        return;
    }

    if ((mr_dentry_add(child, parent) != 0)
        || (mr_dentry_instantiate(child, inode) != 0)) {
        mr_dentry_del(child);
        return;
    }

    mr_dentry_put(child);
}
```

`mr_dentry_create()` 的返回值已经持有创建引用。示例最后的 `put` 在节点仍挂在树上时只会减少一个引用，父目录的 `lower` 仍让节点活着。真正删除名字应使用 `mr_dentry_unlink()` 或路径层的 `unlink`/`rmdir`。

## 注意事项

- 持有命名空间锁时不要调用 inode 回调，也不要阻塞。
- 自定义 hash/compare 必须成对出现，并且父目录下所有子节点用同一套 ops。
- `mr_dentry_move()` 不改 backing 节点；跨文件系统的改名由路径层拒绝（`-MR_EXDEV`）。
- 挂载覆盖走 `mounted`，被覆盖的 dentry 仍留在父目录的 `lower` 里。卸载后名字重新指向原来的目录。
- `MR_CFG_FS_DENTRY_INLINE_NAME_LEN` 只影响短名是否内联，不影响最大文件名长度。

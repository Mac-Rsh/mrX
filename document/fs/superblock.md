[**中文**](fs_superblock.md) | [**English**](fs_superblock_en.md)

---

# Super-block

Super-block 是一次挂载的实例。每种 [文件系统类型](filesystem.md) 可以对应多个 super-block。它保存根 inode、盖在命名空间上的根 dentry、挂载点、父子挂载链表，以及防止卸载与路径操作并发的计数。

应用不直接创建 super-block，而是调用 `mr_fs_mount()` / `mr_fs_umount()`。文件系统实现在 `mount` 回调里填充 `sb->root`；VFS 完成对象初始化和 attach。

## 控制块

```c
typedef struct mr_super_block {
    mr_object_t parent;
    mr_dentry_t *mnt;
    mr_dentry_t *root_dentry;
    mr_inode_t *root;
    int flags;
    struct mr_super_block *next;
    struct mr_super_block *mount_parent;
    struct mr_super_block *mount_child;
    struct mr_super_block *mount_next;
    struct mr_file_system *type;
    const mr_dentry_ops_t *dops;
    mr_bool_t detaching;
    mr_size_t op_cnt;
    void *priv;
} mr_super_block_t;
```

| 字段 | 含义 |
|------|------|
| `parent` | 对象控制块。Super-block 只有动态类，释放时 `mr_free()` 自身。 |
| `mnt` | 挂载点 dentry。全局 ROOT 挂载时 `mnt == root_dentry`。 |
| `root_dentry` | 本文件系统可见的根 dentry。嵌套挂载时它盖在 `mnt->mounted` 上。 |
| `root` | 根目录 inode，由 `mount` 回调创建。 |
| `flags` | 挂载时传入的 flags，VFS 原样保存。 |
| `next` | 全局 sb 链表。 |
| `mount_parent` / `mount_child` / `mount_next` | 挂载拓扑：父实例、第一个子实例、兄弟。 |
| `type` | 文件系统类型描述符。 |
| `dops` | 提供给被挂根 dentry 的名字 ops。 |
| `detaching` | 已进入卸载准备，新的 `op_enter` 会得到 `-MR_EBUSY`。 |
| `op_cnt` | 正在进行的命名空间操作数。非 0 时不能 prepare detach。 |
| `priv` | 文件系统私有数据。 |

## 创建

```c
mr_err_t mr_super_block_create(mr_file_system_t *type, const char *source,
                               int flags, void *data, mr_super_block_t **out);
```

分配动态 super-block，调用 `type->mount`。`mount` 失败则释放刚分配的 sb，且不调用 `kill_sb`。成功则要求 `sb->root` 存在且类型为目录，否则走 `_kill_exit`：放下根 inode 并调用 `kill_sb`。

对象初始化成功后，VFS 对 `sb->root->sb` 做 `mr_super_block_get()`，使根 inode 钉住这份 super-block。创建函数返回时 `*out` 持有创建引用。

## 挂到命名空间

```c
mr_err_t mr_super_block_attach(mr_super_block_t *sb, mr_dentry_t *mnt,
                               mr_dentry_t *root);
mr_err_t mr_super_block_detach(mr_super_block_t *sb, mr_dentry_t **mnt,
                               mr_dentry_t **root);
```

`attach` 分两种：

- **全局 ROOT**：`mnt == root`。根 dentry 就是全局 ROOT，`ops` 设为 `sb->dops`。没有 `mount_parent`。
- **嵌套挂载**：`root` 必须已绑定 `sb->root`，且还没有父节点。`mnt` 必须是已存在的目录，且 `mnt->mounted` 为空。成功后 `mnt->mounted = root`，`root->upper` 复制挂载点的父指针（用于 `".."`），sb 链到父 super-block 的 `mount_child`。

被覆盖的 `mnt` 仍留在父目录的子链表里。查找进入 `mounted`；卸载后名字重新指向原目录。

`detach` 必须先 `detach_prepare`。成功时把 sb 持有的 `mnt` / `root_dentry` 引用转交给输出参数，调用者必须两个都 `mr_dentry_put()`。有子挂载时不能 detach，路径层会先看到 `-MR_EBUSY`。

## 卸载准备和操作计数

```c
mr_err_t mr_super_block_detach_prepare(mr_super_block_t *sb);
void mr_super_block_detach_cancel(mr_super_block_t *sb);
mr_err_t mr_super_block_op_enter(mr_super_block_t *sb);
void mr_super_block_op_exit(mr_super_block_t *sb);
```

`detach_prepare` 在锁内检查：尚未 detaching、`op_cnt == 0`、没有 `mount_child`、已经 attach。成功则置 `detaching`。

路径操作在碰 backing 文件系统前调用 `op_enter`。正在卸载或尚未 attach 会失败。`op_exit` 成对减少计数。这样 `unmount` 回调不会和仍在进行的 lookup/unlink 交错。

`unmount` 失败时路径层调用 `detach_cancel()`，清除 `detaching`，挂载继续可用。

## 查找和删除

```c
mr_super_block_t *mr_super_block_find_by_dentry(mr_dentry_t *dentry);
mr_err_t mr_super_block_del(mr_super_block_t *sb);
mr_super_block_t *mr_super_block_get(mr_super_block_t *sb);
mr_bool_t mr_super_block_put(mr_super_block_t *sb);
```

`find_by_dentry()` 匹配 `mnt` 或 `root_dentry`，返回带引用的 sb，用完必须 `put`。

`mr_super_block_del()` 从全局链表摘下，放下 `sb->root` 的创建引用，再 `mr_object_del()`。最终 `kill_sb` 发生在最后一个引用释放时，不在 `del` 里同步调用（除非那正好是最后一份引用）。释放函数会断言 `op_cnt == 0` 且已经 detach。

## 和路径层的关系

`mr_fs_mount_root()` / `mr_fs_mount()` 的顺序是：`find` 类型 → `super_block_create` → 解析挂载点 → `attach`。失败时 `del` 刚创建的 sb。

不要在文件系统回调里直接 `attach`/`detach`；那是 VFS 的职责。`mount` 只填充 `root`/`dops`/`priv`。

## 注意事项

- Super-block 只有动态分配路径，没有 `mr_super_block_init()`。
- `op_enter` 失败返回 `-MR_ENOENT`（未挂上）或 `-MR_EBUSY`（正在卸）。
- 子挂载挂在父 sb 上，不是挂在 dentry 子链表里。卸父挂载前必须先卸子挂载。
- 根 inode 的 `sb` 指针持有 super-block 引用。inode `destroy` 会 `mr_super_block_put()`，避免 sb 比 inode 先消失。

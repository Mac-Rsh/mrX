[**中文**](fs_ramfs.md) | [**English**](fs_ramfs_en.md)

---

# RamFS

RamFS 是堆内存上的参考文件系统，类型名 `"ramfs"`。启用 `MR_USE_FS_RAMFS` 后，它在 `MR_INIT_LEVEL_FS` 自动 `mr_file_system_register()`，但不自动挂载。把它挂到已存在的目录上即可得到可读写的普通文件。

它同时是第三方文件系统的样板：最小的 `create` / `truncate` / `destroy` / `read` / `write`。目录树本身由 VFS dentry 缓存维护，所以 RamFS 不实现 `lookup`。

## 配置与挂载

Kconfig：`MR_USE_FS_RAMFS`，依赖 `MR_USE_FS`，默认关闭。

```c
mr_fs_mkdir("/ram");
mr_fs_mount(MR_NULL, "/ram", "ramfs", 0, MR_NULL);
```

`source`、`flags`、`data` 都被忽略。可以多次挂载，每次得到独立的 super-block 和独立的文件内容。

## 节点布局

每个 inode 的 `priv` 指向：

```c
typedef struct ramfs_node {
    mr_uint8_t *data;
} ramfs_node_t;
```

目录和普通文件都有这个结构。目录的 `data` 保持 `MR_NULL`。普通文件的内容是一块可 `realloc` 的堆缓冲，长度等于 `inode->size`。

`mount` 分配根目录 inode（类型 `MR_INODE_DIR`），`sb->dops` 为 `MR_NULL`，使用默认 dentry 哈希。

## 实现的操作

| 回调 | 行为 |
|------|------|
| `inode.create` | 再分配一个 `ramfs_node_t` 和 inode，目录和普通文件都走这里。 |
| `inode.truncate` | `length == size` 直接成功。`length == 0` 释放缓冲。增大时 `realloc` 并把新区间填 0。`length < 0` 返回 `-MR_EINVAL`，超过 `MR_SIZE_MAX` 返回 `-MR_ENOMEM`。 |
| `inode.destroy` | 释放 `data` 和 `ramfs_node_t`。 |
| `file.read` | 从 `pos` 拷贝，不超过 `size`。 |
| `file.write` | 如需扩展先 `realloc`，`pos` 超过旧大小时中间填 0，然后写入并更新 `inode->size`。 |

未实现 `lookup`、`unlink`、`rename`、`open`、`close`、`readdir`。这些由 VFS 缺省路径完成：名字只存在 dentry 树上，目录枚举走 `mr_dentry_child_get()`。

因此：

- `mkdir` / `open(O_CREAT)` 会调 `create`。
- `unlink` / `rmdir` 只改 dentry 树；最后一个 inode 引用释放时 `destroy` 丢掉缓冲。
- 打开普通文件即可 `read` / `write` / `truncate`。目录没有文件数据，写目录仍被路径层拒绝。

## 限制

- 内容只在内存中，掉电即丢。
- 没有持久化，也没有 `sync` 钩子，`mr_fs_sync()` 返回 `-MR_ENOSYS`。
- 没有配额，增长受堆分配失败限制（`-MR_ENOMEM`）。
- 不实现符号链接回调；符号链接走 VFS 内建内存目标。

## 作为实现参考

写新文件系统时可以按这个顺序抄：

1. 定义 `mr_inode_ops_t` / `mr_file_ops_t`，至少提供 `create` 和 `destroy`。
2. `mount` 里创建根目录 inode，赋给 `sb->root`。
3. `MR_FILE_SYSTEM_INIT("name", mount, unmount_or_null, kill_sb_or_null)`。
4. 在 initcall 或显式初始化里 `mr_file_system_register()`。

若节点数据在介质上而不是全在 dentry 缓存里，再补 `lookup`、`unlink`、`readdir`。RamFS 不需要它们，是因为每个可见名字都已经 instantiate 在树上。

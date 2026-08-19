[**中文**](fs_ramfs.md) | [**English**](fs_ramfs_en.md)

---

# RamFS

RamFS 是堆内存上的参考文件系统，类型名 `"ramfs"`。启用 `MR_USE_FS_RAMFS` 后，它在 `MR_INIT_LEVEL_FS` 自动 `mr_file_system_register()`，但不自动挂载。把它挂到已存在的目录上即可得到可读写的普通文件，以及可选的 FIFO 节点。

它同时是第三方文件系统的样板。目录树本身由 VFS dentry 缓存维护，所以 RamFS 不实现 `lookup`。

## 配置与挂载

Kconfig：

- `MR_USE_FS_RAMFS`，依赖 `MR_USE_FS`，默认关闭。
- `MR_USE_FS_RAMFS_FIFO`，依赖 ramfs，默认开启。关闭后 FIFO 创建返回 `-MR_ENOSYS`。
- `MR_CFG_FS_RAMFS_FIFO_SIZE`，FIFO 创建时的默认字节容量，默认 32。

```c
mr_fs_mkdir("/ram");
mr_fs_mount(MR_NULL, "/ram", "ramfs", 0, MR_NULL);
```

`source`、`flags`、`data` 都被忽略。可以多次挂载，每次得到独立的 super-block 和独立的文件内容。RamFS 不会把自己挂到 `/tmp`；若需要 `/tmp`，由系统或用户初始化层执行 `mkdir` + `mount`。

## 创建路径

| 公共接口 | 后端回调 | ramfs 接受的类型 |
|----------|----------|------------------|
| `mkdir` | `.create(DIR)` | 目录 |
| `open(O_CREAT)` | `.create(REG)` | 普通文件 |
| `mr_fs_mknod(path, REG/DIR/FIFO, ...)` | 无 `.mknod`，VFS 回退 `.create` | 与 `.create` 相同 |
| `mr_fs_mkfifo()` / `mr_fs_mkfifoat()` | 同上（内部是 `mknod(FIFO)`） | FIFO |

RamFS 不实现 `.mknod`。`mr_fs_mknod()` / `mkfifo` 走 VFS 回退，`priv` 被忽略。CHR/BLK/LNK 仍被拒绝。公共 `open(O_CREAT)` 只会传入 `REG`。

推荐用 `mr_fs_mkfifo()` 创建 FIFO：

```c
mr_fs_mkfifo("/ram/events");
mr_fs_open(&file, "/ram/events", MR_O_RDWR);
capacity = mr_fs_fcntl(&file, MR_F_GETPIPE_SZ, 0);
capacity = mr_fs_fcntl(&file, MR_F_SETPIPE_SZ, 1000);
```

## 节点布局

每个 inode 的 `priv` 指向按类型解释的联合体：

```c
typedef struct ramfs_node {
    union {
        struct {
            mr_uint8_t *data;
        } regular;
        struct {
            mr_size_t capacity;
            mr_uint8_t *data;
            mr_size_t head;
            mr_size_t len;
        } fifo;
    } data;
} ramfs_node_t;
```

- 目录：不分配 `ramfs_node_t`，无数据 file operations。
- 普通文件：可 `realloc` 的堆缓冲，长度等于 `inode->size`。
- FIFO：固定容量环形缓冲。`inode->size` 固定为 0，容量通过 `MR_F_GETPIPE_SZ` 查询。

普通文件和 FIFO 使用不同的 file-operation 表。`mount` 分配根目录 inode（类型 `MR_INODE_DIR`），`sb->dops` 为 `MR_NULL`。

## 实现的操作

| 回调 | 行为 |
|------|------|
| `inode.create` | 接受 DIR/REG/FIFO，走公共节点构造。 |
| `inode.truncate` | 仅普通文件：`length == size` 直接成功。`length == 0` 释放缓冲。增大时 `realloc` 并把新区间填 0。 |
| `inode.destroy` | 按类型释放缓冲和 `ramfs_node_t`。 |
| 普通 `file.read` / `file.write` | 从 `pos` 读写可增长缓冲。 |
| FIFO `file.read` / `file.write` | 共享非阻塞字节队列。空读或满写返回 `-MR_EAGAIN`，允许部分读写。不更新 `file->pos`。 |
| FIFO `file.llseek` | 返回 `-MR_ESPIPE`。 |
| FIFO `file.fcntl` | `MR_F_GETPIPE_SZ` / `MR_F_SETPIPE_SZ`。 |

未实现 `lookup`、`unlink`、`rename`、`open`、`close`、`readdir`。这些由 VFS 缺省路径完成。

## FIFO 语义

第一版是共享的非阻塞字节 FIFO，不是完整 POSIX named pipe：

- 创建时不传容量，使用 `MR_CFG_FS_RAMFS_FIFO_SIZE`，之后用 fcntl 调整。
- 容量可以是任意正整数，不要求 2 的幂，也不按页取整。
- `SETPIPE_SZ` 在请求值小于当前数据量时返回 `-MR_EBUSY`，成功时保留未读字节顺序。
- 不实现打开握手、阻塞等待、reader/writer 计数、EOF 和 `SIGPIPE`。
- 通用 `mr_fs_truncate()` 拒绝 FIFO（`-MR_EINVAL`）。
- fcntl 命令数值是 mrX ABI，不是宿主 Linux `F_*`。

## 限制

- 内容只在内存中，掉电即丢。
- 没有持久化，也没有 `sync` 钩子，`mr_fs_sync()` 返回 `-MR_ENOSYS`。
- 没有配额，增长受堆分配失败限制（`-MR_ENOMEM`）。
- 不实现符号链接回调；符号链接走 VFS 内建内存目标。
- 不提供线程安全保证，调用方需自行串行化同一节点上的并发访问。
- 不自动挂载 `/tmp`。

## 作为实现参考

写新文件系统时可以按这个顺序抄：

1. 定义 `mr_inode_ops_t` / `mr_file_ops_t`，至少提供 `create` 和 `destroy`。
2. `mount` 里创建根目录 inode，赋给 `sb->root`。
3. `MR_FILE_SYSTEM_INIT("name", mount, unmount_or_null, kill_sb_or_null)`。
4. 在 initcall 或显式初始化里 `mr_file_system_register()`。

若节点数据在介质上而不是全在 dentry 缓存里，再补 `lookup`、`unlink`、`readdir`。RamFS 不需要它们，是因为每个可见名字都已经 instantiate 在树上。

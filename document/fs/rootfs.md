[**中文**](fs_rootfs.md) | [**English**](fs_rootfs_en.md)

---

# RootFS

RootFS 是只维护命名空间、不保存普通文件内容的根文件系统，类型名 `"rootfs"`。启用 `MR_USE_FS_ROOTFS` 后，它在 `MR_INIT_LEVEL_FS` 注册，并立刻 `mr_fs_mount_root(MR_NULL, "rootfs", 0, MR_NULL)`，把全局 ROOT 变成一棵可 `mkdir` / `mknod` 的目录树。

它适合作为挂载点骨架：在上面建 `/dev`、`/mnt` 之类的目录，再把 ramfs 或其他文件系统挂进去。不能在 rootfs 上读写普通文件数据。

## 配置

Kconfig：`MR_USE_FS_ROOTFS`，依赖 `MR_USE_FS`，默认关闭。

启动成功后全局 ROOT 已经是 rootfs。若 ROOT 已被其他类型占用，`mr_fs_mount_root()` 返回忙状态，init 代码把 `0` 和 `MR_EBUSY` 都视为可接受。

也可以把 `"rootfs"` 再挂到子目录上，得到另一棵无数据的目录树。

## 能力

| 操作 | 支持 |
|------|------|
| `mkdir` | 是。`create` 只接受 `MR_INODE_DIR`，其他类型返回 `-MR_ENOSYS`。 |
| `mknod` | 是。把调用者传入的 `priv` **借用**到 inode 上，rootfs 不复制也不释放它。 |
| 目录枚举 | 是。走 VFS 默认 `readdir`（dentry 子链表）。 |
| `open` 普通文件后 `read`/`write`/`truncate` | 否。没有 `fops`。 |
| 符号链接数据 | 否。可走 VFS 内建符号链接。 |
| `lookup` | 否。可见名字都在 dentry 缓存里。 |

根 inode 和子目录 inode 使用同一张 `inode_ops`：`create` + `mknod`。`fops` 为 `MR_NULL`。`sb->dops` 为 `MR_NULL`。

## mknod 的 priv

`rootfs_inode_mknod()` 把 `priv` 存进 `inode->priv`，并在注释中写明：指针在 inode 生命周期内被借用。RootFS 的 `destroy` 是缺省空操作，**不会** `free(priv)`。调用者必须保证：

- `priv` 的寿命不短于该 inode。
- 节点删除后若还需要释放 `priv`，由调用者自己做。

这适合把设备对象或驱动指针挂到命名空间节点上，而不是用来存文件字节。

## 和 RamFS 怎么配合

典型启动组合：

1. 打开 `MR_USE_FS` 和 `MR_USE_FS_ROOTFS`，得到 `/`。
2. `mr_fs_mkdir("/tmp")`。
3. 打开 `MR_USE_FS_RAMFS`，`mr_fs_mount(MR_NULL, "/tmp", "ramfs", 0, MR_NULL)`。
4. 之后 `/tmp` 下的普通文件走 ramfs 堆缓冲，`/` 下其他目录仍是无数据的 rootfs 节点。

只开 ramfs、不开 rootfs 时，需要自己先 `mr_fs_mount_root(..., "ramfs", ...)`，否则全局 ROOT 没有 backing 目录 inode，路径创建会失败。

## 注意事项

- RootFS 不是完整 POSIX 根文件系统，只是命名空间容器。
- 不要对 rootfs 上的普通文件节点期望 `read`/`write`。
- `mknod` 的 `priv` 所有权不转移给 RootFS。

[**中文**](fs_overview.md) | [**English**](fs_overview_en.md)

---

# 文件系统总览

mrX 文件系统把路径命名空间、目录项缓存、inode 和挂载实例拆成几层。应用主要使用 [文件接口](file.md)；实现一个文件系统或理解挂载，需要同时看 dentry、inode、super-block 和 file-system 类型。

模块只有在 `MR_USE_FS` 启用时编译。启用后，`<mrX.h>` 会包含 dentry、inode、file、fs、sb 头文件。命名空间接口在 `<mrX/fs/mr_nsp.h>`，由内核和平台适配层使用。

## 分层

```text
应用 / 测试
    mr_fs_open / read / mkdir / mount ...     file
        当前 root / cwd                        nsp
        路径分量查找、挂载穿越                   dentry
        节点类型、操作表、文件数据               inode
        一次挂载的实例                          super-block
            文件系统类型（mount/unmount/kill_sb） file-system
```

| 模块 | 头文件 | 职责 |
|------|--------|------|
| [File](file.md) | `mr_file.h` | 应用侧句柄 API：打开、读写、目录、挂载入口。 |
| [Dentry](dentry.md) | `mr_dentry.h` | 路径名字树、子节点链表、挂载覆盖、查找缓存。 |
| [Inode](inode.md) | `mr_inode.h` | 节点对象、inode/file 操作表、`destroy` 析构。 |
| [File-system](filesystem.md) | `mr_fs.h` | 按名字注册的文件系统类型。 |
| [Super-block](superblock.md) | `mr_sb.h` | 一次挂载的实例、挂载拓扑、操作计数。 |
| [Namespace](nsp.md) | `mr_nsp.h` | 全局 ROOT、当前 root/cwd、命名空间锁。 |
| [RamFS](ramfs.md) | `fs/ramfs/ramfs.c` | 堆内存普通文件的参考实现。 |
| [RootFS](rootfs.md) | `fs/rootfs/rootfs.c` | 只维护命名空间、不存文件数据的根文件系统。 |

Dentry 保存「这个名字在树上的位置」，inode 保存「这个节点是什么、怎么操作」。同一个 inode 可以被多个 dentry 引用（硬链接）。一次 `mr_fs_mount()` 创建一个 super-block，并把它的根 dentry 盖到挂载点上。

## 对象与句柄

dentry、inode、super-block 都内嵌 `mr_object_t`，用引用计数管理生命周期。最后一个引用释放时调用类释放函数。

`mr_file_t` 不是内核对象。它由调用者分配，打开时额外 pin 一份 dentry 和 inode 引用，关闭时再放掉。关闭失败也不会把句柄留着。

## 启动顺序

1. 启用 `MR_USE_FS`。单用户模式下，`mr_fs_file_init()` 通过 initcall 初始化全局命名空间，使 root/cwd 都指向全局 ROOT dentry（名字 `"/"`）。
2. 启用 `MR_USE_FS_ROOTFS` 时，rootfs 在 `MR_INIT_LEVEL_FS` 注册自己，并调用 `mr_fs_mount_root("rootfs")` 把全局 ROOT 变成 rootfs 目录树。
3. 启用 `MR_USE_FS_RAMFS` 时，ramfs 只注册类型，不自动挂载。之后可用 `mr_fs_mount(..., "ramfs", ...)` 挂到已存在的目录上。
4. 自定义文件系统同样先 `mr_file_system_register()`，再 `mr_fs_mount()` / `mr_fs_mount_root()`。

多用户模式下，全局 ROOT 仍然由 FS 提供，但每个任务的 `mr_fs_nsp_t` 由平台适配层分配，并自行调用 `mr_fs_nsp_init()`。

## 一次路径操作怎么走

以 `mr_fs_open(&file, "a/b", flags)` 为例：

1. 取当前命名空间的 cwd（相对路径）或 root（绝对路径）。
2. `mr_dentry_lookup()` 按分量走缓存；未命中则调用父目录 inode 的 `lookup`，再把结果 instantiate 进树。
3. 碰到挂载点时进入 `dentry->mounted`，继续在被挂文件系统里查找。
4. 需要创建时走父目录的 `create` / `mknod`，再把新 inode 绑到新 dentry。
5. `fs_file_open()` pin dentry 和 inode，填入 `mr_file_t`，必要时调用 `fops->open` 和 `truncate`。

卸载则相反：先 `detach_prepare`（不能有进行中的操作和子挂载），通知文件系统 `unmount`，再 `detach` 并把引用交还调用者，最后 `mr_super_block_del()`。最后一个 super-block 引用释放时调用 `kill_sb`。

## 锁

命名空间自旋锁（`mr_fs_nsp_lock()`）保护 dentry 树、挂载拓扑和文件系统注册表。持锁期间不能阻塞，也不能调用文件系统实现回调。inode 的 `lookup` / `create` / `unlink` 等回调在锁外执行；回调返回后再在锁内提交树的变更。

## 配置入口

文件系统总开关和路径/符号链接限制在 `include/mrX/fs/Kconfig`。ramfs、rootfs 各有子菜单。应用侧路径便利接口由 `MR_USE_FS_BY_PATH` 和 `MR_USE_FS_AT` 裁剪，详见 [文件接口](file.md)。

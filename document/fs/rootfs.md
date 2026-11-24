# 根文件系统（RootFS）

仅维护命名空间的骨架文件系统：支持创建目录与任意类型的节点（可借用并保存 `priv`
指针），不存储文件数据；启用后在 `MR_INIT_LEVEL_FS` 阶段自动注册 `"rootfs"`，并在全局根
`/` 未被占用时自动挂载。

| 项目   | 说明                                                                                                                                 |
|--------|--------------------------------------------------------------------------------------------------------------------------------------|
| 功能宏 | `MR_USE_FS_ROOTFS`（依赖 `MR_USE_FS`）                                                                                               |
| 头文件 | `<mrX/fs/mr_fs_file.h>`（无独立公共头，通过 VFS 接口访问）                                                                           |
| 依赖   | [文件系统 VFS](fs.md)、[对象 Object](../kernel/object.md)、[内存分配 Malloc](../libc/malloc.md)、[内核初始化](../kernel/initcall.md) |
| 测试   | `test/fs/rootfs.c`                                                                                                                   |

## 配置

| 配置项             | 类型 | 默认值 | 说明                                            |
|--------------------|------|--------|-------------------------------------------------|
| `MR_USE_FS_ROOTFS` | bool | n      | 启用 RootFS，并在启动时尝试自动挂载为全局根 `/` |

## 快速上手

```c
#include <mrX/fs/mr_fs_file.h>

static int rootfs_demo(void) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_stat_t stat;
    int drv_priv = 42;

    /* mr_user_init() 后若根未被占用，'/' 即为 rootfs */
    mr_fs_mkdir("/system");
    mr_fs_mknod("/system/dev", MR_INODE_CHR, &drv_priv);

    /* 可打开读取状态或取回 priv 指针，但无读写数据能力 */
    if (mr_fs_open(&file, "/system/dev", MR_O_RDONLY) == 0) {
        void *p = file.inode->priv;     /* == &drv_priv */
        mr_fs_fstat(&file, &stat);      /* stat.type == MR_INODE_CHR */
        (void)p;
        mr_fs_close(&file);
    }

    /* 在骨架目录上挂载数据文件系统（需启用 MR_USE_FS_RAMFS） */
    mr_fs_mkdir("/system/data");
    return mr_fs_mount(MR_NULL, "/system/data", "ramfs", 0, MR_NULL);
}
```

## 核心机制与约束

- **自动根挂载与替换**：`mr_user_init()` 执行 `MR_INIT_LEVEL_FS` 初始化时，会注册
  `"rootfs"` 并调用 `mr_fs_mount_root` 尝试挂载到 `/`。若全局根此前已被其他文件系统占用，初始化逻辑接受
  `-MR_EBUSY` 而不覆盖原有根。若需替换为其他根文件系统，须先关闭全部句柄并卸载所有子挂载，调用
  `mr_fs_umount("/")` 后再挂载新根。
- **节点创建与 `priv` 借用**：`mr_fs_mkdir` 用于创建目录；
  `mr_fs_mknod(path, type, priv)` 支持创建 `MR_INODE_REG`/`DIR`/`CHR`/`BLK`/
  `FIFO` 任意类型节点并原样保存 `priv` 指针。RootFS 仅借用 `priv`
  而不负责其释放或并发保护，指针生命周期由调用者管理。
- **不支持普通文件自动创建**：当目标不存在时，`mr_fs_open(..., MR_O_CREAT)` 会调用后端
  `create(..., MR_INODE_REG, ...)`，因 RootFS 的 `create` 仅允许目录而返回
  `-MR_ENOSYS`。
- **无数据回调**：RootFS 不实现 `fops` 与 `truncate`。在通过 VFS
  访问模式与目录类型校验后，非目录节点的 `read`/`write`/`ioctl`/`fsync` 返回
  `-MR_ENOSYS`，普通文件的 `mr_fs_ftruncate` 与 `MR_O_TRUNC` 返回 `-MR_ENOSYS`
  （非普通文件由 VFS 返回 `-MR_EISDIR` 或 `-MR_EINVAL`）。`mr_fs_fstat` 的 `size`
  恒为 `0`。

## API 速查

RootFS 不导出独立 API，全部通过 [文件系统 VFS](fs.md) 接口访问，各操作行为如下：

| VFS 接口                                        | 作用于 RootFS 的行为                                                                 |
|-------------------------------------------------|--------------------------------------------------------------------------------------|
| `mr_fs_mkdir`                                   | 创建目录节点（`MR_INODE_DIR`）                                                       |
| `mr_fs_mknod`                                   | 创建任意类型节点，原样保存 `priv` 指针                                               |
| `mr_fs_open` + `MR_O_CREAT`                     | 目标不存在时返回 `-MR_ENOSYS`；目标已存在按常规 `open` 处理                          |
| `mr_fs_read` / `mr_fs_write`                    | 非目录节点且访问模式合法时返回 `-MR_ENOSYS`                                          |
| `mr_fs_ftruncate` / `MR_O_TRUNC`                | `MR_INODE_REG` 返回 `-MR_ENOSYS`；目录/其他类型由 VFS 返回 `-MR_EISDIR`/`-MR_EINVAL` |
| `mr_fs_fstat`                                   | 返回通用字段（`type`、`mode`，`size` 恒为 `0`）                                      |
| `mr_fs_readdir` / `rename` / `unlink` / `rmdir` | 由 VFS 通用命名空间路径处理                                                          |
| `mr_fs_mount` / `mr_fs_umount`                  | 挂载名字为 `"rootfs"`，支持嵌套挂载；无句柄与子挂载时可正常卸载                      |

## 内部实现

实现位于 `fs/rootfs/rootfs.c`，是预缓存型（无 `lookup` 回调）最小 VFS 后端：

- **`__rootfs_inodes`**：仅实现 `rootfs_inode_create`（限制 `MR_INODE_DIR`）与
  `rootfs_inode_mknod`（透传 `priv`），不实现 `lookup`，缓存缺失时 VFS 直接返回
  `-MR_ENOENT`。
- **启动挂载**：`mr_fs_rootfs_init` 注册 `__rootfs` 描述符后调用
  `mr_fs_mount_root(MR_NULL, "rootfs", 0, MR_NULL)`，将根 inode 原位绑定至
  `fs/struct.c` 定义的静态全局根 dentry `"/"`。

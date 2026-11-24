# RAM 文件系统（RamFS）

纯内存的参考文件系统：目录结构由 VFS
命名空间维护，普通文件由可增长的堆缓冲区支撑，可选支持基于 [FIFO](../kernel/fifo.md)
的非阻塞命名管道；启用后随内核自动注册为 `"ramfs"`。

| 项目   | 说明                                                                                                                                                  |
|--------|-------------------------------------------------------------------------------------------------------------------------------------------------------|
| 功能宏 | `MR_USE_FS_RAMFS`（依赖 `MR_USE_FS`）                                                                                                                 |
| 头文件 | `<mrX/fs/mr_fs_file.h>`（无独立公共头，通过 VFS 接口访问）                                                                                            |
| 依赖   | [文件系统 VFS](fs.md)、[FIFO](../kernel/fifo.md)（`MR_USE_FS_RAMFS_FIFO`）、[内存分配 Malloc](../libc/malloc.md)、[内核初始化](../kernel/initcall.md) |
| 测试   | `test/fs/ramfs.c`、`test/fs/ramfs_matrix.c`、`test/fs/ramfs_rootfs_deep.c`                                                                            |

## 配置

| 配置项                      | 类型 | 默认值 | 说明                                                  |
|-----------------------------|------|--------|-------------------------------------------------------|
| `MR_USE_FS_RAMFS`           | bool | n      | 启用 RamFS                                            |
| `MR_USE_FS_RAMFS_FIFO`      | bool | y      | 启用 FIFO 节点支持；关闭后创建 FIFO 返回 `-MR_ENOSYS` |
| `MR_CFG_FS_RAMFS_FIFO_SIZE` | int  | 32     | 新建 FIFO 的默认字节容量，范围 `1 ~ 1048576`          |

## 快速上手

```c
#include <mrX/fs/mr_fs_file.h>

static int ramfs_demo(void) {
    mr_file_t file = MR_FILE_INIT(file);
    char buffer[16];
    mr_ssize_t ret;

    if ((mr_fs_mkdir("/tmp") != 0) ||
        (mr_fs_mount(MR_NULL, "/tmp", "ramfs", 0, MR_NULL) != 0)) {
        return -1;
    }

    if (mr_fs_open(&file, "/tmp/note.txt", MR_O_RDWR | MR_O_CREAT) != 0) {
        return -1;
    }
    mr_fs_write(&file, "hello ramfs", 11);
    mr_fs_close(&file);

    mr_fs_open(&file, "/tmp/note.txt", MR_O_RDONLY);
    ret = mr_fs_read(&file, buffer, sizeof(buffer));
    mr_fs_close(&file);

    mr_fs_umount("/tmp");
    return (ret == 11) ? 0 : -1;
}
```

## 核心机制与约束

- **普通文件**：写入超过当前长度时通过 `mr_realloc` 自动扩展，文件末尾与写入位置之间的空洞自动补零。读取在
  EOF 处截断。`mr_fs_ftruncate` 支持扩展（补零）与收缩（收缩至 `0` 时释放堆缓冲区）。
  `MR_O_APPEND` 在节点数据锁内调用 `mr_file_apply_append` 定位到文件末尾；失败或零长写不改变原位置。
- **FIFO 节点**：通过 `mr_fs_mkfifo` 或
  `mr_fs_mknod(path, MR_INODE_FIFO, MR_NULL)` 创建，容量由编译期配置
  `MR_CFG_FS_RAMFS_FIFO_SIZE` 固定。读写均 **非阻塞**：队空读出 `0` 字节，队满写入
  `0` 字节。不支持定位（`mr_fs_lseek` 返回 `-MR_ESPIPE`）；`MR_O_TRUNC` 不会清空队列，
  `MR_O_APPEND` 仍向队尾追加。
- **节点级非阻塞并发锁**：同一节点（普通文件或 FIFO）同时只允许一个数据操作（`read`/
  `write`/`ftruncate` 及 `MR_O_TRUNC`）。锁竞争时 `ramfs_node_try_lock` 立即返回
  `-MR_EBUSY`，不自旋等待、不关中断；零长读写不参与锁竞争，直接返回 `0`。
- **数据生命周期**：纯内存存储，掉电或卸载即失。`unlink` 只移除路径名，已打开句柄仍可读写，数据缓冲区或
  FIFO 队列在最后一个 inode 引用释放时回收。

## API 速查

RamFS 不导出独立 API，全部通过 [文件系统 VFS](fs.md) 接口访问，各操作行为如下：

| VFS 接口                             | 作用于 RamFS 的行为                                                    |
|--------------------------------------|------------------------------------------------------------------------|
| `mr_fs_open` + `MR_O_CREAT`          | 创建普通文件（`MR_INODE_REG`），初始无堆缓冲区                         |
| `mr_fs_mkdir` / `mr_fs_readdir`      | 创建目录 / 按 VFS dentry 树枚举目录项                                  |
| `mr_fs_mkfifo` / `mr_fs_mknod(FIFO)` | 创建 FIFO 节点；关闭 `MR_USE_FS_RAMFS_FIFO` 时返回 `-MR_ENOSYS`        |
| `mr_fs_read` / `mr_fs_write`         | 普通文件按偏移读写；FIFO 非阻塞出队/入队                               |
| `mr_fs_lseek`                        | 普通文件正常定位；FIFO 返回 `-MR_ESPIPE`                               |
| `mr_fs_ftruncate`                    | 调整普通文件长度；FIFO 返回 `-MR_EINVAL`                               |
| `mr_fs_fstat`                        | 返回通用字段（`type`、`size`，`mode` 恒为 `0`）                        |
| `mr_fs_fsync` / `mr_fs_ioctl`        | 未实现，返回 `-MR_ENOSYS`                                              |
| `mr_fs_mount` / `mr_fs_umount`       | 挂载名字为 `"ramfs"`，忽略 `source`/`flags`/`data`；卸载后释放实例数据 |

## 内部实现

实现位于 `fs/ramfs/ramfs.c`，可作为自定义文件系统的最小参考模板：

- **节点结构（`ramfs_node_t`）**：非目录节点持有一把 `mr_spinlock_t lock` 和联合体（
  `regular.data` 堆指针或 `fifo` 实例）；目录节点 `inode->priv` 为 `MR_NULL`。
- **自动注册**：`mr_fs_ramfs_init` 通过 `MR_INIT_EXPORT(..., MR_INIT_LEVEL_FS)` 在
  `mr_user_init()` 阶段调用 `mr_file_system_register(&__ramfs)`。`__ramfs` 不设
  `unmount` 回调，所有节点内存在 VFS 引用归零触发 `ramfs_inode_destroy` 时自动释放。

# 文件系统（File-System，VFS）

提供以目录项（dentry）组织的全局路径树、独立的 inode 内容层、基于 `mr_file_t`
句柄的读写接口，以及按名字注册和挂载文件系统的框架；[RamFS](ramfs.md)
和 [RootFS](rootfs.md) 等具体文件系统均构建于其上。

| 项目   | 说明                                                                                                          |
|--------|---------------------------------------------------------------------------------------------------------------|
| 功能宏 | `MR_USE_FS`（自动选中 `MR_USE_REF_LONG_COUNT`）                                                               |
| 头文件 | `<mrX/fs/mr_fs_file.h>`                                                                                       |
| 依赖   | [对象 Object](../kernel/object.md)、[内核初始化](../kernel/initcall.md)、[内存分配 Malloc](../libc/malloc.md) |
| 测试   | `test/fs/`（`file.c`、`file_ops.c`、`dentry.c`、`mount.c`、`concurrency.c` 等）                               |

## 配置

| 配置项                      | 类型 | 默认值 | 说明                                                                                                                      |
|-----------------------------|------|--------|---------------------------------------------------------------------------------------------------------------------------|
| `MR_USE_FS`                 | bool | n      | 启用虚拟文件系统框架                                                                                                      |
| `MR_USE_FS_MULTIUSER`       | bool | n      | 为每个调用者提供独立 `mr_fs_struct_t`（root/cwd）；启用后平台须实现 `mr_fs_struct_current()` 并完成 `mr_fs_struct_init()` |
| `MR_CFG_FS_PATH_LEN`        | int  | 260    | 绝对路径最大长度，也是 `mr_dirent_t.name` 尺寸与 `mr_fs_getcwd` 推荐缓冲区大小                                            |
| `MR_CFG_FS_INLINE_NAME_LEN` | int  | 8      | dentry 内联短名缓冲区长度，超长名字改为堆分配（非文件名长度上限）                                                         |

单用户模式下内核启动时自动通过 `mr_fs_file_init()` 初始化全局命名空间，无需手动初始化。

## 快速上手

```c
#include <mrX/fs/mr_fs_file.h>

static int fs_demo(void) {
    mr_file_t file = MR_FILE_INIT(file);
    char buffer[16];
    mr_ssize_t ret;

    /* 将 ramfs 挂载为全局根（需启用 MR_USE_FS_RAMFS） */
    if (mr_fs_mount_root(MR_NULL, "ramfs", 0, MR_NULL) != 0) {
        return -1;
    }

    if (mr_fs_open(&file, "/hello.txt", MR_O_RDWR | MR_O_CREAT) != 0) {
        return -1;
    }
    mr_fs_write(&file, "hello", 5);
    mr_fs_close(&file);

    if (mr_fs_open(&file, "/hello.txt", MR_O_RDONLY) != 0) {
        return -1;
    }
    ret = mr_fs_read(&file, buffer, sizeof(buffer));
    mr_fs_close(&file);
    return (ret == 5) ? 0 : -1;
}
```

## 核心机制与约束

- **句柄生命周期**：`mr_file_t` 必须先经 `MR_FILE_INIT`、`mr_file_init` 或全零初始化；
  `mr_fs_close` 后句柄回到已关闭状态，可直接复用 `mr_fs_open`。对已打开句柄重复
  `open` 返回 `-MR_EBUSY`，对未打开句柄 `close` 返回 `-MR_EBADF`。`mr_fs_close`
  无论返回值如何都会使句柄失效，不可重试。
- **打开标志**：

| 标志                                        | 说明                                                |
|---------------------------------------------|-----------------------------------------------------|
| `MR_O_RDONLY` / `MR_O_WRONLY` / `MR_O_RDWR` | 访问模式（`MR_O_ACCMODE` 掩码），三选一             |
| `MR_O_CREAT`                                | 不存在则创建普通文件（建目录须用 `mr_fs_mkdir`）    |
| `MR_O_EXCL`                                 | 与 `MR_O_CREAT` 连用，目标已存在时返回 `-MR_EEXIST` |
| `MR_O_TRUNC`                                | 打开成功后将可写普通文件截断为 0                    |
| `MR_O_APPEND`                               | 每次写前由后端在自身数据锁内定位到当前文件末尾      |
| `MR_O_DIRECTORY`                            | 要求目标是目录，否则返回 `-MR_ENOTDIR`              |

- **路径与命名空间规则**：路径长度（不含 `\0`）必须小于 `MR_CFG_FS_PATH_LEN`，否则返回
  `-MR_ENAMETOOLONG`。创建类操作（`mkdir`/`mknod`/`mkfifo`）不接受以 `/`
  结尾的路径，末段名字不能为空、`.` 或 `..`（`-MR_EINVAL`）。`mr_fs_chroot` 之后，`..`
  不会越出新根。
- **目录与遍历**：目录只能以 `MR_O_RDONLY` 打开（可加 `MR_O_DIRECTORY`）。
  `mr_fs_readdir` 返回 `1` 表示读到一项、`0` 表示遍历结束、负数表示错误，循环条件应为
  `ret > 0`。`mr_fs_rmdir` 只能删除空目录（`-MR_ENOTEMPTY`），挂载点目录不可删除或重命名（
  `-MR_EBUSY`）。
- **卸载与引用保活**：`unlink`/`rename` 不影响已打开的句柄，数据在最后一个引用释放后回收。正常
  `mr_fs_umount` 要求目标挂载无子挂载、无在途操作且无打开句柄，否则返回
  `-MR_EBUSY`。`mr_fs_media_detach` 仅用于后端存储介质已物理消失的异常摘除，会将目标及所有子挂载永久置为
  `OFFLINE`，旧句柄后续操作返回 `-MR_ENODEV`。
- **并发与上下文**：VFS API 仅限线程上下文调用。命名空间树由全局关中断自旋锁保护，具体文件系统回调在锁外执行；同一
  inode 上的数据并发控制由具体文件系统自行负责。针对同一目录项的 `mr_fs_unlink` 与
  `mr_fs_rename` 需由调用方串行执行。

## API 速查

**句柄与读写**：

| API                                                                        | 说明                                      |
|----------------------------------------------------------------------------|-------------------------------------------|
| `MR_FILE_INIT(file)` / `void mr_file_init(mr_file_t *file)`                | 静态 / 运行时初始化句柄                   |
| `mr_bool_t mr_file_is_open(const mr_file_t *file)`                         | 查询句柄是否处于打开状态                  |
| `mr_err_t mr_fs_open(mr_file_t *file, const char *path, int flags)`        | 按路径打开或创建文件/目录                 |
| `mr_err_t mr_fs_close(mr_file_t *file)`                                    | 关闭句柄并释放引用                        |
| `mr_ssize_t mr_fs_read(mr_file_t *file, void *buf, mr_size_t size)`        | 读取数据，返回实际读取字节数              |
| `mr_ssize_t mr_fs_write(mr_file_t *file, const void *buf, mr_size_t size)` | 写入数据，返回实际写入字节数              |
| `mr_int64_t mr_fs_lseek(mr_file_t *file, mr_int64_t offset, int whence)`   | 定位文件偏移（`MR_SEEK_SET`/`CUR`/`END`） |
| `mr_err_t mr_fs_ftruncate(mr_file_t *file, mr_int64_t length)`             | 调整可写普通文件长度                      |
| `mr_err_t mr_fs_fsync(mr_file_t *file)`                                    | 同步文件（无 hook 返回 `-MR_ENOSYS`）     |
| `mr_err_t mr_fs_ioctl(mr_file_t *file, int cmd, void *arg)`                | 文件控制（无 hook 返回 `-MR_ENOSYS`）     |
| `mr_err_t mr_fs_fstat(const mr_file_t *file, mr_stat_t *stat)`             | 读取已打开文件状态（`mr_stat_t`）         |

**目录、挂载与命名空间**：

| API                                                                                                     | 说明                                               |
|---------------------------------------------------------------------------------------------------------|----------------------------------------------------|
| `mr_err_t mr_fs_mkdir(const char *path)` / `mr_err_t mr_fs_rmdir(const char *path)`                     | 创建目录 / 删除空目录                              |
| `mr_err_t mr_fs_readdir(mr_file_t *file, mr_dirent_t *dirent)`                                          | 读取下一目录项（`1`=读到，`0`=结束）               |
| `mr_err_t mr_fs_mknod(const char *path, mr_inode_type_t type, void *priv)`                              | 创建指定类型节点并透传 `priv`                      |
| `mr_err_t mr_fs_mkfifo(const char *path)`                                                               | 等价于 `mr_fs_mknod(path, MR_INODE_FIFO, MR_NULL)` |
| `mr_err_t mr_fs_rename(const char *old_path, const char *new_path)`                                     | 重命名或同实例内移动（跨挂载返回 `-MR_EXDEV`）     |
| `mr_err_t mr_fs_unlink(const char *path)`                                                               | 删除非目录节点                                     |
| `mr_err_t mr_fs_mount_root(const char *source, const char *fstype, int flags, void *data)`              | 挂载为全局根（仅初始化阶段使用）                   |
| `mr_err_t mr_fs_mount(const char *source, const char *path, const char *fstype, int flags, void *data)` | 挂载到已存在的目录挂载点                           |
| `mr_err_t mr_fs_umount(const char *path)`                                                               | 卸载挂载点（须先卸载子挂载并关闭句柄）             |
| `mr_err_t mr_fs_media_detach(const char *path)`                                                         | 介质消失后的强制摘除（不可恢复）                   |
| `mr_err_t mr_fs_chdir(const char *path)` / `mr_ssize_t mr_fs_getcwd(char *buf, mr_size_t size)`         | 切换 / 读取当前工作目录                            |
| `mr_err_t mr_fs_chroot(const char *path)`                                                               | 切换当前命名空间根目录                             |

**后端注册与辅助**（供文件系统实现者使用）：

| API                                                       | 说明                                                |
|-----------------------------------------------------------|-----------------------------------------------------|
| `MR_FILE_SYSTEM_INIT(name, mount, unmount)`               | 静态初始化 `mr_file_system_t` 描述符                |
| `mr_err_t mr_file_system_register(mr_file_system_t *fs)`  | 注册文件系统（重名返回 `-MR_EEXIST`）               |
| `mr_file_system_t *mr_file_system_find(const char *name)` | 按名字查找已注册文件系统                            |
| `void mr_file_apply_append(mr_file_t *file)`              | 供写回调在自身数据锁内为 `MR_O_APPEND` 句柄定位 EOF |

**补充语义**：

- **`mr_fs_lseek`**：若后端实现 `fops->llseek` 则直接透传；否则使用默认实现，负位置返回
  `-MR_EINVAL`，有符号 64 位溢出返回 `-MR_ERANGE`，失败时不改变 `file->pos`。
- **`mr_fs_mknod`**：父 inode 若未实现 `mknod` 但实现了 `create`，VFS 会退化调用
  `create`，此时 `priv` 不会被传递。
- **`mr_fs_media_detach`**：不访问已离线设备；若当时存在在途操作，会在置 `OFFLINE`
  后返回 `-MR_EBUSY`，可用同一路径重试回收。超级块最后引用释放时仍会同步调用后端
  `unmount`（要求只释放内存、不得访问设备且不能失败）。

## 内部实现

```text
  mr_fs_* API ---> mr_file_t (dentry/inode 引用 + pos + flags)
                       |
                       v
  mr_fs_struct_t ---> mr_dentry_t 树 (命名空间、MOUNTED 覆盖链)
  (root / cwd)         |
                       v
                   mr_inode_t (ops/fops/size/priv) ---> mr_sblock_t ---> mr_file_system_t
```

实现一个新文件系统只需三步（可参考 `fs/ramfs/ramfs.c`）：用 `MR_FILE_SYSTEM_INIT`
定义描述符（`mount` 必选、`unmount` 可选）；实现所需的 `mr_inode_ops_t` 与
`mr_file_ops_t` 回调；通过 `MR_INIT_EXPORT(..., MR_INIT_LEVEL_FS)` 调用
`mr_file_system_register` 注册。

| 源码位置      | 职责                                                                  |
|---------------|-----------------------------------------------------------------------|
| `fs/file.c`   | 用户态 `mr_fs_*` API 实现、`mr_fs_file_init` 入口、路径解析与并发屏障 |
| `fs/dentry.c` | dentry 树管理、路径查找缓存（`mr_dentry_lookup`）与移动/摘除状态机    |
| `fs/inode.c`  | inode 创建、引用计数与 `destroy` 收尾钩子                             |
| `fs/sblock.c` | 超级块挂载拓扑、`op_cnt`/`open_cnt` 卸载屏障与 `unmount` 回调触发     |
| `fs/struct.c` | 全局根 dentry、文件系统结构锁与单用户 `mr_fs_struct_t` 管理           |
| `fs/system.c` | 文件系统类型全局链表注册与查找                                        |

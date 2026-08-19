[**中文**](fs_file.md) | [**English**](fs_file_en.md)

---

# 文件接口

File 模块提供路径命名空间上的句柄式文件 API：挂载、打开、读写、目录遍历，以及创建、删除、重命名和链接。应用通过 `<mrX.h>` 或 `<mrX/fs/mr_file.h>` 使用这些接口。模块只有在 `MR_USE_FS` 启用时可用。分层关系和实现模块见 [文件系统总览](overview.md)。

相关文档：[Dentry](dentry.md)、[Inode](inode.md)、[File-system](filesystem.md)、[Super-block](superblock.md)、[Namespace](nsp.md)、[RamFS](ramfs.md)、[RootFS](rootfs.md)。

本文说明文件句柄、打开标志、路径解析，以及应用侧常用接口的使用方式。文件系统注册、dentry、inode 和 super-block 的实现细节见对应模块文档。

## 配置

| 选项 | 默认 | 作用 |
|------|------|------|
| `MR_USE_FS` | `n` | 启用文件系统。 |
| `MR_USE_FS_BY_PATH` | `y` | 提供 `mr_fs_*_by_path()` 路径便利接口。 |
| `MR_USE_FS_AT` | `y` | 提供 `mr_fs_*at()` 目录相对接口和 `MR_AT_*` 标志。 |
| `MR_USE_FS_MULTIUSER` | `n` | 每个调用者使用独立的 root/cwd 命名空间。由平台适配层实现 `mr_fs_nsp_current()`。 |
| `MR_CFG_FS_PATH_LEN` | `260` | 绝对路径最大长度，也是 `mr_fs_getcwd()` 的推荐缓冲区大小。 |
| `MR_CFG_FS_SYMLINK_MAX` | `16` | 单次路径查找最多跟随的符号链接层数，超出返回 `-MR_ELOOP`。 |

启用 rootfs 时，启动阶段会注册并调用 `mr_fs_mount_root()` 安装全局 ROOT。之后再用 `mr_fs_mount()` 把其他文件系统挂到已存在的目录上。

## 核心概念

### 文件句柄

```c
typedef struct mr_file {
    mr_dentry_t *dentry;
    mr_inode_t *inode;
    const mr_file_ops_t *fops;
    mr_int64_t pos;
    int flags;
    void *priv;
} mr_file_t;
```

`mr_file_t` 由调用者分配，通常放在栈上或静态存储中。它不是内核对象，不走 `mr_object_get()` / `mr_object_put()`。`inode` 非空表示句柄已打开，`mr_file_is_open()` 据此判断。

首次使用前必须初始化：

```c
mr_file_t file = MR_FILE_INIT(file);
/* 或 */
mr_file_init(&file);
/* 或使用全零存储 */
```

关闭后的句柄可以直接再次 `mr_fs_open()`，不必重新初始化。不要对仍处于打开状态的句柄调用 `mr_file_init()`，那会绕过 `close` 并泄漏 dentry/inode 引用。

### 路径解析

相对路径相对当前工作目录解析，绝对路径从当前命名空间的 ROOT 开始。路径查找会跟随中间的符号链接；最终分量是否跟随由打开标志决定。

`mr_fs_open()` 默认跟随最终符号链接。带 `MR_O_NOFOLLOW` 时打开链接本身，供 `mr_fs_readlink()` 使用。

### 打开标志

```c
#define MR_O_RDONLY     (0x0000)
#define MR_O_WRONLY     (0x0001)
#define MR_O_RDWR       (0x0002)
#define MR_O_ACCMODE    (0x0003)
#define MR_O_CREAT      (0x0100)
#define MR_O_EXCL       (0x0200)
#define MR_O_TRUNC      (0x0400)
#define MR_O_APPEND     (0x0800)
#define MR_O_DIRECTORY  (0x1000)
#define MR_O_NOFOLLOW   (0x2000)
```

| 标志 | 含义 |
|------|------|
| `MR_O_RDONLY` / `WRONLY` / `RDWR` | 访问模式，必须是三者之一。 |
| `MR_O_CREAT` | 节点不存在时创建普通文件。不能用它创建目录，目录用 `mr_fs_mkdir()`。 |
| `MR_O_EXCL` | 与 `MR_O_CREAT` 一起使用时，节点已存在返回 `-MR_EEXIST`。 |
| `MR_O_TRUNC` | 对可写普通文件在成功打开后截断为 0。打开失败不会丢掉原有数据。 |
| `MR_O_APPEND` | 每次写入前把位置移到当前文件末尾。失败或零长度写入不改变原先位置。 |
| `MR_O_DIRECTORY` | 目标必须是目录，否则 `-MR_ENOTDIR`。 |
| `MR_O_NOFOLLOW` | 不跟随最终符号链接。 |

目录只能以只读方式打开，否则返回 `-MR_EISDIR`。已打开的句柄再次 `mr_fs_open()` 返回 `-MR_EBUSY`。

启用 `MR_USE_FS_AT` 后还有路径解析标志：

```c
#define MR_AT_IN_ROOT            (0x4000)
#define MR_AT_SYMLINK_NOFOLLOW   (0x8000)
```

`MR_AT_IN_ROOT` 把目录句柄当作临时 ROOT：父路径不能逃出该目录，绝对路径和符号链接也从该目录重新开始。`mr_fs_open()` 不能携带 `MR_AT_IN_ROOT`。`mr_fs_openat()` 的 `flags` 同时携带 `MR_O_*` 和 `MR_AT_IN_ROOT`。

## 模块接口

### 挂载

```c
mr_err_t mr_fs_mount_root(const char *source, const char *fstype, int flags,
                          void *data);
mr_err_t mr_fs_mount(const char *source, const char *path, const char *fstype,
                     int flags, void *data);
mr_err_t mr_fs_umount(const char *path);
```

`mr_fs_mount_root()` 安装或替换全局 FS ROOT，不访问当前命名空间。`fstype` 必须已通过 `mr_file_system_register()` 注册。`flags` 目前保留，传 `0`。

命名空间就绪后，用 `mr_fs_mount()` 把文件系统挂到路径上。非根挂载点必须已经存在且为目录。路径按当前命名空间解析。

`mr_fs_umount()` 不递归卸载。若挂载点下还有子挂载，返回 `-MR_EBUSY`，需要先卸子挂载。

### 打开和关闭

```c
mr_err_t mr_fs_open(mr_file_t *file, const char *path, int flags);
mr_err_t mr_fs_close(mr_file_t *file);
mr_bool_t mr_file_is_open(const mr_file_t *file);
```

`mr_fs_close()` 无论返回值如何都会销毁句柄。返回非 0 只表示 flush 或句柄收尾失败，不能重试。对已关闭句柄再 close 返回 `-MR_EBADF`。

### 创建和删除

```c
mr_err_t mr_fs_mkdir(const char *path);
mr_err_t mr_fs_rmdir(const char *path);
mr_err_t mr_fs_mknod(const char *path, mr_inode_type_t type, void *priv);
mr_err_t mr_fs_unlink(mr_file_t *file);
mr_err_t mr_fs_unlink_recursive(mr_file_t *file);
```

`mr_fs_mkdir()` 只创建一级目录，父目录必须已存在，否则 `-MR_ENOENT`；叶子已存在返回 `-MR_EEXIST`。

`mr_fs_rmdir()` 只删除空目录，且不跟随最终符号链接。目标不是目录时返回 `-MR_ENOTDIR`，非空目录返回 `-MR_ENOTEMPTY`，挂载点返回 `-MR_EBUSY`。

`mr_fs_unlink()` 删除已打开句柄的当前名字。句柄在名字去掉后仍保持打开。非空目录和挂载点的限制与 `rmdir` 相同。目录删除也可以走 `unlink`，`rmdir` 额外要求目标必须是目录。

`mr_fs_unlink_recursive()` 递归删除打开的文件或目录。符号链接只删链接本身。操作不是原子的：中途失败时，已经删掉的条目不会恢复。

### 读写和定位

```c
mr_ssize_t mr_fs_read(mr_file_t *file, void *buf, mr_size_t size);
mr_ssize_t mr_fs_write(mr_file_t *file, const void *buf, mr_size_t size);
mr_int64_t mr_fs_seek(mr_file_t *file, mr_int64_t offset, int whence);
mr_int64_t mr_fs_tell(mr_file_t *file);
mr_err_t mr_fs_truncate(mr_file_t *file, mr_int64_t length);
mr_err_t mr_fs_sync(mr_file_t *file);
mr_err_t mr_fs_ioctl(mr_file_t *file, int cmd, void *arg);
```

`read` / `write` 成功返回字节数，EOF 时 `read` 返回 `0`，失败返回负错误码。

`whence` 为 `MR_SEEK_SET`、`MR_SEEK_CUR` 或 `MR_SEEK_END`。默认实现把负位置钳到 `0`。若结果无法用 `mr_int64_t` 表示，返回 `-MR_ERANGE` 且不改变当前位置。

`mr_fs_truncate()` 要求句柄可写。目录返回 `-MR_EISDIR`，其他非普通文件返回 `-MR_EINVAL`。`mr_fs_sync()` 在文件系统未提供 sync 钩子时返回 `-MR_ENOSYS`。

### 目录遍历

```c
mr_err_t mr_fs_opendir(mr_file_t *file, const char *path);
mr_err_t mr_fs_readdir(mr_file_t *file, mr_dirent_t *dirent);
mr_err_t mr_fs_closedir(mr_file_t *file);
```

`mr_fs_opendir()` 等价于 `mr_fs_open(file, path, MR_O_DIRECTORY | MR_O_RDONLY)`。`mr_fs_closedir()` 与 `mr_fs_close()` 契约相同。

`mr_fs_readdir()` 的返回值不是普通的“0 表示成功”：

| 返回值 | 含义 |
|--------|------|
| `1` | 读到一条目录项，结果在 `dirent` 中。 |
| `0` | 目录结束。 |
| 负数 | 错误码。 |

应按 `while ((ret = mr_fs_readdir(file, &dirent)) > 0)` 迭代。

### 状态、链接和重命名

```c
mr_err_t mr_fs_stat(const mr_file_t *file, mr_stat_t *stat);
mr_err_t mr_fs_link(const mr_file_t *file, const char *new_path);
mr_err_t mr_fs_symlink(const char *target, const char *link_path);
mr_ssize_t mr_fs_readlink(const mr_file_t *file, char *buf, mr_size_t size);
mr_err_t mr_fs_rename(mr_file_t *file, const char *new_path);
```

`mr_fs_stat()` 在文件被 unlink 后仍可用。文件系统可通过 stat 钩子覆盖通用字段。

`mr_fs_link()` 拒绝目录硬链接（`-MR_EPERM`）和跨文件系统链接（`-MR_EXDEV`）。`mr_fs_rename()` 在目标已存在时返回 `-MR_EEXIST`，跨文件系统返回 `-MR_EXDEV`，挂载点目录返回 `-MR_EBUSY`。

`mr_fs_readlink()` 需要用 `MR_O_NOFOLLOW` 打开符号链接。返回值是目标长度，不含结束符。若缓冲区比目标长，会在目标后写入 `\0`；缓冲区不够长时不保证以 `\0` 结尾。

### 按路径接口

启用 `MR_USE_FS_BY_PATH` 后可用：

```c
mr_err_t mr_fs_stat_by_path(const char *path, mr_stat_t *stat);
mr_err_t mr_fs_rename_by_path(const char *old_path, const char *new_path);
mr_err_t mr_fs_unlink_by_path(const char *path);
mr_err_t mr_fs_unlink_recursive_by_path(const char *path);
```

这些接口内部完成查找，不必先打开句柄。`mr_fs_stat_by_path()` 跟随最终符号链接。关掉该选项后，对应函数不存在，需要先 `open` 再使用句柄接口。

### 目录相对接口

启用 `MR_USE_FS_AT` 后可用：

```c
mr_err_t mr_fs_mkdirat(const mr_file_t *dir, const char *path, int flags);
mr_err_t mr_fs_rmdirat(const mr_file_t *dir, const char *path, int flags);
mr_err_t mr_fs_mknodat(const mr_file_t *dir, const char *path,
                       mr_inode_type_t type, void *priv, int flags);
mr_err_t mr_fs_openat(mr_file_t *file, const mr_file_t *dir, const char *path,
                      int flags);
```

相对路径要求 `dir` 是已打开的目录句柄。绝对路径从当前 FS ROOT 开始，并忽略 `dir`，除非指定了 `MR_AT_IN_ROOT`。

同时启用 `MR_USE_FS_AT` 和 `MR_USE_FS_BY_PATH` 时还有：

```c
mr_err_t mr_fs_statat_by_path(const mr_file_t *dir, const char *path,
                              mr_stat_t *stat, int flags);
mr_err_t mr_fs_renameat_by_path(const mr_file_t *old_dir, const char *old_path,
                                const mr_file_t *new_dir, const char *new_path,
                                int flags);
mr_err_t mr_fs_unlinkat_by_path(const mr_file_t *dir, const char *path,
                                int flags);
mr_err_t mr_fs_unlinkat_recursive_by_path(const mr_file_t *dir,
                                          const char *path, int flags);
```

`mr_fs_statat_by_path()` 在带 `MR_AT_SYMLINK_NOFOLLOW` 时返回最终符号链接自身的状态。

### 工作目录和 chroot

```c
mr_err_t mr_fs_chroot(const char *path);
mr_err_t mr_fs_chdir(const char *path);
mr_ssize_t mr_fs_getcwd(char *buf, mr_size_t size);
```

`mr_fs_chroot()` 按当前上下文解析新 ROOT。若当前工作目录仍落在新 ROOT 内则保留，否则重置为新 ROOT，避免相对路径逃出。已经打开的目录句柄不受影响。

`mr_fs_getcwd()` 成功时返回路径长度，不含 `\0`，并保证缓冲区以 `\0` 结尾。ROOT 上的工作目录写入 `"/"`，返回 `1`。缓冲区不够时返回 `-MR_ERANGE`。推荐缓冲区大小为 `MR_CFG_FS_PATH_LEN`。

## 使用示例

### 打开、读写、关闭

```c
#include <mrX.h>

void sample_file_rw(void)
{
    mr_file_t file = MR_FILE_INIT(file);
    char buf[16];
    mr_ssize_t n;

    if (mr_fs_open(&file, "/note", MR_O_RDWR | MR_O_CREAT) != 0) {
        return;
    }

    mr_fs_write(&file, "hello", 5);
    mr_fs_seek(&file, 0, MR_SEEK_SET);
    n = mr_fs_read(&file, buf, sizeof(buf));
    (void)n;

    mr_fs_close(&file);
}
```

### 创建和删除目录

```c
void sample_dir_create_remove(void)
{
    if (mr_fs_mkdir("/data") != 0) {
        return;
    }

    mr_fs_rmdir("/data");
}
```

### 遍历目录

```c
void sample_listdir(void)
{
    mr_file_t dir = MR_FILE_INIT(dir);
    mr_dirent_t ent;
    mr_err_t ret;

    if (mr_fs_opendir(&dir, "/") != 0) {
        return;
    }

    while ((ret = mr_fs_readdir(&dir, &ent)) > 0) {
        /* Use ent.name and ent.type. */
    }

    mr_fs_closedir(&dir);
}
```

### 按路径删除

```c
void sample_unlink_by_path(void)
{
#if defined(MR_USE_FS_BY_PATH)
    mr_fs_unlink_by_path("/note");
#else
    mr_file_t file = MR_FILE_INIT(file);

    if (mr_fs_open(&file, "/note", MR_O_RDONLY) == 0) {
        mr_fs_unlink(&file);
        mr_fs_close(&file);
    }
#endif
}
```

## 注意事项

- 管理类接口成功返回 `0`，失败返回负错误码。`read` / `write` / `seek` / `tell` / `getcwd` / `readlink` 成功返回非负长度或位置。
- `mr_fs_readdir()` 的 `0` 表示目录结束，不是成功。
- `mr_fs_close()` 失败后句柄已经无效，不要再次 close。
- 不要对打开中的句柄调用 `mr_file_init()`。
- `mkdir` 不递归创建父目录；`rmdir` 不删除非空目录。
- 递归删除不是原子操作。
- 挂载 `flags` 目前保留，调用时传 `0`。
- 多用户命名空间由平台适配层提供，应用代码不直接包含或初始化 `mr_nsp.h`。

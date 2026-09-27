# 文件系统（File-System，VFS）

> 文件系统核心提供一棵以目录项（dentry）组织的全局路径树、独立的 inode 内容层与基于
> 文件句柄的 open/read/write API，并附带按名字注册/挂载文件系统的框架；RamFS、RootFS
> 等具体文件系统都构建在它之上。

| 项       | 说明                                                                                                               |
|----------|--------------------------------------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_FS`（默认关闭，自动选中 `MR_USE_REF_LONG_COUNT`）                                                          |
| 头文件   | `<mrX/fs/mr_fs_file.h>`                                                                                            |
| 依赖模块 | [对象 Object](../kernel/object.md)、[初始化 Initcall](../kernel/initcall.md)、[动态内存 malloc](../libc/malloc.md) |
| 参考测试 | `mrX/test/fs/`（file.c、file_ops.c、dentry.c、mount.c 等）                                                         |

---

## 用户指南

### 1. 模块概述

本模块是 mrX 的虚拟文件系统（VFS）核心，解决的问题是： **用统一的路径与句柄接口访问 不同来源的文件、目录与设备节点**。

特性：

- 路径树与内容分离：目录项（dentry）组成命名空间树，inode 携带类型、大小与数据。
- 句柄式 I/O：`mr_fs_open` 打开后通过 `mr_file_t` 句柄读写；rename/unlink 不会 使句柄失效，正常卸载则会在句柄关闭前返回
  `-MR_EBUSY`。
- 支持目录、普通文件、设备节点与 FIFO 等节点类型。
- 支持按名字注册文件系统，并在任意目录挂载点上挂载/卸载（见
  [RamFS](ramfs.md)、[RootFS](rootfs.md)）。
- 支持工作目录（`chdir`/`getcwd`）与根目录切换（`chroot`），可选按任务隔离的 多用户命名空间。

具体的数据存储由挂载的文件系统提供：RamFS 提供内存中的普通文件与 FIFO， RootFS 只维护目录结构，两者都不需要用户直接操作内部对象。

### 2. 配置与依赖

| 配置项                      | 类型 | 默认值 | 说明                                                                                                                                           |
|-----------------------------|------|--------|------------------------------------------------------------------------------------------------------------------------------------------------|
| `MR_USE_FS`                 | bool | n      | 启用文件系统框架（`mrX/include/mrX/fs/Kconfig:6`）                                                                                             |
| `MR_USE_FS_MULTIUSER`       | bool | n      | 为每个调用者提供独立文件系统结构（root/cwd）；启用后平台必须实现 `mr_fs_struct_current()`，并确保返回的结构已通过 `mr_fs_struct_init()` 初始化 |
| `MR_CFG_FS_PATH_LEN`        | 整型 | 260    | 绝对路径最大长度（含结尾符的推荐缓冲区大小），也是 `mr_dirent_t.name` 的尺寸                                                                   |
| `MR_CFG_FS_INLINE_NAME_LEN` | 整型 | 8      | 目录项对象内联短名缓冲区长度；更长的名字改为堆分配。它不是文件名长度上限                                                                       |

启用 `MR_USE_FS` 并重新生成 `mr_config.h` 后，包含头文件即可使用：

```c
#include <mrX/fs/mr_fs_file.h>
```

该头文件会自动引入挂载框架、dentry、inode 等依赖头。内核初始化时会自动初始化 全局命名空间（单用户模式），无需手动调用初始化函数。

### 3. 快速上手

以 [RamFS](ramfs.md) 作为根文件系统，创建并读回一个文件：

```c
#include <mrX/fs/mr_fs_file.h>

static int fs_demo(void) {
    mr_file_t file = MR_FILE_INIT(file);
    char buffer[16];
    mr_ssize_t ret;

    /* Mount ramfs as the global root (requires MR_USE_FS_RAMFS) */
    if (mr_fs_mount_root(MR_NULL, "ramfs", 0, MR_NULL) != 0) {
        return -1;
    }

    /* Create and write a file */
    if (mr_fs_open(&file, "/hello.txt", MR_O_RDWR | MR_O_CREAT) != 0) {
        return -1;
    }
    mr_fs_write(&file, "hello", 5);
    mr_fs_close(&file);

    /* Read it back */
    if (mr_fs_open(&file, "/hello.txt", MR_O_RDONLY) != 0) {
        return -1;
    }
    ret = mr_fs_read(&file, buffer, sizeof(buffer));
    mr_fs_close(&file);
    return (ret == 5) ? 0 : -1;
}
```

要点：

- 句柄用 `MR_FILE_INIT` 初始化（或全零存储），同一个句柄关闭后可以再次 `open`。
- 路径以 `/` 开头为绝对路径，否则相对当前工作目录（初始为根 `/`）。
- `MR_O_CREAT` 只创建普通文件；创建目录用 `mr_fs_mkdir`。

### 4. 使用说明

**句柄生命周期**

- 静态方式：定义 `mr_file_t` 并用 `MR_FILE_INIT`（或 `mr_file_init`、全零存储） 初始化；`mr_fs_close`
  后句柄回到"已关闭"状态，可重复 `open`，无需重新初始化。
- 句柄只有打开/关闭两种状态，可用 `mr_file_is_open()` 查询当前状态；对一个 已打开的句柄重复 `open` 返回 `-MR_EBUSY`。
- `mr_fs_close` 无论返回值如何都会使句柄失效；返回负数只表示后端 flush/收尾失败， 不可重试。对未打开句柄 `close` 返回
  `-MR_EBADF`。
- 介质被移除（`mr_fs_media_detach`）后，打开句柄上的新操作返回 `-MR_ENODEV`； 对此类句柄 `close` 仍会调用后端收尾以释放私有内存，并返回
  `-MR_ENODEV`。

**并发访问**

- VFS 只保证命名空间操作（open/mkdir/rename/unlink/mount 等）之间以及它们与卸载 之间的一致性；同一文件上的数据读写是否可以并发、冲突时如何表现，由具体文件系统
  决定并在其文档中说明（如 [RamFS](ramfs.md) 在冲突时返回 `-MR_EBUSY`）。

**打开标志**

| 标志                                        | 说明                                                   |
|---------------------------------------------|--------------------------------------------------------|
| `MR_O_RDONLY` / `MR_O_WRONLY` / `MR_O_RDWR` | 访问模式（`MR_O_ACCMODE` 取出），三选一                |
| `MR_O_CREAT`                                | 不存在则创建（只创建普通文件）                         |
| `MR_O_EXCL`                                 | 与 `MR_O_CREAT` 连用，已存在则报 `-MR_EEXIST`          |
| `MR_O_TRUNC`                                | 打开成功后将可写普通文件截断为 0 长                    |
| `MR_O_APPEND`                               | 每次写前由可写文件系统在自身数据锁内定位到当前文件末尾 |
| `MR_O_DIRECTORY`                            | 要求目标是目录，否则 `-MR_ENOTDIR`                     |

**路径与命名空间规则**

- 路径长度（不含结尾符）必须小于 `MR_CFG_FS_PATH_LEN`，否则 `-MR_ENAMETOOLONG`。
- 创建类操作（`mkdir`/`mknod`/`mkfifo` 的目标）不接受以 `/` 结尾 的路径，末段名字不能为空、`.` 或 `..`（`-MR_EINVAL`）。
- `..` 不会越过当前命名空间根：`chroot` 之后 `..` 无法逃出新根。
- 单用户模式下全局只有一份 root/cwd，由 `mr_fs_file_init()` 自动调用 `mr_fs_struct_init()` 初始化。`MR_USE_FS_MULTIUSER`
  启用后，这个自动初始化分支被编译掉；平台需为每个调用者提供 `mr_fs_struct_current()`，并负责让对应 `mr_fs_struct_t` 在使用前完成
  `mr_fs_struct_init()`。

**目录操作**

- 目录只能以 `MR_O_RDONLY` 打开（可加 `MR_O_DIRECTORY`）。
- `mr_fs_readdir` 返回 `1` 表示读到一项（`mr_dirent_t.name` + `type`），返回 `0`
  表示目录结束，负数为错误；循环条件是返回值大于 0。
- `mr_fs_rmdir` 只能删除空目录（`-MR_ENOTEMPTY`）；挂载点目录不能删除 （`-MR_EBUSY`）。

**卸载与打开句柄**

- `mr_fs_unlink`/`mr_fs_rename` 作用于路径，已打开的句柄不受影响：unlink 后句柄 仍可读写，`mr_fs_fstat`
  仍能取到状态，数据在最后一个引用释放后回收。
- 正常卸载要求目标挂载实例没有打开句柄；只要 `open_cnt` 非零，
  `mr_fs_umount` 就返回 `-MR_EBUSY`，句柄仍然有效，应先 `mr_fs_close` 再重试。
- 卸载一个还有子挂载的挂载点返回 `-MR_EBUSY`，必须先卸载子挂载。

### 5. API 参考

句柄初始化与打开/关闭：

| API               | 说明                                 |
|-------------------|--------------------------------------|
| `MR_FILE_INIT`    | 静态初始化宏（全零等价的句柄初始化） |
| `mr_file_init`    | 运行时初始化句柄                     |
| `mr_file_is_open` | 查询句柄是否处于打开状态             |
| `mr_fs_open`      | 按路径打开（或按标志创建）文件/目录  |
| `mr_fs_close`     | 关闭句柄并使句柄失效                 |

数据读写与控制：

| API               | 说明                                                           |
|-------------------|----------------------------------------------------------------|
| `mr_fs_read`      | 读文件，返回读取字节数                                         |
| `mr_fs_write`     | 写文件，返回写入字节数；`MR_O_APPEND` 由后端在数据锁内定位 EOF |
| `mr_fs_lseek`     | 定位，返回新位置                                               |
| `mr_fs_ftruncate` | 将普通文件截断/扩展到指定长度                                  |
| `mr_fs_fsync`     | 刷新文件（无 hook 返回 `-MR_ENOSYS`）                          |
| `mr_fs_ioctl`     | 文件 I/O 控制（无 hook 返回 `-MR_ENOSYS`）                     |
| `mr_fs_fstat`     | 读取打开文件的状态（`mr_stat_t`）                              |

目录与路径元数据：

| API                             | 说明                                               |
|---------------------------------|----------------------------------------------------|
| `mr_fs_mkdir` / `mr_fs_rmdir`   | 创建目录 / 删除空目录                              |
| `mr_fs_readdir`                 | 读取下一目录项（1=读到，0=结束）                   |
| `mr_fs_mknod`                   | 创建指定类型的节点（`mr_inode_type_t` + 私有数据） |
| `mr_fs_mkfifo`                  | 等价于 `mr_fs_mknod(path, MR_INODE_FIFO, MR_NULL)` |
| `mr_fs_rename` / `mr_fs_unlink` | 重命名 / 删除名字（目录用 `rmdir`）                |

挂载框架：

| API                  | 说明                                                           |
|----------------------|----------------------------------------------------------------|
| `mr_fs_mount_root`   | 将文件系统挂载为全局根（不经过当前命名空间；仅初始化阶段使用） |
| `mr_fs_mount`        | 挂载到已存在的目录挂载点                                       |
| `mr_fs_umount`       | 卸载指定路径上的挂载                                           |
| `mr_fs_media_detach` | 介质消失后的强制摘除（非通用强制卸载，见下方说明）             |

命名空间：

| API                            | 说明                    |
|--------------------------------|-------------------------|
| `mr_fs_chdir` / `mr_fs_getcwd` | 切换 / 读取当前工作目录 |
| `mr_fs_chroot`                 | 切换当前命名空间根      |

文件系统注册（供文件系统实现者）：

| API                       | 说明                                                    |
|---------------------------|---------------------------------------------------------|
| `MR_FILE_SYSTEM_INIT`     | 静态初始化文件系统描述符（名字 + mount/unmount 回调）   |
| `mr_file_system_register` | 注册文件系统（重名返回 `-MR_EEXIST`）                   |
| `mr_file_system_find`     | 按名字查找已注册文件系统                                |
| `mr_file_apply_append`    | 供后端写回调在自身数据锁内为 `MR_O_APPEND` 句柄选取 EOF |

**`mr_fs_open`**

```c
mr_err_t mr_fs_open(mr_file_t *file, const char *path, int flags);
```

- `file`：已初始化的句柄；已打开的句柄返回 `-MR_EBUSY`。
- `path`：文件路径；`flags` 见第 4 节打开标志表。
- 常见错误：`-MR_ENOENT`（不存在且无 `MR_O_CREAT`）、`-MR_EEXIST`
  （`MR_O_CREAT|MR_O_EXCL`）、`-MR_EISDIR`（目录非只读打开）、`-MR_ENOTDIR`。
- 返回：成功 `0`，失败负数错误码。

**`mr_fs_read` / `mr_fs_write`**

```c
mr_ssize_t mr_fs_read(mr_file_t *file, void *buf, mr_size_t size);
mr_ssize_t mr_fs_write(mr_file_t *file, const void *buf, mr_size_t size);
```

成功返回实际读/写的字节数（可为 0，如读到 EOF），失败返回负数错误码。只写句柄
`read`、只读句柄 `write` 返回 `-MR_EBADF`；目录的读写返回 `-MR_EISDIR`； 后端未提供该操作返回 `-MR_ENOSYS`。

**`mr_fs_lseek`**

```c
mr_int64_t mr_fs_lseek(mr_file_t *file, mr_int64_t offset, int whence);
```

`whence` 取 `MR_SEEK_SET` / `MR_SEEK_CUR` / `MR_SEEK_END`。若后端提供 `fops->llseek`，VFS 直接调用并原样返回其结果，位置语义也由后端负责。只有在没有自定义
`llseek` 时才使用默认实现：负目标位置返回 `-MR_EINVAL`，有符号加法溢出返回 `-MR_ERANGE`，失败时 `file->pos` 保持不变。FIFO
等不可定位对象通常由后端 `llseek` 返回 `-MR_ESPIPE`。

**`mr_fs_mknod`**

```c
mr_err_t mr_fs_mknod(const char *path, mr_inode_type_t type, void *priv);
```

`type` 为 `MR_INODE_REG`/`MR_INODE_DIR`/`MR_INODE_CHR`/`MR_INODE_BLK`/`MR_INODE_FIFO` 之一。若父 inode 实现 `mknod`，
`priv` 会原样透传；若没有 `mknod` 但实现了 `create`，VFS 会退化为 `create(dir, name, type, &inode)`，此时没有 `priv`
参数，调用者不能依赖私有数据被传递。父目录必须已存在（`-MR_ENOENT`），同名节点返回 `-MR_EEXIST`。

**`mr_fs_mount` / `mr_fs_umount`**

```c
mr_err_t mr_fs_mount(const char *source, const char *path, const char *fstype,
                     int flags, void *data);
mr_err_t mr_fs_umount(const char *path);
```

- `fstype` 必须是已注册的文件系统名（`-MR_ENOENT`）。
- 非根挂载点必须已存在且为目录（`-MR_ENOENT`/`-MR_ENOTDIR`）；重复挂载同一 挂载点返回 `-MR_EBUSY`。挂载后原目录内容被覆盖隐藏，卸载后恢复。
- `source`/`flags`/`data` 透传给文件系统挂载回调（内建文件系统忽略它们）。
- 正常卸载会原子检查挂载状态；有子挂载、在途操作或打开句柄时返回
  `-MR_EBUSY`，应先关闭句柄并按从子到父的顺序卸载。

**`mr_fs_media_detach`**

```c
mr_err_t mr_fs_media_detach(const char *path);
```

- 仅用于后端介质已经消失的异常摘除，不是通用强制卸载。
- 将目标挂载及全部子挂载永久标记为 `OFFLINE` 并从可见命名空间摘除，打开句柄此后的新操作返回 `-MR_ENODEV`，状态不可恢复。
- 有在途操作或其他不可回收状态时本次调用可返回 `-MR_EBUSY`，可用同一路径重试。OFFLINE 摘除逻辑本身不访问已消失设备；当某个
  super-block 的最后引用在此过程中或之后释放时，仍会同步进入文件系统的 `unmount` 清理钩子。该钩子的公共契约要求只释放内存资源、不得访问设备且不能失败。

### 6. 完整示例

示例 1：目录创建、遍历与删除（取材自 `mrX/test/fs/file_ops.c`）：

```c
static void dir_walk_demo(void) {
    mr_file_t dir = MR_FILE_INIT(dir);
    mr_file_t file = MR_FILE_INIT(file);
    mr_dirent_t dirent;
    mr_ssize_t ret;

    mr_fs_mkdir("/log");
    mr_fs_open(&file, "/log/a.txt", MR_O_RDWR | MR_O_CREAT);
    mr_fs_close(&file);
    mr_fs_open(&file, "/log/b.txt", MR_O_RDWR | MR_O_CREAT);
    mr_fs_close(&file);

    /* Open the directory and enumerate its children */
    if (mr_fs_open(&dir, "/log", MR_O_RDONLY | MR_O_DIRECTORY) == 0) {
        while ((ret = mr_fs_readdir(&dir, &dirent)) > 0) {
            /* dirent.name and dirent.type describe one child */
        }
        /* ret == 0 means end-of-directory */
        mr_fs_close(&dir);
    }
    mr_fs_rmdir("/log");    /* Fails with -MR_ENOTEMPTY while children exist */
}
```

示例 2：子目录挂载、工作目录切换与卸载（取材自 `mrX/test/fs/mount.c`）：

```c
static void mount_demo(void) {
    mr_file_t file = MR_FILE_INIT(file);

    mr_fs_mkdir("/mnt");
    mr_fs_mount(MR_NULL, "/mnt", "ramfs", 0, MR_NULL);

    /* /mnt is now the root of a separate ramfs instance */
    mr_fs_open(&file, "/mnt/data", MR_O_RDWR | MR_O_CREAT);
    mr_fs_close(&file);

    mr_fs_chdir("/mnt");                        /* CWD is now /mnt */
    mr_fs_open(&file, "data", MR_O_RDONLY);     /* Relative to CWD */
    mr_fs_close(&file);
    mr_fs_chdir("/");

    /* Unmount children first, then the mount itself */
    mr_fs_umount("/mnt");
}
```

更多行为参考：打开标志、路径解析与目录策略见
`mrX/test/fs/file_ops.c`，挂载/卸载与 chroot 见 `mrX/test/fs/mount.c`。

### 7. 常见问题（FAQ）

**Q：挂载返回 `-MR_EBUSY`？**

目标挂载点已被占用：全局根只能挂载一次，同一目录上不允许叠加第二个文件系统。 先 `mr_fs_umount` 旧挂载，或换一个挂载点。

**Q：卸载返回 `-MR_EBUSY`？**

该挂载实例仍有打开句柄、在途操作或子挂载。先关闭所有相关句柄，等待在途操作 结束，并按从子到父的顺序卸载。另外挂载点目录本身不能用
`mr_fs_rmdir`/`mr_fs_unlink` 删除。

**Q：文件被 unlink / rename 后，句柄还能用吗？**

能。句柄在打开时固定了 inode 与 dentry 引用，unlink 只移除路径名，rename 只改 名字；数据与读写位置保持到 `mr_fs_close`
为止。句柄仍可通过
`mr_fs_fstat` 读取 inode 的当前状态。

**Q：`mr_fs_readdir` 返回 0 是错误吗？**

不是。返回 `1` 表示读到一项，`0` 表示目录遍历结束，负数才是错误。

**Q：为什么 `MR_O_CREAT` 打开 `/dir` 失败？**

`MR_O_CREAT` 只创建普通文件；`MR_O_CREAT|MR_O_DIRECTORY` 组合直接返回
`-MR_ENOTDIR`。创建目录必须用 `mr_fs_mkdir`。

**Q：`mr_fs_getcwd` 返回 `-MR_ERANGE` / `-MR_ENOENT`？**

`-MR_ERANGE` 表示缓冲区小于工作目录路径长度（推荐使用
`MR_CFG_FS_PATH_LEN` 大小的缓冲区）；`-MR_ENOENT` 表示当前工作目录所在挂载已被 卸载，需要先 `mr_fs_chdir` 到有效目录。

---

## 开发者指南

### 8. 架构总览

VFS 位于路径 API 与具体文件系统之间，由五个对象层组成：

```
              mr_fs_open("/a/b")            用户句柄层
+------------------+   路径解析    +------------------+
| mr_file_t        |<-------------| mr_fs_* API      |
+--------+---------+              +--------+---------+
         | 引用 dentry / inode
+--------v---------+  命名空间树   +------------------+
| mr_dentry_t 树    |<------------| mr_fs_struct_t   |  root / cwd
+--------+---------+             +------------------+
         | 绑定 inode                    ^
+--------v---------+  内容/后端     | 挂载拓扑
| mr_inode_t       |<------------+--------------------+
+--------+---------+             | mr_sblock_t   |----> mr_file_system_t
         | priv 数据             | (mnt / root_dentry)|      ("ramfs"/"rootfs"/自定义)
+--------v---------+             +--------------------+
| 文件系统私有数据   |
+------------------+
```

- `mr_file_system_t` 是文件系统"类型"（名字 + mount/unmount 回调）， 通过 `mr_file_system_register` 注册到全局链表（
  `mrX/fs/system.c:18`）。
- 每次挂载生成一个 `mr_sblock_t` 实例；根挂载"原位"占用全局根 dentry， 子挂载在挂载点 dentry 与"已挂载根" dentry 之间建立
  `mounted` 覆盖链 （`mrX/fs/sblock.c:195`）。
- 文件系统结构（root/cwd）由 `mr_fs_struct_t` 持有；单用户模式下是全局静态实例 （`mrX/fs/struct.c:20`），初始化发生在内核初始化序列
  `mrX/kernel/initcall.c:31`
  调用的 `mr_fs_file_init()`（`mrX/fs/file.c:1897`）。

### 9. 数据结构

`mr_dentry_t`（`mrX/include/mrX/fs/mr_fs_dentry.h:58`）——命名空间树节点：

| 字段                | 说明                                                                                                                                                                                                                                                                                               |
|---------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `parent`            | 对象基类，提供引用计数与 dentry clazz 类型检查                                                                                                                                                                                                                                                     |
| `iname` / `name`    | 内联短名缓冲区与名字三元组（`str`/`hash`/`len`）；长名字堆分配                                                                                                                                                                                                                                     |
| `mounted`           | 非空表示该挂载点被某个已挂载根覆盖（挂载链）                                                                                                                                                                                                                                                       |
| `upper`             | 父 dentry（持有其引用）；根的 `upper` 为空                                                                                                                                                                                                                                                         |
| `sibling` / `lower` | 兄弟链表节点 / 子 dentry 链表表头                                                                                                                                                                                                                                                                  |
| `ops`               | 名字 hash/compare 操作，空表示默认 djb2-xor；子节点加入时继承                                                                                                                                                                                                                                      |
| `inode`             | 绑定的 inode（dentry 持有其引用）                                                                                                                                                                                                                                                                  |
| `state`             | 生命周期状态：`ACTIVE`/`MOVING`/`UNLINKING`/`MOUNTING`/`RESERVED`/`DEAD`（`mrX/include/mrX/fs/mr_fs_dentry.h`）。`MOUNTING` 是子挂载建立期间对挂载点的临时保留；`RESERVED` 是 rename 期间占住目标名字的无 inode 占位项：它阻止重名，但路径解析、`mr_dentry_child_get`（readdir）与缓存复用都跳过它 |
| `retained`          | 是否持有"创建引用"：mkdir/mknod/`O_CREAT` 创建的项为真，lookup 回填的项为假；`del`/`unlink` 只在真时删除对象                                                                                                                                                                                       |

`mr_inode_t`（`mrX/include/mrX/fs/mr_fs_inode.h:92`）——内容节点：`type`
（`mr_inode_type_t`）、`mode`、`size`、`sb`（所属超级块）、`ops`
（`mr_inode_ops_t`：create/mknod/lookup/stat/unlink/rename/truncate/destroy）、
`fops`（`mr_file_ops_t`：open/close/read/write/llseek/ioctl/readdir/fsync）、`priv`。

`mr_file_t`（`mrX/include/mrX/fs/mr_fs_file.h:45`）——打开句柄：`dentry`/`inode`
（打开时固定的引用）、`fops`（来自 inode）、`pos`（64 位位置）、`flags`（打开 标志）、`priv`（后端私有状态）。

`mr_sblock_t`（`mrX/include/mrX/fs/mr_fs_sblock.h:38`）——挂载实例：`mnt`（挂载点 dentry）、`root_dentry`（已挂载根 dentry，根挂载时与
`mnt` 相同）、`root`（根 inode）、`next`（全局超级块链表）、`mount_parent`/`mount_child`/`mount_next`
（挂载拓扑）、`type`（所属文件系统）、`dops`（本挂载的 dentry 名字操作）、
`state`、`move_busy`（rename 串行化标志）、`flags`（挂载标志）、`op_cnt` 与
`open_cnt`（卸载和介质摘除屏障）、`priv`。

`mr_fs_struct_t`（`mrX/include/mrX/fs/mr_fs_struct.h:26`）——文件系统结构：`root` 与 `cwd` 两个 dentry 指针。单用户实例由 VFS
初始化；多用户模式下平台提供 current 选择机制并负责各实例初始化。`mr_dirent_t` 与 `mr_stat_t` 是 API 层的值结构（
`mrX/include/mrX/fs/mr_fs_inode.h:44,53`）。

### 10. 核心流程

**路径解析（两级流水）**：`fs_lookup`（`mrX/fs/file.c:114`）先把路径复制到可写 缓冲区并固定文件系统结构快照：绝对路径从
`fs_struct->root` 开始，否则从
`fs_struct->cwd` 开始，同时记录解析上限 `root`。随后逐组件调用
`mr_dentry_lookup`（`mrX/fs/dentry.c:1202`）。后者先走缓存
`dentry_path_lookup`（`mrX/fs/dentry.c:979`）：`.`/`..` 由路径组件处理，`..`
在 `limit`（文件系统结构根）处停住；命中 `mounted` 则进入挂载链。缓存缺失时
`dentry_fs_lookup`（`mrX/fs/dentry.c`）调用父目录 inode 的 `ops->lookup` 回填。 lookup 填出的 dentry
把创建引用交给这次查找，树不钉死它；句柄、cwd、未走完的 路径或其子节点仍持有引用时留在树上，最后一次 `put`
后从父链表摘掉。mkdir / mknod / `O_CREAT` 仍把创建引用留在树上（ramfs/rootfs 的命名空间）。

**挂载**：`mr_fs_mount_root`/`mr_fs_mount` 汇合到 `fs_mount_dentry`
（`mrX/fs/file.c:228`）：查重（已挂载 `-MR_EBUSY`）、校验挂载点为目录，然后
`mr_sblock_create`（`mrX/fs/sblock.c:113`）分配超级块并调用文件系统的
`mount` 回调填充 `sb->root`（必须是目录 inode）；mount 回调失败由后端自行清理， mount 成功但根 inode 校验失败则调用
`unmount` 收尾。子挂载新建一个与挂载点同名的"已挂载根"dentry 并绑 定根 inode；全局根则原位绑定（`mrX/fs/file.c:288`）。
`mr_sblock_attach`
（`mrX/fs/sblock.c:195`）在文件系统结构锁内把已挂载根接到挂载拓扑 （`mnt->mounted = root`）。

**正常卸载**：`mr_fs_umount` 直接调用 `mr_sblock_detach`，在文件系统结构锁内一次性 检查子挂载、`op_cnt` 与 `open_cnt`
；任一非零均返回 `-MR_EBUSY`，成功则把状态从
`ACTIVE` 改为 `INACTIVE` 并摘除 `mounted` 覆盖与挂载拓扑。随后
`fs_purge_children`（`mrX/fs/file.c:396`）后序删除缓存子树，`mr_sblock_del` 摘除全局链表并释放根 inode
的创建引用。最后一个超级块引用释放时才调用无 I/O、无失败的后端
`unmount` 回调释放内存资源，不存在准备、回滚或可失败的卸载事务。

**介质移除**：`mr_fs_media_detach` 把目标及全部子挂载永久标记为 `OFFLINE`，阻止新操作并让旧句柄的新操作返回 `-MR_ENODEV`
；随后按 leaf-first 顺序尝试回收。OFFLINE 状态下 `open_cnt` 不阻止 detach，但 `op_cnt`、move 或子挂载关系仍可能让回收返回
`-MR_EBUSY`，调用者可重试。摘除阶段不会再执行设备 I/O；super-block 最后引用释放时仍会调用 `unmount`
，因此文件系统实现必须遵守“只做内存清理、设备可能已经离线、不得失败”的契约。

**节点创建**：`fs_create_node_op`（`mrX/fs/file.c:642`）采用"先占位、后提交"：先 在父目录下预留 dentry（`mr_dentry_add`），再调用后端
`create`/`mknod` 生成 inode，校验类型并将超级块引用转移给 inode，最后 `mr_dentry_instantiate` 提交 绑定；任一步失败回滚预留。

**打开与截断**：`fs_file_open` 先固定 dentry/inode 引用并增加 `open_cnt`，再调用 后端 `open`，最后才执行 `MR_O_TRUNC`
——保证打开失败绝不丢数据，同时正常卸载 不会越过已打开句柄。
`MR_O_APPEND` 由可写后端在自身数据锁内把位置重设为 `inode->size`（RamFS 见
`mr_file_apply_append`）。失败或零长写恢复原位置；VFS 不再预改 `pos`。

### 11. 并发与中断安全

- 全部文件系统结构数据（dentry 树、超级块链表与拓扑、fs 链表、root/cwd）由同一把 关中断自旋锁保护；API 经 `mr_fs_struct_lock`/
  `mr_fs_struct_unlock` 进入临界区， 临界区内只做指针与链表操作。
- 文件系统回调（mount/unmount/create/lookup/read 等）一律在锁外调用；调用前通过
  `fs_op_enter_by_dentry`/`fs_op_enter_by_file`（`mrX/fs/file.c`）固定超级块并递增
  `op_cnt`，卸载因此无法与回调并发进行。
- 句柄打开期间持有 dentry/inode 引用；正常 umount 由 `open_cnt` 拒绝，介质摘除 后对象仍由引用保活，直到 close 释放；
  `mr_fs_readdir` 对"挂载点被复用"的句柄 做绑定校验。
- VFS 不对同一 inode 的数据操作（read/write/truncate 等）做互斥。`MR_O_APPEND`
  是 write 回调的契约：后端必须在自己的数据序列化里选 EOF（RamFS 在节点锁内调用
  `mr_file_apply_append`）。冲突返回 `-MR_EBUSY`，见 [RamFS](ramfs.md) 第 11 节。
- rename 在锁内固定源父目录快照（`dentry_move_t.old_upper`），后端 `rename` 回调与 提交都以该快照为准，不会在锁外重读
  `dentry->upper`；`fs_rename_dentry` 同样在锁内 固定源父目录后再做目录类型与 `-MR_EXDEV` 校验。
- `fs_purge_children` 遇到无法删除的子项（处于 `MOVING`/`UNLINKING`）会立即停止 遍历而不是反复重试同一子项，剩余对象由引用计数保活直到释放。
- API 设计为线程上下文调用；文件系统结构锁临界区内禁止阻塞或调用文件系统实现回调 （
  `mrX/include/mrX/fs/mr_fs_struct.h:59-66` 的约定）。

### 12. 扩展与派生

实现一个新文件系统只需三步（参考 [RamFS](ramfs.md) 源码）：

1. 用 `MR_FILE_SYSTEM_INIT` 定义描述符：名字、`mount`（必选，填充 `sb->root`
   目录 inode 与可选 `sb->dops`）、`unmount`（可选，在最后一个引用释放后清理 后端内存；不得访问设备、不得失败）。持久化由文件
   close/fsync 完成。
2. 实现 `mr_inode_ops_t` 中需要的回调（至少 `create` 或 `mknod` 之一供节点创建； 预缓存型文件系统可不实现 `lookup`）与
   `mr_file_ops_t` 中的读写回调。
3. 在初始化阶段调用 `mr_file_system_register` 注册（通常通过
   `MR_INIT_EXPORT` 挂到 `MR_INIT_LEVEL_FS`），随后按名字挂载。

dentry 的 `ops->hash`/`ops->compare` 可用于大小写不敏感等名字策略；子 dentry 会继承父的名字操作（`mrX/fs/dentry.c:381-392`
）。名字是否重复以该比较策略为准： 例如大小写不敏感时，`CaseName` 与 `casename` 是同一名字，第二次创建返回
`-MR_EEXIST`。inode 的 `ops->destroy` 是后端释放私有资源的收尾钩子 （`mrX/fs/inode.c:84`）。

#### 12.1 后端开发者 API 导览

下列接口公开在 `mrX/include/mrX/fs/` 中，主要面向文件系统实现者，不是普通文件用户的首选入口：

| 层次            | 主要 API                                                         | 用途                                                      |
|-----------------|------------------------------------------------------------------|-----------------------------------------------------------|
| 文件系统注册    | `mr_file_system_register`、`mr_file_system_find`                 | 注册和查找 `mr_file_system_t`                             |
| 文件系统上下文  | `mr_fs_struct_root/current/init/lock/unlock`                     | 管理 root/cwd 与结构锁；多用户模式下 `current` 由平台提供 |
| 超级块          | `mr_sblock_create/del/attach/detach`、`mr_sblock_get/put`        | 创建、挂载、卸载和管理文件系统实例                        |
| 超级块并发      | `mr_sblock_op_enter/exit`、`mr_sblock_file_acquire/release`      | 阻止操作或已打开文件跨过卸载边界                          |
| dentry 生命周期 | `mr_dentry_init/create/del/get/put`                              | 初始化、分配和引用计数                                    |
| dentry 树       | `mr_dentry_add/remove/move/rename/unlink`、`mr_dentry_child_get` | 维护命名空间树与目录枚举                                  |
| dentry 解析     | `mr_dentry_lookup`、`mr_dentry_instantiate/uninstantiate`        | 路径查找以及 dentry/inode 绑定                            |
| dentry 查询     | `mr_dentry_is_empty`、`mr_dentry_is_root`                        | 检查目录子项及是否为当前全局根                            |
| inode           | `mr_inode_init/create/get/put`                                   | 初始化、分配和引用 inode                                  |

`mr_dentry_is_root(dentry)` 与 `mr_fs_struct_root()` 的当前返回值比较，判断的是全局根，不是某个子文件系统的挂载根。所有结构级
API 都需遵守本文第 11 节的锁顺序、引用计数和卸载边界；不应绕过 `mr_fs_*` 用户 API 直接组装业务文件操作。

### 13. 源码导读

| 位置                                                                                                           | 职责                                                                             |
|----------------------------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------|
| `mrX/include/mrX/fs/mr_fs_file.h`                                                                              | 用户 API 全集、打开标志、`mr_file_t`、挂载 API 声明                              |
| `mrX/include/mrX/fs/mr_fs_system.h` / `mr_fs_struct.h` / `mr_fs_sblock.h` / `mr_fs_dentry.h` / `mr_fs_inode.h` | 各层数据结构与内部接口                                                           |
| `mrX/fs/system.c:18`                                                                                           | 文件系统注册与按名查找                                                           |
| `mrX/fs/struct.c:15`                                                                                           | 全局根 dentry、文件系统结构锁、root/cwd 获取                                     |
| `mrX/fs/dentry.c:979` / `:1054` / `:1202`                                                                      | 缓存步进解析 / 缺失回填 / 解析入口                                               |
| `mrX/fs/dentry.c:296` / `:404` / `:720` / `:752` / `:805`                                                      | dentry 删除/加入/移动/重命名/摘除                                                |
| `mrX/fs/sblock.c:46` / `:195` / `:305` / `:560`                                                                | 超级块初始化/挂载/卸载/最后引用释放（unmount 钩子）                              |
| `mrX/fs/inode.c:39` / `:52` / `:76`                                                                            | inode 静态初始化/动态创建/释放（destroy 钩子）                                   |
| `mrX/fs/file.c:114` / `:228` / `:642` / `:901` / `:983`                                                        | 路径解析与挂载/节点创建/打开/打开校验                                            |
| `mrX/fs/file.c:1897`                                                                                           | `mr_fs_file_init`：单用户文件系统结构初始化入口                                  |
| `mrX/kernel/initcall.c:31`                                                                                     | 内核初始化序列中的 FS 初始化调用点                                               |
| `mrX/test/fs/`                                                                                                 | 按主题划分的行为级测试（file/file_ops/dentry/mount/concurrency/ramfs/rootfs 等） |

### 14. 附录

**配置宏**

| 宏                          | 说明                                                                |
|-----------------------------|---------------------------------------------------------------------|
| `MR_USE_FS`                 | 模块开关，默认 n；选中 `MR_USE_REF_LONG_COUNT`                      |
| `MR_USE_FS_MULTIUSER`       | 多用户文件系统结构开关，默认 n；平台需实现 `mr_fs_struct_current()` |
| `MR_CFG_FS_PATH_LEN`        | 路径上限与 `mr_dirent_t.name` 尺寸，默认 260                        |
| `MR_CFG_FS_INLINE_NAME_LEN` | dentry 内联短名缓冲，默认 8                                         |
| `MR_O_*` / `MR_SEEK_*`      | 打开标志与定位基准（`mr_fs_file.h:29-42`）                          |

**错误码**（`mrX/include/mrX/libc/mr_errno.h`）：本模块失败返回负数错误码， 常用取值：`-MR_EPERM`(1)、`-MR_ENOENT`(2)、
`-MR_EIO`(5)、`-MR_EBADF`(9)、
`-MR_EAGAIN`(11)、`-MR_ENOMEM`(12)、`-MR_EBUSY`(16)、`-MR_EEXIST`(17)、
`-MR_EXDEV`(18)、`-MR_ENOTDIR`(20)、`-MR_EISDIR`(21)、`-MR_EINVAL`(22)、
`-MR_ESPIPE`(29)、`-MR_ERANGE`(34)、`-MR_ENAMETOOLONG`(36)、`-MR_ENOTEMPTY`(39)、
`-MR_ENOSYS`(40)、`-MR_ELOOP`(92)。参数为空指针等非法输入在断言开启时由
`MR_ASSERT` 拦截。

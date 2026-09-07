# 根文件系统（RootFS）

> RootFS 是一个只维护命名空间的根文件系统：它提供目录与带类型的节点（不存储文件
> 数据），在内核启动时自动注册并挂载为全局根 `/`，为系统建立目录骨架和挂载点，
> 供 RamFS 等数据文件系统在其路径下挂载。

| 项       | 说明                                                                                                                                      |
|----------|-------------------------------------------------------------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_FS_ROOTFS`（依赖 `MR_USE_FS`，默认关闭）                                                                                          |
| 头文件   | `<mrX/fs/mr_fs_file.h>`（经 VFS 使用，模块无独立公共头）                                                                                  |
| 依赖模块 | [文件系统 VFS](fs.md)、[对象 Object](../kernel/object.md)、[动态内存 malloc](../libc/malloc.md)、[初始化 Initcall](../kernel/initcall.md) |
| 参考测试 | `mrX/test/fs/rootfs.c`                                                                                                                    |

---

## 用户指南

### 1. 模块概述

RootFS 解决的问题是： **在没有任何存储后端的情况下，让系统启动后立即拥有一棵 可用的路径树**。

特性：

- 启动即就绪：初始化级别 `MR_INIT_LEVEL_FS` 时自动注册 `"rootfs"` 并将其挂载为 全局根 `/`，应用代码无需执行任何挂载步骤即可使用路径
  API。
- 只管结构，不管数据：支持 `mkdir` 创建目录、`mknod` 创建任意类型的节点 （目录、普通文件、字符/块设备、FIFO），节点可携带一个私有指针。
- 目录枚举可用（经 VFS 默认实现），枚举结果带节点类型。
- 重命名、删除与目录管理由 VFS 提供，不需要 RootFS 提供额外回调。
- 读、写、截断一律不支持（`-MR_ENOSYS`）：它不是数据容器。

典型分工：RootFS 搭建 `/`、`/system`、`/tmp` 等目录骨架并在相应目录挂载
[RamFS](ramfs.md) 等数据文件系统；自身只承载"结构性"节点（如设备占位节点）。

### 2. 配置与依赖

| 配置项             | 类型 | 默认值 | 说明                                                                   |
|--------------------|------|--------|------------------------------------------------------------------------|
| `MR_USE_FS_ROOTFS` | bool | n      | 启用 RootFS（依赖 `MR_USE_FS`，`mrX/include/mrX/fs/rootfs/Kconfig:6`） |

RootFS 没有专属的 `MR_CFG_*` 参数。启用后包含 VFS 头文件即可使用：

```c
#include <mrX/fs/mr_fs_file.h>
```

前提：`MR_USE_FS` 已启用。两者都启用时，内核初始化完成前全局根 `/` 已经是 RootFS 的挂载实例。

### 3. 快速上手

```c
#include <mrX/fs/mr_fs_file.h>

static int rootfs_demo(void) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_stat_t stat;

    /* '/' is already a mounted rootfs here; build a skeleton */
    mr_fs_mkdir("/system");
    mr_fs_mknod("/system/console", MR_INODE_CHR, MR_NULL);

    /* Nodes can be opened for status, but hold no data */
    mr_fs_open(&file, "/system/console", MR_O_RDONLY);
    mr_fs_fstat(&file, &stat);          /* stat.type == MR_INODE_CHR */
    mr_fs_close(&file);

    /* Mount a data file system onto the skeleton */
    mr_fs_mkdir("/system/data");
    return mr_fs_mount(MR_NULL, "/system/data", "ramfs", 0, MR_NULL);
}
```

（`ramfs` 需要启用 `MR_USE_FS_RAMFS`。）

### 4. 使用说明

**根的建立**

- 启用 `MR_USE_FS_ROOTFS` 后，全局根 `/` 在内核初始化阶段自动成为 RootFS 挂载实例，无需（也不应重复）调用 `mr_fs_mount_root`
  ；重复挂载返回
  `-MR_EBUSY`。
- 需要替换根文件系统时，先关闭 RootFS 上的全部打开句柄并卸载所有子挂载，再用
  `mr_fs_umount("/")` 摘除 RootFS，最后调用 `mr_fs_mount_root` 挂载其他文件系统。 RootFS 没有后端 `unmount` 回调，但仍遵循
  VFS 的正常卸载检查。

**节点创建**

- `mr_fs_mkdir(path)`：创建目录，是 RootFS 下唯一的"通用创建"途径。
- `mr_fs_mknod(path, type, priv)`：创建 `type` 指定类型的节点；`priv` 由 RootFS 借用并保存在节点上，不拷贝、不释放，所有者须保证节点存活期间指针
  有效。
- `mr_fs_open(..., MR_O_CREAT)` 在 RootFS 上不可用：普通文件创建被拒绝 （`-MR_ENOSYS`），RootFS 没有"数据文件"的概念。

**节点访问**

- 非目录节点可以以任意访问模式打开（目录仍只能只读打开），用于
  `mr_fs_fstat` 读取类型/模式，以及取回创建时保存的 `priv`（打开后经句柄的 inode 私有数据访问，见第 6 节示例）。
- `mr_fs_read`/`mr_fs_write`/`mr_fs_ftruncate` 一律返回 `-MR_ENOSYS`；
  `MR_O_TRUNC` 打开同样被拒绝。
- 目录用 `MR_O_RDONLY | MR_O_DIRECTORY` 打开后以 `mr_fs_readdir` 枚举，
  `mr_dirent_t.type` 区分 `MR_INODE_DIR`/`MR_INODE_REG`/`MR_INODE_CHR` 等。
- `mr_fs_rename`/`mr_fs_unlink`/`mr_fs_rmdir`
  全部可用（由 VFS 实现），可在不破坏结构的前提下调整命名空间。

### 5. API 参考

RootFS 不定义自己的公开 API，全部操作通过 [文件系统 VFS](fs.md) 的通用接口 完成。下表是各 VFS 操作作用于 RootFS 时的行为：

| API                                             | 作用于 RootFS 时的行为                                                                              |
|-------------------------------------------------|-----------------------------------------------------------------------------------------------------|
| `mr_fs_mkdir`                                   | 创建目录节点（`MR_INODE_DIR`）                                                                      |
| `mr_fs_mknod`                                   | 创建任意类型节点，`priv` 借用保存在节点上                                                           |
| `mr_fs_open` + `MR_O_CREAT`                     | 拒绝，返回 `-MR_ENOSYS`（无普通文件创建）                                                           |
| `mr_fs_read` / `mr_fs_write`                    | 拒绝，返回 `-MR_ENOSYS`（节点无数据）                                                               |
| `mr_fs_ftruncate` / `MR_O_TRUNC` 打开           | 拒绝，返回 `-MR_ENOSYS`（无截断回调）                                                               |
| `mr_fs_fstat`                                   | 返回通用字段：`type`/`mode`/`size`（`size` 恒为 0）                                                 |
| `mr_fs_readdir`                                 | VFS 默认枚举，返回兄弟节点名与类型                                                                  |
| `mr_fs_rename` / `mr_fs_unlink` / `mr_fs_rmdir` | 正常可用（VFS 通用路径）                                                                            |
| `mr_fs_mount`（挂载 "rootfs"）                  | 在任意目录挂载点上建立新的 RootFS 实例（支持嵌套）                                                  |
| `mr_fs_umount`                                  | 无 unmount 回调；有打开句柄、在途操作或子挂载时返回 `-MR_EBUSY`，条件满足后释放实例的节点与目录结构 |

**`mr_fs_mknod`（RootFS 上的节点创建）**

```c
mr_err_t mr_fs_mknod(const char *path, mr_inode_type_t type, void *priv);
```

- `type` 可为 `MR_INODE_REG`/`MR_INODE_DIR`/`MR_INODE_CHR`/`MR_INODE_BLK`/
  `MR_INODE_FIFO` 任意一种（目录节点也可经 mknod 创建）。
- `priv` 为指向调用者数据的指针，节点存活期间 RootFS 原样保存；节点删除时 RootFS 不会释放它，所有权归调用者。
- 返回：成功 `0`，失败负数错误码（`-MR_ENOENT` 父目录不存在、`-MR_EEXIST`
  同名节点已存在、`-MR_ENOMEM` 内存不足）。

### 6. 完整示例

构建结构树、枚举并取回节点私有数据（取材自 `mrX/test/fs/rootfs.c`）：

```c
static void rootfs_skeleton_demo(void) {
    mr_file_t dir = MR_FILE_INIT(dir);
    mr_file_t file = MR_FILE_INIT(file);
    mr_dirent_t dirent;
    int metadata = 0;
    void *node_priv;

    /* Build a typed namespace tree */
    mr_fs_mkdir("/system");
    mr_fs_mkdir("/system/services");
    mr_fs_mknod("/system/config", MR_INODE_REG, &metadata);
    mr_fs_mknod("/system/console", MR_INODE_CHR, MR_NULL);

    /* Enumerate the directory with node types */
    if (mr_fs_open(&dir, "/system", MR_O_RDONLY | MR_O_DIRECTORY) == 0) {
        while (mr_fs_readdir(&dir, &dirent) > 0) {
            /* dirent.name / dirent.type describe each child */
        }
        mr_fs_close(&dir);
    }

    /* Open a node and fetch the private pointer given at mknod time */
    if (mr_fs_open(&file, "/system/config", MR_O_RDWR) == 0) {
        node_priv = file.inode->priv;       /* == &metadata */
        (void)node_priv;
        mr_fs_close(&file);                 /* read/write are -MR_ENOSYS */
    }
    mr_fs_rmdir("/system/services");
    mr_fs_unlink("/system/config");
    mr_fs_unlink("/system/console");
    mr_fs_rmdir("/system");
}
```

### 7. 常见问题（FAQ）

**Q：打开 RootFS 节点后 `mr_fs_read` 返回 `-MR_ENOSYS`？**

这是设计行为。RootFS 节点只有名字与类型，没有数据后端；需要读写内容时在 RootFS 的目录上挂载 [RamFS](ramfs.md)
等数据文件系统，再访问挂载后的路径。

**Q：`mr_fs_open("/file", MR_O_RDWR | MR_O_CREAT)` 失败？**

RootFS 不支持创建数据文件（`-MR_ENOSYS`）。用 `mr_fs_mkdir` 建目录、
`mr_fs_mknod` 建带类型的节点。

**Q：启动后 `/` 是什么文件系统？**

启用 `MR_USE_FS_ROOTFS` 时是 RootFS：初始化阶段自动完成注册与根挂载。
`mr_fs_mount_root` 重复挂载会返回 `-MR_EBUSY`。

**Q：怎么把 `/` 换成别的文件系统？**

先关闭 RootFS 上的全部打开句柄并卸载所有子挂载，再调用 `mr_fs_umount("/")`； 成功后用 `mr_fs_mount_root` 挂载目标文件系统。否则正常卸载返回
`-MR_EBUSY`。

**Q：`mknod` 传入的 `priv` 会被释放吗？**

不会。RootFS 只借用该指针（保存在节点上供打开后取回），节点删除时不做任何 释放动作；指针指向的资源的生命周期由创建者管理。

**Q：`mknod` 能创建目录吗？**

能。`mr_fs_mknod(path, MR_INODE_DIR, MR_NULL)` 等价于用 mknod 通道创建目录； 普通 `mr_fs_mkdir` 走的是 create 通道，两者都可用。

---

## 开发者指南

### 8. 架构总览

RootFS 是 VFS 之上最小化的"命名空间后端"，与数据完全解耦：

```
+---------------------------+
| VFS（路径/挂载/句柄）       |  mrX/fs/*.c
+-------------+-------------+
              | mr_inode_ops_t 回调（仅 create / mknod）
+-------------v-------------+
| RootFS（rootfs.c）         |
|  目录骨架 + 带类型节点      |
|  priv 指针原样借用          |
+---------------------------+
（无 fops：read/write/ioctl 在 VFS 层即返回 -MR_ENOSYS）
```

**根的建立方式**（从源码确认的完整链路）：

1. FS 核心编译期即定义了静态全局根 dentry `"/"`（`mrX/fs/struct.c:15`，
   `MR_DENTRY_ROOT_INIT`），此时它没有绑定任何 inode。
2. `mr_fs_rootfs_init`（`mrX/fs/rootfs/rootfs.c:87-96`）通过
   `MR_INIT_EXPORT(..., MR_INIT_LEVEL_FS)` 在 FS 初始化阶段执行：先注册
   `__rootfs` 描述符，再调用 `mr_fs_mount_root(MR_NULL, "rootfs", 0, MR_NULL)`， 并用 `MR_ASSERT` 检查结果（`rootfs.c:95`）。
3. `mr_fs_mount_root`（`mrX/fs/file.c:341`）取全局根 dentry 作为挂载点， 走 `fs_mount_dentry` 的根挂载分支：`rootfs_mount`
   回调（`rootfs.c:69`） 创建根目录 inode 填入 `sb->root`，随后 `mr_dentry_instantiate` 将该 inode **原位绑定**到全局根
   dentry（根挂载不新建"已挂载根"，`mrX/fs/file.c:288`）。
4. 自此 `/` 的所有路径操作都落在 RootFS 实例上；子目录上可再挂载其他文件系统 形成覆盖。

### 9. 数据结构

RootFS 没有私有节点结构，全部状态由 VFS 对象承载：

| 对象                        | RootFS 的用法                                                                                                    |
|-----------------------------|------------------------------------------------------------------------------------------------------------------|
| `mr_inode_t`                | `type` 保存 mknod 给定类型；`priv` 原样保存借用指针；`ops = &__rootfs_inodes`（`rootfs.c:21`）；`fops = MR_NULL` |
| `mr_dentry_t`               | 节点名字与父子关系，完全由 VFS 维护                                                                              |
| `mr_sblock_t`               | 每次挂载一个实例，`dops = MR_NULL`（默认名字操作），`priv` 未使用                                                |
| `mr_file_system_t __rootfs` | 静态描述符：名字 `"rootfs"`，仅 `mount` 回调（`rootfs.c:29-30`）                                                 |

inode 操作表只有两个回调（`rootfs.c:21-24`）：

- `create`（`rootfs.c:46`）：仅接受 `MR_INODE_DIR`，其他类型返回 `-MR_ENOSYS`
  ——这使 `mr_fs_open(MR_O_CREAT)` 与 `mr_fs_mkdir` 的行为被明确区分。
- `mknod`（`rootfs.c:59`）：接受任意类型并把 `priv` 透传给 inode。

### 10. 核心流程

**挂载**：`rootfs_mount`（`mrX/fs/rootfs/rootfs.c:69-85`）忽略
`source`/`flags`/`data`，调用 `mr_inode_create(MR_INODE_DIR, &__rootfs_inodes,
MR_NULL, MR_NULL)` 创建根目录 inode（失败返回 `-MR_ENOMEM`），置
`sb->dops = MR_NULL`、`sb->root = root`。根挂载与嵌套挂载共用该回调。

**节点创建**：VFS 的 `fs_create_node_op`（`mrX/fs/file.c:642`）先在 dentry 树中 占位，再调用
`create` 或 `mknod` 回调；RootFS 的实现不检查 `dir`/`name`（归属由 dentry 表达），只按通道限制类型并把 `priv` 存入
`inode->priv`。由于节点创建时已 完整进入 dentry 缓存，后续路径解析全部命中缓存。

**无 lookup 回调**：`__rootfs_inodes` 不提供 `lookup`，VFS 对缓存缺失直接返回
`-MR_ENOENT`（"预缓存型文件系统"，`mrX/fs/dentry.c:1082-1086`）——RootFS 节点 只会经 VFS API 创建，因此缓存即全集，这是它不需要后端查找的原因。

**读写拒绝**：inode 无 `fops`，`mr_fs_read`/`mr_fs_write`/`mr_fs_ioctl` 在 VFS 层返回 `-MR_ENOSYS`；inode 无
`ops->truncate`，`mr_fs_ftruncate` 与
`MR_O_TRUNC` 打开同样返回 `-MR_ENOSYS`。`mr_fs_readdir` 走 VFS 默认的 dentry 子节点枚举，无需 RootFS 参与。

**卸载**：无 `unmount` 回调，但仍需通过 VFS 对子挂载、`op_cnt` 与 `open_cnt`
的检查；任一非零时 `mr_fs_umount` 返回 `-MR_EBUSY`。成功摘除后，实例的目录 结构与节点随 VFS 引用释放链回收。

### 11. 并发与中断安全

RootFS 自身无任何可变状态（没有全局变量、没有锁），并发语义完全继承 VFS： 命名空间互斥由文件系统结构锁保证，文件系统回调在超级块
`op_cnt` 屏障外执行，详见
[文件系统 VFS](fs.md) 开发者篇第 11 节。

需要注意的只有 `priv` 的所有权：多个调用者可同时打开同一节点并读到同一个
`priv` 指针，指针指向对象的并发访问由其所有者自行保护。

### 12. 扩展与派生

RootFS 的 `mknod` + `priv` 借用机制是把 **任意对象接入路径命名空间**的通用 手段：以
`mr_fs_mknod("/dev/xxx", MR_INODE_CHR, &driver_obj)` 把驱动实例、 控制块等挂到固定路径上，服务代码按路径 `open` 后从句柄
inode 取回对象指针。 需要真正数据读写时，不要扩展 RootFS，而是在其目录上挂载实现 `fops` 的
文件系统（模板见 [RamFS](ramfs.md) 开发者篇第 12 节）。

### 13. 源码导读

| 位置                                        | 职责                                                                 |
|---------------------------------------------|----------------------------------------------------------------------|
| `mrX/fs/rootfs/rootfs.c:13`                 | 文件系统名 `"rootfs"`                                                |
| `mrX/fs/rootfs/rootfs.c:21`                 | inode 操作表（create/mknod，无 lookup/truncate）                     |
| `mrX/fs/rootfs/rootfs.c:29`                 | `mr_file_system_t` 描述符（仅 mount 回调）                           |
| `mrX/fs/rootfs/rootfs.c:32` / `:46` / `:59` | 节点分配 / create 通道（仅目录）/ mknod 通道（任意类型 + priv 借用） |
| `mrX/fs/rootfs/rootfs.c:69`                 | 挂载回调：创建根目录 inode                                           |
| `mrX/fs/rootfs/rootfs.c:87-96`              | 自动注册 + 挂载为全局根（`MR_INIT_LEVEL_FS`）                        |
| `mrX/fs/struct.c:15`                        | 被原位占用的静态全局根 dentry `"/"`                                  |
| `mrX/fs/file.c:288`                         | 根挂载"原位绑定"的分支实现                                           |
| `mrX/fs/dentry.c:1082-1086`                 | 预缓存型文件系统（无 lookup）的约定                                  |
| `mrX/test/fs/rootfs.c`                      | 结构树构建/枚举/嵌套挂载/卸载的行为级测试                            |

### 14. 附录

**配置宏**

| 宏                 | 说明                                                           |
|--------------------|----------------------------------------------------------------|
| `MR_USE_FS_ROOTFS` | 模块开关，依赖 `MR_USE_FS`，默认 n；启用后启动时自动挂载为 `/` |

**错误码**（经 VFS API 返回，均为负数）：`-MR_ENOSYS`（数据读写、截断、 普通文件创建在 RootFS 上不受支持）、`-MR_ENOMEM`（节点/根
inode 分配失败）。 路径类错误（`-MR_ENOENT`/`-MR_EEXIST`/`-MR_ENOTEMPTY` 等）由 VFS 统一产生， 见 [文件系统 VFS](fs.md) 附录。

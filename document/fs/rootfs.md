# 根文件系统（RootFS）

> RootFS 是一个只维护命名空间的文件系统：它提供目录与带类型的节点（不存储文件数据）。
> 启用后会通过 `MR_INIT_LEVEL_FS` initcall 注册，并在全局根尚未被占用时尝试挂载到 `/`，
> 为系统建立目录骨架和挂载点，供 RamFS 等数据文件系统在其路径下挂载。

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

- 自动注册/根挂载：`MR_INIT_LEVEL_FS` initcall 注册 `"rootfs"`，并调用 `mr_fs_mount_root` 尝试占用全局根。该 initcall 由 `mr_user_init()` 执行，不属于 `mr_kernel_init()` 的硬编码阶段；若根已被其他文件系统占用，`-MR_EBUSY` 会被接受，RootFS 不会替换现有根。
- 只管结构，不管数据：支持 `mkdir` 创建目录、`mknod` 创建任意类型的节点 （目录、普通文件、字符/块设备、FIFO），节点可携带一个私有指针。
- 目录枚举可用（经 VFS 默认实现），枚举结果带节点类型。
- 重命名、删除与目录管理由 VFS 提供，不需要 RootFS 提供额外回调。
- RootFS 不提供文件数据回调：对允许进入后端的非目录节点读写通常得到 `-MR_ENOSYS`；目录、访问模式和非 REG truncate 等情况仍会先由 VFS 返回更具体的类型/权限错误。

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

前提：`MR_USE_FS` 已启用，并且启动流程执行了 `mr_user_init()`。RootFS initcall 在 FS 级别运行；若当时全局根仍为空，它会成为 `/` 的挂载实例。若根已被占用，初始化代码容忍 `-MR_EBUSY`，不会覆盖现有文件系统。

### 3. 快速上手

```c
#include <mrX/fs/mr_fs_file.h>

static int rootfs_demo(void) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_stat_t stat;

    /* After mr_user_init(), '/' is rootfs if its auto-mount won the global root */
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

- 启用 `MR_USE_FS_ROOTFS` 后，`mr_user_init()` 执行到 `MR_INIT_LEVEL_FS` 时会注册 RootFS 并尝试挂载全局根。根为空时成功；根已被占用时返回 `-MR_EBUSY`，RootFS 初始化代码把它视为可接受状态。已挂载 RootFS 时再对同一根重复挂载同样得到 `-MR_EBUSY`。
- 需要替换根文件系统时，先关闭 RootFS 上的全部打开句柄并卸载所有子挂载，再用
  `mr_fs_umount("/")` 摘除 RootFS，最后调用 `mr_fs_mount_root` 挂载其他文件系统。 RootFS 没有后端 `unmount` 回调，但仍遵循
  VFS 的正常卸载检查。

**节点创建**

- `mr_fs_mkdir(path)`：创建目录，是 RootFS 下唯一的"通用创建"途径。
- `mr_fs_mknod(path, type, priv)`：创建 `type` 指定类型的节点；`priv` 由 RootFS 借用并保存在节点上，不拷贝、不释放，所有者须保证节点存活期间指针
  有效。
- `MR_O_CREAT` 在**目标不存在且需要创建普通文件**时会调用 RootFS `create(..., MR_INODE_REG, ...)`，因而返回 `-MR_ENOSYS`。如果目标节点本来就存在，`MR_O_CREAT` 本身不会强制失败（`MR_O_EXCL` 另有语义）。

**节点访问**

- 非目录节点可以以任意访问模式打开（目录仍只能只读打开），用于
  `mr_fs_fstat` 读取类型/模式，以及取回创建时保存的 `priv`（打开后经句柄的 inode 私有数据访问，见第 6 节示例）。
- RootFS 没有 `fops` 和 truncate 回调。对非目录、访问模式允许的节点执行 read/write 会到达 `-MR_ENOSYS`；目录会先返回 `-MR_EISDIR`，访问模式不符会先返回 `-MR_EBADF`。`mr_fs_ftruncate` 只有 REG 节点会因缺少 truncate 返回 `-MR_ENOSYS`，目录返回 `-MR_EISDIR`，其他非 REG 类型返回 `-MR_EINVAL`。`MR_O_TRUNC` 只对可写 REG 节点要求 truncate，因此 RootFS 的 REG 节点会被 `-MR_ENOSYS` 拒绝。
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
| `mr_fs_open` + `MR_O_CREAT`                     | 缺失目标需创建 REG 时返回 `-MR_ENOSYS`；已有目标仍按普通 open 规则处理                              |
| `mr_fs_read` / `mr_fs_write`                    | 允许进入 fops 路径的非目录节点返回 `-MR_ENOSYS`；目录/访问模式错误由 VFS 更早返回                    |
| `mr_fs_ftruncate` / `MR_O_TRUNC` 打开           | REG 因无 truncate 返回 `-MR_ENOSYS`；目录/其他 inode 类型由 VFS 返回 `-MR_EISDIR`/`-MR_EINVAL`       |
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

当 `/file` 不存在时，`MR_O_CREAT` 试图通过 generic `create` 创建 `MR_INODE_REG`，而 RootFS 的 `create` 只接受目录，因此返回 `-MR_ENOSYS`。用 `mr_fs_mkdir` 建目录，或用 `mr_fs_mknod` 显式创建带类型的结构节点。

**Q：启动后 `/` 是什么文件系统？**

通常在执行完 `mr_user_init()` 后是 RootFS：其 FS 级 initcall 会注册并尝试根挂载。但如果根在此之前已经被占用，RootFS initcall 会接受 `-MR_EBUSY`，此时 `/` 保持原文件系统。

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
2. `mr_fs_rootfs_init`（`mrX/fs/rootfs/rootfs.c:84-94`）通过 `MR_INIT_EXPORT(..., MR_INIT_LEVEL_FS)` 注册为 initcall，由 `mr_user_init()` 遍历执行：先注册 `__rootfs` 描述符，再调用 `mr_fs_mount_root`。断言接受成功 `0` 或根已占用的 `-MR_EBUSY`，因此该步骤是“尝试成为全局根”，不是无条件替换。
3. `mr_fs_mount_root`（`mrX/fs/file.c:341`）取全局根 dentry 作为挂载点， 走 `fs_mount_dentry` 的根挂载分支：`rootfs_mount`
   回调（`rootfs.c:66`） 创建根目录 inode 填入 `sb->root`，随后 `mr_dentry_instantiate` 将该 inode **原位绑定**到全局根
   dentry（根挂载不新建"已挂载根"，`mrX/fs/file.c:288`）。
4. 自此 `/` 的所有路径操作都落在 RootFS 实例上；子目录上可再挂载其他文件系统 形成覆盖。

### 9. 数据结构

RootFS 没有私有节点结构，全部状态由 VFS 对象承载：

| 对象                        | RootFS 的用法                                                                                                    |
|-----------------------------|------------------------------------------------------------------------------------------------------------------|
| `mr_inode_t`                | `type` 保存 mknod 给定类型；`priv` 原样保存借用指针；`ops = &__rootfs_inodes`（`rootfs.c:18`）；`fops = MR_NULL` |
| `mr_dentry_t`               | 节点名字与父子关系，完全由 VFS 维护                                                                              |
| `mr_sblock_t`               | 每次挂载一个实例，`dops = MR_NULL`（默认名字操作），`priv` 未使用                                                |
| `mr_file_system_t __rootfs` | 静态描述符：名字 `"rootfs"`，仅 `mount` 回调（`rootfs.c:26-27`）                                                 |

inode 操作表只有两个回调（`rootfs.c:18-21`）：

- `create`（`rootfs.c:43`）：仅接受 `MR_INODE_DIR`，其他类型返回 `-MR_ENOSYS`
  ——这使 `mr_fs_open(MR_O_CREAT)` 与 `mr_fs_mkdir` 的行为被明确区分。
- `mknod`（`rootfs.c:56`）：接受任意类型并把 `priv` 透传给 inode。

### 10. 核心流程

**挂载**：`rootfs_mount`（`mrX/fs/rootfs/rootfs.c:66-82`）忽略
`source`/`flags`/`data`，调用 `mr_inode_create(MR_INODE_DIR, &__rootfs_inodes,
MR_NULL, MR_NULL)` 创建根目录 inode（失败返回 `-MR_ENOMEM`），置
`sb->dops = MR_NULL`、`sb->root = root`。根挂载与嵌套挂载共用该回调。

**节点创建**：VFS 的 `fs_create_node_op`（`mrX/fs/file.c:642`）先在 dentry 树中 占位，再调用
`create` 或 `mknod` 回调；RootFS 的实现不检查 `dir`/`name`（归属由 dentry 表达），只按通道限制类型并把 `priv` 存入
`inode->priv`。由于节点创建时已 完整进入 dentry 缓存，后续路径解析全部命中缓存。

**无 lookup 回调**：`__rootfs_inodes` 不提供 `lookup`，VFS 对缓存缺失直接返回
`-MR_ENOENT`（"预缓存型文件系统"，`mrX/fs/dentry.c:1082-1086`）——RootFS 节点 只会经 VFS API 创建，因此缓存即全集，这是它不需要后端查找的原因。

**数据操作**：inode 无 `fops`，因此在通过 VFS 的类型/权限前置检查后，read/write/ioctl 没有对应后端能力时返回 `-MR_ENOSYS`。truncate 也只在 REG 节点进入缺失回调的 `-MR_ENOSYS` 路径；目录和其他类型会由 VFS 更早拒绝。`mr_fs_readdir` 则走 VFS 默认 dentry 子节点枚举，无需 RootFS 回调。

**卸载**：无 `unmount` 回调，但仍需通过 VFS 对子挂载、`op_cnt` 与 `open_cnt`
的检查；任一非零时 `mr_fs_umount` 返回 `-MR_EBUSY`。成功摘除后，实例的目录 结构与节点随 VFS 引用释放链回收。

### 11. 并发与中断安全

RootFS 没有每个节点额外维护的数据结构或私有锁；静态 `mr_file_system_t __rootfs` 描述符在注册时会被系统链表接入。实际命名空间、inode/dentry/sblock 生命周期与并发控制都由 VFS 提供；RootFS 的 create/mknod/mount 回调主要负责分配 inode。详见 [文件系统 VFS](fs.md) 开发者篇第 11 节。

需要注意的只有 `priv` 的所有权：多个调用者可同时打开同一节点并读到同一个
`priv` 指针，指针指向对象的并发访问由其所有者自行保护。

### 12. 扩展与派生

RootFS 的 `mknod` + `priv` 借用机制是把 **任意对象接入路径命名空间** 的通用手段：以
`mr_fs_mknod("/dev/xxx", MR_INODE_CHR, &driver_obj)` 把驱动实例、控制块等挂到固定路径上，服务代码按路径 `open` 后从句柄
inode 取回对象指针。需要真正数据读写时，不要扩展 RootFS，而是在其目录上挂载实现 `fops` 的 文件系统（模板见 [RamFS](ramfs.md)
开发者篇第 12 节）。

### 13. 源码导读

| 位置                                        | 职责                                                                 |
|---------------------------------------------|----------------------------------------------------------------------|
| `mrX/fs/rootfs/rootfs.c:18`                 | inode 操作表（create/mknod，无 lookup/truncate）                     |
| `mrX/fs/rootfs/rootfs.c:26-27`              | `mr_file_system_t` 描述符，名字 `"rootfs"`、仅 mount 回调          |
| `mrX/fs/rootfs/rootfs.c:29` / `:43` / `:56` | 节点分配 / create 通道（仅目录）/ mknod 通道（任意类型 + priv 借用） |
| `mrX/fs/rootfs/rootfs.c:66`                 | 挂载回调：创建根目录 inode                                           |
| `mrX/fs/rootfs/rootfs.c:84-94`              | 自动注册 + 挂载为全局根（`MR_INIT_LEVEL_FS`）                        |
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

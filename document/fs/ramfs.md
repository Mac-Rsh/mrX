# RAM 文件系统（RamFS）

> RamFS 是一个纯内存的参考文件系统：目录结构由 VFS 命名空间维护，普通文件由可增长
> 的堆缓冲区支撑，可选支持基于 FIFO 的非阻塞命名管道；随内核自动注册，按名字
> `"ramfs"` 挂载，常用于临时存储与文件系统框架的参考实现/测试。

| 项       | 说明                                                                                                                                                          |
|----------|---------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 功能宏   | `MR_USE_FS_RAMFS`（依赖 `MR_USE_FS`，默认关闭）                                                                                                               |
| 头文件   | `<mrX/fs/mr_fs_file.h>`（经 VFS 使用，模块无独立公共头）                                                                                                      |
| 依赖模块 | [文件系统 VFS](fs.md)、[FIFO](../kernel/fifo.md)（`MR_USE_FS_RAMFS_FIFO` 时）、[动态内存 malloc](../libc/malloc.md)、[初始化 Initcall](../kernel/initcall.md) |
| 参考测试 | `mrX/test/fs/ramfs.c`、`mrX/test/fs/ramfs_matrix.c`                                                                                                           |

---

## 用户指南

### 1. 模块概述

RamFS 解决的问题是： **在没有块设备与持久存储的场合，提供一个开箱即用的内存文件
系统**。

特性：

- 通过 `MR_INIT_LEVEL_FS` initcall 自动注册 `"ramfs"`；该 initcall 由
  `mr_user_init()` 遍历执行，不属于 `mr_kernel_init()`
  的硬编码阶段。
- 普通文件由堆缓冲区支撑：写入自动扩展，空洞读为零，支持截断与扩展。
- 可选 FIFO 节点：以 [FIFO](../kernel/fifo.md) 环形队列实现的非阻塞字节管道。
- 目录枚举、重命名等命名空间能力全部来自 VFS（见 [文件系统 VFS](fs.md)）， RamFS
  自身只负责文件数据。
- 节点删除/卸载会摘除命名空间所有权；普通数据缓冲、FIFO 与 inode
  等在各自最后引用释放时回收。数据不持久化，掉电即失。

不适用于：需要掉电保存的数据（无后端存储）、需要跨任务阻塞同步的字节流 （FIFO
读写不阻塞，见第 4 节）。

### 2. 配置与依赖

| 配置项                      | 类型 | 默认值 | 说明                                                                 |
|-----------------------------|------|--------|----------------------------------------------------------------------|
| `MR_USE_FS_RAMFS`           | bool | n      | 启用 RamFS（依赖 `MR_USE_FS`，`mrX/include/mrX/fs/ramfs/Kconfig:6`） |
| `MR_USE_FS_RAMFS_FIFO`      | bool | y      | 启用 FIFO 节点；关闭后创建 FIFO 返回 `-MR_ENOSYS`                    |
| `MR_CFG_FS_RAMFS_FIFO_SIZE` | 整型 | 32     | 新建 FIFO 的默认字节容量，范围 1~1048576                             |

启用后包含 VFS 头文件即可使用（RamFS 通过挂载后的路径访问，无独立 API）：

```c
#include <mrX/fs/mr_fs_file.h>
```

在按名字挂载 `"ramfs"` 前，启动流程需要已经执行 `mr_user_init()`（或以其他方式确保
`mr_fs_ramfs_init` 对应 initcall
已运行完成注册）。

### 3. 快速上手

```c
#include <mrX/fs/mr_fs_file.h>

static int ramfs_demo(void) {
    mr_file_t file = MR_FILE_INIT(file);
    char buffer[16];
    mr_ssize_t ret;

    /* 在当前根的某个目录挂载 ramfs */
    if ((mr_fs_mkdir("/tmp") != 0)
        || (mr_fs_mount(MR_NULL, "/tmp", "ramfs", 0, MR_NULL) != 0)) {
        return -1;
    }

    /* 创建普通文件并写入 */
    if (mr_fs_open(&file, "/tmp/note.txt", MR_O_RDWR | MR_O_CREAT) != 0) {
        return -1;
    }
    mr_fs_write(&file, "hello ramfs", 11);
    mr_fs_close(&file);

    /* 读回内容 */
    mr_fs_open(&file, "/tmp/note.txt", MR_O_RDONLY);
    ret = mr_fs_read(&file, buffer, sizeof(buffer));
    mr_fs_close(&file);

    mr_fs_umount("/tmp");    /* 该挂载的全部 ramfs 数据被释放 */
    return (ret == 11) ? 0 : -1;
}
```

### 4. 使用说明

**普通文件**

- 位置语义：`MR_O_APPEND` 在节点数据锁内定位到文件末尾（与写入同一把锁）；
  `MR_O_TRUNC` 打开时清空。失败或零长 append 不改动原先的位置。
- 写入超过当前长度时自动用 `realloc` 扩展，文件位置与旧长度之间的空洞补零。
- 读取在文件末尾截断：返回实际可读字节数，位置在末尾时返回 0。
- `mr_fs_ftruncate` 可收缩或扩展文件；扩展部分补零，收缩到 0 释放缓冲区。
- `mr_fs_fstat` 返回通用状态字段：`type` 为 `MR_INODE_REG`，`size` 为当前 长度；
  `mode` 恒为 0（RamFS 未使用模式位）。

**并发访问**

- 同一个节点（普通文件或 FIFO）在同一时刻只允许一个数据操作：`read`/`write`/
  `ftruncate` 以及带 `MR_O_TRUNC` 的 `open`。若另一上下文正对同一节点执行上述
  操作，本次调用立即返回 `-MR_EBUSY`
  ，不会等待，可稍后重试。不同节点之间没有 RamFS 节点锁级别的互斥；多个句柄打开同一节点也受同一规则约束。零长度读写在
  **已经通过
  VFS 的句柄、访问模式与节点类型检查后**会在 RamFS callback 内直接返回
  0，不参与节点锁竞争。

**FIFO 节点**

- 用 `mr_fs_mkfifo("/path")` 或 `mr_fs_mknod("/path", MR_INODE_FIFO, MR_NULL)`
  创建，容量为 `MR_CFG_FS_RAMFS_FIFO_SIZE`（编译期确定，无运行期修改接口）。
- 读写完全不阻塞：读到当前存量为止，写到当前剩余空间为止，返回实际搬运的 字节数；队空读到
  0，队满写出 0。
- 不支持定位：`mr_fs_lseek` 返回 `-MR_ESPIPE`。
- `MR_O_TRUNC` 只对普通文件生效，FIFO 以 `O_TRUNC` 打开不会清空队列；`MR_O_APPEND`
  对 FIFO 也没有“定位到
  EOF”的特殊意义，写入仍只是向队尾追加字节。
- FIFO 与普通文件一样可以被 unlink，打开中的句柄用完即释放队列内存。

**数据生命周期**

- 文件数据与 FIFO 队列都存放于堆：unlink 后名字立即消失，数据在 inode 最后引用释放时回收。成功
  `mr_fs_umount`
  会摘除并清理整个挂载命名空间；若内部对象仍被额外显式引用持有，其最终内存释放仍遵循各对象的引用计数生命周期。
- 数据仅存在于内存中，复位或卸载后不可恢复，不要存放需要持久化的内容。

### 5. API 参考

RamFS 不定义自己的公开 API，全部操作通过 [文件系统 VFS](fs.md) 的通用接口完成。
下表列出每个 VFS 操作作用于 RamFS 节点时的行为：

| API                                  | 作用于 RamFS 时的行为                                                                                                              |
|--------------------------------------|------------------------------------------------------------------------------------------------------------------------------------|
| `mr_fs_open` + `MR_O_CREAT`          | 创建普通文件（`MR_INODE_REG`），缓冲区初始为空                                                                                     |
| `mr_fs_mkdir`                        | 创建目录（目录无数据缓冲区）                                                                                                       |
| `mr_fs_mkfifo` / `mr_fs_mknod(FIFO)` | 创建 FIFO 节点；`MR_USE_FS_RAMFS_FIFO` 关闭时 `-MR_ENOSYS`                                                                         |
| `mr_fs_read`                         | 普通文件按位置读取并在 EOF 截断；FIFO 非阻塞出队                                                                                   |
| `mr_fs_write`                        | 普通文件按位置写入并按需扩展；FIFO 非阻塞入队                                                                                      |
| `mr_fs_lseek`                        | 普通文件正常定位；FIFO 返回 `-MR_ESPIPE`                                                                                           |
| `mr_fs_ftruncate`                    | 调整普通文件长度；FIFO 返回 `-MR_EINVAL`（非普通文件）                                                                             |
| `mr_fs_fstat`                        | 返回通用字段：`type`/`mode`/`size`（无后端 stat 覆盖）                                                                             |
| `mr_fs_fsync` / `mr_fs_ioctl`        | RamFS 未提供对应 file callback，返回 `-MR_ENOSYS`                                                                                  |
| `mr_fs_readdir`                      | 使用 VFS 默认目录枚举（按 dentry 树遍历）                                                                                          |
| `mr_fs_unlink` / `mr_fs_close`       | 删除名字或释放句柄引用；最后一个引用释放时回收数据缓冲区或 FIFO 队列                                                               |
| `mr_fs_mount` / `mr_fs_umount`       | `source`/`flags`/`data` 被忽略；有打开句柄、在途操作或子挂载时卸载返回 `-MR_EBUSY`，成功后摘除实例命名空间并按引用生命周期回收对象 |

**`mr_fs_mount`（挂载 "ramfs"）**

```c
mr_err_t mr_fs_mount(const char *source, const char *path, const char *fstype,
                     int flags, void *data);
```

- `fstype` 必须为 `"ramfs"`；`source`、`flags`、`data` 会被忽略（可传
  `MR_NULL`/0）。
- `path` 为已存在的目录（或用 `mr_fs_mount_root` 挂为全局根）。
- 返回：成功 `0`，失败负数错误码（`-MR_ENOENT` 未知类型、`-MR_EBUSY` 已挂载、
  `-MR_ENOMEM` 内存不足）。

### 6. 完整示例

示例 1：普通文件的空洞写与截断（取材自 `mrX/test/fs/ramfs.c` 的
`ramfs_reclaim_cycles` 用例）：

```c
static void hole_write_demo(void) {
    mr_file_t file = MR_FILE_INIT(file);
    mr_stat_t stat;

    /* 创建文件，写入前缀，越过 EOF 定位后写入尾部 */
    mr_fs_open(&file, "/reclaim", MR_O_RDWR | MR_O_CREAT | MR_O_EXCL);
    mr_fs_write(&file, "prefix", 6);
    mr_fs_lseek(&file, 64, MR_SEEK_SET);
    mr_fs_write(&file, "tail", 4);        /* 空洞 [6, 64) 读出为零 */

    mr_fs_fstat(&file, &stat);          /* stat.size == 68 */
    mr_fs_ftruncate(&file, 3);          /* 收缩到 "pre" */
    mr_fs_ftruncate(&file, 0);          /* 收缩为空，释放缓冲区 */
    mr_fs_close(&file);
    mr_fs_unlink("/reclaim");
}
```

示例 2：FIFO 环形队列的填满、截断与回绕读（取材自 `mrX/test/fs/ramfs.c` 的
`ramfs_fifo_ring` 用例）：

```c
static void fifo_demo(void) {
    mr_file_t file = MR_FILE_INIT(file);
    char buffer[40];
    mr_ssize_t n;

    mr_fs_mkfifo("/fifo");                          /* 默认容量 32 */
    mr_fs_open(&file, "/fifo", MR_O_RDWR);

    mr_fs_write(&file, "ABCDEFGHIJKLMNOPQRST", 20); /* 写入 20 字节 */
    mr_fs_read(&file, buffer, 5);                   /* 读出 5 字节 */
    mr_fs_write(&file, "VWXYZ12345", 10);           /* 回填并发生回绕 */
    mr_fs_write(&file, "6789abcdef", 10);           /* 被截到 7（受剩余空间限制） */
    mr_fs_write(&file, "x", 1);                     /* 已满：写入 0 字节 */
    mr_fs_lseek(&file, 0, MR_SEEK_SET);             /* -MR_ESPIPE */

    /* 按顺序读出剩余数据 */
    while ((n = mr_fs_read(&file, buffer, sizeof(buffer))) > 0) {
        /* 消费 n 字节 */
    }
    mr_fs_close(&file);
    mr_fs_unlink("/fifo");
}
```

### 7. 常见问题（FAQ）

**Q：FIFO 写满后再写会阻塞或丢失旧数据吗？**

都不会。写操作非阻塞，返回本次实际写入字节数；队满时返回 0，已有数据保持
不变，由消费者读走后腾出空间。

**Q：为什么对 FIFO 调用 `mr_fs_lseek` 返回 `-MR_ESPIPE`？**

FIFO 是字节管道，没有位置概念，RamFS 显式拒绝定位；读写只能按队列顺序进行。

**Q：`mr_fs_mkfifo` 返回 `-MR_ENOSYS`？**

`MR_USE_FS_RAMFS_FIFO` 未启用。启用该配置并重新生成 `mr_config.h`。

**Q：FIFO 容量能按节点调整吗？**

不能。所有 FIFO 使用 `MR_CFG_FS_RAMFS_FIFO_SIZE` 的编译期容量，无运行期
设置接口；需要不同容量时增大该配置或改用普通文件。

**Q：数据会占用多少内存？什么时候归还？**

普通文件缓冲区按逻辑文件长度分配/重分配；目录没有 `ramfs_node_t` 数据区；FIFO
额外分配固定容量的环形缓冲。测试中的正常
unlink/close/umount 生命周期会最终恢复堆占用；若调用者额外持有 inode/dentry
等引用，释放会相应延后。

**Q：掉电或复位后文件还在吗？**

不在。RamFS 没有任何持久化后端，数据随堆内存消失；需要持久化时须另行实现
基于块设备的文件系统。

---

## 开发者指南

### 8. 架构总览

RamFS 是 VFS 之上的最小后端实现，自身不含路径与缓存逻辑：

```
+-----------------------------+
| VFS（路径解析/挂载/句柄）     |  mrX/fs/*.c
+-------------+---------------+
              | mr_inode_ops_t / mr_file_ops_t 回调
+-------------v---------------+
| RamFS（ramfs.c）            |  create / truncate / destroy
|  MR_INODE_REG -> 堆缓冲区    |  read / write（普通文件）
|  MR_INODE_FIFO -> mr_fifo   |  read / write / llseek（FIFO，可选）
+-------------+---------------+
              |
+-------------v---------------+
| 堆 malloc / FIFO（原子索引）  |
+-----------------------------+
```

- 模块初始化函数 `mr_fs_ramfs_init` 通过 `MR_INIT_EXPORT` 挂在
  `MR_INIT_LEVEL_FS`（`mrX/fs/ramfs/ramfs.c:441-444`），由
  `mr_user_init()` 的 initcall 遍历执行并调用
  `mr_file_system_register(&__ramfs)`。注册函数的返回值当前没有在该 initcall
  中检查。
- 文件系统描述符 `__ramfs`（`ramfs.c:57`）只提供 `mount` 回调，
  `unmount` 为 `MR_NULL`：实例没有需要释放的文件系统级后端资源，卸载路径全部由 VFS
  的引用释放链完成。
- `mount` 回调（`ramfs.c:422`）分配一个目录 inode 作为根并置
  `sb->dops = MR_NULL`（使用默认 dentry 名字操作）。

### 9. 数据结构

RamFS 的节点数据是"一把数据锁 + 按类型选用的联合体"（`mrX/fs/ramfs/ramfs.c`，
`ramfs_node_t`）：

```c
typedef struct ramfs_node {
    mr_spinlock_t lock;             /* Data lock, single try, never spins */
    union ramfs_node_data {
        struct ramfs_node_regular {
            mr_uint8_t *data;       /* Heap buffer of a regular file */
        } regular;
#if defined(MR_USE_FS_RAMFS_FIFO)
        mr_fifo_t fifo;             /* Ring buffer of a FIFO node */
#endif
    } data;
} ramfs_node_t;
```

| 成员/规则           | 说明                                                                                                      |
|---------------------|-----------------------------------------------------------------------------------------------------------|
| `lock`              | 节点数据锁。`ramfs_node_try_lock` 只做一次 `mr_spinlock_try_lock`，失败返回 `-MR_EBUSY`；不自旋、不关中断 |
| `data.regular.data` | 普通文件的内容缓冲区，`mr_realloc` 增长，`inode->size` 为逻辑长度                                         |
| `data.fifo`         | FIFO 节点的环形队列；读写下标为原子量（`mrX/include/mrX/kernel/mr_fifo.h:25`）                            |
| 目录节点            | `inode->priv == MR_NULL`，没有私有数据，也没有数据锁                                                      |

每个 RamFS inode 通过 `mr_inode_create(type, &__ramfs_inodes, fops, node)`
创建：`priv` 指向 `ramfs_node_t`，`fops` 按类型选择
`__ramfs_regular_files`（`ramfs.c:35`）或 `__ramfs_fifo_files`（`ramfs.c:47`）， 目录的
`fops` 为 `MR_NULL`（读目录走 VFS
默认实现）。

### 10. 核心流程

**节点分配**：`ramfs_inode_alloc`（`mrX/fs/ramfs/ramfs.c:73`）是 create/mknod
的公共路径：

1. 类型校验：只接受 `MR_INODE_DIR`/`MR_INODE_REG`/`MR_INODE_FIFO`，其余返回
   `-MR_EINVAL`；FIFO 特性关闭时 FIFO 请求返回 `-MR_ENOSYS`（`ramfs.c:83-93`）。
2. 非目录节点分配 `ramfs_node_t`；普通文件不预分配数据缓冲区（首次写/截断时
   才增长），FIFO 用
   `mr_fifo_alloc(&node->data.fifo, MR_CFG_FS_RAMFS_FIFO_SIZE, 1)`
   分配环形队列，失败回滚返回 `-MR_ENOMEM`。
3. `create` 回调（`ramfs.c:150`）忽略 `dir`/`name`，直接转发到分配函数—— 节点归属完全由
   VFS 的 dentry 树表达。

**普通文件写**：`ramfs_regular_write` 零长请求直接返回且不改 `pos`。否则先取节点
锁，再 `mr_file_apply_append`（`O_APPEND` 时
`pos = inode->size`），然后做位置 范围检查（`pos < 0` 或 `size` 溢出 `mr_int64_t`
返回 `-MR_ERANGE`，终点超出
`MR_SIZE_MAX` 返回 `-MR_ENOMEM`，失败时恢复原 `pos`），再按需 `mr_realloc`
扩展并补零 `[old_size, pos)` 空洞，最后拷贝数据并推进 `size`/`pos`。

**普通文件读**：`ramfs_regular_read`（`ramfs.c:250`）在 EOF 处截断请求长度，
拷贝后推进位置；位置在末尾或请求长度为 0 时返回
0。

**截断**：`ramfs_inode_truncate`（`ramfs.c:166`）长度相等直接返回 0；增长时
`realloc` 并把扩展区间补零；收缩到 0 时释放缓冲区；负长度返回 `-MR_EINVAL`。
`MR_O_TRUNC` 的清空、`mr_fs_ftruncate` 都经由该回调。

**FIFO 读写**：`ramfs_fifo_read`/`ramfs_fifo_write`（`ramfs.c:352,385`）把请求
裁剪到当前存量/剩余空间后调用
`mr_fifo_read`/`mr_fifo_write`；零长请求不触碰 队列。`ramfs_fifo_llseek`（
`ramfs.c:413`）恒返回 `-MR_ESPIPE`。

**释放**：`ramfs_inode_destroy`（`ramfs.c:226`）作为 inode 的 `ops->destroy`
钩子，在 inode 引用归零时释放数据缓冲区/FIFO 队列与节点结构——打开中的句柄 持有
inode 引用，因此 unlink 后数据存活到
`close`。

### 11. 并发与中断安全

- 命名空间操作（创建/删除/枚举）的互斥由 VFS 的文件系统结构锁与超级块 `op_cnt`
  屏障保证（见 [文件系统 VFS](fs.md) 开发者篇第 11 节）；RamFS 不为目录树另加锁。
- 节点数据由 RamFS 自己的 `ramfs_node_t.lock` 保护，VFS 不参与：
  `ramfs_regular_read/write`、`ramfs_inode_truncate`、`ramfs_fifo_read/write`
  进入时 `ramfs_node_try_lock`，失败立即返回 `-MR_EBUSY`。`O_APPEND` 的 EOF 选择与
  `mr_realloc`、`inode->size`
  更新都在同一把锁内，因此两个 append 写者不会采样到同一个 size。锁只做一次 CAS
  尝试，不自旋、不关中断。
- 零长读写在取锁之前返回，不会与在途操作冲突，也不会误报 `-MR_EBUSY`；位置
  范围检查在锁内、append 之后进行，失败时恢复原
  `pos`。
- FIFO 的环形队列下标本身是原子量，但一次调用内部"先查存量再搬运"依赖节点锁
  才成为整体原子事务。多个上下文交替读写时，各次调用的先后顺序由调度决定，
  冲突的一方得到 `-MR_EBUSY`。
- `ramfs_inode_destroy` 不取节点锁：它只在 inode 最后引用释放时运行；正常文件 I/O
  通过打开句柄持有 inode 引用，因此在途文件操作会阻止
  inode 进入最终 destroy。

### 12. 扩展与派生

RamFS 的定位是文件系统实现范例（`mrX/include/mrX/fs/ramfs/Kconfig:6`）：
实现自定义文件系统时可直接以 `ramfs.c`
为模板——只实现 `mount` 回调、
`mr_inode_ops_t` 的 `create`/`truncate`/`destroy` 与 `mr_file_ops_t` 的
`read`/`write`，把私有数据挂在 `inode->priv` 上，其余命名空间能力全部交给
VFS。注册与挂载方式见 [文件系统 VFS](fs.md) 开发者篇第
12 节。

### 13. 源码导读

| 位置                                                             | 职责                                                                                   |
|------------------------------------------------------------------|----------------------------------------------------------------------------------------|
| `mrX/fs/ramfs/ramfs.c:57-58`                                     | 文件系统名 `"ramfs"`                                                                   |
| `mrX/fs/ramfs/ramfs.c:24`                                        | inode 操作表（create/truncate/destroy）                                                |
| `mrX/fs/ramfs/ramfs.c:35` / `:47`                                | 普通文件 / FIFO 的 file 操作表                                                         |
| `mrX/fs/ramfs/ramfs.c:57`                                        | `mr_file_system_t` 描述符（仅 mount 回调）                                             |
| `mrX/fs/ramfs/ramfs.c:61`                                        | `ramfs_node_t` 节点结构（数据锁 + 类型联合体）；`ramfs_node_try_lock/unlock` 见 `:156` |
| `mrX/fs/ramfs/ramfs.c:73`                                        | 节点分配（类型校验、FIFO 容量与失败回滚）                                              |
| `mrX/fs/ramfs/ramfs.c:166` / `:253` / `:287`                     | 截断 / 读 / 写的实现细节                                                               |
| `mrX/fs/ramfs/ramfs.c:352` / `:385` / `:416`                     | FIFO 读 / 写 / 拒绝定位                                                                |
| `mrX/fs/ramfs/ramfs.c:422` / `:441`                              | 挂载回调 / 注册入口（`MR_INIT_LEVEL_FS`）                                              |
| `mrX/test/fs/ramfs.c` / `ramfs_matrix.c` / `ramfs_rootfs_deep.c` | 普通文件边界、FIFO 回绕、矩阵化与深层生命周期行为测试                                  |

### 14. 附录

**配置宏**

| 宏                          | 说明                                           |
|-----------------------------|------------------------------------------------|
| `MR_USE_FS_RAMFS`           | 模块开关，依赖 `MR_USE_FS`，默认 n             |
| `MR_USE_FS_RAMFS_FIFO`      | FIFO 节点开关，依赖 `MR_USE_FS_RAMFS`，默认 y  |
| `MR_CFG_FS_RAMFS_FIFO_SIZE` | FIFO 默认容量（字节），默认 32，范围 1~1048576 |

**错误码**（经 VFS API 返回，均为负数）：`-MR_EBUSY`（同一 RamFS 节点数据锁竞争）、
`-MR_ENOSYS`（FIFO 特性关闭或 file 操作未提供）、
`-MR_EINVAL`（不支持的节点类型/负长度）、`-MR_ENOMEM`（缓冲区扩展或队列分配失败）、
`-MR_ERANGE`（普通文件写位置范围溢出）、
`-MR_ESPIPE`（FIFO 定位）。VFS 还会在进入 RamFS callback
前产生访问模式、目录类型、路径等错误，详见 [文件系统 VFS](fs.md) 附录。

[**中文**](fs_filesystem.md) | [**English**](fs_filesystem_en.md)

---

# 文件系统类型

File-system 模块登记「一种文件系统」。它不是一次挂载，而是可被多次挂载的类型描述符：名字、`mount`、可选的 `unmount` 和 `kill_sb`。真正的挂载实例是 [super-block](superblock.md)。

应用通过 `mr_fs_mount()` / `mr_fs_mount_root()` 使用已注册的名字（例如 `"ramfs"`、`"rootfs"`）。实现一个文件系统时，先静态定义 `mr_file_system_t`，再 `mr_file_system_register()`。

## 描述符

```c
typedef mr_err_t (*mr_file_system_mount_t)(mr_super_block_t *sb,
                                           const char *source, int flags,
                                           void *data);
typedef mr_err_t (*mr_file_system_unmount_t)(mr_super_block_t *sb);
typedef void (*mr_file_system_kill_sb_t)(mr_super_block_t *sb);

typedef struct mr_file_system {
    const char *name;
    struct mr_file_system *next;
    mr_file_system_mount_t *mount;
    mr_file_system_unmount_t *unmount;
    mr_file_system_kill_sb_t *kill_sb;
} mr_file_system_t;

#define MR_FILE_SYSTEM_INIT(_name, _mount, _unmount, _kill_sb)
```

| 字段 | 含义 |
|------|------|
| `name` | 类型名，挂载时的 `fstype`。不能为空。注册后必须一直有效。 |
| `next` | 全局注册链表，由 VFS 维护。 |
| `mount` | 必选。填充 `sb->root`（必须是目录 inode）和可选的 `sb->dops` / `sb->priv`。 |
| `unmount` | 可选。卸载准备：此时 super-block 已 `detaching`，不能再开始新的路径操作。返回错误则取消卸载。 |
| `kill_sb` | 可选。最后一个 super-block 引用释放时调用，释放 `priv` 等实例资源。 |

`mount` 失败时，VFS **不会**调用 `kill_sb`。文件系统必须在 `mount` 返回非 0 之前自行清掉已分配的根 inode 和私有数据。`mount` 成功之后，根 inode 的释放和 `kill_sb` 由 VFS 在 super-block 生命周期结束时安排。

`kill_sb` 与 inode `destroy` 一样是析构：对象已在释放路径上，不能失败重试。

## 注册和查找

```c
mr_err_t mr_file_system_register(mr_file_system_t *fs);
mr_file_system_t *mr_file_system_find(const char *name);
```

`mr_file_system_register()` 把描述符链到全局表。同一指针或同一 `name` 再注册返回 `-MR_EEXIST`。`mount` 必须非空，否则断言失败。

`mr_file_system_find()` 按名字查找，未找到返回 `MR_NULL`。`mr_fs_mount()` 内部用它把 `fstype` 字符串变成描述符。

注册表由命名空间锁保护。描述符本身必须是静态存储或与内核同寿命；不要注册栈上的对象。

## 挂载回调要做什么

`mr_super_block_create()` 分配 super-block 后立刻调用 `type->mount(sb, source, flags, data)`。此时对象尚未 `mr_object_init()`，也还没挂到树上。`mount` 应当：

1. 按 `source` / `flags` / `data` 准备私有状态，放入 `sb->priv`。
2. 创建根目录 inode，赋给 `sb->root`。
3. 如需自定义 dentry 哈希，设置 `sb->dops`。
4. 成功返回 0。VFS 接着检查 `sb->root` 是目录，初始化对象，把 `sb` 回链到根 inode，并加入全局 sb 链表。

`source` 和 `flags` 由路径层原样传入。当前 VFS 不解释 `flags`，内置 ramfs/rootfs 忽略它们。

## 卸载回调顺序

路径层 `mr_fs_umount()` 大致顺序：

1. 查到挂载点对应的 super-block。
2. `mr_super_block_detach_prepare()`：不能有子挂载、不能有进行中的 `op_cnt`。
3. 调用 `unmount`（若有）。失败则 `detach_cancel()`。
4. `mr_super_block_detach()` 把 `mnt` / `root` 引用转给调用者。
5. `mr_super_block_del()` 从 sb 链表摘下，放下根 inode 创建引用。
6. 最后一个 sb 引用释放时调用 `kill_sb`，再 `mr_free(sb)`。

有子挂载时 prepare 返回 `-MR_EBUSY`，需要先卸子挂载。这不是递归卸载。

## 使用示例

```c
#include <mrX/fs/mr_fs.h>
#include <mrX/fs/mr_sb.h>
#include <mrX/fs/mr_inode.h>

static mr_err_t sample_mount(mr_super_block_t *sb, const char *source,
                             int flags, void *data)
{
    mr_inode_t *root;

    (void)source;
    (void)flags;
    (void)data;

    root = mr_inode_create(MR_INODE_DIR, MR_NULL, MR_NULL, MR_NULL);
    if (!root) {
        return -MR_ENOMEM;
    }
    sb->root = root;
    return 0;
}

static mr_file_system_t sample_fs =
    MR_FILE_SYSTEM_INIT("samplefs", sample_mount, MR_NULL, MR_NULL);

void sample_fs_register(void)
{
    mr_file_system_register(&sample_fs);
}
```

内置 ramfs、rootfs 在 `MR_INIT_LEVEL_FS` 用同样方式注册。rootfs 还会立刻 `mr_fs_mount_root()`。

## 注意事项

- 类型描述符注册后不能释放，直到不再会被 `find` / 挂载用到。
- `mount` 失败必须自己回收；不要指望 `kill_sb`。
- `unmount` 可以拒绝卸载；`kill_sb` 不能。
- 名字比较区分大小写，使用 `mr_strcmp()`。

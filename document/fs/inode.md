[**中文**](fs_inode.md) | [**English**](fs_inode_en.md)

---

# Inode

Inode 是文件系统节点对象。它描述类型、大小、链接数，并挂上两张操作表：`mr_inode_ops_t` 处理目录级创建/查找/删除，`mr_file_ops_t` 处理已打开句柄的读写。Dentry 保存路径位置，inode 保存节点本身；硬链接就是多个 dentry 指向同一个 inode。

Inode 以内嵌 `mr_object_t` 管理引用。最后一个引用释放时调用 `ops->destroy`（若有），再放下 super-block 引用。`destroy` 是对象死后的析构，返回类型为 `void`，失败也不能复活对象。句柄收尾错误走 `fops->close`。

## 类型和状态

```c
typedef enum mr_inode_type {
    MR_INODE_NONE = 0,
    MR_INODE_REG  = 1,
    MR_INODE_DIR  = 2,
    MR_INODE_CHR  = 3,
    MR_INODE_BLK  = 4,
    MR_INODE_LNK  = 5,
} mr_inode_type_t;

typedef struct mr_inode {
    mr_object_t parent;
    mr_inode_type_t type;
    mr_uint32_t mode;
    mr_uint32_t nlink;
    mr_int64_t size;
    struct mr_super_block *sb;
    const mr_inode_ops_t *ops;
    const mr_file_ops_t *fops;
    void *priv;
} mr_inode_t;
```

| 字段 | 含义 |
|------|------|
| `parent` | 对象控制块。 |
| `type` | 节点类型。`MR_INODE_NONE` 非法，不能用来初始化。 |
| `mode` | 通用模式位，默认 0。`stat` 钩子可以覆盖。 |
| `nlink` | 硬链接数。初始化为 1；`mr_dentry_unlink()` 成功时减 1。 |
| `size` | 逻辑大小。普通文件的读写和 truncate 会更新它。 |
| `sb` | 所属 super-block。挂载成功后 VFS 会 `mr_super_block_get()` 填在根 inode 上；之后创建的 inode 由文件系统自己回链。 |
| `ops` | 目录/节点操作。可以为 `MR_NULL`。 |
| `fops` | 打开后的文件操作。可以为 `MR_NULL`。 |
| `priv` | 文件系统私有数据。ramfs 用来挂数据缓冲。 |

```c
#define MR_INODE_TYPE(_inode)  ((_inode)->type)
#define MR_INODE_SIZE(_inode)  ((_inode)->size)
```

`mr_dirent_t` 和 `mr_stat_t` 也定义在本头文件中，供 `readdir` 和 `stat` 填充。

## Inode 操作表

```c
typedef struct mr_inode_ops {
    mr_err_t (*create)(mr_inode_t *dir, const char *name,
                       mr_inode_type_t type, mr_inode_t **out);
    mr_err_t (*mknod)(mr_inode_t *dir, const char *name,
                      mr_inode_type_t type, void *priv, mr_inode_t **out);
    mr_err_t (*link)(mr_inode_t *dir, const char *name, mr_inode_t *inode);
    mr_err_t (*symlink)(mr_inode_t *dir, const char *name,
                        const char *target, mr_inode_t **out);
    mr_err_t (*lookup)(mr_inode_t *dir, const char *name, mr_inode_t **out);
    mr_ssize_t (*readlink)(mr_inode_t *inode, char *buf, mr_size_t size);
    mr_err_t (*stat)(mr_inode_t *inode, mr_stat_t *stat);
    mr_err_t (*unlink)(mr_inode_t *dir, const char *name);
    mr_err_t (*rename)(mr_inode_t *old_dir, mr_dentry_t *old_dentry,
                       mr_inode_t *new_dir, mr_dentry_t *new_dentry);
    mr_err_t (*truncate)(mr_inode_t *inode, mr_file_t *file, mr_int64_t length);
    void (*destroy)(mr_inode_t *inode);
} mr_inode_ops_t;
```

未实现的函数指针保持 `MR_NULL`。VFS 在缺省时有内建行为：

| 回调 | 缺省行为 |
|------|----------|
| `create` / `mknod` / `lookup` | 路径层返回 `-MR_ENOSYS` 或 `-MR_ENOENT`。 |
| `link` / `symlink` | 仅改命名空间：新 dentry 绑到已有 inode，或创建带目标字符串的内存符号链接。 |
| `unlink` / `rename` | 只提交 dentry 树。 |
| `stat` | 填 `type` / `mode` / `nlink` / `size`。 |
| `truncate` | `O_TRUNC` 在缺 hook 时打开失败，返回 `-MR_ENOSYS`。 |
| `destroy` | 什么都不做。动态 inode 仍会释放 `mr_inode_t` 本身。 |

约定：

- `create` / `mknod` / `lookup` / `symlink` 成功时必须通过 `*out` 返回一个调用者尚未绑定到 dentry 的 inode 引用。
- 回调在命名空间锁外运行，可以分配内存，不能假设当前 dentry 树锁仍持有。
- `destroy` 在引用已经归零、对象即将释放时调用。只释放 `priv` 等私有资源，不要再 `mr_inode_put()` 自己，也不要返回错误。

## 文件操作表

```c
typedef struct mr_file_ops {
    mr_err_t (*open)(mr_inode_t *inode, mr_file_t *file);
    mr_err_t (*close)(mr_file_t *file);
    mr_ssize_t (*read)(mr_file_t *file, void *buf, mr_size_t size);
    mr_ssize_t (*write)(mr_file_t *file, const void *buf, mr_size_t size);
    mr_int64_t (*llseek)(mr_file_t *file, mr_int64_t offset, int whence);
    mr_err_t (*ioctl)(mr_file_t *file, int cmd, void *arg);
    mr_err_t (*readdir)(mr_file_t *file, mr_dirent_t *dirent);
    mr_err_t (*sync)(mr_file_t *file);
} mr_file_ops_t;
```

这些回调作用在已打开的 `mr_file_t` 上：

| 回调 | 缺省行为 |
|------|----------|
| `open` / `close` | 成功。`close` 的错误仍会销毁句柄。 |
| `read` / `write` | 目录或缺少 hook 时由路径层拒绝。 |
| `llseek` | 默认按 `pos` 和 `inode->size` 计算，负位置钳到 0。 |
| `readdir` | 默认枚举 dentry 缓存的子节点。 |
| `sync` | 返回 `-MR_ENOSYS`。 |

`close` 可以报告 flush 失败，但不能阻止 VFS 放掉 dentry/inode 引用。需要可观测的收尾错误时实现 `close` 或 `sync`，不要改 `destroy` 的返回类型。

## 模块接口

```c
mr_err_t mr_inode_init(mr_inode_t *inode, mr_inode_type_t type,
                       const mr_inode_ops_t *ops, const mr_file_ops_t *fops,
                       void *priv);
mr_inode_t *mr_inode_create(mr_inode_type_t type, const mr_inode_ops_t *i_op,
                            const mr_file_ops_t *i_fop, void *priv);
mr_inode_t *mr_inode_get(mr_inode_t *inode);
mr_bool_t mr_inode_put(mr_inode_t *inode);
```

`mr_inode_init()` 初始化静态 inode：`nlink = 1`，`size = 0`，`sb = MR_NULL`。`mr_inode_create()` 从堆分配动态 inode，释放路径在 `destroy` 之后 `mr_free()` 对象本身。

文件系统在 `mount` 回调里必须创建一个目录类型的根 inode 并赋给 `sb->root`。VFS 随后会把 `sb` 回链到该 inode。之后 `create` 出来的子 inode 应使用同一套 `ops`/`fops`，并在合适时机把 `inode->sb` 设上（若需要随 inode 一起钉住 super-block）。

## 使用示例

RamFS 的根 inode 创建可以简化成：

```c
static const mr_inode_ops_t sample_iops = {
    .create = sample_create,
    .truncate = sample_truncate,
    .destroy = sample_destroy,
};

static const mr_file_ops_t sample_fops = {
    .read = sample_read,
    .write = sample_write,
};

static mr_err_t sample_mount(mr_super_block_t *sb, const char *source,
                             int flags, void *data)
{
    mr_inode_t *root;

    (void)source;
    (void)flags;
    (void)data;

    root = mr_inode_create(MR_INODE_DIR, &sample_iops, &sample_fops, MR_NULL);
    if (!root) {
        return -MR_ENOMEM;
    }
    sb->root = root;
    sb->dops = MR_NULL;
    return 0;
}
```

`create` 成功时把新 inode 放进 `*out` 并返回 0。不要在回调里把 inode 挂到 dentry 上，那是 VFS 的工作。

## 注意事项

- `destroy` 与 `fops->close` 不是同一层。前者释放节点私有内存，后者结束一个打开实例。
- 静态 inode 的 `destroy` 不能 `mr_free(inode)`；只有动态类释放函数释放对象内存。
- `nlink` 减到 0 并不立刻 `destroy`。只要还有打开句柄或 dentry 引用，inode 就还活着。
- 目录只能只读打开；truncate 拒绝目录和其他非普通文件。

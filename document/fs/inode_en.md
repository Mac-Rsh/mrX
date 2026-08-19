[**中文**](fs_inode.md) | [**English**](fs_inode_en.md)

---

# Inode

An inode is a file-system node. It holds type, size, and link count, plus two operation tables: `mr_inode_ops_t` for directory-level create/lookup/unlink, and `mr_file_ops_t` for I/O on an open handle. A dentry stores the path location; the inode stores the node itself. A hard link is several dentries pointing at one inode.

Inodes use an embedded `mr_object_t`. When the last reference is dropped, `ops->destroy` runs if present, then the super-block reference is put. `destroy` is a post-mortem destructor with type `void`; failure cannot revive the object. Handle-finalization errors go through `fops->close`.

## Type and Fields

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

| Field | Meaning |
|-------|---------|
| `parent` | Object control block. |
| `type` | Node type. `MR_INODE_NONE` is illegal and cannot be used to initialize. |
| `mode` | Generic mode bits, default 0. A `stat` hook may override them. |
| `nlink` | Hard-link count. Starts at 1; `mr_dentry_unlink()` decrements it on success. |
| `size` | Logical size. Regular-file read/write/truncate update it. |
| `sb` | Owning super-block. After a successful mount, VFS `mr_super_block_get()`s it onto the root inode; later inodes are back-linked by the file system if needed. |
| `ops` | Directory/node operations. May be `MR_NULL`. |
| `fops` | File operations after open. May be `MR_NULL`. |
| `priv` | File-system private data. Ramfs hangs its data buffer here. |

```c
#define MR_INODE_TYPE(_inode)  ((_inode)->type)
#define MR_INODE_SIZE(_inode)  ((_inode)->size)
```

`mr_dirent_t` and `mr_stat_t` are also declared in this header for `readdir` and `stat`.

## Inode Operations

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

Leave unimplemented pointers `MR_NULL`. VFS then uses built-in behavior:

| Callback | Default |
|----------|---------|
| `create` / `mknod` / `lookup` | Path layer returns `-MR_ENOSYS` or `-MR_ENOENT`. |
| `link` / `symlink` | Namespace only: bind a new dentry to the existing inode, or create an in-memory symlink with a target string. |
| `unlink` / `rename` | Commit the dentry tree only. |
| `stat` | Fill `type` / `mode` / `nlink` / `size`. |
| `truncate` | `O_TRUNC` fails at open with `-MR_ENOSYS` if the hook is missing. |
| `destroy` | No-op. A dynamic inode still frees the `mr_inode_t` itself. |

Contracts:

- On success, `create` / `mknod` / `lookup` / `symlink` must return an inode reference in `*out` that is not yet bound to a dentry.
- Callbacks run without the namespace lock. They may allocate. They must not assume the dentry-tree lock is held.
- `destroy` runs after the reference count has already reached zero. Free `priv` and other private resources only. Do not `mr_inode_put()` self, and do not return an error.

## File Operations

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

These callbacks act on an open `mr_file_t`:

| Callback | Default |
|----------|---------|
| `open` / `close` | Success. A `close` error still destroys the handle. |
| `read` / `write` | Rejected by the path layer for directories or a missing hook. |
| `llseek` | Default uses `pos` and `inode->size`; negative results clamp to 0. |
| `readdir` | Default enumerates cached child dentries. |
| `sync` | Returns `-MR_ENOSYS`. |

`close` may report a flush failure, but it cannot stop VFS from dropping the dentry/inode pins. Implement `close` or `sync` for observable finalization errors; do not change the `destroy` return type.

## Module Interfaces

```c
mr_err_t mr_inode_init(mr_inode_t *inode, mr_inode_type_t type,
                       const mr_inode_ops_t *ops, const mr_file_ops_t *fops,
                       void *priv);
mr_inode_t *mr_inode_create(mr_inode_type_t type, const mr_inode_ops_t *i_op,
                            const mr_file_ops_t *i_fop, void *priv);
mr_inode_t *mr_inode_get(mr_inode_t *inode);
mr_bool_t mr_inode_put(mr_inode_t *inode);
```

`mr_inode_init()` initializes a static inode: `nlink = 1`, `size = 0`, `sb = MR_NULL`. `mr_inode_create()` allocates a dynamic inode; the release path `mr_free()`s the object after `destroy`.

In `mount`, the file system must create a directory root inode and assign it to `sb->root`. VFS then back-links `sb` onto that inode. Later `create`d inodes should share the same `ops`/`fops`, and set `inode->sb` when the inode needs to pin the super-block.

## Usage Example

A ramfs-style root inode:

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

On success, `create` stores the new inode in `*out` and returns 0. Do not attach the inode to a dentry inside the callback; VFS does that.

## Notes

- `destroy` and `fops->close` are different layers. The former frees node-private memory; the latter ends one open instance.
- A static inode `destroy` must not `mr_free(inode)`. Only the dynamic class release frees the object memory.
- `nlink` reaching 0 does not destroy immediately. The inode lives while open handles or dentry references remain.
- Directories open read-only only. Truncate rejects directories and other non-regular files.

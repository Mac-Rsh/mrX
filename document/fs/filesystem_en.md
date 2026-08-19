[**中文**](fs_filesystem.md) | [**English**](fs_filesystem_en.md)

---

# File-System Type

The file-system module registers a *kind* of file system. It is not one mount. It is a type descriptor that can be mounted many times: a name, `mount`, and optional `unmount` / `kill_sb`. The mount instance is the [super-block](superblock_en.md).

Applications pass a registered name such as `"ramfs"` or `"rootfs"` to `mr_fs_mount()` / `mr_fs_mount_root()`. To implement a file system, define a static `mr_file_system_t` and call `mr_file_system_register()`.

## Descriptor

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

| Field | Meaning |
|-------|---------|
| `name` | Type name, used as `fstype` at mount. Must not be empty. Must remain valid after registration. |
| `next` | Global registry list, owned by VFS. |
| `mount` | Required. Fill `sb->root` (must be a directory inode) and optionally `sb->dops` / `sb->priv`. |
| `unmount` | Optional. Unmount prepare: the super-block is already `detaching`, so new path operations cannot start. A non-zero return cancels unmount. |
| `kill_sb` | Optional. Called when the last super-block reference is dropped, to free `priv` and other instance resources. |

If `mount` fails, VFS does **not** call `kill_sb`. The file system must clean up the root inode and private data before returning non-zero. After a successful `mount`, VFS arranges root-inode release and `kill_sb` at the end of the super-block lifetime.

Like inode `destroy`, `kill_sb` is a destructor: the object is already on the release path and cannot fail retryably.

## Register and Find

```c
mr_err_t mr_file_system_register(mr_file_system_t *fs);
mr_file_system_t *mr_file_system_find(const char *name);
```

`mr_file_system_register()` links the descriptor into the global table. Registering the same pointer or the same `name` again returns `-MR_EEXIST`. `mount` must be non-null or an assertion fails.

`mr_file_system_find()` looks up by name and returns `MR_NULL` if missing. `mr_fs_mount()` uses it to turn the `fstype` string into a descriptor.

The registry is protected by the namespace lock. The descriptor must be static or live as long as the kernel; do not register a stack object.

## What `mount` Must Do

`mr_super_block_create()` allocates the super-block and immediately calls `type->mount(sb, source, flags, data)`. The object is not yet `mr_object_init()`ed and not yet attached. `mount` should:

1. Build private state from `source` / `flags` / `data` into `sb->priv`.
2. Create a root directory inode and assign `sb->root`.
3. Set `sb->dops` if the file system needs a custom dentry hash.
4. Return 0. VFS then checks that `sb->root` is a directory, initializes the object, back-links `sb` onto the root inode, and inserts the super-block into the global list.

`source` and `flags` are passed through from the path layer. VFS does not interpret `flags` today; built-in ramfs/rootfs ignore them.

## Unmount Order

`mr_fs_umount()` roughly:

1. Find the super-block for the mount-point.
2. `mr_super_block_detach_prepare()`: no child mounts, no in-flight `op_cnt`.
3. Call `unmount` if present. On failure, `detach_cancel()`.
4. `mr_super_block_detach()` transfers the `mnt` / `root` references to the caller.
5. `mr_super_block_del()` unlinks from the sb list and drops the root inode creation reference.
6. The last sb put calls `kill_sb`, then `mr_free(sb)`.

Child mounts make prepare return `-MR_EBUSY`. Unmount the children first. This is not recursive unmount.

## Usage Example

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

Built-in ramfs and rootfs register the same way at `MR_INIT_LEVEL_FS`. Rootfs also calls `mr_fs_mount_root()` immediately.

## Notes

- After registration the descriptor must not be freed while `find` or mount can still see it.
- A failed `mount` must reclaim its own allocations; do not expect `kill_sb`.
- `unmount` may refuse; `kill_sb` may not.
- Names are case-sensitive via `mr_strcmp()`.

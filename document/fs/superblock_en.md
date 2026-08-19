[**中文**](fs_superblock.md) | [**English**](fs_superblock_en.md)

---

# Super-block

A super-block is one mounted instance. One [file-system type](filesystem_en.md) may have many super-blocks. It holds the root inode, the root dentry overlaid on the namespace, the mount-point, the parent/child mount lists, and a counter that keeps unmount from racing path operations.

Applications do not create super-blocks directly; they call `mr_fs_mount()` / `mr_fs_umount()`. A file-system implementation fills `sb->root` in `mount`; VFS initializes the object and attaches it.

## Control Block

```c
typedef struct mr_super_block {
    mr_object_t parent;
    mr_dentry_t *mnt;
    mr_dentry_t *root_dentry;
    mr_inode_t *root;
    int flags;
    struct mr_super_block *next;
    struct mr_super_block *mount_parent;
    struct mr_super_block *mount_child;
    struct mr_super_block *mount_next;
    struct mr_file_system *type;
    const mr_dentry_ops_t *dops;
    mr_bool_t detaching;
    mr_size_t op_cnt;
    void *priv;
} mr_super_block_t;
```

| Field | Meaning |
|-------|---------|
| `parent` | Object control block. Super-blocks are dynamic only; release `mr_free()`s the object. |
| `mnt` | Mount-point dentry. For the global ROOT mount, `mnt == root_dentry`. |
| `root_dentry` | Visible root of this file system. For a nested mount it covers `mnt->mounted`. |
| `root` | Root directory inode, created by the `mount` callback. |
| `flags` | Flags passed at mount time, stored as-is. |
| `next` | Global super-block list. |
| `mount_parent` / `mount_child` / `mount_next` | Mount topology: parent instance, first child, siblings. |
| `type` | File-system type descriptor. |
| `dops` | Name ops given to the mounted root dentry. |
| `detaching` | Unmount prepare has started; new `op_enter` returns `-MR_EBUSY`. |
| `op_cnt` | In-flight namespace operations. Non-zero blocks detach prepare. |
| `priv` | File-system private data. |

## Create

```c
mr_err_t mr_super_block_create(mr_file_system_t *type, const char *source,
                               int flags, void *data, mr_super_block_t **out);
```

Allocates a dynamic super-block and calls `type->mount`. If `mount` fails, the new sb is freed and `kill_sb` is not called. On success `sb->root` must exist and be a directory; otherwise the fail path puts the root inode and calls `kill_sb`.

After object init, VFS `mr_super_block_get()`s onto `sb->root->sb` so the root inode pins this super-block. On return `*out` holds the creation reference.

## Attach to the Namespace

```c
mr_err_t mr_super_block_attach(mr_super_block_t *sb, mr_dentry_t *mnt,
                               mr_dentry_t *root);
mr_err_t mr_super_block_detach(mr_super_block_t *sb, mr_dentry_t **mnt,
                               mr_dentry_t **root);
```

`attach` has two cases:

- **Global ROOT**: `mnt == root`. The root dentry is the global ROOT, `ops` becomes `sb->dops`, and there is no `mount_parent`.
- **Nested mount**: `root` must already be bound to `sb->root` and must have no parent. `mnt` must be an existing directory with `mnt->mounted == NULL`. On success `mnt->mounted = root`, `root->upper` copies the mount-point parent (for `".."`), and the sb is linked into the parent super-block `mount_child` list.

The covered `mnt` stays in the parent's child list. Lookup enters `mounted`. After unmount the name points at the original directory again.

`detach` requires a prior `detach_prepare`. On success it transfers the sb-held `mnt` / `root_dentry` references to the out parameters; the caller must `mr_dentry_put()` both. Child mounts block detach; the path layer reports `-MR_EBUSY` first.

## Detach Prepare and Operation Count

```c
mr_err_t mr_super_block_detach_prepare(mr_super_block_t *sb);
void mr_super_block_detach_cancel(mr_super_block_t *sb);
mr_err_t mr_super_block_op_enter(mr_super_block_t *sb);
void mr_super_block_op_exit(mr_super_block_t *sb);
```

`detach_prepare` checks under the lock: not already detaching, `op_cnt == 0`, no `mount_child`, already attached. Success sets `detaching`.

Path operations call `op_enter` before touching the backing file system. Unmount-in-progress or not-attached fails. `op_exit` decrements the counter in pairs. This keeps the `unmount` callback from overlapping in-flight lookup/unlink.

If `unmount` fails, the path layer calls `detach_cancel()` so the mount stays usable.

## Find and Delete

```c
mr_super_block_t *mr_super_block_find_by_dentry(mr_dentry_t *dentry);
mr_err_t mr_super_block_del(mr_super_block_t *sb);
mr_super_block_t *mr_super_block_get(mr_super_block_t *sb);
mr_bool_t mr_super_block_put(mr_super_block_t *sb);
```

`find_by_dentry()` matches `mnt` or `root_dentry` and returns a referenced sb; `put` it when done.

`mr_super_block_del()` unlinks from the global list, drops the `sb->root` creation reference, then `mr_object_del()`. `kill_sb` runs when the last reference is dropped, not synchronously inside `del` unless that drop *is* the last reference. Release asserts `op_cnt == 0` and that the sb is already detached.

## Relation to the Path Layer

`mr_fs_mount_root()` / `mr_fs_mount()` do: find type → `super_block_create` → resolve the mount-point → `attach`. On failure they `del` the new sb.

Do not `attach`/`detach` from file-system callbacks; that is VFS work. `mount` only fills `root`/`dops`/`priv`.

## Notes

- Super-blocks are dynamically allocated only; there is no `mr_super_block_init()`.
- `op_enter` returns `-MR_ENOENT` if not attached, `-MR_EBUSY` if detaching.
- Child mounts hang off the parent sb, not the dentry child list. Unmount children before the parent.
- The root inode `sb` pointer holds a super-block reference. Inode `destroy` `mr_super_block_put()`s it so the sb cannot vanish before the inode.

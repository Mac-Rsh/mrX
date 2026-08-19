[**中文**](fs_ramfs.md) | [**English**](fs_ramfs_en.md)

---

# RamFS

RamFS is the heap-backed reference file system, type name `"ramfs"`. With `MR_USE_FS_RAMFS` it `mr_file_system_register()`s at `MR_INIT_LEVEL_FS` but does not mount itself. Mount it on an existing directory to get readable and writable regular files.

It is also the template for a third-party file system: a minimal `create` / `truncate` / `destroy` / `read` / `write`. The directory tree is the VFS dentry cache, so RamFS does not implement `lookup`.

## Configuration and Mount

Kconfig: `MR_USE_FS_RAMFS`, depends on `MR_USE_FS`, default off.

```c
mr_fs_mkdir("/ram");
mr_fs_mount(MR_NULL, "/ram", "ramfs", 0, MR_NULL);
```

`source`, `flags`, and `data` are ignored. It can be mounted more than once; each mount gets its own super-block and file contents.

## Node Layout

Each inode `priv` points at:

```c
typedef struct ramfs_node {
    mr_uint8_t *data;
} ramfs_node_t;
```

Directories and regular files both have this structure. Directory `data` stays `MR_NULL`. Regular-file contents are a `realloc`able heap buffer whose length equals `inode->size`.

`mount` allocates a root directory inode (`MR_INODE_DIR`). `sb->dops` is `MR_NULL`, so the default dentry hash is used.

## Implemented Operations

| Callback | Behavior |
|----------|----------|
| `inode.create` | Allocates another `ramfs_node_t` and inode. Directories and regular files both use this. |
| `inode.truncate` | `length == size` succeeds immediately. `length == 0` frees the buffer. Growth `realloc`s and zero-fills the new range. `length < 0` returns `-MR_EINVAL`; larger than `MR_SIZE_MAX` returns `-MR_ENOMEM`. |
| `inode.destroy` | Frees `data` and the `ramfs_node_t`. |
| `file.read` | Copies from `pos`, clipped to `size`. |
| `file.write` | `realloc`s if needed, zero-fills a hole if `pos` is past the old size, writes, then updates `inode->size`. |

`lookup`, `unlink`, `rename`, `open`, `close`, and `readdir` are omitted. VFS defaults handle them: names live only in the dentry tree, and directory enumeration uses `mr_dentry_child_get()`.

So:

- `mkdir` / `open(O_CREAT)` call `create`.
- `unlink` / `rmdir` only update the dentry tree; `destroy` drops the buffer when the last inode reference goes away.
- Open a regular file and `read` / `write` / `truncate`. Directories have no file data; writing a directory is still rejected by the path layer.

## Limits

- Contents live in RAM only and disappear on power loss.
- There is no persistence and no `sync` hook, so `mr_fs_sync()` returns `-MR_ENOSYS`.
- There is no quota; growth fails with `-MR_ENOMEM` when the heap is exhausted.
- There is no symlink callback; symbolic links use the VFS built-in in-memory target.

## As an Implementation Reference

A new file system can follow this order:

1. Define `mr_inode_ops_t` / `mr_file_ops_t`, with at least `create` and `destroy`.
2. Create a root directory inode in `mount` and assign `sb->root`.
3. `MR_FILE_SYSTEM_INIT("name", mount, unmount_or_null, kill_sb_or_null)`.
4. `mr_file_system_register()` from initcall or explicit setup.

If node data lives on media instead of only in the dentry cache, add `lookup`, `unlink`, and `readdir`. RamFS does not need them because every visible name is already instantiated in the tree.

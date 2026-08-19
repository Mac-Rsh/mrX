[**中文**](fs_ramfs.md) | [**English**](fs_ramfs_en.md)

---

# RamFS

RamFS is the heap-backed reference file system, type name `"ramfs"`. With `MR_USE_FS_RAMFS` it `mr_file_system_register()`s at `MR_INIT_LEVEL_FS` but does not mount itself. Mount it on an existing directory to get readable and writable regular files, and optional FIFO nodes.

It is also the template for a third-party file system. The directory tree is the VFS dentry cache, so RamFS does not implement `lookup`.

## Configuration and Mount

Kconfig:

- `MR_USE_FS_RAMFS`, depends on `MR_USE_FS`, default off.
- `MR_USE_FS_RAMFS_FIFO`, depends on ramfs, default on. FIFO creation returns `-MR_ENOSYS` when disabled.
- `MR_CFG_FS_RAMFS_FIFO_SIZE`, default FIFO byte capacity at creation, default 32.

```c
mr_fs_mkdir("/ram");
mr_fs_mount(MR_NULL, "/ram", "ramfs", 0, MR_NULL);
```

`source`, `flags`, and `data` are ignored. It can be mounted more than once; each mount gets its own super-block and file contents. RamFS does not mount itself at `/tmp`. If `/tmp` is needed, the system or user init layer should `mkdir` and `mount`.

## Creation Paths

| Public API | Backend callback | Types accepted by ramfs |
|------------|------------------|-------------------------|
| `mkdir` | `.create(DIR)` | directory |
| `open(O_CREAT)` | `.create(REG)` | regular file |
| `mr_fs_mknod(path, REG/DIR/FIFO, ...)` | no `.mknod`; VFS falls back to `.create` | same as `.create` |
| `mr_fs_mkfifo()` / `mr_fs_mkfifoat()` | same (internally `mknod(FIFO)`) | FIFO |

RamFS does not implement `.mknod`. `mr_fs_mknod()` / `mkfifo` use the VFS fallback, and `priv` is ignored. CHR/BLK/LNK are still rejected. Public `open(O_CREAT)` only ever passes `REG`.

Prefer `mr_fs_mkfifo()` to create a FIFO:

```c
mr_fs_mkfifo("/ram/events");
mr_fs_open(&file, "/ram/events", MR_O_RDWR);
capacity = mr_fs_fcntl(&file, MR_F_GETPIPE_SZ, 0);
capacity = mr_fs_fcntl(&file, MR_F_SETPIPE_SZ, 1000);
```

## Node Layout

Each inode `priv` points at a union interpreted by inode type:

```c
typedef struct ramfs_node {
    union {
        struct {
            mr_uint8_t *data;
        } regular;
        struct {
            mr_size_t capacity;
            mr_uint8_t *data;
            mr_size_t head;
            mr_size_t len;
        } fifo;
    } data;
} ramfs_node_t;
```

- Directories: no `ramfs_node_t`, and no data file operations.
- Regular files: a `realloc`able heap buffer whose length equals `inode->size`.
- FIFOs: a fixed-capacity ring buffer. `inode->size` stays 0; query capacity with `MR_F_GETPIPE_SZ`.

Regular files and FIFOs use different file-operation tables. `mount` allocates a root directory inode (`MR_INODE_DIR`). `sb->dops` is `MR_NULL`.

## Implemented Operations

| Callback | Behavior |
|----------|----------|
| `inode.create` | Accepts DIR/REG/FIFO and uses the shared node constructor. |
| `inode.truncate` | Regular files only: `length == size` succeeds immediately. `length == 0` frees the buffer. Growth `realloc`s and zero-fills the new range. |
| `inode.destroy` | Frees the type-specific buffer and the `ramfs_node_t`. |
| Regular `file.read` / `file.write` | Read and write the growable buffer from `pos`. |
| FIFO `file.read` / `file.write` | Shared non-blocking byte queue. Empty reads or full writes return `-MR_EAGAIN`. Partial I/O is allowed. `file->pos` is not updated. |
| FIFO `file.llseek` | Returns `-MR_ESPIPE`. |
| FIFO `file.fcntl` | `MR_F_GETPIPE_SZ` / `MR_F_SETPIPE_SZ`. |

`lookup`, `unlink`, `rename`, `open`, `close`, and `readdir` are omitted. VFS defaults handle them.

## FIFO Semantics

The first version is a shared non-blocking byte FIFO, not a full POSIX named pipe:

- Creation does not take a capacity; it uses `MR_CFG_FS_RAMFS_FIFO_SIZE`. Change it later with fcntl.
- Capacity may be any positive integer. It does not need to be a power of two and is not rounded to a page size.
- `SETPIPE_SZ` returns `-MR_EBUSY` if the requested size is smaller than the current unread length. A successful resize keeps unread bytes in order.
- There is no open handshake, blocking wait, reader/writer count, EOF, or `SIGPIPE`.
- Generic `mr_fs_truncate()` rejects FIFOs (`-MR_EINVAL`).
- fcntl command numbers are the mrX ABI, not host Linux `F_*` values.

## Limits

- Contents live in RAM only and disappear on power loss.
- There is no persistence and no `sync` hook, so `mr_fs_sync()` returns `-MR_ENOSYS`.
- There is no quota; growth fails with `-MR_ENOMEM` when the heap is exhausted.
- There is no symlink callback; symbolic links use the VFS built-in in-memory target.
- There is no thread-safety guarantee; the caller must serialize concurrent access to the same node.
- `/tmp` is not mounted automatically.

## As an Implementation Reference

A new file system can follow this order:

1. Define `mr_inode_ops_t` / `mr_file_ops_t`, with at least `create` and `destroy`.
2. Create a root directory inode in `mount` and assign `sb->root`.
3. `MR_FILE_SYSTEM_INIT("name", mount, unmount_or_null, kill_sb_or_null)`.
4. `mr_file_system_register()` from initcall or explicit setup.

If node data lives on media instead of only in the dentry cache, add `lookup`, `unlink`, and `readdir`. RamFS does not need them because every visible name is already instantiated in the tree.

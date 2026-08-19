[**中文**](fs_file.md) | [**English**](fs_file_en.md)

---

# File Interface

The File module provides a handle-based API on the path namespace: mount, open, read/write, directory iteration, and create, remove, rename, and link operations. Applications use these interfaces through `<mrX.h>` or `<mrX/fs/mr_file.h>`. The module is available only when `MR_USE_FS` is enabled. See the [file-system overview](overview_en.md) for layers and implementation modules.

Related pages: [Dentry](dentry_en.md), [Inode](inode_en.md), [File-system](filesystem_en.md), [Super-block](superblock_en.md), [Namespace](nsp_en.md), [RamFS](ramfs_en.md), [RootFS](rootfs_en.md).

This document describes the file handle, open flags, path resolution, and the application-facing interfaces. File-system registration, dentry, inode, and super-block internals are covered in their own module documents.

## Configuration

| Option | Default | Role |
|--------|---------|------|
| `MR_USE_FS` | `n` | Enable the file system. |
| `MR_USE_FS_BY_PATH` | `y` | Provide the `mr_fs_*_by_path()` convenience interfaces. |
| `MR_USE_FS_AT` | `y` | Provide the `mr_fs_*at()` directory-relative interfaces and `MR_AT_*` flags. |
| `MR_USE_FS_MULTIUSER` | `n` | Give each caller its own root/cwd namespace. The platform adapter must provide `mr_fs_nsp_current()`. |
| `MR_CFG_FS_PATH_LEN` | `260` | Maximum absolute path length, and the recommended buffer size for `mr_fs_getcwd()`. |
| `MR_CFG_FS_SYMLINK_MAX` | `16` | Maximum number of symbolic links followed in one lookup. Exceeding it returns `-MR_ELOOP`. |

When rootfs is enabled, startup registers it and calls `mr_fs_mount_root()` to install the global ROOT. After that, use `mr_fs_mount()` to attach other file systems to directories that already exist.

## Core Concepts

### File Handle

```c
typedef struct mr_file {
    mr_dentry_t *dentry;
    mr_inode_t *inode;
    const mr_file_ops_t *fops;
    mr_int64_t pos;
    int flags;
    void *priv;
} mr_file_t;
```

`mr_file_t` is allocated by the caller, usually on the stack or in static storage. It is not a kernel object and does not use `mr_object_get()` / `mr_object_put()`. A non-null `inode` means the handle is open; `mr_file_is_open()` uses that field.

Initialize the handle before its first use:

```c
mr_file_t file = MR_FILE_INIT(file);
/* or */
mr_file_init(&file);
/* or all-zero storage */
```

A closed handle can be opened again without reinitializing it. Do not call `mr_file_init()` on an open handle; that bypasses `close` and leaks the dentry/inode references.

### Path Resolution

Relative paths are resolved from the current working directory. Absolute paths start at the current namespace ROOT. Lookup follows intermediate symbolic links; whether the final component is followed depends on the open flags.

`mr_fs_open()` follows the final symbolic link by default. With `MR_O_NOFOLLOW`, it opens the link itself so `mr_fs_readlink()` can read the target.

### Open Flags

```c
#define MR_O_RDONLY     (0x0000)
#define MR_O_WRONLY     (0x0001)
#define MR_O_RDWR       (0x0002)
#define MR_O_ACCMODE    (0x0003)
#define MR_O_CREAT      (0x0100)
#define MR_O_EXCL       (0x0200)
#define MR_O_TRUNC      (0x0400)
#define MR_O_APPEND     (0x0800)
#define MR_O_DIRECTORY  (0x1000)
#define MR_O_NOFOLLOW   (0x2000)
```

| Flag | Meaning |
|------|---------|
| `MR_O_RDONLY` / `WRONLY` / `RDWR` | Access mode. Exactly one of these three. |
| `MR_O_CREAT` | Create a regular file if the node does not exist. Directories are created with `mr_fs_mkdir()`, not this flag. |
| `MR_O_EXCL` | With `MR_O_CREAT`, return `-MR_EEXIST` if the node already exists. |
| `MR_O_TRUNC` | Truncate a writable regular file to length 0 after a successful open. A failed open does not discard existing data. |
| `MR_O_APPEND` | Move to the current end-of-file before every write. A failed or zero-length write does not change the previous position. |
| `MR_O_DIRECTORY` | The node must be a directory, otherwise `-MR_ENOTDIR`. |
| `MR_O_NOFOLLOW` | Do not follow the final symbolic link. |

A directory can be opened read-only only; otherwise the call returns `-MR_EISDIR`. Opening an already-open handle returns `-MR_EBUSY`.

When `MR_USE_FS_AT` is enabled, path-resolution flags are also available:

```c
#define MR_AT_IN_ROOT            (0x4000)
#define MR_AT_SYMLINK_NOFOLLOW   (0x8000)
```

`MR_AT_IN_ROOT` treats the directory handle as a temporary ROOT: parent components cannot escape that directory, and absolute paths and symbolic links restart there. `mr_fs_open()` must not carry `MR_AT_IN_ROOT`. `mr_fs_openat()` takes `MR_O_*` and `MR_AT_IN_ROOT` in the same `flags` argument.

## Module Interfaces

### Mount

```c
mr_err_t mr_fs_mount_root(const char *source, const char *fstype, int flags,
                          void *data);
mr_err_t mr_fs_mount(const char *source, const char *path, const char *fstype,
                     int flags, void *data);
mr_err_t mr_fs_umount(const char *path);
```

`mr_fs_mount_root()` installs or replaces the global FS ROOT and does not access the current namespace. `fstype` must already be registered with `mr_file_system_register()`. `flags` is currently reserved; pass `0`.

After a namespace is live, use `mr_fs_mount()` to attach a file system at a path. A non-root mount-point must already exist and be a directory. The path is resolved in the current namespace.

`mr_fs_umount()` is not recursive. A mount that still has child mounts returns `-MR_EBUSY`; unmount the children first.

### Open and Close

```c
mr_err_t mr_fs_open(mr_file_t *file, const char *path, int flags);
mr_err_t mr_fs_close(mr_file_t *file);
mr_bool_t mr_file_is_open(const mr_file_t *file);
```

`mr_fs_close()` invalidates the handle regardless of the return value. A non-zero return only reports a flush or finalize failure and is not retryable. Closing an already-closed handle returns `-MR_EBADF`.

### Create and Remove

```c
mr_err_t mr_fs_mkdir(const char *path);
mr_err_t mr_fs_rmdir(const char *path);
mr_err_t mr_fs_mknod(const char *path, mr_inode_type_t type, void *priv);
mr_err_t mr_fs_unlink(mr_file_t *file);
mr_err_t mr_fs_unlink_recursive(mr_file_t *file);
```

`mr_fs_mkdir()` creates a single directory level. The parent must already exist, otherwise `-MR_ENOENT`. An existing leaf returns `-MR_EEXIST`.

`mr_fs_rmdir()` removes an empty directory and does not follow the final symbolic link. A non-directory returns `-MR_ENOTDIR`, a non-empty directory returns `-MR_ENOTEMPTY`, and a mount-point returns `-MR_EBUSY`.

`mr_fs_unlink()` removes the current name of an open handle. The handle remains open after the name is removed. Non-empty directories and mount-points have the same restrictions as `rmdir`. A directory can also be removed with `unlink`; `rmdir` additionally requires the target to be a directory.

`mr_fs_unlink_recursive()` recursively removes an open file or directory. Symbolic links are removed without following their target. The operation is not atomic: entries removed before a later failure stay removed.

### I/O and Position

```c
mr_ssize_t mr_fs_read(mr_file_t *file, void *buf, mr_size_t size);
mr_ssize_t mr_fs_write(mr_file_t *file, const void *buf, mr_size_t size);
mr_int64_t mr_fs_seek(mr_file_t *file, mr_int64_t offset, int whence);
mr_int64_t mr_fs_tell(mr_file_t *file);
mr_err_t mr_fs_truncate(mr_file_t *file, mr_int64_t length);
mr_err_t mr_fs_sync(mr_file_t *file);
mr_err_t mr_fs_ioctl(mr_file_t *file, int cmd, void *arg);
```

`read` / `write` return the byte count on success. `read` returns `0` at EOF, and a negative error code on failure.

`whence` is `MR_SEEK_SET`, `MR_SEEK_CUR`, or `MR_SEEK_END`. The default implementation clamps a negative result to `0`. If the result cannot be represented by `mr_int64_t`, the call returns `-MR_ERANGE` without changing the position.

`mr_fs_truncate()` requires a writable handle. Directories return `-MR_EISDIR`; other non-regular files return `-MR_EINVAL`. `mr_fs_sync()` returns `-MR_ENOSYS` when the file system provides no sync hook.

### Directory Iteration

```c
mr_err_t mr_fs_opendir(mr_file_t *file, const char *path);
mr_err_t mr_fs_readdir(mr_file_t *file, mr_dirent_t *dirent);
mr_err_t mr_fs_closedir(mr_file_t *file);
```

`mr_fs_opendir()` is equivalent to `mr_fs_open(file, path, MR_O_DIRECTORY | MR_O_RDONLY)`. `mr_fs_closedir()` has the same close contract as `mr_fs_close()`.

`mr_fs_readdir()` does not use the usual “0 means success” convention:

| Return | Meaning |
|--------|---------|
| `1` | An entry was read into `dirent`. |
| `0` | End of directory. |
| Negative | Error code. |

Iterate with `while ((ret = mr_fs_readdir(file, &dirent)) > 0)`.

### Status, Links, and Rename

```c
mr_err_t mr_fs_stat(const mr_file_t *file, mr_stat_t *stat);
mr_err_t mr_fs_link(const mr_file_t *file, const char *new_path);
mr_err_t mr_fs_symlink(const char *target, const char *link_path);
mr_ssize_t mr_fs_readlink(const mr_file_t *file, char *buf, mr_size_t size);
mr_err_t mr_fs_rename(mr_file_t *file, const char *new_path);
```

`mr_fs_stat()` remains available after the file has been unlinked. A file-system stat hook may override the generic fields.

`mr_fs_link()` rejects directory hard links (`-MR_EPERM`) and cross-file-system links (`-MR_EXDEV`). `mr_fs_rename()` returns `-MR_EEXIST` if the destination exists, `-MR_EXDEV` across file systems, and `-MR_EBUSY` for a mount-point directory.

`mr_fs_readlink()` requires the symbolic link to be opened with `MR_O_NOFOLLOW`. The return value is the target length and does not include a terminator. If the buffer is larger than the target, a null terminator is written after it; otherwise the target is not null-terminated.

### By-Path Interfaces

When `MR_USE_FS_BY_PATH` is enabled:

```c
mr_err_t mr_fs_stat_by_path(const char *path, mr_stat_t *stat);
mr_err_t mr_fs_rename_by_path(const char *old_path, const char *new_path);
mr_err_t mr_fs_unlink_by_path(const char *path);
mr_err_t mr_fs_unlink_recursive_by_path(const char *path);
```

These interfaces look up the path internally, so the caller does not need to open a handle first. `mr_fs_stat_by_path()` follows the final symbolic link. When the option is disabled, the functions are not present; open a handle and use the handle APIs instead.

### Directory-Relative Interfaces

When `MR_USE_FS_AT` is enabled:

```c
mr_err_t mr_fs_mkdirat(const mr_file_t *dir, const char *path, int flags);
mr_err_t mr_fs_rmdirat(const mr_file_t *dir, const char *path, int flags);
mr_err_t mr_fs_mknodat(const mr_file_t *dir, const char *path,
                       mr_inode_type_t type, void *priv, int flags);
mr_err_t mr_fs_openat(mr_file_t *file, const mr_file_t *dir, const char *path,
                      int flags);
```

A relative path requires `dir` to be an open directory handle. An absolute path starts at the current FS ROOT and ignores `dir`, unless `MR_AT_IN_ROOT` is specified.

When both `MR_USE_FS_AT` and `MR_USE_FS_BY_PATH` are enabled:

```c
mr_err_t mr_fs_statat_by_path(const mr_file_t *dir, const char *path,
                              mr_stat_t *stat, int flags);
mr_err_t mr_fs_renameat_by_path(const mr_file_t *old_dir, const char *old_path,
                                const mr_file_t *new_dir, const char *new_path,
                                int flags);
mr_err_t mr_fs_unlinkat_by_path(const mr_file_t *dir, const char *path,
                                int flags);
mr_err_t mr_fs_unlinkat_recursive_by_path(const mr_file_t *dir,
                                          const char *path, int flags);
```

`mr_fs_statat_by_path()` with `MR_AT_SYMLINK_NOFOLLOW` returns the status of the final symbolic link itself.

### Working Directory and chroot

```c
mr_err_t mr_fs_chroot(const char *path);
mr_err_t mr_fs_chdir(const char *path);
mr_ssize_t mr_fs_getcwd(char *buf, mr_size_t size);
```

`mr_fs_chroot()` resolves the new ROOT relative to the current context. The current working directory is kept when it is contained by the new ROOT; otherwise it is reset to the new ROOT so cwd-relative paths cannot escape. Pre-opened directory handles are unaffected.

`mr_fs_getcwd()` returns the path length on success, excluding `\0`, and always writes a terminator. When the working directory is ROOT, it writes `"/"` and returns `1`. A buffer that is too small returns `-MR_ERANGE`. The recommended buffer size is `MR_CFG_FS_PATH_LEN`.

## Usage Examples

### Open, Read, Write, and Close

```c
#include <mrX.h>

void sample_file_rw(void)
{
    mr_file_t file = MR_FILE_INIT(file);
    char buf[16];
    mr_ssize_t n;

    if (mr_fs_open(&file, "/note", MR_O_RDWR | MR_O_CREAT) != 0) {
        return;
    }

    mr_fs_write(&file, "hello", 5);
    mr_fs_seek(&file, 0, MR_SEEK_SET);
    n = mr_fs_read(&file, buf, sizeof(buf));
    (void)n;

    mr_fs_close(&file);
}
```

### Create and Remove a Directory

```c
void sample_dir_create_remove(void)
{
    if (mr_fs_mkdir("/data") != 0) {
        return;
    }

    mr_fs_rmdir("/data");
}
```

### Iterate a Directory

```c
void sample_listdir(void)
{
    mr_file_t dir = MR_FILE_INIT(dir);
    mr_dirent_t ent;
    mr_err_t ret;

    if (mr_fs_opendir(&dir, "/") != 0) {
        return;
    }

    while ((ret = mr_fs_readdir(&dir, &ent)) > 0) {
        /* Use ent.name and ent.type. */
    }

    mr_fs_closedir(&dir);
}
```

### Unlink by Path

```c
void sample_unlink_by_path(void)
{
#if defined(MR_USE_FS_BY_PATH)
    mr_fs_unlink_by_path("/note");
#else
    mr_file_t file = MR_FILE_INIT(file);

    if (mr_fs_open(&file, "/note", MR_O_RDONLY) == 0) {
        mr_fs_unlink(&file);
        mr_fs_close(&file);
    }
#endif
}
```

## Notes

- Management APIs return `0` on success and a negative error code on failure. `read` / `write` / `seek` / `tell` / `getcwd` / `readlink` return a non-negative length or position on success.
- `0` from `mr_fs_readdir()` means end-of-directory, not success.
- After `mr_fs_close()` fails, the handle is already invalid; do not close it again.
- Do not call `mr_file_init()` on an open handle.
- `mkdir` does not create missing parents; `rmdir` does not remove a non-empty directory.
- Recursive unlink is not atomic.
- Mount `flags` are currently reserved; pass `0`.
- A multi-user namespace is provided by the platform adapter. Application code does not include or initialize `mr_nsp.h` directly.

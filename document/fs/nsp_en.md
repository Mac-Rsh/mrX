[**中文**](fs_nsp.md) | [**English**](fs_nsp_en.md)

---

# Namespace

The namespace stores where path resolution starts: ROOT and cwd, both dentries. There is also one static global ROOT dentry (name `"/"`). Every namespace initially points at it.

In single-user mode the FS provides one global `mr_fs_nsp_t` and `mr_fs_nsp_init()`s it from initcall. In multi-user mode the platform adapter implements this API; it is not an application header.

The header is `<mrX/fs/mr_nsp.h>`. It is not aggregated by `<mrX.h>`.

## Structure

```c
typedef struct mr_fs_nsp {
    struct mr_dentry *root;
    struct mr_dentry *cwd;
} mr_fs_nsp_t;
```

Both `root` and `cwd` hold dentry references. `mr_fs_chroot()` / `mr_fs_chdir()` replace them and `put` the old references.

## Interfaces

```c
struct mr_dentry *mr_fs_nsp_root(void);
mr_err_t mr_fs_nsp_init(mr_fs_nsp_t *nsp);
mr_fs_nsp_t *mr_fs_nsp_current(void);
mr_bool_t mr_fs_nsp_is_root(const struct mr_dentry *dentry);
int mr_fs_nsp_lock(void);
void mr_fs_nsp_unlock(int mask);
```

`mr_fs_nsp_root()` returns the global static ROOT without taking a reference. It exists before any namespace is initialized and is the target of `mr_fs_mount_root()`.

`mr_fs_nsp_init()` `get`s both `nsp->root` and `nsp->cwd` to the global ROOT. On failure both fields are null.

`mr_fs_nsp_current()`:

- Without `MR_USE_FS_MULTIUSER`: returns the FS-owned global namespace.
- With it enabled: the platform must provide it and return the namespace owned by the **current task**. `MR_NULL` makes path APIs return `-MR_EINVAL`.

`mr_fs_nsp_is_root()` tests identity with the global ROOT object, not “the current namespace root”. After `chroot` the current root may be a different dentry.

## Lock

`mr_fs_nsp_lock()` disables interrupts and takes the global namespace spinlock. The return value must be passed to the matching `mr_fs_nsp_unlock()`. While holding it, do not:

- Block
- Call file-system `lookup` / `create` / `unlink` callbacks
- Call path APIs that take the same lock

The dentry tree, mount topology, file-system registry, and super-block list all use this lock. The path-layer pattern is: walk the cache under the lock, call the file system without it, then commit under the lock.

## Multi-user Adapter

When `MR_USE_FS_MULTIUSER` is on, the platform must:

1. Store one `mr_fs_nsp_t` per task.
2. Implement `mr_fs_nsp_current()` for the current task.
3. Call `mr_fs_nsp_init()` before the task uses the file system.
4. `put` `root` and `cwd` when the task exits if they are still held.

`mr_fs_file_init()` does not initialize any task namespace in multi-user mode. The FS still owns the global ROOT dentry, and `mr_fs_mount_root()` still does not read the current namespace.

## chroot and cwd

`mr_fs_chdir()` replaces `nsp->cwd` with the looked-up directory. `mr_fs_chroot()` replaces root; if the old cwd is not under the new root, cwd becomes the new root so relative paths cannot escape. Pre-opened directory handles do not follow the change.

`mr_fs_getcwd()` walks from `cwd` to `nsp->root` (not the global ROOT) and builds the absolute path inside the current namespace.

## Notes

- Application code uses `mr_fs_chdir()` / `chroot()` / `getcwd()`. Do not write `nsp` fields directly.
- There is one global lock, not one lock per `mr_fs_nsp_t`.
- The global ROOT’s creation reference comes from static initialization. Do not `put` the pointer from `mr_fs_nsp_root()` unless you `get` it yourself.

[**中文**](fs_overview.md) | [**English**](fs_overview_en.md)

---

# File-System Overview

The mrX file system splits the path namespace, directory-entry cache, inodes, and mount instances into separate layers. Applications mostly use the [file interface](file_en.md). Implementing a file system or understanding mounts also requires dentry, inode, super-block, and file-system type.

The modules are compiled only when `MR_USE_FS` is enabled. After that, `<mrX.h>` includes the dentry, inode, file, fs, and sb headers. The namespace API lives in `<mrX/fs/mr_nsp.h>` and is used by the kernel and platform adapters.

## Layers

```text
application / tests
    mr_fs_open / read / mkdir / mount ...     file
        current root / cwd                     nsp
        component lookup, mount crossing       dentry
        node type, ops, file data              inode
        one mounted instance                   super-block
            file-system type (mount/unmount/kill_sb)
```

| Module | Header | Role |
|--------|--------|------|
| [File](file_en.md) | `mr_file.h` | Application handle API: open, I/O, directories, mount entry points. |
| [Dentry](dentry_en.md) | `mr_dentry.h` | Path-name tree, child lists, mount overlay, lookup cache. |
| [Inode](inode_en.md) | `mr_inode.h` | Node object, inode/file operation tables, `destroy`. |
| [File-system](filesystem_en.md) | `mr_fs.h` | File-system types registered by name. |
| [Super-block](superblock_en.md) | `mr_sb.h` | One mount instance, mount topology, operation count. |
| [Namespace](nsp_en.md) | `mr_nsp.h` | Global ROOT, current root/cwd, namespace lock. |
| [RamFS](ramfs_en.md) | `fs/ramfs/ramfs.c` | Heap-backed regular-file reference implementation. |
| [RootFS](rootfs_en.md) | `fs/rootfs/rootfs.c` | Namespace-only root file system with no regular-file data. |

A dentry stores where a name sits in the tree. An inode stores what the node is and how to operate on it. One inode may be referenced by several dentries (hard links). One `mr_fs_mount()` creates a super-block and covers the mount-point with that file system's root dentry.

## Objects and Handles

Dentry, inode, and super-block embed `mr_object_t` and use reference counts. The class release function runs when the last reference is dropped.

`mr_file_t` is not a kernel object. The caller allocates it. Open pins extra dentry and inode references; close drops them. A failed close still invalidates the handle.

## Boot Sequence

1. Enable `MR_USE_FS`. In single-user mode, `mr_fs_file_init()` initializes the global namespace through initcall so both root and cwd point at the global ROOT dentry (name `"/"`).
2. With `MR_USE_FS_ROOTFS`, rootfs registers at `MR_INIT_LEVEL_FS` and calls `mr_fs_mount_root("rootfs")` so the global ROOT becomes a rootfs directory tree.
3. With `MR_USE_FS_RAMFS`, ramfs only registers its type. Mount it later with `mr_fs_mount(..., "ramfs", ...)` onto an existing directory.
4. A custom file system also calls `mr_file_system_register()` first, then `mr_fs_mount()` / `mr_fs_mount_root()`.

In multi-user mode the FS still owns the global ROOT, but each task's `mr_fs_nsp_t` is allocated by the platform adapter, which must call `mr_fs_nsp_init()`.

## Path of One Operation

For `mr_fs_open(&file, "a/b", flags)`:

1. Take cwd (relative) or root (absolute) from the current namespace.
2. `mr_dentry_lookup()` walks cached components; on a miss it calls the parent inode `lookup` and instantiates the result.
3. A mount-point is entered through `dentry->mounted`, then lookup continues inside the mounted file system.
4. Creation uses the parent `create` / `mknod` callback, then binds the new inode to a new dentry.
5. `fs_file_open()` pins the dentry and inode, fills `mr_file_t`, and may call `fops->open` and `truncate`.

Unmount is the reverse: `detach_prepare` (no in-flight ops, no child mounts), notify `unmount`, `detach` and hand references back to the caller, then `mr_super_block_del()`. `kill_sb` runs when the last super-block reference is dropped.

## Locking

The namespace spinlock (`mr_fs_nsp_lock()`) protects the dentry tree, mount topology, and file-system registry. The locked section must not block or call file-system implementation callbacks. Inode callbacks such as `lookup` / `create` / `unlink` run without the lock; the tree is committed afterwards under the lock.

## Configuration

The file-system master switch and path/symlink limits live in `include/mrX/fs/Kconfig`. Ramfs and rootfs have their own submenus. Application path convenience APIs are trimmed by `MR_USE_FS_BY_PATH` and `MR_USE_FS_AT`; see the [file interface](file_en.md).

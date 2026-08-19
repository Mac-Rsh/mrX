[**中文**](fs_rootfs.md) | [**English**](fs_rootfs_en.md)

---

# RootFS

RootFS is a namespace-only root file system with no regular-file data, type name `"rootfs"`. With `MR_USE_FS_ROOTFS` it registers at `MR_INIT_LEVEL_FS` and immediately calls `mr_fs_mount_root(MR_NULL, "rootfs", 0, MR_NULL)`, turning the global ROOT into a directory tree that supports `mkdir` / `mknod`.

It is meant as a mount-point skeleton: create `/dev` or `/mnt`, then mount ramfs or another file system there. Regular-file I/O is not available on rootfs itself.

## Configuration

Kconfig: `MR_USE_FS_ROOTFS`, depends on `MR_USE_FS`, default off.

After a successful boot the global ROOT is already rootfs. If ROOT is already taken, `mr_fs_mount_root()` returns busy; the init code treats both `0` and `MR_EBUSY` as acceptable.

`"rootfs"` can also be mounted on a subdirectory to get another data-less tree.

## Capabilities

| Operation | Supported |
|-----------|-----------|
| `mkdir` | Yes. `create` accepts only `MR_INODE_DIR`; other types return `-MR_ENOSYS`. |
| `mknod` | Yes. The caller `priv` is **borrowed** onto the inode. RootFS neither copies nor frees it. |
| Directory enumeration | Yes, via the VFS default `readdir` (dentry children). |
| `read`/`write`/`truncate` after opening a regular file | No. There are no `fops`. |
| Symlink payload | No. VFS built-in symlinks can still be used. |
| `lookup` | No. Visible names live in the dentry cache. |

The root inode and child directory inodes share one `inode_ops` table: `create` + `mknod`. `fops` is `MR_NULL`. `sb->dops` is `MR_NULL`.

## mknod `priv`

`rootfs_inode_mknod()` stores `priv` in `inode->priv`. The comment states the pointer is borrowed for the inode lifetime. RootFS `destroy` is the default no-op and **does not** `free(priv)`. The caller must ensure:

- `priv` outlives the inode.
- If `priv` still needs to be released after the node is removed, the caller does that.

This is for hanging a device object or driver pointer on a namespace node, not for storing file bytes.

## Together with RamFS

A typical boot combination:

1. Enable `MR_USE_FS` and `MR_USE_FS_ROOTFS` to get `/`.
2. `mr_fs_mkdir("/tmp")`.
3. Enable `MR_USE_FS_RAMFS` and `mr_fs_mount(MR_NULL, "/tmp", "ramfs", 0, MR_NULL)`.
4. Regular files under `/tmp` use ramfs heap buffers. Other directories under `/` remain data-less rootfs nodes.

If only ramfs is enabled, call `mr_fs_mount_root(..., "ramfs", ...)` yourself. Otherwise the global ROOT has no backing directory inode and path creation fails.

## Notes

- RootFS is not a full POSIX root file system. It is a namespace container.
- Do not expect `read`/`write` on regular-file nodes created in rootfs.
- Ownership of `mknod` `priv` is not transferred to RootFS.

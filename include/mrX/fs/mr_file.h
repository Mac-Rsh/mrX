/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_FS_FILE_H__
#define __MR_FS_FILE_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_FS)
#include <mrX/kernel/mr_initcall.h>
#include <mrX/fs/mr_sb.h>
#endif /* defined(MR_USE_FS) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup File
 * @{
 */

#if defined(MR_USE_FS)
/* File open flags */
#define MR_O_RDONLY                     (0x0000)
#define MR_O_WRONLY                     (0x0001)
#define MR_O_RDWR                       (0x0002)
#define MR_O_ACCMODE                    (0x0003)
#define MR_O_CREAT                      (0x0100)
#define MR_O_EXCL                       (0x0200)
#define MR_O_TRUNC                      (0x0400)
#define MR_O_APPEND                     (0x0800)
#define MR_O_DIRECTORY                  (0x1000)
#define MR_O_NOFOLLOW                   (0x2000)
#define MR_O_IN_ROOT                    (0x4000)

/* Directory-relative path flags */
#define MR_AT_IN_ROOT                   (0x0001)
#define MR_AT_SYMLINK_NOFOLLOW          (0x0002)

/* File seek whence */
#define MR_SEEK_SET                     (0)
#define MR_SEEK_CUR                     (1)
#define MR_SEEK_END                     (2)

/* File type */
typedef struct mr_file {
    mr_dentry_t *dentry;
    mr_inode_t *inode;
    const mr_file_ops_t *fops;
    mr_int64_t pos;
    int flags;
    void *priv;
} mr_file_t;

/**
 * @brief This macro function initializes a file.
 *
 * @param _file The file to initialize.
 */
#define MR_FILE_INIT(_file)                                                    \
    {                                                                          \
        .dentry = MR_NULL,                                                     \
        .inode = MR_NULL,                                                      \
        .fops = MR_NULL,                                                       \
        .pos = 0,                                                              \
        .flags = 0,                                                            \
        .priv = MR_NULL,                                                       \
    }

/**
 * @brief This function initializes a file.
 *
 * @param file The file to initialize.
 *
 * @note Call this function only before the file's first use or after it has
 * been closed. Initializing an open file bypasses its close operation.
 */
MR_INLINE void mr_file_init(mr_file_t *file) {
    MR_ASSERT(file != MR_NULL);

    /* Init the file */
    file->dentry = MR_NULL;
    file->inode = MR_NULL;
    file->fops = MR_NULL;
    file->pos = 0;
    file->flags = 0;
    file->priv = MR_NULL;
}

/**
 * @brief This function mounts a file-system at a path.
 *
 * @param source The mount source.
 * @param path The mount-point path.
 * @param fstype The file-system name(must be registered).
 * @param flags The mount flags.
 * @param data The mount private data(passed to the fs mount callback).
 * @return 0 on success, a negative error code on failure.
 *
 * @note A non-root mount-point must already exist and be a directory.
 */
mr_err_t mr_fs_mount(const char *source, const char *path, const char *fstype,
                     int flags, void *data);

/**
 * @brief This function unmounts the file-system at a path.
 *
 * @param path The mount-point path.
 * @return 0 on success, a negative error code on failure.
 *
 * @note A mount containing child mounts is not recursively unmounted and
 * returns -MR_EBUSY. Unmount child mounts first.
 */
mr_err_t mr_fs_umount(const char *path);

/**
 * @brief This function creates a directory at the given path.
 *
 * @param path The directory path.
 * @return 0 on success, a negative error code on failure.
 *
 * @note The parent must already exist(-MR_ENOENT).
 * @note An existing leaf is rejected with -MR_EEXIST.
 */
mr_err_t mr_fs_mkdir(const char *path);

/**
 * @brief This function creates a directory relative to a directory handle.
 *
 * @param dir The base directory for path resolution. It is ignored for an
 * absolute path unless MR_AT_IN_ROOT is specified and may be MR_NULL otherwise.
 * @param path The directory path.
 * @param flags The path resolution flags(see MR_AT_*).
 * @return 0 on success, a negative error code on failure.
 *
 * @note For a relative path, dir must be an open directory handle.
 * @note MR_AT_IN_ROOT treats dir as a temporary FS root.
 */
mr_err_t mr_fs_mkdirat(const mr_file_t *dir, const char *path, int flags);

/**
 * @brief This function creates a file-system-specific node.
 *
 * @param path The node path.
 * @param type The inode type.
 * @param priv The private data passed to the parent mknod callback.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_fs_mknod(const char *path, mr_inode_type_t type, void *priv);

/**
 * @brief This function creates a file-system-specific node relative to a
 * directory handle.
 *
 * @param dir The base directory for path resolution. It is ignored for an
 * absolute path unless MR_AT_IN_ROOT is specified and may be MR_NULL otherwise.
 * @param path The node path.
 * @param type The inode type.
 * @param priv The private data passed to the parent mknod callback.
 * @param flags The path resolution flags(see MR_AT_*).
 * @return 0 on success, a negative error code on failure.
 *
 * @note For a relative path, dir must be an open directory handle.
 * @note MR_AT_IN_ROOT treats dir as a temporary FS root.
 */
mr_err_t mr_fs_mknodat(const mr_file_t *dir, const char *path,
                       mr_inode_type_t type, void *priv, int flags);

/**
 * @brief This function opens a file by path.
 *
 * @param file The file.
 * @param path The file path.
 * @param flags The open flags(see MR_O_*).
 * @return 0 on success, a negative error code on failure.
 *
 * @note The file must be initialized with MR_FILE_INIT(file), mr_file_init(),
 * or all-zero storage before its first use. A closed file can be opened again
 * without reinitializing it.
 */
mr_err_t mr_fs_open(mr_file_t *file, const char *path, int flags);

/**
 * @brief This function opens a file relative to a directory handle.
 *
 * @param file The file to open.
 * @param dir The base directory for path resolution. It is ignored for an
 * absolute path unless MR_O_IN_ROOT is specified and may be MR_NULL otherwise.
 * @param path The file path.
 * @param flags The open flags(see MR_O_*).
 * @return 0 on success, a negative error code on failure.
 *
 * @note For a relative path, dir must be an open directory handle.
 * @note An absolute path starts at the current FS root and ignores dir.
 * @note MR_O_IN_ROOT treats dir as a temporary FS root. Parent components can
 * not escape above dir, and absolute paths and symbolic links restart at dir.
 * @note The file must be initialized with MR_FILE_INIT(file), mr_file_init(),
 * or all-zero storage before its first use.
 */
mr_err_t mr_fs_openat(mr_file_t *file, const mr_file_t *dir, const char *path,
                      int flags);

/**
 * @brief This function closes a file.
 *
 * @param file The file.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_fs_close(mr_file_t *file);

/**
 * @brief This function opens a directory for reading its entries.
 *
 * @param file The file.
 * @param path The directory path.
 * @return 0 on success, a negative error code on failure.
 *
 * @note The node must be a directory(-MR_ENOTDIR).
 */
mr_err_t mr_fs_opendir(mr_file_t *file, const char *path);

/**
 * @brief This function reads the next directory entry.
 *
 * @param file The directory(must be open).
 * @param dirent The directory entry to fill.
 * @return 1 if an entry was read, 0 at end-of-directory, or a negative error
 * code on failure.
 */
mr_err_t mr_fs_readdir(mr_file_t *file, mr_dirent_t *dirent);

/**
 * @brief This function closes a directory handle.
 *
 * @param file The file.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_fs_closedir(mr_file_t *file);

/**
 * @brief This function gets the status of an open file.
 *
 * @param file The file(must be open).
 * @param stat The file status to fill.
 * @return 0 on success, a negative error code on failure.
 *
 * @note The status remains available after the file has been unlinked.
 * @note A file-system stat hook may override the generic fields.
 */
mr_err_t mr_fs_stat(const mr_file_t *file, mr_stat_t *stat);

/**
 * @brief This function gets file status by path.
 *
 * @param path The file path.
 * @param stat The file status to fill.
 * @return 0 on success, a negative error code on failure.
 *
 * @note The final symbolic link is followed.
 */
mr_err_t mr_fs_stat_by_path(const char *path, mr_stat_t *stat);

/**
 * @brief This function gets file status relative to a directory handle.
 *
 * @param dir The base directory for path resolution. It is ignored for an
 * absolute path unless MR_AT_IN_ROOT is specified and may be MR_NULL otherwise.
 * @param path The file path.
 * @param stat The file status to fill.
 * @param flags The path resolution flags(see MR_AT_*).
 * @return 0 on success, a negative error code on failure.
 *
 * @note MR_AT_IN_ROOT treats dir as a temporary FS root.
 * @note MR_AT_SYMLINK_NOFOLLOW returns the final symbolic link's status.
 */
mr_err_t mr_fs_statat(const mr_file_t *dir, const char *path, mr_stat_t *stat,
                      int flags);

/**
 * @brief This function creates a hard link to an open file.
 *
 * @param file The source file(must be open).
 * @param new_path The new link path.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Directory hard links are rejected with -MR_EPERM.
 * @note A cross-file-system link is rejected with -MR_EXDEV.
 * @note A missing link callback uses the built-in namespace-only behavior.
 */
mr_err_t mr_fs_link(const mr_file_t *file, const char *new_path);

/**
 * @brief This function creates a symbolic link.
 *
 * @param target The target path stored in the link.
 * @param link_path The symbolic link path.
 * @return 0 on success, a negative error code on failure.
 *
 * @note A missing symlink callback creates an in-memory target with built-in
 * readlink support.
 */
mr_err_t mr_fs_symlink(const char *target, const char *link_path);

/**
 * @brief This function reads an opened symbolic link.
 *
 * @param file The symbolic link opened with MR_O_NOFOLLOW.
 * @param buf The target path buffer.
 * @param size The buffer size.
 * @return The target length on success, a negative error code on failure.
 *
 * @note The returned target is not null-terminated.
 */
mr_ssize_t mr_fs_readlink(const mr_file_t *file, char *buf, mr_size_t size);

/**
 * @brief This function renames the current name of an open file.
 *
 * @param file The file(must be open).
 * @param new_path The new path.
 * @return 0 on success, a negative error code on failure.
 *
 * @note The destination must not already exist(-MR_EEXIST).
 * @note A cross-file-system rename is rejected with -MR_EXDEV.
 * @note A mount-point directory can not be renamed(-MR_EBUSY).
 */
mr_err_t mr_fs_rename(mr_file_t *file, const char *new_path);

/**
 * @brief This function renames a file by path.
 *
 * @param old_path The current file path.
 * @param new_path The new file path.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_fs_rename_by_path(const char *old_path, const char *new_path);

/**
 * @brief This function renames a file between directory-relative paths.
 *
 * @param old_dir The base directory for old_path. It is ignored for an
 * absolute old_path unless MR_AT_IN_ROOT is specified.
 * @param old_path The current file path.
 * @param new_dir The base directory for new_path. It is ignored for an
 * absolute new_path unless MR_AT_IN_ROOT is specified.
 * @param new_path The new file path.
 * @param flags The path resolution flags(see MR_AT_*).
 * @return 0 on success, a negative error code on failure.
 *
 * @note Relative paths require their corresponding open directory handles.
 * @note MR_AT_IN_ROOT treats each directory handle as its path's temporary FS
 * root.
 */
mr_err_t mr_fs_renameat(const mr_file_t *old_dir, const char *old_path,
                        const mr_file_t *new_dir, const char *new_path,
                        int flags);

/**
 * @brief This function unlinks the current name of an open file.
 *
 * @param file The file(must be open).
 * @return 0 on success, a negative error code on failure.
 *
 * @note The file remains open after its name is removed.
 * @note A non-empty directory is rejected with -MR_ENOTEMPTY.
 * @note A mount-point directory can not be removed(-MR_EBUSY).
 */
mr_err_t mr_fs_unlink(mr_file_t *file);

/**
 * @brief This function recursively unlinks the current name of an open file or
 * directory.
 *
 * @param file The file or directory(must be open).
 * @return 0 on success, a negative error code on failure.
 *
 * @note The file remains open after its name is removed.
 * @note Symbolic links are removed without following their target.
 * @note A mount-point or lookup root is rejected with -MR_EBUSY.
 * @note The operation is not atomic. Entries removed before a later failure
 * remain removed.
 */
mr_err_t mr_fs_unlink_recursive(mr_file_t *file);

/**
 * @brief This function unlinks a file by path.
 *
 * @param path The file path.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_fs_unlink_by_path(const char *path);

/**
 * @brief This function recursively unlinks a file or directory by path.
 *
 * @param path The file or directory path.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Symbolic links are removed without following their final target.
 * @note A mount-point or lookup root is rejected with -MR_EBUSY.
 * @note The operation is not atomic. Entries removed before a later failure
 * remain removed.
 */
mr_err_t mr_fs_unlink_recursive_by_path(const char *path);

/**
 * @brief This function unlinks a file relative to a directory handle.
 *
 * @param dir The base directory for path resolution. It is ignored for an
 * absolute path unless MR_AT_IN_ROOT is specified and may be MR_NULL otherwise.
 * @param path The file path.
 * @param flags The path resolution flags(see MR_AT_*).
 * @return 0 on success, a negative error code on failure.
 *
 * @note For a relative path, dir must be an open directory handle.
 * @note MR_AT_IN_ROOT treats dir as a temporary FS root.
 */
mr_err_t mr_fs_unlinkat(const mr_file_t *dir, const char *path, int flags);

/**
 * @brief This function recursively unlinks a file or directory relative to a
 * directory handle.
 *
 * @param dir The base directory for path resolution. It is ignored for an
 * absolute path unless MR_AT_IN_ROOT is specified and may be MR_NULL otherwise.
 * @param path The file or directory path.
 * @param flags The path resolution flags(see MR_AT_*).
 * @return 0 on success, a negative error code on failure.
 *
 * @note Symbolic links are removed without following their final target.
 * @note A mount-point or lookup root is rejected with -MR_EBUSY.
 * @note The operation is not atomic. Entries removed before a later failure
 * remain removed.
 */
mr_err_t mr_fs_unlinkat_recursive(const mr_file_t *dir, const char *path,
                                  int flags);

/**
 * @brief This function reads from a file.
 *
 * @param file The file.
 * @param buf The buffer.
 * @param size The number of bytes to read.
 * @return The number of bytes read on success, a negative error code on
 * failure.
 */
mr_ssize_t mr_fs_read(mr_file_t *file, void *buf, mr_size_t size);

/**
 * @brief This function writes to a file.
 *
 * @param file The file.
 * @param buf The buffer.
 * @param size The number of bytes to write.
 * @return The number of bytes written on success, a negative error code on
 * failure.
 *
 * @note MR_O_APPEND selects the current end-of-file before every write. A
 * failed or zero-length append does not change the previous file position.
 */
mr_ssize_t mr_fs_write(mr_file_t *file, const void *buf, mr_size_t size);

/**
 * @brief This function seeks a file.
 *
 * @param file The file.
 * @param offset The offset.
 * @param whence The seek base(see MR_SEEK_*).
 * @return The new position on success, a negative error code on failure.
 *
 * @note The default seek returns -MR_ERANGE without changing the position if
 * the requested position cannot be represented by mr_int64_t.
 */
mr_int64_t mr_fs_seek(mr_file_t *file, mr_int64_t offset, int whence);

/**
 * @brief This function gets the current position of a file.
 *
 * @param file The file.
 * @return The current position on success, a negative error code on failure.
 */
mr_int64_t mr_fs_tell(mr_file_t *file);

/**
 * @brief This function truncates a file to the given length.
 *
 * @param file The file(must be open for writing).
 * @param length The new length.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Directories are rejected with -MR_EISDIR. Other non-regular files are
 * rejected with -MR_EINVAL.
 */
mr_err_t mr_fs_truncate(mr_file_t *file, mr_int64_t length);

/**
 * @brief This function syncs an open file.
 *
 * @param file The file(must be open).
 * @return 0 on success, a negative error code on failure.
 *
 * @note Returns -MR_ENOSYS if no sync hook is provided.
 */
mr_err_t mr_fs_sync(mr_file_t *file);

/**
 * @brief This function performs an I/O control on a file.
 *
 * @param file The file.
 * @param cmd The command.
 * @param arg The argument.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_fs_ioctl(mr_file_t *file, int cmd, void *arg);

/**
 * @brief This function changes the root directory of the current context.
 *
 * @param path The new root path(resolved relative to the current context).
 * @return 0 on success, a negative error code on failure.
 *
 * @note The current working directory is retained when it is contained by the
 * new root. Otherwise, it is reset to the new root so cwd-relative paths
 * cannot escape the new root. Pre-opened directory handles are unaffected.
 */
mr_err_t mr_fs_chroot(const char *path);

/**
 * @brief This function changes the current working directory.
 *
 * @param path The new working directory path.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_fs_chdir(const char *path);

/**
 * @brief This function gets the absolute path of the current working
 * directory.
 *
 * @param buf The buffer to hold the path.
 * @param size The buffer size.
 * @return The path length on success, a negative error code on failure.
 */
mr_ssize_t mr_fs_getcwd(char *buf, mr_size_t size);
#endif /* defined(MR_USE_FS) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_FS_FILE_H__ */

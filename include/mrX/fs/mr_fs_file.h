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
#include <mrX/fs/mr_fs_sblock.h>
#endif /* defined(MR_USE_FS) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup FS-File
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
 * @brief This function checks whether a file handle is open.
 *
 * @param file The file handle.
 * @return MR_TRUE if the file handle is open, otherwise MR_FALSE.
 */
MR_INLINE mr_bool_t mr_file_is_open(const mr_file_t *file) {
    MR_ASSERT(file != MR_NULL);

    /* Check file is open */
    return file->inode ? MR_TRUE : MR_FALSE;
}

/**
 * @brief This function selects the current end-of-file for an append write.
 *
 * @param file The open file.
 *
 * @note Call this from a write callback after the file-system holds its data
 * lock. VFS does not reposition the handle. A failed write must restore the
 * previous position; a zero-length write should not call this helper.
 */
MR_INLINE void mr_file_apply_append(mr_file_t *file) {
    MR_ASSERT((file != MR_NULL) && (file->inode != MR_NULL));

    /* Select EOF only for append handles */
    if ((file->flags & MR_O_APPEND) != 0) {
        file->pos = file->inode->size;
    }
}

/**
 * @brief This function mounts a file-system at the global FS root.
 *
 * @param source The mount source.
 * @param fstype The file-system name(must be registered).
 * @param flags The mount flags.
 * @param data The mount private data(passed to the fs mount callback).
 * @return 0 on success, a negative error code on failure.
 *
 * @note This function does not access the current file-system structure.
 * Use it only during initialization, before any structure context is live.
 * It installs or replaces the global FS root and does not retarget existing
 * root/cwd references. After a structure is live, attach a file-system at a
 * path with mr_fs_mount().
 */
mr_err_t mr_fs_mount_root(const char *source, const char *fstype, int flags,
                          void *data);

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
 * @note Path is resolved in the current structure. Use mr_fs_mount_root() to
 * set or replace the global FS root.
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
 * @note In-flight operations and open file handles return -MR_EBUSY.
 * Close and sync files before unmounting.
 * @note The backend unmount callback only releases memory resources after
 * the final reference. It does not perform device I/O and can not fail.
 */
mr_err_t mr_fs_umount(const char *path);

/**
 * @brief This function forcibly detaches a mount whose backing media is gone.
 *
 * @param path The mount-point path.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Use this only after the device has disappeared. It is not a generic
 * force-unmount.
 * @note The target and every descendant mount are marked OFFLINE and removed
 * from the structure, leaf first. This operation does not call the backend
 * or issue device I/O.
 * @note OFFLINE never returns to ACTIVE. Open handles return -MR_ENODEV.
 * The final backend memory cleanup runs after the last reference is dropped.
 * @note If an operation is still running, this function returns -MR_EBUSY
 * after taking the subtree OFFLINE. Retry the same path to finish
 * reclamation; covering may already be cleared, but
 * mr_sblock_find_by_dentry() still matches sb->mnt.
 */
mr_err_t mr_fs_media_detach(const char *path);

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
 * @brief This function removes an empty directory at the given path.
 *
 * @param path The directory path.
 * @return 0 on success, a negative error code on failure.
 *
 * @note The node must be a directory(-MR_ENOTDIR).
 * @note A non-empty directory is rejected with -MR_ENOTEMPTY.
 * @note A mount-point directory can not be removed(-MR_EBUSY).
 */
mr_err_t mr_fs_rmdir(const char *path);

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
 * @brief This function creates a FIFO node at the given path.
 *
 * @param path The FIFO path.
 * @return 0 on success, a negative error code on failure.
 *
 * @note This is equivalent to mr_fs_mknod(path, MR_INODE_FIFO, MR_NULL).
 * @note The parent must already exist(-MR_ENOENT).
 * @note An existing leaf is rejected with -MR_EEXIST.
 */
mr_err_t mr_fs_mkfifo(const char *path);

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
 * @brief This function closes a file.
 *
 * @param file The file.
 * @return 0 on success, a negative error code on failure.
 *
 * @note The handle is invalidated regardless of the return value. A close
 * error only reports a flush or finalize failure and is not retryable.
 * Closing an offline handle still calls the backend close callback so it can
 * release per-file memory, then returns -MR_ENODEV. The callback must tolerate
 * device errors and release memory regardless of its return value.
 * Closing an already-closed handle returns -MR_EBADF.
 */
mr_err_t mr_fs_close(mr_file_t *file);

/**
 * @brief This function reads the next directory entry.
 *
 * @param file The directory(must be open).
 * @param dirent The directory entry to fill.
 * @return 1 if an entry was read, 0 at end-of-directory, or a negative error
 * code on failure.
 *
 * @note 0 means end-of-directory, not success. Iterate while the return value
 * is greater than 0; treat 0 as EOF and a negative value as an error.
 */
mr_err_t mr_fs_readdir(mr_file_t *file, mr_dirent_t *dirent);

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
mr_err_t mr_fs_fstat(const mr_file_t *file, mr_stat_t *stat);

/**
 * @brief This function renames a file-system object by path.
 *
 * @param old_path The current path.
 * @param new_path The new path.
 * @return 0 on success, a negative error code on failure.
 *
 * @note The destination must not already exist(-MR_EEXIST).
 * @note A rename across super-block instances is rejected with -MR_EXDEV,
 * including instances of the same file-system type.
 * @note Moves are serialized per super-block; a competing move returns
 * -MR_EBUSY. Backend callbacks on different super-blocks can run in parallel.
 * @note A mount-point directory can not be renamed(-MR_EBUSY).
 */
mr_err_t mr_fs_rename(const char *old_path, const char *new_path);

/**
 * @brief This function unlinks a file-system object by path.
 *
 * @param path The file-system object path.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Directories are rejected with -MR_EISDIR; use mr_fs_rmdir() instead.
 * @note A mount-point directory can not be removed(-MR_EBUSY).
 */
mr_err_t mr_fs_unlink(const char *path);

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
 * @note MR_O_APPEND is a write-callback contract: the file-system must
 * select the current end-of-file under its own data serialization. VFS does
 * not reposition the handle. A failed or zero-length append must leave the
 * previous file position unchanged.
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
 * @note A negative resulting position is rejected with -MR_EINVAL
 * @note The default seek returns -MR_ERANGE without changing the position if
 * the requested position cannot be represented by mr_int64_t.
 */
mr_int64_t mr_fs_lseek(mr_file_t *file, mr_int64_t offset, int whence);

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
mr_err_t mr_fs_ftruncate(mr_file_t *file, mr_int64_t length);

/**
 * @brief This function syncs an open file.
 *
 * @param file The file(must be open).
 * @return 0 on success, a negative error code on failure.
 *
 * @note Returns -MR_ENOSYS if no sync hook is provided.
 */
mr_err_t mr_fs_fsync(mr_file_t *file);

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

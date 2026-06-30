/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_FS_INODE_H__
#define __MR_FS_INODE_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_FS)
#include <mrX/kernel/mr_object.h>
#endif /* defined(MR_USE_FS) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Inode
 * @{
 */

#if defined(MR_USE_FS)
struct mr_super_block;
struct mr_dirent;
struct mr_inode;
struct mr_file;

/* Inode type definition */
typedef enum mr_inode_type {
    MR_INODE_NONE = 0,
    MR_INODE_REG = 1,
    MR_INODE_DIR = 2,
    MR_INODE_CHR = 3,
    MR_INODE_BLK = 4,
    MR_INODE_LNK = 5,
} mr_inode_type_t;

/* Directory entry type */
typedef struct mr_dirent {
#if !defined(MR_CFG_FS_FILE_NAME_LEN)
#define MR_CFG_FS_FILE_NAME_LEN        (260)
#endif /* !defined(MR_CFG_FS_FILE_NAME_LEN) */
    char name[MR_CFG_FS_FILE_NAME_LEN];
    mr_inode_type_t type;
} mr_dirent_t;

/* File status type */
typedef struct mr_stat {
    mr_inode_type_t type;
    mr_uint32_t mode;
    mr_uint32_t nlink;
    mr_int64_t size;
} mr_stat_t;

/* Inode operations type */
typedef struct mr_inode_ops {
    mr_err_t (*create)(struct mr_inode *dir, const char *name,
                       mr_inode_type_t type, struct mr_inode **out);
    mr_err_t (*lookup)(struct mr_inode *dir, const char *name,
                       struct mr_inode **out);
    mr_err_t (*stat)(struct mr_inode *inode, mr_stat_t *stat);
    mr_err_t (*unlink)(struct mr_inode *dir, const char *name);
    mr_err_t (*rename)(struct mr_inode *old_dir, const char *old_name,
                       struct mr_inode *new_dir, const char *new_name);
    mr_err_t (*truncate)(struct mr_inode *inode, mr_int64_t length);
    void (*destroy)(struct mr_inode *inode);
} mr_inode_ops_t;

/* File operations type */
typedef struct mr_file_ops {
    mr_err_t (*open)(struct mr_inode *inode, struct mr_file *file);
    mr_err_t (*close)(struct mr_file *file);
    mr_ssize_t (*read)(struct mr_file *file, void *buf, mr_size_t size);
    mr_ssize_t (*write)(struct mr_file *file, const void *buf, mr_size_t size);
    mr_int64_t (*llseek)(struct mr_file *file, mr_int64_t offset, int whence);
    mr_err_t (*ioctl)(struct mr_file *file, int cmd, void *arg);
    mr_bool_t (*readdir)(struct mr_file *file, mr_dirent_t *dirent);
    mr_err_t (*sync)(struct mr_file *file);
} mr_file_ops_t;

/* Inode type */
MR_CLAZZES_EXPORT(inode);
typedef struct mr_inode {
    mr_object_t parent;
    mr_inode_type_t type;
    mr_uint32_t mode;
    mr_uint32_t nlink;
    mr_int64_t size;
    struct mr_super_block *sb;
    const mr_inode_ops_t *ops;
    const mr_file_ops_t *fops;
    void *priv;
} mr_inode_t;

/**
 * @brief This function initializes a static inode.
 *
 * @param inode The inode.
 * @param type The inode type.
 * @param ops The inode operations(maybe MR_NULL).
 * @param fops The file operations(maybe MR_NULL).
 * @param priv The private data.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_inode_init(mr_inode_t *inode, mr_inode_type_t type,
                       const mr_inode_ops_t *ops, const mr_file_ops_t *fops,
                       void *priv);

/**
 * @brief This function creates a dynamic inode.
 *
 * @param type The inode type.
 * @param i_op The inode operations(maybe MR_NULL).
 * @param i_fop The file operations(maybe MR_NULL).
 * @param priv The private data.
 * @return The inode on success, MR_NULL on failure.
 */
mr_inode_t *mr_inode_create(mr_inode_type_t type, const mr_inode_ops_t *i_op,
                            const mr_file_ops_t *i_fop, void *priv);

/**
 * @brief This macro function gets the inode type.
 */
#define MR_INODE_TYPE(_inode)           ((_inode)->type)

/**
 * @brief This macro function gets the inode size.
 */
#define MR_INODE_SIZE(_inode)           ((_inode)->size)

/**
 * @brief This function deletes an inode.
 *
 * @param inode The inode.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_inode_del(mr_inode_t *inode);

/**
 * @brief This function gets an inode.
 *
 * @param inode The inode.
 * @return The inode on success, MR_NULL on failure.
 */
MR_INLINE mr_inode_t *mr_inode_get(mr_inode_t *inode) {
    /* Get inode reference */
    return (mr_inode_t *)mr_object_get((mr_object_t *)inode);
}

/**
 * @brief This function puts an inode.
 *
 * @param inode The inode.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_inode_put(mr_inode_t *inode) {
    /* Put inode reference */
    return mr_object_put((mr_object_t *)inode);
}
#endif /* defined(MR_USE_FS) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_FS_INODE_H__ */

/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/fs/mr_inode.h>
#if defined(MR_USE_FS)
#include <mrX/libc/mr_malloc.h>
#include <mrX/fs/mr_sb.h>

/* Inode clazzes(static & dynamic) */
static void inode_statik_release(mr_object_t *obj);
static void inode_dynamic_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(inode, inode_statik_release, inode_dynamic_release, MR_NULL);

static mr_err_t inode_init(mr_inode_t *inode, const mr_clazz_t *clazz,
                           mr_inode_type_t type, const mr_inode_ops_t *ops,
                           const mr_file_ops_t *fops, void *priv) {
    /* Check parameter */
    MR_ASSERT((inode != MR_NULL) && (clazz != MR_NULL));
    MR_ASSERT((type != MR_INODE_NONE) && (type < MR_INODE_MAX));

    /* Init inode */
    inode->type = type;
    inode->mode = 0;
    inode->nlink = 1;
    inode->size = 0;
    inode->sb = MR_NULL;
    inode->ops = ops;
    inode->fops = fops;
    inode->priv = priv;

    /* Init object */
    return mr_object_init((mr_object_t *)inode, clazz);
}

mr_err_t mr_inode_init(mr_inode_t *inode, mr_inode_type_t type,
                       const mr_inode_ops_t *ops, const mr_file_ops_t *fops,
                       void *priv) {
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(inode);

    /* Check parameter */
    MR_ASSERT(inode != MR_NULL);
    MR_ASSERT((type != MR_INODE_NONE) && (type < MR_INODE_MAX));

    /* Init inode */
    return inode_init(inode, clazz, type, ops, fops, priv);
}

mr_inode_t *mr_inode_create(mr_inode_type_t type, const mr_inode_ops_t *i_op,
                            const mr_file_ops_t *i_fop, void *priv) {
    const mr_clazz_t *clazz = MR_CLAZZES_DYNAMIC(inode);
    mr_inode_t *inode;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((type != MR_INODE_NONE) && (type < MR_INODE_MAX));

    /* Create inode */
    inode = mr_malloc(sizeof(mr_inode_t));
    if (!inode) {
        return MR_NULL;
    }

    /* Init inode */
    ret = inode_init(inode, clazz, type, i_op, i_fop, priv);
    if (ret != 0) {
        mr_free(inode);
        return MR_NULL;
    }
    return inode;
}

static mr_inode_t *inode_obj_release(mr_object_t *obj) {
    mr_inode_t *inode;

    /* Get inode */
    inode = MR_CONTAINER_OF(obj, mr_inode_t, parent);

    /* Let the filesystem release private resources */
    if (inode->ops && inode->ops->destroy) {
        inode->ops->destroy(inode);
    }

    /* Detach from super-block */
    if (inode->sb) {
        mr_super_block_put(inode->sb);
        inode->sb = MR_NULL;
    }
    return inode;
}

static void inode_statik_release(mr_object_t *obj) {
    /* Release inode */
    inode_obj_release(obj);
}

static void inode_dynamic_release(mr_object_t *obj) {
    mr_inode_t *inode;

    /* Release inode */
    inode = inode_obj_release(obj);

    /* Free inode */
    mr_free(inode);
}
#endif /* defined(MR_USE_FS) */

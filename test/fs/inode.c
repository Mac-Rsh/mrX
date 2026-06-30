/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/test/mr_test.h>
#if defined(MR_USE_TEST) && defined(MR_USE_FS)
#include <mrX/fs/mr_inode.h>
#include <mrX/libc/mr_string.h>

/* Mock destroy counter */
static int __inode_destroy_count;

static void __inode_destroy_cb(mr_inode_t *inode) {
    MR_IGNORE(inode);
    __inode_destroy_count++;
}

static const mr_inode_ops_t __test_inode_ops = {
    .destroy = __inode_destroy_cb,
};

static const mr_file_ops_t __test_file_ops = {
    MR_NULL,
};

static void inode_reset(void) {
    __inode_destroy_count = 0;
}

/* Tests */

static void test_inode_init_reg(void) {
    mr_inode_t inode;
    mr_err_t ret;

    inode_reset();
    mr_memset(&inode, 0, sizeof(inode));
    ret = mr_inode_init(&inode, MR_INODE_REG, &__test_inode_ops,
                        &__test_file_ops, MR_NULL);

    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_INITED(&inode), MR_TRUE);
        MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_CLAZZES(&inode, inode), MR_TRUE);
        MR_TEST_EXPECT_EQ_INT(inode.type, MR_INODE_REG);
        MR_TEST_EXPECT_EQ_INT(inode.nlink, 1);
        MR_TEST_EXPECT_EQ_INT(inode.size, 0);
        mr_inode_del(&inode);
    }
}
MR_TEST_EXPORT(inode, init_reg, test_inode_init_reg);

static void test_inode_init_dir(void) {
    mr_inode_t inode;
    mr_err_t ret;

    inode_reset();
    mr_memset(&inode, 0, sizeof(inode));
    ret = mr_inode_init(&inode, MR_INODE_DIR, MR_NULL, MR_NULL, MR_NULL);

    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        MR_TEST_EXPECT_EQ_INT(inode.type, MR_INODE_DIR);
        mr_inode_del(&inode);
    }
}
MR_TEST_EXPORT(inode, init_dir, test_inode_init_dir);

static void test_inode_init_chr(void) {
    mr_inode_t inode;
    mr_err_t ret;

    inode_reset();
    mr_memset(&inode, 0, sizeof(inode));
    ret = mr_inode_init(&inode, MR_INODE_CHR, MR_NULL, MR_NULL, (void *)0xDEAD);

    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        MR_TEST_EXPECT_EQ_INT(inode.type, MR_INODE_CHR);
        MR_TEST_EXPECT_EQ_PTR(inode.priv, (void *)0xDEAD);
        mr_inode_del(&inode);
    }
}
MR_TEST_EXPORT(inode, init_chr, test_inode_init_chr);

static void test_inode_init_priv(void) {
    mr_inode_t inode;
    int priv = 42;
    mr_err_t ret;

    inode_reset();
    mr_memset(&inode, 0, sizeof(inode));
    ret = mr_inode_init(&inode, MR_INODE_LNK, MR_NULL, MR_NULL, &priv);

    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        MR_TEST_EXPECT_EQ_PTR(inode.priv, &priv);
        mr_inode_del(&inode);
    }
}
MR_TEST_EXPORT(inode, init_priv, test_inode_init_priv);

static void test_inode_init_mode(void) {
    mr_inode_t inode;
    mr_err_t ret;

    inode_reset();
    mr_memset(&inode, 0, sizeof(inode));
    ret = mr_inode_init(&inode, MR_INODE_BLK, MR_NULL, MR_NULL, MR_NULL);

    MR_TEST_EXPECT_EQ_INT(ret, 0);
    if (ret == 0) {
        MR_TEST_EXPECT_EQ_INT(inode.mode, 0);
        inode.mode = 0755;
        MR_TEST_EXPECT_EQ_INT(inode.mode, 0755);
        mr_inode_del(&inode);
    }
}
MR_TEST_EXPORT(inode, init_mode, test_inode_init_mode);

static void test_inode_create_dynamic(void) {
    mr_inode_t *inode;

    inode_reset();
    inode = mr_inode_create(MR_INODE_REG, &__test_inode_ops, &__test_file_ops,
                            MR_NULL);

    MR_TEST_EXPECT_NE_PTR(inode, MR_NULL);
    if (inode) {
        MR_TEST_EXPECT_EQ_INT(inode->type, MR_INODE_REG);
        MR_TEST_EXPECT_EQ_INT(MR_OBJECT_IS_CLAZZES(inode, inode), MR_TRUE);
        MR_TEST_EXPECT_EQ_INT(__inode_destroy_count, 0);
        mr_inode_del(inode);
        MR_TEST_EXPECT_EQ_INT(__inode_destroy_count, 1);
    }
}
MR_TEST_EXPORT(inode, create_dynamic, test_inode_create_dynamic);

static void test_inode_del_static_destroy(void) {
    mr_inode_t inode;
    mr_err_t ret;

    inode_reset();
    mr_memset(&inode, 0, sizeof(inode));
    ret = mr_inode_init(&inode, MR_INODE_REG, &__test_inode_ops,
                        &__test_file_ops, MR_NULL);
    MR_TEST_ASSERT_EQ_INT(ret, 0);
    if (ret != 0) {
        return;
    }

    MR_TEST_EXPECT_EQ_INT(__inode_destroy_count, 0);
    mr_inode_del(&inode);
    MR_TEST_EXPECT_EQ_INT(__inode_destroy_count, 1);
}
MR_TEST_EXPORT(inode, del_static_destroy, test_inode_del_static_destroy);

static void test_inode_get_put_refcount(void) {
    mr_inode_t *inode;
    mr_inode_t *ret_inode;

    inode_reset();
    inode = mr_inode_create(MR_INODE_REG, &__test_inode_ops, &__test_file_ops,
                            MR_NULL);
    MR_TEST_ASSERT_NE_PTR(inode, MR_NULL);
    if (!inode) {
        return;
    }

    /* Get/put do not destroy while refs are held */
    ret_inode = mr_inode_get(inode);
    MR_TEST_EXPECT_EQ_PTR(ret_inode, inode);

    ret_inode = mr_inode_get(inode);
    MR_TEST_EXPECT_EQ_PTR(ret_inode, inode);

    mr_inode_put(inode);
    MR_TEST_EXPECT_EQ_INT(__inode_destroy_count, 0);

    mr_inode_put(inode);
    MR_TEST_EXPECT_EQ_INT(__inode_destroy_count, 0);

    /* Final put destroys */
    mr_inode_put(inode);
    MR_TEST_EXPECT_EQ_INT(__inode_destroy_count, 1);
}
MR_TEST_EXPORT(inode, get_put_refcount, test_inode_get_put_refcount);

static void test_inode_get_after_del(void) {
    mr_inode_t *inode;
    mr_inode_t *ret;

    inode_reset();
    inode = mr_inode_create(MR_INODE_REG, MR_NULL, MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(inode, MR_NULL);
    if (!inode) {
        return;
    }

    mr_inode_del(inode);
    ret = mr_inode_get(inode);
    MR_TEST_EXPECT_EQ_PTR(ret, MR_NULL);
}
MR_TEST_EXPORT(inode, get_after_del, test_inode_get_after_del);

static void test_inode_all_types(void) {
    mr_inode_type_t types[] = {MR_INODE_REG, MR_INODE_DIR, MR_INODE_CHR,
                                MR_INODE_BLK, MR_INODE_LNK};
    int i;

    inode_reset();
    for (i = 0; i < (int)(sizeof(types) / sizeof(types[0])); i++) {
        mr_inode_t *inode = mr_inode_create(types[i], MR_NULL, MR_NULL, MR_NULL);
        MR_TEST_EXPECT_NE_PTR(inode, MR_NULL);
        if (inode) {
            MR_TEST_EXPECT_EQ_INT(inode->type, types[i]);
            mr_inode_del(inode);
        }
    }
}
MR_TEST_EXPORT(inode, all_types, test_inode_all_types);

static void test_inode_del_no_destroy_no_ops(void) {
    mr_inode_t *inode;

    inode_reset();
    inode = mr_inode_create(MR_INODE_REG, MR_NULL, MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(inode, MR_NULL);
    if (!inode) {
        return;
    }

    /* No destroy callback: counter stays zero but del succeeds */
    mr_inode_del(inode);
    MR_TEST_EXPECT_EQ_INT(__inode_destroy_count, 0);
}
MR_TEST_EXPORT(inode, del_no_destroy_no_ops, test_inode_del_no_destroy_no_ops);

static void test_inode_sb_detach_on_destroy(void) {
    mr_inode_t *inode;

    inode_reset();
    inode = mr_inode_create(MR_INODE_REG, &__test_inode_ops, MR_NULL, MR_NULL);
    MR_TEST_ASSERT_NE_PTR(inode, MR_NULL);
    if (!inode) {
        return;
    }

    MR_TEST_EXPECT_EQ_PTR(inode->sb, MR_NULL);
    mr_inode_del(inode);
    /* Just verify no crash when sb is NULL (the default) */
    MR_TEST_EXPECT_EQ_INT(__inode_destroy_count, 1);
}
MR_TEST_EXPORT(inode, sb_detach_on_destroy, test_inode_sb_detach_on_destroy);
#endif /* defined(MR_USE_TEST) && defined(MR_USE_FS) */

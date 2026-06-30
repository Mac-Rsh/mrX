/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/fs/mr_dentry.h>
#if defined(MR_USE_FS)
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/libc/mr_sprintf.h>
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>
#include <mrX/fs/mr_inode.h>
#include <mrX/fs/mr_sb.h>

/* Dentry clazzes(static & dynamic) */
static void dentry_statik_release(mr_object_t *obj);
static void dentry_dynamic_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(dentry, dentry_statik_release, dentry_dynamic_release,
                  MR_NULL);

/* Dentry lock */
static mr_spinlock_t __dentry_lock = MR_SPINLOCK_INIT();

/* Last-word definition */
#define LWORD_DOT2                      (0x2e2e)
#define LWORD_DOT                       (0x2e)

static mr_uint32_t dentry_default_hash(const char *str, mr_uint32_t len) {
    mr_uint32_t hash = MR_DENTRY_HASH_SEED;
    mr_uint32_t i;

    /* Default djb2-xor hash */
    for (i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) ^ ((mr_uint8_t)str[i]);
    }
    return hash;
}

static const char *dentry_name_hash(mr_dentry_name_t *name, const char *str,
                                    const mr_dentry_ops_t *ops,
                                    mr_uint32_t *lword) {
    /* Scan name up to the next '/' or end, computing the last-word for
     * recognizing '.' and '..' */
    for (name->str = str, *lword = 0; (*str != '\0') && (*str != '/'); str++) {
        *lword = (*lword << 8) + ((mr_uint8_t)(*str));
    }
    name->len = (mr_uint32_t)(str - name->str);

    /* Use ops->hash if provided, otherwise the default */
    if ((!ops) || (!ops->hash)) {
        name->hash = dentry_default_hash(name->str, name->len);
    } else {
        name->hash = ops->hash(name->str, name->len);
    }
    return str;
}

static char *str_alloc(mr_uint32_t str_len, char *istr, mr_uint32_t istr_len) {
    /* Allocate string */
    if (str_len < istr_len) {
        /* Use inline string if possible */
        return istr;
    }

    /* Use dynamic string instead */
    return mr_malloc(str_len + 1);
}

static mr_err_t str_varg_alloc(const char *fmt, mr_va_list args,
                               const char **str, char *istr,
                               mr_uint32_t istr_len) {
    mr_va_list copied_args;
    mr_uint32_t str_len;
    int ret;

    /* Format string to get length */
    va_copy(copied_args, args);
    ret = mr_vsnprintf(MR_NULL, 0, fmt, copied_args);
    va_end(copied_args);
    if (ret < 0) {
        /* Format error */
        return -MR_EINVAL;
    }
    str_len = (mr_uint32_t)ret;

    /* Allocate string */
    *str = str_alloc(str_len, istr, istr_len);
    if (!(*str)) {
        return -MR_ENOMEM;
    }

    /* Format string */
    mr_vsnprintf((char *)*str, str_len + 1, fmt, args);
    return 0;
}

static void str_free(const char *str, const char *istr) {
    /* Inline string does not need to be freed */
    if (str == istr) {
        return;
    }

    /* Free dynamic string */
    mr_free((char *)str);
}

static mr_err_t dentry_name_alloc(mr_dentry_t *dentry, const char *name,
                                  const mr_dentry_ops_t *ops) {
    mr_uint32_t lword;
    mr_size_t len;
    char *s;

    /* Name must be a single component. "/" is allowed as the sole name of
     * the VFS root dentry */
    if (mr_strchr(name, '/')
        && !((name[0] == '/') && (name[1] == '\0'))) {
        return -MR_EINVAL;
    }

    /* Get name length */
    len = mr_strlen(name);

    /* Allocate name */
    s = str_alloc((mr_uint32_t)len, dentry->iname, sizeof(dentry->iname));
    if (!s) {
        return -MR_ENOMEM;
    }

    /* Copy and hash name using the given ops (NULL = default djb2) */
    mr_strncpy(s, name, len);
    s[len] = '\0';
    dentry_name_hash(&dentry->name, s, ops, &lword);
    return 0;
}

static void dentry_name_free(mr_dentry_t *dentry) {
    /* Free name */
    str_free(dentry->name.str, dentry->iname);
    dentry->name.str = MR_NULL;
}

static mr_err_t dentry_init(mr_dentry_t *dentry, const mr_clazz_t *clazz,
                            const mr_dentry_ops_t *ops, const char *fmt,
                            mr_va_list args) {
    char istr[MR_CFG_FS_DENTRY_NAME_LEN * 2];
    const char *str;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((dentry != MR_NULL) && (clazz != MR_NULL));
    MR_ASSERT((fmt != MR_NULL) && (fmt[0] != '\0'));

    /* Allocate string */
    ret = str_varg_alloc(fmt, args, &str, istr, sizeof(istr));
    if (ret != 0) {
        return ret;
    }

    /* Init dentry */
    ret = dentry_name_alloc(dentry, str, ops);
    if (ret != 0) {
        goto _exit;
    }
    mr_list_init(&dentry->sibling);
    mr_list_init(&dentry->lower);
    dentry->upper = MR_NULL;
    dentry->inode = MR_NULL;
    dentry->ops = ops;

    /* Init object */
    ret = mr_object_init((mr_object_t *)dentry, clazz);
    if (ret != 0) {
        /* Free name */
        dentry_name_free(dentry);
    }
_exit:
    /* Free string */
    str_free(str, istr);
    return ret;
}

mr_err_t mr_dentry_vinit(mr_dentry_t *dentry, const mr_dentry_ops_t *ops,
                         const char *fmt, mr_va_list args) {
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(dentry);

    /* Check parameter */
    MR_ASSERT((dentry != MR_NULL) && (!MR_OBJECT_IS_INITED(dentry)));
    MR_ASSERT((fmt != MR_NULL) && (fmt[0] != '\0'));

    /* Init dentry */
    return dentry_init(dentry, clazz, ops, fmt, args);
}

mr_dentry_t *mr_dentry_vcreate(const mr_dentry_ops_t *ops, const char *fmt,
                               mr_va_list args) {
    const mr_clazz_t *clazz = MR_CLAZZES_DYNAMIC(dentry);
    mr_dentry_t *dentry;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((fmt != MR_NULL) && (fmt[0] != '\0'));

    /* Create dentry */
    dentry = mr_malloc(sizeof(mr_dentry_t));
    if (!dentry) {
        return MR_NULL;
    }

    /* Init dentry */
    ret = dentry_init(dentry, clazz, ops, fmt, args);
    if (ret != 0) {
        mr_free(dentry);
        return MR_NULL;
    }
    return dentry;
}

mr_err_t mr_dentry_init(mr_dentry_t *dentry, const mr_dentry_ops_t *ops,
                        const char *fmt, ...) {
    mr_va_list args;
    mr_err_t ret;

    /* Init dentry */
    mr_va_start(args, fmt);
    ret = mr_dentry_vinit(dentry, ops, fmt, args);
    mr_va_end(args);
    return ret;
}

mr_dentry_t *mr_dentry_create(const mr_dentry_ops_t *ops, const char *fmt,
                              ...) {
    mr_dentry_t *dentry;
    mr_va_list args;

    /* Create dentry */
    mr_va_start(args, fmt);
    dentry = mr_dentry_vcreate(ops, fmt, args);
    mr_va_end(args);
    return dentry;
}

static void dentry_remove(mr_dentry_t *dentry) {
    mr_dentry_t *upper;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__dentry_lock);

    /* Remove dentry from sibling list */
    mr_list_del(&dentry->sibling);
    upper = dentry->upper;
    dentry->upper = MR_NULL;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__dentry_lock, mask);

    /* Put upper */
    if (upper) {
        mr_dentry_put(upper);
    }
}

mr_err_t mr_dentry_remove(mr_dentry_t *dentry) {
    /* Check parameter */
    MR_ASSERT((dentry != MR_NULL) && MR_OBJECT_IS_INITED(dentry));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(dentry, dentry));

    /* Remove dentry */
    dentry_remove(dentry);
    return 0;
}

mr_err_t mr_dentry_del(mr_dentry_t *dentry) {
    /* Check parameter */
    MR_ASSERT((dentry != MR_NULL) && MR_OBJECT_IS_INITED(dentry));

    /* Remove dentry */
    dentry_remove(dentry);

    /* Delete object */
    mr_object_del((mr_object_t *)dentry);
    return 0;
}

static mr_bool_t dentry_name_match(mr_dentry_name_t *n1, mr_dentry_name_t *n2,
                                   const mr_dentry_ops_t *ops) {
    /* Check hash and length */
    if ((n1->hash != n2->hash) || (n1->len != n2->len)) {
        return MR_FALSE;
    }

    /* Check string (hash possible conflicts) */
    if (ops && ops->compare) {
        return ops->compare(n1->str, n2->str, n1->len);
    }
    return (mr_strncmp(n1->str, n2->str, n1->len) == 0);
}

static mr_dentry_t *dentry_lookup(mr_dentry_t *last, mr_dentry_name_t *name) {
    const mr_dentry_ops_t *ops = last->ops;
    mr_dentry_t *dentry;
    mr_list_t *l;

    /* For each dentry */
    MR_LIST_FOR_EACH(l, &last->lower) {
        dentry = MR_LIST_ENTRY(l, struct mr_dentry, sibling);

        /* Match dentry name */
        if (dentry_name_match(&dentry->name, name, ops) != MR_TRUE) {
            continue;
        }

        /* Found */
        return dentry;
    }

    /* Not found */
    return MR_NULL;
}

static void dentry_add(mr_dentry_t *dentry, mr_dentry_t *upper) {
    /* Bind dentry to upper and acquire a reference for the binding */
    mr_list_add(&upper->lower, &dentry->sibling);
    dentry->upper = mr_dentry_get(upper);

    /* Inherit ops from upper when this dentry has none of its own. This makes
     * a per-mount ops table set on the root dentry cover the whole subtree */
    if (!dentry->ops) {
        dentry->ops = upper->ops;
    }

    /* If the inherited/own ops provide a custom hash, recompute the dentry's
     * name hash so it matches how siblings are looked up */
    if (dentry->ops && dentry->ops->hash) {
        dentry->name.hash
            = dentry->ops->hash(dentry->name.str, dentry->name.len);
    }
}

mr_err_t mr_dentry_add(mr_dentry_t *dentry, mr_dentry_t *upper) {
    mr_dentry_name_t lookup;
    mr_dentry_t *p;
    mr_uint32_t lword;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT((dentry != MR_NULL) && MR_OBJECT_IS_INITED(dentry));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(dentry, dentry));
    MR_ASSERT((upper != MR_NULL) && MR_OBJECT_IS_INITED(upper));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(upper, dentry));
    MR_ASSERT(dentry != upper);

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__dentry_lock);

    /* Can not add a dentry into its own subtree: walk up from the new parent
     * and reject if the source is an ancestor of it */
    for (p = upper; p; p = p->upper) {
        if (p == dentry) {
            ret = -MR_EPERM;
            goto _exit;
        }
    }

    /* Check if dentry is added */
    if (MR_DENTRY_IS_ADDED(dentry)) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Check if dentry is existed. Hash the name with the upper's ops so the
     * lookup matches how the siblings were hashed, even when this dentry was
     * created with different(or NULL) ops. dentry is not mutated here, so a
     * failed add leaves it untouched */
    dentry_name_hash(&lookup, dentry->name.str, upper->ops, &lword);
    if (dentry_lookup(upper, &lookup)) {
        ret = -MR_EEXIST;
        goto _exit;
    }

    /* Add dentry to upper */
    dentry_add(dentry, upper);
    ret = 0;
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__dentry_lock, mask);
    return ret;
}

mr_err_t mr_dentry_move(mr_dentry_t *dentry, mr_dentry_t *upper,
                        const char *name) {
    mr_dentry_t *old_upper, *p;
    mr_dentry_name_t lookup;
    mr_uint32_t lword;
    mr_size_t newlen;
    mr_err_t ret;
    char *newbuf;
    int mask;

    /* Check parameter */
    MR_ASSERT((dentry != MR_NULL) && MR_OBJECT_IS_INITED(dentry));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(dentry, dentry));
    MR_ASSERT((upper != MR_NULL) && MR_OBJECT_IS_INITED(upper));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(upper, dentry));
    MR_ASSERT((name != MR_NULL) && (name[0] != '\0'));

    /* Name must be a single component (no '/') */
    if (!mr_strchr(name, '/')) {
        /* Get new name length */
        newlen = mr_strlen(name);
    } else {
        return -MR_EINVAL;
    }

    /* If the new name does not fit inline, pre-allocate it so an allocation
     * failure is reported before any tree mutation */
    if (newlen >= MR_CFG_FS_DENTRY_NAME_LEN) {
        /* Use dynamic name */
        newbuf = mr_malloc(newlen + 1);
        if (!newbuf) {
            return -MR_ENOMEM;
        }
        mr_strncpy(newbuf, name, newlen);
        newbuf[newlen] = '\0';
    } else {
        /* Use inline name */
        newbuf = MR_NULL;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__dentry_lock);

    /* Can not move a dentry into itself */
    if (dentry == upper) {
        ret = -MR_EINVAL;
        goto _exit;
    }

    /* Can not move a dentry into its own subtree: walk up from the new parent
     * and reject if the source is an ancestor of it */
    for (p = upper; p; p = p->upper) {
        if (p == dentry) {
            ret = -MR_EINVAL;
            goto _exit;
        }
    }

    /* Hash the new name with the new parent's ops so the lookup matches how
     * siblings were hashed */
    dentry_name_hash(&lookup, name, upper->ops, &lword);

    /* Same parent and same name: nothing to do. This check must come before
     * the EEXIST check below, otherwise dentry_lookup would find the dentry
     * itself(Still in the parent's children at this point) and wrongly report
     * -MR_EEXIST */
    if ((dentry->upper == upper)
        && (dentry_name_match(&dentry->name, &lookup, upper->ops))) {
        ret = 0;
        goto _exit;
    }

    /* Reject a name that already exists under the new parent */
    if (dentry_lookup(upper, &lookup)) {
        ret = -MR_EEXIST;
        goto _exit;
    }

    /* Detach from the old parent's sibling list */
    old_upper = dentry->upper;
    mr_list_del(&dentry->sibling);
    dentry->upper = MR_NULL;

    /* Install the new name */
    dentry_name_free(dentry);
    if (!newbuf) {
        /* Use the inline name */
        mr_strncpy(dentry->iname, name, newlen);
        dentry->iname[newlen] = '\0';
        dentry->name.str = dentry->iname;
    } else {
        /* Use the dynamic name */
        dentry->name.str = newbuf;
        newbuf = MR_NULL;
    }
    dentry->name.hash = lookup.hash;
    dentry->name.len = lookup.len;

    /* Attach to the new parent: sets upper, inherits ops, re-hashes */
    dentry_add(dentry, upper);

    /* Put old parent */
    if (old_upper) {
        mr_dentry_put(old_upper);
    }
    ret = 0;
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__dentry_lock, mask);

    /* Free the pre-allocated buffer */
    if (newbuf) {
        mr_free(newbuf);
    }
    return ret;
}

mr_dentry_t *mr_dentry_child_get(mr_dentry_t *parent, mr_size_t index) {
    mr_dentry_t *child;
    mr_list_t *l;
    mr_size_t i;
    int mask;

    /* Check parameter */
    MR_ASSERT((parent != MR_NULL) && MR_OBJECT_IS_INITED(parent));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(parent, dentry));

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__dentry_lock);

    /* Find the index-th child */
    child = MR_NULL;
    i = 0;
    MR_LIST_FOR_EACH(l, &parent->lower) {
        if (i == index) {
            child = mr_dentry_get(MR_LIST_ENTRY(l, struct mr_dentry, sibling));
            break;
        }
        i++;
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__dentry_lock, mask);
    return child;
}

mr_bool_t mr_dentry_is_empty(mr_dentry_t *dentry) {
    mr_bool_t empty;
    int mask;

    /* Check parameter */
    MR_ASSERT((dentry != MR_NULL) && MR_OBJECT_IS_INITED(dentry));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(dentry, dentry));

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__dentry_lock);

    /* Check if the dentry has no children */
    empty = mr_list_is_empty(&dentry->lower);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__dentry_lock, mask);
    return empty;
}

static mr_dentry_t *dentry_path_lookup(mr_dentry_t **last, const char **path,
                                       mr_dentry_t *limit) {
    mr_dentry_name_t name;
    mr_dentry_t *dentry;
    mr_uint32_t lword;
    const char *str;

    for (str = *path; *str != '\0';) {
        /* Skip '/' between components */
        while (*str == '/') {
            str++;
        }
        if (*str == '\0') {
            break;
        }

        /* Update path */
        *path = str;

        /* Lookup dentry for name. Use the current directory's ops so the
         * lookup hash matches how its children were hashed */
        str = dentry_name_hash(&name, str, (*last)->ops, &lword);
        switch (lword) {
            case LWORD_DOT2: {
                /* Handle '..' */
                if ((*last == limit) || ((*last)->upper == MR_NULL)) {
                    /* Already at root */
                    continue;
                }
                dentry = (*last)->upper;
                break;
            }
            case LWORD_DOT: {
                /* Handle '.' */
                continue;
            }
            default: {
                /* Handle 'other' */
                dentry = dentry_lookup(*last, &name);
                if (!dentry) {
                    /* Not found */
                    return MR_NULL;
                }
                break;
            }
        }

        /* Update last */
        *last = dentry;
    }
    return *last;
}

static mr_dentry_t *dentry_fs_lookup(mr_dentry_t *parent, const char *name,
                                     mr_uint32_t len) {
    char istr[MR_CFG_FS_DENTRY_NAME_LEN * 2];
    mr_dentry_t *existing, *child;
    mr_dentry_name_t lookup;
    mr_uint32_t lword;
    mr_inode_t *inode;
    mr_err_t ret;
    char *str;
    int mask;

    /* Parent must be a directory providing a lookup op. Pre-cached file
     * systems (like ramfs) leave it NULL, so resolution fails here */
    if ((!parent->inode) || (parent->inode->type != MR_INODE_DIR)
        || (!parent->inode->ops) || (!parent->inode->ops->lookup)) {
        return MR_NULL;
    }

    /* Copy the component (name points into the path string, not NUL-terminated
     * at this boundary) so it can be passed to the FS hook */
    str = str_alloc(len, istr, sizeof(istr));
    if (!str) {
        return MR_NULL;
    }
    mr_strncpy(str, name, len);
    str[len] = '\0';

    /* lookup returns an inode reference that we drop after binding */
    ret = parent->inode->ops->lookup(parent->inode, str, &inode);
    if (ret == 0) {
        /* Inherit the super-block */
        if (parent->inode->sb) {
            inode->sb = mr_super_block_get(parent->inode->sb);
            if (!inode->sb) {
                mr_inode_put(inode);
                child = MR_NULL;
                goto _exit;
            }
        }
    } else {
        child = MR_NULL;
        goto _exit;
    }

    /* FS dentries inherit the super-block's dentry ops so the whole mount
     * subtree shares one revalidate/release/hash scheme */
    child = mr_dentry_create(inode->sb ? inode->sb->dops : MR_NULL, "%s", str);
    if (!child) {
        /* Put inode */
        mr_inode_put(inode);
        goto _exit;
    }

    /* Bind the inode and attach to parent */
    if (mr_dentry_instantiate(child, inode) != 0) {
        /* Delete child */
        mr_dentry_del(child);
        child = MR_NULL;
    } else if (mr_dentry_add(child, parent) != 0) {
        /* Another thread may have won the race; reuse its entry. Hash with
         * parent's ops so the lookup matches how siblings were hashed */
        dentry_name_hash(&lookup, str, parent->ops, &lword);

        /* Lock */
        mask = mr_spinlock_lock_irqsave(&__dentry_lock);

        /* Lookup the existing entry */
        existing = mr_dentry_get(dentry_lookup(parent, &lookup));

        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__dentry_lock, mask);

        /* Replace the existing entry */
        mr_dentry_del(child);
        child = existing;
    } else {
        /* Get dentry */
        child = mr_dentry_get(child);
    }

    /* Put inode */
    mr_inode_put(inode);
_exit:
    /* Free the component copy */
    str_free(str, istr);
    return child;
}

mr_dentry_t *mr_dentry_vlookup(mr_dentry_t *start, mr_dentry_t *limit,
                               const char *fmt, mr_va_list args) {
    mr_dentry_t *orig_start, *result, *cur, *parent, *held;
    char istr[MR_CFG_FS_DENTRY_NAME_LEN * 2];
    const char *path, *orig_path;
    int attempt, mask;
    mr_uint32_t len;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((start != MR_NULL) && MR_OBJECT_IS_INITED(start));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(start, dentry));
    MR_ASSERT((limit == MR_NULL) || MR_OBJECT_IS_INITED(limit));
    MR_ASSERT((limit == MR_NULL) || MR_OBJECT_IS_CLAZZES(limit, dentry));
    MR_ASSERT((fmt != MR_NULL) && (fmt[0] != '\0'));

    /* Allocate the path string */
    ret = str_varg_alloc(fmt, args, &path, istr, sizeof(istr));
    if (ret != 0) {
        return MR_NULL;
    }

    /* Save the original start and path */
    orig_start = start;
    orig_path = path;

    /* Resolve the path. If the final dentry fails revalidation we evict it and
     * retry once so a fresh FS lookup rebuilds the entry */
    for (result = MR_NULL, attempt = 0; attempt < 2; attempt++) {
        cur = orig_start;
        path = orig_path;
        held = MR_NULL;

        /* Walk the dcache as far as it reaches, asking the FS to fill in any
         * missing component along the way */
        for (;;) {
            /* Lock */
            mask = mr_spinlock_lock_irqsave(&__dentry_lock);

            /* Resolve the cached prefix under the lock. On a full hit, take a
             * reference before unlocking so the entry cannot vanish */
            result = dentry_path_lookup(&cur, &path, limit);
            if (result) {
                /* Get dentry */
                result = mr_dentry_get(result);

                /* Unlock */
                mr_spinlock_unlock_irqrestore(&__dentry_lock, mask);
                break;
            }

            /* Miss: 'cur' is the deepest cached parent, 'path' the unresolved
             * component. Snapshot a parent reference and component bounds, then
             * unlock so the (sleepable) FS hook can run */
            for (len = 0; (path[len] != '\0') && (path[len] != '/'); len++) {
            }
            parent = mr_dentry_get(cur);

            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__dentry_lock, mask);

            /* Drop the intermediate reference that kept the previous miss's
             * child alive across the unlock/relock just done */
            if (held) {
                mr_dentry_put(held);
                held = MR_NULL;
            }

            /* Ask the FS to resolve this component. On success a new dentry is
             * cached and returned with one reference */
            result = dentry_fs_lookup(parent, path, len);
            mr_dentry_put(parent);
            if (!result) {
                /* Genuinely absent, or the FS provides no lookup */
                break;
            }

            /* Continue from the freshly cached entry. Its reference is kept in
             * 'held' so it survives the next unlock/relock cycle */
            held = result;
            cur = result;
            path += len;
        }

        /* Drop the last intermediate reference (if any) */
        if (held) {
            mr_dentry_put(held);
        }

        /* Not found */
        if (!result) {
            break;
        }

        /* Revalidate out of the lock; the hook may sleep or recurse. A valid
         * (or unchecked) entry is final */
        if ((!result->ops) || (!result->ops->revalidate)
            || result->ops->revalidate(result)) {
            break;
        }

        /* Lock */
        mask = mr_spinlock_lock_irqsave(&__dentry_lock);

        /* Evict the stale entry (first attempt only, leaf-only) so a fresh FS
         * lookup can rebuild it */
        if ((attempt == 0) && MR_DENTRY_IS_ADDED(result)
            && mr_list_is_empty(&result->lower)) {
            /* Unlock */
            mr_spinlock_unlock_irqrestore(&__dentry_lock, mask);

            /* Delete the dentry */
            mr_dentry_del(result);
            mr_dentry_put(result);
            result = MR_NULL;
            continue;
        }

        /* Unlock */
        mr_spinlock_unlock_irqrestore(&__dentry_lock, mask);

        /* Can't evict (has children, already retried, or detached); give up */
        mr_dentry_put(result);
        result = MR_NULL;
        break;
    }

    /* Free the component copy */
    str_free(orig_path, istr);
    return result;
}

static mr_dentry_t *dentry_path_mkpath(mr_dentry_t *start, mr_dentry_t *limit,
                                       char *path) {
    mr_dentry_name_t name;
    const char *next_path;
    mr_dentry_t *dentry;
    mr_uint32_t lword;

    do {
        /* Lookup existing prefix (as deep as possible) */
        dentry = dentry_path_lookup(&start, (const char **)&path, limit);
        if (dentry) {
            /* Entire path already exists */
            return dentry;
        }

        /* Extract the next component name: hash it, then split at the
         * separator ('/' or '\0') by temporarily writing '\0' */
        next_path = dentry_name_hash(&name, path, start->ops, &lword);
        lword = (mr_uint32_t)*next_path;
        *((char *)next_path) = '\0';

        /* Create and add the dentry for this component. Path dentries own no
         * inode and use the default ops (NULL) */
        dentry = mr_dentry_create(MR_NULL, "%s", path);
        if (!dentry) {
            return MR_NULL;
        }
        dentry_add(dentry, start);

        /* If there are more components, continue from the new dentry */
        if (((char)lword) != '\0') {
            path = (char *)(next_path + 1);
            start = dentry;
        } else {
            break;
        }
    } while (*path != '\0');
    return dentry;
}

mr_dentry_t *mr_dentry_vmkpath(mr_dentry_t *start, mr_dentry_t *limit,
                               const char *fmt, mr_va_list args) {
    char istr[MR_CFG_FS_DENTRY_NAME_LEN * 2];
    mr_dentry_t *dentry;
    const char *path;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    if (((!start) || (!MR_OBJECT_IS_INITED(start)))
        || (limit && (!MR_OBJECT_IS_INITED(limit)))
        || ((!fmt) || (fmt[0] == '\0'))) {
        return MR_NULL;
    }

    /* Allocate path string */
    ret = str_varg_alloc(fmt, args, &path, istr, sizeof(istr));
    if (ret != 0) {
        return MR_NULL;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__dentry_lock);

    /* Make dentry path */
    dentry = dentry_path_mkpath(start, limit, (char *)path);

    /* Acquire a reference for the caller (inside the lock to avoid races) */
    if (dentry) {
        dentry = mr_dentry_get(dentry);
    }

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__dentry_lock, mask);

    /* Free path */
    str_free(path, istr);
    return dentry;
}

mr_dentry_t *mr_dentry_lookup(mr_dentry_t *start, mr_dentry_t *limit,
                              const char *fmt, ...) {
    mr_dentry_t *dentry;
    mr_va_list args;

    /* Lookup dentry */
    mr_va_start(args, fmt);
    dentry = mr_dentry_vlookup(start, limit, fmt, args);
    mr_va_end(args);
    return dentry;
}

mr_dentry_t *mr_dentry_mkpath(mr_dentry_t *start, mr_dentry_t *limit,
                              const char *fmt, ...) {
    mr_dentry_t *dentry;
    mr_va_list args;

    /* Make dentry path */
    mr_va_start(args, fmt);
    dentry = mr_dentry_vmkpath(start, limit, fmt, args);
    mr_va_end(args);
    return dentry;
}

mr_err_t mr_dentry_instantiate(mr_dentry_t *dentry, struct mr_inode *inode) {
    /* Check parameter */
    MR_ASSERT((dentry != MR_NULL) && MR_OBJECT_IS_INITED(dentry));
    MR_ASSERT((inode != MR_NULL) && MR_OBJECT_IS_INITED(inode));

    /* The dentry must not already have an inode bound */
    if (dentry->inode) {
        return -MR_EBUSY;
    }

    /* Acquire a reference and bind (the dentry owns the reference, released by
     * dentry_obj_release) */
    inode = mr_inode_get(inode);
    if (!inode) {
        return -MR_ENOENT;
    }
    dentry->inode = inode;
    return 0;
}

static mr_dentry_t *dentry_obj_release(mr_object_t *obj) {
    mr_dentry_t *dentry;

    /* Get dentry */
    dentry = MR_CONTAINER_OF(obj, mr_dentry_t, parent);

    /* Notify the file system before tearing down. The dentry is still fully
     * intact here (inode bound, name valid), so the hook can release any FS
     * private data attached via ops->release */
    if (dentry->ops && dentry->ops->release) {
        dentry->ops->release(dentry);
    }

    /* Remove dentry */
    dentry_remove(dentry);

    /* Release bound inode reference (if any). Use ops->iput if the FS wants
     * custom handling, otherwise the default mr_inode_put() */
    if (dentry->inode) {
        if (dentry->ops && dentry->ops->iput) {
            dentry->ops->iput(dentry, dentry->inode);
        } else {
            mr_inode_put(dentry->inode);
        }
        dentry->inode = MR_NULL;
    }

    /* Free dentry name */
    dentry_name_free(dentry);
    return dentry;
}

static void dentry_statik_release(mr_object_t *obj) {
    /* Release dentry */
    dentry_obj_release(obj);
}

static void dentry_dynamic_release(mr_object_t *obj) {
    mr_dentry_t *dentry;

    /* Release dentry */
    dentry = dentry_obj_release(obj);

    /* Free dentry */
    mr_free(dentry);
}
#endif /* defined(MR_USE_FS) */

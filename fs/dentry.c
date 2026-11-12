/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/fs/mr_fs_dentry.h>
#if defined(MR_USE_FS)
#include <mrX/fs/mr_fs_struct.h>
#include <mrX/fs/mr_fs_sblock.h>
#include <mrX/fs/mr_fs_inode.h>
#include <mrX/libc/mr_sprintf.h>
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>

/* Dentry clazzes(static & dynamic) */
static void dentry_statik_release(mr_object_t *obj);
static void dentry_dynamic_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(dentry, dentry_statik_release, dentry_dynamic_release,
                  MR_NULL);

/* Serialize unbacked-tree moves */
static mr_bool_t __dentry_unbacked_moving = MR_FALSE;

/* Prepared dentry move type */
typedef struct dentry_move {
    mr_dentry_t *dentry;
    mr_dentry_t *old_upper;
    mr_dentry_t *upper;
    mr_dentry_t *reservation;
    mr_sblock_t *sb;
    mr_bool_t noop;
} dentry_move_t;

/* Last-word definition */
#define DENTRY_LWORD_DOT2               (0x2e2e)
#define DENTRY_LWORD_DOT                (0x2e)

static const char *dentry_name_hash(mr_dentry_name_t *name, const char *str,
                                    const mr_dentry_ops_t *ops,
                                    mr_uint32_t *lword) {
    mr_uint32_t hash;
    mr_uint8_t ch;

    /* Scan name up to the next '/' or end, computing the last-word for
     * recognizing '.' and '..' */
    for (name->str = str, hash = MR_DENTRY_HASH_SEED, *lword = 0;
         (*str != '\0') && (*str != '/'); str++) {
        ch = (mr_uint8_t)(*str);
        *lword = (*lword << 8) + ch;

        /* Default djb2-xor hash */
        hash = ((hash << 5) + hash) ^ ch;
    }
    name->len = (mr_uint32_t)(str - name->str);

    /* Use ops->hash if provided, otherwise the default */
    if (ops && ops->hash) {
        name->hash = ops->hash(name->str, name->len);
    } else {
        name->hash = hash;
    }
    return str;
}

static char *dentry_str_alloc(mr_uint32_t str_len, char *istr,
                              mr_uint32_t istr_len) {
    /* Allocate string */
    if (str_len < istr_len) {
        /* Use inline string if possible */
        return istr;
    }

    /* Use dynamic string instead */
    return mr_malloc(str_len + 1);
}

static mr_err_t dentry_str_varg_alloc(const char *fmt, mr_va_list args,
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
    *str = dentry_str_alloc(str_len, istr, istr_len);
    if (!(*str)) {
        return -MR_ENOMEM;
    }

    /* Format string */
    mr_vsnprintf((char *)*str, str_len + 1, fmt, args);
    return 0;
}

static void dentry_str_free(const char *str, const char *istr) {
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
     * the FS root dentry */
    for (len = 0; name[len] != '\0'; len++) {
        if ((name[len] == '/') && ((len != 0) || (name[len + 1] != '\0'))) {
            return -MR_EINVAL;
        }
    }
    if ((len == 0) || ((len == 1) && (name[0] == '.'))
        || ((len == 2) && (name[0] == '.') && (name[1] == '.'))) {
        return -MR_EINVAL;
    }

    /* Allocate name */
    s = dentry_str_alloc((mr_uint32_t)len, dentry->iname,
                         sizeof(dentry->iname));
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
    dentry_str_free(dentry->name.str, dentry->iname);
    dentry->name.str = MR_NULL;
}

static mr_err_t dentry_init(mr_dentry_t *dentry, const mr_clazz_t *clazz,
                            const mr_dentry_ops_t *ops, const char *fmt,
                            mr_va_list args) {
    char istr[MR_CFG_FS_INLINE_NAME_LEN * 2];
    const char *str;
    mr_err_t ret;

    /* Allocate string */
    ret = dentry_str_varg_alloc(fmt, args, &str, istr, sizeof(istr));
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
    dentry->mounted = MR_NULL;
    dentry->upper = MR_NULL;
    dentry->ops = ops;
    dentry->inode = MR_NULL;
    dentry->retained = MR_TRUE;
    dentry->state = MR_DENTRY_STATE_ACTIVE;

    /* Init object */
    ret = mr_object_init((mr_object_t *)dentry, clazz);
    if (ret != 0) {
        /* Free name */
        dentry_name_free(dentry);
    }
_exit:
    /* Free string */
    dentry_str_free(str, istr);
    return ret;
}

mr_bool_t mr_dentry_is_root(mr_dentry_t *dentry) {
    /* Check whether dentry is global root */
    return (dentry == mr_fs_struct_root()) ? MR_TRUE : MR_FALSE;
}

mr_err_t mr_dentry_init(mr_dentry_t *dentry, const mr_dentry_ops_t *ops,
                        const char *fmt, ...) {
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(dentry);
    mr_va_list args;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT(dentry != MR_NULL);
    MR_ASSERT((fmt != MR_NULL) && (fmt[0] != '\0'));

    /* Init dentry */
    mr_va_start(args, fmt);
    ret = dentry_init(dentry, clazz, ops, fmt, args);
    mr_va_end(args);
    return ret;
}

mr_dentry_t *mr_dentry_create(const mr_dentry_ops_t *ops, const char *fmt,
                              ...) {
    const mr_clazz_t *clazz = MR_CLAZZES_DYNAMIC(dentry);
    mr_dentry_t *dentry;
    mr_va_list args;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((fmt != MR_NULL) && (fmt[0] != '\0'));

    /* Create dentry */
    dentry = mr_malloc(sizeof(mr_dentry_t));
    if (!dentry) {
        return MR_NULL;
    }

    /* Init dentry */
    mr_va_start(args, fmt);
    ret = dentry_init(dentry, clazz, ops, fmt, args);
    mr_va_end(args);
    if (ret != 0) {
        mr_free(dentry);
        return MR_NULL;
    }
    return dentry;
}

static void dentry_remove(mr_dentry_t *dentry) {
    mr_dentry_t *upper;
    int mask;

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Remove dentry from sibling list */
    mr_list_del(&dentry->sibling);
    upper = dentry->upper;
    dentry->upper = MR_NULL;

    /* Unlock */
    mr_fs_struct_unlock(mask);

    /* Put upper */
    if (upper) {
        mr_dentry_put(upper);
    }
}

mr_err_t mr_dentry_remove(mr_dentry_t *dentry) {
    mr_dentry_t *upper;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT((dentry != MR_NULL) && MR_OBJECT_IS_INITED(dentry));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(dentry, dentry));

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Remove only an active, added dentry */
    if ((dentry->state == MR_DENTRY_STATE_DEAD) || (!dentry->upper)) {
        ret = -MR_ENOENT;
        upper = MR_NULL;
    } else if ((dentry->state != MR_DENTRY_STATE_ACTIVE) || dentry->mounted) {
        ret = -MR_EBUSY;
        upper = MR_NULL;
    } else {
        mr_list_del(&dentry->sibling);
        upper = dentry->upper;
        dentry->upper = MR_NULL;
        ret = 0;
    }

    /* Unlock */
    mr_fs_struct_unlock(mask);

    /* Put upper */
    if (upper) {
        mr_dentry_put(upper);
    }
    return ret;
}

mr_err_t mr_dentry_del(mr_dentry_t *dentry) {
    mr_dentry_t *upper;
    mr_bool_t retained;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT((dentry != MR_NULL) && MR_OBJECT_IS_INITED(dentry));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(dentry, dentry));

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Delete only once */
    if (dentry->mounted || (dentry->state == MR_DENTRY_STATE_UNLINKING)
        || (dentry->state == MR_DENTRY_STATE_MOVING)
        || (dentry->state == MR_DENTRY_STATE_MOUNTING)) {
        retained = MR_FALSE;
        upper = MR_NULL;
        ret = -MR_EBUSY;
    } else if (dentry->state == MR_DENTRY_STATE_DEAD) {
        retained = MR_FALSE;
        upper = MR_NULL;
        ret = -MR_ENOENT;
    } else {
        mr_list_del(&dentry->sibling);
        dentry->state = MR_DENTRY_STATE_DEAD;
        retained = dentry->retained;
        dentry->retained = MR_FALSE;
        upper = dentry->upper;
        dentry->upper = MR_NULL;
        ret = 0;
    }

    /* Unlock */
    mr_fs_struct_unlock(mask);
    if (ret != 0) {
        return ret;
    }

    /* Put upper */
    if (upper) {
        mr_dentry_put(upper);
    }

    /* Drop only a retained creation reference, never a lookup caller's pin */
    if (retained) {
        mr_object_del((mr_object_t *)dentry);
    }
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
    mr_dentry_t *dentry;
    mr_list_t *l;

    /* For each dentry */
    MR_LIST_FOR_EACH(l, &last->lower) {
        dentry = MR_LIST_ENTRY(l, struct mr_dentry, sibling);

        /* Match dentry name */
        if (dentry_name_match(&dentry->name, name, last->ops) != MR_TRUE) {
            continue;
        }

        /* Found */
        return dentry;
    }

    /* Not found */
    return MR_NULL;
}

static void dentry_add(mr_dentry_t *dentry, mr_dentry_t *upper) {
    mr_uint32_t lword;

    /* Bind dentry to upper and acquire a reference for the binding */
    mr_list_add(&upper->lower, &dentry->sibling);
    dentry->upper = mr_dentry_get(upper);

    /* Inherit ops from upper when this dentry has none of its own. This makes
     * a per-mount ops table set on the root dentry cover the whole subtree */
    if (!dentry->ops) {
        dentry->ops = upper->ops;
    }

    /* A child is always looked up with its parent's ops(dentry_lookup hashes
     * the query with the parent's hash and compares with the parent's
     * compare), so the stored hash must come from the parent's ops as well,
     * even when the dentry carries different ops for its own children. The
     * default hash applies when the parent has no custom hash */
    dentry_name_hash(&dentry->name, dentry->name.str, upper->ops, &lword);
}

mr_err_t mr_dentry_add(mr_dentry_t *dentry, mr_dentry_t *upper) {
    mr_dentry_name_t lookup;
    mr_uint32_t lword;
    mr_dentry_t *p;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT((dentry != MR_NULL) && MR_OBJECT_IS_INITED(dentry));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(dentry, dentry));
    MR_ASSERT((upper != MR_NULL) && MR_OBJECT_IS_INITED(upper));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(upper, dentry));
    MR_ASSERT(dentry != upper);

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Check dentry states */
    if ((dentry->state != MR_DENTRY_STATE_ACTIVE)
        || (upper->state != MR_DENTRY_STATE_ACTIVE)) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* Can not add a dentry into its own subtree: walk up from the new parent
     * and reject if the source is an ancestor of it */
    for (p = upper; p; p = p->upper) {
        if (p == dentry) {
            ret = -MR_EPERM;
            goto _exit;
        }
    }

    /* Check whether dentry is added */
    if (MR_DENTRY_IS_ADDED(dentry)) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Check whether dentry is existed. Hash the name with the upper's ops so the
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
    mr_fs_struct_unlock(mask);
    return ret;
}

static mr_err_t dentry_move_prepare(mr_dentry_t *dentry, mr_dentry_t *upper,
                                    const char *name, dentry_move_t *move) {
    mr_sblock_t *sb, *target;
    mr_uint32_t lword;
    mr_dentry_t *p;
    mr_err_t ret;
    int mask;

    /* Init only fields read before a move has been prepared */
    move->old_upper = MR_NULL;
    move->sb = MR_NULL;
    move->noop = MR_FALSE;

    /* Create destination reservation */
    move->reservation = mr_dentry_create(upper->ops, "%s", name);
    if (!move->reservation) {
        return -MR_ENOMEM;
    }

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Check source and destination state */
    if (dentry->mounted || (dentry->state == MR_DENTRY_STATE_MOVING)
        || (dentry->state == MR_DENTRY_STATE_MOUNTING)
        || (upper->state == MR_DENTRY_STATE_MOVING)) {
        ret = -MR_EBUSY;
        goto _exit;
    }
    if ((dentry->state != MR_DENTRY_STATE_ACTIVE)
        || (upper->state != MR_DENTRY_STATE_ACTIVE)
        || (!MR_DENTRY_IS_ADDED(dentry))) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* Source and destination must belong to the same super-block instance */
    sb = dentry->upper->inode ? dentry->upper->inode->sb : MR_NULL;
    target = upper->inode ? upper->inode->sb : MR_NULL;
    if ((sb != target) || (dentry->inode && (dentry->inode->sb != sb))) {
        ret = -MR_EXDEV;
        goto _exit;
    }
    if (sb && (sb->state != MR_SBLOCK_STATE_ACTIVE)) {
        ret = (sb->state == MR_SBLOCK_STATE_OFFLINE) ? -MR_ENODEV : -MR_ENOENT;
        goto _exit;
    }
    if (sb ? sb->move_busy : __dentry_unbacked_moving) {
        ret = -MR_EBUSY;
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

    /* Hash the owned name snapshot with the current parent's ops */
    dentry_name_hash(&move->reservation->name,
                     MR_DENTRY_NAME(move->reservation), upper->ops, &lword);

    /* Same parent and same name: nothing to do. This check must come before
     * the EEXIST check below, otherwise dentry_lookup would find the dentry
     * itself(Still in the parent's children at this point) and wrongly report
     * -MR_EEXIST */
    if ((dentry->upper == upper)
        && (dentry_name_match(&dentry->name, &move->reservation->name,
                              upper->ops))) {
        move->noop = MR_TRUE;
        ret = 0;
        goto _exit;
    }

    /* Reject a name that already exists under the new parent */
    if (dentry_lookup(upper, &move->reservation->name)) {
        ret = -MR_EEXIST;
        goto _exit;
    }

    /* Pin the source parent as of this moment. The backend rename callback
     * and the commit both rely on this snapshot instead of re-reading
     * dentry->upper outside the lock */
    move->old_upper = mr_dentry_get(dentry->upper);
    if (!move->old_upper) {
        ret = -MR_ENOENT;
        goto _exit;
    }
    if (sb) {
        move->sb = mr_sblock_get(sb);
        if (!move->sb) {
            ret = -MR_ENOENT;
            goto _exit;
        }
    }

    /* Reserve destination name and source move. The reservation is a
     * placeholder without inode: it blocks duplicate names, but path lookup
     * and readdir must not expose it */
    dentry_add(move->reservation, upper);
    move->reservation->state = MR_DENTRY_STATE_RESERVED;
    dentry->state = MR_DENTRY_STATE_MOVING;
    if (!sb) {
        __dentry_unbacked_moving = MR_TRUE;
    } else {
        sb->move_busy = MR_TRUE;
    }

    /* Save prepared move */
    move->dentry = dentry;
    move->upper = upper;
    ret = 0;
_exit:
    /* Unlock */
    mr_fs_struct_unlock(mask);

    /* Release unused reservation and pins */
    if ((ret != 0) || move->noop) {
        mr_dentry_del(move->reservation);
        move->reservation = MR_NULL;
        if (move->old_upper) {
            mr_dentry_put(move->old_upper);
            move->old_upper = MR_NULL;
        }
        if (move->sb) {
            mr_sblock_put(move->sb);
            move->sb = MR_NULL;
        }
    }
    return ret;
}

static mr_err_t dentry_move_commit(dentry_move_t *move) {
    mr_dentry_t *reservation_upper, *reservation;
    mr_dentry_t *dentry, *old_upper;
    const char *old_name;
    int mask;

    /* Ignore no-op move */
    if (move->noop) {
        return 0;
    }

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Get prepared objects */
    reservation = move->reservation;
    dentry = move->dentry;

    /* Validate prepared move. The source must still hang under the parent
     * snapshot taken by prepare */
    if ((dentry->state != MR_DENTRY_STATE_MOVING)
        || (dentry->upper != move->old_upper) || (!reservation)
        || (reservation->upper != move->upper)
        || (!MR_DENTRY_IS_ADDED(reservation))) {
        /* Unlock */
        mr_fs_struct_unlock(mask);
        return -MR_EAGAIN;
    }

    /* Detach from the old parent */
    old_upper = dentry->upper;
    mr_list_del(&dentry->sibling);
    dentry->upper = MR_NULL;

    /* Remove destination reservation */
    reservation_upper = reservation->upper;
    mr_list_del(&reservation->sibling);
    reservation->upper = MR_NULL;

    /* Install prepared name */
    old_name = dentry->name.str;
    if (reservation->name.str == reservation->iname) {
        /* Copy the short name and its terminator into the source dentry */
        mr_memcpy(dentry->iname, reservation->iname,
                  reservation->name.len + 1U);
        dentry->name.str = dentry->iname;
    } else {
        /* Transfer the buffer so deleting the reservation can not free it */
        dentry->name.str = reservation->name.str;
        reservation->name.str = reservation->iname;
        reservation->iname[0] = '\0';
        reservation->name.len = 0;
    }

    /* Attach and recompute name metadata using the prepared parent's ops */
    dentry_add(dentry, move->upper);
    dentry->state = MR_DENTRY_STATE_ACTIVE;
    if (!move->sb) {
        __dentry_unbacked_moving = MR_FALSE;
    } else {
        move->sb->move_busy = MR_FALSE;
    }

    /* Unlock */
    mr_fs_struct_unlock(mask);

    /* Free the replaced name and drop namespace references. old_upper is put
     * twice: once for the retired tree binding, once for the prepare pin */
    dentry_str_free(old_name, dentry->iname);
    mr_dentry_put(reservation_upper);
    mr_dentry_del(reservation);
    mr_dentry_put(old_upper);
    mr_dentry_put(move->old_upper);
    move->old_upper = MR_NULL;
    move->reservation = MR_NULL;
    if (move->sb) {
        mr_sblock_put(move->sb);
        move->sb = MR_NULL;
    }
    return 0;
}

static void dentry_move_cancel(dentry_move_t *move) {
    int mask;

    /* Release source reservation */
    if (move->dentry) {
        /* Lock */
        mask = mr_fs_struct_lock();

        /* Restore dentry state */
        if (move->dentry->state == MR_DENTRY_STATE_MOVING) {
            move->dentry->state = MR_DENTRY_STATE_ACTIVE;
        }
        if (!move->sb) {
            __dentry_unbacked_moving = MR_FALSE;
        } else {
            move->sb->move_busy = MR_FALSE;
        }

        /* Unlock */
        mr_fs_struct_unlock(mask);
    }

    /* Release the source parent pin */
    if (move->old_upper) {
        mr_dentry_put(move->old_upper);
        move->old_upper = MR_NULL;
    }

    /* Delete destination reservation */
    if (move->reservation) {
        mr_dentry_del(move->reservation);
        move->reservation = MR_NULL;
    }

    /* Release the move's super-block pin */
    if (move->sb) {
        mr_sblock_put(move->sb);
        move->sb = MR_NULL;
    }
}

mr_err_t mr_dentry_move(mr_dentry_t *dentry, mr_dentry_t *upper,
                        const char *name) {
    dentry_move_t move;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((dentry != MR_NULL) && MR_OBJECT_IS_INITED(dentry));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(dentry, dentry));
    MR_ASSERT((upper != MR_NULL) && MR_OBJECT_IS_INITED(upper));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(upper, dentry));
    MR_ASSERT((name != MR_NULL) && (name[0] != '\0'));

    /* Check whether dentry is moved to itself or has a slash in the name */
    if ((dentry == upper) || mr_strchr(name, '/')) {
        return -MR_EINVAL;
    }

    /* Prepare move */
    ret = dentry_move_prepare(dentry, upper, name, &move);
    if (ret != 0) {
        return ret;
    }

    /* Commit move */
    ret = dentry_move_commit(&move);
    if (ret != 0) {
        dentry_move_cancel(&move);
    }
    return ret;
}

mr_err_t mr_dentry_rename(mr_dentry_t *dentry, mr_dentry_t *upper,
                          const char *name) {
    mr_dentry_t *old_upper;
    dentry_move_t move;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((dentry != MR_NULL) && MR_OBJECT_IS_INITED(dentry));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(dentry, dentry));
    MR_ASSERT((upper != MR_NULL) && MR_OBJECT_IS_INITED(upper));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(upper, dentry));
    MR_ASSERT((name != MR_NULL) && (name[0] != '\0'));

    /* Check whether dentry is moved to itself or has a slash in the name */
    if ((dentry == upper) || mr_strchr(name, '/')) {
        return -MR_EINVAL;
    }

    /* Prepare move. From here the source is MOVING, so no other move can
     * change its parent; the pinned snapshot is the authoritative old parent */
    ret = dentry_move_prepare(dentry, upper, name, &move);
    if ((ret != 0) || move.noop) {
        return ret;
    }

    /* Check whether old and new parent are backed by inodes */
    old_upper = move.old_upper;
    if ((!old_upper->inode) || (!upper->inode)) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* Rename backing node */
    if (old_upper->inode->ops && old_upper->inode->ops->rename) {
        ret = old_upper->inode->ops->rename(old_upper->inode, dentry,
                                            upper->inode, move.reservation);
        if (ret != 0) {
            goto _exit;
        }
    }

    /* Commit dentry move */
    ret = dentry_move_commit(&move);
    if (ret != 0) {
        goto _exit;
    }
    return 0;
_exit:
    dentry_move_cancel(&move);
    return ret;
}

mr_err_t mr_dentry_unlink(mr_dentry_t *dentry) {
    mr_dentry_t *parent;
    mr_bool_t retained;
    mr_inode_t *inode;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT((dentry != MR_NULL) && MR_OBJECT_IS_INITED(dentry));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(dentry, dentry));

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Reserve unlink and pin backing objects */
    if (dentry->mounted || (dentry->state == MR_DENTRY_STATE_MOUNTING)) {
        /* Unlock */
        mr_fs_struct_unlock(mask);
        return -MR_EBUSY;
    }
    if ((dentry->state != MR_DENTRY_STATE_ACTIVE) || (!dentry->upper)
        || (!dentry->inode) || (!MR_DENTRY_IS_ADDED(dentry))) {
        /* Unlock */
        mr_fs_struct_unlock(mask);
        return -MR_ENOENT;
    }
    if ((!dentry->upper->inode)
        || (dentry->upper->inode->type != MR_INODE_DIR)) {
        /* Unlock */
        mr_fs_struct_unlock(mask);
        return -MR_ENOTDIR;
    }
    if ((dentry->inode->type == MR_INODE_DIR)
        && (!mr_list_is_empty(&dentry->lower))) {
        /* Unlock */
        mr_fs_struct_unlock(mask);
        return -MR_ENOTEMPTY;
    }
    dentry->state = MR_DENTRY_STATE_UNLINKING;
    parent = mr_dentry_get(dentry->upper);
    inode = mr_inode_get(dentry->inode);
    if ((!parent) || (!inode)) {
        dentry->state = MR_DENTRY_STATE_ACTIVE;

        /* Unlock */
        mr_fs_struct_unlock(mask);
        if (parent) {
            mr_dentry_put(parent);
        }
        if (inode) {
            mr_inode_put(inode);
        }
        return -MR_ENOENT;
    }

    /* Unlock */
    mr_fs_struct_unlock(mask);

    /* Remove backing entry when the FS provides a callback */
    if (parent->inode->ops && parent->inode->ops->unlink) {
        ret = parent->inode->ops->unlink(parent->inode, MR_DENTRY_NAME(dentry));
        if (ret != 0) {
            /* Lock */
            mask = mr_fs_struct_lock();

            /* Restore dentry state */
            if (dentry->state == MR_DENTRY_STATE_UNLINKING) {
                dentry->state = MR_DENTRY_STATE_ACTIVE;
            }

            /* Unlock */
            mr_fs_struct_unlock(mask);

            /* Drop the namespace and temporary references */
            mr_dentry_put(parent);
            mr_inode_put(inode);
            return ret;
        }
    }

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Commit unlink exactly once */
    if ((dentry->state == MR_DENTRY_STATE_UNLINKING) && (dentry->inode == inode)
        && (dentry->upper == parent)) {
        mr_list_del(&dentry->sibling);
        dentry->upper = MR_NULL;
        dentry->state = MR_DENTRY_STATE_DEAD;
        retained = dentry->retained;
        dentry->retained = MR_FALSE;
        ret = 0;
    } else {
        retained = MR_FALSE;
        ret = -MR_EAGAIN;
    }

    /* Unlock */
    mr_fs_struct_unlock(mask);

    /* Check error */
    if (ret != 0) {
        mr_inode_put(inode);
        mr_dentry_put(parent);
        return ret;
    }

    /* Drop the namespace and temporary references */
    if (retained) {
        mr_object_del((mr_object_t *)dentry);
    }
    mr_dentry_put(parent);
    mr_dentry_put(parent);
    mr_inode_put(inode);
    return 0;
}

mr_dentry_t *mr_dentry_child_get(mr_dentry_t *parent, mr_size_t index) {
    mr_dentry_t *child;
    mr_list_t *l;
    mr_size_t i;
    int mask;

    /* Check parameter */
    MR_ASSERT((parent != MR_NULL) && MR_OBJECT_IS_INITED(parent));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(parent, dentry));

    /* Init result */
    child = MR_NULL;
    i = 0;

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Find the index-th child. Rename reservations are placeholders and are
     * not counted as children */
    MR_LIST_FOR_EACH(l, &parent->lower) {
        child = MR_LIST_ENTRY(l, struct mr_dentry, sibling);
        if (child->state == MR_DENTRY_STATE_RESERVED) {
            child = MR_NULL;
            continue;
        }
        if (i == index) {
            child = mr_dentry_get(child);
            break;
        }
        child = MR_NULL;
        i++;
    }

    /* Unlock */
    mr_fs_struct_unlock(mask);
    return child;
}

mr_bool_t mr_dentry_is_empty(mr_dentry_t *dentry) {
    mr_bool_t empty;
    int mask;

    /* Check parameter */
    MR_ASSERT((dentry != MR_NULL) && MR_OBJECT_IS_INITED(dentry));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(dentry, dentry));

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Check whether the dentry has no children */
    empty = mr_list_is_empty(&dentry->lower);

    /* Unlock */
    mr_fs_struct_unlock(mask);
    return empty;
}

static mr_dentry_t *dentry_path_lookup(mr_dentry_t **last, const char **path,
                                       mr_dentry_t *limit, mr_uint32_t *miss,
                                       mr_err_t *error) {
    mr_dentry_name_t name;
    mr_dentry_t *dentry;
    mr_uint32_t lword;
    const char *str;

    /* Init error */
    *error = 0;

    /* Enter a file-system mounted on the starting dentry */
    while ((*last)->mounted) {
        *last = (*last)->mounted;
    }

    for (str = *path; *str != '\0';) {
        /* Every further component or slash requires a directory inode */
        if ((*last)->inode && ((*last)->inode->type != MR_INODE_DIR)) {
            *error = -MR_ENOTDIR;
            return MR_NULL;
        }

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
            case DENTRY_LWORD_DOT2: {
                /* Handle '..' */
                if ((*last == limit) || ((*last)->upper == MR_NULL)) {
                    /* Already at root */
                    continue;
                }
                dentry = (*last)->upper;
                break;
            }
            case DENTRY_LWORD_DOT: {
                /* Handle '.' */
                continue;
            }
            default: {
                /* Handle 'other'. A rename reservation only holds the name; it
                 * is invisible to path resolution */
                dentry = dentry_lookup(*last, &name);
                if ((!dentry) || (dentry->state == MR_DENTRY_STATE_RESERVED)) {
                    /* Not found */
                    *miss = name.len;
                    return MR_NULL;
                }
                break;
            }
        }

        /* Enter a mounted file-system before resolving the next component */
        while (dentry->mounted) {
            dentry = dentry->mounted;
        }

        /* Update last */
        *last = dentry;
    }
    return *last;
}

static mr_err_t dentry_fs_lookup(mr_dentry_t *parent, const char *name,
                                 mr_uint32_t len, mr_dentry_t **result) {
    char istr[MR_CFG_FS_INLINE_NAME_LEN * 2];
    mr_dentry_t *existing, *child;
    mr_inode_t *dir_inode, *inode;
    mr_dentry_name_t lookup;
    mr_uint32_t lword;
    mr_sblock_t *sb;
    mr_err_t ret;
    char *str;
    int mask;

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Pin the parent inode while the dentry binding is stable */
    dir_inode = parent->inode ? mr_inode_get(parent->inode) : MR_NULL;

    /* Unlock */
    mr_fs_struct_unlock(mask);

    /* Check whether the parent is a directory */
    if (!dir_inode) {
        return -MR_ENOENT;
    }
    if (dir_inode->type != MR_INODE_DIR) {
        ret = -MR_ENOTDIR;
        goto _exit_inode;
    }

    /* Pre-cached file systems leave lookup NULL */
    if ((!dir_inode->ops) || (!dir_inode->ops->lookup)) {
        ret = -MR_ENOENT;
        goto _exit_inode;
    }

    /* Protect the backing FS across lookup and cache publication */
    sb = mr_sblock_get(dir_inode->sb);
    if (!sb) {
        ret = -MR_ENOENT;
        goto _exit_inode;
    }
    ret = mr_sblock_op_enter(sb);
    if (ret != 0) {
        goto _exit_sb;
    }

    /* Copy the component (name points into the path string, not NUL-terminated
     * at this boundary) so it can be passed to the FS hook */
    str = dentry_str_alloc(len, istr, sizeof(istr));
    if (!str) {
        ret = -MR_ENOMEM;
        goto _exit_op;
    }
    mr_strncpy(str, name, len);
    str[len] = '\0';

    /* Lookup returns a temporary inode reference */
    inode = MR_NULL;
    ret = dir_inode->ops->lookup(dir_inode, str, &inode);
    if (ret != 0) {
        goto _exit;
    }
    if ((!inode) || inode->sb) {
        ret = -MR_EINVAL;
        goto _exit;
    }

    /* Inherit the super-block */
    inode->sb = mr_sblock_get(sb);
    if (!inode->sb) {
        ret = -MR_ENOENT;
        goto _exit;
    }

    /* FS dentries inherit the super-block's name operations */
    child = mr_dentry_create(inode->sb->dops, "%s", str);
    if (!child) {
        ret = -MR_ENOMEM;
        goto _exit;
    }

    /* Bind the inode and attach to parent */
    ret = mr_dentry_instantiate(child, inode);
    if (ret != 0) {
        /* Delete child */
        mr_dentry_del(child);
        goto _exit;
    }

    /* Lookup entries have no namespace-owned creation reference */
    child->retained = MR_FALSE;
    ret = mr_dentry_add(child, parent);
    if (ret == -MR_EEXIST) {
        /* Another thread may have won the race; reuse its entry. Hash with
         * parent's ops so the lookup matches how siblings were hashed */
        dentry_name_hash(&lookup, str, parent->ops, &lword);

        /* Lock */
        mask = mr_fs_struct_lock();

        /* Lookup the existing entry. A rename reservation holding the name
         * is not a resolvable entry yet */
        existing = dentry_lookup(parent, &lookup);
        if (existing && (existing->state == MR_DENTRY_STATE_RESERVED)) {
            existing = MR_NULL;
            ret = -MR_ENOENT;
        } else {
            existing = mr_dentry_get(existing);
            ret = existing ? 0 : -MR_EAGAIN;
        }

        /* Unlock */
        mr_fs_struct_unlock(mask);

        /* Replace the existing entry */
        mr_dentry_put(child);
        child = existing;
    } else if (ret == 0) {
        /* Transfer the create reference to the caller. The directory list
         * does not pin lookup-filled dentries; they live only while a handle,
         * cwd, path walk, or child still holds them */
    } else {
        /* Delete failed reservation */
        mr_dentry_put(child);
    }

    /* Return resolved dentry */
    if (ret == 0) {
        *result = child;
    }
_exit:
    /* Put the temporary inode reference */
    if (inode) {
        mr_inode_put(inode);
    }

    /* Free the component copy */
    dentry_str_free(str, istr);
_exit_op:
    mr_sblock_op_exit(sb);
_exit_sb:
    mr_sblock_put(sb);
_exit_inode:
    mr_inode_put(dir_inode);
    return ret;
}

mr_err_t mr_dentry_lookup(mr_dentry_t *start, mr_dentry_t *limit,
                          const char *path, mr_dentry_t **result) {
    mr_dentry_t *resolved, *cur, *parent, *held;
    mr_uint32_t len;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT((start != MR_NULL) && MR_OBJECT_IS_INITED(start));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(start, dentry));
    MR_ASSERT((limit == MR_NULL) || MR_OBJECT_IS_INITED(limit));
    MR_ASSERT((limit == MR_NULL) || MR_OBJECT_IS_CLAZZES(limit, dentry));
    MR_ASSERT((path != MR_NULL) && (path[0] != '\0'));
    MR_ASSERT(result != MR_NULL);

    /* Walk the cache and ask the file-system to fill each miss */
    for (cur = start, held = MR_NULL; MR_TRUE; path += len) {
        /* Lock */
        mask = mr_fs_struct_lock();

        /* Resolve the cached prefix */
        resolved = dentry_path_lookup(&cur, &path, limit, &len, &ret);
        if (resolved || (ret != 0)) {
            if (resolved) {
                resolved = mr_dentry_get(resolved);
                ret = resolved ? 0 : -MR_ENOENT;
            }

            /* Unlock */
            mr_fs_struct_unlock(mask);
            break;
        }

        /* Pin the deepest cached parent across the file-system callback */
        parent = mr_dentry_get(cur);

        /* Unlock */
        mr_fs_struct_unlock(mask);

        /* Release the previous intermediate dentry */
        if (held) {
            mr_dentry_put(held);
            held = MR_NULL;
        }
        if (!parent) {
            ret = -MR_ENOENT;
            break;
        }

        /* Resolve the missing component through the file-system */
        ret = dentry_fs_lookup(parent, path, len, &resolved);
        mr_dentry_put(parent);
        if (ret != 0) {
            break;
        }

        /* Continue from the newly cached dentry */
        held = resolved;
        cur = resolved;
    }

    /* Release intermediate dentry */
    if (held) {
        mr_dentry_put(held);
    }

    /* Return resolved dentry */
    if (ret == 0) {
        *result = resolved;
    }
    return ret;
}

mr_err_t mr_dentry_instantiate(mr_dentry_t *dentry, struct mr_inode *inode) {
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT((dentry != MR_NULL) && MR_OBJECT_IS_INITED(dentry));
    MR_ASSERT((inode != MR_NULL) && MR_OBJECT_IS_INITED(inode));

    /* Lock */
    mask = mr_fs_struct_lock();

    /* The dentry must be active without an inode bound */
    if (dentry->state != MR_DENTRY_STATE_ACTIVE) {
        ret = -MR_ENOENT;
        goto _exit;
    }
    if (dentry->inode) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Acquire a reference and bind (the dentry owns the reference, released by
     * dentry_obj_release) */
    inode = mr_inode_get(inode);
    if (!inode) {
        ret = -MR_ENOENT;
        goto _exit;
    }
    dentry->inode = inode;
    ret = 0;
_exit:
    /* Unlock */
    mr_fs_struct_unlock(mask);
    return ret;
}

mr_err_t mr_dentry_uninstantiate(mr_dentry_t *dentry) {
    mr_inode_t *inode;
    int mask;

    /* Check parameter */
    MR_ASSERT((dentry != MR_NULL) && MR_OBJECT_IS_INITED(dentry));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(dentry, dentry));

    /* Lock */
    mask = mr_fs_struct_lock();

    /* Detach inode from an active dentry */
    if (dentry->state != MR_DENTRY_STATE_ACTIVE) {
        /* Unlock */
        mr_fs_struct_unlock(mask);
        return -MR_ENOENT;
    }
    inode = dentry->inode;
    if (!inode) {
        /* Unlock */
        mr_fs_struct_unlock(mask);
        return -MR_ENOENT;
    }
    dentry->inode = MR_NULL;

    /* Unlock */
    mr_fs_struct_unlock(mask);

    /* Put inode */
    mr_inode_put(inode);
    return 0;
}

static mr_dentry_t *dentry_obj_release(mr_object_t *obj) {
    mr_dentry_t *dentry;

    /* Get dentry */
    dentry = MR_CONTAINER_OF(obj, mr_dentry_t, parent);
    MR_ASSERT(dentry->mounted == MR_NULL);

    /* Remove dentry */
    dentry_remove(dentry);

    /* Release bound inode reference */
    if (dentry->inode) {
        mr_inode_put(dentry->inode);
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

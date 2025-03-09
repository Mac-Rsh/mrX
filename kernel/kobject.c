/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <kernel/mr_kobject.h>
#include <kernel/mr_kspinlock.h>
#include <libc/mr_malloc.h>
#include <libc/mr_printf.h>
#include <libc/mr_string.h>

/* Kobject lock */
static mr_kspinlock_t klock = MR_KSPINLOCK_INIT();
/* Kobject kset */
static mr_kset_t kroot = MR_KSET_INIT(&kroot, MR_NULL);

/* Last-word definition */
#define LWORD_DOT                       (0x2e)
#define LWORD_DOT2                      (0x2e2e)

void mr_ktype_init(mr_ktype_t *type, const char *name,
                   void (*release)(mr_kobject_t *kobj), mr_kattr_t **attrs) {
    /* Check arguments */
    if (!type) {
        return;
    }

    /* Init ktype */
    type->name = name;
    type->release = release;
    type->attrs = attrs;
}

MR_INLINE void kname_init(mr_kname_t *kname) {
    kname->str = MR_NULL;
    kname->len = 0;
    kname->hash = 0;
}

void mr_kobject_init(mr_kobject_t *kobj, mr_ktype_t *type) {
    /* Check arguments */
    if ((!kobj) || MR_KOBJECT_IS_INITED(kobj)) {
        return;
    }

    /* Init kobject */
    kname_init(&kobj->name);
    kobj->parent = kobj;
    kobj->type = type;
    mr_klist_init(&kobj->entry);
    mr_klist_init(&kobj->list);
    mr_kref_init(&kobj->ref);
    kobj->magic = MR_KOBJECT_MAGIC;
}

MR_INLINE mr_err_t path_alloc_varg(const char *fmt, mr_va_list args,
                                   const char **path, char *ipath,
                                   mr_size_t ipath_len) {
    int path_len;

    /* Get path length */
    path_len = mr_vsnprintf(MR_NULL, 0, fmt, args);
    if (path_len < 0) {
        return -MR_EINVAL;
    }

    /* Allocate path */
    if (path_len < ipath_len) {
        /* Use inline path if possible */
        *path = ipath;
    } else {
        *path = mr_malloc(path_len + 1);
        if (!(*path)) {
            return -MR_ENOMEM;
        }
    }

    /* Format path */
    mr_vsnprintf((char *)*path, path_len + 1, fmt, args);
    return 0;
}

MR_INLINE const char *kname_hash(mr_kname_t *kname, const char *name,
                                 mr_uint32_t *lword) {
    mr_uint32_t hash;

    for (kname->str = name, hash = 5381, *lword = 0;
         (*name != '\0') && (*name != '/'); name++) {
        hash = ((hash << 5) + hash) ^ (*name);
        *lword = (*lword << 8) + (*name);
    }
    kname->len = name - kname->str;
    kname->hash = hash;
    return name;
}

MR_INLINE mr_bool_t kname_match(mr_kname_t *kn1, mr_kname_t *kn2) {
    if (kn1->hash != kn2->hash) {
        return MR_FALSE;
    }
    if (kn1->len != kn2->len) {
        return MR_FALSE;
    }
    return mr_strncmp(kn1->str, kn2->str, kn1->len) == 0;
}

MR_INLINE mr_kobject_t *kobject_lookup_kname(mr_kobject_t *last,
                                             mr_kname_t *kname) {
    mr_kobject_t *kobj;
    mr_klist_t *l;

    /* For each kobject */
    MR_KLIST_FOR_EACH(l, &last->list) {
        kobj = MR_KLIST_ENTRY(l, mr_kobject_t, entry);

        /* Match object name */
        if (!kname_match(&kobj->name, kname)) {
            continue;
        }

        /* Found */
        return kobj;
    }

    /* Not found */
    return MR_NULL;
}

MR_INLINE mr_kobject_t *kobject_lookup_path(mr_kobject_t **last,
                                            const char **path,
                                            mr_kobject_t *root) {
    mr_kobject_t *kobj;
    mr_uint32_t lword;
    mr_kname_t kname;
    const char *name;

    /* Skip leading '/' */
    for (name = (*path); *name != '\0'; name++) {
        if (*name == '/') {
            continue;
        }
        break;
    }

    while (*name != '\0') {
        /* Skip leading '/' */
        while (*name == '/') {
            name++;
        }

        /* Update path */
        *path = name;

        /* Lookup kobject for name */
        name = kname_hash(&kname, name, &lword);
        switch (lword) {
            case LWORD_DOT: {
                /* Handle '.' */
                continue;
            }
            case LWORD_DOT2: {
                /* Handle '..' */
                if (*last == root) {
                    /* Already at kroot */
                    continue;
                }
                kobj = (*last)->parent;
                break;
            }
            default: {
                /* Handle 'other' */
                kobj = kobject_lookup_kname(*last, &kname);
                if (!kobj) {
                    /* Not found */
                    return MR_NULL;
                }
                break;
            }
        }

        /* Update last */
        *last = kobj;
    }
    return *last;
}

MR_INLINE mr_err_t kobject_name_alloc(mr_kobject_t *kobj, const char *name) {
    mr_uint32_t lword;
    mr_size_t size;
    char *s;

    /* Name cannot contain '/', it's a path */
    if (mr_strchr(name, '/')) {
        return -MR_EINVAL;
    }

    /* Get name length */
    size = mr_strlen(name);

    /* Allocate name */
    if (size < sizeof(kobj->iname)) {
        /* Use inline name if possible */
        s = kobj->iname;
    } else {
        s = mr_malloc(size + 1);
        if (!s) {
            return -MR_ENOMEM;
        }
    }

    /* Copy name */
    mr_strncpy(s, name, size);
    s[size] = '\0';
    kname_hash(&kobj->name, s, &lword);
    return 0;
}

MR_INLINE void path_free(const char *path, char *ipath) {
    if (path == ipath) {
        /* Inline path not need to free */
        return;
    }
    mr_free((char *)path);
}

MR_INLINE mr_err_t kobject_add(mr_kobject_t *kobj, mr_kobject_t *parent,
                               const char *path) {
    mr_err_t ret;

    /* Lookup path in parent */
    if (kobject_lookup_path(&parent, &path, parent)) {
        return -MR_EEXIST;
    }

    /* Allocate name */
    ret = kobject_name_alloc(kobj, path);
    if (ret != 0) {
        return ret;
    }

    /* Add kobject to parent */
    kobj->parent = parent;
    mr_klist_add(&parent->list, &kobj->entry);
    return 0;
}

MR_INLINE mr_err_t kobject_add_varg(mr_kobject_t *kobj, mr_kobject_t *parent,
                                    const char *fmt, mr_va_list args) {
    char ipath[MR_CFG_KOBJECT_INAME_SIZE * 2];
    const char *path;
    mr_err_t ret;
    int mask;

    /* Get parent */
    parent = mr_kobject_get(parent);
    if (!parent) {
        return -MR_EINVAL;
    }

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Already added */
    if (!mr_klist_is_empty(&kobj->entry)) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Allocate path */
    ret = path_alloc_varg(fmt, args, &path, ipath, sizeof(ipath));
    if (ret != 0) {
        goto _exit;
    }

    /* Add kobject to parent */
    ret = kobject_add(kobj, parent, path);

    /* Free path */
    path_free(path, ipath);
_exit:
    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);

    /* Put parent */
    if (ret != 0) {
        mr_kobject_put(parent);
    }
    return ret;
}

mr_err_t mr_kobject_add(mr_kobject_t *kobj, mr_kobject_t *parent,
                        const char *fmt, ...) {
    mr_va_list args;
    mr_err_t ret;

    /* Check arguments */
    if ((!kobj) || (!MR_KOBJECT_IS_INITED(kobj)) || (!parent)
        || (!MR_KOBJECT_IS_INITED(parent)) || (!fmt)) {
        return -MR_EINVAL;
    }

    /* Add kobject to parent */
    mr_va_start(args, fmt);
    ret = kobject_add_varg(kobj, parent, fmt, args);
    mr_va_end(args);
    return ret;
}

MR_INLINE void kobject_del(mr_kobject_t *kobj) {
    int mask;

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Check kobject is deleted */
    if (!mr_klist_is_empty(&kobj->entry)) {
        /* Remove kobject from parent */
        mr_klist_del(&kobj->entry);
    }

    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);
}

void mr_kobject_del(mr_kobject_t *kobj) {
    /* Check arguments */
    if ((!kobj) || (!MR_KOBJECT_IS_INITED(kobj))) {
        return;
    }

    /* Delete kobject */
    kobject_del(kobj);

    /* Put kobject */
    mr_kobject_put(kobj);
}

MR_INLINE mr_kobject_t *kobject_lookup_varg(mr_kobject_t *parent,
                                            const char *fmt, mr_va_list args) {
    char ipath[MR_CFG_KOBJECT_INAME_SIZE * 2];
    const char *path, *path2;
    mr_kobject_t *kobj;
    mr_err_t ret;
    int mask;

    /* Get parent */
    parent = mr_kobject_get(parent);
    if (!parent) {
        return MR_NULL;
    }

    /* Lock */
    mask = mr_kspinlock_lock_irqsave(&klock);

    /* Allocate path */
    ret = path_alloc_varg(fmt, args, &path, ipath, sizeof(ipath));
    if (ret != 0) {
        kobj = MR_NULL;
        goto _exit;
    } else {
        /* Save path for free */
        path2 = path;
    }

    /* Lookup kobject for path */
    kobj = kobject_lookup_path(&parent, &path, parent);

    /* Free path */
    path_free(path2, ipath);
_exit:
    /* Unlock */
    mr_kspinlock_unlock_irqrestore(&klock, mask);

    /* Put parent */
    mr_kobject_put(parent);
    return kobj;
}

mr_kobject_t *mr_kobject_lookup(mr_kobject_t *parent, const char *fmt, ...) {
    mr_kobject_t *kobj;
    mr_va_list args;

    /* Check arguments */
    if ((!parent) || (!MR_KOBJECT_IS_INITED(parent)) || (!fmt)) {
        return MR_NULL;
    }

    /* Lookup kobject from parent */
    mr_va_start(args, fmt);
    kobj = kobject_lookup_varg(parent, fmt, args);
    mr_va_end(args);
    return kobj;
}

mr_kobject_t *mr_kobject_get(mr_kobject_t *kobj) {
    /* Check arguments */
    if ((!kobj) || (!MR_KOBJECT_IS_INITED(kobj))) {
        return MR_NULL;
    }

    /* Get kobject */
    mr_kref_get(&kobj->ref);
    return kobj;
}

MR_INLINE void kobject_release(mr_kobject_t *kobj) {
    /* Mark as released */
    kobj->magic = ~MR_KOBJECT_MAGIC;

    /* Release kobject */
    if ((kobj->type) && (kobj->type->release)) {
        kobj->type->release(kobj);
    }
}

MR_INLINE void kobject_release_ref(mr_kref_t *ref) {
    mr_kobject_t *kobj, *parent;
    const char *name;

    /* Get kobject */
    kobj = MR_CONTAINER_OF(ref, mr_kobject_t, ref);

    /* If kobject not added to parent, not need to release */
    parent = (kobj->parent != kobj) ? kobj->parent : MR_NULL;

    /* If name is inline, not need to free */
    name = (kobj->name.str != kobj->iname) ? kobj->name.str : MR_NULL;

    /* Remove kobject from parent */
    kobject_del(kobj);

    /* Release kobject */
    kobject_release(kobj);

    /* Free name */
    if (name) {
        mr_free((void *)name);
    }

    /* Put parent */
    mr_kobject_put(parent);
}

void mr_kobject_put(mr_kobject_t *kobj) {
    /* Check arguments */
    if ((!kobj) || (!MR_KOBJECT_IS_INITED(kobj))) {
        return;
    }

    /* Put kobject */
    mr_kref_put(&kobj->ref, kobject_release_ref);
}

MR_INLINE mr_kattr_t *kobject_attr_lookup(mr_kobject_t *kobj,
                                          const char *name) {
    mr_kattr_t **attrs;
    mr_size_t i;

    /* Check type and attrs */
    if ((!kobj->type) || (!kobj->type->attrs)) {
        return MR_NULL;
    }

    /* Lookup attribute */
    attrs = kobj->type->attrs;
    for (i = 0; attrs[i]; i++) {
        /* Match name */
        if (mr_strcmp(name, attrs[i]->name)) {
            continue;
        }

        /* Found */
        return attrs[i];
    }

    /* Not found */
    return MR_NULL;
}

mr_ssize_t mr_kobject_attr_show(mr_kobject_t *kobj, const char *name, char *buf,
                                mr_size_t size) {
    mr_kattr_t *attr;

    /* Check arguments */
    if ((!kobj) || (!MR_KOBJECT_IS_INITED(kobj)) || (!name)
        || ((!buf) && (size > 0))) {
        return -MR_EINVAL;
    }

    /* Lookup attribute */
    attr = kobject_attr_lookup(kobj, name);
    if (!attr) {
        return -MR_ENOENT;
    }

    /* Show attribute */
    return attr->show(kobj, attr, buf, size);
}

mr_ssize_t mr_kobject_attr_store(mr_kobject_t *kobj, const char *name,
                                 const char *buf, mr_size_t size) {
    mr_kattr_t *attr;

    /* Check arguments */
    if ((!kobj) || (!MR_KOBJECT_IS_INITED(kobj)) || (!name)
        || ((!buf) && (size > 0))) {
        return -MR_EINVAL;
    }

    /* Lookup attribute */
    attr = kobject_attr_lookup(kobj, name);
    if (!attr) {
        return -MR_ENOENT;
    }

    /* Store attribute */
    return attr->store(kobj, attr, buf, size);
}

void mr_kset_init(mr_kset_t *kset, mr_ktype_t *type) {
    /* Init kset */
    mr_kobject_init((mr_kobject_t *)kset, type);
}

mr_err_t mr_kset_register(mr_kset_t *kset, const char *name) {
    /* Add kset to kroot */
    return mr_kobject_add((mr_kobject_t *)kset, (mr_kobject_t *)&kroot, name);
}

mr_kset_t *mr_kset_find(const char *name) {
    /* Lookup kset in kroot */
    return (mr_kset_t *)mr_kobject_lookup((mr_kobject_t *)&kroot, name);
}

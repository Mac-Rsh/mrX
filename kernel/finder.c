/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/kernel/mr_finder.h>
#if defined(MR_USE_FINDER)
#include <mrX/kernel/mr_spinlock.h>
#include <mrX/libc/mr_sprintf.h>
#include <mrX/libc/mr_malloc.h>
#include <mrX/libc/mr_string.h>

/* Finder clazzes(static & dynamic) */
static void finder_statik_release(mr_object_t *obj);
static void finder_dynamic_release(mr_object_t *obj);
MR_CLAZZES_DEFINE(finder, finder_statik_release, finder_dynamic_release,
                  MR_NULL);

/* Finder lock */
static mr_spinlock_t __finder_lock = MR_SPINLOCK_INIT();

/* Last-word definition */
#define LWORD_DOT2                      (0x2e2e)
#define LWORD_DOT                       (0x2e)

static const char *finder_name_hash(mr_finder_name_t *name, const char *str,
                                    mr_uint32_t *lword) {
    mr_uint32_t hash;

    /* Hash name */
    for (name->str = str, hash = 5381, *lword = 0;
         (*str != '\0') && (*str != '/'); str++) {
        hash = ((hash << 5) + hash) ^ ((mr_uint8_t)(*str));
        *lword = (*lword << 8) + ((mr_uint8_t)(*str));
    }
    name->len = (mr_uint32_t)(str - name->str);
    name->hash = hash;
    return str;
}

static mr_err_t finder_name_alloc(mr_finder_t *finder, const char *name) {
    mr_uint32_t lword;
    mr_size_t len;
    char *s;

    /* Name cannot contain '/', it's a string */
    if (mr_strchr(name, '/') != MR_NULL) {
        return -MR_EINVAL;
    }

    /* Get name length */
    len = mr_strlen(name);

    /* Allocate name */
    if (len < sizeof(finder->iname)) {
        /* Use inline name if possible */
        s = finder->iname;
    } else {
        /* Use dynamic name instead */
        s = mr_malloc(len + 1);
        if (s == MR_NULL) {
            return -MR_ENOMEM;
        }
    }

    /* Copy and hash name */
    mr_strncpy(s, name, len);
    s[len] = '\0';
    finder_name_hash(&finder->name, s, &lword);
    return 0;
}

static void finder_name_free(mr_finder_t *finder) {
    /* Check inline name */
    if (finder->name.str != finder->iname) {
        /* Not inline name need to free */
        mr_free((void *)finder->name.str);
    }
    finder->name.str = MR_NULL;
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
    } else {
        /* Save string length */
        str_len = (mr_uint32_t)ret;
    }

    /* Allocate string */
    if (str_len < istr_len) {
        /* Use inline string if possible */
        *str = istr;
    } else {
        /* Use dynamic string instead */
        *str = mr_malloc(str_len + 1);
        if (*str == MR_NULL) {
            return -MR_ENOMEM;
        }
    }

    /* Format string */
    mr_vsnprintf((char *)*str, str_len + 1, fmt, args);
    return 0;
}

static void str_varg_free(const char *str, const char *istr) {
    /* Check inline string */
    if (str == istr) {
        /* Inline string not need to free */
        return;
    }

    /* Free string */
    mr_free((char *)str);
}

static mr_err_t finder_init(mr_finder_t *finder, const mr_clazz_t *clazz,
                            const char *fmt, mr_va_list args) {
    char istr[MR_CFG_FINDER_INAME_LEN * 2];
    const char *str;
    mr_err_t ret;

    /* Allocate string */
    ret = str_varg_alloc(fmt, args, &str, istr, sizeof(istr));
    if (ret != 0) {
        return ret;
    }

    /* Init finder */
    ret = finder_name_alloc(finder, str);
    if (ret != 0) {
        goto _exit;
    }
    mr_list_init(&finder->sibling);
    mr_list_init(&finder->lower);
    finder->upper = MR_NULL;

    /* Init object */
    ret = mr_object_init((mr_object_t *)finder, clazz);
    if (ret != 0) {
        /* Free name */
        finder_name_free(finder);
    }
_exit:
    /* Free string */
    str_varg_free(str, istr);
    return ret;
}

mr_err_t mr_finder_vinit(mr_finder_t *finder, const char *fmt,
                         mr_va_list args) {
    const mr_clazzes_t *clazzes = MR_CLAZZES_FIND(finder);
    const mr_clazz_t *clazz = MR_CLAZZES_STATIK(clazzes);

    /* Check parameter */
    MR_ASSERT((finder != MR_NULL) && (!MR_OBJECT_IS_INITED(finder)));
    MR_ASSERT((fmt != MR_NULL) && (fmt[0] != '\0'));

    /* Init finder */
    return finder_init(finder, clazz, fmt, args);
}

mr_finder_t *mr_finder_vcreate(const char *fmt, mr_va_list args) {
    const mr_clazzes_t *clazzes = MR_CLAZZES_FIND(finder);
    const mr_clazz_t *clazz = MR_CLAZZES_DYNAMIC(clazzes);
    mr_finder_t *finder;
    mr_err_t ret;

    /* Check parameter */
    MR_ASSERT((fmt != MR_NULL) && (fmt[0] != '\0'));

    /* Create finder */
    finder = mr_malloc(sizeof(mr_finder_t));
    if (!finder) {
        return MR_NULL;
    }

    /* Init finder */
    ret = finder_init(finder, clazz, fmt, args);
    if (ret != 0) {
        mr_free(finder);
        return MR_NULL;
    }
    return finder;
}

mr_err_t mr_finder_init(mr_finder_t *finder, const char *fmt, ...) {
    mr_va_list args;
    mr_err_t ret;

    /* Init finder */
    mr_va_start(args, fmt);
    ret = mr_finder_vinit(finder, fmt, args);
    mr_va_end(args);
    return ret;
}

mr_finder_t *mr_finder_create(const char *fmt, ...) {
    mr_va_list args;
    mr_finder_t *finder;

    /* Create finder */
    mr_va_start(args, fmt);
    finder = mr_finder_vcreate(fmt, args);
    mr_va_end(args);
    return finder;
}

static void finder_remove(mr_finder_t *finder) {
    mr_finder_t *upper;
    int mask;

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__finder_lock);

    /* Remove finder from sibling list */
    mr_list_del(&finder->sibling);
    upper = finder->upper;
    finder->upper = MR_NULL;

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__finder_lock, mask);

    /* Put upper */
    if (upper != MR_NULL) {
        mr_finder_put(upper);
    }
}

mr_err_t mr_finder_remove(mr_finder_t *finder) {
    /* Check parameter */
    MR_ASSERT((finder != MR_NULL) && MR_OBJECT_IS_INITED(finder));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(finder, finder));

    /* Remove finder */
    finder_remove(finder);
    return 0;
}

mr_err_t mr_finder_del(mr_finder_t *finder) {
    /* Check parameter */
    MR_ASSERT((finder != MR_NULL) && MR_OBJECT_IS_INITED(finder));

    /* Remove finder */
    finder_remove(finder);

    /* Delete object */
    mr_object_del((mr_object_t *)finder);
    return 0;
}

static mr_bool_t finder_name_match(mr_finder_name_t *n1, mr_finder_name_t *n2) {
    /* Check hash and length */
    if ((n1->hash != n2->hash) || (n1->len != n2->len)) {
        return MR_FALSE;
    }

    /* Check string(hash possible conflicts) */
    return mr_strncmp(n1->str, n2->str, n1->len) == 0;
}

static mr_finder_t *finder_name_lookup(mr_finder_t *last,
                                       mr_finder_name_t *name) {
    mr_finder_t *finder;
    mr_list_t *l;

    /* For each finder */
    MR_LIST_FOR_EACH(l, &last->lower) {
        finder = MR_LIST_ENTRY(l, struct mr_finder, sibling);

        /* Match finder name */
        if (finder_name_match(&finder->name, name) != MR_TRUE) {
            continue;
        }

        /* Found */
        return finder;
    }

    /* Not found */
    return MR_NULL;
}

static void finder_add(mr_finder_t *finder, mr_finder_t *upper) {
    /* Add finder to upper */
    mr_list_add(&upper->lower, &finder->sibling);
    finder->upper = upper;
}

mr_err_t mr_finder_add(mr_finder_t *finder, mr_finder_t *upper) {
    mr_err_t ret;
    int mask;

    /* Check parameter */
    MR_ASSERT((finder != MR_NULL) && MR_OBJECT_IS_INITED(finder));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(finder, finder));
    MR_ASSERT((upper != MR_NULL) && MR_OBJECT_IS_INITED(upper));
    MR_ASSERT(MR_OBJECT_IS_CLAZZES(upper, finder));
    MR_ASSERT(finder != upper);

    /* Get upper */
    upper = mr_finder_get(upper);
    if (!upper) {
        return -MR_EINVAL;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__finder_lock);

    /* Prevent direct recursion */
    if (upper->upper == finder) {
        ret = -MR_EPERM;
        goto _exit;
    }

    /* Check if finder is added */
    if (MR_FINDER_IS_ADDED(finder)) {
        ret = -MR_EBUSY;
        goto _exit;
    }

    /* Check if finder name is existed */
    if (finder_name_lookup(upper, &finder->name)) {
        ret = -MR_EEXIST;
        goto _exit;
    }

    /* Add finder to upper */
    finder_add(finder, upper);
    ret = 0;
_exit:
    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__finder_lock, mask);

    /* Upper will be referenced once, if successful */
    if (ret != 0) {
        mr_finder_put(upper);
    }
    return ret;
}

static mr_finder_t *finder_path_lookup(mr_finder_t **last, const char **path,
                                       mr_finder_t *limit) {
    mr_finder_name_t name;
    mr_finder_t *finder;
    mr_uint32_t lword;
    const char *str;

    /* Skip leading '/' */
    for (str = (*path); *str != '\0'; str++) {
        if (*str == '/') {
            continue;
        }
        break;
    }

    while (*str != '\0') {
        /* Skip leading '/' */
        while (*str == '/') {
            str++;
        }

        /* Update path */
        *path = str;

        /* Lookup finder for name */
        str = finder_name_hash(&name, str, &lword);
        switch (lword) {
            case LWORD_DOT2: {
                /* Handle '..' */
                if ((*last == limit) || ((*last)->upper == MR_NULL)) {
                    /* Already at root */
                    continue;
                }
                finder = (*last)->upper;
                break;
            }
            case LWORD_DOT: {
                /* Handle '.' */
                continue;
            }
            default: {
                /* Handle 'other' */
                finder = finder_name_lookup(*last, &name);
                if (finder == MR_NULL) {
                    /* Not found */
                    return MR_NULL;
                }
                break;
            }
        }

        /* Update last */
        *last = finder;
    }
    return *last;
}

mr_finder_t *mr_finder_vlookup(mr_finder_t *start, mr_finder_t *limit,
                               const char *fmt, mr_va_list args) {
    char istr[MR_CFG_FINDER_INAME_LEN * 2];
    const char *path;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    if ((start == MR_NULL) || (!MR_OBJECT_IS_INITED(start))
        || ((limit != MR_NULL) && (!MR_OBJECT_IS_INITED(limit)))
        || (fmt == MR_NULL) || (fmt[0] == '\0')) {
        return MR_NULL;
    }

    /* Allocate path string */
    ret = str_varg_alloc(fmt, args, &path, istr, sizeof(istr));
    if (ret != 0) {
        return MR_NULL;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__finder_lock);

    /* Lookup finder */
    start = finder_path_lookup(&start, &path, limit);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__finder_lock, mask);

    /* Free path */
    str_varg_free(path, istr);
    return start;
}

static mr_finder_t *finder_path_mkdir(mr_finder_t *start, mr_finder_t *limit,
                                      char *path) {
    mr_finder_name_t name;
    const char *next_path;
    mr_finder_t *finder;
    mr_uint32_t lword;

    do {
        /* Lookup valid path */
        finder = finder_path_lookup(&start, (const char **)&path, limit);
        if (finder != MR_NULL) {
            /* Path already exists */
            return finder;
        }

        /* Get current name */
        next_path = finder_name_hash(&name, path, &lword);
        lword = (mr_uint32_t)*next_path;
        *((char *)next_path) = '\0';

        /* Create and add finder */
        finder = mr_finder_create("%s", path);
        if (finder == MR_NULL) {
            return MR_NULL;
        }
        finder_add(finder, start);

        /* Continue lookup from current finder */
        if ((char)lword != '\0') {
            path = (char *)(next_path + 1);
            start = finder;
        } else {
            break;
        }
    } while (*path != '\0');
    return finder;
}

mr_finder_t *mr_finder_vmkdir(mr_finder_t *start, mr_finder_t *limit,
                              const char *fmt, mr_va_list args) {
    char istr[MR_CFG_FINDER_INAME_LEN * 2];
    mr_finder_t *finder;
    const char *path;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    if ((start == MR_NULL) || (!MR_OBJECT_IS_INITED(start))
        || ((limit != MR_NULL) && (!MR_OBJECT_IS_INITED(limit)))
        || (fmt == MR_NULL) || (fmt[0] == '\0')) {
        return MR_NULL;
    }

    /* Allocate path string */
    ret = str_varg_alloc(fmt, args, &path, istr, sizeof(istr));
    if (ret != 0) {
        return MR_NULL;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__finder_lock);

    /* Make directory */
    finder = finder_path_mkdir(start, limit, (char *)path);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__finder_lock, mask);

    /* Free path */
    str_varg_free(path, istr);
    return finder;
}

mr_err_t mr_finder_vmkdir_add(mr_finder_t *start, mr_finder_t *limit,
                              mr_finder_t *finder, const char *fmt,
                              mr_va_list args) {
    char istr[MR_CFG_FINDER_INAME_LEN * 2];
    mr_finder_t *upper;
    const char *path;
    mr_err_t ret;
    int mask;

    /* Check parameter */
    if ((start == MR_NULL) || (!MR_OBJECT_IS_INITED(start))
        || ((limit != MR_NULL) && (!MR_OBJECT_IS_INITED(limit)))
        || (finder == MR_NULL) || (!MR_OBJECT_IS_INITED(finder))
        || (fmt == MR_NULL) || (fmt[0] == '\0')) {
        return -MR_EINVAL;
    }

    /* Allocate path string */
    ret = str_varg_alloc(fmt, args, &path, istr, sizeof(istr));
    if (ret != 0) {
        return ret;
    }

    /* Lock */
    mask = mr_spinlock_lock_irqsave(&__finder_lock);

    /* Make directory and get upper reference */
    upper = finder_path_mkdir(start, limit, (char *)path);
    upper = mr_finder_get(upper);

    /* Unlock */
    mr_spinlock_unlock_irqrestore(&__finder_lock, mask);

    /* Free path */
    str_varg_free(path, istr);

    /* Check if upper is valid */
    if (upper == MR_NULL) {
        return -MR_ENOMEM;
    }

    /* Add finder to upper */
    ret = mr_finder_add(finder, upper);

    /* Put upper */
    mr_finder_put(upper);
    return ret;
}

mr_finder_t *mr_finder_lookup(mr_finder_t *start, mr_finder_t *limit,
                              const char *fmt, ...) {
    mr_finder_t *finder;
    mr_va_list args;

    /* Lookup finder */
    mr_va_start(args, fmt);
    finder = mr_finder_vlookup(start, limit, fmt, args);
    mr_va_end(args);
    return finder;
}

mr_finder_t *mr_finder_mkdir(mr_finder_t *start, mr_finder_t *limit,
                             const char *fmt, ...) {
    mr_finder_t *finder;
    mr_va_list args;

    /* Make directory */
    mr_va_start(args, fmt);
    finder = mr_finder_vmkdir(start, limit, fmt, args);
    mr_va_end(args);
    return finder;
}

mr_err_t mr_finder_mkdir_add(mr_finder_t *start, mr_finder_t *limit,
                             mr_finder_t *finder, const char *fmt, ...) {
    mr_va_list args;
    mr_err_t ret;

    /* Make directory and add finder */
    mr_va_start(args, fmt);
    ret = mr_finder_vmkdir_add(start, limit, finder, fmt, args);
    mr_va_end(args);
    return ret;
}

static mr_finder_t *finder_obj_release(mr_object_t *obj) {
    mr_finder_t *finder;

    /* Get finder */
    finder = MR_CONTAINER_OF(obj, mr_finder_t, parent);

    /* Remove finder */
    finder_remove(finder);

    /* Free finder name */
    finder_name_free(finder);
    return finder;
}

static void finder_statik_release(mr_object_t *obj) {
    /* Release finder */
    finder_obj_release(obj);
}

static void finder_dynamic_release(mr_object_t *obj) {
    mr_finder_t *finder;

    /* Release finder */
    finder = finder_obj_release(obj);

    /* Free finder */
    mr_free(finder);
}
#endif /* defined(MR_USE_FINDER) */

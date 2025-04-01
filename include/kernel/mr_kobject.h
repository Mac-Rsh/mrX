/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_KOBJECT_H__
#define __MR_KOBJECT_H__

#include <kernel/mr_klist.h>
#include <kernel/mr_kref.h>
#include <libc/mr_errno.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Kobject
 * @{
 */

/* Kobject magic number */
#define MR_KOBJECT_MAGIC                (0x6d722d58U)

struct mr_kobject;

/* Kname type */
typedef struct mr_kname {
    const char *str;
    mr_uint32_t len;
    mr_uint32_t hash;
} mr_kname_t;

/* Kattr type */
typedef struct mr_kattr {
    const char *name;
    mr_ssize_t (*show)(struct mr_kobject *kobj, struct mr_kattr *attr,
                       char *buf, mr_size_t size);
    mr_ssize_t (*store)(struct mr_kobject *kobj, struct mr_kattr *attr,
                        const char *buf, mr_size_t size);
} mr_kattr_t;

/* Kobject release type */
typedef void(mr_kobject_release_t)(struct mr_kobject *kobj);

/* Ktype type */
typedef struct mr_ktype {
    const char *name;
    mr_kobject_release_t *release;
    mr_kattr_t **attrs;
} mr_ktype_t;

/* Kobject type */
typedef struct mr_kobject {
    mr_uint32_t magic;
    mr_kname_t name;
#if !defined(MR_CFG_KOBJECT_INAME_SIZE)
#define MR_CFG_KOBJECT_INAME_SIZE       (8)
#endif /* !defined(MR_CFG_KOBJECT_INAME_SIZE) */
    char iname[MR_CFG_KOBJECT_INAME_SIZE];
    struct mr_kobject *parent;
    mr_ktype_t *type;
    mr_klist_t entry;
    mr_klist_t list;
    mr_kref_t ref;
} mr_kobject_t;

/* Kset type */
typedef struct mr_kset {
    mr_kobject_t parent;
} mr_kset_t;

/**
 * @brief This macro function initializes a kattr.
 *
 * @param _name The name.
 * @param _show The show function.
 * @param _store The store function.
 */
#define MR_KATTR_INIT(_name, _show, _store)                                    \
    {.name = (_name), .show = (_show), .store = (_store)}

/**
 * @brief This macro function initializes a ktype.
 *
 * @param _name The name.
 * @param _release The release function.
 * @param _attrs The attributes.
 */
#define MR_KTYPE_INIT(_name, _release, _attrs)                                 \
    {.name = (_name), .release = (_release), .attrs = (_attrs)}

/**
 * @brief This function initializes a ktype.
 *
 * @param type The ktype.
 * @param name The name.
 * @param release The release function.
 * @param attrs The attributes.
 */
void mr_ktype_init(mr_ktype_t *type, const char *name,
                   mr_kobject_release_t *release, mr_kattr_t **attrs);

/**
 * @brief This macro function checks if a kobject is initialized.
 *
 * @param _kobj The kobject.
 * @return MR_TRUE if the kobject is initialized, MR_FALSE otherwise.
 */
#define MR_KOBJECT_IS_INITED(_kobj)                                            \
    (((mr_kobject_t *)(_kobj))->magic == MR_KOBJECT_MAGIC)

/**
 * @brief This macro function initializes a kobject.
 *
 * @param _kobj The kobject.
 * @param _type The ktype.
 */
#define MR_KOBJECT_INIT(_kobj, _type)                                          \
    {                                                                          \
        .magic = MR_KOBJECT_MAGIC,                                             \
        .name = {.str = MR_NULL, .len = 0, .hash = 0},                         \
        .parent = (_kobj),                                                     \
        .type = (_type),                                                       \
        .entry = MR_KLIST_INIT(&(_kobj)->entry),                               \
        .list = MR_KLIST_INIT(&(_kobj)->list),                                 \
        .ref = MR_KREF_INIT()                                                  \
    }

/**
 * @brief This function initializes a kobject.
 *
 * @param kobj The kobject.
 * @param type The ktype.
 */
void mr_kobject_init(mr_kobject_t *kobj, mr_ktype_t *type);

/**
 * @brief This function adds a kobject.
 *
 * @param kobj The kobject.
 * @param parent The parent kobject.
 * @param fmt The format string(path).
 * @param ... The format arguments.
 * @returns 0 on success, a negative error code on failure.
 */
mr_err_t mr_kobject_add(mr_kobject_t *kobj, mr_kobject_t *parent,
                        const char *fmt, ...);

/**
 * @brief This macro function checks if a kobject is released.
 *
 * @param _kobj The kobject.
 * @return MR_TRUE if the kobject is released, MR_FALSE otherwise.
 */
#define MR_KOBJECT_IS_RELEASED(_kobj)                                          \
    (((mr_kobject_t *)(_kobj))->magic == (~MR_KOBJECT_MAGIC))

/**
 * @brief This function deletes a kobject.
 *
 * @param kobj The kobject.
 *
 * @note Deleting doesn't free it, it just makes it invisible.
 */
void mr_kobject_del(mr_kobject_t *kobj);

/**
 * @brief This function looks up a kobject.
 *
 * @param parent The parent kobject.
 * @param fmt The format string(path).
 * @param ... The format arguments.
 * @return The kobject on success, MR_NULL on failure.
 */
mr_kobject_t *mr_kobject_lookup(mr_kobject_t *parent, const char *fmt, ...);

/**
 * @brief This function gets a kobject.
 *
 * @param kobj The kobject.
 * @returns The kobject on success, MR_NULL on failure.
 */
mr_kobject_t *mr_kobject_get(mr_kobject_t *kobj);

/**
 * @brief This function puts a kobject.
 *
 * @param kobj The kobject.
 */
void mr_kobject_put(mr_kobject_t *kobj);

/**
 * @brief This function gets the value of an attribute.
 *
 * @param kobj The kobject.
 * @param name The name of the attribute.
 * @param buf The buffer to store the value.
 * @param size The size of the buffer.
 * @return The length of the value on success, a negative error code on
 * failure.
 *
 * @note Set buf to MR_NULL to get the length of the value.
 */
mr_ssize_t mr_kobject_attr_show(mr_kobject_t *kobj, const char *name, char *buf,
                                mr_size_t size);

/**
 * @brief This function sets the value of an attribute.
 *
 * @param kobj The kobject.
 * @param name The name of the attribute.
 * @param buf The buffer to store the value.
 * @param size The size of the buffer.
 * @return The length of the value on success, a negative error code on
 * failure.
 *
 * @note Set buf to MR_NULL to get the length of the value.
 */
mr_ssize_t mr_kobject_attr_store(mr_kobject_t *kobj, const char *name,
                                 const char *buf, mr_size_t size);

/**
 * @brief This macro function gets the name of a kobject.
 *
 * @param _kobj The kobject.
 * @return The name of the kobject.
 */
#define MR_KOBJECT_NAME(_kobj)          (((mr_kobject_t *)(_kobj))->name.str)

/**
 * @brief This macro function gets the length of a kobject name.
 *
 * @param _kobj The kobject.
 * @return The length of the kobject name.
 *
 * @note The name length includes the null terminator('\0').
 */
#define MR_KOBJECT_NAME_LEN(_kobj)      (((mr_kobject_t *)(_kobj))->name.len)

/**
 * @brief This macro function gets the parent of a kobject.
 *
 * @param _kobj The kobject.
 * @return The parent kobject on success, MR_NULL on failure.
 */
#define MR_KOBJECT_PARENT(_kobj)                                               \
    (((mr_kobject_t *)(_kobj))->parent != ((mr_kobject_t *)(_kobj))            \
         ? ((mr_kobject_t *)(_kobj))->parent                                   \
         : MR_NULL)

/**
 * @brief This macro function gets the type of kobject.
 *
 * @param _kobj The kobject.
 * @return The type name on success, MR_NULL on failure.
 */
#define MR_KOBJECT_TYPE(_kobj)                                                 \
    (((mr_kobject_t *)(_kobj))->type ? ((mr_kobject_t *)(_kobj))->type->name   \
                                     : MR_NULL)

/**
 * @brief This macro function initializes a kset.
 *
 * @param _kset The kset.
 * @param _type The ktype.
 */
#define MR_KSET_INIT(_kset, _type)                                             \
    {.parent = MR_KOBJECT_INIT(&(_kset)->parent, (_type))}

/**
 * @brief This function initializes a kset.
 *
 * @param kset The kset.
 * @param type The ktype.
 */
void mr_kset_init(mr_kset_t *kset, mr_ktype_t *type);

/**
 * @brief This function registers a kset.
 *
 * @param kset The kset.
 * @param name The name.
 * @returns 0 on success, a negative error code on failure.
 */
mr_err_t mr_kset_register(mr_kset_t *kset, const char *name);

/**
 * @brief This function finds a kset.
 *
 * @param name The name.
 * @returns The kset on success, MR_NULL on failure.
 */
mr_kset_t *mr_kset_find(const char *name);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_KOBJECT_H__ */

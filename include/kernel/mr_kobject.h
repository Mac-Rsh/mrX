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
#define MR_KOBJECT_MAGIC                (0x6d722d58)

struct mr_kobject;

/* Kname type */
typedef struct mr_kname {
    const char *str;
    mr_size_t len;
    mr_uint32_t hash;
} mr_kname_t;

/* Ktype type */
typedef struct mr_ktype {
    void (*release)(struct mr_kobject *kobj);
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
 * @brief This macro function initializes a ktype.
 *
 * @param _release The release function.
 */
#define MR_KTYPE_INIT(_release)                                                \
    { .release = (_release) }

/**
 * @brief This function initializes a ktype.
 *
 * @param type The ktype.
 * @param release The release function.
 */
void mr_ktype_init(mr_ktype_t *type, void (*release)(mr_kobject_t *kobj));

/**
 * @brief This macro function initializes a kobject.
 *
 * @param _kobj The kobject.
 * @param _type The ktype.
 */
#define MR_KOBJECT_INIT(_kobj, _type)                                          \
    {                                                                          \
        .magic = MR_KOBJECT_MAGIC,                                             \
        .name = {.str = (_kobj)->iname, .len = 0, .hash = 0},                  \
        .parent = (_kobj), .type = (_type),                                    \
        .entry = MR_KLIST_INIT(&(_kobj)->entry),                               \
        .list = MR_KLIST_INIT(&(_kobj)->list), .ref = MR_KREF_INIT()           \
    }

/**
 * @brief This macro function checks if a kobject is initialized.
 *
 * @param _kobj The kobject.
 * @return MR_TRUE if the kobject is initialized, MR_FALSE otherwise.
 */
#define MR_KOBJECT_IS_INITED(_kobj)                                            \
    (((mr_kobject_t *)(_kobj))->magic == MR_KOBJECT_MAGIC)

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
 * @returns 0 on success, or a negative error code on failure.
 */
mr_err_t mr_kobject_add(mr_kobject_t *kobj, mr_kobject_t *parent,
                        const char *fmt, ...);

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
 * @return The kobject on success, or MR_NULL on failure.
 */
mr_kobject_t *mr_kobject_lookup(mr_kobject_t *parent, const char *fmt, ...);

/**
 * @brief This function gets a kobject.
 *
 * @param kobj The kobject.
 * @returns The kobject on success, or MR_NULL on failure.
 */
mr_kobject_t *mr_kobject_get(mr_kobject_t *kobj);

/**
 * @brief This function puts a kobject.
 *
 * @param kobj The kobject.
 */
void mr_kobject_put(mr_kobject_t *kobj);

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
 * @return The parent of the kobject.
 */
#define MR_KOBJECT_PARENT(_kobj)        (((mr_kobject_t *)(_kobj))->parent)

/**
 * @brief This macro function initializes a kset.
 *
 * @param _kset The kset.
 * @param _type The ktype.
 */
#define MR_KSET_INIT(_kset, _type)                                             \
    { .parent = MR_KOBJECT_INIT(&(_kset)->parent, (_type)) }

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
 * @returns 0 on success, or a negative error code on failure.
 */
mr_err_t mr_kset_register(mr_kset_t *kset, const char *name);

/**
 * @brief This function finds a kset.
 *
 * @param name The name.
 * @returns The kset on success, or MR_NULL on failure.
 */
mr_kset_t *mr_kset_find(const char *name);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_KOBJECT_H__ */

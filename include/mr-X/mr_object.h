/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_OBJECT_H__
#define __MR_OBJECT_H__

#include <mr-X/mr_list.h>
#include <mr-X/mr_ref.h>
#include <libc/mr_assert.h>
#include <libc/mr_errno.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Object
 * @{
 */

struct mr_object;

/* Object magic number */
#define MR_OBJECT_MAGIC                 (0x6d722d58U)

/* Object release type */
typedef void(mr_object_release_t)(struct mr_object *obj);

/* Attribute type */
typedef struct mr_attr {
    const char *name;
    mr_ssize_t (*show)(struct mr_object *obj, const struct mr_attr *attr,
                       char *buf, mr_size_t size);
    mr_ssize_t (*store)(struct mr_object *obj, const struct mr_attr *attr,
                        const char *buf, mr_size_t size);
} mr_attr_t;

/* Class type */
typedef struct mr_class {
    const char *name;
    const mr_attr_t **attrs;
    mr_object_release_t *release;
} mr_class_t;

/* Object type */
typedef struct mr_object {
    volatile mr_uint32_t magic;
    const mr_class_t *class;
    mr_ref_t ref;
} mr_object_t;

/**
 * @brief This macro function initializes an attribute.
 *
 * @param _name The name.
 * @param _show The show function.
 * @param _store The store function.
 */
#define MR_ATTR_INIT(_name, _show, _store)                                     \
    { .name = (_name), .show = (_show), .store = (_store) }

/**
 * @brief This macro function initializes a class.
 *
 * @param _name The name.
 * @param _attrs The attributes.
 * @param _release The release function.
 */
#define MR_CLASS_INIT(_name, _attrs, _release)                                 \
    { .name = (_name), .attrs = (_attrs), .release = (_release) }

/**
 * @brief This function initializes a class.
 *
 * @param class The class.
 * @param name The name.
 * @param attrs The attributes.
 * @param release The release function.
 */
MR_INLINE void mr_class_init(mr_class_t *class, const char *name,
                             const mr_attr_t **attrs,
                             mr_object_release_t *release) {
    /* Init class */
    class->name = name;
    class->attrs = attrs;
    class->release = release;
}

/**
 * @brief This macro function initializes an object.
 *
 * @param _obj The object.
 * @param _class The class.
 */
#define MR_OBJECT_INIT(_obj, _class)                                           \
    { .magic = MR_OBJECT_MAGIC, .class = (_class), .ref = MR_REF_INIT() }

/**
 * @brief This function initializes an object.
 *
 * @param obj The object.
 * @param class The class.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_object_init(mr_object_t *obj, const mr_class_t *class);

/**
 * @brief This macro function checks if an object is initialized.
 *
 * @param _obj The object.
 */
#define MR_OBJECT_IS_INITED(_obj)                                              \
    (((mr_object_t *)(_obj))->magic == MR_OBJECT_MAGIC)

/**
 * @brief This macro function checks if an object is of a class.
 *
 * @param _obj The object.
 * @param _class The class.
 */
#define MR_OBJECT_CLASS_IS(_obj, _class)                                       \
    (((mr_object_t *)(_obj))->class == (_class))

/**
 * @brief This macro function checks if an object is of one of the classes.
 *
 * @param _obj The object.
 * @param _class1 The first class.
 * @param _class2 The second class.
 */
#define MR_OBJECT_CLASS_IS_OR(_obj, _class1, _class2)                          \
    (MR_OBJECT_CLASS_IS(_obj, _class1) || MR_OBJECT_CLASS_IS(_obj, _class2))

/**
 * @brief This function deletes an object.
 *
 * @param obj The object.
 */
void mr_object_del(mr_object_t *obj);

/**
 * @brief This function gets an object.
 *
 * @param obj The object.
 * @return The object on success, MR_NULL on failure.
 */
mr_object_t *mr_object_get(mr_object_t *obj);

/**
 * @brief This function puts an object.
 *
 * @param obj The object.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
mr_bool_t mr_object_put(mr_object_t *obj);

/**
 * @brief This function shows an attribute.
 *
 * @param obj The object.
 * @param name The attribute name.
 * @param buf The buffer.
 * @param size The buffer size.
 * @return The actual size on success, a negative error code on failure.
 */
mr_ssize_t mr_object_attr_show(mr_object_t *obj, const char *name, char *buf,
                               mr_size_t size);

/**
 * @brief This function stores an attribute.
 *
 * @param obj The object.
 * @param name The attribute name.
 * @param buf The buffer.
 * @param size The buffer size.
 * @return The actual size on success, a negative error code on failure.
 */
mr_ssize_t mr_object_attr_store(mr_object_t *obj, const char *name,
                                const char *buf, mr_size_t size);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_OBJECT_H__ */

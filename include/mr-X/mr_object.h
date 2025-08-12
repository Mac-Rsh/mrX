/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_OBJECT_H__
#define __MR_OBJECT_H__

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

/* Object release type */
typedef void(mr_object_release_t)(struct mr_object *obj);

/* Clazz type(which is 'class', renamed to avoid C/C++ keyword) */
typedef struct mr_clazz {
    const char *name;
    mr_object_release_t *release;
} mr_clazz_t;

/* Object type */
typedef struct mr_object {
    mr_ref_t magic_ref;
    const mr_clazz_t *clazz;
} mr_object_t;

/**
 * @brief This macro function initializes a clazz.
 *
 * @param _name The name.
 * @param _release The release function.
 */
#define MR_CLAZZ_INIT(_name, _release)                                         \
    { .name = (_name), .release = (_release) }

/**
 * @brief This function initializes a clazz.
 *
 * @param clazz The clazz.
 * @param name The name.
 * @param release The release function.
 */
MR_INLINE void mr_clazz_init(mr_clazz_t *clazz, const char *name,
                             mr_object_release_t *release) {
    /* Init clazz */
    clazz->name = name;
    clazz->release = release;
}

/**
 * @brief This macro function initializes an object.
 *
 * @param _clazz The clazz.
 */
#define MR_OBJECT_INIT(_clazz)                                                 \
    { .magic_ref = MR_REF_INIT(), .clazz = (_clazz) }

/**
 * @brief This function initializes an object.
 *
 * @param obj The object.
 * @param clazz The clazz.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_object_init(mr_object_t *obj, const mr_clazz_t *clazz);

/**
 * @brief This macro function checks if an object is initialized.
 *
 * @param _obj The object.
 * @return MR_TRUE on initialized, MR_FALSE otherwise.
 */
#define MR_OBJECT_IS_INITED(_obj)                                              \
    (MR_REF_IS_INITED(((mr_object_t *)(_obj))->magic_ref))

/**
 * @brief This macro function checks if an object is of a clazz.
 *
 * @param _obj The object.
 * @param _clazz The clazz.
 * @return MR_TRUE on success, MR_FALSE otherwise.
 */
#define MR_OBJECT_CLAZZ_IS(_obj, _clazz)                                       \
    (((mr_object_t *)(_obj))->clazz == (_clazz))

/**
 * @brief This macro function checks if an object is of one of the clazzes.
 *
 * @param _obj The object.
 * @param _clazz1 The first clazz.
 * @param _clazz2 The second clazz.
 * @return MR_TRUE on success, MR_FALSE otherwise.
 */
#define MR_OBJECT_CLAZZ_IS_OR(_obj, _clazz1, _clazz2)                          \
    (MR_OBJECT_CLAZZ_IS(_obj, _clazz1) || MR_OBJECT_CLAZZ_IS(_obj, _clazz2))

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

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_OBJECT_H__ */

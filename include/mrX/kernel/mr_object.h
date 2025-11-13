/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_OBJECT_H__
#define __MR_OBJECT_H__

#include <mrX/kernel/mr_service.h>
#include <mrX/kernel/mr_ref.h>
#include <mrX/libc/mr_errno.h>

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

/* Clazzes type */
typedef struct mr_clazzes {
    const mr_clazz_t statik;
    const mr_clazz_t dynamic;
} mr_clazzes_t;

/* Object type */
typedef struct mr_object {
    mr_ref_t magic_ref;
    const mr_clazz_t *clazz;
} mr_object_t;

/**
 * @brief This macro function initializes clazz.
 *
 * @param _name The clazz name.
 * @param _release The release function.
 */
#define MR_CLAZZ_INIT(_name, _release)  {.name = (_name), .release = (_release)}

/**
 * @brief This function initializes clazz.
 *
 * @param clazz The clazz.
 * @param name The clazz name.
 * @param release The release function.
 */
MR_INLINE void mr_clazz_init(mr_clazz_t *clazz, const char *name,
                             mr_object_release_t *release) {
    /* Check parameter */
    if (!clazz) {
        return;
    }

    /* Init clazz */
    clazz->name = name;
    clazz->release = release;
}

/**
 * @brief This macro function initializes clazzes.
 *
 * @param _name The clazz name.
 * @param _statik_release The static release function.
 * @param _dynamic_release The dynamic release function.
 */
#define MR_CLAZZES_INIT(_name, _statik_release, _dynamic_release)              \
    {.statik = MR_CLAZZ_INIT((_name), (_statik_release)),                      \
     .dynamic = MR_CLAZZ_INIT((_name), (_dynamic_release))}

/**
 * @brief This macro function defines clazzes.
 *
 * @param _name The clazz name.
 * @param _statik_release The static release function.
 * @param _dynamic_release The dynamic release function.
 */
#define MR_CLAZZES_DEFINE(_name, _statik_release, _dynamic_release)            \
    const mr_clazzes_t MR_CAT(__mr_clazzes_, _name)                            \
        = MR_CLAZZES_INIT(MR_STR(_name), (_statik_release), (_dynamic_release))

/**
 * @brief This macro function gets the static clazz of clazzes.
 *
 * @param _clazzes The clazzes.
 * @return The static clazz.
 */
#define MR_CLAZZES_STATIK(_clazzes)     (&(_clazzes)->statik)

/**
 * @brief This macro function gets the dynamic clazz of clazzes.
 *
 * @param _clazzes The clazzes.
 * @return The dynamic clazz.
 */
#define MR_CLAZZES_DYNAMIC(_clazzes)    (&(_clazzes)->dynamic)

/**
 * @brief This macro function exports clazzes.
 *
 * @param _name The clazz name.
 */
#define MR_CLAZZES_EXPORT(_name)                                               \
    extern const mr_clazzes_t MR_CAT(__mr_clazzes_, _name)

/**
 * @brief This macro function finds clazzes.
 *
 * @param _name The clazz name.
 * @return The clazzes.
 */
#define MR_CLAZZES_FIND(_name)          (&MR_CAT(__mr_clazzes_, _name))

/**
 * @brief This macro function initializes an object.
 *
 * @param _clazz The clazz clazz.
 */
#define MR_OBJECT_INIT(_clazz)                                                 \
    {.magic_ref = MR_REF_INIT(), .clazz = (_clazz)}

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
 * @brief This macro function gets the clazz of an object.
 *
 * @param _obj The object.
 * @return The clazz.
 */
#define MR_OBJECT_CLAZZ(_obj)           (((mr_object_t *)(_obj))->clazz)

/**
 * @brief This macro function checks if an object is of clazzes.
 *
 * @param _obj The object.
 * @param _name The clazzes name.
 * @return MR_TRUE on success, MR_FALSE otherwise.
 */
#define MR_OBJECT_IS_CLAZZES(_obj, _name)                                      \
    ((MR_OBJECT_CLAZZ(_obj) == MR_CLAZZES_STATIK(MR_CLAZZES_FIND(_name)))      \
     || (MR_OBJECT_CLAZZ(_obj) == MR_CLAZZES_DYNAMIC(MR_CLAZZES_FIND(_name))))

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

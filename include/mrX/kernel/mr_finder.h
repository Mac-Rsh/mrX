/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_FINDER_H__
#define __MR_FINDER_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_FINDER)
#include <mrX/kernel/mr_object.h>
#include <mrX/kernel/mr_list.h>
#endif /* defined(MR_USE_FINDER) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Finder
 * @{
 */

#if defined(MR_USE_FINDER)
/* Finder name type */
typedef struct mr_finder_name {
    const char *str;
    mr_uint32_t hash;
    mr_uint32_t len;
} mr_finder_name_t;

/* Finder type */
MR_CLAZZES_EXPORT(finder);
typedef struct mr_finder {
    mr_object_t parent;
#if !defined(MR_CFG_FINDER_INAME_LEN)
#define MR_CFG_FINDER_INAME_LEN        (4)
#endif /* defined(MR_CFG_FINDER_INAME_LEN) */
    char iname[MR_CFG_FINDER_INAME_LEN];
    mr_finder_name_t name;
    struct mr_finder *upper;
    mr_list_t sibling, lower;
    mr_object_t *target;
} mr_finder_t;

/**
 * @brief This function initializes a finder.
 *
 * @param finder The finder.
 * @param fmt The finder name format.
 * @param ... The format arguments.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Finder names must not be empty, "." or "..".
 */
mr_err_t mr_finder_init(mr_finder_t *finder, const char *fmt, ...);

/**
 * @brief This function creates a finder.
 *
 * @param fmt The finder name format.
 * @param ... The format arguments.
 * @return The finder on success, MR_NULL on failure.
 *
 * @note Finder names must not be empty, "." or "..".
 */
mr_finder_t *mr_finder_create(const char *fmt, ...);

/**
 * @brief This macro function gets a finder name.
 *
 * @param finder The finder.
 * @return The finder name.
 */
#define MR_FINDER_NAME(_finder)         ((_finder)->name.str)

/**
 * @brief This macro function gets a finder name length.
 *
 * @param finder The finder.
 * @return The finder name length.
 *
 * @note The name length does not include the '\0'.
 */
#define MR_FINDER_NAME_LEN(_finder)     ((_finder)->name.len)

/**
 * @brief This function deletes a finder.
 *
 * @param finder The finder.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_finder_del(mr_finder_t *finder);

/**
 * @brief This function binds an object to a finder.
 *
 * @param finder The finder.
 * @param target The target object.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_finder_bind(mr_finder_t *finder, mr_object_t *target);

/**
 * @brief This macro function gets a finder target.
 *
 * @param finder The finder.
 * @return The finder target.
 */
#define MR_FINDER_TARGET(_finder)       ((_finder)->target)

/**
 * @brief This function adds a finder to an upper finder.
 *
 * @param finder The finder.
 * @param upper The upper finder.
 * @return 0 on success, a negative error code on failure.
 */
mr_err_t mr_finder_add(mr_finder_t *finder, mr_finder_t *upper);

/**
 * @brief This macro function checks if a finder is added.
 *
 * @param finder The finder.
 * @return MR_TRUE if the finder is added, MR_FALSE otherwise.
 */
#define MR_FINDER_IS_ADDED(_finder)     (!mr_list_is_empty(&(_finder)->sibling))

/**
 * @brief This function looks up a finder from a path.
 *
 * @param start The start finder.
 * @param limit The lookup limit.
 * @param fmt The path format.
 * @param ... The format arguments.
 * @return The finder on success, MR_NULL on failure.
 *
 * @note Paths are relative to start.
 * @note Call mr_finder_get() if the result needs to be held.
 */
mr_finder_t *mr_finder_lookup(mr_finder_t *start, mr_finder_t *limit,
                              const char *fmt, ...);

/**
 * @brief This function makes finders from a path.
 *
 * @param start The start finder.
 * @param limit The lookup limit.
 * @param fmt The path format.
 * @param ... The format arguments.
 * @return The finder on success, MR_NULL on failure.
 *
 * @note Paths are relative to start.
 * @note Call mr_finder_get() if the result needs to be held.
 */
mr_finder_t *mr_finder_mkdir(mr_finder_t *start, mr_finder_t *limit,
                             const char *fmt, ...);

/**
 * @brief This function makes finders from a path and adds a finder.
 *
 * @param start The start finder.
 * @param limit The lookup limit.
 * @param finder The finder.
 * @param fmt The path format.
 * @param ... The format arguments.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Paths are relative to start.
 */
mr_err_t mr_finder_mkdir_add(mr_finder_t *start, mr_finder_t *limit,
                             mr_finder_t *finder, const char *fmt, ...);

/**
 * @brief This function initializes a finder.
 *
 * @param finder The finder.
 * @param fmt The finder name format.
 * @param args The variable argument list.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Finder names must not be empty, "." or "..".
 */
mr_err_t mr_finder_vinit(mr_finder_t *finder, const char *fmt, mr_va_list args);

/**
 * @brief This function creates a finder.
 *
 * @param fmt The finder name format.
 * @param args The variable argument list.
 * @return The finder on success, MR_NULL on failure.
 *
 * @note Finder names must not be empty, "." or "..".
 */
mr_finder_t *mr_finder_vcreate(const char *fmt, mr_va_list args);

/**
 * @brief This function looks up a finder from a path.
 *
 * @param start The start finder.
 * @param limit The lookup limit.
 * @param fmt The path format.
 * @param args The variable argument list.
 * @return The finder on success, MR_NULL on failure.
 *
 * @note Paths are relative to start.
 * @note Call mr_finder_get() if the result needs to be held.
 */
mr_finder_t *mr_finder_vlookup(mr_finder_t *start, mr_finder_t *limit,
                               const char *fmt, mr_va_list args);

/**
 * @brief This function makes finders from a path.
 *
 * @param start The start finder.
 * @param limit The lookup limit.
 * @param fmt The path format.
 * @param args The variable argument list.
 * @return The finder on success, MR_NULL on failure.
 *
 * @note Paths are relative to start.
 * @note Call mr_finder_get() if the result needs to be held.
 */
mr_finder_t *mr_finder_vmkdir(mr_finder_t *start, mr_finder_t *limit,
                              const char *fmt, mr_va_list args);

/**
 * @brief This function makes finders from a path and adds a finder.
 *
 * @param start The start finder.
 * @param limit The lookup limit.
 * @param finder The finder.
 * @param fmt The path format.
 * @param args The variable argument list.
 * @return 0 on success, a negative error code on failure.
 *
 * @note Paths are relative to start.
 */
mr_err_t mr_finder_vmkdir_add(mr_finder_t *start, mr_finder_t *limit,
                              mr_finder_t *finder, const char *fmt,
                              mr_va_list args);

/**
 * @brief This function gets a finder.
 *
 * @param finder The finder.
 * @return The finder on success, MR_NULL on failure.
 */
MR_INLINE mr_finder_t *mr_finder_get(mr_finder_t *finder) {
    /* Get finder reference */
    return (mr_finder_t *)mr_object_get((mr_object_t *)finder);
}

/**
 * @brief This function puts a finder.
 *
 * @param finder The finder.
 * @return MR_TRUE on end of life cycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_finder_put(mr_finder_t *finder) {
    /* Put finder reference */
    return mr_object_put((mr_object_t *)finder);
}
#endif /* defined(MR_USE_FINDER) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_FINDER_H__ */

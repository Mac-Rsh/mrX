/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_KREF_H__
#define __MR_KREF_H__

#include <libc/mr_atomic.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Kref
 * @{
 */

/* Kref type */
typedef mr_atomic_t                     mr_kref_t;

/* Kref release type */
typedef void(mr_kref_release_t)(mr_kref_t *ref);

/**
 * @brief This macro function initializes a kref.
 */
#define MR_KREF_INIT()                  MR_ATOMIC_INIT(1)

/**
 * @brief This function initializes a kref.
 *
 * @param kref The kref.
 */
MR_INLINE void mr_kref_init(mr_kref_t *kref) {
    /* Init kref(endow with an effective life cycle) */
    mr_atomic_init(kref, 1);
}

/**
 * @brief This function gets a kref.
 *
 * @param kref The kref.
 * @return MR_TRUE on success, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_kref_get(mr_kref_t *kref) {
    mr_atomic_t last;

    do {
        /* Get current kref */
        last = mr_atomic_load(kref);
        if (last == 0) {
            /* End of life cycle */
            return MR_FALSE;
        }

        /* Try to increment kref */
    } while (!mr_atomic_compare_exchange(kref, &last, last + 1));
    return MR_TRUE;
}

/**
 * @brief This function puts a kref.
 *
 * @param kref The kref.
 * @param release The release function.
 * @return MR_TRUE on end of lifecycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_kref_put(mr_kref_t *kref, mr_kref_release_t *release) {
    mr_atomic_t ret;

    /* Decrement kref */
    ret = mr_atomic_fetch_sub(kref, 1);
    if ((ret == 1) && (release != MR_NULL)) {
        /* End of life cycle */
        release(kref);
        return MR_TRUE;
    }
    return MR_FALSE;
}

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_KREF_H__ */

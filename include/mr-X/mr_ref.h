/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_REF_H__
#define __MR_REF_H__

#include <libc/mr_atomic.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Ref
 * @{
 */

/* Ref type */
typedef mr_atomic_t                     mr_ref_t;

/* Ref release type */
typedef void(mr_ref_release_t)(mr_ref_t *ref);

/**
 * @brief This macro function initializes a ref.
 */
#define MR_REF_INIT()                  MR_ATOMIC_INIT(1)

/**
 * @brief This function initializes a ref.
 *
 * @param ref The ref.
 */
MR_INLINE void mr_ref_init(mr_ref_t *ref) {
    /* Init ref(endow with an effective life cycle) */
    mr_atomic_init(ref, 1);
}

/**
 * @brief This function gets a ref.
 *
 * @param ref The ref.
 * @return MR_TRUE on success, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_ref_get(mr_ref_t *ref) {
    mr_atomic_t last;

    do {
        /* Get current ref */
        last = mr_atomic_load(ref);
        if (last == 0) {
            /* End of life cycle */
            return MR_FALSE;
        }

        /* Try to increment ref */
    } while (!mr_atomic_compare_exchange(ref, &last, last + 1));
    return MR_TRUE;
}

/**
 * @brief This function puts a ref.
 *
 * @param ref The ref.
 * @param release The release function.
 * @return MR_TRUE on end of lifecycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_ref_put(mr_ref_t *ref, mr_ref_release_t *release) {
    mr_atomic_t ret;

    /* Decrement ref */
    ret = mr_atomic_fetch_sub(ref, 1);
    if ((ret == 1) && (release != MR_NULL)) {
        /* End of life cycle */
        release(ref);
        return MR_TRUE;
    }
    return MR_FALSE;
}

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_REF_H__ */

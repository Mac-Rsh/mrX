/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_REF_H__
#define __MR_REF_H__

#include <mrX/libc/mr_assert.h>
#include <mrX/libc/mr_atomic.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Ref
 * @{
 */

#if defined(MR_USE_REF_LONG_COUNT)
/* Ref magic definition(16bits ref-count) */
#define MR_REF_MAGIC                    (0x6d720000U)
#define MR_REF_MAGIC_MASK               (0xffff0000U)
#define MR_REF_OVERFLOW                 (0x6d730000U)
#else
/* Ref magic definition(8bits ref-count) */
#define MR_REF_MAGIC                    (0x6d725800U)
#define MR_REF_MAGIC_MASK               (0xffffff00U)
#define MR_REF_OVERFLOW                 (0x6d725900U)
#endif /* defined(MR_USE_REF_LONG_COUNT) */

/* Ref type */
typedef volatile mr_atomic_t mr_ref_t;

/* Ref release type */
typedef void(mr_ref_release_t)(mr_ref_t *ref);

/**
 * @brief This macro function initializes a ref.
 */
#define MR_REF_INIT()                   MR_ATOMIC_INIT(MR_REF_MAGIC | 0x0001U)

/**
 * @brief This macro function checks if a ref is initialized.
 *
 * @param _ref The ref.
 * @return MR_TRUE on initialized, MR_FALSE otherwise.
 */
#define MR_REF_IS_INITED(_ref)                                                 \
    (((_ref) & MR_REF_MAGIC_MASK) == MR_REF_MAGIC)

/**
 * @brief This macro function gets the ref count.
 *
 * @param _ref The ref.
 * @return The ref count.
 */
#define MR_REF_COUNT(_ref)              ((_ref) & (~MR_REF_MAGIC))

/**
 * @brief This function initializes a ref.
 *
 * @param ref The ref.
 */
MR_INLINE void mr_ref_init(mr_ref_t *ref) {
    /* Init ref(endow with an effective life cycle) */
    mr_atomic_init(ref, MR_REF_MAGIC | 0x0001U);
}

/**
 * @brief This function gets a ref.
 *
 * @param ref The ref.
 * @return MR_TRUE on success, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_ref_get(mr_ref_t *ref) {
    mr_atomic_t last;

    /* Get current ref */
    last = mr_atomic_load(ref);

    do {
        /* Check if ref is inited or ref count is zero */
        if ((!MR_REF_IS_INITED(last)) || (MR_REF_COUNT(last) == 0)) {
            MR_ASSERT(last != MR_REF_OVERFLOW);
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
    mr_atomic_t last;

    /* Decrement ref */
    last = mr_atomic_fetch_sub(ref, 1);
    if (MR_REF_IS_INITED(last) && (MR_REF_COUNT(last) == 1)
        && (release != MR_NULL)) {
        /* Mark as released */
        mr_atomic_fetch_and(ref, ~MR_REF_MAGIC);

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

/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_LOCKREF_H__
#define __MR_LOCKREF_H__

#include <kernel/mr_spinlock.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Lockref
 * @{
 */

/* Lockref type */
typedef struct mr_lockref {
    mr_spinlock_t lock;
    mr_atomic_t count;
} mr_lockref_t;

/**
 * @brief This macro function initializes a lockref.
 */
#define MR_LOCKREF_INIT()                                                      \
    { MR_SPINLOCK_INIT(), 1 }

/**
 * @brief This function initializes a lockref.
 *
 * @param lockref The lockref to initialize.
 */
MR_INLINE void mr_lockref_init(mr_lockref_t *lockref) {
    mr_spinlock_init(&lockref->lock);
    mr_atomic_init(&lockref->count, 1);
}

/**
 * @brief This function tries to lock a lockref.
 *
 * @param lockref The lockref to lock.
 * @return MR_TRUE on success, MR_FALSE on failure.
 */
MR_INLINE mr_bool_t mr_lockref_trylock(mr_lockref_t *lockref) {
    return mr_spinlock_trylock(&lockref->lock);
}

/**
 * @brief This function locks a lockref.
 *
 * @param lockref The lockref to lock.
 */
MR_INLINE void mr_lockref_lock(mr_lockref_t *lockref) {
    mr_spinlock_lock(&lockref->lock);
}

/**
 * @brief This function unlocks a lockref.
 *
 * @param lockref The lockref to unlock.
 */
MR_INLINE void mr_lockref_unlock(mr_lockref_t *lockref) {
    mr_spinlock_unlock(&lockref->lock);
}

/**
 * @brief This function gets a locked lockref.
 *
 * @param lockref The lockref to get.
 */
MR_INLINE void mr_lockref_get_locked(mr_lockref_t *lockref) {
    mr_atomic_fetch_add(&lockref->count, 1);
}

/**
 * @brief This function puts a locked lockref.
 *
 * @param lockref The lockref to put.
 * @param release The release function to call.
 * @return MR_TRUE on end of lifecycle, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_lockref_put_locked(mr_lockref_t *lockref,
                                          void (*release)(mr_lockref_t *)) {
    if (mr_atomic_load(&lockref->count) > 1) {
        mr_atomic_fetch_sub(&lockref->count, 1);
        return MR_FALSE;
    }
    /* End of lifecycle */
    if (release != MR_NULL) {
        release(lockref);
    }
    return MR_TRUE;
}

/**
 * @brief This function gets a lockref.
 *
 * @param lockref The lockref to get.
 */
MR_INLINE void mr_lockref_get(mr_lockref_t *lockref) {
    mr_lockref_lock(lockref);
    mr_lockref_get_locked(lockref);
    mr_lockref_unlock(lockref);
}

/**
 * @brief This function puts a lockref.
 *
 * @param lockref The lockref to put.
 * @param release The release function to call.
 * @return MR_TRUE on end of lifecycle, MR_FALSE otherwise.
 *
 * @note If the lockref will be zero, it will be not unlocked.
 */
MR_INLINE mr_bool_t mr_lockref_put(mr_lockref_t *lockref,
                                   void (*release)(mr_lockref_t *)) {
    int ret;

    mr_lockref_lock(lockref);
    ret = mr_lockref_put_locked(lockref, release);
    if (!ret) {
        mr_lockref_unlock(lockref);
    }
    return ret;
}

/**
 * @brief This function gets the count of a lockref.
 *
 * @param lockref The lockref to get.
 * @return The count of the lockref.
 */
MR_INLINE mr_atomic_t mr_lockref_count(mr_lockref_t *lockref) {
    return mr_atomic_load(&lockref->count);
}

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_LOCKREF_H__ */

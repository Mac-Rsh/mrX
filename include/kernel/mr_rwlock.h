/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_RWLOCK_H__
#define __MR_RWLOCK_H__

#include <kernel//mr_atomic.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Rwlock
 * @{
 */

/* Rwlock type */
typedef mr_atomic_t                     mr_rwlock_t;

/**
 * @brief This macro function initializes a rwlock.
 */
#define MR_RWLOCK_INIT()                MR_ATOMIC_INIT(0)

/**
 * @brief This function initializes a rwlock.
 *
 * @param lock The rwlock to initialize.
 */
MR_INLINE void mr_rwlock_init(mr_rwlock_t *lock) {
    mr_atomic_init(lock, 0);
}

/**
 * @brief This function locks a rwlock for reading.
 *
 * @param lock The rwlock to lock.
 */
MR_INLINE void mr_rwlock_read_lock(mr_rwlock_t *lock) {
    while (mr_atomic_fetch_add(lock, 1) & (1U << 31)) {
        mr_atomic_fetch_sub(lock, 1);
    }
}

/**
 * @brief This function unlocks a rwlock for reading.
 *
 * @param lock The rwlock to unlock.
 */
MR_INLINE void mr_rwlock_read_unlock(mr_rwlock_t *lock) {
    mr_atomic_fetch_sub(lock, 1);
}

/**
 * @brief This function unlocks a rwlock for reading.
 *
 * @param lock The rwlock to unlock.
 * @return MR_TRUE on success, MR_FALSE on failure.
 */
MR_INLINE mr_bool_t mr_rwlock_read_trylock(mr_rwlock_t *lock) {
    if (mr_atomic_fetch_add(lock, 1) & (1U << 31)) {
        mr_atomic_fetch_sub(lock, 1);
        return MR_FALSE;
    }
    return MR_TRUE;
}

/**
 * @brief This function locks a rwlock for writing.
 *
 * @param lock The rwlock to lock.
 */
MR_INLINE void mr_rwlock_write_lock(mr_rwlock_t *lock) {
    while (mr_atomic_fetch_or(lock, (mr_atomic_t)(1U << 31))) {
    }
}

/**
 * @brief This function unlocks a rwlock for writing.
 *
 * @param lock The rwlock to unlock.
 */
MR_INLINE void mr_rwlock_write_unlock(mr_rwlock_t *lock) {
    mr_atomic_fetch_and(lock, (mr_atomic_t)(~(1U << 31)));
}

/**
 * @brief This function unlocks a rwlock for writing.
 *
 * @param lock The rwlock to unlock.
 * @return MR_TRUE on success, MR_FALSE on failure.
 */
MR_INLINE mr_bool_t mr_rwlock_write_trylock(mr_rwlock_t *lock) {
    if (mr_atomic_fetch_or(lock, (mr_atomic_t)(1U << 31))) {
        return MR_FALSE;
    }
    return MR_TRUE;
}

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_RWLOCK_H__ */

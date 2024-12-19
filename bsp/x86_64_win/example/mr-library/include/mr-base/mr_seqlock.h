/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_SEQLOCK_H__
#define __MR_SEQLOCK_H__

#include <mr-base/mr_spinlock.h>
#include <mr-base/mr_types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Seqlock
 * @{
 */

/* Seqlock type */
typedef struct mr_seqlock {
    mr_spinlock_t spinlock;
    volatile mr_atomic_t seqnum;
} mr_seqlock_t;

/**
 * @brief This macro function initializes a seqlock.
 */
#define MR_SEQLOCK_INIT()               {MR_SPINLOCK_INIT(), MR_ATOMIC_INIT(0)}

/**
 * @brief This function initializes a seqlock.
 *
 * @param lock The seqlock to initialize.
 */
MR_INLINE void mr_seqlock_init(mr_seqlock_t *lock) {
    mr_spinlock_init(&lock->spinlock);
    mr_atomic_store(&lock->seqnum, 0);
}

/**
 * @brief This function reads the beginning sequence number of a seqlock.
 *
 * @param lock The seqlock to read.
 * @return The current sequence number.
 */
MR_INLINE mr_uint32_t mr_seqlock_read_begin(mr_seqlock_t *lock) {
    mr_uint32_t seqnum;

    while ((seqnum = (mr_uint32_t)mr_atomic_load(&lock->seqnum)) & 0x01) {
        mr_hw_cpu_relax();
    }
    return seqnum;
}

/**
 * @brief This function determines if a seqlock requires retry.
 *
 * @param lock The seqlock to read.
 * @param seqnum The beginning sequence number.
 * @return MR_TRUE on need retry, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_seqlock_read_retry(mr_seqlock_t *lock,
                                          mr_uint32_t seqnum) {
    return lock->seqnum != (mr_atomic_t)seqnum;
}

/**
 * @brief This function locks a seqlock for writing.
 *
 * @param lock The seqlock to lock.
 */
MR_INLINE void mr_seqlock_write_lock(mr_seqlock_t *lock) {
    mr_spinlock_lock(&lock->spinlock);
    mr_atomic_fetch_add(&lock->seqnum, 1);
}

/**
 * @brief This function unlocks a seqlock for writing.
 *
 * @param lock The seqlock to unlock.
 */
MR_INLINE void mr_seqlock_write_unlock(mr_seqlock_t *lock) {
    mr_atomic_fetch_add(&lock->seqnum, 1);
    mr_spinlock_unlock(&lock->spinlock);
}

/**
 * @brief This function invalidates a seqlock for writing.
 *
 * @param lock The seqlock to invalidate.
 */
MR_INLINE void mr_seqlock_write_invalidate(mr_seqlock_t *lock) {
    mr_seqlock_write_lock(lock);
    mr_seqlock_write_unlock(lock);
}

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_SEQLOCK_H__ */

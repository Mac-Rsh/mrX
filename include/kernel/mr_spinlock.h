/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_SPINLOCK_H__
#define __MR_SPINLOCK_H__

#include <arch/mr_hw_irq.h>
#include <kernel/mr_atomic.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Spinlock
 * @{
 */

/* Spinlock type */
typedef mr_atomic_t                     mr_spinlock_t;

/**
 * @brief This macro function initializes a spinlock.
 */
#define MR_SPINLOCK_INIT()              MR_ATOMIC_INIT(0)

/**
 * @brief This function initializes a spinlock.
 *
 * @param lock The spinlock to initialize.
 */
MR_INLINE void mr_spinlock_init(mr_spinlock_t *lock) {
    mr_atomic_init(lock, 0);
}

/**
 * @brief This function tries to lock a spinlock.
 *
 * @param lock The spinlock to lock.
 * @return MR_TRUE on success, MR_FALSE on failure.
 */
MR_INLINE mr_bool_t mr_spinlock_trylock(mr_spinlock_t *lock) {
    mr_spinlock_t unlock;

    mr_spinlock_init(&unlock);
    if (!mr_atomic_compare_exchange(lock, &unlock, 1)) {
        return MR_FALSE;
    }
    return MR_TRUE;
}

/**
 * @brief This function locks a spinlock.
 *
 * @param lock The spinlock to lock.
 */
MR_INLINE void mr_spinlock_lock(mr_spinlock_t *lock) {
    while (!mr_spinlock_trylock(lock)) {
    }
}

/**
 * @brief This function unlocks a spinlock.
 *
 * @param lock The spinlock to unlock.
 */
MR_INLINE void mr_spinlock_unlock(mr_spinlock_t *lock) {
    mr_atomic_store(lock, 0);
}

/**
 * @brief This function locks a spinlock with IRQs disabled and returns the
 *        IRQ flags.
 *
 * @param lock The spinlock to lock.
 * @return The IRQ flags.
 */
MR_INLINE int mr_spinlock_lock_irqsave(mr_spinlock_t *lock) {
    int mask;

    mask = mr_hw_irq_save();
    mr_spinlock_lock(lock);
    return mask;
}

/**
 * @brief This function unlocks a spinlock with IRQs disabled and restores the
 *        IRQ mask.
 *
 * @param lock The spinlock to unlock.
 * @param mask The IRQ mask.
 */
MR_INLINE void mr_spinlock_unlock_irqrestore(mr_spinlock_t *lock, int mask) {
    mr_spinlock_unlock(lock);
    mr_hw_irq_restore(mask);
}

/**
 * @brief This function checks if a spinlock is locked.
 *
 * @param lock The spinlock to check.
 * @return MR_TRUE on locked, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_spinlock_is_locked(mr_spinlock_t *lock) {
    return mr_atomic_load(lock) != 0;
}

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_SPINLOCK_H__ */

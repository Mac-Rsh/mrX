/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_SPINLOCK_H__
#define __MR_SPINLOCK_H__

#include <port/mr_port_irq.h>
#include <libc/mr_atomic.h>

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
#define MR_SPINLOCK_INIT()             MR_ATOMIC_INIT(0)

/**
 * @brief This function initializes a spinlock.
 *
 * @param lock The spinlock.
 */
MR_INLINE void mr_spinlock_init(mr_spinlock_t *lock) {
    /* Init spinlock */
    mr_atomic_init(lock, 0);
}

/**
 * @brief This function locks a spinlock.
 *
 * @param lock The spinlock.
 */
MR_INLINE void mr_spinlock_lock(mr_spinlock_t *lock) {
    mr_atomic_t unlock;

    do {
        /* Initialize unlock value */
        mr_atomic_init(&unlock, 0);

        /* Try to lock spinlock */
        unlock = mr_atomic_compare_exchange(lock, &unlock, 1);
    } while (!unlock);
}

/**
 * @brief This function unlocks a spinlock.
 *
 * @param lock The spinlock.
 */
MR_INLINE void mr_spinlock_unlock(mr_spinlock_t *lock) {
    /* Unlock spinlock */
    mr_atomic_store(lock, 0);
}

/**
 * @brief This function locks a spinlock with interrupts disabled.
 *
 * @param lock The spinlock.
 * @return The interrupt mask.
 */
MR_INLINE int mr_spinlock_lock_irqsave(mr_spinlock_t *lock) {
    int mask;

    /* Disable interrupt */
    mask = mr_port_irq_disable();

    /* Lock spinlock */
    mr_spinlock_lock(lock);
    return mask;
}

/**
 * @brief This function unlocks a spinlock with interrupts restored.
 *
 * @param lock The spinlock.
 * @param mask The interrupt mask.
 */
MR_INLINE void mr_spinlock_unlock_irqrestore(mr_spinlock_t *lock, int mask) {
    /* Unlock spinlock */
    mr_spinlock_unlock(lock);

    /* Enable interrupt */
    mr_port_irq_enable(mask);
}

/**
 * @brief This function checks if a spinlock is locked.
 *
 * @param lock The spinlock to check.
 * @return MR_TRUE on locked, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_spinlock_is_locked(mr_spinlock_t *lock) {
    /* Check if spinlock is locked */
    return mr_atomic_load(lock) != 0;
}

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_SPINLOCK_H__ */

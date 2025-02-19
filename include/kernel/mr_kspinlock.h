/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_KSPINLOCK_H__
#define __MR_KSPINLOCK_H__

#include <arch/mr_hw_irq.h>
#include <libc/mr_atomic.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Spinlock
 * @{
 */

/* Spinlock type */
typedef mr_atomic_t                     mr_kspinlock_t;

/**
 * @brief This macro function initializes a kspinlock.
 */
#define MR_KSPINLOCK_INIT()             MR_ATOMIC_INIT(0)

/**
 * @brief This function initializes a kspinlock.
 *
 * @param klock The kspinlock to initialize.
 */
MR_INLINE void mr_kspinlock_init(mr_kspinlock_t *klock) {
    mr_atomic_init(klock, 0);
}

/**
 * @brief This function locks a kspinlock.
 *
 * @param klock The kspinlock to lock.
 */
MR_INLINE void mr_kspinlock_lock(mr_kspinlock_t *klock) {
    mr_atomic_t unlock;

    do {
        mr_atomic_init(&unlock, 0);
        unlock = mr_atomic_compare_exchange(klock, &unlock, 1);
    } while (!unlock);
}

/**
 * @brief This function unlocks a kspinlock.
 *
 * @param klock The kspinlock to unlock.
 */
MR_INLINE void mr_kspinlock_unlock(mr_kspinlock_t *klock) {
    mr_atomic_store(klock, 0);
}

/**
 * @brief This function locks a kspinlock with interrupts disabled.
 *
 * @param klock The kspinlock to lock.
 * @return The interrupt mask.
 */
MR_INLINE int mr_kspinlock_lock_irqsave(mr_kspinlock_t *klock) {
    int mask;

    mask = mr_hw_irq_save();
    mr_kspinlock_lock(klock);
    return mask;
}

/**
 * @brief This function unlocks a kspinlock with interrupts restored.
 *
 * @param klock The kspinlock to unlock.
 * @param mask The interrupt mask.
 */
MR_INLINE void mr_kspinlock_unlock_irqrestore(mr_kspinlock_t *klock, int mask) {
    mr_kspinlock_unlock(klock);
    mr_hw_irq_restore(mask);
}

/**
 * @brief This function checks if a kspinlock is locked.
 *
 * @param klock The kspinlock to check.
 * @return MR_TRUE on locked, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_kspinlock_is_locked(mr_kspinlock_t *klock) {
    return mr_atomic_load(klock) != 0;
}

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_KSPINLOCK_H__ */

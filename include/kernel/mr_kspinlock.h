/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_KSPINLOCK_H__
#define __MR_KSPINLOCK_H__

#include <port/mr_port_irq.h>
#include <libc/mr_atomic.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Kspinlock
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
 * @param klock The kspinlock.
 */
MR_INLINE void mr_kspinlock_init(mr_kspinlock_t *klock) {
    /* Init kspinlock */
    mr_atomic_init(klock, 0);
}

/**
 * @brief This function locks a kspinlock.
 *
 * @param klock The kspinlock.
 */
MR_INLINE void mr_kspinlock_lock(mr_kspinlock_t *klock) {
    mr_atomic_t unlock;

    do {
        /* Initialize unlock value */
        mr_atomic_init(&unlock, 0);

        /* Try to lock kspinlock */
        unlock = mr_atomic_compare_exchange(klock, &unlock, 1);
    } while (!unlock);
}

/**
 * @brief This function unlocks a kspinlock.
 *
 * @param klock The kspinlock.
 */
MR_INLINE void mr_kspinlock_unlock(mr_kspinlock_t *klock) {
    /* Unlock kspinlock */
    mr_atomic_store(klock, 0);
}

/**
 * @brief This function locks a kspinlock with interrupts disabled.
 *
 * @param klock The kspinlock.
 * @return The interrupt mask.
 */
MR_INLINE int mr_kspinlock_lock_irqsave(mr_kspinlock_t *klock) {
    int mask;

    /* Disable interrupt */
    mask = mr_port_irq_save();

    /* Lock kspinlock */
    mr_kspinlock_lock(klock);
    return mask;
}

/**
 * @brief This function unlocks a kspinlock with interrupts restored.
 *
 * @param klock The kspinlock.
 * @param mask The interrupt mask.
 */
MR_INLINE void mr_kspinlock_unlock_irqrestore(mr_kspinlock_t *klock, int mask) {
    /* Unlock kspinlock */
    mr_kspinlock_unlock(klock);

    /* Enable interrupt */
    mr_port_irq_restore(mask);
}

/**
 * @brief This function checks if a kspinlock is locked.
 *
 * @param klock The kspinlock to check.
 * @return MR_TRUE on locked, MR_FALSE otherwise.
 */
MR_INLINE mr_bool_t mr_kspinlock_is_locked(mr_kspinlock_t *klock) {
    /* Check if kspinlock is locked */
    return mr_atomic_load(klock) != 0;
}

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_KSPINLOCK_H__ */

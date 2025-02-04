/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_ATOMIC_H__
#define __MR_ATOMIC_H__

#include <libc/mr_compiler.h>
#include <libc/mr_types.h>
#if !defined(__CC_ARM) && !defined(__GNUC__) && !defined(__clang__)
#include <arch/mr_hw_irq.h>
#endif /* defined(__CC_ARM) || defined(__GNUC__) || defined(__clang__) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Atomic
 * @{
 */

/* Atomic type */
typedef int                             mr_atomic_t;

/**
 * @brief This macro function initializes an atomic.
 *
 * @param _val The initial value of the atomic.
 */
#define MR_ATOMIC_INIT(_val)            (_val)

/**
 * @brief This function initializes an atomic.
 *
 * @param atomic The atomic to initialize.
 * @param val The value to initialize.
 */
MR_INLINE void mr_atomic_init(volatile mr_atomic_t *atomic, mr_atomic_t val) {
    *atomic = val;
}

/**
 * @brief This function loads an atomic.
 *
 * @param atomic The atomic to load.
 * @return The loaded atomic.
 */
MR_INLINE mr_atomic_t mr_atomic_load(volatile mr_atomic_t *atomic) {
    mr_atomic_t tmp;
    int mask;

#if defined(__CC_ARM) || defined(__GNUC__) || defined(__clang__)
    MR_UNUSED(mask);
    tmp = __atomic_load_n(atomic, __ATOMIC_SEQ_CST);
#else
    mask = mr_hw_irq_save();
    tmp = *atomic;
    mr_hw_irq_restore(mask);
#endif /* defined(__CC_ARM) || defined(__GNUC__) || defined(__clang__) */
    return tmp;
}

/**
 * @brief This function stores an atomic.
 *
 * @param atomic The atomic to store.
 * @param val The value to store.
 */
MR_INLINE void mr_atomic_store(volatile mr_atomic_t *atomic, mr_atomic_t val) {
    int mask;

#if defined(__CC_ARM) || defined(__GNUC__) || defined(__clang__)
    MR_UNUSED(mask);
    __atomic_store_n(atomic, val, __ATOMIC_SEQ_CST);
#else
    mask = mr_hw_irq_save();
    *atomic = val;
    mr_hw_irq_restore(mask);
#endif /* defined(__CC_ARM) || defined(__GNUC__) || defined(__clang__) */
}

/**
 * @brief This function exchanges an atomic.
 *
 * @param atomic The atomic to exchange.
 * @param val The value to exchange.
 * @return The exchanged atomic.
 */
MR_INLINE mr_atomic_t mr_atomic_exchange(volatile mr_atomic_t *atomic,
                                         mr_atomic_t val) {
    mr_atomic_t tmp;
    int mask;

#if defined(__CC_ARM) || defined(__GNUC__) || defined(__clang__)
    MR_UNUSED(mask);
    tmp = __atomic_exchange_n(atomic, val, __ATOMIC_SEQ_CST);
#else
    mask = mr_hw_irq_save();
    tmp = *atomic;
    *atomic = val;
    mr_hw_irq_restore(mask);
#endif /* defined(__CC_ARM) || defined(__GNUC__) || defined(__clang__) */
    return tmp;
}

/**
 * @brief This function compares and exchanges an atomic.
 *
 * @param atomic The atomic to compare and exchange.
 * @param old_val The old value to compare.
 * @param new_val The new value to exchange.
 * @return MR_TRUE on success, MR_FALSE on failure.
 */
MR_INLINE mr_bool_t mr_atomic_compare_exchange(volatile mr_atomic_t *atomic,
                                               mr_atomic_t *old_val,
                                               mr_atomic_t new_val) {
    int mask;

#if defined(__CC_ARM) || defined(__GNUC__) || defined(__clang__)
    MR_UNUSED(mask);
    return __atomic_compare_exchange_n(atomic, old_val, new_val, MR_FALSE,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#else
    mask = mr_hw_irq_save();
    if (*atomic != *old_val) {
        mr_hw_irq_restore(mask);
        return MR_FALSE;
    }
    *atomic = new_val;
    mr_hw_irq_restore(mask);
    return MR_TRUE;
#endif /* defined(__CC_ARM) || defined(__GNUC__) || defined(__clang__) */
}

/**
 * @brief This function fetches and adds an atomic.
 *
 * @param atomic The atomic to add.
 * @param val The value to add.
 * @return The fetched atomic.
 */
MR_INLINE mr_atomic_t mr_atomic_fetch_add(volatile mr_atomic_t *atomic,
                                          mr_atomic_t val) {
    mr_atomic_t tmp;
    int mask;

#if defined(__CC_ARM) || defined(__GNUC__) || defined(__clang__)
    MR_UNUSED(mask);
    tmp = __atomic_fetch_add(atomic, val, __ATOMIC_SEQ_CST);
#else
    mask = mr_hw_irq_save();
    tmp = *atomic;
    *atomic += val;
    mr_hw_irq_restore(mask);
#endif /* defined(__CC_ARM) || defined(__GNUC__) || defined(__clang__) */
    return tmp;
}

/**
 * @brief This function fetches and subtracts an atomic.
 *
 * @param atomic The atomic to subtract.
 * @param val The value to subtract.
 * @return The fetched atomic.
 */
MR_INLINE mr_atomic_t mr_atomic_fetch_sub(volatile mr_atomic_t *atomic,
                                          mr_atomic_t val) {
    mr_atomic_t tmp;
    int mask;

#if defined(__CC_ARM) || defined(__GNUC__) || defined(__clang__)
    MR_UNUSED(mask);
    tmp = __atomic_fetch_sub(atomic, val, __ATOMIC_SEQ_CST);
#else
    mask = mr_hw_irq_save();
    tmp = *atomic;
    *atomic -= val;
    mr_hw_irq_restore(mask);
#endif /* defined(__CC_ARM) || defined(__GNUC__) || defined(__clang__) */
    return tmp;
}

/**
 * @brief This function fetches and ands an atomic.
 *
 * @param atomic The atomic to and.
 * @param val The value to and.
 * @return The fetched atomic.
 */
MR_INLINE mr_atomic_t mr_atomic_fetch_and(volatile mr_atomic_t *atomic,
                                          mr_atomic_t val) {
    mr_atomic_t tmp;
    int mask;

#if defined(__CC_ARM) || defined(__GNUC__) || defined(__clang__)
    MR_UNUSED(mask);
    tmp = __atomic_fetch_and(atomic, val, __ATOMIC_SEQ_CST);
#else
    mask = mr_hw_irq_save();
    tmp = *atomic;
    *atomic &= val;
    mr_hw_irq_restore(mask);
#endif /* defined(__CC_ARM) || defined(__GNUC__) || defined(__clang__) */
    return tmp;
}

/**
 * @brief This function fetches and ors an atomic.
 *
 * @param atomic The atomic to or.
 * @param val The value to or.
 * @return The fetched atomic.
 */
MR_INLINE mr_atomic_t mr_atomic_fetch_or(volatile mr_atomic_t *atomic,
                                         mr_atomic_t val) {
    mr_atomic_t tmp;
    int mask;

#if defined(__CC_ARM) || defined(__GNUC__) || defined(__clang__)
    MR_UNUSED(mask);
    tmp = __atomic_fetch_or(atomic, val, __ATOMIC_SEQ_CST);
#else
    mask = mr_hw_irq_save();
    tmp = *atomic;
    *atomic |= val;
    mr_hw_irq_restore(mask);
#endif /* defined(__CC_ARM) || defined(__GNUC__) || defined(__clang__) */
    return tmp;
}

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_ATOMIC_H__ */

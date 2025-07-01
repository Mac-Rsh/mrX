/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_LIBC_ATOMIC_H__
#define __MR_LIBC_ATOMIC_H__

#include <mr_config.h>
#if defined(MR_USE_3PARTY_ATOMIC)
#include <3party/libc/mr_atomic.h>
#else
#include <libc/mr_compiler.h>
#include <libc/mr_types.h>
#if !defined(__ATOMIC_SEQ_CST)
#include <port/mr_port_irq.h>
#endif /* !defined(__ATOMIC_SEQ_CST) */
#endif /* defined(MR_USE_3PARTY_ATOMIC) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Atomic
 * @{
 */

/* Atomic definition */
#if defined(MR_USE_3PARTY_ATOMIC)
/* In '3party/libc/mr_atomic.h' */
#else
/* Atomic type */
typedef int                             mr_atomic_t;

/**
 * @brief This macro function initializes an atomic.
 *
 * @param _val The initial value.
 */
#define MR_ATOMIC_INIT(_val)            (_val)

/**
 * @brief This function initializes an atomic.
 *
 * @param atomic The atomic.
 * @param val The initial value.
 */
MR_INLINE void mr_atomic_init(volatile mr_atomic_t *atomic, mr_atomic_t val) {
    /* Init atomic */
    *atomic = val;
}

/**
 * @brief This function loads an atomic.
 *
 * @param atomic The atomic.
 * @return The loaded value.
 */
MR_INLINE mr_atomic_t mr_atomic_load(volatile mr_atomic_t *atomic) {
    mr_atomic_t tmp;
    int mask;

#if defined(__ATOMIC_SEQ_CST)
    MR_UNUSED(mask);
    tmp = __atomic_load_n(atomic, __ATOMIC_SEQ_CST);
#else
    /* Disable interrupt */
    mask = mr_port_irq_save();

    /* Load atomic */
    tmp = *atomic;

    /* Enable interrupt */
    mr_port_irq_restore(mask);
#endif /* defined(__ATOMIC_SEQ_CST) */
    return tmp;
}

/**
 * @brief This function stores an atomic.
 *
 * @param atomic The atomic..
 * @param val The stored value.
 */
MR_INLINE void mr_atomic_store(volatile mr_atomic_t *atomic, mr_atomic_t val) {
    int mask;

#if defined(__ATOMIC_SEQ_CST)
    MR_UNUSED(mask);
    __atomic_store_n(atomic, val, __ATOMIC_SEQ_CST);
#else
    /* Disable interrupt */
    mask = mr_port_irq_save();

    /* Store atomic */
    *atomic = val;

    /* Enable interrupt */
    mr_port_irq_restore(mask);
#endif /* defined(__ATOMIC_SEQ_CST) */
}

/**
 * @brief This function exchanges an atomic.
 *
 * @param atomic The atomic.
 * @param val The exchange value.
 * @return The exchanged value.
 */
MR_INLINE mr_atomic_t mr_atomic_exchange(volatile mr_atomic_t *atomic,
                                         mr_atomic_t val) {
    mr_atomic_t tmp;
    int mask;

#if defined(__ATOMIC_SEQ_CST)
    MR_UNUSED(mask);
    tmp = __atomic_exchange_n(atomic, val, __ATOMIC_SEQ_CST);
#else
    /* Disable interrupt */
    mask = mr_port_irq_save();

    /* Exchange atomic */
    tmp = *atomic;
    *atomic = val;

    /* Enable interrupt */
    mr_port_irq_restore(mask);
#endif /* defined(__ATOMIC_SEQ_CST) */
    return tmp;
}

/**
 * @brief This function compares and exchanges an atomic.
 *
 * @param atomic The atomic..
 * @param val The compared value.
 * @param desired The desired value.
 * @return MR_TRUE on success, MR_FALSE on failure.
 */
MR_INLINE mr_bool_t mr_atomic_compare_exchange(volatile mr_atomic_t *atomic,
                                               mr_atomic_t *val,
                                               mr_atomic_t desired) {
    mr_bool_t tmp;
    int mask;

#if defined(__ATOMIC_SEQ_CST)
    MR_UNUSED(mask);
    tmp = __atomic_compare_exchange_n(atomic, val, desired, MR_FALSE,
                                      __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#else
    /* Disable interrupt */
    mask = mr_port_irq_save();

    /* Compare and exchange atomic */
    if (*atomic != *val) {
        *val = *atomic;
        tmp = MR_FALSE;
    } else {
        *atomic = desired;
        tmp = MR_TRUE;
    }

    /* Enable interrupt */
    mr_port_irq_restore(mask);
#endif /* defined(__ATOMIC_SEQ_CST) */
    return tmp;
}

/**
 * @brief This function fetches and adds an atomic.
 *
 * @param atomic The atomic.
 * @param val The added value.
 * @return The fetched atomic.
 */
MR_INLINE mr_atomic_t mr_atomic_fetch_add(volatile mr_atomic_t *atomic,
                                          mr_atomic_t val) {
    mr_atomic_t tmp;
    int mask;

#if defined(__ATOMIC_SEQ_CST)
    MR_UNUSED(mask);
    tmp = __atomic_fetch_add(atomic, val, __ATOMIC_SEQ_CST);
#else
    /* Disable interrupt */
    mask = mr_port_irq_save();

    /* Fetch and add atomic */
    tmp = *atomic;
    *atomic += val;

    /* Enable interrupt */
    mr_port_irq_restore(mask);
#endif /* defined(__ATOMIC_SEQ_CST) */
    return tmp;
}

/**
 * @brief This function fetches and subtracts an atomic.
 *
 * @param atomic The atomic.
 * @param val The subtracted value.
 * @return The fetched atomic.
 */
MR_INLINE mr_atomic_t mr_atomic_fetch_sub(volatile mr_atomic_t *atomic,
                                          mr_atomic_t val) {
    mr_atomic_t tmp;
    int mask;

#if defined(__ATOMIC_SEQ_CST)
    MR_UNUSED(mask);
    tmp = __atomic_fetch_sub(atomic, val, __ATOMIC_SEQ_CST);
#else
    /* Disable interrupt */
    mask = mr_port_irq_save();

    /* Fetch and subtract atomic */
    tmp = *atomic;
    *atomic -= val;

    /* Enable interrupt */
    mr_port_irq_restore(mask);
#endif /* defined(__ATOMIC_SEQ_CST) */
    return tmp;
}

/**
 * @brief This function fetches and ands an atomic.
 *
 * @param atomic The atomic.
 * @param val The and value.
 * @return The fetched atomic.
 */
MR_INLINE mr_atomic_t mr_atomic_fetch_and(volatile mr_atomic_t *atomic,
                                          mr_atomic_t val) {
    mr_atomic_t tmp;
    int mask;

#if defined(__ATOMIC_SEQ_CST)
    MR_UNUSED(mask);
    tmp = __atomic_fetch_and(atomic, val, __ATOMIC_SEQ_CST);
#else
    /* Disable interrupt */
    mask = mr_port_irq_save();

    /* Fetch and and atomic */
    tmp = *atomic;
    *atomic &= val;

    /* Enable interrupt */
    mr_port_irq_restore(mask);
#endif /* defined(__ATOMIC_SEQ_CST) */
    return tmp;
}

/**
 * @brief This function fetches and ors an atomic.
 *
 * @param atomic The atomic.
 * @param val The or value.
 * @return The fetched atomic.
 */
MR_INLINE mr_atomic_t mr_atomic_fetch_or(volatile mr_atomic_t *atomic,
                                         mr_atomic_t val) {
    mr_atomic_t tmp;
    int mask;

#if defined(__ATOMIC_SEQ_CST)
    MR_UNUSED(mask);
    tmp = __atomic_fetch_or(atomic, val, __ATOMIC_SEQ_CST);
#else
    /* Disable interrupt */
    mask = mr_port_irq_save();

    /* Fetch and or atomic */
    tmp = *atomic;
    *atomic |= val;

    /* Enable interrupt */
    mr_port_irq_restore(mask);
#endif /* defined(__ATOMIC_SEQ_CST) */
    return tmp;
}
#endif /* defined(MR_USE_3PARTY_ATOMIC) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_LIBC_ATOMIC_H__ */

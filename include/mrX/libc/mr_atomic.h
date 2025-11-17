/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_LIBC_ATOMIC_H__
#define __MR_LIBC_ATOMIC_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_ATOMIC_3RD)
#include <3party/libc/mr_atomic.h>
#else
#include <mrX/port/mr_port_irq.h>
#endif /* defined(MR_USE_ATOMIC_3RD) */
#include <mrX/libc/mr_compiler.h>
#include <mrX/libc/mr_types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Atomic
 * @{
 */

/* Atomic definition */
#if defined(MR_USE_ATOMIC_3RD)
/* In '3party/libc/mr_atomic.h' */
#else
/* Atomic type */
typedef mr_intptr_t                     mr_atomic_t;

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

    /* Disable interrupt */
    mask = mr_port_irq_disable();

    /* Load atomic */
    tmp = *atomic;

    /* Enable interrupt */
    mr_port_irq_enable(mask);
    return tmp;
}

/**
 * @brief This function stores an atomic.
 *
 * @param atomic The atomic.
 * @param val The stored value.
 */
MR_INLINE void mr_atomic_store(volatile mr_atomic_t *atomic, mr_atomic_t val) {
    int mask;

    /* Disable interrupt */
    mask = mr_port_irq_disable();

    /* Store atomic */
    *atomic = val;

    /* Enable interrupt */
    mr_port_irq_enable(mask);
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

    /* Disable interrupt */
    mask = mr_port_irq_disable();

    /* Exchange atomic */
    tmp = *atomic;
    *atomic = val;

    /* Enable interrupt */
    mr_port_irq_enable(mask);
    return tmp;
}

/**
 * @brief This function compares and exchanges an atomic.
 *
 * @param atomic The atomic..
 * @param val The compared value.
 * @param des The desired value.
 * @return MR_TRUE on success, MR_FALSE on failure.
 */
MR_INLINE mr_bool_t mr_atomic_compare_exchange(volatile mr_atomic_t *atomic,
                                               mr_atomic_t *val,
                                               mr_atomic_t des) {
    mr_bool_t tmp;
    int mask;

    /* Disable interrupt */
    mask = mr_port_irq_disable();

    /* Compare and exchange atomic */
    if (*atomic != *val) {
        *val = *atomic;
        tmp = MR_FALSE;
    } else {
        *atomic = des;
        tmp = MR_TRUE;
    }

    /* Enable interrupt */
    mr_port_irq_enable(mask);
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

    /* Disable interrupt */
    mask = mr_port_irq_disable();

    /* Fetch and '+' atomic */
    tmp = *atomic;
    *atomic += val;

    /* Enable interrupt */
    mr_port_irq_enable(mask);
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

    /* Disable interrupt */
    mask = mr_port_irq_disable();

    /* Fetch and '-' atomic */
    tmp = *atomic;
    *atomic -= val;

    /* Enable interrupt */
    mr_port_irq_enable(mask);
    return tmp;
}

/**
 * @brief This function fetches and ands('&') an atomic.
 *
 * @param atomic The atomic.
 * @param val The and value.
 * @return The fetched atomic.
 */
MR_INLINE mr_atomic_t mr_atomic_fetch_and(volatile mr_atomic_t *atomic,
                                          mr_atomic_t val) {
    mr_atomic_t tmp;
    int mask;

    /* Disable interrupt */
    mask = mr_port_irq_disable();

    /* Fetch and '&' atomic */
    tmp = *atomic;
    *atomic &= val;

    /* Enable interrupt */
    mr_port_irq_enable(mask);
    return tmp;
}

/**
 * @brief This function fetches and ors('|') an atomic.
 *
 * @param atomic The atomic.
 * @param val The or value.
 * @return The fetched atomic.
 */
MR_INLINE mr_atomic_t mr_atomic_fetch_or(volatile mr_atomic_t *atomic,
                                         mr_atomic_t val) {
    mr_atomic_t tmp;
    int mask;

    /* Disable interrupt */
    mask = mr_port_irq_disable();

    /* Fetch and '|' atomic */
    tmp = *atomic;
    *atomic |= val;

    /* Enable interrupt */
    mr_port_irq_enable(mask);
    return tmp;
}

/**
 * @brief This function fetches and xors('^') an atomic.
 *
 * @param atomic The atomic.
 * @param val The xor value.
 * @return The fetched atomic.
 */
MR_INLINE mr_atomic_t mr_atomic_fetch_xor(volatile mr_atomic_t *atomic,
                                          mr_atomic_t val) {
    mr_atomic_t tmp;
    int mask;

    /* Disable interrupt */
    mask = mr_port_irq_disable();

    /* Fetch and '^' atomic */
    tmp = *atomic;
    *atomic ^= val;

    /* Enable interrupt */
    mr_port_irq_enable(mask);
    return tmp;
}
#endif /* defined(MR_USE_ATOMIC_3RD) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_LIBC_ATOMIC_H__ */

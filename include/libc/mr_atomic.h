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
#if defined(MR_USE_ATOMIC_LIBC)
#include <stdatomic.h>
#elif defined(MR_USE_3PARTY_ATOMIC)
#include <3party/libc/mr_atomic.h>
#else
#include <port/mr_port_irq.h>
#endif /* defined(MR_USE_3PARTY_ATOMIC) */
#include <libc/mr_compiler.h>
#include <libc/mr_types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Atomic
 * @{
 */

/* Atomic definition */
#if defined(MR_USE_ATOMIC_LIBC)
typedef atomic_int                      mr_atomic_t;
#define MR_ATOMIC_INIT(_val)            ATOMIC_VAR_INIT(_val)
#define mr_atomic_init(_a, _val)        atomic_init(_a, _val)
#define mr_atomic_load(_a)              atomic_load(_a)
#define mr_atomic_store(_a, _val)       atomic_store(_a, _val)
#define mr_atomic_exchange(_a, _val)    atomic_exchange(_a, _val)
#define mr_atomic_compare_exchange(_a, _val, _desired)                         \
    atomic_compare_exchange_strong(_a, _val, _desired)
#define mr_atomic_fetch_add(_a, _val)   atomic_fetch_add(_a, _val)
#define mr_atomic_fetch_sub(_a, _val)   atomic_fetch_sub(_a, _val)
#define mr_atomic_fetch_and(_a, _val)   atomic_fetch_and(_a, _val)
#define mr_atomic_fetch_or(_a, _val)    atomic_fetch_or(_a, _val)
#elif defined(MR_USE_3PARTY_ATOMIC)
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

    /* Disable interrupt */
    mask = mr_port_irq_save();

    /* Load atomic */
    tmp = *atomic;

    /* Enable interrupt */
    mr_port_irq_restore(mask);
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

    /* Disable interrupt */
    mask = mr_port_irq_save();

    /* Store atomic */
    *atomic = val;

    /* Enable interrupt */
    mr_port_irq_restore(mask);
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
    mask = mr_port_irq_save();

    /* Exchange atomic */
    tmp = *atomic;
    *atomic = val;

    /* Enable interrupt */
    mr_port_irq_restore(mask);
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
    mask = mr_port_irq_save();

    /* Fetch and add atomic */
    tmp = *atomic;
    *atomic += val;

    /* Enable interrupt */
    mr_port_irq_restore(mask);
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
    mask = mr_port_irq_save();

    /* Fetch and subtract atomic */
    tmp = *atomic;
    *atomic -= val;

    /* Enable interrupt */
    mr_port_irq_restore(mask);
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

    /* Disable interrupt */
    mask = mr_port_irq_save();

    /* Fetch and and atomic */
    tmp = *atomic;
    *atomic &= val;

    /* Enable interrupt */
    mr_port_irq_restore(mask);
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

    /* Disable interrupt */
    mask = mr_port_irq_save();

    /* Fetch and or atomic */
    tmp = *atomic;
    *atomic |= val;

    /* Enable interrupt */
    mr_port_irq_restore(mask);
    return tmp;
}
#endif /* defined(MR_USE_3PARTY_ATOMIC) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_LIBC_ATOMIC_H__ */

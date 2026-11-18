/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_WIN_LIBC_ATOMIC_H__
#define __MR_WIN_LIBC_ATOMIC_H__

#include <mrX/mr_config.h>
#if defined(MR_USE_ATOMIC_3RD)
#include <mrX/libc/mr_compiler.h>
#include <mrX/libc/mr_types.h>
#endif /* defined(MR_USE_ATOMIC_3RD) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup Atomic
 * @{
 */

#if defined(MR_USE_ATOMIC_3RD)
#if !defined(__ATOMIC_SEQ_CST)
#error "Windows atomic backend requires GCC-compatible __atomic builtins"
#endif /* !defined(__ATOMIC_SEQ_CST) */

/* Atomic type */
typedef mr_intptr_t                     mr_atomic_t;

/* Require native atomics for naturally aligned mr_atomic_t objects */
#ifdef __cplusplus
static_assert(__atomic_always_lock_free(sizeof(mr_atomic_t), MR_NULL),
              "Windows atomic backend requires lock-free mr_atomic_t");
#else
_Static_assert(__atomic_always_lock_free(sizeof(mr_atomic_t), MR_NULL),
               "Windows atomic backend requires lock-free mr_atomic_t");
#endif /* __cplusplus */

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
    __atomic_store_n(atomic, val, __ATOMIC_RELAXED);
}

/**
 * @brief This function loads an atomic.
 *
 * @param atomic The atomic.
 * @return The loaded value.
 */
MR_INLINE mr_atomic_t mr_atomic_load(volatile mr_atomic_t *atomic) {
    return __atomic_load_n(atomic, __ATOMIC_SEQ_CST);
}

/**
 * @brief This function stores an atomic.
 *
 * @param atomic The atomic.
 * @param val The stored value.
 */
MR_INLINE void mr_atomic_store(volatile mr_atomic_t *atomic, mr_atomic_t val) {
    __atomic_store_n(atomic, val, __ATOMIC_SEQ_CST);
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
    return __atomic_exchange_n(atomic, val, __ATOMIC_SEQ_CST);
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
    return __atomic_compare_exchange_n(atomic, val, des, MR_FALSE,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
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
    return __atomic_fetch_add(atomic, val, __ATOMIC_SEQ_CST);
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
    return __atomic_fetch_sub(atomic, val, __ATOMIC_SEQ_CST);
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
    return __atomic_fetch_and(atomic, val, __ATOMIC_SEQ_CST);
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
    return __atomic_fetch_or(atomic, val, __ATOMIC_SEQ_CST);
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
    return __atomic_fetch_xor(atomic, val, __ATOMIC_SEQ_CST);
}
#endif /* defined(MR_USE_ATOMIC_3RD) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_WIN_LIBC_ATOMIC_H__ */

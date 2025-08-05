/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_LIBC_STRING_H__
#define __MR_LIBC_STRING_H__

#include <mr_config.h>
#if defined(MR_USE_STRING_STD)
#include <string.h>
#else
#endif /* defined(MR_USE_STRING_STD) */
#include <libc/mr_compiler.h>
#include <libc/mr_types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup String
 * @{
 */

/* String definition */
#if defined(MR_USE_STRING_STD)
#define mr_memccpy(_d, _s, _c, _sz)     memccpy(_d, _s, _c, _sz)
#define mr_memcmp(_m1, _m2, _sz)        memcmp(_m1, _m2, _sz)
#define mr_memcpy(_d, _s, _sz)          memcpy(_d, _s, _sz)
#define mr_memset(_d, _c, _sz)          memset(_d, _c, _sz)
#define mr_memchr(_s, _c, _sz)          memchr(_s, _c, _sz)
#define mr_strcmp(_s1, _s2)             strcmp(_s1, _s2)
#define mr_strcpy(_d, _s)               strcpy(_d, _s)
#define mr_strerror(_e)                 strerror(_e)
#define mr_strlen(_s)                   strlen(_s)
#define mr_strnlen(_s, _sz)             strnlen(_s, _sz)
#define mr_strncmp(_s1, _s2, _sz)       strncmp(_s1, _s2, _sz)
#define mr_strncpy(_d, _s, _sz)         strncpy(_d, _s, _sz)
#define mr_strchr(_s, _c)               strchr(_s, _c)
#else
/**
 * @brief This function copies size bytes from src to dst until c is found.
 *
 * @param dst The destination.
 * @param src The source.
 * @param c The byte to search.
 * @param size The copy size.
 *
 * @return The destination.
 */
void *mr_memccpy(void *dst, const void *src, int c, mr_size_t size);

/**
 * @brief This function compares size bytes from m1 and m2.
 *
 * @param m1 The first memory.
 * @param m2 The second memory.
 * @param size The compare size.
 * @return The difference between m1 and m2.
 */
int mr_memcmp(const void *m1, const void *m2, mr_size_t size);

/**
 * @brief This function copies size bytes from src to dst.
 *
 * @param dst The destination.
 * @param src The source.
 * @param size The copy size.
 * @return The destination.
 */
void *mr_memcpy(void *dst, const void *src, mr_size_t size);

/**
 * @brief This function sets size bytes of dst to c.
 *
 * @param dst The destination.
 * @param c The byte to set.
 * @param size The set size.
 * @return The destination.
 */
void *mr_memset(void *dst, int c, mr_size_t size);

/**
 * @brief This function searches for c in size bytes of s.
 *
 * @param s The string to search.
 * @param c The byte to search.
 * @param size The search size.
 * @return The pointer to the first occurrence of c in s, MR_NULL if not found.
 */
void *mr_memchr(const void *s, int c, mr_size_t size);

/**
 * @brief This function compares two strings.
 *
 * @param s1 The first string.
 * @param s2 The second string.
 * @return The difference between s1 and s2.
 */
int mr_strcmp(const char *s1, const char *s2);

/**
 * @brief This function copies size bytes from src to dst.
 *
 * @param dst The destination.
 * @param src The source.
 * @return The destination.
 */
char *mr_strcpy(char *dst, const char *src);

/**
 * @brief This function returns a string representation of the error code.
 *
 * @param errnum The error code.
 * @return The error code string representation.
 */
char *mr_strerror(int errnum);

/**
 * @brief This function returns the length of the string.
 *
 * @param s The string.
 * @return The string length.
 */
mr_size_t mr_strlen(const char *s);

/**
 * @brief This function returns the length of the string.
 *
 * @param s The string.
 * @param size The maximum string length.
 * @return The string length.
 */
mr_size_t mr_strnlen(const char *s, mr_size_t size);

/**
 * @brief This function compares the first size bytes of s1 and s2.
 *
 * @param s1 The first string.
 * @param s2 The second string.
 * @param size The compare size.
 * @return The difference between s1 and s2.
 */
int mr_strncmp(const char *s1, const char *s2, mr_size_t size);

/**
 * @brief This function copies size bytes from src to dst.
 *
 * @param dst The destination.
 * @param src The source.
 * @param size The copy size.
 * @return The destination.
 */
char *mr_strncpy(char *dst, const char *src, mr_size_t size);

/**
 * @brief This function searches for c in s.
 *
 * @param s The search string.
 * @param c The search byte.
 * @return The pointer to the first occurrence of c in s, MR_NULL if not found.
 */
char *mr_strchr(const char *s, int c);
#endif /* defined(MR_USE_STRING_STD) */

/**
 * @brief This function returns the index of the first set bit.
 *
 * @param x The value.
 * @return The first set bit index.
 */
MR_INLINE int mr_ffs32(mr_uint32_t x) {
    extern const int __mr_ffs_debruijn32[32];

    /* Check zero */
    if (x == 0) {
        return 0;
    }

    /* DeBruijn ffs32 */
    return __mr_ffs_debruijn32[((x & (-x)) * 0x077cb531U) >> 27];
}

/**
 * @brief This function returns the index of the first set bit.
 *
 * @param x The value.
 * @return The first set bit index.
 */
MR_INLINE int mr_ffs64(mr_uint64_t x) {
    extern const int __mr_ffs_debruijn64[64];

    /* Check zero */
    if (x == 0) {
        return 0;
    }

    /* DeBruijn ffs64 */
    return __mr_ffs_debruijn64[((x & (-x)) * 0x03f79d71b4cb0a89ULL) >> 58];
}

/**
 * @brief This function returns the index of the last set bit.
 *
 * @param x The value.
 * @return The last set bit index.
 */
MR_INLINE int mr_fls32(mr_uint32_t x) {
    /* Check zero */
    if (x == 0) {
        return 0;
    }

    /* Higher order bits */
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x -= x >> 1;

    /* Find last set bit */
    return mr_ffs32(x);
}

/**
 * @brief This function returns the index of the last set bit.
 *
 * @param x The value.
 * @return The last set bit index.
 */
MR_INLINE int mr_fls64(mr_uint64_t x) {
    /* Check zero */
    if (x == 0) {
        return 0;
    }

    /* Higher order bits */
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x |= x >> 32;
    x -= x >> 1;

    return mr_ffs64(x);
}

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_LIBC_STRING_H__ */

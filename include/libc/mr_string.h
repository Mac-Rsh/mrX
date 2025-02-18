/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#ifndef __MR_STRING_H__
#define __MR_STRING_H__

#include <mr_config.h>
#if defined(MR_USE_LIBC_STRING)
#include <string.h>
#else
#include <libc/mr_types.h>
#endif /* defined(MR_USE_LIBC_STRING) */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @addtogroup String
 * @{
 */

/* String definition */
#if defined(MR_USE_LIBC_STRING)
#define mr_memccpy(_d, _s, _c, _sz)     memccpy(_d, _s, _c, _sz)
#define mr_memcmp(_m1, _m2, _sz)        memcmp(_m1, _m2, _sz)
#define mr_memcpy(_d, _s, _sz)          memcpy(_d, _s, _sz)
#define mr_memset(_d, _c, _sz)          memset(_d, _c, _sz)
#define mr_memchr(_s, _c, _sz)          memchr(_s, _c, _sz)
#define mr_strcmp(_s1, _s2)             strcmp(_s1, _s2)
#define mr_strcpy(_d, _s)               strcpy(_d, _s)
#define mr_strerror(_e)                 strerror(_e)
#define mr_strlen(_s)                   strlen(_s)
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
 * @param size The number of bytes to copy.
 *
 * @return The destination.
 */
void *mr_memccpy(void *dst, const void *src, int c, mr_size_t size);

/**
 * @brief This function compares size bytes from m1 and m2.
 *
 * @param m1 The first memory.
 * @param m2 The second memory.
 * @param size The number of bytes to compare.
 * @return The difference between m1 and m2.
 */
int mr_memcmp(const void *m1, const void *m2, mr_size_t size);

/**
 * @brief This function copies size bytes from src to dst.
 *
 * @param dst The destination.
 * @param src The source.
 * @param size The number of bytes to copy.
 * @return The destination.
 */
void *mr_memcpy(void *dst, const void *src, mr_size_t size);

/**
 * @brief This function sets size bytes of dst to c.
 *
 * @param dst The destination.
 * @param c The byte to set.
 * @param size The number of bytes to set.
 * @return The destination.
 */
void *mr_memset(void *dst, int c, mr_size_t size);

/**
 * @brief This function searches for c in size bytes of s.
 *
 * @param s The string to search.
 * @param c The byte to search.
 * @param size The number of bytes to search.
 * @return The pointer to the first occurrence of c in s, or NULL if not found.
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
 * @return The string representation of the error code.
 */
char *mr_strerror(int errnum);

/**
 * @brief This function returns the length of the string.
 *
 * @param s The string.
 * @return The length of the string.
 */
mr_size_t mr_strlen(const char *s);

/**
 * @brief This function returns the length of the string.
 *
 * @param s The string.
 * @param size The maximum length of the string.
 * @return The length of the string.
 */
mr_size_t mr_strnlen(const char *s, mr_size_t size);

/**
 * @brief This function compares the first size bytes of s1 and s2.
 *
 * @param s1 The first string.
 * @param s2 The second string.
 * @param size The number of bytes to compare.
 * @return The difference between s1 and s2.
 */
int mr_strncmp(const char *s1, const char *s2, mr_size_t size);

/**
 * @brief This function copies size bytes from src to dst.
 *
 * @param dst The destination.
 * @param src The source.
 * @param size The number of bytes to copy.
 * @return The destination.
 */
char *mr_strncpy(char *dst, const char *src, mr_size_t size);

/**
 * @brief This function searches for c in s.
 *
 * @param s The string to search.
 * @param c The byte to search.
 * @return The pointer to the first occurrence of c in s, or NULL if not found.
 */
char *mr_strchr(const char *s, int c);
#endif /* defined(MR_USE_LIBC_STRING) */

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MR_STRING_H__ */

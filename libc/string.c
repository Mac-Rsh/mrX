/**
 * @copyright (c) 2024-2025, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <libc/mr_string.h>
#if defined(MR_USE_STRING)
#include <libc/mr_errno.h>

/* DeBruijn ffs32 */
const int __mr_ffs_debruijn32[32] = {
    1,  2,  29, 3,  30, 15, 25, 4, 31, 23, 21, 16, 26, 18, 5,  9,
    32, 28, 14, 24, 22, 20, 17, 8, 27, 13, 19, 7,  12, 6,  11, 10,
};

/* DeBruijn ffs64 */
const int __mr_ffs_debruijn64[64] = {
    1,  2,  57, 3,  58, 50, 29, 4,  62, 59, 43, 51, 39, 30, 18, 5,
    63, 48, 60, 37, 46, 44, 52, 23, 54, 40, 34, 31, 25, 19, 13, 6,
    64, 56, 49, 28, 61, 42, 38, 17, 47, 36, 45, 22, 53, 33, 24, 12,
    55, 27, 41, 16, 35, 21, 32, 11, 26, 15, 20, 10, 14, 9,  8,  7,
};

void *mr_memccpy(void *dst, const void *src, int c, mr_size_t size) {
    const char *s;
    char *d;

#if defined(MR_USE_STRING_SAFE)
    /* Check parameter */
    if ((!dst) || (!src)) {
        return MR_NULL;
    }
#endif /* defined(MR_USE_STRING_SAFE) */

    /* Copy bytes */
    for (d = dst, s = src; size > 0; size--) {
        (*d++) = *s;

        /* Match character */
        if (*s == c) {
            return d;
        }
        s++;
    }
    return MR_NULL;
}

int mr_memcmp(const void *m1, const void *m2, mr_size_t size) {
    const char *s1, *s2;

#if defined(MR_USE_STRING_SAFE)
    /* Check parameter */
    if ((!m1) || (!m2)) {
        return 0;
    }
#endif /* defined(MR_USE_STRING_SAFE) */

    /* Compare bytes */
    for (s1 = m1, s2 = m2; size > 0; size--) {
        if (*s1 == *s2) {
            s1++;
            s2++;
            continue;
        }
        return *s1 - *s2;
    }
    return 0;
}

void *mr_memcpy(void *dst, const void *src, mr_size_t size) {
    const mr_uint8_t align = sizeof(mr_uintptr_t) - 1;
    const char *s;
    char *d;

#if defined(MR_USE_STRING_SAFE)
    /* Check parameter */
    if ((!dst) || (!src)) {
        return MR_NULL;
    }
#endif /* defined(MR_USE_STRING_SAFE) */

    /* Check dst and src alignment */
    if (((mr_uintptr_t)dst & align) || ((mr_uintptr_t)src & align)) {
        /* It is expected to be replaced with 'build in' */
        for (d = dst, s = src; size > 0; size--) {
            (*d++) = (*s++);
        }
    } else {
        /* Copy aligned bytes */
        for (d = dst, s = src; size >= sizeof(mr_uintptr_t);
             size -= sizeof(mr_uintptr_t)) {
            *(mr_uintptr_t *)d = *(mr_uintptr_t *)s;
            d += sizeof(mr_uintptr_t);
            s += sizeof(mr_uintptr_t);
        }

        /* Copy unaligned bytes */
        while (size--) {
            (*d++) = (*s++);
        }
    }
    return dst;
}

void *mr_memset(void *dst, int c, mr_size_t size) {
    const mr_uint8_t align = sizeof(mr_uintptr_t) - 1;
    mr_uintptr_t pat;
    char *d;

#if defined(MR_USE_STRING_SAFE)
    /* Check parameter */
    if (!dst) {
        return MR_NULL;
    }
#endif /* defined(MR_USE_STRING_SAFE) */

    /* Make pattern(aligned) */
    pat = (mr_uint8_t)c;
    pat |= pat << 8;
    pat |= pat << 16;
    pat |= pat << 24;
#if defined(MR_USE_ARCH_64B)
    pat |= pat << 32;
    pat |= pat << 40;
    pat |= pat << 48;
    pat |= pat << 56;
#endif /* defined(MR_USE_ARCH_64B) */

    /* Copy unaligned bytes */
    for (d = dst; (size > 0) && ((mr_uintptr_t)d & align); size--) {
        (*d++) = (char)c;
    }

    /* Copy aligned bytes */
    while (size >= sizeof(mr_uintptr_t)) {
        *(mr_uintptr_t *)d = pat;
        d += sizeof(mr_uintptr_t);
        size -= sizeof(mr_uintptr_t);
    }

    /* Copy unaligned bytes */
    while (size--) {
        (*d++) = (char)c;
    }
    return dst;
}

void *mr_memchr(const void *s, int c, mr_size_t size) {
    const char *p;

#if defined(MR_USE_STRING_SAFE)
    /* Check parameter */
    if (!s) {
        return MR_NULL;
    }
#endif /* defined(MR_USE_STRING_SAFE) */

    /* Search bytes */
    for (p = s; size > 0; size--) {
        if (*p != c) {
            p++;
            continue;
        }
        return (void *)p;
    }
    return MR_NULL;
}

int mr_strcmp(const char *s1, const char *s2) {
#if defined(MR_USE_STRING_SAFE)
    /* Check parameter */
    if ((!s1) || (!s2)) {
        return 0;
    }
#endif /* defined(MR_USE_STRING_SAFE) */

    /* Compare bytes */
    while (*s1 == *s2) {
        if (*s1 != '\0') {
            s1++;
            s2++;
            continue;
        }
        return 0;
    }
    return *s1 - (*s2);
}

char *mr_strcpy(char *dst, const char *src) {
    char *d;

#if defined(MR_USE_STRING_SAFE)
    /* Check parameter */
    if ((!dst) || (!src)) {
        return MR_NULL;
    }
#endif /* defined(MR_USE_STRING_SAFE) */

    /* Copy bytes */
    for (d = dst; *src != '\0'; src++, dst++) {
        *dst = *src;
    }
    *dst = '\0';
    return d;
}

char *mr_strerror(int errnum) {
    char *error;

    switch (errnum) {
        case 0: {
            error = "Success";
            break;
        }
        case MR_EPERM: {
            error = "Operation not permitted";
            break;
        }
        case MR_ENOENT: {
            error = "No such file or directory";
            break;
        }
        case MR_EIO: {
            error = "I/O error";
            break;
        }
        case MR_EAGAIN: {
            error = "Resource temporarily unavailable";
            break;
        }
        case MR_ENOMEM: {
            error = "Out of memory";
            break;
        }
        case MR_EBUSY: {
            error = "Device or resource busy";
            break;
        }
        case MR_EEXIST: {
            error = "File exists";
            break;
        }
        case MR_EINVAL: {
            error = "Invalid argument";
            break;
        }
        case MR_ENOSYS: {
            error = "Function not implemented";
            break;
        }
        case MR_ETIMEDOUT: {
            error = "Connection timed out";
            break;
        }
        default: {
            error = "Unknown error";
        }
    }
    return error;
}

mr_size_t mr_strlen(const char *s) {
    mr_size_t len;

#if defined(MR_USE_STRING_SAFE)
    /* Check parameter */
    if (!s) {
        return 0;
    }
#endif /* defined(MR_USE_STRING_SAFE) */

    /* Count bytes */
    for (len = 0; *s != '\0'; s++) {
        len++;
    }
    return len;
}

mr_size_t mr_strnlen(const char *s, mr_size_t size) {
    mr_size_t len;

#if defined(MR_USE_STRING_SAFE)
    /* Check parameter */
    if (!s) {
        return 0;
    }
#endif /* defined(MR_USE_STRING_SAFE) */

    /* Count bytes */
    for (len = 0; (len < size) && (*s != '\0'); s++) {
        len++;
    }
    return len;
}

int mr_strncmp(const char *s1, const char *s2, mr_size_t size) {
    /* Compare bytes */
    return mr_memcmp(s1, s2, size);
}

char *mr_strncpy(char *dst, const char *src, mr_size_t size) {
    char *d;

    /* Copy bytes */
    d = mr_memccpy(dst, src, '\0', size);
    if (d > dst) {
        /* Fill with '\0' */
        mr_memset(d, '\0', size - (mr_size_t)(d - dst));
    }
    return dst;
}

char *mr_strchr(const char *s, int c) {
    /* Search bytes */
    return mr_memchr(s, c, mr_strlen(s) + 1);
}
#endif /* defined(MR_USE_STRING) */

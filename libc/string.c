/**
 * @copyright (c) 2024-2026, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <mrX/libc/mr_string.h>
#if defined(MR_USE_STRING)
#include <mrX/libc/mr_errno.h>

/* String word type */
typedef mr_uintptr_t MR_MAY_ALIAS       string_word_t;

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
        if ((mr_uint8_t)*s == (mr_uint8_t)c) {
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
        return (int)(mr_uint8_t)*s1 - (int)(mr_uint8_t)*s2;
    }
    return 0;
}

void *mr_memcpy(void *dst, const void *src, mr_size_t size) {
    const mr_uintptr_t align = sizeof(mr_uintptr_t) - 1U;
    const char *s;
    char *d;

#if defined(MR_USE_STRING_SAFE)
    /* Check parameter */
    if ((!dst) || (!src)) {
        return MR_NULL;
    }
#endif /* defined(MR_USE_STRING_SAFE) */

    /* Init copy pointers */
    d = dst;
    s = src;

    /* Check whether dst and src share the same alignment offset */
    if ((((mr_uintptr_t)dst ^ (mr_uintptr_t)src) & align) == 0U) {
        /* Copy leading unaligned bytes */
        while ((size > 0U) && (((mr_uintptr_t)d & align) != 0U)) {
            (*d++) = (*s++);
            size--;
        }

        /* Copy aligned machine words */
        for (; size >= sizeof(mr_uintptr_t); size -= sizeof(mr_uintptr_t)) {
            *(string_word_t *)d = *(const string_word_t *)s;
            d += sizeof(mr_uintptr_t);
            s += sizeof(mr_uintptr_t);
        }
    }

    /* Copy remaining bytes */
    while (size--) {
        (*d++) = (*s++);
    }
    return dst;
}

void *mr_memmove(void *dst, const void *src, mr_size_t size) {
    const mr_uintptr_t align = sizeof(mr_uintptr_t) - 1;
    mr_uintptr_t dst_addr, src_addr;
    const char *s;
    char *d;

#if defined(MR_USE_STRING_SAFE)
    /* Check parameter */
    if ((!dst) || (!src)) {
        return MR_NULL;
    }
#endif /* defined(MR_USE_STRING_SAFE) */

    /* Check empty or identical range */
    if ((size == 0) || (dst == src)) {
        return dst;
    }

    /* Init addresses and pointers */
    dst_addr = (mr_uintptr_t)dst;
    src_addr = (mr_uintptr_t)src;
    d = dst;
    s = src;

    /* Copy backward when dst overlaps the end of src */
    if ((dst_addr > src_addr) && ((dst_addr - src_addr) < size)) {
        d += size;
        s += size;

        /* Copy aligned machine words */
        if (((dst_addr ^ src_addr) & align) == 0) {
            while ((size > 0) && (((mr_uintptr_t)d & align) != 0)) {
                (*(--d)) = (*(--s));
                size--;
            }
            while (size >= sizeof(mr_uintptr_t)) {
                d -= sizeof(mr_uintptr_t);
                s -= sizeof(mr_uintptr_t);
                *(string_word_t *)d = *(const string_word_t *)s;
                size -= sizeof(mr_uintptr_t);
            }
        }

        /* Copy remaining bytes */
        while (size--) {
            (*(--d)) = (*(--s));
        }
    } else {
        /* Copy aligned machine words */
        if (((dst_addr ^ src_addr) & align) == 0) {
            while ((size > 0) && (((mr_uintptr_t)d & align) != 0)) {
                (*d++) = (*s++);
                size--;
            }
            while (size >= sizeof(mr_uintptr_t)) {
                *(string_word_t *)d = *(const string_word_t *)s;
                d += sizeof(mr_uintptr_t);
                s += sizeof(mr_uintptr_t);
                size -= sizeof(mr_uintptr_t);
            }
        }

        /* Copy remaining bytes */
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
        *(string_word_t *)d = pat;
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
        if ((mr_uint8_t)*p != (mr_uint8_t)c) {
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
    return (int)(mr_uint8_t)*s1 - (int)(mr_uint8_t)*s2;
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
    int code;

    /* Convert negative error number to positive */
    code = (errnum < 0) ? -errnum : errnum;
    switch (code) {
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
        case MR_EBADF: {
            error = "Bad file descriptor";
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
        case MR_EXDEV: {
            error = "Cross-device link";
            break;
        }
        case MR_ENODEV: {
            error = "No such device";
            break;
        }
        case MR_ENOTDIR: {
            error = "Not a directory";
            break;
        }
        case MR_EISDIR: {
            error = "Is a directory";
            break;
        }
        case MR_EINVAL: {
            error = "Invalid argument";
            break;
        }
        case MR_EMFILE: {
            error = "Too many open files";
            break;
        }
        case MR_ESPIPE: {
            error = "Illegal seek";
            break;
        }
        case MR_ERANGE: {
            error = "Result too large";
            break;
        }
        case MR_ENAMETOOLONG: {
            error = "File name too long";
            break;
        }
        case MR_ENOTEMPTY: {
            error = "Directory not empty";
            break;
        }
        case MR_ENOSYS: {
            error = "Function not implemented";
            break;
        }
        case MR_ELOOP: {
            error = "Too many levels of symbolic links";
            break;
        }
        case MR_ETIMEDOUT: {
            error = "Connection timed out";
            break;
        }
        case MR_ECANCELED: {
            error = "Operation canceled";
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
#if defined(MR_USE_STRING_SAFE)
    /* Check parameter */
    if ((!s1) || (!s2)) {
        return 0;
    }
#endif /* defined(MR_USE_STRING_SAFE) */

    /* Compare bytes */
    while (size > 0) {
        if ((mr_uint8_t)*s1 != (mr_uint8_t)*s2) {
            return (int)(mr_uint8_t)*s1 - (int)(mr_uint8_t)*s2;
        }
        if (*s1 == '\0') {
            return 0;
        }
        s1++;
        s2++;
        size--;
    }
    return 0;
}

char *mr_strncpy(char *dst, const char *src, mr_size_t size) {
    char *d;

    /* Copy bytes */
    d = mr_memccpy(dst, src, '\0', size);
    if (d != MR_NULL) {
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

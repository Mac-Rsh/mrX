/**
 * @copyright (c) 2024, MacRsh
 *
 * @license SPDX-License-Identifier: Apache-2.0
 *
 * @date 2024-09-06        MacRsh        First version
 */

#include <libc/mr_string.h>
#if (!defined(MR_USE_LIBC_STRING)) && (!defined(MR_USE_3PARTY_STRING))
#include <libc/mr_errno.h>

void *mr_memccpy(void *dst, const void *src, int c, mr_size_t size) {
    register const char *s;
    register char *d;

    for (d = dst, s = src; size > 0; size--) {
        (*d++) = *s;
        if (*s == c) {
            return (void *)d;
        }
        s++;
    }
    return dst;
}

int mr_memcmp(const void *m1, const void *m2, mr_size_t size) {
    register const char *s1;
    register const char *s2;

    for (s1 = m1, s2 = m2; size > 0; size--) {
        if (*s1 != *s2) {
            return *s1 - *s2;
        }
        s1++;
        s2++;
    }
    return 0;
}

void *mr_memcpy(void *dst, const void *src, mr_size_t size) {
    register const char *s;
    register char *d;

    for (d = dst, s = src; size > 0; size--) {
        (*d++) = (*s++);
    }
    return dst;
}

void *mr_memset(void *dst, int c, mr_size_t size) {
    register char *d;

    for (d = dst; size > 0; size--) {
        (*d++) = (char)c;
    }
    return dst;
}

void *mr_memchr(const void *s, int c, mr_size_t size) {
    register const char *p;

    for (p = s; size > 0; size--) {
        if (*p == c) {
            return (void *)p;
        }
        p++;
    }
    return MR_NULL;
}

int mr_strcmp(const char *s1, const char *s2) {
    for (; *s1 == *s2; s1++, s2++) {
        if (*s1 == '\0') {
            return 0;
        }
    }
    return *s1 - *s2;
}

char *mr_strcpy(char *dst, const char *src) {
    register char *d;

    for (d = dst; (*d++ = *src++) != '\0';) {
    }
    return dst;
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

    if (!s) {
        return 0;
    }
    for (len = 0; *s != '\0'; s++) {
        len++;
    }
    return len;
}

mr_size_t mr_strnlen(const char *s, mr_size_t size) {
    mr_size_t len;

    if (!s) {
        return 0;
    }
    for (len = 0; (len < size) && (*s != '\0'); s++) {
        len++;
    }
    return len;
}

int mr_strncmp(const char *s1, const char *s2, mr_size_t size) {
    return mr_memcmp(s1, s2, size);
}

char *mr_strncpy(char *dst, const char *src, mr_size_t size) {
    mr_memccpy(dst, src, '\0', size);
    return dst;
}

char *mr_strchr(const char *s, int c) {
    return mr_memchr(s, c, mr_strlen(s) + 1);
}
#endif /* (!defined(MR_USE_LIBC_STRING)) && (!defined(MR_USE_3PARTY_STRING)) */
